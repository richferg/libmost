#ifndef MOSTD_PROTOCOL_CONTROL_H
#define MOSTD_PROTOCOL_CONTROL_H

#include <stddef.h>
#include <stdint.h>
#include <poll.h>

#include "sdars.h"

/*
 * Control plane for the SatRadio responder: a Unix stream socket on which one
 * client (the Bluetooth agent, tools/bt-agent) feeds what the phone is doing
 * and receives what the MMI asked for. Line protocol, '\n' terminated, text
 * fields separated by '|'.
 *
 *   -> text <title>|<artist>|<album>       0x206 programme text
 *   -> stations <n>                        begin a station-table transaction
 *   ->   category <id>|<short8>|<long20>
 *   ->   station <short8>|<long20>|<category>[|<subscribed 0/1>]
 *   -> end                                 commit (exactly n station lines)
 *   -> abort
 *   -> current <station>                   the station that is playing (1-based)
 *   -> signal 0|1                          0x400 reception flag
 *   -> esn <text up to 12>
 *   -> get                                 ask for a state report
 *   -> ping
 *   <- ok <command> | err <command> <reason> | pong
 *   <- events, see mostd_sdars_event_fn in sdars.h
 *
 * Socket path: MOSTD_CTL_SOCKET, default /run/mostd/sdars.sock. Non-blocking
 * throughout; the ring must never wait on the agent. Output that does not fit
 * the buffer is dropped with a log line.
 */
#define MOSTD_CONTROL_DEFAULT_PATH "/run/mostd/sdars.sock"
#define MOSTD_CONTROL_LINE_MAX 256u

struct mostd_control {
	struct mostd_sdars *sdars;
	int listen_fd;
	int client_fd;
	char path[108];
	char rx[MOSTD_CONTROL_LINE_MAX + 1u];
	size_t rx_len;
	char tx[2048];
	size_t tx_len;
	uint32_t dropped;
	/* station-table transaction */
	uint8_t txn_open;
	uint8_t txn_expected;
	uint8_t txn_count;
	uint8_t txn_ncat;
	struct mostd_sdars_station txn_stations[MOSTD_SDARS_MAX_STATIONS];
	struct mostd_sdars_category txn_categories[MOSTD_SDARS_MAX_CATEGORIES];
};

/* Bind and listen. path NULL = MOSTD_CTL_SOCKET or the default. Returns -1
 * (and leaves the control plane disabled) if the socket cannot be created. */
int mostd_control_init(struct mostd_control *c, struct mostd_sdars *sdars, const char *path);
void mostd_control_close(struct mostd_control *c);

/* Fill up to `max` pollfds for the listen and client sockets; returns the count. */
int mostd_control_fill_pollfds(const struct mostd_control *c, struct pollfd *pfd, int max);
/* Service the fds filled above after poll() returned. */
void mostd_control_service(struct mostd_control *c, const struct pollfd *pfd, int n);

/* Parse and apply one command line (no newline). Public for tests. */
void mostd_control_handle_line(struct mostd_control *c, char *line);
/* Queue a line for the client (used as the SatRadio event sink). */
void mostd_control_send(struct mostd_control *c, const char *line);

#endif /* MOSTD_PROTOCOL_CONTROL_H */
