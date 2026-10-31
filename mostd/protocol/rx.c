#include "rx.h"

#include <string.h>

#include "cmt.h"
#include "frame_layout.h"

#define MOSTD_RX_SLOTS 8

struct rx_slot {
	uint8_t active;
	uint8_t msg_type;
	uint16_t source;
	uint8_t fblock;
	uint8_t inst;
	uint16_t fktid;
	uint8_t optype;
	uint8_t telid;
	uint8_t counter;
	uint16_t payload_len;
	uint8_t payload[MOSTD_RX_MAX_PAYLOAD];
};

static struct rx_slot g_slots[MOSTD_RX_SLOTS];

static void decode_frame_header(const uint8_t raw[MOST_FRAME_LEN], struct mostd_rx_msg *m,
				uint8_t *telid, uint8_t *tellen)
{
	m->msg_type = raw[MOST_OFS_MSGTYPE];
	m->source = most_source_from_frame(raw);
	m->fblock = raw[MOST_OFS_FBLOCK];
	m->inst = raw[MOST_OFS_INST];
	m->fktid = most_cmt_fktid(raw[MOST_OFS_FKTID_HI], raw[MOST_OFS_FKTID_LO_OP]);
	m->optype = most_cmt_optype(raw[MOST_OFS_FKTID_LO_OP]);
	m->telid = most_tel_id(raw[MOST_OFS_TEL]);
	*telid = m->telid;
	*tellen = most_tel_len(raw[MOST_OFS_TEL]);
}

static int slot_match(const struct rx_slot *s, const struct mostd_rx_msg *m)
{
	return s->active &&
		s->source == m->source &&
		s->fblock == m->fblock &&
		s->inst == m->inst &&
		s->fktid == m->fktid &&
		s->optype == m->optype;
}

static struct rx_slot *find_slot(const struct mostd_rx_msg *m)
{
	int i;

	for (i = 0; i < MOSTD_RX_SLOTS; i++) {
		if (slot_match(&g_slots[i], m))
			return &g_slots[i];
	}
	return NULL;
}

static struct rx_slot *alloc_slot(void)
{
	int i;
	struct rx_slot *oldest = &g_slots[0];

	for (i = 0; i < MOSTD_RX_SLOTS; i++) {
		if (!g_slots[i].active)
			return &g_slots[i];
	}

	/* No free slot: recycle first one. */
	for (i = 1; i < MOSTD_RX_SLOTS; i++) {
		if (g_slots[i].source < oldest->source)
			oldest = &g_slots[i];
	}
	return oldest;
}

void mostd_rx_init(void)
{
	memset(g_slots, 0, sizeof(g_slots));
}

void mostd_rx_process_frame(const uint8_t raw[MOST_FRAME_LEN], mostd_rx_cb_t cb, void *userdata)
{
	struct mostd_rx_msg msg;
	struct rx_slot *slot;
	uint8_t telid;
	uint8_t tellen;
	uint8_t counter;
	uint16_t chunk_len;

	if (!raw || !cb)
		return;

	memset(&msg, 0, sizeof(msg));
	decode_frame_header(raw, &msg, &telid, &tellen);

	if (telid == 0) {
		msg.tellen = tellen;
		if (msg.tellen > 0)
			memcpy(msg.data, &raw[MOST_OFS_DATA], msg.tellen);
		msg.segmented = 0;
		cb(&msg, userdata);
		return;
	}

	if (tellen == 0)
		return;

	counter = raw[MOST_OFS_DATA];
	chunk_len = (uint16_t)(tellen - 1);

	if (telid == 1) {
		slot = alloc_slot();
		memset(slot, 0, sizeof(*slot));
		slot->active = 1;
		slot->msg_type = msg.msg_type;
		slot->source = msg.source;
		slot->fblock = msg.fblock;
		slot->inst = msg.inst;
		slot->fktid = msg.fktid;
		slot->optype = msg.optype;
		slot->telid = msg.telid;
		slot->counter = counter;
		slot->payload_len = 0;

		if (chunk_len > MOSTD_RX_MAX_PAYLOAD) {
			slot->active = 0;
			return;
		}
		if (chunk_len > 0) {
			memcpy(slot->payload, &raw[MOST_OFS_DATA + 1], chunk_len);
			slot->payload_len = chunk_len;
		}
		return;
	}

	if (telid != 2 && telid != 3)
		return;

	slot = find_slot(&msg);
	if (!slot)
		return;

	if (counter != (uint8_t)(slot->counter + 1)) {
		slot->active = 0;
		return;
	}

	if ((uint32_t)slot->payload_len + chunk_len > MOSTD_RX_MAX_PAYLOAD) {
		slot->active = 0;
		return;
	}

	if (chunk_len > 0) {
		memcpy(&slot->payload[slot->payload_len], &raw[MOST_OFS_DATA + 1], chunk_len);
		slot->payload_len = (uint16_t)(slot->payload_len + chunk_len);
	}
	slot->counter = counter;

	if (telid == 3) {
		msg.msg_type = slot->msg_type;
		msg.source = slot->source;
		msg.fblock = slot->fblock;
		msg.inst = slot->inst;
		msg.fktid = slot->fktid;
		msg.optype = slot->optype;
		msg.telid = slot->telid;
		msg.tellen = slot->payload_len;
		msg.segmented = 1;
		if (msg.tellen > 0)
			memcpy(msg.data, slot->payload, msg.tellen);
		slot->active = 0;
		cb(&msg, userdata);
	}
}
