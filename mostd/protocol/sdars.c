#include "sdars.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../log.h"
#include "cmt.h"
#include "rx.h"
#include "tx.h"

/*
 * The MMI and firmware references in the comments (mmi_satradio_most.md,
 * sdars_source_funcs_resolved.md, SDARS_bringup_sequence.md) are decompile
 * notes kept outside this repository.
 */

#define SDARS_FBLOCK			MOSTD_SDARS_FBLOCK
#define SDARS_INST			MOSTD_SDARS_INST

/* Request-only InstID wildcards. */
#define INST_WILDCARD_ANY		0x00u
#define INST_WILDCARD_ALL		0xFFu

/* Coordination functions (GeneralFBlock catalog). */
#define FKT_FKTIDS			0x000u
#define FKT_NOTIFICATION		0x001u
#define FKT_NOTIFICATION_CHECK		0x002u
#define FKT_CREATE_ARRAY_WINDOW		0x090u
#define FKT_DESTROY_ARRAY_WINDOW	0x091u
#define FKT_MOVE_ARRAY_WINDOW		0x092u
#define FKT_LONG_ARRAY_INFO		0x094u

/* SatRadio functions, named as the MMI consumes them (mmi_satradio_most.md §2). */
#define FKT_USERS			0x120u	/* users list: {FBlockID, InstID, x} triples */
#define FKT_STATION_SELECT		0x200u	/* SRStationSelect: current station / tune */
#define FKT_ELECTRONIC_CODE		0x205u	/* ESN */
#define FKT_PROGRAM_TEXT		0x206u	/* title, artist, album + 10 unused strings */
#define FKT_CURRENT_STATION		0x212u	/* producer-side only; not registered on the MMI */
#define FKT_SERVICE_STATUS		0x400u	/* FID_SR_SERVICE_STATUS: {receptionOK, ?} */
#define FKT_PRESETS			0x401u	/* FID_SR_PRESETS */
#define FKT_0403			0x403u	/* u8 enum, unused by the MMI UI */
#define FKT_STEP			0x410u	/* next/previous channel, preset or category */
#define FKT_SERVICE_ENABLED		0x411u	/* 0 opens the "service disabled" dialog */
#define FKT_MOTHER_ARRAY		0xC02u	/* ShortStationName list (array window only) */
#define FKT_CATEGORY_INFO		0xC04u	/* FID_SR_SHORT_CATEGORY_INFO */
#define FKT_GET_CATEGORY_STATIONS	0xC05u	/* FID_SR_GET_CATEGORY_STATIONS (method) */
#define FKT_CURRENT_STATION_INFO	0xC10u	/* FID_SR_CURRENT_STATION_INFO */

/*
 * Source-side functions (GeneralFBlock coordination, inherited by SDARS 0x44;
 * needed for the ConnectionMaster to build the audio connection). FktIDs per
 * MOST25 GeneralFBlock spec - 0x101 is Allocate (a METHOD returning SourceNr,
 * SrcDelay, ChannelList), NOT SourceInfo. SourceInfo is the 0x100 property.
 * SDARS firmware implements 0x101/0x102/0x103 as real FBlock 0x44 functions
 * (response framer 0x329160: sets FBlock 0x44 + these FktIDs).
 */
#define FKT_SOURCE_INFO			0x100u	/* property: SourceNr,DataType,DataDescription */
#define FKT_ALLOCATE			0x101u	/* method:   Result = SourceNr,SrcDelay,ChannelList */
#define FKT_DEALLOCATE			0x102u
#define FKT_SOURCE_ACTIVITY		0x103u
#define FKT_SYNC_DATA_INFO		0x116u

/* ---- Synchronous audio source (Resource Allocate + OS8104A routing) ------
 * Datasheet-grounded, lifted from tools/examples/resource_alloc.c and
 * audio_tx.c. Resource Allocate (Type 03h) asks the timing-master for N
 * synchronous channels; the granted physical channels ARE the MOST Routing
 * Table (MRT) byte locations we then route our source port onto. */
#define RA_FRAME_PRIORITY	0x01u
#define RA_FRAME_TYPE_ALLOC	0x03u
#define RA_FRAME_TYPE_DEALLOC	0x04u
#define RA_TM_ADDR_HI		0x04u	/* timing-master physical 0x0400 (node pos 0) */
#define RA_TM_ADDR_LO		0x00u
#define RA_REG_ANSWER1		0xC7u	/* 0x01 = ALLOC_GRANT */
#define RA_REG_ANSWER2		0xC8u	/* channels still free */
#define RA_REG_P0		0xC9u	/* P0..P7 granted channels = 0xC9..0xD0 */
#define RA_ANSWER1_GRANT	0x01u
#define RA_CHAN_UNUSABLE	0xFFu
#define RA_POLL_TRIES		100
#define RA_POLL_WAIT_US		2000	/* 100 * 2ms = 200ms answer budget */
#define RA_TX_BUSY_RETRIES	8
#define RA_TX_BUSY_WAIT_US	3000

/* OS8104A registers / bits (see tools/examples/audio_tx.c). */
#define OS_REG_BXCR		0x80u
#define OS_REG_BSDC1		0x82u
#define OS_REG_BNPR		0x87u
#define OS_REG_BSDC2		0x8Cu
#define OS_REG_BSDC3		0x8Du
/*
 * bNDR, Node Delay Register (OS8104A datasheet §6.2.6, addr 0x8F): read-only,
 * reports the synchronous-data delay (in frame delays) between the timing-master
 * and this node. This is exactly the MOST "SrcDelay" parameter ("delay of
 * streaming data related to the TimingMaster", GeneralFBlock spec) carried in
 * Allocate.Result and SourceInfo. We use it for src_delay. bNPR (0x87, node
 * POSITION) is a different quantity (only active nodes add delay, so delay !=
 * position) and is the wrong value here.
 * NOTE: an earlier rationale tied this to a firmware handler (0x30be43) that was
 * later identified as NetBlock DeviceInfo, not SDARS 0x101; the value is still
 * correct because SrcDelay IS the node delay to the timing-master.
 */
#define OS_REG_BNDR		0x8Fu
/* bXCR polarity (datasheet Table 6-1): ABY SET = node active on the ring; ABY
 * CLEAR = All-Bypass, node invisible. SBY CLEAR = node routes source data via
 * the MRT. To transmit source data: ABY stays SET, SBY CLEAR; clearing ABY
 * drops the node off the bus. */
#define OS_BXCR_SBY		0x04u	/* Source Data Bypass (clear to route source) */
#define OS_BXCR_ABY		0x02u	/* All-Bypass enable (SET = active node) */
#define OS_BSDC1_I2S		0xD3u	/* I2S source format used by SocketMost. */
#define OS_BSDC2_32FS		0x40u	/* SPR[2:0]=010 */
#define OS_BSDC3_ON		0x00u	/* SPEN=0 enabled, MTI=0 unmuted */

/* Stereo 16-bit source-port input Address References at 32Fs. */
#define SDARS_SRC_BYTES		4
static const uint8_t SDARS_SRC_MRA[SDARS_SRC_BYTES] = { 0x49, 0x59, 0x69, 0x79 };

/* Notification.Set Control values. */
#define NOTIFY_SET_ALL			0x00u
#define NOTIFY_SET_FUNCTION		0x01u
#define NOTIFY_CLEAR_ALL		0x02u
#define NOTIFY_CLEAR_FUNCTION		0x03u

#define MOST_ERR_FKT_NOT_AVAILABLE	0x03u
#define MOST_ERR_PARAM_WRONG		0x06u
#define MOST_ERR_DEVICE_NOT_READY	0x07u

#define SDARS_NUM_STATIONS		0x08u
#define SDARS_CURRENT_STATION		0x01u	/* the playing station; MUST be a member of the 1..N list */
#define SDARS_ESN			"000000000001"
#define SDARS_WINDOW_FKTID(slot)		((uint16_t)(0x0D10u + (slot)))
#define SDARS_TX_MAX			512u	/* a full 20-row window Status is ~290 bytes */

static char *hex(const uint8_t *d, size_t n);
static void free_hex(char *h);
static void load_default_stations(struct mostd_sdars *s);

void mostd_sdars_init(struct mostd_sdars *s, struct most_handle *handle)
{
	uint8_t i;

	if (!s)
		return;
	memset(s, 0, sizeof(*s));
	s->handle = handle;
	s->our_inst = SDARS_INST;
	load_default_stations(s);
	s->current_station = SDARS_CURRENT_STATION;
	for (i = 0; i < MOSTD_SDARS_NUM_PRESETS; i++)
		s->presets[i] = (uint8_t)(i + 1u);	/* preset n = station n+1 until stored over */
	snprintf(s->esn, sizeof(s->esn), "%s", SDARS_ESN);
	s->signal_ok = 1;
	{
		const char *e = getenv("SDARS_SEEK");

		s->seek_station = e && strcmp(e, "station") == 0;
	}
	memset(s->src_chan, RA_CHAN_UNUSABLE, sizeof(s->src_chan));
}

/* Event line to the control plane (no-op without a sink). */
static void emit(struct mostd_sdars *s, const char *fmt, ...)
{
	char line[96];
	va_list ap;

	if (!s->event_fn)
		return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	s->event_fn(s->event_ctx, line);
}

static char *hex(const uint8_t *d, size_t n)
{
	return mostd_payload_hex_alloc(d, n);
}

static void free_hex(char *h)
{
	if (h && (h[0] != '-' || h[1] != '\0'))
		free(h);
}

/*
 * Method reply: [SenderHandle, <result params>], with caller-chosen op.
 *
 * SDAR65 firmware 6939772A replies to the array-window method 0x090 with
 * op 0xC (Result), not 0xD (ResultAck): handler FUN_30bc56 ends `LDAB #0xc;
 * RTC`, and MOST semantics put Result (slave->controller) at 0xC while
 * ResultAck (0xD) is the controller's ack. See SDARS_bringup_sequence.md.
 * 0x091 (DestroyArrayWindow) stays on 0xD pending its own firmware check.
 */
static int reply_method(struct mostd_sdars *s, const struct mostd_rx_msg *msg,
			uint8_t op, const uint8_t *params, size_t plen)
{
	uint8_t payload[SDARS_TX_MAX];
	size_t n = 0;

	/* Echo the 2-byte SenderHandle the request carried, then params. */
	if (msg->tellen >= 2u) {
		payload[n++] = msg->data[0];
		payload[n++] = msg->data[1];
	} else {
		payload[n++] = 0x00;
		payload[n++] = 0x00;
	}
	if (plen && params) {
		if (n + plen > sizeof(payload))
			return -1;
		memcpy(&payload[n], params, plen);
		n += plen;
	}
	if (mostd_tx_op(s->handle, msg->source, SDARS_FBLOCK, s->our_inst, msg->fktid,
			op, 0, payload, n) < 0)
		return -1;
	s->stats.replies++;
	return 0;
}

static int reply_status(struct mostd_sdars *s, const struct mostd_rx_msg *msg,
			const uint8_t *payload, size_t len)
{
	if (mostd_tx_status(s->handle, msg->source, SDARS_FBLOCK, s->our_inst, msg->fktid,
			    0, payload, len) < 0)
		return -1;
	s->stats.replies++;
	return 0;
}

static int reply_error(struct mostd_sdars *s, const struct mostd_rx_msg *msg,
		       uint8_t code, uint8_t info)
{
	if (mostd_tx_error_for_request(s->handle, msg->source, SDARS_FBLOCK, s->our_inst,
				       msg->fktid, 0, msg->optype, msg->data, msg->tellen,
				       code, info) < 0)
		return -1;
	s->stats.errors++;
	return 0;
}

/* ---- Emulated SDARS state ------------------------------------------------ *
 * Every payload below is shaped for the MMI-side parser (9172146A, see
 * mmi_satradio_most.md §2-§4), not the SDAR65 producer firmware. The
 * grammar the MMI enforces:
 *   Single  = bare value, exact length
 *   Record  = 00 00 + all fields in order, exact length
 *   Array   = 00 00 + rows x all fields until the message ends
 *   String  = 01 <chars> 00
 * Any parse error, or an Error reply, invalidates the function on the MMI, and
 * a Status only validates a function the MMI has already subscribed to.
 */

static int tx_status_to(struct mostd_sdars *s, uint16_t dst, uint16_t fktid,
			const uint8_t *payload, size_t len)
{
	if (mostd_tx_status(s->handle, dst, SDARS_FBLOCK, s->our_inst, fktid, 0, payload, len) < 0)
		return -1;
	s->stats.replies++;
	return 0;
}

/* Append an MMI string (01 <chars> 00). Returns the new length, 0 if it won't fit. */
static size_t append_text_field(uint8_t *out, size_t cap, size_t n,
				const char *text, size_t max_len)
{
	size_t len = strlen(text);

	if (len > max_len)
		len = max_len;
	if (n + len + 2u > cap)
		return 0;
	out[n++] = 0x01;
	memcpy(&out[n], text, len);
	n += len;
	out[n++] = 0x00;
	return n;
}

/*
 * Built-in station list, served until the control plane replaces it
 * (mostd_sdars_set_stations). Station numbers are 1-based positions.
 */
static const struct mostd_sdars_station default_stations[SDARS_NUM_STATIONS] = {
	{ "SIRIUS1",  "Sirius Hits 1",   0x02, 1 },
	{ "SIRIUS2",  "Sirius Hits 2",   0x02, 1 },
	{ "POP1",     "Pop Hits",        0x02, 1 },
	{ "ROCK1",    "Classic Rock",    0x03, 1 },
	{ "JAZZ1",    "Smooth Jazz",     0x04, 1 },
	{ "NEWS1",    "World News",      0x05, 1 },
	{ "TALK1",    "Talk Radio",      0x06, 1 },
	{ "COUNTRY1", "Country Hits",    0x07, 1 },
};

static const struct mostd_sdars_category default_categories[] = {
	{ 0x02, "Pop",     "Pop Music" },
	{ 0x03, "Rock",    "Rock Music" },
	{ 0x04, "Jazz",    "Jazz & Blues" },
	{ 0x05, "News",    "News & Weather" },
	{ 0x06, "Talk",    "Talk & Comedy" },
	{ 0x07, "Country", "Country Music" },
};
#define SDARS_NUM_CATEGORIES	(sizeof(default_categories) / sizeof(default_categories[0]))

/*
 * Load the built-in list. SDARS_LIST_COUNT clamps the advertised count to 1..N
 * so a short list can be tested on the car without a rebuild.
 */
static void load_default_stations(struct mostd_sdars *s)
{
	const char *e = getenv("SDARS_LIST_COUNT");
	long v = e ? strtol(e, NULL, 0) : SDARS_NUM_STATIONS;

	if (v < 1)
		v = 1;
	if (v > SDARS_NUM_STATIONS)
		v = SDARS_NUM_STATIONS;
	memcpy(s->stations, default_stations, sizeof(default_stations));
	s->station_count = (uint8_t)v;
	memcpy(s->categories, default_categories, sizeof(default_categories));
	s->category_count = (uint8_t)SDARS_NUM_CATEGORIES;
}

/* Number of stations we advertise (window total, presets, categories). */
static uint8_t sdars_list_count(const struct mostd_sdars *s)
{
	return s->station_count;
}

/* Preset slots in use; s->presets[] holds the station stored in each. */
static uint8_t sdars_preset_count(const struct mostd_sdars *s)
{
	uint8_t count = sdars_list_count(s);

	return count < MOSTD_SDARS_NUM_PRESETS ? count : (uint8_t)MOSTD_SDARS_NUM_PRESETS;
}

static const struct mostd_sdars_station *station_at(const struct mostd_sdars *s,
						    uint8_t station)
{
	uint8_t idx = station >= 1u ? (uint8_t)(station - 1u) : 0u;

	return &s->stations[idx % MOSTD_SDARS_MAX_STATIONS];
}

static const struct mostd_sdars_station *current_station(const struct mostd_sdars *s)
{
	return station_at(s, s->current_station);
}

static const struct mostd_sdars_category *category_at(const struct mostd_sdars *s,
						      uint8_t id)
{
	uint8_t i;

	for (i = 0; i < s->category_count; i++)
		if (s->categories[i].id == id)
			return &s->categories[i];
	return NULL;
}

static int category_advertised(const struct mostd_sdars *s, uint8_t category)
{
	uint8_t i;

	for (i = 0; i < sdars_list_count(s); i++)
		if (s->stations[i].category == category)
			return 1;
	return 0;
}

/* 0x120 users list: the stored {FBlockID, InstID, x} triples. A stream needs
 * at least one byte, so an empty list has no Status at all. */
static size_t build_users(const struct mostd_sdars *s, uint8_t *out, size_t cap)
{
	if (!s->users_len || cap < s->users_len)
		return 0;
	memcpy(out, s->users, s->users_len);
	return s->users_len;
}

/* 0x200 SRStationSelect: the current station number. */
static size_t build_station_select(const struct mostd_sdars *s, uint8_t *out, size_t cap)
{
	if (cap < 1u)
		return 0;
	out[0] = s->current_station;
	return 1u;
}

/* 0x205 ElectronicCode (ESN): one string <=12. */
static size_t build_electronic_code(const struct mostd_sdars *s, uint8_t *out, size_t cap)
{
	return append_text_field(out, cap, 0, s->esn, MOSTD_SDARS_ESN_LEN);
}

/*
 * 0x206 programme text: Record of 13 strings <=36. The MMI reads only f0-f2
 * (title, artist, album); the other ten still have to be present. Without
 * agent-supplied text the station's long/short names stand in.
 */
static size_t build_program_text(const struct mostd_sdars *s, uint8_t *out, size_t cap)
{
	const struct mostd_sdars_station *st = current_station(s);
	const char *f0 = s->text_set ? s->text[0] : st->longname;
	const char *f1 = s->text_set ? s->text[1] : st->shortname;
	const char *f2 = s->text_set ? s->text[2] : "";
	size_t n = 0;
	uint8_t i;

	if (cap < 2u)
		return 0;
	out[n++] = 0x00;
	out[n++] = 0x00;
	n = append_text_field(out, cap, n, f0, MOSTD_SDARS_TEXT_LEN);	/* title */
	if (n)
		n = append_text_field(out, cap, n, f1, MOSTD_SDARS_TEXT_LEN);	/* artist */
	if (n)
		n = append_text_field(out, cap, n, f2, MOSTD_SDARS_TEXT_LEN);	/* album */
	for (i = 0; n && i < 10u; i++)					/* f3-f12 */
		n = append_text_field(out, cap, n, "", MOSTD_SDARS_TEXT_LEN);
	return n;
}

/*
 * 0x400 FID_SR_SERVICE_STATUS: Record {bool receptionOK, bool f1}. This is the
 * isValid() gate that greys SAT, so it must parse. f1 has no reader on the MMI.
 * f0 = 0 makes the MMI report "no reception" (voice result KEIN_EMPFANG).
 */
static size_t build_service_status(const struct mostd_sdars *s, uint8_t *out, size_t cap)
{
	if (cap < 4u)
		return 0;
	out[0] = 0x00;
	out[1] = 0x00;
	out[2] = s->signal_ok ? 0x01 : 0x00;
	out[3] = 0x01;
	return 4u;
}

/*
 * 0x401 FID_SR_PRESETS: Array of {eSRPresetSelection, station, short <=8,
 * long <=20, subscribed}. The MMI (ondevPresets 0c1ac844) marks the row whose
 * selection is 1 as the active preset.
 */
static size_t build_presets(const struct mostd_sdars *s, uint8_t *out, size_t cap)
{
	size_t n = 0;
	uint8_t i;

	if (cap < 2u)
		return 0;
	out[n++] = 0x00;
	out[n++] = 0x00;
	for (i = 0; i < sdars_preset_count(s); i++) {
		uint8_t station = s->presets[i];
		const struct mostd_sdars_station *st = station_at(s, station);

		if (n + 2u > cap)
			return 0;
		out[n++] = station == s->current_station;	/* selection: 1 = active */
		out[n++] = station;
		n = append_text_field(out, cap, n, st->shortname, 8u);
		if (n)
			n = append_text_field(out, cap, n, st->longname, 20u);
		if (!n || n + 1u > cap)
			return 0;
		out[n++] = st->subscribed ? 0x01 : 0x00;
	}
	return n;
}

/* 0x403: a single enum byte. Meaning unknown; the MMI UI ignores it. */
static size_t build_0x403(uint8_t *out, size_t cap)
{
	if (cap < 1u)
		return 0;
	out[0] = 0x00;
	return 1u;
}

/* 0x411 service enabled: 0 would open the MMI's "service disabled" dialog. */
static size_t build_service_enabled(uint8_t *out, size_t cap)
{
	if (cap < 1u)
		return 0;
	out[0] = 0x01;
	return 1u;
}

/* 0xC04 FID_SR_SHORT_CATEGORY_INFO: Array of {category id, short name <=8}. */
static size_t build_category_info(const struct mostd_sdars *s, uint8_t *out, size_t cap)
{
	size_t n = 0, i;

	if (cap < 2u)
		return 0;
	out[n++] = 0x00;
	out[n++] = 0x00;
	for (i = 0; i < s->category_count; i++) {
		if (!category_advertised(s, s->categories[i].id))
			continue;
		if (n + 1u > cap)
			return 0;
		out[n++] = s->categories[i].id;
		n = append_text_field(out, cap, n, s->categories[i].shortname, 8u);
		if (!n)
			return 0;
	}
	return n;
}

/*
 * 0xC10 FID_SR_CURRENT_STATION_INFO: one Record for the current station,
 * {station, short <=8, long <=20, subscribed, category}.
 */
static size_t build_current_station_info(const struct mostd_sdars *s,
					 uint8_t *out, size_t cap)
{
	const struct mostd_sdars_station *st = current_station(s);
	size_t n = 0;

	if (cap < 3u)
		return 0;
	out[n++] = 0x00;
	out[n++] = 0x00;
	out[n++] = s->current_station;
	n = append_text_field(out, cap, n, st->shortname, 8u);
	if (n)
		n = append_text_field(out, cap, n, st->longname, 20u);
	if (!n || n + 2u > cap)
		return 0;
	out[n++] = st->subscribed ? 0x01 : 0x00;
	out[n++] = st->category;
	return n;
}

/*
 * 0xC05 GetCategoryStations result:
 *   SenderHandle | category | long name <=20 | { station, short name, subscribed }...
 * Returns 0 for a category none of the advertised stations belong to.
 */
static size_t build_category_stations(const struct mostd_sdars *s, uint16_t handle,
				      uint8_t category, uint8_t *out, size_t cap)
{
	const struct mostd_sdars_category *cat = category_at(s, category);
	size_t n = 0, i;

	if (!cat || !category_advertised(s, category) || cap < 3u)
		return 0;
	out[n++] = (uint8_t)(handle >> 8);
	out[n++] = (uint8_t)(handle & 0xFFu);
	out[n++] = category;
	n = append_text_field(out, cap, n, cat->longname, 20u);
	for (i = 0; n && i < sdars_list_count(s); i++) {
		if (s->stations[i].category != category)
			continue;
		if (n + 1u > cap)
			return 0;
		out[n++] = (uint8_t)(i + 1u);
		n = append_text_field(out, cap, n, s->stations[i].shortname, 8u);
		if (!n || n + 1u > cap)
			return 0;
		out[n++] = s->stations[i].subscribed ? 0x01 : 0x00;
	}
	return n;
}

/*
 * FktIDs (0x000) Status = the FBlock's supported functions, as a stream of
 * 12-bit FktIDs (3 nibbles each, big-endian, ascending) -- the exact on-wire
 * form the real FM tuner (0x40) returned when probed.
 */
static const uint16_t sdars_fktids[] = {
	0x000u, 0x001u, 0x002u, 0x090u, 0x091u, 0x092u,
	0x100u, 0x101u, 0x102u, 0x103u, 0x116u, 0x120u,
	0x200u, 0x205u, 0x206u, 0x400u, 0x401u, 0x403u,
	0x410u, 0x411u, 0xC02u, 0xC04u, 0xC05u, 0xC10u,
};

static size_t build_fktids(uint8_t *out, size_t cap)
{
	size_t nfk = sizeof(sdars_fktids) / sizeof(sdars_fktids[0]);
	size_t nib = nfk * 3u;
	size_t bytes = (nib + 1u) / 2u;
	size_t i;

	if (cap < bytes)
		return 0;
	memset(out, 0, bytes);
	for (i = 0; i < nfk; i++) {
		uint16_t f = sdars_fktids[i];
		uint8_t n[3];
		size_t b = i * 3u, k;

		n[0] = (uint8_t)((f >> 8) & 0x0Fu);
		n[1] = (uint8_t)((f >> 4) & 0x0Fu);
		n[2] = (uint8_t)(f & 0x0Fu);
		for (k = 0; k < 3u; k++) {
			size_t idx = b + k;
			if (idx & 1u)
				out[idx / 2u] |= n[k];
			else
				out[idx / 2u] |= (uint8_t)(n[k] << 4);
		}
	}
	return bytes;
}

/* The one builder per property: Get replies and Notification pushes share it. */
static size_t build_property(const struct mostd_sdars *s, uint16_t fktid,
			     uint8_t *out, size_t cap)
{
	switch (fktid) {
	case FKT_USERS:
		return build_users(s, out, cap);
	case FKT_STATION_SELECT:
		return build_station_select(s, out, cap);
	case FKT_ELECTRONIC_CODE:
		return build_electronic_code(s, out, cap);
	case FKT_PROGRAM_TEXT:
		return build_program_text(s, out, cap);
	case FKT_SERVICE_STATUS:
		return build_service_status(s, out, cap);
	case FKT_PRESETS:
		return build_presets(s, out, cap);
	case FKT_0403:
		return build_0x403(out, cap);
	case FKT_SERVICE_ENABLED:
		return build_service_enabled(out, cap);
	case FKT_CATEGORY_INFO:
		return build_category_info(s, out, cap);
	case FKT_CURRENT_STATION_INFO:
		return build_current_station_info(s, out, cap);
	default:
		return 0;
	}
}

/* Send a property's Status. Returns -1 when there is nothing to send. */
static int push_property(struct mostd_sdars *s, uint16_t dst, uint16_t fktid)
{
	uint8_t b[SDARS_TX_MAX];
	size_t n = build_property(s, fktid, b, sizeof(b));

	if (!n)
		return -1;
	return tx_status_to(s, dst, fktid, b, n);
}

/* Push a changed property to every subscriber except `skip` (0 = none). */
static void notify_subscribers(struct mostd_sdars *s, uint16_t fktid, uint16_t skip)
{
	uint8_t i;

	for (i = 0; i < s->notify_count; i++)
		if (s->notify[i].fktid == fktid && s->notify[i].device != skip)
			push_property(s, s->notify[i].device, fktid);
}

static int subscription_index(const struct mostd_sdars *s, uint16_t dev, uint16_t fktid)
{
	uint8_t i;

	for (i = 0; i < s->notify_count; i++)
		if (s->notify[i].device == dev && s->notify[i].fktid == fktid)
			return i;
	return -1;
}

static void add_subscription(struct mostd_sdars *s, uint16_t dev, uint16_t fktid)
{
	if (subscription_index(s, dev, fktid) >= 0)
		return;
	if (s->notify_count >= MOSTD_SDARS_MAX_SUBSCRIPTIONS) {
		MOSTD_INFO("sdars: notification table full; dev=0x%04X fkt=0x%03X ignored",
			   dev, fktid);
		return;
	}
	s->notify[s->notify_count].device = dev;
	s->notify[s->notify_count].fktid = fktid;
	s->notify_count++;
}

static void remove_subscription(struct mostd_sdars *s, uint16_t dev, uint16_t fktid)
{
	int i = subscription_index(s, dev, fktid);

	if (i < 0)
		return;
	s->notify[i] = s->notify[--s->notify_count];
}

/* Notification.Set(Control, DeviceID, FktIDList) - record the subscriber. */
static void handle_notification(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	uint8_t ctrl;
	uint16_t dev;
	size_t i;

	if (msg->tellen < 3u) {
		MOSTD_INFO("sdars: Notification op=0x%X short len=%u", msg->optype, msg->tellen);
		return;
	}
	ctrl = msg->data[0];
	dev = (uint16_t)((msg->data[1] << 8) | msg->data[2]);

	/* Log the subscribed FktIDs (2-byte words after Control+DeviceID). */
	{
		char list[80];
		size_t cur = 0;
		for (i = 3; i + 1u < msg->tellen && cur + 7u < sizeof(list); i += 2u)
			cur += (size_t)snprintf(list + cur, sizeof(list) - cur, "%s0x%03X",
						cur ? "," : "",
						(unsigned)((msg->data[i] << 8) | msg->data[i + 1u]));
		MOSTD_INFO("sdars: Notification.Set ctrl=0x%02X dev=0x%04X fkts=[%s]",
			   ctrl, dev, cur ? list : "");
	}

	if (ctrl == NOTIFY_SET_FUNCTION) {
		for (i = 3; i + 1u < msg->tellen; i += 2u) {
			uint16_t fkt = (uint16_t)((msg->data[i] << 8) | msg->data[i + 1u]);

			add_subscription(s, dev, fkt);
			/*
			 * The MMI only marks a function valid from a Status that
			 * arrives once its subscription is armed, and it sends no Get
			 * of its own, so publish the current value now. 0xC05 (a
			 * method) and an empty 0x120 list have nothing to publish.
			 */
			push_property(s, dev, fkt);
		}
	} else if (ctrl == NOTIFY_CLEAR_FUNCTION) {
		for (i = 3; i + 1u < msg->tellen; i += 2u)
			remove_subscription(s, dev,
					    (uint16_t)((msg->data[i] << 8) | msg->data[i + 1u]));
	} else {
		/* The MMI only builds Set(Function)/Clear(Function), one FktID each. */
		MOSTD_INFO("sdars: Notification control 0x%02X not recovered; no state change",
			   ctrl);
	}
}

/* ---- 0xC02 station list array window (mmi_satradio_most.md §4) ---------- */

/*
 * Both MMIs (0x0102, 0x0103) create their window with SenderHandle 0x0C02, so
 * a slot is owned by (peer, handle). Each slot has its own FktID, which is all
 * that Move/Destroy/Get carry.
 */
static int find_window_by_handle(const struct mostd_sdars *s, uint16_t peer,
				 uint16_t handle)
{
	uint8_t slot;

	for (slot = 0; slot < MOSTD_SDARS_WINDOW_SLOTS; slot++)
		if (s->window[slot].active && s->window[slot].peer == peer &&
		    s->window[slot].sender_handle == handle)
			return slot;
	return -1;
}

static int find_window_by_fktid(const struct mostd_sdars *s, uint16_t peer,
				uint16_t fktid)
{
	uint8_t slot;

	for (slot = 0; slot < MOSTD_SDARS_WINDOW_SLOTS; slot++)
		if (s->window[slot].active && s->window[slot].peer == peer &&
		    SDARS_WINDOW_FKTID(slot) == fktid)
			return slot;
	return -1;
}

static int find_free_window(const struct mostd_sdars *s)
{
	uint8_t slot;

	for (slot = 0; slot < MOSTD_SDARS_WINDOW_SLOTS; slot++)
		if (!s->window[slot].active)
			return slot;
	return -1;
}

/* Highest first-row position a window of `size` rows can take. */
static uint16_t window_max_pos(const struct mostd_sdars *s, uint8_t size)
{
	uint16_t total = sdars_list_count(s);

	return (size && total > size) ? (uint16_t)(total - size) : 0u;
}

/*
 * One Status per window, parsed by the MMI's LongArray container:
 *   tag(u16)=0 | pos(u8)=0 | total(u16) | absPos(u16) |
 *   { elementTag(u16) station(u8) name(string <=8) subscribed(u8) } x rows |
 *   FFFF
 * total = size of the whole mother array (it becomes the GUI list size), absPos
 * = index of the first row served. The element tag must be unique and never
 * 0xFFFF (that is the mandatory end tag), so use position + 1.
 */
static size_t build_window_status(const struct mostd_sdars *s, uint16_t pos, uint8_t size,
				  uint8_t *out, size_t cap)
{
	uint16_t total = sdars_list_count(s);
	uint16_t rows = total > pos ? (uint16_t)(total - pos) : 0u;
	size_t n = 0;
	uint16_t i;

	if (size && rows > size)
		rows = size;
	if (cap < 7u)
		return 0;
	out[n++] = 0x00;
	out[n++] = 0x00;
	out[n++] = 0x00;
	out[n++] = (uint8_t)(total >> 8);
	out[n++] = (uint8_t)(total & 0xFFu);
	out[n++] = (uint8_t)(pos >> 8);
	out[n++] = (uint8_t)(pos & 0xFFu);
	for (i = 0; i < rows; i++) {
		uint16_t idx = (uint16_t)(pos + i);
		uint16_t tag = (uint16_t)(idx + 1u);

		if (n + 3u > cap)
			return 0;
		out[n++] = (uint8_t)(tag >> 8);
		out[n++] = (uint8_t)(tag & 0xFFu);
		out[n++] = (uint8_t)(idx + 1u);	/* station id */
		n = append_text_field(out, cap, n, s->stations[idx].shortname, 8u);
		if (!n || n + 1u > cap)
			return 0;
		out[n++] = s->stations[idx].subscribed ? 0x01 : 0x00;
	}
	if (n + 2u > cap)
		return 0;
	out[n++] = 0xFF;
	out[n++] = 0xFF;
	return n;
}

/* Publish the window at its current position on its own FktID (0x0D10+slot). */
static void serve_window(struct mostd_sdars *s, struct mostd_sdars_window *window,
			 uint8_t slot, int after_move)
{
	uint8_t status[SDARS_TX_MAX];
	uint16_t fktid = SDARS_WINDOW_FKTID(slot);
	size_t n;

	if (!window || !window->active)
		return;
	n = build_window_status(s, window->pos, window->size, status, sizeof(status));
	if (!n)
		return;
	tx_status_to(s, window->peer, fktid, status, n);
	MOSTD_INFO("sdars: served %swindow 0x%03X to 0x%04X: total=%u absPos=%u size=%u",
		   after_move ? "post-move " : "", fktid, window->peer,
		   sdars_list_count(s), window->pos, window->size);
}

/*
 * CreateArrayWindow.StartResultAck:
 *   SenderHandle, FktIDMotherArray, StartPos, WindowSize
 * The MMI routes the result by its first u16, so it must echo the SenderHandle
 * (the MMI uses the array FktID 0x0C02), followed by the window's FktID. It
 * accepts Result 0xC here. The window Status follows at once; nothing may be
 * sent on 0xC02 itself, which the MMI now parses as window data.
 */
static void handle_create_array_window(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	struct mostd_sdars_window *entry;
	uint16_t handle, mother, start, window;
	uint8_t wsize, params[2];
	int slot;

	if (msg->optype != MOST_OP_STARTRESULTACK || msg->tellen != 7u) {
		reply_error(s, msg, MOST_ERR_FKT_NOT_AVAILABLE, 0);
		return;
	}
	handle = (uint16_t)((msg->data[0] << 8) | msg->data[1]);
	mother = (uint16_t)((msg->data[2] << 8) | msg->data[3]);
	start = (uint16_t)((msg->data[4] << 8) | msg->data[5]);
	wsize = msg->data[6];
	if (mother != FKT_MOTHER_ARRAY) {
		reply_error(s, msg, MOST_ERR_FKT_NOT_AVAILABLE, 0);
		return;
	}

	slot = find_window_by_handle(s, msg->source, handle);
	if (slot < 0)
		slot = find_free_window(s);
	if (slot < 0) {
		reply_error(s, msg, MOST_ERR_DEVICE_NOT_READY, 0);
		return;
	}

	entry = &s->window[slot];
	memset(entry, 0, sizeof(*entry));
	entry->active = 1;
	entry->peer = msg->source;
	entry->sender_handle = handle;
	entry->mother = mother;
	entry->size = wsize;
	entry->pos = start > window_max_pos(s, wsize) ? window_max_pos(s, wsize) : start;
	window = SDARS_WINDOW_FKTID((uint8_t)slot);

	MOSTD_INFO("sdars: CreateArrayWindow peer=0x%04X handle=0x%04X mother=0x%03X "
		   "start=%u wsize=%u -> window=0x%03X",
		   msg->source, handle, mother, start, wsize, window);
	params[0] = (uint8_t)(window >> 8);
	params[1] = (uint8_t)(window & 0xFFu);
	reply_method(s, msg, MOST_OP_RESULT, params, sizeof(params));
	serve_window(s, entry, (uint8_t)slot, 0);
}

/* DestroyArrayWindow.StartResultAck: SenderHandle, FktIDArrayWindow. */
static void handle_destroy_array_window(struct mostd_sdars *s,
					const struct mostd_rx_msg *msg)
{
	uint16_t fktid;
	int slot;

	if (msg->optype != MOST_OP_STARTRESULTACK || msg->tellen != 4u) {
		reply_error(s, msg, MOST_ERR_FKT_NOT_AVAILABLE, 0);
		return;
	}
	fktid = (uint16_t)((msg->data[2] << 8) | msg->data[3]);
	slot = find_window_by_fktid(s, msg->source, fktid);
	if (slot < 0) {
		reply_error(s, msg, MOST_ERR_FKT_NOT_AVAILABLE, 0);
		return;
	}

	memset(&s->window[slot], 0, sizeof(s->window[slot]));
	MOSTD_INFO("sdars: DestroyArrayWindow window=0x%03X slot=%d", fktid, slot);
	reply_method(s, msg, MOST_OP_RESULTACK, NULL, 0);
}

/*
 * MoveArrayWindow.StartAck:
 *   SenderHandle, FktIDArrayWindow, MovingMode, Number, Tag
 * The MMI puts the window FktID in both handle fields. Success has no method
 * reply - the MMI expects only the window Status from the new position (an
 * echo of the StartAck is not a report OpType). Failure is an ErrorAck.
 */
static void handle_move_array_window(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	struct mostd_sdars_window *entry;
	uint16_t window, number, max_pos;
	uint8_t mode;
	int slot;

	if (msg->tellen < 7u) {
		MOSTD_INFO("sdars: MoveArrayWindow short payload len=%u", msg->tellen);
		reply_error(s, msg, MOST_ERR_FKT_NOT_AVAILABLE, 0);
		return;
	}
	window = (uint16_t)((msg->data[2] << 8) | msg->data[3]);
	mode = msg->data[4];
	number = (uint16_t)((msg->data[5] << 8) | msg->data[6]);
	slot = find_window_by_fktid(s, msg->source, window);
	if (slot < 0) {
		MOSTD_INFO("sdars: MoveArrayWindow unknown window=0x%04X from 0x%04X",
			   window, msg->source);
		reply_error(s, msg, MOST_ERR_FKT_NOT_AVAILABLE, 0);
		return;
	}
	entry = &s->window[slot];
	max_pos = window_max_pos(s, entry->size);

	switch (mode) {
	case 0x00:	/* top */
		entry->pos = 0;
		break;
	case 0x01:	/* bottom */
		entry->pos = max_pos;
		break;
	case 0x02:	/* up by number, clamped at top */
		entry->pos = entry->pos > number ? (uint16_t)(entry->pos - number) : 0u;
		break;
	case 0x03:	/* down by number, clamped at bottom */
		entry->pos = (uint32_t)entry->pos + number > max_pos
				   ? max_pos : (uint16_t)(entry->pos + number);
		break;
	default:
		/* The MMI builds modes 0-3 only; re-serve the window unmoved. */
		MOSTD_INFO("sdars: MoveArrayWindow unsupported mode=0x%02X", mode);
		break;
	}
	MOSTD_INFO("sdars: MoveArrayWindow window=0x%04X mode=0x%02X number=%u -> absPos=%u",
		   window, mode, number, entry->pos);
	serve_window(s, entry, (uint8_t)slot, 1);
}

/*
 * Bring our SDARS node up as a synchronous audio source: allocate channels from
 * the timing-master (Resource Allocate Type 03h), then route our source port
 * onto the granted channels via the OS8104A MRT. Idempotent (guarded by
 * src_allocated). Returns 0 on success.
 *
 * WARNING: writes INIC hardware registers and consumes ring synchronous
 * bandwidth on a live MOST ring.
 */
static int sdars_source_bringup(struct mostd_sdars *s)
{
	uint8_t frm[MOST_FRAME_LEN];
	uint8_t answer1 = 0, answer2 = 0, bxcr = 0, npr = 0;
	int attempt, i, n = 0;

	if (s->src_allocated)
		return 0;

	/* --- Resource Allocate (Type 03h) for our stereo source bytes. --- */
	memset(frm, 0, sizeof(frm));
	frm[0] = RA_FRAME_PRIORITY;
	frm[1] = RA_FRAME_TYPE_ALLOC;
	frm[2] = RA_TM_ADDR_HI;
	frm[3] = RA_TM_ADDR_LO;
	frm[5] = SDARS_SRC_BYTES;                 /* channels requested */

	for (attempt = 0; attempt < RA_TX_BUSY_RETRIES; attempt++) {
		if (most_write(s->handle, frm) == 0)
			break;
		usleep(RA_TX_BUSY_WAIT_US);
	}
	if (attempt == RA_TX_BUSY_RETRIES) {
		MOSTD_INFO("sdars: source bringup: ctrl TX slot busy, allocate not sent");
		return -1;
	}

	for (i = 0; i < RA_POLL_TRIES; i++) {
		if (most_reg_read(s->handle, RA_REG_ANSWER1, &answer1, 1) < 0)
			break;
		if (answer1 != 0x00)
			break;
		usleep(RA_POLL_WAIT_US);
	}
	if (answer1 != RA_ANSWER1_GRANT) {
		MOSTD_INFO("sdars: source bringup: timing-master did not grant (Answer1=0x%02X)",
			   answer1);
		return -1;
	}
	most_reg_read(s->handle, RA_REG_ANSWER2, &answer2, 1);
	for (i = 0; i < SDARS_SRC_BYTES; i++) {
		uint8_t ch = RA_CHAN_UNUSABLE;
		most_reg_read(s->handle, (uint16_t)(RA_REG_P0 + i), &ch, 1);
		s->src_chan[i] = ch;
		if (ch != RA_CHAN_UNUSABLE)
			n++;
	}
	s->src_nchan = (uint8_t)n;
	s->src_label = s->src_chan[0];
	/*
	 * SourceInfo byte[4] = bNDR (node delay), what the firmware reads here,
	 * not bNPR (node position). Fall back to bNPR only if the bNDR read fails.
	 */
	if (most_reg_read(s->handle, OS_REG_BNDR, &npr, 1) == 0)
		s->src_delay = npr;
	else if (most_reg_read(s->handle, OS_REG_BNPR, &npr, 1) == 0)
		s->src_delay = npr;

	/* --- OS8104A: enable source-data routing, source port, load MRT. ---
	 * Clear SBY only so the node routes source data via the MRT. Leave ABY
	 * SET: the node is already active on the ring (it has a node position and
	 * is passing control traffic); clearing ABY would force All-Bypass and
	 * drop us off the bus (datasheet Table 6-1). */
	if (most_reg_read(s->handle, OS_REG_BXCR, &bxcr, 1) == 0) {
		bxcr = (uint8_t)((bxcr | OS_BXCR_ABY) & ~OS_BXCR_SBY);
		most_reg_write(s->handle, OS_REG_BXCR, &bxcr, 1);
	}
	{ uint8_t v = OS_BSDC1_I2S; most_reg_write(s->handle, OS_REG_BSDC1, &v, 1); }
	{ uint8_t v = OS_BSDC2_32FS; most_reg_write(s->handle, OS_REG_BSDC2, &v, 1); }
	{ uint8_t v = OS_BSDC3_ON;  most_reg_write(s->handle, OS_REG_BSDC3, &v, 1); }
	for (i = 0; i < SDARS_SRC_BYTES; i++) {
		uint8_t ch = s->src_chan[i], mra = SDARS_SRC_MRA[i];
		if (ch != RA_CHAN_UNUSABLE)
			most_reg_write(s->handle, ch, &mra, 1);
	}

	s->src_allocated = 1;
	MOSTD_INFO("sdars: source bringup OK: granted %d chan(s) label=0x%02X free=0x%02X "
		   "chans=%02X %02X %02X %02X (MRT routed, SBY clear/ABY kept set)",
		   n, s->src_label, answer2,
		   s->src_chan[0], s->src_chan[1], s->src_chan[2], s->src_chan[3]);
	return 0;
}

static int sdars_source_deallocate(struct mostd_sdars *s)
{
	uint8_t frm[MOST_FRAME_LEN];
	uint8_t answer1 = 0, bxcr = 0;
	int attempt, i;

	if (!s->src_allocated)
		return 0;

	memset(frm, 0, sizeof(frm));
	frm[0] = RA_FRAME_PRIORITY;
	frm[1] = RA_FRAME_TYPE_DEALLOC;
	frm[2] = RA_TM_ADDR_HI;
	frm[3] = RA_TM_ADDR_LO;
	frm[5] = s->src_label;
	for (attempt = 0; attempt < RA_TX_BUSY_RETRIES; attempt++) {
		if (most_write(s->handle, frm) == 0)
			break;
		usleep(RA_TX_BUSY_WAIT_US);
	}
	if (attempt == RA_TX_BUSY_RETRIES)
		return -1;
	for (i = 0; i < RA_POLL_TRIES; i++) {
		if (most_reg_read(s->handle, RA_REG_ANSWER1, &answer1, 1) < 0)
			break;
		if (answer1 != 0)
			break;
		usleep(RA_POLL_WAIT_US);
	}
	if (answer1 != RA_ANSWER1_GRANT)
		return -1;

	if (most_reg_read(s->handle, OS_REG_BXCR, &bxcr, 1) == 0) {
		bxcr |= OS_BXCR_SBY;
		most_reg_write(s->handle, OS_REG_BXCR, &bxcr, 1);
	}
	s->src_allocated = 0;
	s->src_nchan = 0;
	s->src_label = 0;
	memset(s->src_chan, RA_CHAN_UNUSABLE, sizeof(s->src_chan));
	return 0;
}

/*
 * Firmware-exact SourceInfo/Allocate report payload (9 bytes), decompiled from
 * SDAR65 v17 FUN_4c3ac0 / sdars_source_funcs_resolved.md:
 *   01 00 02 02 <val> <ch0 ch1 ch2 ch3>
 * The real module reports val=0xFF and channels=0xFF while UNALLOCATED - it does
 * NOT own a fixed channel and does NOT self-allocate; the ConnectionMaster
 * allocates and the source then reports the granted channels. Channels are 4
 * bytes (2 stereo channels) regardless, FF-padded.
 */
static size_t build_source_info(struct mostd_sdars *s, uint8_t *out)
{
	int i;

	out[0] = 0x01;	/* source number */
	out[1] = 0x00;
	out[2] = 0x02;	/* 16-bit samples = 2 bytes */
	out[3] = 0x02;	/* stereo / 2 channels */
	out[4] = s->src_allocated ? s->src_delay : RA_CHAN_UNUSABLE;	/* val: FF when inactive */
	for (i = 0; i < 4; i++)
		out[5 + i] = (s->src_allocated && i < s->src_nchan)
				? s->src_chan[i] : RA_CHAN_UNUSABLE;
	return 9;
}

/*
 * Allocate (0x101) Result payload per MOST25 GeneralFBlock spec 2.1.19:
 *   [SourceNr][SrcDelay][Channel ... ]   (NO DataType/Resolution/AudioChannels)
 * The ConnectionMaster invokes Allocate as a method and parses exactly this; the
 * previous code answered 0x101 with a SourceInfo blob (SourceNr,DataType,
 * DataDescription), which the CM cannot parse as an Allocate.Result, so the
 * source contract stalled. SrcDelay = our streaming delay; ChannelList = the
 * channels we actually occupy.
 *
 * Channel count: we emit what we allocated (src_nchan, normally 4 = stereo
 * 16-bit). If the SDARS connection profile on the CM expects a specific count
 * (profile_record[+7]), set SDARS_SRC_BYTES to match.
 */
static size_t build_allocate_result(struct mostd_sdars *s, uint8_t source_nr,
				    uint8_t *out, size_t cap)
{
	size_t n = 0;
	uint8_t i;

	if (cap < 2u + (size_t)SDARS_SRC_BYTES)
		return 0;
	out[n++] = source_nr;					/* SourceNr */
	out[n++] = s->src_allocated ? s->src_delay : 0x00u;	/* SrcDelay */
	for (i = 0; i < s->src_nchan && i < SDARS_SRC_BYTES; i++)
		out[n++] = s->src_chan[i];			/* ChannelList */
	return n;
}

/*
 * Report the source on/off edge to the control plane. ASK sends SourceActivity
 * 2 repeatedly while it builds the connection (16x in one second on the car
 * log of 2026-10-27), so only the change is reported. Off arrives as
 * SourceActivity 0 followed by Deallocate at key-off.
 */
static void set_source_active(struct mostd_sdars *s, int on)
{
	if (!!on == s->source_active)
		return;
	s->source_active = (uint8_t)!!on;
	emit(s, "source %s", on ? "on" : "off");
}

/* GeneralFBlock Rev 2.5.1 source contract used by ASK's ConnectionMaster.
 * Response byte formats are firmware-exact (sdars_source_funcs_resolved.md). */
static void handle_source_func(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	uint8_t info[16];
	uint8_t source_nr = msg->tellen ? msg->data[0] : 0x01u;
	size_t n = 0;

	MOSTD_INFO("sdars: SOURCE fn 0x%03X op=0x%X source=%u allocated=%u nchan=%u label=0x%02X",
		   msg->fktid, msg->optype, source_nr, s->src_allocated,
		   s->src_nchan, s->src_label);

	switch (msg->fktid) {
	case FKT_ALLOCATE:
		/*
		 * 0x101 Allocate is a METHOD (GeneralFBlock 2.1.19). The
		 * ConnectionMaster invokes it (live: op 0x2, payload = SourceNr) and
		 * parses the reply as Allocate.Result = [SourceNr][SrcDelay][Channel
		 * List]. Allocate our synchronous channels on first call, then return
		 * the real Result (op 0xC). Returning a SourceInfo blob here (the old
		 * bug: SourceNr,DataType,DataDescription) is unparseable as an
		 * Allocate.Result, so the CM never completes the source contract.
		 */
		if ((msg->optype != MOST_OP_GET && msg->optype != MOST_OP_SETGET) ||
		    msg->tellen < 1u || source_nr != 0x01u)
			break;
		if (!s->src_allocated) {
			if (sdars_source_bringup(s) < 0)
				MOSTD_INFO("sdars: allocate: source bringup failed");
		}
		n = build_allocate_result(s, source_nr, info, sizeof(info));
		if (!n) {
			reply_error(s, msg, MOST_ERR_DEVICE_NOT_READY, 0);
			return;
		}
		reply_status(s, msg, info, n);	/* op 0xC = Result */
		return;
	case FKT_SOURCE_INFO:
		/*
		 * 0x100 SourceInfo is a read-only PROPERTY (GeneralFBlock 2.1.18):
		 * Status = SourceNr, DataType, DataDescription (for PCM: Resolution,
		 * AudioChannels, SrcDelay, ChannelList). Report current state only;
		 * allocation happens via 0x101 Allocate, not here.
		 */
		if ((msg->optype != MOST_OP_GET && msg->optype != MOST_OP_SETGET) ||
		    msg->tellen != 1u || source_nr != 0x01u)
			break;
		n = build_source_info(s, info);
		reply_status(s, msg, info, n);
		return;
	case FKT_DEALLOCATE:
		if (msg->optype != MOST_OP_SETGET)
			break;
		if (sdars_source_deallocate(s) < 0) {
			reply_error(s, msg, MOST_ERR_FKT_NOT_AVAILABLE, 0);
			return;
		}
		info[0] = source_nr;
		reply_status(s, msg, info, 1);
		set_source_active(s, 0);
		return;
	case FKT_SOURCE_ACTIVITY:
	{
		/*
		 * 0x103 SourceActivity is a METHOD (spec 2.1.21): "controls the
		 * activity of a source". Request and Result are both
		 * [SourceNr][Activity] (Activity: 0x00 Off, 0x01 On, 0x02 Standby).
		 * The CM invokes it right after Allocate to start the source (live:
		 * op 0x2, len 2, payload 01 02). Previously mostd required len==1 and
		 * so errored (op 0xF) on the valid 2-byte request, looping the CM.
		 *
		 * The OS8104 source path is already routed + enabled in
		 * sdars_source_bringup (on 0x101 Allocate); the firmware's extra
		 * activate step toggles a board audio-enable GPIO (MMIO 0x4001) that
		 * has no Pi equivalent. So we just acknowledge with the spec Result,
		 * echoing SourceNr + Activity.
		 */
		uint8_t activity;

		if ((msg->optype != MOST_OP_GET && msg->optype != MOST_OP_SETGET) ||
		    msg->tellen < 2u || source_nr != 0x01u)
			break;
		activity = msg->data[1];
		info[0] = source_nr;
		info[1] = activity;
		reply_status(s, msg, info, 2);	/* Result: SourceNr, Activity */
		set_source_active(s, activity != 0x00u);
		return;
	}
	default:
		break;
	}
	reply_error(s, msg, MOST_ERR_FKT_NOT_AVAILABLE, 0);
}

static void handle_sync_data_info(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	uint8_t payload[2];

	if (msg->optype != MOST_OP_GET) {
		reply_error(s, msg, MOST_ERR_FKT_NOT_AVAILABLE, 0);
		return;
	}
	payload[0] = 0x01;   /* SourceCount = 1 (our SDARS audio source) */
	payload[1] = 0x00;   /* SinkCount = 0 */
	MOSTD_INFO("sdars: SyncDataInfo.Get -> sources=1 sinks=0");
	reply_status(s, msg, payload, sizeof(payload));
}

/* ---- Tuning (mmi_satradio_most.md §2, §3b) ------------------------------ */

/* Publish everything that depends on the current station. */
static void push_station_change(struct mostd_sdars *s, uint16_t skip_select)
{
	notify_subscribers(s, FKT_STATION_SELECT, skip_select);
	notify_subscribers(s, FKT_CURRENT_STATION_INFO, 0);
	notify_subscribers(s, FKT_PROGRAM_TEXT, 0);
	notify_subscribers(s, FKT_PRESETS, 0);		/* the active-preset mark moves */
}

/*
 * Make `station` current. The requester always gets the 0x200 Status; on a real
 * change every other subscriber gets it too, followed by the dependent
 * current-station record (0xC10), programme text (0x206) and presets (0x401).
 */
static void tune(struct mostd_sdars *s, uint8_t station, uint16_t requester)
{
	int changed = 0;

	if (station < 1u || station > sdars_list_count(s)) {
		MOSTD_INFO("sdars: no SRStation=0x%02X; current remains 0x%02X",
			   station, s->current_station);
	} else if (station != s->current_station) {
		s->current_station = station;
		changed = 1;
		MOSTD_INFO("sdars: tuned SRStation=0x%02X requester=0x%04X",
			   station, requester);
	}
	push_property(s, requester, FKT_STATION_SELECT);
	if (!changed)
		return;
	push_station_change(s, requester);
	emit(s, "tune %u", station);
}

/* list: 0 = every channel, 2 = stations of `category` (1 = presets, below). */
static int station_in_list(const struct mostd_sdars *s, uint8_t station, uint8_t list,
			   uint8_t category)
{
	switch (list) {
	case 0x00:
		return 1;
	case 0x02:
		return s->stations[station - 1u].category == category;
	default:
		return 0;
	}
}

/* Next (dir != 0) or previous preset slot, wrapping. From a station that is
 * not a preset, next goes to the first slot and previous to the last. */
static uint8_t step_preset(const struct mostd_sdars *s, int dir)
{
	uint8_t count = sdars_preset_count(s);
	uint8_t i;

	for (i = 0; i < count; i++)
		if (s->presets[i] == s->current_station)
			break;
	if (i == count)
		return s->presets[dir ? 0u : count - 1u];
	return s->presets[(i + (dir ? 1u : count - 1u)) % count];
}

/* Next (dir != 0) or previous station within a list, wrapping. */
static uint8_t step_station(const struct mostd_sdars *s, int dir, uint8_t list,
			    uint8_t category)
{
	uint8_t count = sdars_list_count(s);
	uint8_t cur = (s->current_station >= 1u && s->current_station <= count)
			    ? (uint8_t)(s->current_station - 1u) : 0u;
	uint8_t k;

	if (list == 0x01)
		return step_preset(s, dir);
	for (k = 1; k <= count; k++) {
		uint8_t cand = (uint8_t)((cur + (dir ? k : count - k)) % count);

		if (station_in_list(s, (uint8_t)(cand + 1u), list, category))
			return (uint8_t)(cand + 1u);
	}
	return s->current_station;
}

/* 0x200 SRStationSelect: Set/SetGet(station), Increment/Decrement(steps), Get. */
static void handle_station_select(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	uint8_t count = sdars_list_count(s);
	uint8_t steps = msg->tellen ? msg->data[0] : 1u;
	uint8_t cur = (s->current_station >= 1u && s->current_station <= count)
			    ? (uint8_t)(s->current_station - 1u) : 0u;

	switch (msg->optype) {
	case MOST_OP_GET:
		push_property(s, msg->source, FKT_STATION_SELECT);
		return;
	case MOST_OP_SET:
	case MOST_OP_SETGET:
		if (msg->tellen != 1u)
			break;
		tune(s, msg->data[0], msg->source);
		return;
	case MOST_OP_INCREMENT:
		tune(s, (uint8_t)((cur + steps) % count + 1u), msg->source);
		return;
	case MOST_OP_DECREMENT:
		tune(s, (uint8_t)((cur + count - steps % count) % count + 1u), msg->source);
		return;
	default:
		break;
	}
	MOSTD_INFO("sdars: 0x200 SRStationSelect op=0x%X len=%u ignored",
		   msg->optype, msg->tellen);
	s->stats.unhandled++;
}

/*
 * 0x410 step command: RecordVec {direction 1 next / 0 previous, list 0 channel /
 * 1 preset / 2 category, category id}. The car sends `00 00 dir list cat`; the
 * bare 3-byte form is accepted too.
 *
 * The MMI never picks the next station itself (mmi_satradio_most.md §2), so a
 * step is ours to interpret. `list` is whichever list the MMI has open, not the
 * button: the steering wheel sends list 1 (car log of 2026-10-31), and the MMI
 * selects a preset with 0x401, never with this. With audio coming from a phone
 * every step is a track skip: hand it to the control plane as a "seek" event
 * and leave the station alone. The requester still gets the 0x200 Status a tune
 * would have sent. SDARS_SEEK=station restores stepping through the list.
 */
static void handle_step(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	const uint8_t *f;

	if (msg->optype != MOST_OP_SET && msg->optype != MOST_OP_SETGET) {
		MOSTD_INFO("sdars: 0x410 step op=0x%X ignored", msg->optype);
		s->stats.unhandled++;
		return;
	}
	if (msg->tellen == 5u && msg->data[0] == 0x00 && msg->data[1] == 0x00) {
		f = &msg->data[2];
	} else if (msg->tellen == 3u) {
		f = &msg->data[0];
	} else {
		MOSTD_INFO("sdars: 0x410 step framing not recognized (len=%u)", msg->tellen);
		s->stats.unhandled++;
		return;
	}
	if (f[0] > 1u || f[1] > 2u) {
		MOSTD_INFO("sdars: 0x410 step dir=0x%02X list=0x%02X out of range", f[0], f[1]);
		s->stats.unhandled++;
		return;
	}
	MOSTD_INFO("sdars: 0x410 step dir=%u list=%u category=0x%02X", f[0], f[1], f[2]);
	if (!s->seek_station) {
		emit(s, "seek %s %u %u", f[0] ? "next" : "prev", f[1], f[2]);
		push_property(s, msg->source, FKT_STATION_SELECT);
		return;
	}
	tune(s, step_station(s, f[0], f[1], f[2]), msg->source);
}

/*
 * 0x401 presets: Get, or the MMI's SetAt `row+1 | field+1 | value` on the
 * selection field (1). Value 2 = PS_SELECT tunes the preset; value 3 is what the
 * MMI sends from its store mode (onGUIPresetsItemStateChanged 0c1ab39c) and
 * stores the current station in that slot.
 */
static void handle_presets(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	uint8_t slot;

	if (msg->optype == MOST_OP_GET) {
		push_property(s, msg->source, FKT_PRESETS);
		return;
	}
	if ((msg->optype != MOST_OP_SET && msg->optype != MOST_OP_SETGET) ||
	    msg->tellen != 3u || msg->data[0] < 1u ||
	    msg->data[0] > sdars_preset_count(s) || msg->data[1] != 0x01 ||
	    (msg->data[2] != 0x02 && msg->data[2] != 0x03)) {
		MOSTD_INFO("sdars: 0x401 presets op=0x%X len=%u ignored",
			   msg->optype, msg->tellen);
		s->stats.unhandled++;
		return;
	}
	slot = (uint8_t)(msg->data[0] - 1u);
	if (msg->data[2] == 0x02) {
		MOSTD_INFO("sdars: 0x401 select preset %u -> SRStation=0x%02X",
			   slot, s->presets[slot]);
		tune(s, s->presets[slot], msg->source);
		if (msg->optype == MOST_OP_SETGET)
			push_property(s, msg->source, FKT_PRESETS);
		return;
	}
	MOSTD_INFO("sdars: 0x401 store SRStation=0x%02X in preset %u",
		   s->current_station, slot);
	s->presets[slot] = s->current_station;
	push_property(s, msg->source, FKT_PRESETS);
	notify_subscribers(s, FKT_PRESETS, msg->source);
	emit(s, "preset store %u %u", slot, s->current_station);
}

/*
 * 0x120 users list. The MMI registers itself as a user of the source with Set
 * `ctl | 0x10 (its FBlock) | 0 front / 1 rear | 0x7F`; the Status is the list of
 * 3-byte user triples. ctl 00 = register, 01 = unregister (MMI 9172146A
 * LOG_ENSdarsTuner 0c1ae0dc / 0c1ae170); other values leave the list alone.
 */
static void handle_users(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	uint8_t i;

	if (msg->optype == MOST_OP_GET) {
		if (push_property(s, msg->source, FKT_USERS) < 0)
			MOSTD_INFO("sdars: 0x120 users list empty; nothing to report");
		return;
	}
	if ((msg->optype != MOST_OP_SET && msg->optype != MOST_OP_SETGET) ||
	    msg->tellen != 4u) {
		MOSTD_INFO("sdars: 0x120 users op=0x%X len=%u ignored",
			   msg->optype, msg->tellen);
		s->stats.unhandled++;
		return;
	}
	for (i = 0; i < s->users_len; i += 3u)
		if (memcmp(&s->users[i], &msg->data[1], 3u) == 0)
			break;
	if (msg->data[0] > 0x01u) {
		MOSTD_INFO("sdars: 0x120 users ctl=0x%02X not recovered; list unchanged",
			   msg->data[0]);
	} else if (msg->data[0] == 0x01u) {
		if (i < s->users_len) {
			memmove(&s->users[i], &s->users[i + 3u], s->users_len - i - 3u);
			s->users_len = (uint8_t)(s->users_len - 3u);
			MOSTD_INFO("sdars: 0x120 removed user fblock=0x%02X inst=0x%02X",
				   msg->data[1], msg->data[2]);
		}
	} else {
		if (i == s->users_len && s->users_len + 3u <= sizeof(s->users)) {
			memcpy(&s->users[s->users_len], &msg->data[1], 3u);
			s->users_len = (uint8_t)(s->users_len + 3u);
			MOSTD_INFO("sdars: 0x120 added user fblock=0x%02X inst=0x%02X",
				   msg->data[1], msg->data[2]);
		}
	}
	push_property(s, msg->source, FKT_USERS);
	notify_subscribers(s, FKT_USERS, msg->source);
	if (!!s->users_len != s->ui_open) {
		s->ui_open = (uint8_t)!!s->users_len;
		emit(s, "ui %s", s->ui_open ? "open" : "closed");
	}
}

/*
 * 0xC05 GetCategoryStations.StartResultAck: SenderHandle, category. The MMI's
 * receive mask for this method excludes Result 0xC, so answer ResultAck 0xD.
 */
static void handle_category_stations(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	uint8_t result[SDARS_TX_MAX];
	uint16_t handle;
	size_t n;

	if (msg->optype != MOST_OP_STARTRESULTACK || msg->tellen != 3u) {
		MOSTD_INFO("sdars: 0xC05 GetCategoryStations op=0x%X len=%u ignored",
			   msg->optype, msg->tellen);
		s->stats.unhandled++;
		return;
	}
	handle = (uint16_t)((msg->data[0] << 8) | msg->data[1]);
	n = build_category_stations(s, handle, msg->data[2], result, sizeof(result));
	if (!n) {
		MOSTD_INFO("sdars: 0xC05 unknown category 0x%02X", msg->data[2]);
		reply_error(s, msg, MOST_ERR_PARAM_WRONG, 0x02);
		return;
	}
	if (mostd_tx_op(s->handle, msg->source, SDARS_FBLOCK, s->our_inst, msg->fktid,
			MOST_OP_RESULTACK, 0, result, n) == 0)
		s->stats.replies++;
}

/*
 * Get on a read-only property. Anything else is logged and dropped: an Error
 * reply would invalidate the function on the MMI.
 */
static void handle_property(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	if (msg->optype != MOST_OP_GET) {
		MOSTD_INFO("sdars: property 0x%03X op=0x%X ignored", msg->fktid, msg->optype);
		s->stats.unhandled++;
		return;
	}
	push_property(s, msg->source, msg->fktid);
}

int mostd_sdars_handle_msg(struct mostd_sdars *s, const struct mostd_rx_msg *msg)
{
	char *h;
	int window_slot;

	if (!s || !s->handle || !msg)
		return 0;
	if (msg->fblock != SDARS_FBLOCK)
		return 0;
	if (msg->inst != s->our_inst &&
	    msg->inst != INST_WILDCARD_ANY && msg->inst != INST_WILDCARD_ALL)
		return 0;

	/* Decode-log every SDARS request so we can implement the rest. */
	h = hex(msg->data, msg->tellen);
	MOSTD_INFO("sdars: rx src=0x%04X inst=0x%02X fkt=0x%03X op=0x%X len=%u payload=%s",
		   msg->source, msg->inst, msg->fktid, msg->optype, msg->tellen,
		   h ? h : "-");
	free_hex(h);

	window_slot = find_window_by_fktid(s, msg->source, msg->fktid);
	if (window_slot >= 0) {
		/* Get on the array-window FktID: re-serve it where it stands. */
		s->stats.handled++;
		if (msg->optype == MOST_OP_GET)
			serve_window(s, &s->window[window_slot], (uint8_t)window_slot, 0);
		else
			MOSTD_INFO("sdars: array window 0x%03X op=0x%X ignored",
				   msg->fktid, msg->optype);
		return 1;
	}

	switch (msg->fktid) {
	case FKT_FKTIDS:
		/* FktIDs.Get -> advertise our supported-function list (capability
		 * surface the head unit may gate source selectability on). */
		s->stats.handled++;
		if (msg->optype == MOST_OP_GET) {
			uint8_t b[48];
			size_t n = build_fktids(b, sizeof(b));

			if (n)
				reply_status(s, msg, b, n);
		}
		return 1;
	case FKT_NOTIFICATION:
		s->stats.handled++;
		handle_notification(s, msg);
		return 1;
	case FKT_CREATE_ARRAY_WINDOW:
		s->stats.handled++;
		handle_create_array_window(s, msg);
		return 1;
	case FKT_DESTROY_ARRAY_WINDOW:
		s->stats.handled++;
		handle_destroy_array_window(s, msg);
		return 1;
	case FKT_SYNC_DATA_INFO:
		s->stats.handled++;
		handle_sync_data_info(s, msg);
		return 1;
	case FKT_MOVE_ARRAY_WINDOW:
		s->stats.handled++;
		handle_move_array_window(s, msg);
		return 1;
	case FKT_USERS:
		s->stats.handled++;
		handle_users(s, msg);
		return 1;
	case FKT_STATION_SELECT:
		s->stats.handled++;
		handle_station_select(s, msg);
		return 1;
	case FKT_STEP:
		s->stats.handled++;
		handle_step(s, msg);
		return 1;
	case FKT_PRESETS:
		s->stats.handled++;
		handle_presets(s, msg);
		return 1;
	case FKT_GET_CATEGORY_STATIONS:
		s->stats.handled++;
		handle_category_stations(s, msg);
		return 1;
	case FKT_ELECTRONIC_CODE:
	case FKT_PROGRAM_TEXT:
	case FKT_SERVICE_STATUS:
	case FKT_0403:
	case FKT_SERVICE_ENABLED:
	case FKT_CATEGORY_INFO:
	case FKT_CURRENT_STATION_INFO:
		s->stats.handled++;
		handle_property(s, msg);
		return 1;
	case FKT_MOTHER_ARRAY:
		/* The list is only reachable through an array window (0x090). Once
		 * one exists the MMI parses any 0xC02 Status as window data, so
		 * never publish on this FktID. */
		s->stats.unhandled++;
		MOSTD_INFO("sdars: 0xC02 op=0x%X ignored; station list is window-only",
			   msg->optype);
		return 1;
	case FKT_LONG_ARRAY_INFO:
		s->stats.unhandled++;
		return 1;
	case FKT_CURRENT_STATION:
		s->stats.unhandled++;
		MOSTD_INFO("sdars: scalar fn 0x%03X op=0x%X logged; not registered on the MMI",
			   msg->fktid, msg->optype);
		return 1;
	case FKT_SOURCE_INFO:
	case FKT_ALLOCATE:
	case FKT_DEALLOCATE:
	case FKT_SOURCE_ACTIVITY:
		s->stats.handled++;
		handle_source_func(s, msg);
		return 1;
	default:
		/* Unknown (likely BMW-vendor) function: leave for the raw log so we
		 * can see exactly what the HMI wants and add a handler. */
		s->stats.unhandled++;
		return 1;
	}
}

/* ---- control-plane API (protocol/control.c) ----------------------------- */

void mostd_sdars_set_event_sink(struct mostd_sdars *s, mostd_sdars_event_fn fn, void *ctx)
{
	if (!s)
		return;
	s->event_fn = fn;
	s->event_ctx = ctx;
}

static int name_ok(const char *name, size_t max)
{
	size_t i, n = strlen(name);

	if (n > max)
		return 0;
	for (i = 0; i < n; i++)
		if ((unsigned char)name[i] < 0x20u || name[i] == '|')
			return 0;
	return 1;
}

int mostd_sdars_set_stations(struct mostd_sdars *s,
			     const struct mostd_sdars_station *stations, uint8_t count,
			     const struct mostd_sdars_category *categories, uint8_t ncat)
{
	uint8_t i, slot;

	if (!s || !stations || count < 1u || count > MOSTD_SDARS_MAX_STATIONS ||
	    ncat > MOSTD_SDARS_MAX_CATEGORIES || (ncat && !categories))
		return -1;
	for (i = 0; i < count; i++)
		if (!name_ok(stations[i].shortname, MOSTD_SDARS_SHORT_LEN) ||
		    !name_ok(stations[i].longname, MOSTD_SDARS_LONG_LEN))
			return -1;
	for (i = 0; i < ncat; i++)
		if (!name_ok(categories[i].shortname, MOSTD_SDARS_SHORT_LEN) ||
		    !name_ok(categories[i].longname, MOSTD_SDARS_LONG_LEN))
			return -1;

	memset(s->stations, 0, sizeof(s->stations));
	memcpy(s->stations, stations, (size_t)count * sizeof(*stations));
	s->station_count = count;
	memset(s->categories, 0, sizeof(s->categories));
	if (ncat)
		memcpy(s->categories, categories, (size_t)ncat * sizeof(*categories));
	s->category_count = ncat;

	/* Keep the current station and the preset slots inside the new list. */
	if (s->current_station < 1u || s->current_station > count)
		s->current_station = 1u;
	for (i = 0; i < MOSTD_SDARS_NUM_PRESETS; i++)
		if (s->presets[i] < 1u || s->presets[i] > count)
			s->presets[i] = (uint8_t)(i % count + 1u);
	MOSTD_INFO("sdars: station list replaced: %u stations, %u categories, current=%u",
		   count, ncat, s->current_station);

	/* Every list the MMI holds is stale: re-serve the open windows (the
	 * window Status carries the new total) and push the dependent lists. */
	for (slot = 0; slot < MOSTD_SDARS_WINDOW_SLOTS; slot++) {
		struct mostd_sdars_window *w = &s->window[slot];

		if (!w->active)
			continue;
		if (w->pos > window_max_pos(s, w->size))
			w->pos = window_max_pos(s, w->size);
		serve_window(s, w, slot, 0);
	}
	notify_subscribers(s, FKT_CATEGORY_INFO, 0);
	notify_subscribers(s, FKT_PRESETS, 0);
	notify_subscribers(s, FKT_CURRENT_STATION_INFO, 0);
	notify_subscribers(s, FKT_STATION_SELECT, 0);
	if (!s->text_set)
		notify_subscribers(s, FKT_PROGRAM_TEXT, 0);
	return 0;
}

int mostd_sdars_set_current(struct mostd_sdars *s, uint8_t station)
{
	if (!s || station < 1u || station > sdars_list_count(s))
		return -1;
	if (station == s->current_station)
		return 0;
	s->current_station = station;
	MOSTD_INFO("sdars: current station set to %u by the control plane", station);
	push_station_change(s, 0);
	return 0;
}

int mostd_sdars_set_text(struct mostd_sdars *s, const char *title, const char *artist,
			 const char *album)
{
	char next[3][MOSTD_SDARS_TEXT_LEN + 1u] = { { 0 } };	/* compared whole */
	const char *in[3] = { title, artist, album };
	int i;

	if (!s)
		return -1;
	for (i = 0; i < 3; i++)
		snprintf(next[i], sizeof(next[i]), "%s", in[i] ? in[i] : "");
	if (s->text_set && memcmp(next, s->text, sizeof(next)) == 0)
		return 0;
	memcpy(s->text, next, sizeof(s->text));
	s->text_set = 1;
	MOSTD_INFO("sdars: programme text \"%s\" / \"%s\" / \"%s\"",
		   s->text[0], s->text[1], s->text[2]);
	notify_subscribers(s, FKT_PROGRAM_TEXT, 0);
	return 0;
}

void mostd_sdars_set_signal(struct mostd_sdars *s, int ok)
{
	if (!s || !!ok == s->signal_ok)
		return;
	s->signal_ok = (uint8_t)!!ok;
	MOSTD_INFO("sdars: service status reception=%u", s->signal_ok);
	notify_subscribers(s, FKT_SERVICE_STATUS, 0);
}

int mostd_sdars_set_esn(struct mostd_sdars *s, const char *esn)
{
	if (!s || !esn || !name_ok(esn, MOSTD_SDARS_ESN_LEN))
		return -1;
	if (strcmp(esn, s->esn) == 0)
		return 0;
	snprintf(s->esn, sizeof(s->esn), "%s", esn);
	notify_subscribers(s, FKT_ELECTRONIC_CODE, 0);
	return 0;
}

void mostd_sdars_report_state(struct mostd_sdars *s)
{
	if (!s)
		return;
	emit(s, "current %u", s->current_station);
	emit(s, "source %s", s->source_active ? "on" : "off");
	emit(s, "ui %s", s->ui_open ? "open" : "closed");
}
