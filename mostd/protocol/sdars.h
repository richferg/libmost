#ifndef MOSTD_PROTOCOL_SDARS_H
#define MOSTD_PROTOCOL_SDARS_H

#include <stdint.h>

#include "most.h"

struct mostd_rx_msg;

/*
 * Our SatRadio identity on the ring: FBlock 0x44, InstID 0x00. One definition,
 * used both by the NetBlock FBlockIDs advertisement (netblock_server.c) and by
 * every reply the responder sends (sdars.c). The SDAR65 firmware registers and
 * answers as 0x44.00, and ASK's connection profile 0x6B names the source as
 * 0x44.00 and keys its pending source build on the FBlock/InstID of the reply.
 * (Until 2026-10-27 we registered as 0x44.01 and echoed the request's InstID
 * on the source functions only, which worked but gave us two identities.)
 */
#define MOSTD_SDARS_FBLOCK 0x44u
#define MOSTD_SDARS_INST 0x00u

#define MOSTD_SDARS_MAX_SUBSCRIPTIONS 32u
#define MOSTD_SDARS_WINDOW_SLOTS 4u
#define MOSTD_SDARS_NUM_PRESETS 6u
#define MOSTD_SDARS_MAX_USERS 11u	/* MMI reads <=34 bytes of triples */

/* Runtime station model limits, from the MMI parser (mmi_satradio_most.md
 * §2): 0x401 holds <=24 rows, short names <=8, long names <=20, programme text
 * fields <=36, ESN <=12. */
#define MOSTD_SDARS_MAX_STATIONS 24u
#define MOSTD_SDARS_MAX_CATEGORIES 8u
#define MOSTD_SDARS_SHORT_LEN 8u
#define MOSTD_SDARS_LONG_LEN 20u
#define MOSTD_SDARS_TEXT_LEN 36u
#define MOSTD_SDARS_ESN_LEN 12u

struct mostd_sdars_station {
	char shortname[MOSTD_SDARS_SHORT_LEN + 1u];
	char longname[MOSTD_SDARS_LONG_LEN + 1u];
	uint8_t category;
	uint8_t subscribed;
};

struct mostd_sdars_category {
	uint8_t id;
	char shortname[MOSTD_SDARS_SHORT_LEN + 1u];
	char longname[MOSTD_SDARS_LONG_LEN + 1u];
};

/*
 * Events for whoever drives the audio behind this FBlock (the Bluetooth agent,
 * via protocol/control.c). One line each, no newline:
 *   "seek next|prev <list> <category>"   0x410 step the module should act on
 *   "tune <station>"                     the MMI selected a station (1-based)
 *   "source on|off"                      ASK SourceActivity / Deallocate edge
 *   "ui open|closed"                     first MMI registered / last unregistered (0x120)
 *   "preset store <slot> <station>"      the MMI stored a preset
 *   "current <station>"                  state report (on request)
 */
typedef void (*mostd_sdars_event_fn)(void *ctx, const char *event);

struct mostd_sdars_subscription {
	uint16_t device;
	uint16_t fktid;
};

struct mostd_sdars_window {
	uint8_t active;
	uint16_t peer;
	uint16_t sender_handle;
	uint16_t mother;
	uint8_t size;
	uint16_t pos;		/* index of the first row served (mArrayWndAbsPos) */
};

/*
 * Minimal SDARS (SatRadio, FBlock 0x44) responder. We advertise 0x44 via the
 * NetBlock (see netblock_server.c); this answers the head unit / HMI controllers
 * that then talk to it, so we can bring the emulated SDARS source up far enough
 * for the ConnectionMaster to wire our audio to the amplifier.
 *
 * BMW's SDARS predates the MOST spec and uses vendor-specific FktIDs, so most of
 * this is discovered empirically: every request is decoded + logged, the known
 * coordination methods are answered, and the rest is left for the raw log so we
 * can implement them iteratively.
 */
struct mostd_sdars {
	struct most_handle *handle;
	uint8_t our_inst;
	struct mostd_sdars_subscription notify[MOSTD_SDARS_MAX_SUBSCRIPTIONS];
	uint8_t notify_count;
	struct mostd_sdars_window window[MOSTD_SDARS_WINDOW_SLOTS];
	uint8_t current_station;
	uint8_t presets[MOSTD_SDARS_NUM_PRESETS];	/* station stored in each preset slot */
	/* The station list the MMI sees. Station numbers are 1-based positions in
	 * stations[]; the Bluetooth agent replaces the whole table at once. */
	struct mostd_sdars_station stations[MOSTD_SDARS_MAX_STATIONS];
	uint8_t station_count;
	struct mostd_sdars_category categories[MOSTD_SDARS_MAX_CATEGORIES];
	uint8_t category_count;
	/* 0x206 programme text f0 title, f1 artist, f2 album. Until the agent sets
	 * it (text_set), it is derived from the current station's names. */
	char text[3][MOSTD_SDARS_TEXT_LEN + 1u];
	uint8_t text_set;
	char esn[MOSTD_SDARS_ESN_LEN + 1u];	/* 0x205 */
	uint8_t signal_ok;		/* 0x400 f0: 0 = "no reception" on the MMI */
	uint8_t seek_station;		/* SDARS_SEEK=station: 0x410 steps tune
					 * instead of emitting a seek event */
	uint8_t source_active;		/* last SourceActivity edge reported */
	uint8_t ui_open;		/* last 0x120 users edge reported */
	mostd_sdars_event_fn event_fn;
	void *event_ctx;
	/* 0x120 users list: 3-byte {FBlockID, InstID, x} triples, as registered. */
	uint8_t users[MOSTD_SDARS_MAX_USERS * 3u];
	uint8_t users_len;
	/* Synchronous audio-source state (Resource Allocate + OS8104A routing). The
	 * MMI gates SAT activation on us being an active sync source in its
	 * HIGH_SYNC_CONNECTION_TABLE (FID_SVS_STATUS running), not just on control
	 * STATUS. We allocate channels from the timing-master and route our source
	 * port onto them so the ConnectionMaster can wire us to the amp. */
	uint8_t src_allocated;            /* 1 once channels granted + routed */
	uint8_t src_nchan;                /* number of channels granted */
	uint8_t src_label;                /* P0 = connection label (for de-alloc) */
	uint8_t src_delay;                /* node-position delay reported to ASK */
	uint8_t src_chan[8];              /* granted physical channels (MRT bytes) */
	struct {
		uint32_t handled;
		uint32_t replies;
		uint32_t errors;
		uint32_t unhandled;
	} stats;
};

void mostd_sdars_init(struct mostd_sdars *s, struct most_handle *handle);

/* ---- control-plane API (protocol/control.c, the Bluetooth agent) --------- */
void mostd_sdars_set_event_sink(struct mostd_sdars *s, mostd_sdars_event_fn fn, void *ctx);
/* Replace the station and category tables; re-publishes every list the MMI
 * holds (open array windows, 0xC04, 0x401, 0xC10, 0x200). Returns -1 and
 * changes nothing if a name is too long or the counts are out of range. */
int mostd_sdars_set_stations(struct mostd_sdars *s,
			     const struct mostd_sdars_station *stations, uint8_t count,
			     const struct mostd_sdars_category *categories, uint8_t ncat);
/* The agent reports which station is playing (no "tune" event back). */
int mostd_sdars_set_current(struct mostd_sdars *s, uint8_t station);
/* Programme text; each field is truncated to 36 bytes. Pushes 0x206 on change. */
int mostd_sdars_set_text(struct mostd_sdars *s, const char *title, const char *artist,
			 const char *album);
void mostd_sdars_set_signal(struct mostd_sdars *s, int ok);
int mostd_sdars_set_esn(struct mostd_sdars *s, const char *esn);
/* Emit "current", "source" and "ui" events describing the present state. */
void mostd_sdars_report_state(struct mostd_sdars *s);

/* Returns 1 if the message was for SDARS (0x44) and consumed, else 0. */
int mostd_sdars_handle_msg(struct mostd_sdars *s, const struct mostd_rx_msg *msg);

#endif /* MOSTD_PROTOCOL_SDARS_H */
