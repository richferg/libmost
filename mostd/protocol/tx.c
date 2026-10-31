#include "tx.h"

#include "most.h"
#include "cmt.h"
#include "frame_layout.h"
#include "../log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * MOST25 has one ctrl TX slot and the driver returns EBUSY while a frame is in
 * flight. A reply often has to go out before CTRL_TX_DONE for the previous
 * frame has propagated back to us, so retry up to TX_BUSY_RETRIES x
 * TX_BUSY_WAIT_US rather than drop it.
 */
#define TX_BUSY_RETRIES  40
#define TX_BUSY_WAIT_US  3000

/*
 * The 21-byte ctrl frame has 12 data bytes after the TelID/TelLen byte. A
 * payload that fits goes out as one TelID-0 frame; anything longer is
 * segmented, each segment spending one data byte on the segment counter.
 */
#define TX_SINGLE_MAX    (MOST_FRAME_LEN - MOST_OFS_DATA)
#define TX_SEGMENT_MAX   (TX_SINGLE_MAX - 1)

static void mostd_tx_log(uint16_t addr, uint8_t fblock, uint8_t inst, uint16_t fktid,
			 uint8_t op, uint8_t telid, const uint8_t *payload, size_t payload_len,
			 const char *result, int is_error)
{
	char *payload_hex;

	payload_hex = mostd_payload_hex_alloc(payload, payload_len);
	if (!payload_hex)
		payload_hex = "-";

	if (is_error) {
		MOSTD_ERROR("ctrl tx addr=0x%04X fb=0x%02X inst=0x%02X fkt=0x%03X op=0x%X telid=%u len=%zu payload=%s result=%s",
			    addr, fblock, inst, fktid, op, telid, payload_len, payload_hex,
			    result ? result : "error");
	} else {
		MOSTD_DEBUG("ctrl tx addr=0x%04X fb=0x%02X inst=0x%02X fkt=0x%03X op=0x%X telid=%u len=%zu payload=%s result=%s",
			    addr, fblock, inst, fktid, op, telid, payload_len, payload_hex,
			    result ? result : "sent");
	}

	if (payload_hex[0] != '-' || payload_hex[1] != '\0')
		free(payload_hex);
}

/* Write one ctrl frame, retrying while the single MOST25 TX slot is busy. */
static int tx_write_retry(struct most_handle *h, const uint8_t *frm)
{
	int attempt;

	for (attempt = 0; attempt < TX_BUSY_RETRIES; attempt++) {
		if (most_write(h, frm) == 0)
			return 0;
		if (errno != EBUSY && errno != EAGAIN)
			return -1;
		if (attempt + 1 < TX_BUSY_RETRIES)
			usleep(TX_BUSY_WAIT_US);
	}
	return -1;
}

static int tx_single(struct most_handle *h, uint16_t addr, uint8_t fblock, uint8_t inst,
		     uint16_t fktid, uint8_t op, uint8_t telid, const uint8_t *payload,
		     size_t payload_len)
{
	uint8_t frm[MOST_FRAME_LEN] = {0};

	frm[MOST_OFS_PRIORITY] = 0x00;
	frm[MOST_OFS_MSGTYPE] = 0x00;
	frm[MOST_OFS_SOURCE_HI] = addr >> 8;
	frm[MOST_OFS_SOURCE_LO] = addr & 0xff;
	frm[MOST_OFS_FBLOCK] = fblock;
	frm[MOST_OFS_INST] = inst;
	frm[MOST_OFS_FKTID_HI] = most_cmt_fktid_hi(fktid);
	frm[MOST_OFS_FKTID_LO_OP] = most_cmt_fktid_lo_op(fktid, op);
	frm[MOST_OFS_TEL] = most_tel_pack(telid, (uint8_t)payload_len);

	if (payload_len && payload)
		memcpy(&frm[MOST_OFS_DATA], payload, payload_len);

	return tx_write_retry(h, frm);
}

int mostd_tx_op(struct most_handle *h, uint16_t addr, uint8_t fblock, uint8_t inst,
		uint16_t fktid, uint8_t op, uint8_t telid, const uint8_t *payload, size_t payload_len)
{
	size_t ofs = 0;
	uint8_t seg_counter = 0;

	if (payload_len <= TX_SINGLE_MAX) {
		if (tx_single(h, addr, fblock, inst, fktid, op, telid, payload, payload_len) < 0) {
			char result[64];
			int saved_errno = errno;

			snprintf(result, sizeof(result), "write_error errno=%d (%s)",
				 saved_errno, strerror(saved_errno));
			mostd_trace_cmt("tx", addr, fblock, inst, fktid, op, telid,
					payload, payload_len, -1, -1, result);
			mostd_tx_log(addr, fblock, inst, fktid, op, telid, payload, payload_len,
				     result, 1);
			errno = saved_errno;
			return -1;
		}
		mostd_trace_cmt("tx", addr, fblock, inst, fktid, op, telid,
				payload, payload_len, -1, -1, "sent");
		mostd_tx_log(addr, fblock, inst, fktid, op, telid, payload, payload_len, "sent", 0);
		return 0;
	}

	while (ofs < payload_len) {
		size_t remain = payload_len - ofs;
		size_t chunk = (remain > TX_SEGMENT_MAX) ? TX_SEGMENT_MAX : remain;
		uint8_t seg_telid;
		uint8_t seg_tellen;
		uint8_t seg_frame[MOST_FRAME_LEN] = {0};

		if (ofs == 0)
			seg_telid = 1;
		else
			seg_telid = (remain > TX_SEGMENT_MAX) ? 2 : 3;

		seg_tellen = (uint8_t)(chunk + 1);

		seg_frame[MOST_OFS_PRIORITY] = 0x00;
		seg_frame[MOST_OFS_MSGTYPE] = 0x00;
		seg_frame[MOST_OFS_SOURCE_HI] = addr >> 8;
		seg_frame[MOST_OFS_SOURCE_LO] = addr & 0xff;
		seg_frame[MOST_OFS_FBLOCK] = fblock;
		seg_frame[MOST_OFS_INST] = inst;
		seg_frame[MOST_OFS_FKTID_HI] = most_cmt_fktid_hi(fktid);
		seg_frame[MOST_OFS_FKTID_LO_OP] = most_cmt_fktid_lo_op(fktid, op);
		seg_frame[MOST_OFS_TEL] = most_tel_pack(seg_telid, seg_tellen);
		seg_frame[MOST_OFS_DATA] = seg_counter;
		memcpy(&seg_frame[MOST_OFS_DATA + 1], &payload[ofs], chunk);

		if (tx_write_retry(h, seg_frame) < 0) {
			char result[74];
			int saved_errno = errno;

			snprintf(result, sizeof(result), "write_error_segmented errno=%d (%s)",
				 saved_errno, strerror(saved_errno));
			mostd_trace_cmt("tx", addr, fblock, inst, fktid, op, seg_telid,
					payload, payload_len, -1, -1, result);
			mostd_tx_log(addr, fblock, inst, fktid, op, seg_telid, payload, payload_len,
				     result, 1);
			errno = saved_errno;
			return -1;
		}

		ofs += chunk;
		seg_counter++;
	}

	mostd_trace_cmt("tx", addr, fblock, inst, fktid, op, 0,
			payload, payload_len, -1, -1, "sent_segmented");
	mostd_tx_log(addr, fblock, inst, fktid, op, 0, payload, payload_len, "sent_segmented", 0);
	return 0;
}

int mostd_tx_status(struct most_handle *h, uint16_t addr, uint8_t fblock, uint8_t inst,
		    uint16_t fktid, uint8_t telid, const uint8_t *payload, size_t payload_len)
{
	return mostd_tx_op(h, addr, fblock, inst, fktid, MOST_OP_STATUS, telid, payload, payload_len);
}

int mostd_tx_error_for_request(struct most_handle *h, uint16_t addr, uint8_t fblock,
			       uint8_t inst, uint16_t fktid, uint8_t telid, uint8_t req_op,
			       const uint8_t *req_payload, size_t req_len,
			       uint8_t error_code, uint8_t error_info)
{
	uint8_t payload[4];
	uint8_t op = MOST_OP_ERROR;
	size_t len = 2;

	payload[0] = error_code;
	payload[1] = error_info;

	if ((req_op == MOST_OP_STARTRESULTACK || req_op == MOST_OP_STARTACK ||
	     req_op == MOST_OP_ABORTACK) && req_payload && req_len >= 2) {
		payload[0] = req_payload[0];
		payload[1] = req_payload[1];
		payload[2] = error_code;
		payload[3] = error_info;
		op = MOST_OP_ERRORACK;
		len = 4;
	}

	if (mostd_tx_op(h, addr, fblock, inst, fktid, op, telid, payload, len) < 0)
		return -1;

	return 0;
}

