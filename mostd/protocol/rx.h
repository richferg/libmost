#ifndef MOSTD_PROTOCOL_RX_H
#define MOSTD_PROTOCOL_RX_H

#include <stdint.h>

#include "most.h"

#define MOSTD_RX_MAX_PAYLOAD 2048

struct mostd_rx_msg {
	uint8_t msg_type;
	uint16_t source;
	uint8_t fblock;
	uint8_t inst;
	uint16_t fktid;
	uint8_t optype;
	uint8_t telid;
	uint16_t tellen;
	uint8_t data[MOSTD_RX_MAX_PAYLOAD];
	uint8_t segmented;
};

typedef void (*mostd_rx_cb_t)(const struct mostd_rx_msg *msg, void *userdata);

void mostd_rx_init(void);
void mostd_rx_process_frame(const uint8_t raw[MOST_FRAME_LEN], mostd_rx_cb_t cb, void *userdata);

#endif
