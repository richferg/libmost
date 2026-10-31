#ifndef MOSTD_PROTOCOL_NETBLOCK_SERVER_H
#define MOSTD_PROTOCOL_NETBLOCK_SERVER_H

#include <stdint.h>

#include "most.h"
#include "rx.h"

/*
 * Minimal NetBlock (FBlock 0x01) server for our own node.
 *
 * Every MOST node must host a NetBlock the NetworkMaster can query; without it
 * we look half-present and the NM keeps re-polling. This answers the management
 * requests addressed to us:
 *   0x000 FBlockIDs           Get -> STATUS {0x44,0x00, 0x06,0x54}
 *   0x001 DeviceInfo          Get <id> -> STATUS {id, identity string}
 *   0x002 NodePositionAddress Get -> STATUS {0x04, position}
 *   0x003 NodeAddress         Get -> {nah,nal}; Set/SetGet -> reprogram + STATUS
 *   0x004 GroupAddress        Get -> {0x03,ga};  Set/SetGet -> reprogram + STATUS
 *   0xC00 (Becker)            Get -> "nothing unavailable"
 *
 * Position and addresses are read live from the INIC. NodeAddress.Set is how
 * the NM moves us: we program it (most_set_node_addr) and STATUS-ack.
 *
 * Per MOST_Book_2 §4, NetBlock InstID = ring position (bNPR); we respond with
 * that real inst. Requests may use the InstID wildcards (0x00 = any instance,
 * 0xFF = all) - we accept them, but responses always carry our real inst, as
 * the spec requires.
 */

struct mostd_netblock_server_stats {
	uint32_t handled;
	uint32_t replies;
	uint32_t errors;
	uint32_t node_addr_assigned;
	uint32_t group_addr_assigned;
};

struct mostd_netblock_server {
	struct most_handle *handle;
	uint8_t our_inst;	/* our NetBlock InstID = ring position (bNPR) */
	uint8_t advertised;	/* set once we answered an FBlockIDs.Get (NM scanned us) */
	struct mostd_netblock_server_stats stats;
};

void mostd_netblock_server_init(struct mostd_netblock_server *nb, struct most_handle *handle);

/* Handle a NetBlock request addressed to us. Returns 1 if handled, else 0. */
int mostd_netblock_server_handle_msg(struct mostd_netblock_server *nb,
				     const struct mostd_rx_msg *msg);

#endif /* MOSTD_PROTOCOL_NETBLOCK_SERVER_H */
