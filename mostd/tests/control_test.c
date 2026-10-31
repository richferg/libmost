#include <assert.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int mostd_log_level;

/* Both implementations in one unit so the parser drives the real responder. */
#include "../protocol/control.c"
#include "../protocol/sdars.c"

static size_t tx_count;

int mostd_tx_op(struct most_handle *handle, uint16_t device, uint8_t fblock,
		uint8_t inst, uint16_t fktid, uint8_t op, uint8_t telid,
		const uint8_t *payload, size_t length)
{
	(void)handle; (void)device; (void)fblock; (void)inst; (void)fktid;
	(void)op; (void)telid; (void)payload; (void)length;
	tx_count++;
	return 0;
}

int mostd_tx_status(struct most_handle *handle, uint16_t device, uint8_t fblock,
		    uint8_t inst, uint16_t fktid, uint8_t telid,
		    const uint8_t *payload, size_t length)
{
	return mostd_tx_op(handle, device, fblock, inst, fktid, MOST_OP_STATUS, telid,
			   payload, length);
}

int mostd_tx_error_for_request(struct most_handle *handle, uint16_t device,
			       uint8_t fblock, uint8_t inst, uint16_t fktid,
			       uint8_t telid, uint8_t request_op,
			       const uint8_t *request_payload, size_t request_length,
			       uint8_t error_code, uint8_t error_info)
{
	(void)handle; (void)device; (void)fblock; (void)inst; (void)fktid; (void)telid;
	(void)request_op; (void)request_payload; (void)request_length;
	(void)error_code; (void)error_info;
	return 0;
}

char *mostd_payload_hex_alloc(const uint8_t *data, size_t tellen)
{
	(void)data; (void)tellen;
	return NULL;
}

int most_write(struct most_handle *h, const uint8_t buf[MOST_FRAME_LEN])
{
	(void)h; (void)buf;
	return -1;
}

int most_reg_read(struct most_handle *h, uint16_t reg, void *buf, uint16_t len)
{
	(void)h; (void)reg; (void)buf; (void)len;
	return -1;
}

int most_reg_write(struct most_handle *h, uint16_t reg, const void *buf, uint16_t len)
{
	(void)h; (void)reg; (void)buf; (void)len;
	return -1;
}

/* ---- parser, no sockets ----------------------------------------------------- */

static void feed(struct mostd_control *c, const char *line)
{
	char buf[MOSTD_CONTROL_LINE_MAX];

	snprintf(buf, sizeof(buf), "%s", line);
	mostd_control_handle_line(c, buf);
}

/* Take the queued reply lines and clear the buffer. */
static const char *take(struct mostd_control *c)
{
	static char out[2048];

	memcpy(out, c->tx, c->tx_len);
	out[c->tx_len] = '\0';
	c->tx_len = 0;
	return out;
}

static void test_parser(void)
{
	struct mostd_sdars s;
	struct mostd_control c;

	mostd_sdars_init(&s, (struct most_handle *)(uintptr_t)1);
	memset(&c, 0, sizeof(c));
	c.sdars = &s;
	c.listen_fd = -1;
	c.client_fd = 1000;	/* "connected": replies queue in c.tx (never written here) */
	mostd_sdars_set_event_sink(&s, (mostd_sdars_event_fn)mostd_control_send, &c);

	feed(&c, "ping");
	assert(strcmp(take(&c), "pong\n") == 0);
	feed(&c, "bogus 1 2");
	assert(strcmp(take(&c), "err bogus unknown command\n") == 0);
	feed(&c, "");
	assert(c.tx_len == 0);

	feed(&c, "text Billie Jean|Michael Jackson|Thriller\r");
	assert(strcmp(take(&c), "ok text\n") == 0);
	assert(s.text_set && strcmp(s.text[0], "Billie Jean") == 0);
	assert(strcmp(s.text[1], "Michael Jackson") == 0);
	assert(strcmp(s.text[2], "Thriller") == 0);
	feed(&c, "text Only a title");
	assert(strcmp(take(&c), "ok text\n") == 0);
	assert(strcmp(s.text[0], "Only a title") == 0 && s.text[1][0] == '\0');

	/* station table transaction */
	feed(&c, "station Pixel|Richard's Pixel|1");
	assert(strcmp(take(&c), "err station no open stations transaction\n") == 0);
	feed(&c, "stations 2");
	assert(strcmp(take(&c), "ok stations\n") == 0);
	feed(&c, "category 1|Phones|Paired phones");
	feed(&c, "station Pixel|Richard's Pixel|1");
	feed(&c, "station iPhone|Work iPhone|1|0");
	assert(c.tx_len == 0);
	feed(&c, "end");
	assert(strcmp(take(&c), "ok end\n") == 0);
	assert(s.station_count == 2 && s.category_count == 1);
	assert(strcmp(s.stations[0].shortname, "Pixel") == 0);
	assert(strcmp(s.stations[1].longname, "Work iPhone") == 0);
	assert(s.stations[0].subscribed == 1 && s.stations[1].subscribed == 0);
	assert(s.categories[0].id == 1 && strcmp(s.categories[0].longname, "Paired phones") == 0);

	/* wrong row count, overlong name, too many rows: rejected, table intact */
	feed(&c, "stations 2");
	feed(&c, "station One|One|1");
	feed(&c, "end");
	assert(strncmp(take(&c), "ok stations\nerr end expected 2", 30) == 0);
	assert(s.station_count == 2);
	feed(&c, "stations 1");
	feed(&c, "station WayTooLongName|x|1");
	assert(strncmp(take(&c), "ok stations\nerr station", 23) == 0);
	assert(!c.txn_open);
	feed(&c, "stations 1");
	feed(&c, "station A|a|1");
	feed(&c, "station B|b|1");
	assert(strncmp(take(&c), "ok stations\nerr station", 23) == 0);
	feed(&c, "stations 0");
	assert(strncmp(take(&c), "err stations", 12) == 0);
	feed(&c, "stations 1");
	feed(&c, "abort");
	assert(strcmp(take(&c), "ok stations\nok abort\n") == 0 && !c.txn_open);
	assert(s.station_count == 2);

	feed(&c, "current 2");
	assert(strcmp(take(&c), "ok current\n") == 0 && s.current_station == 2);
	feed(&c, "current 9");
	assert(strncmp(take(&c), "err current", 11) == 0);
	feed(&c, "signal 0");
	assert(strcmp(take(&c), "ok signal\n") == 0 && s.signal_ok == 0);
	feed(&c, "signal 2");
	assert(strncmp(take(&c), "err signal", 10) == 0);
	feed(&c, "esn Pixel 8");
	assert(strcmp(take(&c), "ok esn\n") == 0 && strcmp(s.esn, "Pixel 8") == 0);
	feed(&c, "esn 0123456789ABC");
	assert(strncmp(take(&c), "err esn", 7) == 0);

	/* events from the responder land in the same stream */
	feed(&c, "get");
	assert(strcmp(take(&c), "current 2\nsource off\nui closed\nok get\n") == 0);
	{
		static const uint8_t next_channel[] = { 0x00, 0x00, 0x01, 0x00, 0x00 };
		struct mostd_rx_msg msg;

		memset(&msg, 0, sizeof(msg));
		msg.source = 0x0103;
		msg.fblock = SDARS_FBLOCK;
		msg.inst = SDARS_INST;
		msg.fktid = FKT_STEP;
		msg.optype = MOST_OP_SET;
		msg.tellen = sizeof(next_channel);
		memcpy(msg.data, next_channel, sizeof(next_channel));
		assert(mostd_sdars_handle_msg(&s, &msg) == 1);
		assert(strcmp(take(&c), "seek next 0 0\n") == 0);
	}

	/* without a client nothing is queued */
	c.client_fd = -1;
	feed(&c, "ping");
	assert(c.tx_len == 0);
}

/* ---- sockets: accept, read lines, write replies ----------------------------- */

static void service_until_quiet(struct mostd_control *c)
{
	int i;

	for (i = 0; i < 20; i++) {
		struct pollfd pfd[2];
		int n = mostd_control_fill_pollfds(c, pfd, 2);

		if (poll(pfd, n, 20) <= 0)
			break;
		mostd_control_service(c, pfd, n);
	}
}

static void test_socket(void)
{
	struct mostd_sdars s;
	struct mostd_control c;
	struct sockaddr_un addr;
	char path[96], buf[256];
	int fd;
	ssize_t n;

	snprintf(path, sizeof(path), "/tmp/mostd-control-test-%ld.sock", (long)getpid());
	mostd_sdars_init(&s, (struct most_handle *)(uintptr_t)1);
	assert(mostd_control_init(&c, &s, path) == 0);
	mostd_sdars_set_event_sink(&s, (mostd_sdars_event_fn)mostd_control_send, &c);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	assert(fd >= 0);
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	assert(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
	service_until_quiet(&c);
	assert(c.client_fd >= 0);

	/* two commands in one write, one split across two writes */
	assert(write(fd, "ping\ntext A|B|C\nsta", 19) == 19);
	service_until_quiet(&c);
	assert(write(fd, "tions 1\nstation X|x|1\nend\n", 26) == 26);
	service_until_quiet(&c);
	n = read(fd, buf, sizeof(buf) - 1);
	assert(n > 0);
	buf[n] = '\0';
	assert(strcmp(buf, "pong\nok text\nok stations\nok end\n") == 0);
	assert(s.station_count == 1 && strcmp(s.stations[0].shortname, "X") == 0);

	/* client goes away: the control plane notices and keeps running */
	close(fd);
	service_until_quiet(&c);
	assert(c.client_fd < 0);
	mostd_control_close(&c);
	assert(access(path, F_OK) != 0);
}

int main(void)
{
	test_parser();
	test_socket();
	return 0;
}
