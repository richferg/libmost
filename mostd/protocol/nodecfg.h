#ifndef MOSTD_PROTOCOL_NODECFG_H
#define MOSTD_PROTOCOL_NODECFG_H

struct most_handle;

/*
 * Program our MOST node address and group on link-up.
 *
 * The productive ECUs keep the logical addresses they were assigned at the
 * original ring configuration, so a position-derived address (0x0100|bNPR)
 * collides with whichever ECU already owns it. We therefore take a fixed
 * address in the free slot above the productive ring (MOSTD_DIAG_NODE_ADDR,
 * 0x0110 on the observed 15-node ring) and the hardware group the real SDAR65
 * module uses (0x62). The NetworkMaster can still move us through the NetBlock
 * NodeAddress.Set / GroupAddress.Set handlers in netblock_server.c.
 *
 * Call after the link is up. Returns 0 on success.
 */
int mostd_nodecfg_apply(struct most_handle *h);

#endif /* MOSTD_PROTOCOL_NODECFG_H */
