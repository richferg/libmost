#include "runtime.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../log.h"
#include "decode.h"
#include "nodecfg.h"
#include "rx.h"

/* NetBlock ShutDown (0x006) Control parameter values (MOST_Book_2 / NetBlock spec). */
static const char *shutdown_control_name(uint8_t c)
{
	switch (c) {
	case 0x00: return "Query";
	case 0x01: return "Suspend";
	case 0x02: return "Execute";
	case 0x03: return "TemperatureShutdown";
	case 0x04: return "DeviceShutdown";
	case 0x05: return "WakeFromDeviceShutdown";
	default:   return "?";
	}
}

/* Passively observe the PowerMaster's NetBlock ShutDown coordination. */
static void observe_shutdown(const struct mostd_rx_msg *msg)
{
	uint8_t control;

	if (msg->fblock != 0x01u || msg->fktid != 0x006u || msg->tellen < 1u)
		return;
	/* Payload is [SenderHandle?][Control]; Control is the trailing enum. */
	control = msg->data[msg->tellen - 1u];
	MOSTD_INFO("shutdown: src=0x%04X op=0x%X control=0x%02X (%s) len=%u",
		   msg->source, msg->optype, control, shutdown_control_name(control),
		   msg->tellen);
}

/*
 * Address to send the CentralRegistry Get (0xA01) to. The registry is
 * request-only: the NetworkMaster replies only after this Get.
 *
 * Observed on the live bench ring (most.log): the NetworkMaster sends from
 * logical address 0x0100 (NM 0x02 / NetBlock 0x01 traffic all originate there),
 * NOT a 0x04xx node-position address. So target the NM by its logical address.
 * Override per ring if the NM sits elsewhere.
 */
#define MOSTD_NM_REQUEST_ADDR 0x0100u

/*
 * Request the CentralRegistry only after the NetworkMaster has scanned our
 * NetBlock (so it has registered our advertised FBlock, e.g. SDARS 0x44, and
 * the registry we fetch contains it). Fall back to requesting anyway after a
 * few ticks in case the NM never queries us.
 */
#define MOSTD_REGISTRY_FALLBACK_TICKS 20u
#define MOSTD_HW_ERROR_LOG_INTERVAL_MS 5000u

static uint64_t monotonic_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
		return 0;
	return ((uint64_t)ts.tv_sec * 1000u) + ((uint64_t)ts.tv_nsec / 1000000u);
}

static void log_hw_error(const struct most_event *evt, uint32_t repeated)
{
	uint8_t bmsgs = evt->len >= 1 ? evt->data[0] : 0;
	uint8_t xsr = evt->len >= 2 ? evt->data[1] : (uint8_t)evt->aux;
	uint8_t flags = evt->len >= 3 ? evt->data[2] : 0;

	if (repeated) {
		MOSTD_ERROR("hw error unchanged seq=%u status=%d bMSGS=0x%02X bXSR=0x%02X flags=0x%02X repeated=%u/5s",
			    evt->seq, evt->status, bmsgs, xsr, flags, repeated);
		return;
	}

	MOSTD_ERROR("hw error seq=%u status=%d bMSGS=0x%02X bXSR=0x%02X flags=0x%02X len=%u",
		    evt->seq, evt->status, bmsgs, xsr, flags, evt->len);
}

static void handle_hw_error_log(struct mostd_runtime *runtime, const struct most_event *evt)
{
	uint64_t now_ms = monotonic_ms();
	uint8_t cached_len = evt->len < sizeof(runtime->last_hw_error_data) ?
			     (uint8_t)evt->len : (uint8_t)sizeof(runtime->last_hw_error_data);
	int changed;

	changed = !runtime->last_hw_error_valid ||
		  runtime->last_hw_error_status != evt->status ||
		  runtime->last_hw_error_aux != evt->aux ||
		  runtime->last_hw_error_len != evt->len ||
		  memcmp(runtime->last_hw_error_data, evt->data, cached_len) != 0;

	if (changed) {
		if (runtime->hw_error_suppressed)
			MOSTD_ERROR("previous hw error repeated %u additional times before changing",
				    runtime->hw_error_suppressed);

		runtime->last_hw_error_status = evt->status;
		runtime->last_hw_error_aux = evt->aux;
		runtime->last_hw_error_len = (uint8_t)evt->len;
		memset(runtime->last_hw_error_data, 0, sizeof(runtime->last_hw_error_data));
		memcpy(runtime->last_hw_error_data, evt->data, cached_len);
		runtime->last_hw_error_valid = 1;
		runtime->last_hw_error_log_ms = now_ms;
		runtime->hw_error_suppressed = 0;
		log_hw_error(evt, 0);
		return;
	}

	runtime->hw_error_suppressed++;
	if (now_ms &&
	    now_ms - runtime->last_hw_error_log_ms >= MOSTD_HW_ERROR_LOG_INTERVAL_MS) {
		log_hw_error(evt, runtime->hw_error_suppressed);
		runtime->last_hw_error_log_ms = now_ms;
		runtime->hw_error_suppressed = 0;
	}
}

static void maybe_request_registry(struct mostd_runtime *runtime)
{
	if (!runtime->registry_armed || runtime->registry_requested)
		return;
	if (!runtime->netblock_server.advertised &&
	    runtime->link_up_ticks < MOSTD_REGISTRY_FALLBACK_TICKS)
		return;

	MOSTD_INFO("requesting CentralRegistry (advertised=%u, tick=%u); subscriptions follow once it resolves",
		   runtime->netblock_server.advertised, runtime->link_up_ticks);
	mostd_registry_request(runtime->handle, MOSTD_NM_REQUEST_ADDR);
	runtime->registry_requested = 1;
}

struct ctrl_rx_dispatch_ctx {
	struct mostd_runtime *runtime;
};

/*
 * Log de-duplication: high-rate broadcasters (e.g. Vehicle 0x05 0x220) repeat
 * an identical payload many times a second and drown the log (and stall the
 * event loop with fprintf). Log a repeating (fblock,inst,fktid,payload) once,
 * count the suppressed repeats, and log again only when it changes. Large
 * payloads (segmented registry, etc.) are never deduped. All message handlers
 * still run on every message; only the console log lines are gated.
 */
#define DEDUP_SLOTS	16u
#define DEDUP_PAYLOAD	24u

struct dedup_slot {
	uint8_t used;
	uint8_t fblock;
	uint8_t inst;
	uint16_t fktid;
	uint8_t len;
	uint8_t data[DEDUP_PAYLOAD];
	uint32_t suppressed;
};

static struct dedup_slot g_dedup[DEDUP_SLOTS];
static size_t g_dedup_next;

/*
 * Returns 1 if this message should be logged (new or changed), 0 to suppress.
 * On a log-worthy message, *prev_suppressed = repeats hidden since it last
 * changed.
 */
static int dedup_should_log(const struct mostd_rx_msg *msg, uint32_t *prev_suppressed)
{
	size_t i;
	struct dedup_slot *slot = NULL;

	*prev_suppressed = 0;

	/* Don't dedup large payloads (rare, and can't be cached). */
	if (msg->tellen > DEDUP_PAYLOAD)
		return 1;

	for (i = 0; i < DEDUP_SLOTS; i++) {
		if (g_dedup[i].used && g_dedup[i].fblock == msg->fblock &&
		    g_dedup[i].inst == msg->inst && g_dedup[i].fktid == msg->fktid) {
			slot = &g_dedup[i];
			break;
		}
	}

	if (slot && slot->len == msg->tellen &&
	    memcmp(slot->data, msg->data, msg->tellen) == 0) {
		slot->suppressed++;
		return 0;
	}

	if (!slot) {
		slot = &g_dedup[g_dedup_next];
		g_dedup_next = (g_dedup_next + 1u) % DEDUP_SLOTS;
	} else {
		*prev_suppressed = slot->suppressed;
	}

	slot->used = 1;
	slot->fblock = msg->fblock;
	slot->inst = msg->inst;
	slot->fktid = msg->fktid;
	slot->len = (uint8_t)msg->tellen;
	memcpy(slot->data, msg->data, msg->tellen);
	slot->suppressed = 0;
	return 1;
}

static void mostd_runtime_handle_complete_rx(const struct mostd_rx_msg *msg, void *userdata)
{
	struct ctrl_rx_dispatch_ctx *ctx = userdata;
	struct mostd_runtime *runtime;
	char *payload_hex;
	char decoded[256];
	uint32_t prev_suppressed;
	int dec;

	if (!ctx || !ctx->runtime || !msg)
		return;

	runtime = ctx->runtime;
	runtime->stats.ctrl_rx_messages++;

	/* Learn this sender's logical address; decode the CentralRegistry. */
	mostd_addrbook_observe(&runtime->addrbook, msg);
	mostd_registry_handle_msg(&runtime->registry, msg);

	/* Answer NetBlock requests the NM/others address to us. */
	mostd_netblock_server_handle_msg(&runtime->netblock_server, msg);
	/* Once the NM has scanned us (we advertised), fetch the registry. */
	maybe_request_registry(runtime);

	/* Answer SDARS (0x44) requests from the HMI we advertise to. */
	mostd_sdars_handle_msg(&runtime->sdars, msg);
	/* Answer the v17 Diagnosis server advertised as 0x06.54. */
	mostd_diagnosis_handle_msg(&runtime->diagnosis, msg);

	/*
	 * SAT-select replay: the real module emits a Diagnosis 0x050 STATUS from
	 * its selection flow (firmware FUN_4D131E). Fire ours on the 0x120 select,
	 * toward both MMI client contexts (0x0102 service + 0x0103 list client).
	 */
	if (msg->fblock == 0x44u && msg->fktid == 0x120u &&
	    msg->optype == 0x00u /* SET */) {
		mostd_diagnosis_emit_channel_status(&runtime->diagnosis, 0x0102u);
		mostd_diagnosis_emit_channel_status(&runtime->diagnosis, 0x0103u);
	}

	observe_shutdown(msg);

	mostd_trace_cmt("rx", msg->source, msg->fblock, msg->inst, msg->fktid, msg->optype, 0,
			msg->data, msg->tellen, -1, -1, msg->segmented ? "reassembled" : "recv");

	/* Suppress identical repeats from high-rate broadcasters. */
	if (!dedup_should_log(msg, &prev_suppressed))
		return;

	payload_hex = mostd_payload_hex_alloc(msg->data, msg->tellen);
	if (!payload_hex)
		payload_hex = "-";
	if (prev_suppressed)
		MOSTD_INFO("ctrl msg src=0x%04X fb=0x%02X inst=0x%02X fkt=0x%03X op=0x%X len=%u payload=%s (prev repeated x%u)",
			   msg->source, msg->fblock, msg->inst, msg->fktid, msg->optype,
			   msg->tellen, payload_hex, prev_suppressed);
	else
		MOSTD_INFO("ctrl msg src=0x%04X fb=0x%02X inst=0x%02X fkt=0x%03X op=0x%X len=%u segmented=%u payload=%s",
			   msg->source, msg->fblock, msg->inst, msg->fktid, msg->optype,
			   msg->tellen, msg->segmented, payload_hex);
	if (payload_hex[0] != '-' || payload_hex[1] != '\0')
		free(payload_hex);

	dec = mostd_decode_telegram(msg, decoded, sizeof(decoded));
	if (dec == 1)
		MOSTD_INFO("decoded %s", decoded);
	else if (dec < 0)
		MOSTD_DEBUG("decode %s", decoded);
}

static void mostd_runtime_handle_ctrl_rx(struct mostd_runtime *runtime, const struct most_event *evt)
{
	struct ctrl_rx_dispatch_ctx ctx;

	if (!runtime || !evt)
		return;

	if (evt->len < MOST_FRAME_LEN) {
		MOSTD_ERROR("short ctrl rx event len=%u", evt->len);
		return;
	}

	runtime->stats.ctrl_rx_frames++;
	ctx.runtime = runtime;
	mostd_rx_process_frame(evt->data, mostd_runtime_handle_complete_rx, &ctx);
}

static void mostd_runtime_handle_link_change(struct mostd_runtime *runtime, const struct most_event *evt)
{
	enum most_link_state old_state;
	enum most_link_state new_state;

	if (!runtime || !evt)
		return;

	runtime->stats.link_change_events++;
	old_state = runtime->link_state;
	new_state = old_state;
	if (evt->len >= 2)
		new_state = (enum most_link_state)evt->data[1];
	runtime->link_state = new_state;

	MOSTD_INFO("link change seq=%u old=%u new=%u xsr=0x%02X",
		   evt->seq,
		   evt->len >= 1 ? evt->data[0] : old_state,
		   evt->len >= 2 ? evt->data[1] : new_state,
		   evt->len >= 3 ? evt->data[2] : 0);

	if (new_state == MOST_LINK_UP && old_state != MOST_LINK_UP) {
		/* Program our node address and group before any TX (nodecfg.c). */
		mostd_nodecfg_apply(runtime->handle);
		MOSTD_INFO("link up: deferring CentralRegistry request until we advertise our FBlock");
		/* Defer the registry request until after the NM scans our NetBlock
		 * (advertises SDARS 0x44), so the fetched registry contains it. */
		runtime->netblock_server.advertised = 0;
		runtime->registry_armed = 1;
		runtime->registry_requested = 0;
		runtime->link_up_ticks = 0;
	} else if (new_state != MOST_LINK_UP && old_state == MOST_LINK_UP) {
		runtime->registry_armed = 0;
	}
}

void mostd_runtime_init(struct mostd_runtime *runtime, struct most_handle *handle)
{
	if (!runtime)
		return;

	memset(runtime, 0, sizeof(*runtime));
	runtime->handle = handle;
	runtime->link_state = MOST_LINK_DOWN;
	mostd_rx_init();
	mostd_addrbook_init(&runtime->addrbook);
	mostd_registry_init(&runtime->registry);
	mostd_netblock_server_init(&runtime->netblock_server, handle);
	mostd_diagnosis_init(&runtime->diagnosis, handle);
	mostd_sdars_init(&runtime->sdars, handle);
}

void mostd_runtime_tick(struct mostd_runtime *runtime)
{
	if (!runtime)
		return;

	/* Fallback: request the registry even if the NM never queried our NetBlock. */
	if (runtime->registry_armed && !runtime->registry_requested) {
		if (runtime->link_up_ticks < 0xFFFFu)
			runtime->link_up_ticks++;
		maybe_request_registry(runtime);
	}

}

void mostd_runtime_handle_event(struct mostd_runtime *runtime, const struct most_event *evt)
{
	const char *name;

	if (!runtime || !evt)
		return;

	runtime->last_event_seq = evt->seq;
	name = mostd_transport_event_name(evt->type);
	mostd_trace_event(name, evt->seq, evt->status, evt->aux, evt->data, evt->len, "recv");

	switch (evt->type) {
	case MOST_EVT_CTRL_RX:
		mostd_runtime_handle_ctrl_rx(runtime, evt);
		break;
	case MOST_EVT_CTRL_TX_DONE:
		runtime->stats.ctrl_tx_done++;
		MOSTD_DEBUG("ctrl tx done seq=%u", evt->seq);
		break;
	case MOST_EVT_CTRL_TX_FAIL:
		runtime->stats.ctrl_tx_fail++;
		MOSTD_ERROR("ctrl tx fail seq=%u status=%d aux=0x%04X",
			    evt->seq, evt->status, evt->aux);
		break;
	case MOST_EVT_ASYNC_RX:
		break;
	case MOST_EVT_ASYNC_TX_DONE:
		MOSTD_INFO("async tx done seq=%u", evt->seq);
		break;
	case MOST_EVT_ASYNC_TX_FAIL:
		MOSTD_ERROR("async tx fail seq=%u status=%d", evt->seq, evt->status);
		break;
	case MOST_EVT_LINK_CHANGE:
		mostd_runtime_handle_link_change(runtime, evt);
		break;
	case MOST_EVT_HW_ERROR:
		runtime->stats.hw_error_events++;
		handle_hw_error_log(runtime, evt);
		break;
	case MOST_EVT_TOPOLOGY_CHANGE:
		runtime->stats.topology_change_events++;
		runtime->last_topology_aux = evt->aux;
		MOSTD_INFO("topology change seq=%u aux=0x%04X", evt->seq, evt->aux);
		break;
	default:
		MOSTD_DEBUG("driver event kind=%s seq=%u status=%d aux=0x%04X len=%u",
			    name, evt->seq, evt->status, evt->aux, evt->len);
		break;
	}
}
