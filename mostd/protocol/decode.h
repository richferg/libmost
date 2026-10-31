#ifndef MOSTD_PROTOCOL_DECODE_H
#define MOSTD_PROTOCOL_DECODE_H

#include <stddef.h>

#include "rx.h"

/*
 * Decode an inbound telegram against the catalog into a human string.
 * Returns:
 *    1  decoded; out holds "name=value ..."
 *    0  no catalog entry for this (fblock, fktid, optype)
 *   -1  catalog entry exists but payload length did not match; out holds a note
 */
int mostd_decode_telegram(const struct mostd_rx_msg *msg, char *out, size_t outsz);

#endif /* MOSTD_PROTOCOL_DECODE_H */
