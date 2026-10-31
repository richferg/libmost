#ifndef MOST_PROTOCOL_CMT_H
#define MOST_PROTOCOL_CMT_H

#include <stdint.h>

/* ------------------------------------------------------------
 * MOST25 Operation Types (4-bit OPType field)
 * Meanings depend on function type (property vs method), per
 * MOST Book 2 table 4.4.
 * ------------------------------------------------------------ */
enum most_optype {
	/* Requests (properties) */
	MOST_OP_SET          = 0x00,
	MOST_OP_GET          = 0x01,
	MOST_OP_SETGET       = 0x02,
	MOST_OP_INCREMENT   = 0x03,
	MOST_OP_DECREMENT   = 0x04,
	MOST_OP_GETINTERFACE= 0x05,

	/* Requests (methods, preferred ACK-based forms) */
	MOST_OP_STARTRESULTACK = 0x06,
	MOST_OP_ABORTACK       = 0x07,
	MOST_OP_STARTACK       = 0x08,

	/* Requests (methods, deprecated no-SenderHandle forms) */
	MOST_OP_START        = 0x00, /* alias */
	MOST_OP_ABORT        = 0x01, /* deprecated alias */
	MOST_OP_STARTRESULT  = 0x02, /* deprecated alias */

	/* Responses */
	MOST_OP_STATUS       = 0x0C,
	MOST_OP_ERRORACK     = 0x09,
	MOST_OP_PROCESSINGACK = 0x0A,
	MOST_OP_PROCESSING    = 0x0B, /* deprecated */
	MOST_OP_RESULT        = 0x0C, /* deprecated alias */
	MOST_OP_RESULTACK     = 0x0D,
	MOST_OP_INTERFACE     = 0x0E,
	MOST_OP_ERROR        = 0x0F,
};

/* ------------------------------------------------------------
 * CMT field packing helpers
 * ------------------------------------------------------------ */
static inline uint16_t most_cmt_fktid(uint8_t b6, uint8_t b7)
{
	return (b6 << 4) | (b7 >> 4);
}

static inline uint8_t most_cmt_optype(uint8_t b7)
{
	return b7 & 0x0F;
}

static inline uint8_t most_cmt_fktid_hi(uint16_t fktid)
{
	return (fktid >> 4) & 0xFF;
}

static inline uint8_t most_cmt_fktid_lo_op(uint16_t fktid, uint8_t optype)
{
	return ((fktid & 0x0F) << 4) | (optype & 0x0F);
}

#endif /* MOST_PROTOCOL_CMT_H */
