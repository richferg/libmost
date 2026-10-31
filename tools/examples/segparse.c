#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define FRAME_LEN 21
#define MAX_ACCUM 2048

static uint8_t accum[MAX_ACCUM];
static int accum_len = 0;
static int assembling = 0;
static int expected_counter = 0;

/* Decode a complete, reassembled MOST control message.
 * buf[0] = FBlockID
 * buf[1] = InstID
 * buf[2..3] = FktID+OpType packed
 * buf[4] = TelID/TelLen packed
 * buf[5..] = data payload
 */
void decode_complete(uint8_t *buf, int len)
{
    printf("---- COMPLETE MOST MESSAGE (%d bytes) ----\n", len);

    uint8_t fblock = buf[0];
    uint8_t inst   = buf[1];
    uint16_t fktid = (buf[2] << 4) | (buf[3] >> 4);
    uint8_t optype = buf[3] & 0x0F;
    uint8_t telid  = buf[4] >> 4;
    uint8_t tellen = buf[4] & 0x0F;
    int actual_payload_len = len - 5;

    printf("FBlockID : 0x%02X\n", fblock);
    printf("InstID   : 0x%02X\n", inst);
    printf("FktID    : 0x%03X\n", fktid);
    printf("OpType   : %u\n", optype);
    printf("TelID    : %u (should be 0 if unsegmented)\n", telid);
    printf("TelLen   : %u\n", tellen);
    printf("TotalLen : %d\n", actual_payload_len);

    if (telid == 0) {
        if ((int)tellen == actual_payload_len)
            printf("TelLen check: PASS (TelLen matches payload length)\n");
        else
            printf("TelLen check: FAIL (TelLen=%u payload=%d)\n", tellen, actual_payload_len);
    } else {
        printf("TelLen check: N/A (segmented message; TelLen is per-segment)\n");
    }

    printf("Data     : ");
    for (int i = 5; i < len; i++)
        printf("%02X ", buf[i]);
    printf("\n\n");
}

/* Process one 21-byte raw frame (padding + OS8104 header + MOST CMD) */
void process_frame(uint8_t raw[FRAME_LEN])
{
    uint8_t telid  = raw[8] >> 4;
    uint8_t tellen = raw[8] & 0x0F;
    uint16_t src   = (raw[2] << 8) | raw[3];

    int counter = -1;
    int payload_offset = 9;
    int payload_len = tellen;

    if (telid > 0) {  // segmented
        if (tellen == 0) {
            printf("ERROR: segmented telegram with TelLen=0\n");
            return;
        }
        counter = raw[9];
        payload_offset = 10;
        payload_len = tellen - 1;  // minus counter
    }

    printf("[TelID=%d] Src=0x%04X Counter=%d Payload=%d bytes\n",
           telid, src, counter, payload_len);

    if (payload_len < 0 || payload_offset + payload_len > FRAME_LEN) {
        printf("ERROR: invalid payload length\n");
        return;
    }

    if (telid == 0) {
        /* Unsegmented: just decode this one.
         * &raw[4] → FBlock, Inst, FktID bytes, TelID/TelLen, payload
         * header is 5 bytes, so len = 5 + payload_len
         */
        decode_complete(&raw[4], 5 + payload_len);
        return;
    }

    if (telid == 1) {
        /* Start of segmented sequence */
        assembling = 1;
        accum_len = 0;
        expected_counter = 0;

        if (counter != 0)
            printf("WARNING: start segment counter != 0 (got %d)\n", counter);

        /* Copy header once: FBlock, Inst, FktID bytes, TelID/TelLen */
        memcpy(accum, &raw[4], 5);
        accum_len = 5;

        /* Copy payload from this segment */
        if (accum_len + payload_len > MAX_ACCUM) {
            printf("ERROR: accum overflow (start)\n");
            assembling = 0;
            return;
        }
        memcpy(accum + accum_len, raw + payload_offset, payload_len);
        accum_len += payload_len;

        return;
    }

    if (telid == 2) {
        /* Middle segment */
        if (!assembling) {
            printf("ERROR: mid segment without start\n");
            return;
        }

        if (counter != ((expected_counter + 1) & 0xFF))
            printf("WARNING: counter mismatch: expected %d got %d\n",
                   (expected_counter + 1) & 0xFF, counter);

        expected_counter = counter;

        if (accum_len + payload_len > MAX_ACCUM) {
            printf("ERROR: accum overflow (mid)\n");
            assembling = 0;
            return;
        }
        memcpy(accum + accum_len, raw + payload_offset, payload_len);
        accum_len += payload_len;
        return;
    }

    if (telid == 3) {
        /* Final segment */
        if (!assembling) {
            printf("ERROR: end segment without start\n");
            return;
        }

        expected_counter = counter;

        if (accum_len + payload_len > MAX_ACCUM) {
            printf("ERROR: accum overflow (end)\n");
            assembling = 0;
            return;
        }
        memcpy(accum + accum_len, raw + payload_offset, payload_len);
        accum_len += payload_len;

        /* Now decode the entire assembled message */
        decode_complete(accum, accum_len);

        assembling = 0;
        return;
    }

    printf("WARNING: unknown TelID %d\n", telid);
}

/* Main: read frames from stdin as 21 hex bytes each and process them */
int main(void)
{
    uint8_t raw[FRAME_LEN];

    while (1) {
        int count = 0;

        /* Try to read one full frame (21 hex bytes). If EOF before first
         * byte, exit cleanly; if EOF mid-frame, treat as error.
         */
        while (count < FRAME_LEN) {
            unsigned int x;
            int r = scanf("%x", &x);
            if (r != 1) {
                if (count == 0) {
                    /* clean EOF */
                    return 0;
                }
                fprintf(stderr,
                        "ERROR: incomplete frame on stdin (got %d bytes)\n",
                        count);
                return 1;
            }
            raw[count++] = (uint8_t)x;
        }

        process_frame(raw);
    }

    return 0;
}
