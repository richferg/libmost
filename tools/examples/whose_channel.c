/*
 * whose_channel - ask the ring which node sources a given connection label
 * (the first channel of a streaming connection), via a Remote GetSource
 * message (Type 05h). Use it to identify who owns the channels the amplifier
 * is reading - i.e. who we collide with when injecting onto them.
 *
 * Format (OS8104A; matches SocketMost getRemoteSource()):
 *   TCMB 0xC0.. = [01, 05, 03, C8, 00, <ConnectionLabel>, 00]
 *     C0h=01 priority, C1h=05 type=Remote GetSource, C2h/C3h=03C8 (broadcast),
 *     C5h=ConnectionLabel (e.g. 0x0A, the first channel from amp_probe SinkInfo).
 *   Kick STX; the answer overwrites the TCMB:
 *     CAh=node position, CCh=group address, CDh/CEh=logical address hi/lo of
 *     the sourcing node.
 *
 * If the owner is the head unit (node position 0 / logical 0x0100) it is the
 * car's main audio source feeding the amp - expect a hard collision.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "most.h"

#define RA_PRIORITY   0x01
#define RA_TYPE_GETSRC 0x05
#define BC_HI         0x03
#define BC_LO         0xC8

#define REG_NODEPOS   0xCA
#define REG_GROUP     0xCC
#define REG_LOG_HI    0xCD
#define REG_LOG_LO    0xCE

#define POLL_TRIES    150
#define POLL_WAIT_US  2000
#define TX_BUSY_RETRIES 8
#define TX_BUSY_WAIT_US 3000

int main(int argc, char **argv)
{
	const char *dev;
	struct most_handle *h;
	uint8_t frm[MOST_FRAME_LEN] = {0};
	uint8_t nodepos = 0, group = 0, lhi = 0, llo = 0;
	unsigned label;
	int attempt, i;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <dev> <connection_label_hex>\n"
			"  e.g. %s 0 0x0A  (first channel from amp_probe SinkInfo)\n",
			argv[0], argv[0]);
		return 2;
	}
	dev   = argv[1];
	label = (unsigned)strtoul(argv[2], NULL, 0) & 0xFF;

	h = most_open(dev);
	if (!h) { perror("most_open"); return 1; }

	printf("whose_channel: dev=/dev/most%s, connection label=0x%02X\n", dev, label);

	frm[0] = RA_PRIORITY;
	frm[1] = RA_TYPE_GETSRC;
	frm[2] = BC_HI;
	frm[3] = BC_LO;
	frm[4] = 0x00;
	frm[5] = (uint8_t)label;
	frm[6] = 0x00;

	for (attempt = 0; attempt < TX_BUSY_RETRIES; attempt++) {
		if (most_write(h, frm) == 0)
			break;
		if (errno != EBUSY) { perror("most_write"); most_close(h); return 1; }
		usleep(TX_BUSY_WAIT_US);
	}
	if (attempt == TX_BUSY_RETRIES) {
		fprintf(stderr, "ctrl TX slot stayed busy\n"); most_close(h); return 1;
	}

	/* Logical address high byte is 0x01 for any node, so poll it for validity. */
	for (i = 0; i < POLL_TRIES; i++) {
		if (most_reg_read(h, REG_LOG_HI, &lhi, 1) < 0) {
			perror("read"); most_close(h); return 1;
		}
		if (lhi != 0x00)
			break;
		usleep(POLL_WAIT_US);
	}
	if (lhi == 0x00) {
		fprintf(stderr, "no response (label may be unused, or owner did not answer)\n");
		most_close(h);
		return 1;
	}

	most_reg_read(h, REG_NODEPOS, &nodepos, 1);
	most_reg_read(h, REG_GROUP,   &group,   1);
	most_reg_read(h, REG_LOG_LO,  &llo,     1);

	printf("source of label 0x%02X: node position=0x%02X group=0x%02X "
	       "logical addr=0x%02X%02X\n", label, nodepos, group, lhi, llo);
	if (nodepos == 0x00 || (lhi == 0x01 && llo == 0x00))
		printf("  -> this is the head unit / network master (node 0): its own "
		       "audio source feeds the amp. Hard collision.\n");

	most_close(h);
	return 0;
}
