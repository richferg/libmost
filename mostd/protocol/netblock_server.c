#include "netblock_server.h"
#include "sdars.h"

#include <string.h>

#include "../log.h"
#include "cmt.h"
#include "tx.h"

#define NETBLOCK_FBLOCK			0x01u
/*
 * Per MOST_Book_2 §4: for NetBlock (and EnhancedTestability) the InstID equals
 * the ring position (TimingMaster = 0x00). So our NetBlock inst is bNPR, read
 * live. Until the first successful read, fall back to this default.
 */
#define NETBLOCK_INST_DEFAULT		0x0Du
#define OS8104_REG_BNPR			0x87u
/* Request-only InstID wildcards: 0x00 = any instance, 0xFF = all instances. */
#define INST_WILDCARD_ANY		0x00u
#define INST_WILDCARD_ALL		0xFFu

#define NB_FKT_FBLOCKIDS		0x000u
#define NB_FKT_DEVICEINFO		0x001u
#define NB_FKT_NODE_POSITION_ADDRESS	0x002u
#define NB_FKT_NODE_ADDRESS		0x003u
#define NB_FKT_GROUP_ADDRESS		0x004u
/*
 * Becker BMW-E65 proprietary NetBlock fn (not in MOST25 spec). MMI's
 * CRBModelNetBlock::readFromMsg(ISrvNetBlock::FID_UNAVAILABILITY) GETs each
 * node's unavailable-function set; if unanswered, the MMI may treat the node's
 * functions as unavailable and refuse to activate SAT. We reply "nothing
 * unavailable" so the SDARS looks fully available.
 */
#define NB_FKT_UNAVAILABILITY		0xC00u

/*
 * V17 hosts both the SatRadio application and its Diagnosis server. Both must
 * be advertised so the NetworkMaster routes explicitly addressed traffic.
 */
#define SDARS_FBLOCK			MOSTD_SDARS_FBLOCK
#define SDARS_INST			MOSTD_SDARS_INST
#define DIAGNOSIS_FBLOCK		0x06u
#define DIAGNOSIS_INST			0x54u

/* GroupAddress payload tag. */
#define NB_GROUP_ADDR_TAG		0x03u

/* MOST error codes. */
#define MOST_ERR_UNKNOWN_OPTYPE		0x02u
#define MOST_ERR_PARAMETER_ERROR	0x03u
#define MOST_ERR_DEVICE_NOT_READY	0x07u

void mostd_netblock_server_init(struct mostd_netblock_server *nb, struct most_handle *handle)
{
	if (!nb)
		return;
	memset(nb, 0, sizeof(*nb));
	nb->handle = handle;
	nb->our_inst = NETBLOCK_INST_DEFAULT;
}

/* Our NetBlock InstID = ring position (bNPR), refreshed live; cache last good. */
static uint8_t our_netblock_inst(struct mostd_netblock_server *nb)
{
	uint8_t pos;

	if (most_reg_read(nb->handle, OS8104_REG_BNPR, &pos, 1) == 0 && pos != 0xFFu)
		nb->our_inst = pos;
	return nb->our_inst;
}

/* Responses must carry our real InstID, never the (possibly wildcard) request inst. */
static int reply_status(struct mostd_netblock_server *nb, const struct mostd_rx_msg *msg,
			const uint8_t *payload, size_t len)
{
	int rc = mostd_tx_status(nb->handle, msg->source, NETBLOCK_FBLOCK, nb->our_inst,
				 msg->fktid, 0, payload, len);
	if (rc < 0)
		return rc;
	nb->stats.replies++;
	return 0;
}

static int reply_error(struct mostd_netblock_server *nb, const struct mostd_rx_msg *msg,
		       uint8_t code, uint8_t info)
{
	int rc = mostd_tx_error_for_request(nb->handle, msg->source, NETBLOCK_FBLOCK, nb->our_inst,
					    msg->fktid, 0, msg->optype, msg->data, msg->tellen,
					    code, info);
	if (rc < 0)
		return rc;
	nb->stats.errors++;
	return 0;
}

static int handle_fblockids(struct mostd_netblock_server *nb, const struct mostd_rx_msg *msg)
{
	static const uint8_t payload[] = {
		SDARS_FBLOCK, SDARS_INST,
		DIAGNOSIS_FBLOCK, DIAGNOSIS_INST
	};

	if (msg->optype != MOST_OP_GET)
		return reply_error(nb, msg, MOST_ERR_UNKNOWN_OPTYPE, msg->optype);

	/*
	 * FBlockIDs.Status = bare {FBlockID, InstID} pairs, NO branch byte.
	 * Confirmed against real ring nodes (e.g. 0x0105 raw=40000647 =
	 * {0x40,0x00}{0x06,0x47}). The leading 0x02 on the NM node is FBlock
	 * 0x02 (NetworkMaster), not a classifier. Advertise SDARS so the
	 * NetworkMaster registers both application FBlocks at our address.
	 */
	nb->advertised = 1;
	MOSTD_INFO("netblock: FBlockIDs.Get from 0x%04X -> advertise 0x%02X.%02X, 0x%02X.%02X",
		   msg->source, SDARS_FBLOCK, SDARS_INST,
		   DIAGNOSIS_FBLOCK, DIAGNOSIS_INST);
	return reply_status(nb, msg, payload, sizeof(payload));
}

/*
 * DeviceInfo (0x001) = KWP2000 ReadECUIdentification: a tagged item set addressed by a
 * 1-byte item ID (the GET selector); response = [ID][content]. The previous code IGNORED
 * the selector and returned network info (ndr/npr/ga/addr) for EVERY item, so any read of
 * our CompanyName/ProductName/ProductVersion got garbage. Serve the requested item; identity
 * strings are the real Visteon SDARS module's (from 9142852A: "Copyright 2003, Visteon Corp."
 * / "SDR_02_01.01"). NodeAddress/Position come from the dedicated funcs 0x002/0x003, not here.
 */
static int handle_deviceinfo(struct mostd_netblock_server *nb, const struct mostd_rx_msg *msg)
{
	uint8_t payload[40];
	const char *content;
	uint8_t id;
	size_t clen;

	if (msg->optype != MOST_OP_GET)
		return reply_error(nb, msg, MOST_ERR_UNKNOWN_OPTYPE, msg->optype);
	if (msg->tellen < 1u)
		return reply_error(nb, msg, MOST_ERR_PARAMETER_ERROR, 0);
	id = msg->data[0];

	switch (id) {
	case 0x00: content = "Visteon Corp."; break;	/* CompanyName */
	case 0x01: content = "SDARS"; break;		/* ProductName */
	case 0x02: content = "SDR_02_01.01"; break;	/* ProductVersion */
	case 0x03: content = "0000000000"; break;	/* ProductSerialNumber */
	default:
		/* Unsupported item -> parameter error (not garbage). Mandatory items
		 * beyond identity (0x07 CapabilityToWake, KWP HW/SW numbers) would need
		 * the producer's exact values if the MMI requires them. */
		return reply_error(nb, msg, MOST_ERR_PARAMETER_ERROR, id);
	}
	clen = strlen(content);
	if (1u + clen > sizeof(payload))
		clen = sizeof(payload) - 1u;
	payload[0] = id;
	memcpy(&payload[1], content, clen);
	MOSTD_INFO("netblock: DeviceInfo.Get id=0x%02X -> \"%s\"", id, content);
	return reply_status(nb, msg, payload, 1u + clen);
}

static int handle_node_position(struct mostd_netblock_server *nb, const struct mostd_rx_msg *msg)
{
	uint8_t npr = 0;
	uint8_t payload[2];

	if (msg->optype != MOST_OP_GET)
		return reply_error(nb, msg, MOST_ERR_UNKNOWN_OPTYPE, msg->optype);
	if (most_reg_read(nb->handle, OS8104_REG_BNPR, &npr, 1) < 0)
		return reply_error(nb, msg, MOST_ERR_DEVICE_NOT_READY, 0);

	/* Reply is the 2-byte node-position address 0x04NN (NN = ring position). */
	payload[0] = 0x04;
	payload[1] = npr;
	MOSTD_INFO("netblock: NodePositionAddress.Get -> posaddr=0x04%02X", npr);
	return reply_status(nb, msg, payload, sizeof(payload));
}

static int handle_node_address(struct mostd_netblock_server *nb, const struct mostd_rx_msg *msg)
{
	uint16_t addr = 0;
	uint8_t payload[2];

	if (msg->optype == MOST_OP_GET) {
		if (most_get_node_addr(nb->handle, &addr) < 0)
			return reply_error(nb, msg, MOST_ERR_DEVICE_NOT_READY, 0);
	} else if (msg->optype == MOST_OP_SET || msg->optype == MOST_OP_SETGET) {
		/* NetworkMaster assigns our node address: accept and program it. */
		if (msg->tellen < 2u)
			return reply_error(nb, msg, MOST_ERR_PARAMETER_ERROR, (uint8_t)msg->tellen);
		addr = (uint16_t)((msg->data[0] << 8) | msg->data[1]);
		if (most_set_node_addr(nb->handle, addr) < 0) {
			MOSTD_ERROR("netblock: NodeAddress.Set 0x%04X program failed", addr);
			return reply_error(nb, msg, MOST_ERR_DEVICE_NOT_READY, 0);
		}
		nb->stats.node_addr_assigned++;
		MOSTD_INFO("netblock: NodeAddress.Set from 0x%04X -> assigned node addr 0x%04X (programmed)",
			   msg->source, addr);
	} else {
		return reply_error(nb, msg, MOST_ERR_UNKNOWN_OPTYPE, msg->optype);
	}

	payload[0] = (uint8_t)(addr >> 8);
	payload[1] = (uint8_t)(addr & 0xFFu);
	return reply_status(nb, msg, payload, sizeof(payload));
}

static int handle_group_address(struct mostd_netblock_server *nb, const struct mostd_rx_msg *msg)
{
	uint8_t group = 0;
	uint8_t payload[2];

	if (msg->optype == MOST_OP_GET) {
		if (most_get_group_addr(nb->handle, &group) < 0)
			return reply_error(nb, msg, MOST_ERR_DEVICE_NOT_READY, 0);
	} else if (msg->optype == MOST_OP_SET || msg->optype == MOST_OP_SETGET) {
		if (msg->tellen < 2u || msg->data[0] != NB_GROUP_ADDR_TAG)
			return reply_error(nb, msg, MOST_ERR_PARAMETER_ERROR,
					   msg->tellen >= 1u ? msg->data[0] : 0);
		group = msg->data[1];
		if (most_set_group_addr(nb->handle, group) < 0)
			return reply_error(nb, msg, MOST_ERR_DEVICE_NOT_READY, 0);
		nb->stats.group_addr_assigned++;
		MOSTD_INFO("netblock: GroupAddress.Set from 0x%04X -> group 0x%02X (programmed)",
			   msg->source, group);
	} else {
		return reply_error(nb, msg, MOST_ERR_UNKNOWN_OPTYPE, msg->optype);
	}

	payload[0] = NB_GROUP_ADDR_TAG;
	payload[1] = group;
	return reply_status(nb, msg, payload, sizeof(payload));
}

static int handle_unavailability(struct mostd_netblock_server *nb,
				 const struct mostd_rx_msg *msg)
{
	/*
	 * GET carries a 2-byte selector (observed 00 00 = wildcard scope). Reply
	 * "nothing unavailable": echo the selector with no trailing entries. Exact
	 * Becker payload format is unknown (handler is trace-walled) — best-effort.
	 */
	uint8_t payload[2] = { 0x00, 0x00 };

	if (msg->optype != MOST_OP_GET)
		return reply_error(nb, msg, MOST_ERR_UNKNOWN_OPTYPE, msg->optype);
	if (msg->tellen >= 2u) {
		payload[0] = msg->data[0];
		payload[1] = msg->data[1];
	}
	MOSTD_INFO("netblock: FID_UNAVAILABILITY(0xC00).Get from 0x%04X -> reply nothing-unavailable",
		   msg->source);
	return reply_status(nb, msg, payload, sizeof(payload));
}

int mostd_netblock_server_handle_msg(struct mostd_netblock_server *nb,
				     const struct mostd_rx_msg *msg)
{
	if (!nb || !nb->handle || !msg)
		return 0;
	if (msg->fblock != NETBLOCK_FBLOCK)
		return 0;
	/* Only requests (Get/Set/SetGet); ignore responses/notifications. */
	if (msg->optype > MOST_OP_GETINTERFACE)
		return 0;
	/*
	 * Accept our own NetBlock instance (= ring position) or a request-only
	 * InstID wildcard (0x00 = any, 0xFF = all). Replies carry our real inst.
	 */
	(void)our_netblock_inst(nb);
	if (msg->inst != nb->our_inst &&
	    msg->inst != INST_WILDCARD_ANY && msg->inst != INST_WILDCARD_ALL)
		return 0;

	switch (msg->fktid) {
	case NB_FKT_FBLOCKIDS:
		nb->stats.handled++;
		handle_fblockids(nb, msg);
		return 1;
	case NB_FKT_DEVICEINFO:
		nb->stats.handled++;
		handle_deviceinfo(nb, msg);
		return 1;
	case NB_FKT_NODE_POSITION_ADDRESS:
		nb->stats.handled++;
		handle_node_position(nb, msg);
		return 1;
	case NB_FKT_NODE_ADDRESS:
		nb->stats.handled++;
		handle_node_address(nb, msg);
		return 1;
	case NB_FKT_GROUP_ADDRESS:
		nb->stats.handled++;
		handle_group_address(nb, msg);
		return 1;
	case NB_FKT_UNAVAILABILITY:
		nb->stats.handled++;
		handle_unavailability(nb, msg);
		return 1;
	default:
		/* Unknown NetBlock function: leave it for the raw log, no reply. */
		return 0;
	}
}
