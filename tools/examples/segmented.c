#include "segmented.h"
#include <string.h>
#include <stdio.h>

static struct most_pdu current;
static int assembling = 0;
static uint8_t expected_counter = 0;

void segmented_process_frame(const uint8_t raw[MOST_FRAME_LEN],
                             most_pdu_callback_t cb,
                             void *userdata)
{
    uint8_t telid  = raw[8] >> 4;
    uint8_t tellen = raw[8] & 0x0F;

    /* Unsegmented: just pass through as a single PDU */
    if (telid == 0) {
        struct most_pdu p;

        p.msg_type = raw[1];
        p.source  = ((uint16_t)raw[2] << 8) | raw[3];
        p.fblock = raw[4];
        p.inst   = raw[5];
        p.fktid  = (raw[6] << 4) | (raw[7] >> 4);
        p.optype = raw[7] & 0x0F;

        /* Unsegmented payload starts at raw[9]; no counter byte is present. */
        int payload_len = tellen;
        if (payload_len > MOST_MAX_PDU) {
            fprintf(stderr, "segmented: invalid TelLen=%d for unsegmented\n",
                    tellen);
            return;
        }

        p.payload_len = payload_len;
        if (payload_len > 0)
            memcpy(p.payload, &raw[9], payload_len);

        cb(&p, userdata);
        return;
    }

    /* Segmented: first segment (TelID = 1) */
    if (telid == 1) {
        uint8_t counter = raw[9];

        assembling = 1;
        expected_counter = counter;

        memset(&current, 0, sizeof(current));

        current.msg_type = raw[1];
        current.source  = ((uint16_t)raw[2] << 8) | raw[3];
        current.fblock = raw[4];
        current.inst   = raw[5];
        current.fktid  = (raw[6] << 4) | (raw[7] >> 4);
        current.optype = raw[7] & 0x0F;

        /* Data starts at raw[10], first byte (raw[9]) is counter */
        int payload_len = tellen - 1;   /* minus counter */
        if (payload_len < 0) {
            fprintf(stderr, "segmented: bad TelLen=%d in start segment\n",
                    tellen);
            assembling = 0;
            return;
        }

        if (payload_len > 0) {
            memcpy(current.payload, &raw[10], payload_len);
            current.payload_len = payload_len;
        } else {
            current.payload_len = 0;
        }
        return;
    }

    /* Middle segments (TelID = 2) */
    if (telid == 2) {
        uint8_t counter = raw[9];

        if (!assembling)
            return;

        uint16_t frame_source = ((uint16_t)raw[2] << 8) | raw[3];
        if (frame_source != current.source) {
            fprintf(stderr, "segmented: source changed mid-assembly (0x%04X -> 0x%04X)\n",
                    current.source, frame_source);
            assembling = 0;
            return;
        }

        int payload_len = tellen - 1;
        if (payload_len < 0) {
            fprintf(stderr, "segmented: bad TelLen=%d in middle segment\n",
                    tellen);
            assembling = 0;
            return;
        }

        if (counter != (uint8_t)(expected_counter + 1)) {
            fprintf(stderr,
                    "segmented: counter mismatch in middle segment (expected %u got %u)\n",
                    (uint8_t)(expected_counter + 1), counter);
            assembling = 0;
            return;
        }
        expected_counter = counter;

        if (payload_len > 0) {
            if (current.payload_len + payload_len > MOST_MAX_PDU) {
                fprintf(stderr, "segmented: PDU buffer overflow (mid)\n");
                assembling = 0;
                return;
            }

            memcpy(&current.payload[current.payload_len],
                   &raw[10], payload_len);
            current.payload_len += payload_len;
        }
        return;
    }

    /* Final segment (TelID = 3) */
    if (telid == 3) {
        uint8_t counter = raw[9];

        if (!assembling)
            return;

        uint16_t frame_source = ((uint16_t)raw[2] << 8) | raw[3];
        if (frame_source != current.source) {
            fprintf(stderr, "segmented: source changed mid-assembly (0x%04X -> 0x%04X)\n",
                    current.source, frame_source);
            assembling = 0;
            return;
        }

        int payload_len = tellen - 1;
        if (payload_len < 0) {
            fprintf(stderr, "segmented: bad TelLen=%d in final segment\n",
                    tellen);
            assembling = 0;
            return;
        }

        if (counter != (uint8_t)(expected_counter + 1)) {
            fprintf(stderr,
                    "segmented: counter mismatch in final segment (expected %u got %u)\n",
                    (uint8_t)(expected_counter + 1), counter);
            assembling = 0;
            return;
        }
        expected_counter = counter;

        if (payload_len > 0) {
            if (current.payload_len + payload_len > MOST_MAX_PDU) {
                fprintf(stderr, "segmented: PDU buffer overflow (end)\n");
                assembling = 0;
                return;
            }

            memcpy(&current.payload[current.payload_len],
                   &raw[10], payload_len);
            current.payload_len += payload_len;
        }

        /* Done: emit full PDU */
        cb(&current, userdata);
        assembling = 0;
        return;
    }

    /* Unknown TelID */
    fprintf(stderr, "segmented: unknown TelID=%u\n", telid);
}
