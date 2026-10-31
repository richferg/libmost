#include "diagnosis.h"

#include <string.h>

#include "../log.h"
#include "cmt.h"
#include "diagnosis_jobmap.h"
#include "tx.h"

#define INST_WILDCARD_ANY	0x00u
#define INST_WILDCARD_ALL	0xFFu

#define FKT_FKTIDS		0x000u
#define FKT_DIAGNOSTICS_CHANNEL	0x050u

/*
 * Routing byte the v17 transport ingest (FUN_4C72E8) keys on: a tunneled KWP
 * request frame is laid out [src][0x54 routing][service][params...], so the
 * SDARS diag node id 0x54 sits at payload offset 1.
 */
#define KWP_ROUTING_BYTE	0x54u
#define KWP_ROUTING_OFFSET	1u
#define KWP_SERVICE_OFFSET	2u

/* KWP2000 service id -> short name, for readable trace logs (no behavior). */
static const char *kwp_service_name(uint8_t sid)
{
	switch (sid) {
	case 0x10: return "StartDiagnosticSession";
	case 0x11: return "EcuReset";
	case 0x14: return "ClearDiagnosticInformation";
	case 0x17: return "ReadStatusOfDtc";
	case 0x18: return "ReadDtcByStatus";
	case 0x1A: return "ReadEcuIdentification";
	case 0x20: return "StopDiagnosticSession";
	case 0x21: return "ReadDataByLocalIdentifier";
	case 0x22: return "ReadDataByCommonIdentifier";
	case 0x23: return "ReadMemoryByAddress";
	case 0x27: return "SecurityAccess";
	case 0x28: return "DisableNormalMessageTransmission";
	case 0x29: return "EnableNormalMessageTransmission";
	case 0x2E: return "WriteDataByCommonIdentifier";
	case 0x31: return "StartRoutineByLocalIdentifier";
	case 0x34: return "RequestDownload";
	case 0x36: return "TransferData";
	case 0x37: return "RequestTransferExit";
	case 0x3B: return "WriteDataByLocalIdentifier";
	case 0x3D: return "WriteMemoryByAddress";
	case 0x3E: return "TesterPresent";
	default:   return "?";
	}
}

/*
 * V17 FUN_4C7510 returns this exact nibble-packed function directory:
 * 0x001, 0x050, 0x051.
 */
static const uint8_t diagnosis_fktids[] = {
	0x00, 0x10, 0x50, 0x05, 0x10
};

void mostd_diagnosis_init(struct mostd_diagnosis *diag, struct most_handle *handle)
{
	if (!diag)
		return;
	memset(diag, 0, sizeof(*diag));
	diag->handle = handle;
}

static int reply_status(struct mostd_diagnosis *diag,
			const struct mostd_rx_msg *msg,
			const uint8_t *payload, size_t length)
{
	if (mostd_tx_status(diag->handle, msg->source, MOSTD_DIAGNOSIS_FBLOCK,
			    MOSTD_DIAGNOSIS_INST, msg->fktid, 0,
			    payload, length) < 0)
		return -1;
	diag->stats.replies++;
	return 0;
}

/*
 * Proactively emit the DiagnosticsChannel (0x050) STATUS the real module sends
 * from its SAT selection flow (firmware FUN_4D131E -> FUN_4D15DC). Minimal form:
 * the 6-byte payload `54 f1 71 fc 01 02` (inst echo + fixed header). Best-effort
 * replay — the trailing encoded station records (FUN_4D153F) aren't decoded.
 */
int mostd_diagnosis_emit_channel_status(struct mostd_diagnosis *diag, uint16_t dst)
{
	static const uint8_t payload[] = { 0x54, 0xf1, 0x71, 0xfc, 0x01, 0x02 };

	if (!diag || !diag->handle)
		return -1;
	if (mostd_tx_status(diag->handle, dst, MOSTD_DIAGNOSIS_FBLOCK,
			    MOSTD_DIAGNOSIS_INST, FKT_DIAGNOSTICS_CHANNEL, 0,
			    payload, sizeof(payload)) < 0)
		return -1;
	diag->stats.replies++;
	MOSTD_INFO("diagnosis: emitted DiagnosticsChannel(0x050) STATUS to 0x%04X "
		   "(SAT-select replay)", dst);
	return 0;
}

static void handle_fktids(struct mostd_diagnosis *diag,
			  const struct mostd_rx_msg *msg)
{
	if (msg->optype != MOST_OP_GET)
		return;

	reply_status(diag, msg, diagnosis_fktids, sizeof(diagnosis_fktids));
}

static void handle_diagnostics_channel(struct mostd_diagnosis *diag,
				       const struct mostd_rx_msg *msg)
{
	if (msg->optype != MOST_OP_SET)
		return;

	if (msg->tellen == 0u) {
		MOSTD_INFO("diagnosis: ignored empty DiagnosticsChannel.Set from 0x%04X",
			   msg->source);
		return;
	}

	/*
	 * Two documented inbound forms (DiagnosisFBlock06.md):
	 *
	 * (A) Immediate-leaf request, FUN_4C1650: byte0 mode 0/1, >=3 bytes, byte2
	 *     is the backend command selector dispatched into FUN_4D6BF5. The leaf
	 *     emits NO immediate MOST reply; the backend owns any later Status/Error.
	 *
	 * (B) Tunneled KWP transport record, FUN_4C72E8: routing byte 0x54 at
	 *     offset 1, KWP service id at offset 2 (e.g. EDIABAS IDENT -> 1A 80).
	 *     The ingest stores the requester and the result is emitted later as an
	 *     async 0x050 Status. Absent a modeled backend we classify and log only
	 *     — synthesizing the runtime-derived KWP reply here would be invented
	 *     behavior the firmware evidence forbids.
	 */
	if (msg->tellen >= 3u && msg->data[0] <= 1u) {
		diag->stats.deferred++;
		MOSTD_INFO("diagnosis: DiagnosticsChannel.Set (leaf) from 0x%04X mode=%u command=0x%02X len=%u; backend deferred",
			   msg->source, msg->data[0], msg->data[2], msg->tellen);
		return;
	}

	if (msg->tellen > KWP_SERVICE_OFFSET &&
	    msg->data[KWP_ROUTING_OFFSET] == KWP_ROUTING_BYTE) {
		uint8_t sid = msg->data[KWP_SERVICE_OFFSET];
		uint8_t sub = (msg->tellen > KWP_SERVICE_OFFSET + 1u)
				? msg->data[KWP_SERVICE_OFFSET + 1u] : 0u;
		const char *job = kwp_job_name(sid, sub);

		diag->stats.kwp_records++;
		MOSTD_INFO("diagnosis: KWP record from 0x%04X service=0x%02X (%s) sub=0x%02X job=%s len=%u; backend not modeled, no reply",
			   msg->source, sid, kwp_service_name(sid), sub,
			   job ? job : "?", msg->tellen);
		return;
	}

	MOSTD_INFO("diagnosis: unclassified DiagnosticsChannel.Set from 0x%04X byte0=0x%02X len=%u",
		   msg->source, msg->data[0], msg->tellen);
}

int mostd_diagnosis_handle_msg(struct mostd_diagnosis *diag,
			       const struct mostd_rx_msg *msg)
{
	if (!diag || !diag->handle || !msg)
		return 0;
	if (msg->fblock != MOSTD_DIAGNOSIS_FBLOCK)
		return 0;
	if (msg->inst != MOSTD_DIAGNOSIS_INST &&
	    msg->inst != INST_WILDCARD_ANY && msg->inst != INST_WILDCARD_ALL)
		return 0;
	if (msg->optype > MOST_OP_GETINTERFACE)
		return 0;

	switch (msg->fktid) {
	case FKT_FKTIDS:
		diag->stats.handled++;
		handle_fktids(diag, msg);
		return 1;
	case FKT_DIAGNOSTICS_CHANNEL:
		diag->stats.handled++;
		handle_diagnostics_channel(diag, msg);
		return 1;
	default:
		return 0;
	}
}
