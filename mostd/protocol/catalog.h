#ifndef MOSTD_PROTOCOL_CATALOG_H
#define MOSTD_PROTOCOL_CATALOG_H

#include <stddef.h>
#include <stdint.h>

/*
 * FBlock telegram catalog: per (fblock, fktid, optype) field layout, used to
 * decode inbound Status payloads into named fields.
 *
 * Seeded from the most-wince-research telegram layouts. FbNavigation Status
 * telegrams are add*-framed: a leading u16 "presence" word precedes the fields
 * (has_presence=1). The current decoder handles the "all fields present" case
 * (what a node sends during active guidance); a partial presence subset is
 * logged raw for now.
 */

enum mostd_field_type {
	MFLD_U8,
	MFLD_U16BE,
	MFLD_U32BE,
	MFLD_I32BE,	/* signed (lon/lat coordinates) */
	MFLD_BLOB,	/* variable: consumes the rest of the payload (must be last) */
};

struct mostd_field {
	const char *name;
	enum mostd_field_type type;
};

struct mostd_telegram_desc {
	uint8_t fblock;
	uint16_t fktid;
	uint8_t optype;
	const char *name;
	uint8_t has_presence;	/* leading add* u16 presence word */
	const struct mostd_field *fields;
	size_t field_count;
};

size_t mostd_field_size(enum mostd_field_type type);

/* Find a descriptor, or NULL. */
const struct mostd_telegram_desc *
mostd_catalog_lookup(uint8_t fblock, uint16_t fktid, uint8_t optype);

#endif /* MOSTD_PROTOCOL_CATALOG_H */
