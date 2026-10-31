#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>

#include "most.h"
#include "protocol/control.h"
#include "protocol/runtime.h"
#include "protocol/transport.h"
#include "log.h"
#include "build_info.h"

static volatile int running = 1;
int mostd_log_level = MOSTD_LOG_INFO;
static char mostd_trace_file[256];
#define MOSTD_TICK_MS 100

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s [-v|--debug] [--quiet] [--trace-file PATH]\n", argv0);
	fprintf(stderr, "  Env: MOSTD_LOG=error|info|debug|0|1|2\n");
	fprintf(stderr, "  Env: MOSTD_TRACE_FILE=/path/to/mostd-trace.log\n");
}

static void apply_env_log_level(void)
{
	const char *s = getenv("MOSTD_LOG");
	if (!s || !*s)
		return;

	if (!strcmp(s, "error") || !strcmp(s, "0")) {
		mostd_log_level = MOSTD_LOG_ERROR;
		return;
	}
	if (!strcmp(s, "info") || !strcmp(s, "1")) {
		mostd_log_level = MOSTD_LOG_INFO;
		return;
	}
	if (!strcmp(s, "debug") || !strcmp(s, "2")) {
		mostd_log_level = MOSTD_LOG_DEBUG;
		return;
	}
}

static void apply_env_trace_file(void)
{
	const char *s = getenv("MOSTD_TRACE_FILE");

	if (!s || !*s)
		return;

	snprintf(mostd_trace_file, sizeof(mostd_trace_file), "%s", s);
}

static void sigint_handler(int sig)
{
	(void)sig;
	running = 0;
}

int main(int argc, char **argv)
{
	struct mostd_transport transport;
	struct mostd_runtime runtime;
	struct mostd_control control;
	struct most_event evt;
	int i;

	apply_env_log_level();
	apply_env_trace_file();
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--debug")) {
			mostd_log_level = MOSTD_LOG_DEBUG;
			continue;
		}
		if (!strcmp(argv[i], "--quiet")) {
			mostd_log_level = MOSTD_LOG_ERROR;
			continue;
		}
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		}
		if (!strcmp(argv[i], "--trace-file")) {
			if (i + 1 >= argc) {
				usage(argv[0]);
				return 1;
			}
			snprintf(mostd_trace_file, sizeof(mostd_trace_file), "%s", argv[++i]);
			continue;
		}
		if (!strncmp(argv[i], "--trace-file=", 13)) {
			snprintf(mostd_trace_file, sizeof(mostd_trace_file), "%s", argv[i] + 13);
			continue;
		}
		usage(argv[0]);
		return 1;
	}

	signal(SIGINT, sigint_handler);

	if (mostd_transport_open(&transport, "0") < 0) {
		perror("mostd_transport_open");
		return 1;
	}
	if (mostd_trace_open(mostd_trace_file) < 0) {
		MOSTD_ERROR("trace open failed path=%s errno=%d", mostd_trace_file, errno);
		mostd_transport_close(&transport);
		return 1;
	}
	mostd_trace_session_start(mostd_trace_file, mostd_log_level);
	MOSTD_INFO("started (git=%s device=/dev/most0, log=%d trace=%s)", MOSTD_GIT_HASH,
		   mostd_log_level, mostd_trace_file[0] ? mostd_trace_file : "off");
	mostd_runtime_init(&runtime, transport.handle);
	/* Control plane for the Bluetooth agent; the daemon runs without it. */
	if (mostd_control_init(&control, &runtime.sdars, NULL) == 0)
		mostd_sdars_set_event_sink(&runtime.sdars,
					   (mostd_sdars_event_fn)mostd_control_send, &control);

	while (running) {
		struct pollfd pfd[3];
		int ready, nctl;

		/*
		 * libmost may already hold a queued event; drain it before blocking
		 * (most_poll_event with a zero timeout also pulls one raw event in
		 * from the device when the fd is readable).
		 */
		ready = mostd_transport_poll_event(&transport, 0);
		if (ready < 0) {
			perror("mostd_transport_poll_event");
			MOSTD_ERROR("poll failed, stopping");
			break;
		}
		if (ready > 0) {
			if (mostd_transport_read_event(&transport, &evt) < 0) {
				perror("mostd_transport_read_event");
				MOSTD_ERROR("read failed, stopping");
				break;
			}
			mostd_runtime_handle_event(&runtime, &evt);
			continue;
		}

		pfd[0].fd = most_fd(transport.handle);
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		nctl = mostd_control_fill_pollfds(&control, &pfd[1], 2);
		ready = poll(pfd, 1 + nctl, MOSTD_TICK_MS);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			MOSTD_ERROR("poll failed, stopping");
			break;
		}
		if (ready == 0) {
			mostd_runtime_tick(&runtime);
			continue;
		}
		if (nctl)
			mostd_control_service(&control, &pfd[1], nctl);
		/* The device fd is picked up by the zero-timeout poll at the top. */
	}

	MOSTD_INFO("stopped");
	mostd_control_close(&control);
	mostd_trace_close();
	mostd_transport_close(&transport);
	return 0;
}
