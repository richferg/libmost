/*
 * resource_alloc - request synchronous channel(s) from the MOST timing-master
 * via a Resource Allocate control message (Type 03h), then print the granted
 * physical channels.
 *
 * This is the front half of "send my own audio": before you load the MOST
 * Routing Table (see audio_inject) you must own the network bytes you write
 * into. On a MOST25 ring the timing-master is the resource manager; you ask it
 * for N channels and it hands back the physical channel numbers, which ARE the
 * MRT byte locations you may then route your source data into.
 *
 * Grounded in OS8104A datasheet 13.5.2.4 "Resource Allocate (Type 03h)":
 *   The transmit Control Message Buffer (TCMB, regs 0xC0-0xD4) is filled with:
 *     C0h = 01h   priority
 *     C1h = 03h   message type = Resource Allocate          <-- the key field
 *     C2h = 04h   target address hi  (timing-master = physical 0x0400,
 *     C3h = 00h   target address lo   always node position 0)
 *     C4h = 00h   reserved
 *     C5h = N     number of channels requested (1..8)
 *   STX is set (the driver does this on write()). The timing-master answers and
 *   the response OVERWRITES the same TCMB buffer:
 *     C7h = Answer1   01h = ALLOC_GRANT, else error/denied
 *     C8h = Answer2   AL_FREE = channels still free afterwards
 *     C9h..D0h = P0..P7  the granted physical channel numbers (= MRT locations).
 *                        FFh = location not usable.
 *     P0 (C9h) is also the CONNECTION LABEL, the handle used to de-allocate.
 *
 * The kernel driver writes the 21-byte frame straight into TCMB (frame[N] ->
 * reg 0xC0+N) and kicks STX; it does not read the answer back, so we poll the
 * TCMB registers here after the message is sent.
 *
 * On the car the Pi is a slave, so the car's head unit (timing-master) grants
 * the channels. If the OS8104 were itself the timing-master it would answer
 * incoming allocates internally with no software.
 *
 * WARNING: this consumes ring synchronous bandwidth. De-allocate (Type 04h,
 * see deallocate.c) using the connection label when done, or the
 * channels stay reserved until the ring resets.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "most.h"

/* Resource Allocate message layout in the 21-byte frame (== TCMB reg 0xC0+N). */
#define RA_OFS_PRIORITY   0   /* -> reg 0xC0 */
#define RA_OFS_TYPE       1   /* -> reg 0xC1 */
#define RA_OFS_TGT_HI     2   /* -> reg 0xC2 */
#define RA_OFS_TGT_LO     3   /* -> reg 0xC3 */
#define RA_OFS_REQUEST    5   /* -> reg 0xC5 */

#define RA_PRIORITY       0x01
#define RA_TYPE_ALLOCATE  0x03
#define TM_ADDR_HI        0x04  /* timing-master physical address 0x0400 */
#define TM_ADDR_LO        0x00

/* TCMB response registers. */
#define REG_ANSWER1       0xC7
#define REG_ANSWER2       0xC8
#define REG_P0            0xC9  /* P0..P7 = 0xC9..0xD0 */
#define RA_MAX_CHANNELS   8

#define ANSWER1_GRANT     0x01
#define CHAN_UNUSABLE     0xFF

/* How long to wait for the timing-master's answer to land in the TCMB. */
#define POLL_TRIES        100
#define POLL_WAIT_US      2000   /* 100 * 2ms = 200ms budget */

/* most_write retries: the single ctrl TX slot may be momentarily busy. */
#define TX_BUSY_RETRIES   8
#define TX_BUSY_WAIT_US   3000

static struct most_handle *g_h;

static int rd(uint8_t reg, uint8_t *val)
{
	return most_reg_read(g_h, reg, val, 1);
}

int main(int argc, char **argv)
{
	const char *dev = "0";
	unsigned num = 1;
	uint8_t frm[MOST_FRAME_LEN] = {0};
	uint8_t answer1 = 0, answer2 = 0;
	uint8_t chans[RA_MAX_CHANNELS] = {0};
	int attempt, i, n_granted = 0;

	if (argc >= 2)
		dev = argv[1];
	if (argc >= 3)
		num = (unsigned)strtoul(argv[2], NULL, 0);
	if (num < 1 || num > RA_MAX_CHANNELS) {
		fprintf(stderr, "channel count must be 1..%d\n", RA_MAX_CHANNELS);
		return 2;
	}

	g_h = most_open(dev);
	if (!g_h) {
		perror("most_open");
		return 1;
	}

	printf("resource_alloc: dev=/dev/most%s, requesting %u synchronous channel(s) "
	       "from timing-master (0x%02X%02X)\n", dev, num, TM_ADDR_HI, TM_ADDR_LO);

	/* Build the Type-03h Resource Allocate frame. */
	frm[RA_OFS_PRIORITY] = RA_PRIORITY;
	frm[RA_OFS_TYPE]     = RA_TYPE_ALLOCATE;
	frm[RA_OFS_TGT_HI]   = TM_ADDR_HI;
	frm[RA_OFS_TGT_LO]   = TM_ADDR_LO;
	frm[RA_OFS_REQUEST]  = (uint8_t)num;

	/* Send it (retry while the ctrl TX slot is busy). */
	for (attempt = 0; attempt < TX_BUSY_RETRIES; attempt++) {
		if (most_write(g_h, frm) == 0)
			break;
		if (errno != EBUSY) {
			perror("most_write");
			most_close(g_h);
			return 1;
		}
		usleep(TX_BUSY_WAIT_US);
	}
	if (attempt == TX_BUSY_RETRIES) {
		fprintf(stderr, "ctrl TX slot stayed busy, giving up\n");
		most_close(g_h);
		return 1;
	}

	/*
	 * Poll the TCMB for the answer. We zeroed the frame before sending, so
	 * Answer1 starts at 0x00; a non-zero value means the timing-master has
	 * written its response back into the buffer.
	 */
	for (i = 0; i < POLL_TRIES; i++) {
		if (rd(REG_ANSWER1, &answer1) < 0) {
			perror("read Answer1");
			most_close(g_h);
			return 1;
		}
		if (answer1 != 0x00)
			break;
		usleep(POLL_WAIT_US);
	}

	if (answer1 == 0x00) {
		fprintf(stderr, "timed out waiting for timing-master answer (no response in "
			"%d ms). Is the head unit awake and acting as resource manager?\n",
			(POLL_TRIES * POLL_WAIT_US) / 1000);
		most_close(g_h);
		return 1;
	}

	rd(REG_ANSWER2, &answer2);
	for (i = 0; i < RA_MAX_CHANNELS; i++)
		rd((uint8_t)(REG_P0 + i), &chans[i]);

	printf("Answer1=0x%02X (%s)  Answer2(free)=0x%02X\n",
	       answer1, answer1 == ANSWER1_GRANT ? "ALLOC_GRANT" : "NOT GRANTED", answer2);

	if (answer1 != ANSWER1_GRANT) {
		fprintf(stderr, "allocation refused by timing-master.\n");
		most_close(g_h);
		return 1;
	}

	printf("granted physical channels (P0..P7):");
	for (i = 0; i < RA_MAX_CHANNELS; i++) {
		if (chans[i] == CHAN_UNUSABLE)
			printf(" --");
		else {
			printf(" 0x%02X", chans[i]);
			n_granted++;
		}
	}
	printf("\n");
	printf("connection label (P0) = 0x%02X  (keep this to de-allocate later)\n", chans[0]);
	printf("%d channel(s) granted. Route audio onto them with audio_inject, or "
	       "free them with:\n  deallocate %s 0x%02X\n",
	       n_granted, dev, chans[0]);

	most_close(g_h);
	return 0;
}
