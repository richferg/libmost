#ifndef MOSTD_PROTOCOL_ADDRBOOK_H
#define MOSTD_PROTOCOL_ADDRBOOK_H

#include <stddef.h>
#include <stdint.h>

#include "rx.h"

/*
 * Learned logical-address book.
 *
 * Records fblock+inst -> logical address from the source of every inbound
 * response (requests are sourced by the requester, so they are ignored). The
 * log line on each new or moved entry is the live map of which node hosts
 * which FBlock on this ring.
 */

#define MOSTD_ADDRBOOK_MAX 96u

struct mostd_addr_entry {
	uint8_t fblock;
	uint8_t inst;
	uint16_t addr;	/* learned logical (source) address */
};

struct mostd_addrbook {
	struct mostd_addr_entry entries[MOSTD_ADDRBOOK_MAX];
	size_t count;
	uint32_t learns;	/* inserts + updates applied */
};

void mostd_addrbook_init(struct mostd_addrbook *book);

/*
 * Record that (fblock, inst) was seen sourced from addr. Ignores addr 0x0000.
 * Returns 1 if a new mapping was added or an existing one changed, else 0.
 */
int mostd_addrbook_learn(struct mostd_addrbook *book, uint8_t fblock, uint8_t inst,
			 uint16_t addr);

/* Convenience: learn from a parsed inbound message (uses msg->source). */
int mostd_addrbook_observe(struct mostd_addrbook *book, const struct mostd_rx_msg *msg);

#endif /* MOSTD_PROTOCOL_ADDRBOOK_H */
