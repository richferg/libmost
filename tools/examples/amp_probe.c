/*
 * amp_probe - interrogate an AudioAmplifier (or any sink FBlock) to discover
 * the sink numbers and connection parameters you need before calling audio_tx.
 *
 * Sends the GenFB sink/stream introspection Gets and prints the decoded
 * responses:
 *   SyncDataInfo (0x116).Get            -> SourceCount, SinkCount
 *   SinkInfo     (0x110).Get(SinkNr)    -> DataType + current channels per sink
 *   SinkName     (0x114).Get(SinkNr)    -> human-readable sink name (string)
 *   Mute         (0x113).Get(SinkNr)    -> current mute state of each sink
 *
 * SyncDataInfo gives the sink COUNT; sink numbers are 0..SinkCount-1. Use the
 * SinkNr whose SinkName/SinkInfo identifies the main audio input as the sink_nr
 * argument to audio_tx.
 *
 * Grounding: GeneralFBlock Rev2.5.1 sections 2.1.26 (SinkInfo), 2.1.29 (Mute),
 * 2.1.30 (SinkName), 2.1.32 (SyncDataInfo). Responses printed raw + decoded.
 *
 * Read-only: only sends Get requests, writes no registers, allocates no
 * channels. Safe to run against the live ring (other than generating a few
 * control messages to the amplifier).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include "most.h"

/* mostd's collision-avoiding node address; chip can revert to 0x0000 when mostd
 * stops, and replies to a 0x0000 source are unroutable - so set one here. */
#define TOOL_NODE_ADDR 0x0110

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Control frame layout (21 bytes, == TCMB reg 0xC0+N for normal messages). */
#define F_PRIORITY  0
#define F_MSGTYPE   1
#define F_ADDR_HI   2   /* target on TX, source on RX */
#define F_ADDR_LO   3
#define F_FBLOCK    4
#define F_INST      5
#define F_FKTID_HI  6
#define F_FKTID_LOOP 7
#define F_TEL       8
#define F_DATA      9

#define FB_AUDIOAMP        0x22
#define FKT_SINKINFO       0x110
#define FKT_SINKNAME       0x114
#define FKT_MUTE           0x113
#define FKT_SYNCDATAINFO   0x116
#define OP_GET             0x01
#define OP_STATUS          0x0C
#define OP_ERROR           0x0F

#define TX_BUSY_RETRIES 8
#define TX_BUSY_WAIT_US 3000

static struct most_handle *g_h;

/* Drain pending control-RX frames. Control replies arrive on the frame path
 * (most_read), not the event queue; an unread frame backs up the OS8104
 * control buffer and stalls further TX. */
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

static int send_get(uint16_t addr, uint8_t fb, uint8_t inst, uint16_t fkt,
		    const uint8_t *payload, uint8_t plen)
{
	uint8_t frm[MOST_FRAME_LEN] = {0};
	int attempt;

	if (plen > 15) return -1;
	flush_events();   /* keep the RX buffers drained before each request */
	frm[F_ADDR_HI]   = (uint8_t)(addr >> 8);
	frm[F_ADDR_LO]   = (uint8_t)(addr & 0xff);
	frm[F_FBLOCK]    = fb;
	frm[F_INST]      = inst;
	frm[F_FKTID_HI]  = (uint8_t)(fkt >> 4);
	frm[F_FKTID_LOOP] = (uint8_t)(((fkt & 0x0F) << 4) | (OP_GET & 0x0F));
	frm[F_TEL]       = (uint8_t)(plen & 0x0F);
	if (plen)
		memcpy(&frm[F_DATA], payload, plen);

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

static const char *fkt_name(uint16_t fkt)
{
	switch (fkt) {
	case FKT_SYNCDATAINFO: return "SyncDataInfo";
	case FKT_SINKINFO:     return "SinkInfo";
	case FKT_SINKNAME:     return "SinkName";
	case FKT_MUTE:         return "Mute";
	default:               return "?";
	}
}

/* Drain control RX for `ms`, decode any AudioAmplifier responses. */
static void drain(int ms)
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
		if (getenv("AMP_PROBE_VERBOSE")) {
			printf("    [frame]");
			for (i = 0; i < MOST_FRAME_LEN; i++)
				printf(" %02X", f[i]);
			printf("\n");
		}

		src  = ((uint16_t)f[F_ADDR_HI] << 8) | f[F_ADDR_LO];
		fb   = f[F_FBLOCK];
		inst = f[F_INST];
		fkt  = ((uint16_t)f[F_FKTID_HI] << 4) | (f[F_FKTID_LOOP] >> 4);
		op   = f[F_FKTID_LOOP] & 0x0F;
		tlen = f[F_TEL] & 0x0F;
		if (tlen > MOST_FRAME_LEN - F_DATA)
			tlen = (uint8_t)(MOST_FRAME_LEN - F_DATA);
		/* only decode replies from the amp we are probing */
		if (fb != FB_AUDIOAMP)
			continue;

		printf("  src=0x%04X fb=0x%02X inst=0x%02X %s(0x%03X) op=0x%X%s  payload[%u]=",
		       src, fb, inst, fkt_name(fkt), fkt, op,
		       op == OP_ERROR ? " ERROR" : (op == OP_STATUS ? " Status" : ""), tlen);
		for (i = 0; i < tlen; i++)
			printf("%02X ", f[F_DATA + i]);

		/* ASCII view (useful for SinkName). */
		printf(" |");
		for (i = 0; i < tlen; i++) {
			uint8_t c = f[F_DATA + i];
			putchar(isprint(c) ? c : '.');
		}
		printf("|\n");

		/* SyncDataInfo.Status = [SourceCount][SinkCount] (GenFB 2.1.32). */
		if (fkt == FKT_SYNCDATAINFO && op == OP_STATUS && tlen >= 2)
			printf("    [decode] SourceCount=%u SinkCount=%u -> probe sinks 0..%u\n",
			       f[F_DATA], f[F_DATA + 1], f[F_DATA + 1] ? f[F_DATA + 1] - 1 : 0);

		/* SinkInfo.Status = [SinkNr][DataType][DataDescription] (GenFB 2.1.26).
		 * For PCM (0x00): Resolution, AudioChannels, SinkDelay, ChannelList. */
		if (fkt == FKT_SINKINFO && op == OP_STATUS && tlen >= 2) {
			uint8_t dt = f[F_DATA + 1];
			const char *dts = dt == 0x00 ? "PCM" : dt == 0x01 ? "CDROM" :
					  dt == 0x02 ? "SPDIF" : dt == 0xFF ? "Unknown" : "MPEG/other";
			printf("    [decode] sink=%u DataType=0x%02X(%s)", f[F_DATA], dt, dts);
			if (dt == 0x00 && tlen >= 5) {
				printf(" Resolution=%uB AudioChannels=%u SinkDelay=%u chans:",
				       f[F_DATA + 2], f[F_DATA + 3], f[F_DATA + 4]);
				for (i = 5; i < tlen; i++)
					printf(" 0x%02X", f[F_DATA + i]);
			} else if (tlen >= 3) {
				printf(" desc:");
				for (i = 2; i < tlen; i++)
					printf(" %02X", f[F_DATA + i]);
			}
			printf("\n");
		}
	}
}

static void usage(const char *p)
{
	fprintf(stderr,
	  "usage: %s <dev> <amp_addr_hex> [amp_inst=0xFF] [max_sink=3]\n"
	  "  dev          /dev/mostX suffix, e.g. 0\n"
	  "  amp_addr_hex AudioAmplifier logical address, e.g. 0x0147\n"
	  "  amp_inst     InstID (0xFF wildcard default)\n"
	  "  max_sink     probe SinkInfo/SinkName/Mute for sink 0..max_sink\n", p);
}

int main(int argc, char **argv)
{
	const char *dev;
	uint16_t amp_addr;
	uint8_t amp_inst = 0xFF;
	unsigned max_sink = 3, s;

	if (argc < 3) { usage(argv[0]); return 2; }
	dev = argv[1];
	amp_addr = (uint16_t)strtoul(argv[2], NULL, 0);
	if (argc >= 4) amp_inst = (uint8_t)strtoul(argv[3], NULL, 0);
	if (argc >= 5) max_sink = (unsigned)strtoul(argv[4], NULL, 0);

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
		printf("amp_probe: amp=0x%04X inst=0x%02X (our node addr=0x%04X, "
		       "flushed %d stale events)\n", amp_addr, amp_inst, cur, flush_events());
	}

	printf("[SyncDataInfo.Get] (source/sink counts)\n");
	if (send_get(amp_addr, FB_AUDIOAMP, amp_inst, FKT_SYNCDATAINFO, NULL, 0) < 0)
		perror("  send");
	drain(800);

	for (s = 0; s <= max_sink; s++) {
		uint8_t sn = (uint8_t)s;
		printf("[sink %u] SinkInfo / SinkName / Mute .Get\n", s);
		if (send_get(amp_addr, FB_AUDIOAMP, amp_inst, FKT_SINKINFO, &sn, 1) < 0)
			perror("  SinkInfo send");
		drain(400);
		if (send_get(amp_addr, FB_AUDIOAMP, amp_inst, FKT_SINKNAME, &sn, 1) < 0)
			perror("  SinkName send");
		drain(400);
		if (send_get(amp_addr, FB_AUDIOAMP, amp_inst, FKT_MUTE, &sn, 1) < 0)
			perror("  Mute send");
		drain(400);
	}

	printf("Done. Use a reported SinkNr as the sink_nr arg to audio_tx.\n");
	most_close(g_h);
	return 0;
}
