/*
 * audio_inject - route the local serial source port onto channels the amplifier
 * is ALREADY reading, without allocating or sending any ConnectionMaster /
 * Connect control messages.
 *
 * Why: on this car the AudioAmplifier sink is owned by the head-unit
 * ConnectionMaster - it answers queries but rejects our Connect/DisConnect with
 * ErrorCode 0x07 ("parameter not available"). However the amp is already
 * connected to a fixed set of network channels (read them with amp_probe:
 * SinkInfo -> ChannelList, e.g. 0x0A 0x0B 0x0C 0x0D) and unmuted. If we source
 * our own audio onto exactly those channels, the amp plays it - no connection
 * control needed.
 *
 * CAVEAT: those channels are allocated/sourced by the car's own source node. A
 * MOST synchronous byte is meant to have ONE source; if the car source is
 * actively driving the channels, you collide and get garbage. This works only
 * when the car's source for that connection is idle/silent (no media playing,
 * or a source selected that does not feed the amp). It's a test of whether we
 * can drive the amp at all, not a clean long-term path.
 *
 * Grounded in OS8104A datasheet (source port + MRT) and the SocketMost driver
 * source-streaming values:
 *   - bXCR is left untouched (driver init leaves the node active; clearing the
 *     ABY bit would put it in all-bypass and kill it - see audio_tx.c).
 *   - MRT[networkChannel] = source-port input address reference
 *     (0x49,0x59,0x69,0x79 for stereo, from SocketMost).
 *   - bSDC2 SCK rate must match the external I2S clock (default 32Fs = 0x40;
 *     pass an override if your source clocks differently).
 *   - bSDC3 = 0x00 (source ports enabled + unmuted); bSDC1 |= UNMUTE_SOURCE.
 *
 * Usage:
 *   audio_inject <dev> <first_channel_hex> [count=4] [bsdc2=0x40]
 *   e.g. audio_inject 0 0x0A 4      # drive the amp's current channels
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "most.h"

#define REG_BSDC1   0x82
#define REG_BSDC2   0x8C
#define REG_BSDC3   0x8D
/* Full I2S source-port format (SocketMost init): EDG|DEL|SCK_OUTPUT|
 * TRANSPARENT_DIS|UNMUTE_SOURCE, POL/NBR/SPDIF=0. The driver leaves bSDC1 at
 * 0x12 (missing the EDG/DEL I2S framing bits), so set it explicitly. */
#define BSDC1_I2S           0xD3
#define BSDC3_ON            0x00

/* Source-port input address references (SocketMost), stereo. */
static const uint8_t SRC_MRA[4] = { 0x49, 0x59, 0x69, 0x79 };

int main(int argc, char **argv)
{
	const char *dev;
	struct most_handle *h;
	unsigned first, count = 4, bsdc2 = 0x40;
	uint8_t v = 0;
	unsigned i;

	if (argc < 3) {
		fprintf(stderr,
		  "usage: %s <dev> <first_channel_hex> [count=4] [bsdc2=0x40]\n"
		  "  drive the channels the amp already reads (from amp_probe SinkInfo)\n"
		  "  count  number of contiguous channels (4 = stereo 16-bit)\n"
		  "  bsdc2  source-port SCK rate: 0x40=32Fs, 0x60=64Fs (match your source)\n",
		  argv[0]);
		return 2;
	}
	dev   = argv[1];
	first = (unsigned)strtoul(argv[2], NULL, 0);
	if (argc >= 4) count = (unsigned)strtoul(argv[3], NULL, 0);
	if (argc >= 5) bsdc2 = (unsigned)strtoul(argv[4], NULL, 0);
	if (count < 1 || count > 4) { fprintf(stderr, "count must be 1..4\n"); return 2; }

	h = most_open(dev);
	if (!h) { perror("most_open"); return 1; }

	printf("audio_inject: routing source port onto channels");
	for (i = 0; i < count; i++)
		printf(" 0x%02X", first + i);
	printf("  (bSDC2=0x%02X)\n", bsdc2);

	/* MRT: each network channel gets its data from a source-port input ref. */
	for (i = 0; i < count; i++) {
		v = SRC_MRA[i];
		if (most_reg_write(h, (uint16_t)(first + i), &v, 1) < 0) {
			fprintf(stderr, "MRT write 0x%02X failed\n", first + i);
			most_close(h);
			return 1;
		}
	}

	v = BSDC1_I2S;                 most_reg_write(h, REG_BSDC1, &v, 1);  /* I2S format */
	v = (uint8_t)bsdc2;            most_reg_write(h, REG_BSDC2, &v, 1);  /* SCK rate */
	v = BSDC3_ON;                  most_reg_write(h, REG_BSDC3, &v, 1);  /* ports on, unmuted */

	/* Read back the chip state so we can confirm the config actually took. */
	{
		uint8_t bxcr = 0, s1 = 0, s2 = 0, s3 = 0, m[4] = {0};
		most_reg_read(h, 0x80, &bxcr, 1);
		most_reg_read(h, REG_BSDC1, &s1, 1);
		most_reg_read(h, REG_BSDC2, &s2, 1);
		most_reg_read(h, REG_BSDC3, &s3, 1);
		for (i = 0; i < count; i++)
			most_reg_read(h, (uint16_t)(first + i), &m[i], 1);
		printf("readback: bXCR=0x%02X bSDC1=0x%02X bSDC2=0x%02X bSDC3=0x%02X  "
		       "MRT[0x%02X..]=%02X %02X %02X %02X\n",
		       bxcr, s1, s2, s3, first, m[0], m[1], m[2], m[3]);
		printf("  expect: bXCR bit1(ABY)=1 active, bSDC1=0xD3, bSDC2=0x%02X, "
		       "bSDC3=0x00, MRT=49 59 69 79\n", (uint8_t)bsdc2);
	}

	printf("Done. Source port now driving those channels. If the amp is reading "
	       "them and the car's own source is idle, you should hear your audio.\n"
	       "If silent: confirm real bits are clocking into the source-port pins, "
	       "and that bSDC2 matches your I2S clock (try 0x60 for 64Fs).\n");

	most_close(h);
	return 0;
}
