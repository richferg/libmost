/*
 * audio_tx - end-to-end "send my own audio over MOST" for a MOST25 ring.
 *
 * Chains the whole source-side sequence into one tool:
 *
 *   1. Resource Allocate (Type 03h) -> ask the timing-master for N synchronous
 *      channels. Returns the granted physical channel bytes P0..P(N-1) and the
 *      Connection Label (P0).  [see resource_alloc.c for the bare mechanism]
 *   2. Local OS8104 setup -> leave bXCR untouched, enable the serial Source Port, and
 *      load the MOST Routing Table so the source-port input bytes are inserted
 *      into the granted network channel bytes.
 *   3. Connect -> send AudioAmplifier(0x22).Connect(0x111).StartResult
 *      (SinkNr, SrcDelay, ChannelList) directly to the amplifier's sink. This
 *      is exactly the message the ConnectionMaster sends the sink (GenFB
 *      Rev2.5.1 2.1.27) - the amp does not care that we sent it instead of CM.
 *   4. Unmute -> Mute(0x113).SetGet(SinkNr, Status=0) (GenFB 2.1.29).
 *   5. Drain control RX for a moment and print the amp's Result / Error.
 *
 * Why this works without being a registered source FBlock: the sink Connect
 * method takes an explicit ChannelList - the literal network channel numbers
 * the sink should read. We own those channels (allocated via Resource Allocate,
 * audio routed in via the MRT), so we hand the amp that exact list and it reads
 * our audio. Channels need not be contiguous. The CM's Allocate-on-source step
 * is replaced by our own Resource Allocate + MRT load.
 *
 * Grounding:
 *   OS8104A datasheet 13.5.2.4 (Resource Allocate Type 03h, TCMB 0xC0-0xD4),
 *   sect. 6/7/12 (bXCR bypass, source port, MOST Routing Table);
 *   GeneralFBlock Rev2.5.1 2.1.27 Connect(0x111).StartResult(SinkNr, SrcDelay,
 *   ChannelList), 2.1.29 Mute(0x113).SetGet(SinkNr, Status);
 *   AudioAmplifier FBlockID 0x22 receives audio over standard sinks.
 *   Note: GenFB uses the non-Ack method model (StartResult 0x02 / Result 0x0C).
 *
 * CAVEATS (resolve on the car):
 *   - amp logical address + SinkNr are not auto-discovered: pass them in. Find
 *     the amp address from the addrbook/registry log lines; probe SinkNr with
 *     amp_probe (SyncDataInfo 0x116 / SinkInfo 0x110 / SinkName 0x114).
 *   - the head unit's own ConnectionMaster does not know about this connection
 *     and may tear it down, or the amp may already be playing head-unit audio
 *     on that sink. Test with the system idle first.
 *   - SrcDelay is sent as our bNPR node position; a real source would report
 *     its delay from Allocate.Result - verify if audio is skewed.
 *
 * WARNING: writes INIC hardware registers, makes the node an active ring
 * participant, consumes synchronous bandwidth, and sends control messages to
 * the amplifier. Use on a ring you own / are authorized to test.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "most.h"

/* Default node address for the tool when the ring left us unconfigured. mostd
 * uses this collision-avoiding value; the chip address can revert to 0x0000
 * when mostd stops, and replies to a 0x0000 source are unroutable. */
#define TOOL_NODE_ADDR 0x0110

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static struct most_handle *g_h;

/* Drain pending control-RX frames. Control replies arrive on the frame path
 * (most_read), not the event queue; an unread reply frame backs up the OS8104
 * control buffer and stalls the next TX (the Mute EBUSY). Call before each
 * send. Returns count drained. */
static int flush_events(void)
{
	uint8_t f[MOST_FRAME_LEN];
	int n = 0;
	while (most_poll(g_h, 0) > 0) {
		if (most_read(g_h, f) != 0)
			break;
		n++;
	}
	return n;
}

/* ---- OS8104A registers (source-port + bypass) ----------------------------*/
#define REG_BSDC1   0x82
#define REG_BSDC2   0x8C
#define REG_BSDC3   0x8D
#define REG_BNPR    0x87   /* node position register (used as SrcDelay) */
#define BSDC1_I2S           0xD3   /* EDG|DEL|SCK_OUTPUT|TRANS_DIS|UNMUTE (I2S, SocketMost) */
#define BSDC2_SCK_32F       0x40   /* SPR[2:0]=010 (bits 7:5) -> 32Fs */
#define BSDC3_ON            0x00   /* source ports enabled + unmuted */

/*
 * Source-port input Address References written into the granted network
 * channel MRT locations. Values + sequence taken from the working SocketMost
 * OS8104A driver (rhysmorgan134/SocketMost) for stereo source streaming.
 * NOTE: we deliberately do NOT touch bXCR. The driver init already leaves the
 * node active (ALL_BYPASS_DIS, bit1 set); clearing it puts the node into
 * all-bypass and kills its ring participation (control RX dies).
 */
static const uint8_t SRC_MRA[4] = { 0x49, 0x59, 0x69, 0x79 };

/* ---- Resource Allocate (Type 03h) ---------------------------------------- */
#define RA_OFS_PRIORITY  0
#define RA_OFS_TYPE      1
#define RA_OFS_TGT_HI    2
#define RA_OFS_TGT_LO    3
#define RA_OFS_REQUEST   5
#define RA_PRIORITY      0x01
#define RA_TYPE_ALLOCATE 0x03
#define TM_ADDR_HI       0x04
#define TM_ADDR_LO       0x00
#define REG_ANSWER1      0xC7
#define REG_ANSWER2      0xC8
#define REG_P0           0xC9
#define RA_MAX_CHANNELS  8
#define ANSWER1_GRANT    0x01
#define CHAN_UNUSABLE    0xFF

/* ---- Control frame layout (21 bytes == TCMB reg 0xC0+N for normal msgs) ---*/
#define F_PRIORITY  0
#define F_MSGTYPE   1
#define F_TGT_HI    2
#define F_TGT_LO    3
#define F_FBLOCK    4
#define F_INST      5
#define F_FKTID_HI  6
#define F_FKTID_LOOP 7
#define F_TEL       8
#define F_DATA      9

/* FBlock / FktID / OPType */
#define FB_AUDIOAMP   0x22
#define FKT_SINKINFO  0x110
#define FKT_CONNECT   0x111
#define FKT_DISCONNECT 0x112
#define FKT_MUTE      0x113
#define OP_GET        0x01
/* GenFB Rev2.5.1 uses the non-Ack method model: invoke methods with StartResult
 * (0x02), result returns as Result (0x0C). Properties set via SetGet (0x02). */
#define OP_SETGET      0x02   /* method StartResult / property SetGet */
#define OP_RESULT      0x0C   /* method Result / property Status */
#define OP_ERROR       0x0F

#define POLL_TRIES 100
#define POLL_WAIT_US 2000
#define TX_BUSY_RETRIES 8
#define TX_BUSY_WAIT_US 3000

static int rd(uint8_t reg, uint8_t *val) { return most_reg_read(g_h, reg, val, 1); }
static int wr(uint8_t reg, uint8_t val)  { return most_reg_write(g_h, reg, &val, 1); }

static int send_frame(const uint8_t *frm)
{
	int attempt;
	for (attempt = 0; attempt < TX_BUSY_RETRIES; attempt++) {
		if (most_write(g_h, frm) == 0)
			return 0;
		if (errno != EBUSY)
			return -1;
		usleep(TX_BUSY_WAIT_US);
	}
	errno = EBUSY;
	return -1;
}

/* Step 1: Resource Allocate. Fills chans[0..num-1], returns count granted or <0. */
static int resource_allocate(unsigned num, uint8_t chans[RA_MAX_CHANNELS],
			     uint8_t *answer1, uint8_t *answer2)
{
	uint8_t frm[MOST_FRAME_LEN] = {0};
	int i, n_granted = 0;

	frm[RA_OFS_PRIORITY] = RA_PRIORITY;
	frm[RA_OFS_TYPE]     = RA_TYPE_ALLOCATE;
	frm[RA_OFS_TGT_HI]   = TM_ADDR_HI;
	frm[RA_OFS_TGT_LO]   = TM_ADDR_LO;
	frm[RA_OFS_REQUEST]  = (uint8_t)num;

	if (send_frame(frm) < 0) {
		perror("resource allocate write");
		return -1;
	}

	*answer1 = 0;
	for (i = 0; i < POLL_TRIES; i++) {
		if (rd(REG_ANSWER1, answer1) < 0) {
			perror("read Answer1");
			return -1;
		}
		if (*answer1 != 0x00)
			break;
		usleep(POLL_WAIT_US);
	}
	if (*answer1 == 0x00) {
		fprintf(stderr, "timed out waiting for timing-master allocate answer "
			"(is the head unit awake?)\n");
		return -1;
	}
	rd(REG_ANSWER2, answer2);
	for (i = 0; i < RA_MAX_CHANNELS; i++) {
		rd((uint8_t)(REG_P0 + i), &chans[i]);
		if (chans[i] != CHAN_UNUSABLE)
			n_granted++;
	}
	if (*answer1 != ANSWER1_GRANT)
		return 0;
	return n_granted;
}

/* Route the serial source-port inputs onto the granted network channels and
 * enable + unmute the source port. bXCR is left untouched (see SRC_MRA note). */
static int local_audio_setup(const uint8_t *chans, int n)
{
	int i;

	for (i = 0; i < n && i < 4; i++) {
		if (wr(chans[i], SRC_MRA[i]) < 0) {
			fprintf(stderr, "write MRT[0x%02X] failed\n", chans[i]);
			return -1;
		}
	}

	wr(REG_BSDC1, BSDC1_I2S);                  /* I2S source-port format */
	wr(REG_BSDC2, BSDC2_SCK_32F);              /* source-port bit clock 32Fs */
	wr(REG_BSDC3, BSDC3_ON);                   /* source ports enabled + unmuted */
	return 0;
}

/* Build + send AudioAmplifier(0x22).<fkt>.<op> with a payload. */
static int send_amp(uint16_t amp_addr, uint8_t amp_inst, uint16_t fkt, uint8_t op,
		    const uint8_t *payload, uint8_t plen)
{
	uint8_t frm[MOST_FRAME_LEN] = {0};

	if (plen > 15) return -1;
	frm[F_PRIORITY]  = 0x00;
	frm[F_MSGTYPE]   = 0x00;
	frm[F_TGT_HI]    = (uint8_t)(amp_addr >> 8);
	frm[F_TGT_LO]    = (uint8_t)(amp_addr & 0xff);
	frm[F_FBLOCK]    = FB_AUDIOAMP;
	frm[F_INST]      = amp_inst;
	frm[F_FKTID_HI]  = (uint8_t)(fkt >> 4);
	frm[F_FKTID_LOOP] = (uint8_t)(((fkt & 0x0F) << 4) | (op & 0x0F));
	frm[F_TEL]       = (uint8_t)(plen & 0x0F);     /* telid 0 (single), tellen=plen */
	if (plen)
		memcpy(&frm[F_DATA], payload, plen);
	return send_frame(frm);
}

/* Step 5: print any control RX (the amp's ResultAck/Error) for a short window. */
static void drain_responses(int ms)
{
	long deadline = now_ms() + ms;
	long remain;

	while ((remain = deadline - now_ms()) > 0) {
		uint8_t f[MOST_FRAME_LEN];
		uint16_t src, fkt;
		uint8_t fb, inst, op, tlen;
		int i;

		if (most_poll(g_h, (int)remain) <= 0)
			break;
		if (most_read(g_h, f) != 0)
			break;
		fb = f[F_FBLOCK];
		if (fb != FB_AUDIOAMP)        /* ignore unrelated ring traffic */
			continue;
		src  = ((uint16_t)f[F_TGT_HI] << 8) | f[F_TGT_LO];
		inst = f[F_INST];
		fkt  = ((uint16_t)f[F_FKTID_HI] << 4) | (f[F_FKTID_LOOP] >> 4);
		op   = f[F_FKTID_LOOP] & 0x0F;
		tlen = f[F_TEL] & 0x0F;
		if (tlen > MOST_FRAME_LEN - F_DATA)
			tlen = (uint8_t)(MOST_FRAME_LEN - F_DATA);
		printf("  rx src=0x%04X fb=0x%02X inst=0x%02X fkt=0x%03X op=0x%X%s data=",
		       src, fb, inst, fkt, op,
		       op == OP_RESULT ? " (Result/Status)" :
		       op == OP_ERROR ? " (ERROR)" : "");
		for (i = 0; i < tlen; i++)
			printf("%02X ", f[F_DATA + i]);
		printf("\n");
	}
}

static void usage(const char *p)
{
	fprintf(stderr,
	  "usage: %s <dev> <amp_addr_hex> [sink_nr=0] [num_channels=4] [amp_inst=0xFF] [ack=0]\n"
	  "  dev          /dev/mostX suffix, e.g. 0\n"
	  "  amp_addr_hex AudioAmplifier logical address, e.g. 0x0147 (from the mostd addrbook log)\n"
	  "  sink_nr      amp sink number (probe with amp_probe if 0 fails)\n"
	  "  num_channels synchronous bytes/frame to allocate; 4 = stereo 16-bit\n"
	  "  amp_inst     AudioAmplifier InstID (0xFF wildcard default)\n"
	  "  ack          method model: 0=non-Ack StartResult(0x02) [GenFB default],\n"
	  "               1=Ack StartResultAck(0x06)+SenderHandle. Amp uses one or the\n"
	  "               other (never mixed); try the other if Connect is ignored.\n", p);
}

int main(int argc, char **argv)
{
	const char *dev;
	uint16_t amp_addr;
	uint8_t sink_nr = 0x00, amp_inst = 0xFF;
	unsigned num = 4, ack = 0;
	uint8_t chans[RA_MAX_CHANNELS] = {0}, answer1 = 0, answer2 = 0;
	uint8_t cl, payload[4 + RA_MAX_CHANNELS];
	uint8_t op, plen, npos = 0;
	int n, i;

	if (argc < 3) { usage(argv[0]); return 2; }
	dev = argv[1];
	amp_addr = (uint16_t)strtoul(argv[2], NULL, 0);
	if (argc >= 4) sink_nr = (uint8_t)strtoul(argv[3], NULL, 0);
	if (argc >= 5) num = (unsigned)strtoul(argv[4], NULL, 0);
	if (argc >= 6) amp_inst = (uint8_t)strtoul(argv[5], NULL, 0);
	if (argc >= 7) ack = (unsigned)strtoul(argv[6], NULL, 0);
	if (num < 1 || num > RA_MAX_CHANNELS) {
		fprintf(stderr, "num_channels must be 1..%d\n", RA_MAX_CHANNELS);
		return 2;
	}

	g_h = most_open(dev);
	if (!g_h) { perror("most_open"); return 1; }

	/* Ensure a routable node address so the amp's replies reach us. */
	{
		uint16_t cur = 0;
		most_get_node_addr(g_h, &cur);
		if (cur == 0x0000 || cur == 0xFFFF) {
			if (most_set_node_addr(g_h, TOOL_NODE_ADDR) == 0)
				cur = TOOL_NODE_ADDR;
		}
		printf("[0] node address = 0x%04X, flushed %d stale events\n",
		       cur, flush_events());
	}

	/* 1. allocate */
	printf("[1] Resource Allocate: requesting %u channel(s) from timing-master\n", num);
	n = resource_allocate(num, chans, &answer1, &answer2);
	if (n < 0) { most_close(g_h); return 1; }
	printf("    Answer1=0x%02X (%s) free=0x%02X  channels:", answer1,
	       answer1 == ANSWER1_GRANT ? "GRANT" : "NOT GRANTED", answer2);
	for (i = 0; i < RA_MAX_CHANNELS; i++)
		printf(chans[i] == CHAN_UNUSABLE ? " --" : " 0x%02X", chans[i]);
	printf("\n");
	if (answer1 != ANSWER1_GRANT || n < (int)num) {
		fprintf(stderr, "    allocation failed/short, aborting.\n");
		most_close(g_h);
		return 1;
	}
	cl = chans[0];   /* P0 == OS8104 connection label, used only for de-allocate */

	/* 2. Route the source port onto the granted channels (MRT + source port).
	 *    bXCR is left untouched, so control RX stays alive. Order matches the
	 *    working SocketMost driver: set up the source side before connecting. */
	printf("[2] Local source routing: MRT(0x49/59/69/79) + source port (32Fs)\n");
	if (local_audio_setup(chans, num) < 0) { most_close(g_h); return 1; }

	/* SrcDelay value: SocketMost passes the node position (bNPR). */
	rd(REG_BNPR, &npos);

	/* 3. DisConnect(0x112).StartResult(SinkNr) to clear any existing connection
	 *    on the amp sink first (SocketMost does this before Connect). */
	printf("[3] DisConnect(0x112) sink=%u\n", sink_nr);
	{
		uint8_t d = sink_nr;
		flush_events();
		if (send_amp(amp_addr, amp_inst, FKT_DISCONNECT, OP_SETGET, &d, 1) < 0)
			perror("    DisConnect send");
		drain_responses(300);
	}

	/* 4. Connect(0x111).StartResult(SinkNr, SrcDelay, ChannelList), op 0x02.
	 *    data = [SinkNr, nodePosition(SrcDelay), chan0..chanN-1]. SocketMost
	 *    sends this fire-and-forget (no reply awaited). */
	printf("[4] AudioAmplifier(0x22) Connect.%s: sink=%u srcdelay=0x%02X channels[%d]:",
	       ack ? "StartResultAck" : "StartResult", sink_nr, npos, n);
	for (i = 0; i < (int)num; i++)
		printf(" 0x%02X", chans[i]);
	printf("\n");
	plen = 0;
	if (ack) {
		op = 0x06;                       /* StartResultAck */
		payload[plen++] = 0x00;          /* SenderHandle hi */
		payload[plen++] = 0x01;          /* SenderHandle lo */
	} else {
		op = OP_SETGET;                  /* StartResult (0x02) */
	}
	payload[plen++] = sink_nr;               /* SinkNr  */
	payload[plen++] = npos;                  /* SrcDelay = node position */
	for (i = 0; i < (int)num; i++)
		payload[plen++] = chans[i];      /* ChannelList */
	flush_events();
	if (send_amp(amp_addr, amp_inst, FKT_CONNECT, op, payload, plen) < 0) {
		perror("    Connect send");
		most_close(g_h);
		return 1;
	}
	drain_responses(800);

	/* 5. unmute the amp sink: Mute(0x113).SetGet(SinkNr, 0). Not in SocketMost
	 *    (it unmutes its own source) but the probe showed this sink muted. */
	printf("[5] Mute.SetGet(sink=%u, off)\n", sink_nr);
	{
		uint8_t mp[2] = { sink_nr, 0x00 };
		flush_events();
		if (send_amp(amp_addr, amp_inst, FKT_MUTE, OP_SETGET, mp, 2) < 0)
			perror("    Mute send");
		drain_responses(400);
	}

	/* 6. verify: re-read SinkInfo - ChannelList should now be our channels
	 *    instead of FF FF FF FF (unconnected). */
	printf("[6] verify SinkInfo.Get(sink=%u) - ChannelList should be our channels:\n",
	       sink_nr);
	{
		uint8_t sn = sink_nr;
		flush_events();
		if (send_amp(amp_addr, amp_inst, FKT_SINKINFO, OP_GET, &sn, 1) == 0)
			drain_responses(600);
	}

	printf("Done. If silent: verify amp_addr/sink_nr, check the head unit isn't "
	       "overriding the connection, and scope the source bytes on the granted "
	       "channels. De-allocate (Type 04h) with label 0x%02X when finished.\n", cl);
	most_close(g_h);
	return 0;
}
