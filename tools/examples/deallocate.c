/*
 * deallocate - free MOST synchronous channels previously reserved with
 * resource_alloc / audio_tx, by sending a De-Allocate (Type 04h) message to the
 * timing-master with the connection label.
 *
 * Each Resource Allocate (Type 03h) reserves channels that stay reserved until
 * de-allocated or the ring resets; run this between attempts so you do not
 * exhaust the ring's free channels.
 *
 * Format (OS8104A; matches the working SocketMost driver deallocate()):
 *   TCMB 0xC0.. = [01, 04, 04, 00, 00, <ConnectionLabel>, 00]
 *     C0h=01 priority, C1h=04 type=De-Allocate, C2h/C3h=0400 (timing-master),
 *     C5h=ConnectionLabel (the P0 / label printed by resource_alloc/audio_tx).
 *   Kick STX; the answer overwrites the TCMB: C7h Answer1 (1=GRANT, 2=BUSY,
 *   3=WRONG, 4=WRONG_TARGET).
 *
 * The connection label is the value resource_alloc prints as "connection label
 * (P0)" and audio_tx prints in its closing "De-allocate ... with label" line.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "most.h"

#define RA_OFS_PRIORITY  0
#define RA_OFS_TYPE      1
#define RA_OFS_TGT_HI    2
#define RA_OFS_TGT_LO    3
#define RA_OFS_LABEL     5
#define RA_PRIORITY      0x01
#define RA_TYPE_DEALLOC  0x04
#define TM_ADDR_HI       0x04
#define TM_ADDR_LO       0x00
#define REG_ANSWER1      0xC7

#define POLL_TRIES       100
#define POLL_WAIT_US     2000
#define TX_BUSY_RETRIES  8
#define TX_BUSY_WAIT_US  3000

int main(int argc, char **argv)
{
	const char *dev;
	struct most_handle *h;
	uint8_t frm[MOST_FRAME_LEN] = {0};
	uint8_t answer1 = 0;
	unsigned label;
	int attempt, i;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <dev> <connection_label_hex>\n"
			"  e.g. %s 0 0x0A   (label from resource_alloc/audio_tx)\n",
			argv[0], argv[0]);
		return 2;
	}
	dev = argv[1];
	label = (unsigned)strtoul(argv[2], NULL, 0) & 0xFF;

	h = most_open(dev);
	if (!h) { perror("most_open"); return 1; }

	printf("deallocate: dev=/dev/most%s, connection label=0x%02X\n", dev, label);

	frm[RA_OFS_PRIORITY] = RA_PRIORITY;
	frm[RA_OFS_TYPE]     = RA_TYPE_DEALLOC;
	frm[RA_OFS_TGT_HI]   = TM_ADDR_HI;
	frm[RA_OFS_TGT_LO]   = TM_ADDR_LO;
	frm[RA_OFS_LABEL]    = (uint8_t)label;

	for (attempt = 0; attempt < TX_BUSY_RETRIES; attempt++) {
		if (most_write(h, frm) == 0)
			break;
		if (errno != EBUSY) { perror("most_write"); most_close(h); return 1; }
		usleep(TX_BUSY_WAIT_US);
	}
	if (attempt == TX_BUSY_RETRIES) {
		fprintf(stderr, "ctrl TX slot stayed busy\n");
		most_close(h);
		return 1;
	}

	for (i = 0; i < POLL_TRIES; i++) {
		if (most_reg_read(h, REG_ANSWER1, &answer1, 1) < 0) {
			perror("read Answer1"); most_close(h); return 1;
		}
		if (answer1 != 0x00)
			break;
		usleep(POLL_WAIT_US);
	}

	if (answer1 == 0x00) {
		fprintf(stderr, "timed out waiting for timing-master answer\n");
		most_close(h);
		return 1;
	}

	printf("Answer1=0x%02X (%s)\n", answer1,
	       answer1 == 1 ? "DEALLOC_GRANT" : answer1 == 2 ? "BUSY" :
	       answer1 == 3 ? "WRONG" : answer1 == 4 ? "WRONG_TARGET" : "ERROR");

	most_close(h);
	return answer1 == 1 ? 0 : 1;
}
