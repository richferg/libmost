#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int mostd_log_level;

/*
 * Include the implementation so the tests exercise the static builders and
 * handlers directly. Link-time section garbage collection drops hardware paths.
 */
#include "../protocol/sdars.c"

#define MMI_FRONT	0x0102u
#define MMI_REAR	0x0103u

struct captured_tx {
	uint16_t device;
	uint16_t fktid;
	uint8_t fblock;
	uint8_t inst;
	uint8_t op;
	uint8_t payload[SDARS_TX_MAX];
	size_t length;
};

static struct captured_tx captured[64];
static size_t captured_count;
static size_t error_count;
static uint8_t last_error_op;
static char events[32][96];
static size_t event_count;

static void reset_capture(void)
{
	captured_count = 0;
	error_count = 0;
	event_count = 0;
}

static void event_sink(void *ctx, const char *event)
{
	(void)ctx;
	assert(event_count < sizeof(events) / sizeof(events[0]));
	snprintf(events[event_count++], sizeof(events[0]), "%s", event);
}

static const char *last_event(void)
{
	assert(event_count > 0);
	return events[event_count - 1];
}

static size_t count_events(const char *prefix)
{
	size_t i, n = 0;

	for (i = 0; i < event_count; i++)
		if (strncmp(events[i], prefix, strlen(prefix)) == 0)
			n++;
	return n;
}

int mostd_tx_op(struct most_handle *handle, uint16_t device, uint8_t fblock,
		uint8_t inst, uint16_t fktid, uint8_t op, uint8_t telid,
		const uint8_t *payload, size_t length)
{
	struct captured_tx *tx;

	(void)handle;
	(void)telid;
	assert(captured_count < sizeof(captured) / sizeof(captured[0]));
	assert(length <= sizeof(captured[0].payload));
	tx = &captured[captured_count++];
	tx->device = device;
	tx->fktid = fktid;
	tx->fblock = fblock;
	tx->inst = inst;
	tx->op = op;
	tx->length = length;
	if (length)
		memcpy(tx->payload, payload, length);
	return 0;
}

int mostd_tx_status(struct most_handle *handle, uint16_t device, uint8_t fblock,
		    uint8_t inst, uint16_t fktid, uint8_t telid,
		    const uint8_t *payload, size_t length)
{
	return mostd_tx_op(handle, device, fblock, inst, fktid, MOST_OP_STATUS, telid,
			   payload, length);
}

int mostd_tx_error_for_request(struct most_handle *handle, uint16_t device,
			       uint8_t fblock, uint8_t inst, uint16_t fktid,
			       uint8_t telid, uint8_t request_op,
			       const uint8_t *request_payload, size_t request_length,
			       uint8_t error_code, uint8_t error_info)
{
	(void)handle;
	(void)device;
	(void)fblock;
	(void)inst;
	(void)fktid;
	(void)telid;
	(void)request_payload;
	(void)request_length;
	(void)error_code;
	(void)error_info;
	last_error_op = request_op;
	error_count++;
	return 0;
}

char *mostd_payload_hex_alloc(const uint8_t *data, size_t tellen)
{
	(void)data;
	(void)tellen;
	return NULL;
}

int most_write(struct most_handle *handle, const uint8_t buf[MOST_FRAME_LEN])
{
	(void)handle;
	(void)buf;
	assert(!"unexpected INIC write");
	return -1;
}

int most_reg_read(struct most_handle *handle, uint16_t reg, void *buf, uint16_t len)
{
	(void)handle;
	(void)reg;
	(void)buf;
	(void)len;
	assert(!"unexpected INIC register read");
	return -1;
}

int most_reg_write(struct most_handle *handle, uint16_t reg,
		   const void *buf, uint16_t len)
{
	(void)handle;
	(void)reg;
	(void)buf;
	(void)len;
	assert(!"unexpected INIC register write");
	return -1;
}

/* ---- The MMI's receive grammar (mmi_satradio_most.md §3, §4) -------
 * A small re-implementation of what the 9172146A parsers accept, so every TX
 * builder can be checked against the consumer instead of against itself. */

struct cursor {
	const uint8_t *p;
	const uint8_t *end;
	int ok;
};

static struct cursor cursor_of(const struct captured_tx *tx)
{
	struct cursor c = { tx->payload, tx->payload + tx->length, 1 };

	return c;
}

static uint8_t rd8(struct cursor *c)
{
	if (c->p >= c->end) {
		c->ok = 0;
		return 0;
	}
	return *c->p++;
}

static uint16_t rd16(struct cursor *c)
{
	uint16_t hi = rd8(c);

	return (uint16_t)((hi << 8) | rd8(c));
}

/* String: ident 00/01/02, chars, 00. Returns the char count. */
static size_t rd_string(struct cursor *c, size_t max, char *out)
{
	size_t len = 0;
	uint8_t ch;

	if (rd8(c) > 0x02u)
		c->ok = 0;
	while (c->ok && (ch = rd8(c)) != 0x00) {
		if (out && len < 63u)
			out[len] = (char)ch;
		len++;
	}
	if (out)
		out[len < 63u ? len : 63u] = '\0';
	if (len > max)
		c->ok = 0;
	return len;
}

/* Field spec: 'b' byte/bool/enum, 's' string<=8, 'l' <=20, 't' <=36, 'e' <=12. */
static void rd_fields(struct cursor *c, const char *spec)
{
	for (; *spec && c->ok; spec++) {
		switch (*spec) {
		case 'b': rd8(c); break;
		case 's': rd_string(c, 8u, NULL); break;
		case 'l': rd_string(c, 20u, NULL); break;
		case 't': rd_string(c, 36u, NULL); break;
		case 'e': rd_string(c, 12u, NULL); break;
		default: assert(!"bad spec");
		}
	}
}

/* Single: bare fields, exact length. */
static int mmi_single(const struct captured_tx *tx, const char *spec)
{
	struct cursor c = cursor_of(tx);

	rd_fields(&c, spec);
	return c.ok && c.p == c.end;
}

/* Record: 00 00 + all fields, exact length. */
static int mmi_record(const struct captured_tx *tx, const char *spec)
{
	struct cursor c = cursor_of(tx);

	if (rd8(&c) != 0x00 || rd8(&c) != 0x00)
		return 0;
	rd_fields(&c, spec);
	return c.ok && c.p == c.end;
}

/* Array: 00 00 + whole rows until the message ends. Returns rows, -1 on error. */
static int mmi_array(const struct captured_tx *tx, const char *spec, int max_rows)
{
	struct cursor c = cursor_of(tx);
	int rows = 0;

	if (rd8(&c) != 0x00 || rd8(&c) != 0x00)
		return -1;
	while (c.ok && c.p < c.end) {
		rd_fields(&c, spec);
		rows++;
	}
	return (c.ok && rows <= max_rows) ? rows : -1;
}

struct mmi_window {
	uint16_t total;
	uint16_t abs_pos;
	int rows;
	uint16_t tag[32];
	uint8_t station[32];
	char name[32][64];
};

/* LongArray window Status. Returns 1 if the MMI would accept it. */
static int mmi_window(const struct captured_tx *tx, uint8_t window_size,
		      struct mmi_window *w)
{
	struct cursor c = cursor_of(tx);
	uint16_t tag;

	memset(w, 0, sizeof(*w));
	if (rd16(&c) != 0x0000u || rd8(&c) != 0x00)
		return 0;
	w->total = rd16(&c);
	w->abs_pos = rd16(&c);
	while (c.ok && (tag = rd16(&c)) != 0xFFFFu) {
		int i;

		if (w->rows >= window_size || w->rows >= 32)
			return 0;
		for (i = 0; i < w->rows; i++)
			if (w->tag[i] == tag)
				return 0;	/* element tags must be unique */
		w->tag[w->rows] = tag;
		w->station[w->rows] = rd8(&c);
		rd_string(&c, 8u, w->name[w->rows]);
		rd8(&c);
		w->rows++;
	}
	return c.ok && c.p == c.end;	/* FFFF end tag found, nothing after it */
}

/* ---- helpers to drive the emulator like the MMI does -------------------- */

static void sdars_setup(struct mostd_sdars *s)
{
	mostd_sdars_init(s, (struct most_handle *)(uintptr_t)1);
	mostd_sdars_set_event_sink(s, event_sink, NULL);
	reset_capture();
}

static uint8_t rx_inst = SDARS_INST;

static void rx(struct mostd_sdars *s, uint16_t src, uint16_t fktid, uint8_t op,
	       const uint8_t *payload, size_t len)
{
	static struct mostd_rx_msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.source = src;
	msg.fblock = SDARS_FBLOCK;
	msg.inst = rx_inst;
	msg.fktid = fktid;
	msg.optype = op;
	msg.tellen = (uint16_t)len;
	if (len)
		memcpy(msg.data, payload, len);
	assert(mostd_sdars_handle_msg(s, &msg) == 1);
}

/* Notification.Set as the MMI builds it: 01 | DeviceID | FktID, one per message. */
static void subscribe(struct mostd_sdars *s, uint16_t dev, uint16_t fktid)
{
	uint8_t p[5] = { NOTIFY_SET_FUNCTION, (uint8_t)(dev >> 8), (uint8_t)dev,
			 (uint8_t)(fktid >> 8), (uint8_t)fktid };

	rx(s, dev, FKT_NOTIFICATION, MOST_OP_SET, p, sizeof(p));
}

static void create_window(struct mostd_sdars *s, uint16_t dev, uint8_t wsize)
{
	uint8_t p[7] = { 0x0C, 0x02, 0x0C, 0x02, 0x00, 0x00, wsize };

	rx(s, dev, FKT_CREATE_ARRAY_WINDOW, MOST_OP_STARTRESULTACK, p, sizeof(p));
}

static void move_window(struct mostd_sdars *s, uint16_t dev, uint16_t window,
			uint8_t mode, uint16_t steps)
{
	uint8_t p[9] = { (uint8_t)(window >> 8), (uint8_t)window,
			 (uint8_t)(window >> 8), (uint8_t)window, mode,
			 (uint8_t)(steps >> 8), (uint8_t)steps, 0x00, 0x00 };

	rx(s, dev, FKT_MOVE_ARRAY_WINDOW, MOST_OP_STARTACK, p, sizeof(p));
}

static const struct captured_tx *last_tx(void)
{
	assert(captured_count > 0);
	return &captured[captured_count - 1];
}

/* Number of captured messages to `dev` on `fktid`; *out = the last one. */
static size_t count_tx(uint16_t dev, uint16_t fktid, const struct captured_tx **out)
{
	size_t i, n = 0;

	for (i = 0; i < captured_count; i++)
		if (captured[i].device == dev && captured[i].fktid == fktid) {
			n++;
			if (out)
				*out = &captured[i];
		}
	return n;
}

/* ---- tests -------------------------------------------------------------- */

/* Every subscribed property must publish a Status the MMI parser accepts. */
static void test_subscribed_properties_parse(void)
{
	static const uint8_t service_status[] = { 0x00, 0x00, 0x01, 0x01 };
	struct mostd_sdars s;
	const struct captured_tx *tx = NULL;

	sdars_setup(&s);

	subscribe(&s, MMI_FRONT, 0x400);
	assert(count_tx(MMI_FRONT, 0x400, &tx) == 1);
	assert(tx->op == MOST_OP_STATUS);
	assert(tx->length == sizeof(service_status));
	assert(memcmp(tx->payload, service_status, sizeof(service_status)) == 0);
	assert(mmi_record(tx, "bb"));
	/* No unsolicited 0x200 rides along with the 0x400 subscription. */
	assert(captured_count == 1);

	subscribe(&s, MMI_FRONT, 0x205);
	assert(mmi_single(last_tx(), "e"));
	assert(last_tx()->length == 14u);

	subscribe(&s, MMI_FRONT, 0x206);
	assert(mmi_record(last_tx(), "ttttttttttttt"));

	subscribe(&s, MMI_FRONT, 0x401);
	assert(mmi_array(last_tx(), "bbslb", 24) == MOSTD_SDARS_NUM_PRESETS);

	subscribe(&s, MMI_FRONT, 0x403);
	assert(mmi_single(last_tx(), "b"));

	subscribe(&s, MMI_FRONT, 0x411);
	assert(mmi_single(last_tx(), "b"));
	assert(last_tx()->payload[0] == 0x01);

	subscribe(&s, MMI_FRONT, 0xC04);
	assert(mmi_array(last_tx(), "bs", 255) == (int)SDARS_NUM_CATEGORIES);

	subscribe(&s, MMI_FRONT, 0xC10);
	assert(count_tx(MMI_FRONT, 0xC10, &tx) == 1);	/* one record, not a row loop */
	assert(mmi_record(tx, "bslbb"));
	assert(tx->payload[2] == SDARS_CURRENT_STATION);

	/* 0xC05 is a method and the users list is empty: nothing to publish. */
	captured_count = 0;
	subscribe(&s, MMI_FRONT, 0xC05);
	subscribe(&s, MMI_FRONT, 0x120);
	assert(captured_count == 0);
	assert(error_count == 0);
	assert(s.notify_count == 10);
}

static void test_subscription_table(void)
{
	static const uint8_t clear[] = { NOTIFY_CLEAR_FUNCTION, 0x01, 0x02, 0x0C, 0x04 };
	struct mostd_sdars s;

	sdars_setup(&s);
	subscribe(&s, MMI_FRONT, 0xC04);
	subscribe(&s, MMI_FRONT, 0xC10);
	subscribe(&s, MMI_FRONT, 0xC04);
	assert(s.notify_count == 2);
	assert(subscription_index(&s, MMI_FRONT, 0xC04) >= 0);
	assert(subscription_index(&s, MMI_REAR, 0xC04) < 0);

	rx(&s, MMI_FRONT, FKT_NOTIFICATION, MOST_OP_SET, clear, sizeof(clear));
	assert(s.notify_count == 1);
	assert(subscription_index(&s, MMI_FRONT, 0xC04) < 0);
	assert(subscription_index(&s, MMI_FRONT, 0xC10) >= 0);
}

/* Get returns the same payload as the Notification push; other ops never Error. */
static void test_property_get_and_no_error(void)
{
	struct mostd_sdars s;
	uint8_t junk[1] = { 0x00 };

	sdars_setup(&s);
	rx(&s, MMI_FRONT, 0x400, MOST_OP_GET, NULL, 0);
	assert(captured_count == 1 && mmi_record(last_tx(), "bb"));
	rx(&s, MMI_FRONT, 0xC10, MOST_OP_GET, NULL, 0);
	assert(captured_count == 2 && mmi_record(last_tx(), "bslbb"));

	rx(&s, MMI_FRONT, 0x400, MOST_OP_SET, junk, sizeof(junk));
	rx(&s, MMI_FRONT, 0x403, MOST_OP_SET, junk, sizeof(junk));
	rx(&s, MMI_FRONT, 0xC02, MOST_OP_GET, NULL, 0);
	rx(&s, MMI_FRONT, 0x120, MOST_OP_GET, NULL, 0);
	assert(captured_count == 2);
	assert(error_count == 0);
}

static void test_windows_are_per_peer(void)
{
	struct mostd_sdars s;
	struct mmi_window w;

	sdars_setup(&s);
	create_window(&s, MMI_FRONT, 0x14);
	/* Result echoes the SenderHandle, then the window FktID; then the window. */
	assert(captured_count == 2);
	assert(captured[0].fktid == FKT_CREATE_ARRAY_WINDOW);
	assert(captured[0].op == MOST_OP_RESULT);
	assert(captured[0].length == 4);
	assert(memcmp(captured[0].payload, "\x0C\x02\x0D\x10", 4) == 0);
	assert(captured[1].device == MMI_FRONT && captured[1].fktid == 0x0D10);
	assert(mmi_window(&captured[1], 0x14, &w));
	assert(w.total == SDARS_NUM_STATIONS && w.abs_pos == 0);
	assert(w.rows == SDARS_NUM_STATIONS);

	/* Same SenderHandle from the other MMI gets its own slot and FktID. */
	create_window(&s, MMI_REAR, 0x14);
	assert(memcmp(captured[2].payload, "\x0C\x02\x0D\x11", 4) == 0);
	assert(captured[3].device == MMI_REAR && captured[3].fktid == 0x0D11);
	assert(s.window[0].peer == MMI_FRONT && s.window[1].peer == MMI_REAR);

	/* Re-create from the first MMI reuses its slot. */
	create_window(&s, MMI_FRONT, 0x14);
	assert(memcmp(captured[4].payload, "\x0C\x02\x0D\x10", 4) == 0);
	assert(find_free_window(&s) == 2);

	/* Nothing is ever published on the mother array itself. */
	assert(count_tx(MMI_FRONT, FKT_MOTHER_ARRAY, NULL) == 0);
	assert(count_tx(MMI_REAR, FKT_MOTHER_ARRAY, NULL) == 0);
	assert(error_count == 0);
}

/* The MMI's complete-download loop: move down by windowSize until the last
 * window ("total <= size" or "total - size <= absPos"). */
static void test_complete_download(void)
{
	const uint8_t wsize = 3;
	struct mostd_sdars s;
	struct mmi_window w;
	uint8_t seen[SDARS_NUM_STATIONS];
	int moves = 0, i;

	sdars_setup(&s);
	memset(seen, 0, sizeof(seen));
	create_window(&s, MMI_FRONT, wsize);
	for (;;) {
		assert(mmi_window(last_tx(), wsize, &w));
		assert(last_tx()->fktid == 0x0D10);
		assert(w.total == SDARS_NUM_STATIONS);
		for (i = 0; i < w.rows; i++) {
			/* rows land at absPos + i; station ids are position + 1 */
			assert(w.station[i] == w.abs_pos + i + 1);
			assert(strcmp(w.name[i],
				      default_stations[w.abs_pos + i].shortname) == 0);
			seen[w.abs_pos + i] = 1;
		}
		if (w.total <= wsize || w.total - wsize <= w.abs_pos)
			break;
		assert(++moves < 10);
		captured_count = 0;
		move_window(&s, MMI_FRONT, 0x0D10, 0x03, wsize);
		/* exactly one reply: the window Status, no 0x092 echo */
		assert(captured_count == 1);
	}
	for (i = 0; i < (int)SDARS_NUM_STATIONS; i++)
		assert(seen[i]);
	assert(moves == 2);	/* absPos 0, 3, 5 */
	assert(error_count == 0);
}

static void test_move_modes(void)
{
	struct mostd_sdars s;
	struct mmi_window w;

	sdars_setup(&s);
	create_window(&s, MMI_FRONT, 3);

	move_window(&s, MMI_FRONT, 0x0D10, 0x01, 0);	/* bottom */
	assert(mmi_window(last_tx(), 3, &w) && w.abs_pos == 5 && w.rows == 3);
	move_window(&s, MMI_FRONT, 0x0D10, 0x02, 2);	/* up 2 */
	assert(mmi_window(last_tx(), 3, &w) && w.abs_pos == 3);
	move_window(&s, MMI_FRONT, 0x0D10, 0x02, 11);	/* up past the top */
	assert(mmi_window(last_tx(), 3, &w) && w.abs_pos == 0);
	move_window(&s, MMI_FRONT, 0x0D10, 0x03, 0xFFFF);	/* down past the bottom */
	assert(mmi_window(last_tx(), 3, &w) && w.abs_pos == 5);
	move_window(&s, MMI_FRONT, 0x0D10, 0x00, 0);	/* top */
	assert(mmi_window(last_tx(), 3, &w) && w.abs_pos == 0);
	assert(error_count == 0);
	assert(count_tx(MMI_FRONT, FKT_MOVE_ARRAY_WINDOW, NULL) == 0);

	/* A window Get re-serves it; the other MMI cannot move it. */
	rx(&s, MMI_FRONT, 0x0D10, MOST_OP_GET, NULL, 0);
	assert(mmi_window(last_tx(), 3, &w) && w.abs_pos == 0);
	captured_count = 0;
	move_window(&s, MMI_REAR, 0x0D10, 0x01, 0);
	assert(captured_count == 0);
	assert(error_count == 1 && last_error_op == MOST_OP_STARTACK);
	assert(s.window[0].pos == 0);
}

static void test_destroy_releases_slot(void)
{
	static const uint8_t destroy[] = { 0x0C, 0x02, 0x0D, 0x10 };
	struct mostd_sdars s;

	sdars_setup(&s);
	create_window(&s, MMI_FRONT, 0x14);
	captured_count = 0;

	rx(&s, MMI_REAR, FKT_DESTROY_ARRAY_WINDOW, MOST_OP_STARTRESULTACK,
	   destroy, sizeof(destroy));
	assert(error_count == 1 && s.window[0].active);

	rx(&s, MMI_FRONT, FKT_DESTROY_ARRAY_WINDOW, MOST_OP_STARTRESULTACK,
	   destroy, sizeof(destroy));
	assert(!s.window[0].active);
	assert(captured_count == 1);
	assert(captured[0].op == MOST_OP_RESULTACK);
	assert(captured[0].length == 2);
	assert(memcmp(captured[0].payload, "\x0C\x02", 2) == 0);
}

static void test_station_select_tunes(void)
{
	struct mostd_sdars s;
	const struct captured_tx *tx = NULL;
	uint8_t p[1];

	sdars_setup(&s);
	subscribe(&s, MMI_FRONT, 0xC10);
	subscribe(&s, MMI_REAR, 0xC10);
	subscribe(&s, MMI_REAR, 0x206);
	captured_count = 0;

	p[0] = 0x04;
	rx(&s, MMI_FRONT, 0x200, MOST_OP_SET, p, 1);
	assert(s.current_station == 0x04);
	assert(captured[0].device == MMI_FRONT && captured[0].fktid == 0x200);
	assert(captured[0].length == 1 && captured[0].payload[0] == 0x04);
	/* the change reaches every subscriber, not just the requester */
	assert(count_tx(MMI_FRONT, 0xC10, &tx) == 1 && tx->payload[2] == 0x04);
	assert(mmi_record(tx, "bslbb"));
	assert(count_tx(MMI_REAR, 0xC10, &tx) == 1 && tx->payload[2] == 0x04);
	assert(count_tx(MMI_REAR, 0x206, &tx) == 1 && mmi_record(tx, "ttttttttttttt"));

	/* Re-selecting the current station reports it but pushes nothing. */
	captured_count = 0;
	rx(&s, MMI_FRONT, 0x200, MOST_OP_SET, p, 1);
	assert(captured_count == 1 && captured[0].fktid == 0x200);

	/* An unknown station leaves the tune alone. */
	p[0] = 0x7F;
	rx(&s, MMI_FRONT, 0x200, MOST_OP_SET, p, 1);
	assert(s.current_station == 0x04);
	assert(last_tx()->payload[0] == 0x04);

	/* Increment / Decrement by steps, wrapping 1..N. */
	p[0] = 0x05;
	rx(&s, MMI_FRONT, 0x200, MOST_OP_INCREMENT, p, 1);
	assert(s.current_station == 0x01);
	p[0] = 0x01;
	rx(&s, MMI_FRONT, 0x200, MOST_OP_DECREMENT, p, 1);
	assert(s.current_station == SDARS_NUM_STATIONS);

	rx(&s, MMI_FRONT, 0x200, MOST_OP_GET, NULL, 0);
	assert(last_tx()->payload[0] == SDARS_NUM_STATIONS);
	assert(error_count == 0);
}

static void test_step_command(void)
{
	static const uint8_t next_channel[] = { 0x00, 0x00, 0x01, 0x00, 0x00 };
	static const uint8_t prev_channel[] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t next_preset[]  = { 0x00, 0x00, 0x01, 0x01, 0x00 };
	static const uint8_t prev_preset[]  = { 0x00, 0x00, 0x00, 0x01, 0x00 };
	static const uint8_t next_pop[]     = { 0x01, 0x02, 0x02 };	/* no prefix */
	static const uint8_t bad[]          = { 0x00, 0x00, 0x05, 0x00, 0x00 };
	struct mostd_sdars s;

	/* A step is the module's to interpret: with a phone behind the FBlock it
	 * is a track skip, so it goes out as a seek event and the station stays
	 * put. The requester gets the 0x200 Status. */
	sdars_setup(&s);
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, next_channel, sizeof(next_channel));
	assert(s.current_station == 0x01);
	assert(captured_count == 1 && count_tx(MMI_FRONT, 0x200, NULL) == 1);
	assert(event_count == 1 && strcmp(last_event(), "seek next 0 0") == 0);
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, prev_channel, sizeof(prev_channel));
	assert(strcmp(last_event(), "seek prev 0 0") == 0);
	assert(s.current_station == 0x01);

	/* The list is whichever one the MMI has open. The steering wheel sends
	 * list 1 (car log of 2026-10-31); it still skips a track. */
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, next_preset, sizeof(next_preset));
	assert(strcmp(last_event(), "seek next 1 0") == 0);
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, prev_preset, sizeof(prev_preset));
	assert(strcmp(last_event(), "seek prev 1 0") == 0);
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, next_pop, sizeof(next_pop));
	assert(strcmp(last_event(), "seek next 2 2") == 0);
	assert(s.current_station == 0x01);
	assert(count_events("tune") == 0);

	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, bad, sizeof(bad));
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, bad, 2);
	assert(s.current_station == 0x01);
	assert(error_count == 0);

	/* SDARS_SEEK=station: steps walk the list like a real tuner. Station 1
	 * sits in preset slot 0, so the previous preset is the last slot. */
	setenv("SDARS_SEEK", "station", 1);
	sdars_setup(&s);
	unsetenv("SDARS_SEEK");
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, prev_preset, sizeof(prev_preset));
	assert(s.current_station == MOSTD_SDARS_NUM_PRESETS);
	assert(strcmp(last_event(), "tune 6") == 0);
	/* Pop (category 2) = stations 1..3, wrapping from 6 to 1 */
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, next_pop, sizeof(next_pop));
	assert(s.current_station == 0x01);
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, next_channel, sizeof(next_channel));
	assert(s.current_station == 0x02);
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, prev_channel, sizeof(prev_channel));
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, prev_channel, sizeof(prev_channel));
	assert(s.current_station == SDARS_NUM_STATIONS);
	assert(count_events("seek") == 0);
}

static void test_preset_select(void)
{
	static const uint8_t select3[] = { 0x04, 0x01, 0x02 };	/* preset n=3 */
	static const uint8_t beyond[]  = { 0x19, 0x01, 0x02 };
	struct mostd_sdars s;

	sdars_setup(&s);
	rx(&s, MMI_FRONT, 0x401, MOST_OP_SET, select3, sizeof(select3));
	assert(s.current_station == 0x04);
	rx(&s, MMI_FRONT, 0x401, MOST_OP_SET, beyond, sizeof(beyond));
	assert(s.current_station == 0x04);
	rx(&s, MMI_FRONT, 0x401, MOST_OP_GET, NULL, 0);
	assert(mmi_array(last_tx(), "bbslb", 24) == MOSTD_SDARS_NUM_PRESETS);
	assert(error_count == 0);
}

/* First row of a 0x401 Status: selection flag and station of preset `slot`. */
static void preset_row(const struct captured_tx *tx, int slot, uint8_t *sel, uint8_t *station)
{
	struct cursor c = cursor_of(tx);
	int i;

	rd16(&c);
	for (i = 0; i <= slot; i++) {
		*sel = rd8(&c);
		*station = rd8(&c);
		rd_fields(&c, "slb");
	}
	assert(c.ok);
}

/* Bench: `01 01 03` = store the current station in preset 0; the active preset
 * is the row whose selection is 1, and it follows the tune. */
static void test_preset_store_and_active_mark(void)
{
	static const uint8_t store0[]  = { 0x01, 0x01, 0x03 };
	static const uint8_t select0[] = { 0x01, 0x01, 0x02 };
	static const uint8_t next_preset[] = { 0x00, 0x00, 0x01, 0x01, 0x00 };
	static const uint8_t prev_preset[] = { 0x00, 0x00, 0x00, 0x01, 0x00 };
	struct mostd_sdars s;
	const struct captured_tx *tx = NULL;
	uint8_t p[1] = { 0x08 }, sel, station;

	setenv("SDARS_SEEK", "station", 1);
	sdars_setup(&s);
	unsetenv("SDARS_SEEK");
	subscribe(&s, MMI_REAR, 0x401);
	preset_row(last_tx(), 0, &sel, &station);
	assert(sel == 1 && station == 1);	/* station 1 is current */
	preset_row(last_tx(), 1, &sel, &station);
	assert(sel == 0 && station == 2);

	rx(&s, MMI_FRONT, 0x200, MOST_OP_SET, p, 1);	/* tune station 8 */
	assert(count_tx(MMI_REAR, 0x401, &tx) == 2);
	preset_row(tx, 0, &sel, &station);
	assert(sel == 0);			/* no preset holds station 8 yet */

	rx(&s, MMI_FRONT, 0x401, MOST_OP_SET, store0, sizeof(store0));
	assert(s.presets[0] == 8 && s.current_station == 8);
	assert(count_tx(MMI_FRONT, 0x401, &tx) == 1);
	assert(mmi_array(tx, "bbslb", 24) == MOSTD_SDARS_NUM_PRESETS);
	preset_row(tx, 0, &sel, &station);
	assert(sel == 1 && station == 8);
	assert(count_tx(MMI_REAR, 0x401, NULL) == 3);

	/* stepping walks the preset slots, not the station numbers */
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, next_preset, sizeof(next_preset));
	assert(s.current_station == 2);
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, prev_preset, sizeof(prev_preset));
	assert(s.current_station == 8);
	rx(&s, MMI_FRONT, 0x410, MOST_OP_SET, prev_preset, sizeof(prev_preset));
	assert(s.current_station == MOSTD_SDARS_NUM_PRESETS);
	rx(&s, MMI_FRONT, 0x401, MOST_OP_SET, select0, sizeof(select0));
	assert(s.current_station == 8);
	assert(error_count == 0);
}

static void test_users_list(void)
{
	static const uint8_t front[] = { 0x00, 0x10, 0x00, 0x7F };
	static const uint8_t rear[]  = { 0x00, 0x10, 0x01, 0x7F };
	static const uint8_t ctl[]   = { 0x05, 0x10, 0x02, 0x7F };
	static const uint8_t unreg_front[] = { 0x01, 0x10, 0x00, 0x7F };
	struct mostd_sdars s;
	const struct captured_tx *tx = NULL;

	sdars_setup(&s);
	subscribe(&s, MMI_REAR, 0x120);
	assert(captured_count == 0);

	/* 7F is a constant of the user triple, not a station to tune. */
	rx(&s, MMI_FRONT, 0x120, MOST_OP_SET, front, sizeof(front));
	assert(s.current_station == SDARS_CURRENT_STATION);
	assert(count_tx(MMI_FRONT, 0x120, &tx) == 1);
	assert(tx->length == 3 && memcmp(tx->payload, "\x10\x00\x7F", 3) == 0);
	assert(count_tx(MMI_REAR, 0x120, NULL) == 1);
	/* no 0x411/0xC04/0x400 re-emission */
	assert(captured_count == 2);

	rx(&s, MMI_FRONT, 0x120, MOST_OP_SET, front, sizeof(front));
	rx(&s, MMI_REAR, 0x120, MOST_OP_SET, rear, sizeof(rear));
	rx(&s, MMI_REAR, 0x120, MOST_OP_SET, ctl, sizeof(ctl));
	assert(s.users_len == 6);
	rx(&s, MMI_FRONT, 0x120, MOST_OP_GET, NULL, 0);
	assert(last_tx()->length == 6);
	assert(memcmp(last_tx()->payload, "\x10\x00\x7F\x10\x01\x7F", 6) == 0);

	/* ctl 01 unregisters; everyone is told who is left. */
	captured_count = 0;
	rx(&s, MMI_FRONT, 0x120, MOST_OP_SET, unreg_front, sizeof(unreg_front));
	assert(s.users_len == 3);
	assert(count_tx(MMI_FRONT, 0x120, &tx) == 1);
	assert(tx->length == 3 && memcmp(tx->payload, "\x10\x01\x7F", 3) == 0);
	assert(count_tx(MMI_REAR, 0x120, NULL) == 1);
	assert(error_count == 0);
}

static void test_category_stations(void)
{
	static const uint8_t pop[]     = { 0x0C, 0x05, 0x02 };
	static const uint8_t unknown[] = { 0x0C, 0x05, 0x63 };
	struct mostd_sdars s;
	struct cursor c;
	char name[64];
	int rows = 0;

	sdars_setup(&s);
	rx(&s, MMI_FRONT, 0xC05, MOST_OP_STARTRESULTACK, pop, sizeof(pop));
	assert(captured_count == 1);
	assert(last_tx()->op == MOST_OP_RESULTACK);	/* 0xC is outside the MMI's mask */
	c = cursor_of(last_tx());
	assert(rd16(&c) == 0x0C05u);
	assert(rd8(&c) == 0x02);
	rd_string(&c, 20u, name);
	assert(strcmp(name, "Pop Music") == 0);
	while (c.ok && c.p < c.end) {
		assert(rd8(&c) == rows + 1);	/* stations 1..3 */
		rd_string(&c, 8u, NULL);
		assert(rd8(&c) == 0x01);
		rows++;
	}
	assert(c.ok && rows == 3);

	rx(&s, MMI_FRONT, 0xC05, MOST_OP_STARTRESULTACK, unknown, sizeof(unknown));
	assert(captured_count == 1);
	assert(error_count == 1 && last_error_op == MOST_OP_STARTRESULTACK);
}

static void test_fktids_list(void)
{
	static const uint16_t wanted[] = { 0x200u, 0x410u, 0xC05u };
	uint8_t b[48];
	size_t n = build_fktids(b, sizeof(b));
	size_t i, k, found = 0;
	uint16_t prev = 0;

	assert(n == (sizeof(sdars_fktids) / sizeof(sdars_fktids[0]) * 3u + 1u) / 2u);
	for (i = 0; i < sizeof(sdars_fktids) / sizeof(sdars_fktids[0]); i++) {
		size_t nib = i * 3u;
		uint16_t f = 0;

		for (k = 0; k < 3u; k++) {
			uint8_t byte = b[(nib + k) / 2u];

			f = (uint16_t)((f << 4) |
				       (((nib + k) & 1u) ? (byte & 0x0Fu) : (byte >> 4)));
		}
		assert(i == 0 || f > prev);	/* ascending */
		prev = f;
		for (k = 0; k < sizeof(wanted) / sizeof(wanted[0]); k++)
			found += (f == wanted[k]);
	}
	assert(found == sizeof(wanted) / sizeof(wanted[0]));
}

/* A full 20-row window must fit the TX buffer even with 8-char names. */
static void test_window_buffer_bound(void)
{
	/* header 7 + 20 x (tag 2 + station 1 + string 10 + sub 1) + end tag 2 */
	assert(7u + 20u * 14u + 2u <= SDARS_TX_MAX);
}

/* The source path ASK drives is unchanged (mmi_satradio_most.md §5b). */
static void test_source_queries(void)
{
	static const uint8_t expected_source[] = {
		0x01, 0x00, 0x02, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	};
	static const uint8_t source_nr[] = { 0x01 };
	static const uint8_t activity[] = { 0x01, 0x02 };
	struct mostd_sdars s;

	sdars_setup(&s);
	/* ASK addresses the source as 0x44.00 and must be answered as such. */
	rx_inst = INST_WILDCARD_ANY;
	rx(&s, 0x0100, FKT_SOURCE_INFO, MOST_OP_GET, source_nr, sizeof(source_nr));
	assert(captured_count == 1);
	assert(captured[0].fktid == FKT_SOURCE_INFO);
	assert(captured[0].length == sizeof(expected_source));
	assert(memcmp(captured[0].payload, expected_source, sizeof(expected_source)) == 0);

	rx(&s, 0x0100, FKT_SOURCE_ACTIVITY, MOST_OP_SETGET, activity, sizeof(activity));
	assert(captured_count == 2);
	assert(captured[1].length == 2);
	assert(memcmp(captured[1].payload, activity, sizeof(activity)) == 0);
	assert(captured[0].fblock == SDARS_FBLOCK && captured[1].fblock == SDARS_FBLOCK);
	assert(captured[0].inst == SDARS_INST && captured[1].inst == SDARS_INST);
	rx_inst = SDARS_INST;

	/* Every reply carries our one identity, 0x44.00, including those to the
	 * all-instances wildcard. */
	rx_inst = INST_WILDCARD_ALL;
	rx(&s, MMI_FRONT, 0x400, MOST_OP_GET, NULL, 0);
	assert(last_tx()->fktid == 0x400 && last_tx()->inst == SDARS_INST);
	rx_inst = SDARS_INST;
	assert(error_count == 0);
}

/* ---- control-plane API ---------------------------------------------------- */

static void fill_station(struct mostd_sdars_station *st, const char *sh, const char *lo,
			 uint8_t cat)
{
	memset(st, 0, sizeof(*st));
	snprintf(st->shortname, sizeof(st->shortname), "%s", sh);
	snprintf(st->longname, sizeof(st->longname), "%s", lo);
	st->category = cat;
	st->subscribed = 1;
}

/* Replacing the station table re-serves every open window with the new total
 * and pushes the dependent lists; presets and the current station are clamped. */
static void test_station_table_replace(void)
{
	struct mostd_sdars s;
	struct mostd_sdars_station st[3];
	struct mostd_sdars_category cat = { 1, "Phones", "Paired phones" };
	struct mmi_window w;
	const struct captured_tx *tx = NULL;
	uint8_t p[1] = { 0x08 };
	int i;

	sdars_setup(&s);
	subscribe(&s, MMI_FRONT, 0xC04);
	subscribe(&s, MMI_FRONT, 0x401);
	subscribe(&s, MMI_FRONT, 0xC10);
	subscribe(&s, MMI_FRONT, 0x200);
	subscribe(&s, MMI_REAR, 0x206);
	create_window(&s, MMI_FRONT, 0x14);
	create_window(&s, MMI_REAR, 0x14);
	rx(&s, MMI_FRONT, 0x200, MOST_OP_SET, p, 1);	/* station 8 */
	assert(s.current_station == 8);
	reset_capture();

	fill_station(&st[0], "Pixel", "Richard's Pixel", 1);
	fill_station(&st[1], "iPhone", "Work iPhone", 1);
	fill_station(&st[2], "Laptop", "ThinkPad", 1);
	assert(mostd_sdars_set_stations(&s, st, 3, &cat, 1) == 0);
	assert(s.station_count == 3 && s.category_count == 1);
	assert(s.current_station == 1);			/* 8 is gone */
	for (i = 0; i < (int)MOSTD_SDARS_NUM_PRESETS; i++)
		assert(s.presets[i] >= 1 && s.presets[i] <= 3);

	assert(count_tx(MMI_FRONT, 0x0D10, &tx) == 1);
	assert(mmi_window(tx, 0x14, &w));
	assert(w.total == 3 && w.abs_pos == 0 && w.rows == 3);
	assert(strcmp(w.name[0], "Pixel") == 0 && strcmp(w.name[2], "Laptop") == 0);
	assert(count_tx(MMI_REAR, 0x0D11, &tx) == 1 && mmi_window(tx, 0x14, &w));
	assert(count_tx(MMI_FRONT, 0xC04, &tx) == 1 && mmi_array(tx, "bs", 255) == 1);
	assert(count_tx(MMI_FRONT, 0x401, &tx) == 1 && mmi_array(tx, "bbslb", 24) == 3);
	assert(count_tx(MMI_FRONT, 0xC10, &tx) == 1 && mmi_record(tx, "bslbb"));
	assert(tx->payload[2] == 1);
	assert(count_tx(MMI_FRONT, 0x200, &tx) == 1 && tx->payload[0] == 1);
	assert(count_tx(MMI_REAR, 0x206, &tx) == 1 && mmi_record(tx, "ttttttttttttt"));
	assert(count_tx(MMI_FRONT, FKT_MOTHER_ARRAY, NULL) == 0);
	assert(event_count == 0);	/* a table change is not a tune */

	/* A window past the end of a shorter list is pulled back. */
	move_window(&s, MMI_FRONT, 0x0D10, 0x01, 0);	/* bottom = 0 for 3 rows */
	assert(s.window[0].pos == 0);

	/* Rejected tables leave everything in place ('|' is the line separator;
	 * name lengths are bounded by the struct and checked by the parser). */
	fill_station(&st[0], "Bad|Name", "x", 1);
	assert(mostd_sdars_set_stations(&s, st, 3, &cat, 1) == -1);
	fill_station(&st[0], "Ok", "ctrl\x01char", 1);
	assert(mostd_sdars_set_stations(&s, st, 3, &cat, 1) == -1);
	assert(mostd_sdars_set_stations(&s, st, 0, &cat, 1) == -1);
	assert(s.station_count == 3 && strcmp(s.stations[0].shortname, "Pixel") == 0);

	/* The category drill-in follows the new table. */
	{
		static const uint8_t req[] = { 0x0C, 0x05, 0x01 };

		reset_capture();
		rx(&s, MMI_FRONT, 0xC05, MOST_OP_STARTRESULTACK, req, sizeof(req));
		assert(captured_count == 1 && last_tx()->op == MOST_OP_RESULTACK);
	}
	assert(error_count == 0);
}

static void test_program_text(void)
{
	struct mostd_sdars s;
	const struct captured_tx *tx = NULL;
	struct cursor c;
	char f0[64];
	char longtitle[64];
	uint8_t p[1] = { 0x03 };

	sdars_setup(&s);
	subscribe(&s, MMI_FRONT, 0x206);
	subscribe(&s, MMI_REAR, 0x206);
	reset_capture();

	assert(mostd_sdars_set_text(&s, "Bohemian Rhapsody", "Queen",
				    "A Night at the Opera") == 0);
	assert(count_tx(MMI_FRONT, 0x206, &tx) == 1 && mmi_record(tx, "ttttttttttttt"));
	assert(count_tx(MMI_REAR, 0x206, NULL) == 1);
	c = cursor_of(tx);
	rd16(&c);
	rd_string(&c, 36u, f0);
	assert(strcmp(f0, "Bohemian Rhapsody") == 0);

	/* Unchanged text is not re-pushed. */
	assert(mostd_sdars_set_text(&s, "Bohemian Rhapsody", "Queen",
				    "A Night at the Opera") == 0);
	assert(count_tx(MMI_FRONT, 0x206, NULL) == 1);

	/* Over 36 bytes the MMI would reject the whole record: truncate. */
	memset(longtitle, 'x', 50);
	longtitle[50] = '\0';
	assert(mostd_sdars_set_text(&s, longtitle, "", "") == 0);
	assert(count_tx(MMI_FRONT, 0x206, &tx) == 2 && mmi_record(tx, "ttttttttttttt"));
	c = cursor_of(tx);
	rd16(&c);
	assert(rd_string(&c, 36u, f0) == 36u);

	/* A tune re-pushes the text (same content) rather than station names. */
	rx(&s, MMI_FRONT, 0x200, MOST_OP_SET, p, 1);
	assert(count_tx(MMI_FRONT, 0x206, &tx) == 3);
	c = cursor_of(tx);
	rd16(&c);
	rd_string(&c, 36u, f0);
	assert(f0[0] == 'x');
	assert(error_count == 0);
}

/* ASK's repeated SourceActivity(2) is one "source on"; 0 and Deallocate are off. */
static void test_source_events(void)
{
	static const uint8_t on[]  = { 0x01, 0x02 };
	static const uint8_t off[] = { 0x01, 0x00 };
	static const uint8_t nr[]  = { 0x01 };
	struct mostd_sdars s;

	sdars_setup(&s);
	rx_inst = INST_WILDCARD_ANY;
	rx(&s, 0x0100, FKT_SOURCE_ACTIVITY, MOST_OP_SETGET, on, sizeof(on));
	rx(&s, 0x0100, FKT_SOURCE_ACTIVITY, MOST_OP_SETGET, on, sizeof(on));
	rx(&s, 0x0100, FKT_SOURCE_ACTIVITY, MOST_OP_SETGET, on, sizeof(on));
	assert(event_count == 1 && strcmp(events[0], "source on") == 0);
	rx(&s, 0x0100, FKT_SOURCE_ACTIVITY, MOST_OP_SETGET, off, sizeof(off));
	assert(event_count == 2 && strcmp(events[1], "source off") == 0);
	rx(&s, 0x0100, FKT_DEALLOCATE, MOST_OP_SETGET, nr, sizeof(nr));
	assert(event_count == 2);
	rx(&s, 0x0100, FKT_SOURCE_ACTIVITY, MOST_OP_SETGET, on, sizeof(on));
	rx(&s, 0x0100, FKT_DEALLOCATE, MOST_OP_SETGET, nr, sizeof(nr));
	assert(event_count == 4 && strcmp(events[3], "source off") == 0);
	rx_inst = SDARS_INST;
	assert(error_count == 0);
}

/* The users list going empty <-> non-empty is the SAT UI open/closed edge. */
static void test_ui_events(void)
{
	static const uint8_t front[] = { 0x00, 0x10, 0x00, 0x7F };
	static const uint8_t rear[]  = { 0x00, 0x10, 0x01, 0x7F };
	static const uint8_t unreg_front[] = { 0x01, 0x10, 0x00, 0x7F };
	static const uint8_t unreg_rear[]  = { 0x01, 0x10, 0x01, 0x7F };
	struct mostd_sdars s;

	sdars_setup(&s);
	rx(&s, MMI_FRONT, 0x120, MOST_OP_SET, front, sizeof(front));
	assert(event_count == 1 && strcmp(events[0], "ui open") == 0);
	rx(&s, MMI_REAR, 0x120, MOST_OP_SET, rear, sizeof(rear));
	rx(&s, MMI_FRONT, 0x120, MOST_OP_SET, unreg_front, sizeof(unreg_front));
	assert(event_count == 1);
	rx(&s, MMI_REAR, 0x120, MOST_OP_SET, unreg_rear, sizeof(unreg_rear));
	assert(event_count == 2 && strcmp(events[1], "ui closed") == 0);
}

static void test_signal_esn_current(void)
{
	struct mostd_sdars s;
	const struct captured_tx *tx = NULL;

	sdars_setup(&s);
	subscribe(&s, MMI_FRONT, 0x400);
	subscribe(&s, MMI_FRONT, 0x205);
	subscribe(&s, MMI_FRONT, 0xC10);
	reset_capture();

	mostd_sdars_set_signal(&s, 0);
	assert(count_tx(MMI_FRONT, 0x400, &tx) == 1 && mmi_record(tx, "bb"));
	assert(tx->payload[2] == 0x00 && tx->payload[3] == 0x01);
	mostd_sdars_set_signal(&s, 0);
	assert(count_tx(MMI_FRONT, 0x400, NULL) == 1);
	mostd_sdars_set_signal(&s, 1);
	assert(count_tx(MMI_FRONT, 0x400, &tx) == 2 && tx->payload[2] == 0x01);

	assert(mostd_sdars_set_esn(&s, "AA:BB:CC:DDEE") == -1);	/* 13 chars */
	assert(mostd_sdars_set_esn(&s, "Pixel 8") == 0);
	assert(count_tx(MMI_FRONT, 0x205, &tx) == 1 && mmi_single(tx, "e"));

	/* The agent reporting the playing station pushes but is not a "tune". */
	assert(mostd_sdars_set_current(&s, 3) == 0);
	assert(count_tx(MMI_FRONT, 0xC10, &tx) == 1 && tx->payload[2] == 3);
	assert(mostd_sdars_set_current(&s, 3) == 0);
	assert(count_tx(MMI_FRONT, 0xC10, NULL) == 1);
	assert(mostd_sdars_set_current(&s, 99) == -1);
	assert(count_events("tune") == 0);

	mostd_sdars_report_state(&s);
	assert(event_count == 3);
	assert(strcmp(events[0], "current 3") == 0);
	assert(strcmp(events[1], "source off") == 0);
	assert(strcmp(events[2], "ui closed") == 0);
	assert(error_count == 0);
}

int main(void)
{
	test_subscribed_properties_parse();
	test_subscription_table();
	test_property_get_and_no_error();
	test_windows_are_per_peer();
	test_complete_download();
	test_move_modes();
	test_destroy_releases_slot();
	test_station_select_tunes();
	test_step_command();
	test_preset_select();
	test_preset_store_and_active_mark();
	test_users_list();
	test_category_stations();
	test_fktids_list();
	test_window_buffer_bound();
	test_source_queries();
	test_station_table_replace();
	test_program_text();
	test_source_events();
	test_ui_events();
	test_signal_esn_current();
	return 0;
}
