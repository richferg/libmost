#ifndef MOSTD_PROTOCOL_TX_H
#define MOSTD_PROTOCOL_TX_H

#include <stddef.h>
#include <stdint.h>

struct most_handle;

int mostd_tx_status(struct most_handle *h, uint16_t addr, uint8_t fblock, uint8_t inst,
		    uint16_t fktid, uint8_t telid, const uint8_t *payload, size_t payload_len);
int mostd_tx_op(struct most_handle *h, uint16_t addr, uint8_t fblock, uint8_t inst,
		uint16_t fktid, uint8_t op, uint8_t telid, const uint8_t *payload, size_t payload_len);
int mostd_tx_error_for_request(struct most_handle *h, uint16_t addr, uint8_t fblock,
			       uint8_t inst, uint16_t fktid, uint8_t telid, uint8_t req_op,
			       const uint8_t *req_payload, size_t req_len,
			       uint8_t error_code, uint8_t error_info);
#endif
