/* Built as `segtest` by tools/Makefile together with segmented.c. */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "segmented.h"

static void on_complete(const struct most_pdu *p, void *userdata)
{
    (void)userdata;

    printf("DECODED COMPLETE PDU:\n");
    printf("  FBlock : 0x%02X\n", p->fblock);
    printf("  Inst   : 0x%02X\n", p->inst);
    printf("  FktID  : 0x%03X\n", p->fktid);
    printf("  OpType : 0x%X\n",  p->optype);
    printf("  PayloadLen = %d\n", p->payload_len);
    printf("  Payload    = ");

    for (int i = 0; i < p->payload_len; i++)
        printf("%02X ", p->payload[i]);
    printf("\n");
}


/*---------------------------------------------------------
 * Read a line of 21 hex bytes.
 * Returns 1 if frame read, 0 on EOF.
 *--------------------------------------------------------*/
static int read_frame(uint8_t out[21], FILE *f)
{
    char line[512];
    if (!fgets(line, sizeof(line), f))
        return 0;

    int count = 0;
    char *tok = strtok(line, " \t\r\n");
    while (tok && count < 21) {
        unsigned int x;
        if (sscanf(tok, "%x", &x) != 1) {
            fprintf(stderr, "Invalid hex byte: %s\n", tok);
            return 0;
        }
        out[count++] = (uint8_t)x;
        tok = strtok(NULL, " \t\r\n");
    }

    if (count != 21) {
        fprintf(stderr, "Expected 21 bytes, got %d\n", count);
        return 0;
    }

    return 1;
}

/*---------------------------------------------------------
 * MAIN: Feed frames to the segmented parser
 *--------------------------------------------------------*/
int main(int argc, char **argv)
{
    FILE *f = stdin;

    if (argc >= 2) {
        f = fopen(argv[1], "r");
        if (!f) {
            perror("fopen");
            return 1;
        }
    }

    printf("MOST Segmentation Test Harness\n");
    printf("Reading frames...\n");

    uint8_t frame[21];

    while (read_frame(frame, f)) {

        printf("FRAME: ");
        for (int i = 0; i < 21; i++)
            printf("%02X ", frame[i]);
        printf("\n");

        segmented_process_frame(frame, on_complete, NULL);
    }

    if (f != stdin)
        fclose(f);

    return 0;
}
