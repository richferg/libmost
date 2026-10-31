#include "addrbook.h"

#include <string.h>

#include "../log.h"

void mostd_addrbook_init(struct mostd_addrbook *book)
{
	if (book)
		memset(book, 0, sizeof(*book));
}

int mostd_addrbook_learn(struct mostd_addrbook *book, uint8_t fblock, uint8_t inst,
			 uint16_t addr)
{
	size_t i;

	if (!book || addr == 0x0000u)
		return 0;

	for (i = 0; i < book->count; i++) {
		struct mostd_addr_entry *e = &book->entries[i];

		if (e->fblock != fblock || e->inst != inst)
			continue;
		if (e->addr == addr)
			return 0; /* already known */
		MOSTD_INFO("addrbook: fb=0x%02X inst=0x%02X addr 0x%04X -> 0x%04X (moved)",
			   fblock, inst, e->addr, addr);
		e->addr = addr;
		book->learns++;
		return 1;
	}

	if (book->count >= MOSTD_ADDRBOOK_MAX) {
		MOSTD_ERROR("addrbook: full (max %u), dropping fb=0x%02X inst=0x%02X",
			    MOSTD_ADDRBOOK_MAX, fblock, inst);
		return 0;
	}

	book->entries[book->count].fblock = fblock;
	book->entries[book->count].inst = inst;
	book->entries[book->count].addr = addr;
	book->count++;
	book->learns++;
	MOSTD_INFO("addrbook: learned fb=0x%02X inst=0x%02X @ 0x%04X (%zu known)",
		   fblock, inst, addr, book->count);
	return 1;
}

int mostd_addrbook_observe(struct mostd_addrbook *book, const struct mostd_rx_msg *msg)
{
	if (!book || !msg)
		return 0;
	/*
	 * Only learn from responses. In a request (OPType 0x00..0x08: Get/Set/
	 * Start...) the FBlockID names the TARGET but the source is the requester,
	 * so learning from it maps an fblock to a client (seen live: a nav->phone
	 * Get taught fb=0x50 @ the nav). Responses (0x09..0x0F: Status/Result/
	 * Error/Interface) are sourced by the FBlock's host - the address we want.
	 */
	if (msg->optype < 0x09u)
		return 0;
	return mostd_addrbook_learn(book, msg->fblock, msg->inst, msg->source);
}

