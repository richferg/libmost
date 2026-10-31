#include "nodecfg.h"

#include <stdint.h>

#include "most.h"
#include "../log.h"
#include "frame_layout.h"

/* OS8104 node position register (DeviceInfo bNPR). */
#define OS8104_REG_BNPR 0x87u

/*
 * The production SDAR65 v17 OS8104A image programs hardware multicast group
 * 0x62. This is independent of the application FBlock ID 0x44. The
 * NetworkMaster may still reassign it later through NetBlock GroupAddress.Set.
 */
#define MOSTD_NODE_GROUP_ADDR 0x62u

/*
 * Diagnostic node logical address.
 *
 * We are a late-joining add-on node. Productive logical addresses are sticky:
 * each ECU keeps the address it was assigned at the original ring config, even
 * after our insertion shifts physical positions. So 0x0100|our-position is NOT
 * free - it collides with whichever ECU already owns that logical address (seen
 * live: our 0x010D vs the phone node's 0x010D). Use a fixed address in the free
 * slot above the productive ring instead; if the NetworkMaster wants to move us
 * it can, via the NetBlock NodeAddress.Set handler (netblock_server.c).
 *
 * 0x0110 is the first free slot on the observed 15-node ring. Adjust if a ring
 * populates it.
 */
#define MOSTD_DIAG_NODE_ADDR 0x0110u

int mostd_nodecfg_apply(struct most_handle *h)
{
	uint8_t pos = 0;
	uint16_t addr = MOSTD_DIAG_NODE_ADDR;
	uint16_t cur = 0;

	if (!h)
		return -1;

	/* Position is read for information only; it must not drive our address. */
	if (most_reg_read(h, OS8104_REG_BNPR, &pos, 1) < 0)
		pos = 0xFFu;

	most_get_node_addr(h, &cur);

	if (most_set_node_addr(h, addr) < 0) {
		MOSTD_ERROR("nodecfg: set node addr 0x%04X failed", addr);
		return -1;
	}
	if (most_set_group_addr(h, MOSTD_NODE_GROUP_ADDR) < 0)
		MOSTD_ERROR("nodecfg: set group addr 0x%02X failed", MOSTD_NODE_GROUP_ADDR);

	MOSTD_INFO("nodecfg: position=0x%02X, node addr 0x%04X (was 0x%04X), group 0x%02X",
		   pos, addr, cur, MOSTD_NODE_GROUP_ADDR);
	return 0;
}
