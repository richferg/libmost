#include "transport.h"

#include <errno.h>
#include <string.h>

int mostd_transport_open(struct mostd_transport *transport, const char *devname)
{
	if (!transport || !devname) {
		errno = EINVAL;
		return -1;
	}

	memset(transport, 0, sizeof(*transport));
	transport->handle = most_open(devname);
	if (!transport->handle)
		return -1;

	return 0;
}

void mostd_transport_close(struct mostd_transport *transport)
{
	if (!transport || !transport->handle)
		return;

	most_close(transport->handle);
	transport->handle = NULL;
}

int mostd_transport_read_event(struct mostd_transport *transport, struct most_event *evt)
{
	if (!transport || !transport->handle || !evt) {
		errno = EBADF;
		return -1;
	}

	return most_read_event(transport->handle, evt);
}

int mostd_transport_poll_event(struct mostd_transport *transport, int timeout_ms)
{
	if (!transport || !transport->handle) {
		errno = EBADF;
		return -1;
	}

	return most_poll_event(transport->handle, timeout_ms);
}

const char *mostd_transport_event_name(uint16_t type)
{
	switch (type) {
	case MOST_EVT_CTRL_RX:
		return "ctrl-rx";
	case MOST_EVT_CTRL_TX_DONE:
		return "ctrl-tx-done";
	case MOST_EVT_CTRL_TX_FAIL:
		return "ctrl-tx-fail";
	case MOST_EVT_ASYNC_RX:
		return "async-rx";
	case MOST_EVT_ASYNC_TX_DONE:
		return "async-tx-done";
	case MOST_EVT_ASYNC_TX_FAIL:
		return "async-tx-fail";
	case MOST_EVT_LINK_CHANGE:
		return "link-change";
	case MOST_EVT_HW_ERROR:
		return "hw-error";
	case MOST_EVT_TOPOLOGY_CHANGE:
		return "topology-change";
	default:
		return "unknown";
	}
}

int mostd_transport_decode_async_rx(const struct most_event *evt,
				       struct mostd_async_rx_image *image)
{
	if (!evt || !image) {
		errno = EINVAL;
		return -1;
	}

	if (evt->type != MOST_EVT_ASYNC_RX ||
	    !(evt->flags & MOST_EVT_FLAG_ASYNC_RX_RAW_MARP) ||
	    evt->len < 4) {
		errno = EPROTO;
		return -1;
	}

	image->target_addr = ((uint16_t)evt->data[0] << 8) | evt->data[1];
	image->source_addr = ((uint16_t)evt->data[2] << 8) | evt->data[3];
	image->payload = &evt->data[4];
	image->payload_capacity = evt->len - 4;
	return 0;
}
