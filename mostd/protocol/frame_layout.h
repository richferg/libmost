#ifndef MOST_PROTOCOL_FRAME_LAYOUT_H
#define MOST_PROTOCOL_FRAME_LAYOUT_H

#include <stdint.h>

/* Raw 21-byte MOST control frame byte offsets. */
enum most_frame_offset {
	MOST_OFS_PRIORITY = 0,
	MOST_OFS_MSGTYPE = 1,
	MOST_OFS_SOURCE_HI = 2,
	MOST_OFS_SOURCE_LO = 3,
	MOST_OFS_FBLOCK = 4,
	MOST_OFS_INST = 5,
	MOST_OFS_FKTID_HI = 6,
	MOST_OFS_FKTID_LO_OP = 7,
	MOST_OFS_TEL = 8,
	MOST_OFS_DATA = 9,
};

static inline uint8_t most_tel_pack(uint8_t telid, uint8_t tellen)
{
	return ((telid & 0x0F) << 4) | (tellen & 0x0F);
}

static inline uint8_t most_tel_id(uint8_t tel)
{
	return tel >> 4;
}

static inline uint8_t most_tel_len(uint8_t tel)
{
	return tel & 0x0F;
}

static inline uint16_t most_source_from_frame(const uint8_t *raw)
{
	return ((uint16_t)raw[MOST_OFS_SOURCE_HI] << 8) | raw[MOST_OFS_SOURCE_LO];
}

#endif /* MOST_PROTOCOL_FRAME_LAYOUT_H */
