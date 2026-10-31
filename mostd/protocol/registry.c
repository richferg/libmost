#include "registry.h"

#include <string.h>

#include "../log.h"
#include "cmt.h"
#include "tx.h"

#define REGISTRY_FLAG_ACTIVE 0x01u

void mostd_registry_init(struct mostd_registry *reg)
{
	if (!reg)
		return;
	memset(reg, 0, sizeof(*reg));
}

int mostd_registry_handle_msg(struct mostd_registry *reg, const struct mostd_rx_msg *msg)
{
	const uint8_t *data;
	size_t len;
	size_t i;
	size_t new_count = 0;
	struct mostd_registry_entry parsed[MOSTD_REGISTRY_MAX_ENTRIES];
	int changed;

	if (!reg || !msg)
		return 0;

	if (msg->fblock != MOST_FB_NETWORKMASTER ||
	    msg->fktid != MOST_FKTID_CENTRAL_REGISTRY ||
	    msg->optype != MOST_OP_STATUS)
		return 0;

	data = msg->data;
	len = msg->tellen;

	/*
	 * Payload is N four-byte records (60 -> 240 bytes). The early-capture
	 * 241-byte "0x1C-prefixed" payloads were a driver misalignment that was
	 * fixed; a clean driver never emits them, so a non-multiple-of-4 length
	 * here means a real framing fault, not something to silently repair.
	 */
	if (len == 0u || len % 4u != 0u) {
		MOSTD_ERROR("registry: bad CentralRegistry payload len=%u (expect multiple of 4)",
			    msg->tellen);
		return 1;
	}

	for (i = 0; i + 4u <= len && new_count < MOSTD_REGISTRY_MAX_ENTRIES; i += 4u) {
		struct mostd_registry_entry *e = &parsed[new_count++];

		e->flags = data[i];
		e->node = data[i + 1u];
		e->fblock = data[i + 2u];
		e->inst = data[i + 3u];
	}

	if (i < len)
		MOSTD_ERROR("registry: %zu trailing record(s) dropped (cap %u)",
			    (len - i) / 4u, MOSTD_REGISTRY_MAX_ENTRIES);

	changed = (!reg->valid || new_count != reg->count ||
		   memcmp(parsed, reg->entries,
			  new_count * sizeof(parsed[0])) != 0);

	memcpy(reg->entries, parsed, new_count * sizeof(parsed[0]));
	reg->count = new_count;
	reg->valid = 1;
	reg->updates++;

	MOSTD_INFO("registry: CentralRegistry decoded %zu entries (update #%u, %s)",
		   reg->count, reg->updates, changed ? "changed" : "unchanged");

	/* Did our SDARS advertisement (FBlock 0x44) make it into the registry? */
	{
		size_t k;
		int found = 0;
		for (k = 0; k < reg->count; k++) {
			if (reg->entries[k].fblock == 0x44u) {
				MOSTD_INFO("registry: SDARS 0x44 registered -> node=0x%02X inst=0x%02X flags=0x%02X",
					   reg->entries[k].node, reg->entries[k].inst,
					   reg->entries[k].flags);
				found = 1;
			}
		}
		if (!found)
			MOSTD_INFO("registry: SDARS 0x44 NOT present in CentralRegistry");
	}

	return changed ? 2 : 1;
}

int mostd_registry_request(struct most_handle *handle, uint16_t nm_addr)
{
	int rc;

	if (!handle)
		return -1;

	rc = mostd_tx_op(handle, nm_addr, MOST_FB_NETWORKMASTER, 0x00,
			 MOST_FKTID_CENTRAL_REGISTRY, MOST_OP_GET, 0, NULL, 0);
	if (rc < 0) {
		MOSTD_ERROR("registry: CentralRegistry Get to 0x%04X failed rc=%d",
			    nm_addr, rc);
		return rc;
	}
	MOSTD_INFO("registry: requested CentralRegistry (Get) from NM addr=0x%04X", nm_addr);
	return 0;
}
