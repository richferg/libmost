#include "control.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../log.h"

static void reply(struct mostd_control *c, const char *fmt, ...)
{
	char line[MOSTD_CONTROL_LINE_MAX];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	mostd_control_send(c, line);
}

void mostd_control_send(struct mostd_control *c, const char *line)
{
	size_t n;

	if (!c || !line)
		return;
	n = strlen(line);
	if (c->client_fd < 0)
		return;
	if (c->tx_len + n + 1u > sizeof(c->tx)) {
		c->dropped++;
		MOSTD_INFO("control: tx buffer full, dropped \"%s\" (%u so far)", line,
			   c->dropped);
		return;
	}
	memcpy(c->tx + c->tx_len, line, n);
	c->tx_len += n;
	c->tx[c->tx_len++] = '\n';
}

/* ---- line parser --------------------------------------------------------- */

/* Split `s` on '|' into at most `max` fields (in place). Returns the count. */
static int split_bar(char *s, char **out, int max)
{
	int n = 0;

	while (s && n < max) {
		char *bar = strchr(s, '|');

		out[n++] = s;
		if (!bar)
			break;
		*bar = '\0';
		s = bar + 1;
	}
	return n;
}

static int parse_u8(const char *s, uint8_t *out)
{
	char *end;
	long v;

	if (!s || !*s)
		return -1;
	v = strtol(s, &end, 0);
	if (*end || v < 0 || v > 255)
		return -1;
	*out = (uint8_t)v;
	return 0;
}

static void txn_reset(struct mostd_control *c)
{
	c->txn_open = 0;
	c->txn_expected = 0;
	c->txn_count = 0;
	c->txn_ncat = 0;
}

void mostd_control_handle_line(struct mostd_control *c, char *line)
{
	char *cmd, *arg, *f[4];
	int nf;

	if (!c || !line)
		return;
	/* strip trailing CR/space, split "cmd arg..." */
	{
		size_t n = strlen(line);

		while (n && (line[n - 1u] == '\r' || line[n - 1u] == ' ' || line[n - 1u] == '\t'))
			line[--n] = '\0';
	}
	cmd = line;
	while (*cmd == ' ')
		cmd++;
	if (!*cmd)
		return;
	arg = strchr(cmd, ' ');
	if (arg) {
		*arg++ = '\0';
		while (*arg == ' ')
			arg++;
	} else {
		arg = cmd + strlen(cmd);
	}

	if (strcmp(cmd, "ping") == 0) {
		reply(c, "pong");
		return;
	}
	if (strcmp(cmd, "get") == 0) {
		mostd_sdars_report_state(c->sdars);
		reply(c, "ok get");
		return;
	}
	if (strcmp(cmd, "text") == 0) {
		nf = split_bar(arg, f, 3);
		mostd_sdars_set_text(c->sdars, nf > 0 ? f[0] : "", nf > 1 ? f[1] : "",
				     nf > 2 ? f[2] : "");
		reply(c, "ok text");
		return;
	}
	if (strcmp(cmd, "current") == 0) {
		uint8_t st;

		if (parse_u8(arg, &st) < 0 || mostd_sdars_set_current(c->sdars, st) < 0)
			reply(c, "err current no such station");
		else
			reply(c, "ok current");
		return;
	}
	if (strcmp(cmd, "signal") == 0) {
		uint8_t v;

		if (parse_u8(arg, &v) < 0 || v > 1u) {
			reply(c, "err signal expected 0|1");
			return;
		}
		mostd_sdars_set_signal(c->sdars, v);
		reply(c, "ok signal");
		return;
	}
	if (strcmp(cmd, "esn") == 0) {
		if (mostd_sdars_set_esn(c->sdars, arg) < 0)
			reply(c, "err esn up to 12 printable chars");
		else
			reply(c, "ok esn");
		return;
	}
	if (strcmp(cmd, "stations") == 0) {
		uint8_t n;

		if (parse_u8(arg, &n) < 0 || n < 1u || n > MOSTD_SDARS_MAX_STATIONS) {
			reply(c, "err stations count 1..%u", MOSTD_SDARS_MAX_STATIONS);
			return;
		}
		txn_reset(c);
		c->txn_open = 1;
		c->txn_expected = n;
		reply(c, "ok stations");
		return;
	}
	if (strcmp(cmd, "abort") == 0) {
		txn_reset(c);
		reply(c, "ok abort");
		return;
	}
	if (strcmp(cmd, "category") == 0 || strcmp(cmd, "station") == 0 ||
	    strcmp(cmd, "end") == 0) {
		if (!c->txn_open) {
			reply(c, "err %s no open stations transaction", cmd);
			return;
		}
	}
	if (strcmp(cmd, "category") == 0) {
		struct mostd_sdars_category *cat;

		nf = split_bar(arg, f, 3);
		if (nf != 3 || c->txn_ncat >= MOSTD_SDARS_MAX_CATEGORIES ||
		    strlen(f[1]) > MOSTD_SDARS_SHORT_LEN || strlen(f[2]) > MOSTD_SDARS_LONG_LEN) {
			reply(c, "err category id|short8|long20");
			txn_reset(c);
			return;
		}
		cat = &c->txn_categories[c->txn_ncat];
		if (parse_u8(f[0], &cat->id) < 0) {
			reply(c, "err category bad id");
			txn_reset(c);
			return;
		}
		snprintf(cat->shortname, sizeof(cat->shortname), "%s", f[1]);
		snprintf(cat->longname, sizeof(cat->longname), "%s", f[2]);
		c->txn_ncat++;
		return;
	}
	if (strcmp(cmd, "station") == 0) {
		struct mostd_sdars_station *st;

		nf = split_bar(arg, f, 4);
		if (nf < 3 || c->txn_count >= c->txn_expected ||
		    strlen(f[0]) > MOSTD_SDARS_SHORT_LEN || strlen(f[1]) > MOSTD_SDARS_LONG_LEN) {
			reply(c, "err station short8|long20|category[|sub] (max %u rows)",
			      c->txn_expected);
			txn_reset(c);
			return;
		}
		st = &c->txn_stations[c->txn_count];
		memset(st, 0, sizeof(*st));
		snprintf(st->shortname, sizeof(st->shortname), "%s", f[0]);
		snprintf(st->longname, sizeof(st->longname), "%s", f[1]);
		st->subscribed = 1;
		if (parse_u8(f[2], &st->category) < 0 ||
		    (nf > 3 && (parse_u8(f[3], &st->subscribed) < 0 || st->subscribed > 1u))) {
			reply(c, "err station bad category/subscribed");
			txn_reset(c);
			return;
		}
		c->txn_count++;
		return;
	}
	if (strcmp(cmd, "end") == 0) {
		int rc;

		if (c->txn_count != c->txn_expected) {
			reply(c, "err end expected %u station lines, got %u", c->txn_expected,
			      c->txn_count);
			txn_reset(c);
			return;
		}
		rc = mostd_sdars_set_stations(c->sdars, c->txn_stations, c->txn_count,
					      c->txn_categories, c->txn_ncat);
		txn_reset(c);
		if (rc < 0)
			reply(c, "err end rejected");
		else
			reply(c, "ok end");
		return;
	}
	reply(c, "err %s unknown command", cmd);
}

/* ---- socket plumbing ----------------------------------------------------- */

static int set_nonblock(int fd)
{
	int fl = fcntl(fd, F_GETFL, 0);

	return fl < 0 ? -1 : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int mostd_control_init(struct mostd_control *c, struct mostd_sdars *sdars, const char *path)
{
	struct sockaddr_un addr;

	if (!c)
		return -1;
	memset(c, 0, sizeof(*c));
	c->sdars = sdars;
	c->listen_fd = -1;
	c->client_fd = -1;
	if (!path || !*path)
		path = getenv("MOSTD_CTL_SOCKET");
	if (!path || !*path)
		path = MOSTD_CONTROL_DEFAULT_PATH;
	if (strlen(path) >= sizeof(addr.sun_path)) {
		MOSTD_ERROR("control: socket path too long: %s", path);
		return -1;
	}
	snprintf(c->path, sizeof(c->path), "%s", path);

	c->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (c->listen_fd < 0) {
		MOSTD_ERROR("control: socket: errno=%d", errno);
		return -1;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, c->path, strlen(c->path));
	unlink(c->path);
	if (bind(c->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
	    listen(c->listen_fd, 1) < 0 || set_nonblock(c->listen_fd) < 0) {
		MOSTD_ERROR("control: bind/listen %s: errno=%d (control plane disabled)",
			    c->path, errno);
		close(c->listen_fd);
		c->listen_fd = -1;
		return -1;
	}
	MOSTD_INFO("control: listening on %s", c->path);
	return 0;
}

void mostd_control_close(struct mostd_control *c)
{
	if (!c)
		return;
	if (c->client_fd >= 0)
		close(c->client_fd);
	if (c->listen_fd >= 0) {
		close(c->listen_fd);
		unlink(c->path);
	}
	c->client_fd = c->listen_fd = -1;
}

int mostd_control_fill_pollfds(const struct mostd_control *c, struct pollfd *pfd, int max)
{
	int n = 0;

	if (!c || !pfd)
		return 0;
	if (c->listen_fd >= 0 && n < max) {
		pfd[n].fd = c->listen_fd;
		pfd[n].events = POLLIN;
		pfd[n].revents = 0;
		n++;
	}
	if (c->client_fd >= 0 && n < max) {
		pfd[n].fd = c->client_fd;
		pfd[n].events = (short)(POLLIN | (c->tx_len ? POLLOUT : 0));
		pfd[n].revents = 0;
		n++;
	}
	return n;
}

static void drop_client(struct mostd_control *c, const char *why)
{
	MOSTD_INFO("control: client gone (%s)", why);
	close(c->client_fd);
	c->client_fd = -1;
	c->rx_len = 0;
	c->tx_len = 0;
	txn_reset(c);
}

static void accept_client(struct mostd_control *c)
{
	int fd = accept(c->listen_fd, NULL, NULL);

	if (fd < 0)
		return;
	if (c->client_fd >= 0)
		drop_client(c, "replaced by a new connection");
	set_nonblock(fd);
	c->client_fd = fd;
	c->rx_len = 0;
	c->tx_len = 0;
	txn_reset(c);
	MOSTD_INFO("control: client connected");
}

static void read_client(struct mostd_control *c)
{
	ssize_t n = read(c->client_fd, c->rx + c->rx_len, sizeof(c->rx) - 1u - c->rx_len);
	char *start, *nl;

	if (n == 0) {
		drop_client(c, "closed");
		return;
	}
	if (n < 0) {
		if (errno != EAGAIN && errno != EINTR)
			drop_client(c, "read error");
		return;
	}
	c->rx_len += (size_t)n;
	c->rx[c->rx_len] = '\0';
	start = c->rx;
	while ((nl = strchr(start, '\n')) != NULL) {
		*nl = '\0';
		mostd_control_handle_line(c, start);
		start = nl + 1;
	}
	c->rx_len = strlen(start);
	memmove(c->rx, start, c->rx_len + 1u);
	if (c->rx_len >= sizeof(c->rx) - 1u) {
		MOSTD_INFO("control: line too long, discarded");
		c->rx_len = 0;
	}
}

static void write_client(struct mostd_control *c)
{
	ssize_t n;

	if (!c->tx_len)
		return;
	n = write(c->client_fd, c->tx, c->tx_len);
	if (n < 0) {
		if (errno != EAGAIN && errno != EINTR)
			drop_client(c, "write error");
		return;
	}
	memmove(c->tx, c->tx + n, c->tx_len - (size_t)n);
	c->tx_len -= (size_t)n;
}

void mostd_control_service(struct mostd_control *c, const struct pollfd *pfd, int n)
{
	int i;

	if (!c)
		return;
	for (i = 0; i < n; i++) {
		if (!pfd[i].revents)
			continue;
		if (pfd[i].fd == c->listen_fd) {
			accept_client(c);
		} else if (pfd[i].fd == c->client_fd) {
			if (pfd[i].revents & (POLLHUP | POLLERR)) {
				drop_client(c, "hangup");
				continue;
			}
			if (pfd[i].revents & POLLIN)
				read_client(c);
			if (c->client_fd >= 0 && (pfd[i].revents & POLLOUT))
				write_client(c);
		}
	}
	/* Events queued while handling a line go out now if the socket allows. */
	if (c->client_fd >= 0 && c->tx_len)
		write_client(c);
}
