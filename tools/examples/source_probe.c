/*
 * source_probe - interrogate an audio SOURCE FBlock (default AmFmTuner 0x40)
 * for its source-connection state, to learn the exact wire shapes a WORKING
 * source reports - so the SDARS emulator (FBlock 0x44) can mirror them.
 *
 * Sniffing the live bus is not available here; this actively GETs the source
 * properties instead. The BMW/GenFB audio-source FktID catalog is shared across
 * source FBlocks, so 0x40 answers the same IDs the firmware proved for 0x44:
 *
 *   SourceInfo/Allocate (0x101).Get(SrcNr)  -> 01 00 02 02 <val> <ch0..ch3>
 *        <val>=0xFF inactive; <ch0..3>=FF FF FF FF unallocated, else granted.
 *   SourceActivity      (0x103).Get(SrcNr)  -> 01            (inactive, Len 1)
 *                                           -> 01 01         (active,   Len 2)
 *   SyncDataInfo        (0x116).Get(01 sub) -> tied to SourceActivity report.
 *
 * Grounding: sdars_source_funcs_resolved.md (decompile notes outside this repo) (firmware-exact for 0x44).
 *
 * Procedure for finding the SAT gate: run this against 0x40 while FM is the
 * ACTIVE source, then again while SAT is selected (tuner idle). The delta in
 * SourceActivity / SourceInfo <val>+channels is the active-source signature the
 * ConnectionMaster sets - i.e. what 0x44 must report to look connected.
 *
 * Read-only: sends Get requests only; writes no registers, allocates nothing.
 * Safe on the live ring (other than a few control messages to the source).
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

/* Routable node addr so the source's replies reach us (chip reverts to 0x0000
 * when mostd stops; replies to 0x0000 are unroutable). */
#define TOOL_NODE_ADDR 0x0110

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Control frame layout (21 bytes, == TCMB reg 0xC0+N for normal messages). */
#define F_PRIORITY   0
#define F_MSGTYPE    1
#define F_ADDR_HI    2   /* target on TX, source on RX */
#define F_ADDR_LO    3
#define F_FBLOCK     4
#define F_INST       5
#define F_FKTID_HI   6
#define F_FKTID_LOOP 7
#define F_TEL        8
#define F_DATA       9

#define FB_AMFMTUNER       0x40
#define FKT_SOURCEINFO     0x101
#define FKT_SOURCEACTIVITY 0x103
#define FKT_SYNCDATAINFO   0x116
#define OP_GET             0x01
#define OP_SETGET          0x02
#define OP_STATUS          0x0C
#define OP_ERROR           0x0F

/* NetBlock FBlockIDs: ask a node which FBlocks it hosts (firmware-proven on this
 * ring; CentralRegistry 0xA01 errors here so per-node FBlockIDs is the path). */
#define NETBLOCK_FBLOCK    0x01
#define FKT_FBLOCKIDS      0x000

#define TX_BUSY_RETRIES 8
#define TX_BUSY_WAIT_US 3000

static struct most_handle *g_h;
static uint8_t g_fb = FB_AMFMTUNER;   /* FBlock we are probing / decoding */

/* Drain pending control-RX frames so the OS8104 control buffer does not back up
 * and stall further TX. */
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

static int send_op(uint16_t addr, uint8_t fb, uint8_t inst, uint16_t fkt,
		   uint8_t op, const uint8_t *payload, uint8_t plen)
{
	uint8_t frm[MOST_FRAME_LEN] = {0};
	int attempt;

	if (plen > 15) return -1;
	flush_events();
	frm[F_ADDR_HI]    = (uint8_t)(addr >> 8);
	frm[F_ADDR_LO]    = (uint8_t)(addr & 0xff);
	frm[F_FBLOCK]     = fb;
	frm[F_INST]       = inst;
	frm[F_FKTID_HI]   = (uint8_t)(fkt >> 4);
	frm[F_FKTID_LOOP] = (uint8_t)(((fkt & 0x0F) << 4) | (op & 0x0F));
	frm[F_TEL]        = (uint8_t)(plen & 0x0F);
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

static int send_get(uint16_t addr, uint8_t fb, uint8_t inst, uint16_t fkt,
		    const uint8_t *payload, uint8_t plen)
{
	return send_op(addr, fb, inst, fkt, OP_GET, payload, plen);
}

static const char *fkt_name(uint16_t fkt)
{
	switch (fkt) {
	case FKT_SOURCEINFO:     return "SourceInfo";
	case FKT_SOURCEACTIVITY: return "SourceActivity";
	case FKT_SYNCDATAINFO:   return "SyncDataInfo";
	default:                 return "?";
	}
}

/* Drain control RX for `ms`, decode any responses from the probed FBlock. */
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
		if (getenv("SOURCE_PROBE_VERBOSE")) {
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
		if (fb != g_fb)
			continue;

		printf("  src=0x%04X fb=0x%02X inst=0x%02X %s(0x%03X) op=0x%X%s  payload[%u]=",
		       src, fb, inst, fkt_name(fkt), fkt, op,
		       op == OP_ERROR ? " ERROR" : (op == OP_STATUS ? " Status" : ""), tlen);
		for (i = 0; i < tlen; i++)
			printf("%02X ", f[F_DATA + i]);
		printf("\n");

		if (op == OP_ERROR) {
			if (tlen >= 1) {
				uint8_t ec = f[F_DATA];
				const char *es =
				  ec == 0x01 ? "FBlockID not available" :
				  ec == 0x02 ? "InstID not available" :
				  ec == 0x03 ? "FktID not available" :
				  ec == 0x04 ? "OPType not available" :
				  ec == 0x05 ? "length wrong" :
				  ec == 0x06 ? "parameter wrong" :
				  ec == 0x07 ? "parameter not available" : "?";
				printf("    [decode] error code 0x%02X (%s)%s\n", ec, es,
				       (ec == 0x04 && tlen >= 2) ? " for the sent OPType" : "");
			}
			continue;
		}

		/* SourceInfo.Status. FBlock 0x44 (SDARS) is 01 00 02 02 <val> <ch0..3>;
		 * FBlock 0x40 (AmFmTuner) answers shorter, e.g. 01 05 0A 0B 0C 0D where
		 * the trailing bytes are the allocated physical channels. Print
		 * format-agnostic: SrcNr then the channel field, flag FF=unallocated. */
		if (fkt == FKT_SOURCEINFO && op == OP_STATUS && tlen >= 2) {
			int allff = 1;
			printf("    [decode] SrcNr=%u rest:", f[F_DATA]);
			for (i = 1; i < tlen; i++) {
				uint8_t c = f[F_DATA + i];
				if (c == 0xFF) printf(" FF(unalloc)");
				else { printf(" 0x%02X", c); allff = 0; }
			}
			printf("  -> %s\n", allff ? "INACTIVE/unallocated" :
			       "ACTIVE (channels allocated)");
		}

		/* SourceActivity.Status: Len1 01 (firmware 0x44 inactive baseline);
		 * Len2 = [SrcNr][activeFlag], flag!=0 means commanded-active. */
		if (fkt == FKT_SOURCEACTIVITY && op == OP_STATUS) {
			if (tlen >= 2)
				printf("    [decode] SourceActivity SrcNr=%u activeFlag=%u (%s)\n",
				       f[F_DATA], f[F_DATA + 1],
				       f[F_DATA + 1] ? "ACTIVE" : "not commanded-active");
			else if (tlen == 1)
				printf("    [decode] SourceActivity INACTIVE (Len1 baseline)\n");
		}
	}
}

/* Query one node's NetBlock FBlockIDs; if `want` is hosted, print + return 1.
 * Reply = bare {fblock,inst} pairs (a leading 0x02/0x03 is the NM/CM FBlock
 * entry itself, not a tag), per mostd netblock_server.c. */
static int fblockids_query(uint16_t addr, uint8_t inst, uint8_t want,
			   uint16_t *host_addr, uint8_t *host_inst)
{
	long deadline;
	int found = 0;

	if (send_get(addr, NETBLOCK_FBLOCK, inst, FKT_FBLOCKIDS, NULL, 0) < 0) {
		perror("  FBlockIDs send");
		return 0;
	}

	deadline = now_ms() + 500;
	for (;;) {
		uint8_t f[MOST_FRAME_LEN];
		uint16_t src, fkt;
		uint8_t fb, op, tlen;
		long remain = deadline - now_ms();
		int i;

		if (remain <= 0) break;
		if (most_poll(g_h, (int)remain) <= 0) break;
		if (most_read(g_h, f) != 0) break;

		src  = ((uint16_t)f[F_ADDR_HI] << 8) | f[F_ADDR_LO];
		fb   = f[F_FBLOCK];
		fkt  = ((uint16_t)f[F_FKTID_HI] << 4) | (f[F_FKTID_LOOP] >> 4);
		op   = f[F_FKTID_LOOP] & 0x0F;
		tlen = f[F_TEL] & 0x0F;
		if (tlen > MOST_FRAME_LEN - F_DATA)
			tlen = (uint8_t)(MOST_FRAME_LEN - F_DATA);
		if (fb != NETBLOCK_FBLOCK || fkt != FKT_FBLOCKIDS || op != OP_STATUS)
			continue;

		printf("  node 0x%04X hosts:", src);
		for (i = 0; i + 1 < tlen; i += 2) {
			printf(" 0x%02X.%02X", f[F_DATA + i], f[F_DATA + i + 1]);
			/* Key the match on the responding frame, not the queried
			 * address: control replies arrive async, so a late STATUS
			 * from an earlier node can land in this read. src + the
			 * matched pair's inst are the only reliable host coords. */
			if (f[F_DATA + i] == want) {
				found = 1;
				if (host_addr) *host_addr = src;
				if (host_inst) *host_inst = f[F_DATA + i + 1];
			}
		}
		if (found)
			printf("   <-- hosts target 0x%02X", want);
		printf("\n");
		break;   /* one STATUS per read is enough */
	}
	return found;
}

/* Scan candidate node addresses for the FBlock hosting `want`. */
static void find_fblock(const uint16_t *addrs, size_t n, uint8_t want)
{
	static const uint8_t insts[] = { 0x00, 0x0D };
	size_t a, k;
	int any = 0;

	printf("[find] scanning %zu node(s) for FBlock 0x%02X via NetBlock FBlockIDs\n",
	       n, want);
	for (a = 0; a < n; a++) {
		for (k = 0; k < sizeof(insts); k++) {
			uint16_t host = 0;
			uint8_t hinst = 0xFF;

			if (fblockids_query(addrs[a], insts[k], want, &host, &hinst)) {
				any = 1;
				printf("  -> use: source_probe <dev> 0x%04X 0x%02X 0x%02X\n",
				       host, want, hinst);
			}
			flush_events();
		}
	}
	if (!any)
		printf("[find] FBlock 0x%02X not found on scanned nodes "
		       "(widen the address list)\n", want);
}

/* GET-only interface map: which FktIDs a FBlock implements. A reply with error
 * code 0x03 (FktID not available) = absent; STATUS or any other error (0x04/05/06
 * = wrong verb/length/param, but the FktID exists) = present. GET-only, no
 * payload, so it never commands anything - safe to sweep the live ConnectionMaster. */
static void sweep_fb(uint16_t addr, uint8_t fb, uint8_t inst, uint16_t lo, uint16_t hi)
{
	uint16_t fkt;
	int hits = 0;

	printf("[sweep] addr=0x%04X fb=0x%02X inst=0x%02X fkt 0x%03X..0x%03X (GET, no payload)\n",
	       addr, fb, inst, lo, hi);
	for (fkt = lo; fkt <= hi; fkt++) {
		long deadline;

		if (send_get(addr, fb, inst, fkt, NULL, 0) < 0) {
			perror("  send");
			continue;
		}
		deadline = now_ms() + 150;
		for (;;) {
			uint8_t f[MOST_FRAME_LEN];
			uint16_t rsrc, rfkt;
			uint8_t rfb, op, tlen, i;
			long remain = deadline - now_ms();

			if (remain <= 0) break;
			if (most_poll(g_h, (int)remain) <= 0) break;
			if (most_read(g_h, f) != 0) break;

			rsrc = ((uint16_t)f[F_ADDR_HI] << 8) | f[F_ADDR_LO];
			rfb  = f[F_FBLOCK];
			rfkt = ((uint16_t)f[F_FKTID_HI] << 4) | (f[F_FKTID_LOOP] >> 4);
			op   = f[F_FKTID_LOOP] & 0x0F;
			tlen = f[F_TEL] & 0x0F;
			if (tlen > MOST_FRAME_LEN - F_DATA)
				tlen = (uint8_t)(MOST_FRAME_LEN - F_DATA);
			if (rfb != fb)
				continue;
			/* Error 0x03 = FktID absent: skip quietly. */
			if (op == OP_ERROR && tlen >= 1 && f[F_DATA] == 0x03)
				break;

			hits++;
			printf("  fkt=0x%03X op=0x%X%s", rfkt, op,
			       op == OP_ERROR ? " ERROR" : (op == OP_STATUS ? " STATUS" : ""));
			if (tlen) {
				printf(" payload=");
				for (i = 0; i < tlen; i++)
					printf("%02X ", f[F_DATA + i]);
			}
			if (op == OP_ERROR && tlen >= 1)
				printf(" (errcode 0x%02X = exists, wrong verb/len/param)",
				       f[F_DATA]);
			printf("  [src=0x%04X]\n", rsrc);
			break;
		}
		flush_events();
	}
	printf("[sweep] done, %d implemented FktID(s)\n", hits);
}

static void usage(const char *p)
{
	fprintf(stderr,
	  "usage: %s <dev> [src_addr_hex=0x0100] [fblock=0x40] [inst=0xFF] [max_src=1]\n"
	  "       %s <dev> find [fblock=0x40] [addr_hex ...]\n"
	  "  dev          /dev/mostX suffix, e.g. 0\n"
	  "  src_addr_hex logical address of the node hosting the source FBlock\n"
	  "  fblock       source FBlock id (0x40=AmFmTuner, 0x44=SDARS)\n"
	  "  inst         InstID (0xFF wildcard default)\n"
	  "  max_src      probe SourceInfo/SourceActivity for src 0..max_src\n"
	  "\n"
	  "Run once while the source is ACTIVE and once while idle; diff the\n"
	  "SourceActivity / SourceInfo activity-val+channels.\n"
	  "\n"
	  "       %s <dev> sweep <addr_hex> <fblock> [inst=0xFF] [lo=0x100] [hi=0x130]\n"
	  "  GET-only interface map: list the FktIDs a FBlock implements.\n"
	  "  e.g. %s 0 sweep 0x0100 0x03 0xFF 0x000 0x1FF   (ConnectionMaster)\n",
	  p, p, p, p);
}

/* Default node addresses to scan in `find` mode (seen on this ring). */
static const uint16_t default_scan_addrs[] = {
	0x0100, 0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0106, 0x0107,
	0x0108, 0x0110, 0x0111, 0x0112, 0x0113, 0x0114, 0x0115, 0x0116,
};

int main(int argc, char **argv)
{
	const char *dev;
	uint16_t src_addr = 0x0100;
	uint8_t inst = 0xFF;
	unsigned max_src = 1, s;

	int find_mode = 0, sweep_mode = 0;

	if (argc < 2) { usage(argv[0]); return 2; }
	dev = argv[1];
	if (argc >= 3 && strcmp(argv[2], "find") == 0) {
		find_mode = 1;
	} else if (argc >= 3 && strcmp(argv[2], "sweep") == 0) {
		sweep_mode = 1;
	} else {
		if (argc >= 3) src_addr = (uint16_t)strtoul(argv[2], NULL, 0);
		if (argc >= 4) g_fb     = (uint8_t)strtoul(argv[3], NULL, 0);
		if (argc >= 5) inst     = (uint8_t)strtoul(argv[4], NULL, 0);
		if (argc >= 6) max_src  = (unsigned)strtoul(argv[5], NULL, 0);
	}

	g_h = most_open(dev);
	if (!g_h) { perror("most_open"); return 1; }

	{
		uint16_t cur = 0;
		most_get_node_addr(g_h, &cur);
		if (cur == 0x0000 || cur == 0xFFFF) {
			if (most_set_node_addr(g_h, TOOL_NODE_ADDR) == 0)
				cur = TOOL_NODE_ADDR;
		}
		printf("source_probe: (our node addr=0x%04X, flushed %d stale events)\n",
		       cur, flush_events());
	}

	if (sweep_mode) {
		uint16_t addr, lo = 0x100, hi = 0x130;
		uint8_t fb;

		if (argc < 5) { usage(argv[0]); most_close(g_h); return 2; }
		addr = (uint16_t)strtoul(argv[3], NULL, 0);
		fb   = (uint8_t)strtoul(argv[4], NULL, 0);
		if (argc >= 6) inst = (uint8_t)strtoul(argv[5], NULL, 0);
		if (argc >= 7) lo   = (uint16_t)strtoul(argv[6], NULL, 0);
		if (argc >= 8) hi   = (uint16_t)strtoul(argv[7], NULL, 0);
		sweep_fb(addr, fb, inst, lo, hi);
		most_close(g_h);
		return 0;
	}

	if (find_mode) {
		uint8_t want = FB_AMFMTUNER;
		uint16_t addrs[64];
		size_t n = 0;
		int i;

		if (argc >= 4) want = (uint8_t)strtoul(argv[3], NULL, 0);
		for (i = 4; i < argc && n < sizeof(addrs) / sizeof(addrs[0]); i++)
			addrs[n++] = (uint16_t)strtoul(argv[i], NULL, 0);

		if (n)
			find_fblock(addrs, n, want);
		else
			find_fblock(default_scan_addrs,
				    sizeof(default_scan_addrs) / sizeof(default_scan_addrs[0]),
				    want);
		most_close(g_h);
		return 0;
	}

	printf("probing addr=0x%04X fb=0x%02X inst=0x%02X\n", src_addr, g_fb, inst);
	printf("[SyncDataInfo.Get] (01 00)\n");
	{
		uint8_t p[2] = { 0x01, 0x00 };
		if (send_get(src_addr, g_fb, inst, FKT_SYNCDATAINFO, p, 2) < 0)
			perror("  send");
		drain(800);
	}

	/* SourceInfo/SourceActivity: firmware handles these as mixed OPTypes
	 * (0x101 GET, 0x103 SETGET). The AmFmTuner answered GET on these with
	 * "OPType not available", so try GET then SETGET and let the node pick. */
	for (s = 0; s <= max_src; s++) {
		uint8_t sn = (uint8_t)s;
		static const uint8_t ops[] = { OP_GET, OP_SETGET };
		size_t o;

		for (o = 0; o < sizeof(ops); o++) {
			/* SourceActivity SETGET wants a 2-byte payload (1 byte -> err
			 * 0x05 length wrong); GET wants 1. Use {SrcNr,0x00} for SETGET
			 * (sub-index 0 = query, no state change on the firmware path). */
			uint8_t pa[2] = { sn, 0x00 };
			uint8_t pa_len = (ops[o] == OP_SETGET) ? 2 : 1;

			printf("[src %u] SourceInfo op=0x%X\n", s, ops[o]);
			if (send_op(src_addr, g_fb, inst, FKT_SOURCEINFO, ops[o], &sn, 1) < 0)
				perror("  SourceInfo send");
			drain(400);
			printf("[src %u] SourceActivity op=0x%X\n", s, ops[o]);
			if (send_op(src_addr, g_fb, inst, FKT_SOURCEACTIVITY, ops[o], pa, pa_len) < 0)
				perror("  SourceActivity send");
			drain(400);
		}
	}

	printf("Done.\n");
	most_close(g_h);
	return 0;
}
