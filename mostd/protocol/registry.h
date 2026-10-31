#ifndef MOSTD_PROTOCOL_REGISTRY_H
#define MOSTD_PROTOCOL_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "most.h"
#include "rx.h"

/*
 * NetworkMaster CentralRegistry decoder.
 *
 * The CentralRegistry STATUS telegram (FBlock 0x02 NetworkMaster, FktID 0x0A01,
 * OPType 0x0C) carries the full ring inventory as 4-byte records
 * { flags, node, fblock, inst }, where flags 0x01 = active entry and node is
 * the 1-byte physical ring position. A clean payload is 60 records = 240 bytes.
 *
 * mostd requests it once per link-up (runtime.c) and uses the decode to log the
 * inventory and confirm that our SatRadio FBlock 0x44 was registered.
 */

#define MOST_FB_NETWORKMASTER	0x02u
#define MOST_FKTID_CENTRAL_REGISTRY 0x0A01u
#define MOSTD_REGISTRY_MAX_ENTRIES 80u /* 60 records observed; margin */

struct mostd_registry_entry {
	uint8_t flags;
	uint8_t node;
	uint8_t fblock;
	uint8_t inst;
};

struct mostd_registry {
	struct mostd_registry_entry entries[MOSTD_REGISTRY_MAX_ENTRIES];
	size_t count;
	uint8_t valid;		/* decoded at least one full registry */
	uint32_t updates;	/* number of registry telegrams applied */
};

void mostd_registry_init(struct mostd_registry *reg);

/*
 * Feed an inbound message. Returns:
 *   0  not a CentralRegistry telegram (ignored)
 *   1  CentralRegistry decoded, no actionable change
 *   2  CentralRegistry decoded and the table became valid or changed
 */
int mostd_registry_handle_msg(struct mostd_registry *reg, const struct mostd_rx_msg *msg);

/* Send a Get(NetworkMaster.CentralRegistry) to request the registry. */
int mostd_registry_request(struct most_handle *handle, uint16_t nm_addr);

#endif /* MOSTD_PROTOCOL_REGISTRY_H */
