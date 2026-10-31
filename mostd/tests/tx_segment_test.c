#include <assert.h>
#include <stdint.h>
#include <string.h>

int mostd_log_level;

#include "../protocol/tx.c"

static uint8_t frames[64][MOST_FRAME_LEN];
static size_t frame_count;

int most_write(struct most_handle *handle, const uint8_t buf[MOST_FRAME_LEN])
{
	(void)handle;
	assert(frame_count < sizeof(frames) / sizeof(frames[0]));
	memcpy(frames[frame_count++], buf, MOST_FRAME_LEN);
	return 0;
}

char *mostd_payload_hex_alloc(const uint8_t *data, size_t tellen)
{
	(void)data;
	(void)tellen;
	return NULL;
}

void mostd_trace_cmt(const char *dir, uint16_t addr, uint8_t fblock, uint8_t inst,
		     uint16_t fktid, uint8_t op, uint8_t telid, const uint8_t *data,
		     size_t tellen, int error_code, int error_info, const char *result)
{
	(void)dir;
	(void)addr;
	(void)fblock;
	(void)inst;
	(void)fktid;
	(void)op;
	(void)telid;
	(void)data;
	(void)tellen;
	(void)error_code;
	(void)error_info;
	(void)result;
}

/* Send `len` counting bytes and reassemble what went on the wire. */
static size_t send_and_reassemble(size_t len, uint8_t *out)
{
	uint8_t payload[512] = {0};
	size_t i, n = 0;

	for (i = 0; i < len; i++)
		payload[i] = (uint8_t)(i + 1u);
	frame_count = 0;
	assert(mostd_tx_op(NULL, 0x0102, 0x44, 0x01, 0xD10, MOST_OP_STATUS, 0,
			   payload, len) == 0);

	for (i = 0; i < frame_count; i++) {
		const uint8_t *frm = frames[i];
		uint8_t telid = most_tel_id(frm[MOST_OFS_TEL]);
		uint8_t tellen = most_tel_len(frm[MOST_OFS_TEL]);

		/* TelLen never claims more than the 12 data bytes a frame has. */
		assert(tellen <= MOST_FRAME_LEN - MOST_OFS_DATA);
		if (telid == 0) {
			assert(frame_count == 1);
			memcpy(&out[n], &frm[MOST_OFS_DATA], tellen);
			n += tellen;
			continue;
		}
		assert(telid == (i == 0 ? 1 : (i + 1 == frame_count ? 3 : 2)));
		assert(frm[MOST_OFS_DATA] == (uint8_t)i);	/* segment counter */
		memcpy(&out[n], &frm[MOST_OFS_DATA + 1], tellen - 1u);
		n += tellen - 1u;
	}
	assert(n == len);
	for (i = 0; i < len; i++)
		assert(out[i] == (uint8_t)(i + 1u));
	return frame_count;
}

int main(void)
{
	uint8_t out[512];

	assert(send_and_reassemble(0, out) == 1);
	assert(send_and_reassemble(12, out) == 1);
	/* 13-15 bytes used to go out as one frame, overflowing it and losing the
	 * tail (14-byte window rows, 12-char ESN strings). */
	assert(send_and_reassemble(13, out) == 2);
	assert(send_and_reassemble(14, out) == 2);
	assert(send_and_reassemble(15, out) == 2);
	assert(send_and_reassemble(22, out) == 2);
	assert(send_and_reassemble(23, out) == 3);
	assert(send_and_reassemble(289, out) == 27);	/* a full 20-row window */
	return 0;
}
