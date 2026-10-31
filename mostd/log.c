#include "log.h"

#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static FILE *trace_fp;

static const char *mostd_op_name(uint8_t op)
{
	switch (op) {
	case 0x00: return "SET/START";
	case 0x01: return "GET/ABORT";
	case 0x02: return "SETGET/STARTRESULT";
	case 0x03: return "INCREMENT";
	case 0x04: return "DECREMENT";
	case 0x05: return "GETINTERFACE";
	case 0x06: return "STARTRESULTACK";
	case 0x07: return "ABORTACK";
	case 0x08: return "STARTACK";
	case 0x09: return "ERRORACK";
	case 0x0A: return "PROCESSINGACK";
	case 0x0B: return "PROCESSING";
	case 0x0C: return "STATUS/RESULT";
	case 0x0D: return "RESULTACK";
	case 0x0E: return "INTERFACE";
	case 0x0F: return "ERROR";
	default: return "UNKNOWN";
	}
}

char *mostd_payload_hex_alloc(const uint8_t *data, size_t tellen)
{
	char *out;
	size_t i;
	size_t out_sz;
	size_t used = 0;

	if (!data || tellen == 0) {
		out = malloc(2);
		if (!out)
			return NULL;
		out[0] = '-';
		out[1] = '\0';
		return out;
	}

	if (tellen > ((SIZE_MAX - 1) / 2))
		return NULL;

	out_sz = (tellen * 2) + 1;
	out = malloc(out_sz);
	if (!out)
		return NULL;

	for (i = 0; i < tellen; i++) {
		int n;
		n = snprintf(out + used, out_sz - used, "%02X", data[i] & 0xFF);
		if (n < 0)
			break;
		if ((size_t)n >= (out_sz - used)) {
			used = out_sz - 1;
			break;
		}
		used += (size_t)n;
	}
	out[used] = '\0';
	return out;
}

static void mostd_timestamp(char *buf, size_t len)
{
	struct timespec ts;
	struct tm tmv;
	size_t n;

	if (!buf || len == 0)
		return;

	if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
		snprintf(buf, len, "1970-01-01T00:00:00.000");
		return;
	}
	localtime_r(&ts.tv_sec, &tmv);
	n = strftime(buf, len, "%Y-%m-%dT%H:%M:%S", &tmv);
	if (n == 0 || n + 5 >= len) {
		snprintf(buf, len, "1970-01-01T00:00:00.000");
		return;
	}
	snprintf(buf + n, len - n, ".%03ld", ts.tv_nsec / 1000000L);
}

static void mostd_kernel_uptime(char *buf, size_t len)
{
	struct timespec ts;

	if (!buf || len == 0)
		return;

#ifdef CLOCK_BOOTTIME
	if (clock_gettime(CLOCK_BOOTTIME, &ts) == 0) {
		snprintf(buf, len, "%lld.%03ld",
			 (long long)ts.tv_sec, ts.tv_nsec / 1000000L);
		return;
	}
#endif
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		snprintf(buf, len, "%lld.%03ld",
			 (long long)ts.tv_sec, ts.tv_nsec / 1000000L);
		return;
	}

	snprintf(buf, len, "unknown");
}

int mostd_trace_open(const char *path)
{
	if (!path || !*path)
		return 0;

	trace_fp = fopen(path, "a");
	if (!trace_fp)
		return -1;

	return 0;
}

void mostd_trace_close(void)
{
	if (!trace_fp)
		return;
	fclose(trace_fp);
	trace_fp = NULL;
}

void mostd_trace_session_start(const char *trace_path, int log_level)
{
	char ts[32];
	char uptime[32];
	const char *path = (trace_path && *trace_path) ? trace_path : "off";

	if (!trace_fp)
		return;

	mostd_timestamp(ts, sizeof(ts));
	mostd_kernel_uptime(uptime, sizeof(uptime));
	fprintf(trace_fp, "# ---- mostd session start ----\n");
	fprintf(trace_fp, "# ts=%s uptime_s=%s pid=%ld log=%d trace=%s\n",
		ts, uptime, (long)getpid(), log_level, path);
	fflush(trace_fp);
}

void mostd_trace_cmt(const char *dir, uint16_t addr, uint8_t fblock, uint8_t inst, uint16_t fktid,
		     uint8_t op, uint8_t telid, const uint8_t *data, size_t tellen,
		     int error_code, int error_info, const char *result)
{
	char *payload_hex;
	char ts[32];
	const char *op_name = mostd_op_name(op);
	const char *dir_s = (dir && *dir) ? dir : "?";
	const char *res_s = (result && *result) ? result : "-";

	payload_hex = mostd_payload_hex_alloc(data, tellen);
	if (!payload_hex)
		payload_hex = "-";

	mostd_timestamp(ts, sizeof(ts));

	if (!trace_fp)
		goto out;

	if (error_code >= 0 && error_info >= 0) {
		fprintf(trace_fp,
			"cmt ts=%s dir=%s addr=0x%04X fblock=0x%02X inst=0x%02X fkt=0x%03X op=0x%X op_name=%s telid=%u tellen=%" PRIuMAX " payload=%s ec=0x%02X ei=0x%02X result=%s\n",
			ts, dir_s, addr, fblock, inst, fktid, op, op_name, telid, (uintmax_t)tellen,
			payload_hex, error_code & 0xFF, error_info & 0xFF, res_s);
	} else {
		fprintf(trace_fp,
			"cmt ts=%s dir=%s addr=0x%04X fblock=0x%02X inst=0x%02X fkt=0x%03X op=0x%X op_name=%s telid=%u tellen=%" PRIuMAX " payload=%s result=%s\n",
			ts, dir_s, addr, fblock, inst, fktid, op, op_name, telid, (uintmax_t)tellen,
			payload_hex, res_s);
	}
	fflush(trace_fp);

out:
	if (payload_hex[0] != '-' || payload_hex[1] != '\0')
		free(payload_hex);
}

void mostd_trace_event(const char *kind, uint32_t seq, int status, uint16_t aux,
		       const uint8_t *data, size_t len, const char *result)
{
	char *payload_hex;
	char ts[32];
	const char *kind_s = (kind && *kind) ? kind : "?";
	const char *res_s = (result && *result) ? result : "-";

	payload_hex = mostd_payload_hex_alloc(data, len);
	if (!payload_hex)
		payload_hex = "-";

	mostd_timestamp(ts, sizeof(ts));

	if (trace_fp) {
		fprintf(trace_fp,
			"evt ts=%s kind=%s seq=%" PRIu32 " status=%d aux=0x%04X len=%" PRIuMAX " payload=%s result=%s\n",
			ts, kind_s, seq, status, aux, (uintmax_t)len, payload_hex, res_s);
		fflush(trace_fp);
	}

	if (payload_hex[0] != '-' || payload_hex[1] != '\0')
		free(payload_hex);
}
