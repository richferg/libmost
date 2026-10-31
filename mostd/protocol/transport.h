#ifndef MOSTD_PROTOCOL_TRANSPORT_H
#define MOSTD_PROTOCOL_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "most.h"

struct mostd_transport {
	struct most_handle *handle;
};

struct mostd_async_rx_image {
	uint16_t target_addr;
	uint16_t source_addr;
	const uint8_t *payload;
	size_t payload_capacity;
};

int mostd_transport_open(struct mostd_transport *transport, const char *devname);
void mostd_transport_close(struct mostd_transport *transport);
int mostd_transport_read_event(struct mostd_transport *transport, struct most_event *evt);
int mostd_transport_poll_event(struct mostd_transport *transport, int timeout_ms);
const char *mostd_transport_event_name(uint16_t type);
int mostd_transport_decode_async_rx(const struct most_event *evt,
				       struct mostd_async_rx_image *image);

#endif /* MOSTD_PROTOCOL_TRANSPORT_H */
