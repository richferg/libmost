#ifndef SEGMENTED_H
#define SEGMENTED_H

#include <stdint.h>

#define MOST_FRAME_LEN 21
#define MOST_MAX_PDU   2048

struct most_pdu {
    uint8_t msg_type;
    uint16_t source;
    uint8_t fblock;
    uint8_t inst;
    uint16_t fktid;
    uint8_t optype;
    int payload_len;
    uint8_t payload[MOST_MAX_PDU];
};

typedef void (*most_pdu_callback_t)(const struct most_pdu *pdu, void *userdata);

/* Feed one raw 21-byte frame into the reassembler */
void segmented_process_frame(const uint8_t raw[MOST_FRAME_LEN],
                             most_pdu_callback_t cb,
                             void *userdata);

#endif
