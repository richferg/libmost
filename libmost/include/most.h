#ifndef MOST_H
#define MOST_H

#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOST_FRAME_LEN 21
#define MOST_EVENT_DATA_LEN 256
#define MOST_ASYNC_DATA_MAX 48
#define MOST_ASYNC_TX_FRAME_LEN (2 + MOST_ASYNC_DATA_MAX)
#define MOST_ASYNC_RX_IMAGE_LEN (4 + MOST_ASYNC_DATA_MAX)

/*
 * Async RX events return the raw OS8104 mARP image:
 *   data[0..1] = target address
 *   data[2..3] = source address
 *   data[4..51] = bARD0..47
 *
 * The chip does not report the actual received payload length, so userspace
 * must parse it from the packet protocol carried in the data bytes.
 */
#define MOST_EVT_FLAG_ASYNC_RX_RAW_MARP (1u << 0)

struct most_handle;

enum most_evt_type {
    MOST_EVT_CTRL_RX = 1,
    MOST_EVT_CTRL_TX_DONE,
    MOST_EVT_CTRL_TX_FAIL,
    MOST_EVT_ASYNC_RX,
    MOST_EVT_ASYNC_TX_DONE,
    MOST_EVT_ASYNC_TX_FAIL,
    MOST_EVT_LINK_CHANGE,
    MOST_EVT_HW_ERROR,
    MOST_EVT_TOPOLOGY_CHANGE,
};

struct most_event {
    __u16 type;
    __u16 flags;
    __u32 seq;
    __u64 ts_ns;
    __s32 status;
    __u16 aux;
    __u16 len;
    __u8 data[MOST_EVENT_DATA_LEN];
};

struct most_stats {
    __u32 ctrl_rx;
    __u32 ctrl_tx_ok;
    __u32 ctrl_tx_fail;
    __u32 ctrl_tx_timeout;
    __u32 async_rx;
    __u32 async_rx_err;
    __u32 async_tx_ok;
    __u32 async_tx_fail;
    __u32 async_tx_timeout;
    __u32 irq_err;
    __u32 topology_change;
    __u32 evt_dropped;
    __u32 link_down;
    __u32 link_degraded;
    __u32 link_recovered;
};

struct most_reg_io {
    __u16 reg;
    __u16 len;
    __u8 data[62];
};

struct most_async_tx {
    __u16 target_addr;
    __u8 priority;
    __u8 len;
    __u8 data[MOST_ASYNC_DATA_MAX];
};

/* Open /dev/mostX (example: "0" -> /dev/most0) */
struct most_handle *most_open(const char *devname);

/* Close the handle */
void most_close(struct most_handle *h);

/* Blocking read of one 21-byte RCMB frame */
int most_read(struct most_handle *h, uint8_t buf[MOST_FRAME_LEN]);

/* Blocking write of one 21-byte TCMB frame */
int most_write(struct most_handle *h, const uint8_t buf[MOST_FRAME_LEN]);

/* Wait up to timeout_ms (<0 = forever) for a control RX frame: 1 when one is
 * ready for most_read(), 0 on timeout, -1 on error. Other event types seen
 * meanwhile are queued for most_read_event(). */
int most_poll(struct most_handle *h, int timeout_ms);

/* Blocking read of one typed driver event */
int most_read_event(struct most_handle *h, struct most_event *evt);

/* Poll for any queued driver event */
int most_poll_event(struct most_handle *h, int timeout_ms);

/* Retrieve the underlying file descriptor (advanced use) */
int most_fd(struct most_handle *h);

/* ---- OS8104-specific ioctl helpers ------------------------------------- */

#define OS8104_IOC_MAGIC             'm'
#define OS8104_IOC_SET_NODE_ADDR     _IOW(OS8104_IOC_MAGIC, 0x01, __u16)
#define OS8104_IOC_GET_NODE_ADDR     _IOR(OS8104_IOC_MAGIC, 0x02, __u16)
#define OS8104_IOC_SET_GROUP_ADDR    _IOW(OS8104_IOC_MAGIC, 0x03, __u8)
#define OS8104_IOC_GET_GROUP_ADDR    _IOR(OS8104_IOC_MAGIC, 0x04, __u8)
#define OS8104_IOC_GET_LINK_STATE    _IOR(OS8104_IOC_MAGIC, 0x05, __u8)
#define OS8104_IOC_RESET_CHIP        _IO(OS8104_IOC_MAGIC, 0x06)
#define OS8104_IOC_REINIT_LINK       _IO(OS8104_IOC_MAGIC, 0x07)
#define OS8104_IOC_GET_STATS         _IOR(OS8104_IOC_MAGIC, 0x08, struct most_stats)
#define OS8104_IOC_REG_READ          _IOWR(OS8104_IOC_MAGIC, 0x09, struct most_reg_io)
#define OS8104_IOC_REG_WRITE         _IOW(OS8104_IOC_MAGIC, 0x0a, struct most_reg_io)
#define OS8104_IOC_ASYNC_TX          _IOW(OS8104_IOC_MAGIC, 0x0b, struct most_async_tx)

enum most_link_state {
    MOST_LINK_DOWN = 0,
    MOST_LINK_ACQUIRING = 1,
    MOST_LINK_UP = 2,
    MOST_LINK_DEGRADED = 3,
};

int most_get_node_addr(struct most_handle *h, uint16_t *node);
int most_set_node_addr(struct most_handle *h, uint16_t node);
int most_get_group_addr(struct most_handle *h, uint8_t *group);
int most_set_group_addr(struct most_handle *h, uint8_t group);
int most_get_link_state(struct most_handle *h, enum most_link_state *state);
int most_reset_chip(struct most_handle *h);
int most_reinit_link(struct most_handle *h);
int most_get_stats(struct most_handle *h, struct most_stats *stats);
int most_reg_read(struct most_handle *h, uint16_t reg, void *buf, uint16_t len);
int most_reg_write(struct most_handle *h, uint16_t reg, const void *buf, uint16_t len);
int most_cp_read_paged(struct most_handle *h, uint8_t page, uint8_t reg, void *buf, uint16_t len);
int most_cp_write_paged(struct most_handle *h, uint8_t page, uint8_t reg, const void *buf, uint16_t len);
int most_cp_update_bits_paged(struct most_handle *h, uint8_t page, uint8_t reg, uint8_t mask,
			      uint8_t value);
int most_async_write(struct most_handle *h, const struct most_async_tx *tx);

#ifdef __cplusplus
}
#endif

#endif /* MOST_H */
