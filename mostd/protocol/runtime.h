#ifndef MOSTD_PROTOCOL_RUNTIME_H
#define MOSTD_PROTOCOL_RUNTIME_H

#include <stdint.h>

#include "most.h"
#include "addrbook.h"
#include "diagnosis.h"
#include "netblock_server.h"
#include "registry.h"
#include "sdars.h"
#include "transport.h"

struct mostd_runtime_stats {
	uint32_t ctrl_rx_frames;
	uint32_t ctrl_rx_messages;
	uint32_t ctrl_tx_done;
	uint32_t ctrl_tx_fail;
	uint32_t link_change_events;
	uint32_t topology_change_events;
	uint32_t hw_error_events;
};

struct mostd_runtime {
	struct most_handle *handle;
	enum most_link_state link_state;
	uint32_t last_event_seq;
	uint16_t last_topology_aux;
	uint16_t last_hw_error_aux;
	int last_hw_error_status;
	uint64_t last_hw_error_log_ms;
	uint32_t hw_error_suppressed;
	uint8_t last_hw_error_len;
	uint8_t last_hw_error_data[3];
	uint8_t last_hw_error_valid;
	uint8_t registry_armed;		/* link up: waiting to request the registry */
	uint8_t registry_requested;	/* requested it for this link-up session */
	uint16_t link_up_ticks;		/* ticks since link up (registry fallback) */
	struct mostd_runtime_stats stats;
	struct mostd_addrbook addrbook;
	struct mostd_registry registry;
	struct mostd_netblock_server netblock_server;
	struct mostd_diagnosis diagnosis;
	struct mostd_sdars sdars;
};

void mostd_runtime_init(struct mostd_runtime *runtime, struct most_handle *handle);
void mostd_runtime_handle_event(struct mostd_runtime *runtime, const struct most_event *evt);
void mostd_runtime_tick(struct mostd_runtime *runtime);

#endif /* MOSTD_PROTOCOL_RUNTIME_H */
