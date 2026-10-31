#ifndef MOSTD_LOG_H
#define MOSTD_LOG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum mostd_log_level {
	MOSTD_LOG_ERROR = 0,
	MOSTD_LOG_INFO = 1,
	MOSTD_LOG_DEBUG = 2,
};

extern int mostd_log_level;

/* Seconds on the monotonic clock, so bench logs can be lined up with actions. */
static inline double mostd_log_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

#define MOSTD_LOG(level, fmt, ...)                                              \
	do {                                                                    \
		if (mostd_log_level >= (level))                                 \
			fprintf(stderr, "mostd: [%.3f] " fmt "\n",              \
				mostd_log_now(), ##__VA_ARGS__);                \
	} while (0)

#define MOSTD_ERROR(fmt, ...) MOSTD_LOG(MOSTD_LOG_ERROR, fmt, ##__VA_ARGS__)
#define MOSTD_INFO(fmt, ...) MOSTD_LOG(MOSTD_LOG_INFO, fmt, ##__VA_ARGS__)
#define MOSTD_DEBUG(fmt, ...) MOSTD_LOG(MOSTD_LOG_DEBUG, fmt, ##__VA_ARGS__)

char *mostd_payload_hex_alloc(const uint8_t *data, size_t tellen);
int mostd_trace_open(const char *path);
void mostd_trace_close(void);
void mostd_trace_session_start(const char *trace_path, int log_level);
void mostd_trace_cmt(const char *dir, uint16_t addr, uint8_t fblock, uint8_t inst, uint16_t fktid,
		     uint8_t op, uint8_t telid, const uint8_t *data, size_t tellen,
		     int error_code, int error_info, const char *result);
void mostd_trace_event(const char *kind, uint32_t seq, int status, uint16_t aux,
		       const uint8_t *data, size_t len, const char *result);

#endif /* MOSTD_LOG_H */
