#define _GNU_SOURCE

#include "most.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>

struct most_pending_event {
    struct most_event evt;
    struct most_pending_event *next;
};

struct most_handle {
    int fd;
    char devpath[64];
    struct most_pending_event *pending_head;
    struct most_pending_event *pending_tail;
};

static void most_queue_event(struct most_handle *h, const struct most_event *evt)
{
    struct most_pending_event *node;

    if (!h || !evt)
        return;

    node = calloc(1, sizeof(*node));
    if (!node)
        return;

    node->evt = *evt;
    if (!h->pending_tail) {
        h->pending_head = node;
        h->pending_tail = node;
        return;
    }

    h->pending_tail->next = node;
    h->pending_tail = node;
}

static int most_pop_event(struct most_handle *h, struct most_event *evt)
{
    struct most_pending_event *node;

    if (!h || !evt || !h->pending_head)
        return 0;

    node = h->pending_head;
    h->pending_head = node->next;
    if (!h->pending_head)
        h->pending_tail = NULL;
    *evt = node->evt;
    free(node);
    return 1;
}

static int most_pop_ctrl_rx(struct most_handle *h, struct most_event *evt)
{
    struct most_pending_event *node;
    struct most_pending_event *prev = NULL;

    if (!h || !evt)
        return 0;

    node = h->pending_head;
    while (node) {
        if (node->evt.type == MOST_EVT_CTRL_RX) {
            if (prev)
                prev->next = node->next;
            else
                h->pending_head = node->next;

            if (h->pending_tail == node)
                h->pending_tail = prev;

            *evt = node->evt;
            free(node);
            return 1;
        }
        prev = node;
        node = node->next;
    }

    return 0;
}

static int most_has_ctrl_rx(const struct most_handle *h)
{
    const struct most_pending_event *node;

    if (!h)
        return 0;

    node = h->pending_head;
    while (node) {
        if (node->evt.type == MOST_EVT_CTRL_RX)
            return 1;
        node = node->next;
    }

    return 0;
}

static void most_free_pending(struct most_handle *h)
{
    struct most_pending_event *node;
    struct most_pending_event *next;

    if (!h)
        return;

    node = h->pending_head;
    while (node) {
        next = node->next;
        free(node);
        node = next;
    }

    h->pending_head = NULL;
    h->pending_tail = NULL;
}

static int most_read_raw_event(struct most_handle *h, struct most_event *evt)
{
    uint8_t raw[MOST_FRAME_LEN];
    size_t got = 0;
    ssize_t r;

    if (!h || !evt || h->fd < 0) {
        errno = EBADF;
        return -1;
    }

    memset(evt, 0, sizeof(*evt));

    while (got < sizeof(*evt)) {
        r = read(h->fd, ((uint8_t *)evt) + got, sizeof(*evt) - got);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            if (got == 0 && errno == EMSGSIZE)
                break;
            return -1;
        }
        if (r == 0)
            return -1;

        got += (size_t)r;
        if (got == sizeof(*evt))
            return 0;
        if (got >= MOST_FRAME_LEN)
            break;
    }

    if (got != MOST_FRAME_LEN) {
        errno = EPROTO;
        return -1;
    }

    memcpy(raw, evt, MOST_FRAME_LEN);
    memset(evt, 0, sizeof(*evt));
    evt->type = MOST_EVT_CTRL_RX;
    evt->len = MOST_FRAME_LEN;
    memcpy(evt->data, raw, MOST_FRAME_LEN);
    return 0;
}

/* ------------------------------------------------------------ */
/* Open device: devname="0" -> /dev/most0 */
struct most_handle *most_open(const char *devname)
{
    struct most_handle *h = calloc(1, sizeof(*h));
    if (!h)
        return NULL;

    snprintf(h->devpath, sizeof(h->devpath), "/dev/most%s", devname);

    h->fd = open(h->devpath, O_RDWR | O_CLOEXEC);
    if (h->fd < 0) {
        free(h);
        return NULL;
    }

    return h;
}

/* ------------------------------------------------------------ */
void most_close(struct most_handle *h)
{
    if (!h)
        return;
    if (h->fd >= 0)
        close(h->fd);
    most_free_pending(h);
    free(h);
}

/* ------------------------------------------------------------ */
/* Blocking read of the next 21-byte control frame; other driver events read
 * underneath are queued for most_read_event(). */
int most_read(struct most_handle *h, uint8_t buf[MOST_FRAME_LEN])
{
    struct most_event evt;

    if (!h || h->fd < 0)
        return -1;

    for (;;) {
        if (most_pop_ctrl_rx(h, &evt)) {
            if (evt.len < MOST_FRAME_LEN) {
                errno = EPROTO;
                return -1;
            }
            memcpy(buf, evt.data, MOST_FRAME_LEN);
            return 0;
        }

        if (most_read_raw_event(h, &evt) < 0)
            return -1;

        if (evt.type == MOST_EVT_CTRL_RX) {
            if (evt.len < MOST_FRAME_LEN) {
                errno = EPROTO;
                return -1;
            }
            memcpy(buf, evt.data, MOST_FRAME_LEN);
            return 0;
        }

        most_queue_event(h, &evt);
    }
}

int most_read_event(struct most_handle *h, struct most_event *evt)
{
    if (!h || !evt || h->fd < 0) {
        errno = EBADF;
        return -1;
    }

    if (most_pop_event(h, evt))
        return 0;

    return most_read_raw_event(h, evt);
}

/* ------------------------------------------------------------ */
/* Blocking write of exactly 21 bytes */
int most_write(struct most_handle *h, const uint8_t buf[MOST_FRAME_LEN])
{
    ssize_t w;
    size_t sent = 0;

    if (!h || h->fd < 0)
        return -1;

    while (sent < MOST_FRAME_LEN) {
        w = write(h->fd, buf + sent, MOST_FRAME_LEN - sent);

        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (w == 0)
            return -1;  // Should not happen

        sent += w;
    }

    return 0;
}

/* ------------------------------------------------------------ */
/* Poll for ready-to-read data */
int most_poll(struct most_handle *h, int timeout_ms)
{
    int remaining = timeout_ms;
    int ret;
    struct timespec start;
    struct timespec now;
    struct most_event evt;

    if (!h)
        return -1;

    if (most_has_ctrl_rx(h))
        return 1;

    if (timeout_ms > 0)
        clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        struct pollfd pfd = {
            .fd = h->fd,
            .events = POLLIN,
        };

        ret = poll(&pfd, 1, remaining);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ret == 0)
            return 0;
        if (!(pfd.revents & POLLIN))
            return -1;

        if (most_read_raw_event(h, &evt) < 0)
            return -1;

        if (evt.type == MOST_EVT_CTRL_RX) {
            most_queue_event(h, &evt);
            return 1;
        }

        most_queue_event(h, &evt);
        if (timeout_ms < 0)
            continue;
        if (timeout_ms == 0)
            return 0;

        clock_gettime(CLOCK_MONOTONIC, &now);
        remaining = timeout_ms - (int)((now.tv_sec - start.tv_sec) * 1000 +
                                       (now.tv_nsec - start.tv_nsec) / 1000000);
        if (remaining <= 0)
            return 0;
    }
}

int most_poll_event(struct most_handle *h, int timeout_ms)
{
    struct pollfd pfd;
    struct most_event evt;

    if (!h)
        return -1;

    if (h->pending_head)
        return 1;

    pfd.fd = h->fd;
    pfd.events = POLLIN;

    for (;;) {
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (ret == 0)
            return 0;
        if (!(pfd.revents & POLLIN))
            return -1;
        if (most_read_raw_event(h, &evt) < 0)
            return -1;
        most_queue_event(h, &evt);
        return 1;
    }
}

/* ------------------------------------------------------------ */
int most_fd(struct most_handle *h)
{
    if (!h)
        return -1;
    return h->fd;
}

/* ------------------------------------------------------------ */
static int most_do_ioctl(struct most_handle *h, unsigned long cmd, void *arg)
{
    if (!h || h->fd < 0) {
        errno = EBADF;
        return -1;
    }

    if (ioctl(h->fd, cmd, arg) < 0)
        return -1;

    return 0;
}

int most_get_node_addr(struct most_handle *h, uint16_t *node)
{
    __u16 tmp;

    if (!node) {
        errno = EINVAL;
        return -1;
    }

    if (most_do_ioctl(h, OS8104_IOC_GET_NODE_ADDR, &tmp) < 0)
        return -1;

    *node = tmp;
    return 0;
}

int most_set_node_addr(struct most_handle *h, uint16_t node)
{
    __u16 tmp = node;
    return most_do_ioctl(h, OS8104_IOC_SET_NODE_ADDR, &tmp);
}

int most_get_group_addr(struct most_handle *h, uint8_t *group)
{
    __u8 tmp;

    if (!group) {
        errno = EINVAL;
        return -1;
    }

    if (most_do_ioctl(h, OS8104_IOC_GET_GROUP_ADDR, &tmp) < 0)
        return -1;

    *group = tmp;
    return 0;
}

int most_set_group_addr(struct most_handle *h, uint8_t group)
{
    __u8 tmp = group;
    return most_do_ioctl(h, OS8104_IOC_SET_GROUP_ADDR, &tmp);
}

int most_get_link_state(struct most_handle *h, enum most_link_state *state)
{
    __u8 tmp;

    if (!state) {
        errno = EINVAL;
        return -1;
    }

    if (most_do_ioctl(h, OS8104_IOC_GET_LINK_STATE, &tmp) < 0)
        return -1;

    switch (tmp) {
    case MOST_LINK_DOWN:
    case MOST_LINK_ACQUIRING:
    case MOST_LINK_UP:
    case MOST_LINK_DEGRADED:
        *state = (enum most_link_state)tmp;
        return 0;
    default:
        errno = EPROTO;
        return -1;
    }
}

int most_reset_chip(struct most_handle *h)
{
    return most_do_ioctl(h, OS8104_IOC_RESET_CHIP, NULL);
}

int most_reinit_link(struct most_handle *h)
{
    return most_do_ioctl(h, OS8104_IOC_REINIT_LINK, NULL);
}

int most_get_stats(struct most_handle *h, struct most_stats *stats)
{
    if (!stats) {
        errno = EINVAL;
        return -1;
    }

    return most_do_ioctl(h, OS8104_IOC_GET_STATS, stats);
}

int most_reg_read(struct most_handle *h, uint16_t reg, void *buf, uint16_t len)
{
    struct most_reg_io io;

    if (!buf || len > sizeof(io.data)) {
        errno = EINVAL;
        return -1;
    }

    memset(&io, 0, sizeof(io));
    io.reg = reg;
    io.len = len;

    if (most_do_ioctl(h, OS8104_IOC_REG_READ, &io) < 0)
        return -1;

    memcpy(buf, io.data, len);
    return 0;
}

int most_reg_write(struct most_handle *h, uint16_t reg, const void *buf, uint16_t len)
{
    struct most_reg_io io;

    if ((!buf && len) || len > sizeof(io.data)) {
        errno = EINVAL;
        return -1;
    }

    memset(&io, 0, sizeof(io));
    io.reg = reg;
    io.len = len;
    if (len)
        memcpy(io.data, buf, len);

    return most_do_ioctl(h, OS8104_IOC_REG_WRITE, &io);
}

int most_cp_read_paged(struct most_handle *h, uint8_t page, uint8_t reg, void *buf, uint16_t len)
{
    uint8_t page_sel = page;
    uint8_t page_zero = 0;
    int ret;

    if (!buf) {
        errno = EINVAL;
        return -1;
    }

    if (page > 3) {
        errno = ERANGE;
        return -1;
    }
    if ((uint16_t)reg + len > 0x0100) {
        errno = ERANGE;
        return -1;
    }

    if (page != 0) {
        ret = most_reg_write(h, 0x00ff, &page_sel, 1);
        if (ret < 0)
            return -1;
    }

    ret = most_reg_read(h, reg, buf, len);
    if (page != 0) {
        int restore_ret = most_reg_write(h, 0x00ff, &page_zero, 1);
        if (ret == 0 && restore_ret < 0)
            ret = restore_ret;
    }

    return ret;
}

int most_cp_write_paged(struct most_handle *h, uint8_t page, uint8_t reg, const void *buf, uint16_t len)
{
    uint8_t page_sel = page;
    uint8_t page_zero = 0;
    int ret;

    if (len != 0 && !buf) {
        errno = EINVAL;
        return -1;
    }

    if (page > 3) {
        errno = ERANGE;
        return -1;
    }
    if ((uint16_t)reg + len > 0x0100) {
        errno = ERANGE;
        return -1;
    }

    if (page != 0) {
        ret = most_reg_write(h, 0x00ff, &page_sel, 1);
        if (ret < 0)
            return -1;
    }

    ret = most_reg_write(h, reg, buf, len);
    if (page != 0) {
        int restore_ret = most_reg_write(h, 0x00ff, &page_zero, 1);
        if (ret == 0 && restore_ret < 0)
            ret = restore_ret;
    }

    return ret;
}

int most_cp_update_bits_paged(struct most_handle *h, uint8_t page, uint8_t reg, uint8_t mask,
                              uint8_t value)
{
    uint8_t current;
    uint8_t next;

    if (most_cp_read_paged(h, page, reg, &current, 1) < 0)
        return -1;

    next = (uint8_t)((current & ~mask) | (value & mask));
    return most_cp_write_paged(h, page, reg, &next, 1);
}

int most_async_write(struct most_handle *h, const struct most_async_tx *tx)
{
    struct most_async_tx tmp;

    if (!tx) {
        errno = EINVAL;
        return -1;
    }
    if (tx->len > sizeof(tmp.data)) {
        errno = EINVAL;
        return -1;
    }

    tmp = *tx;
    return most_do_ioctl(h, OS8104_IOC_ASYNC_TX, &tmp);
}
