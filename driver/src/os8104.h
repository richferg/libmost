/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _OS8104_H
#define _OS8104_H

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/ioctl.h>

struct device;
struct gpio_desc;
struct regmap;
struct spi_device;

/* Register addresses and bit masks */
#define OS8104_REG_BXCR			0x80
#define OS8104_BXCR_MTR			BIT(7)
#define OS8104_BXCR_OE			BIT(6)
#define OS8104_BXCR_LB			BIT(5)
#define OS8104_BXCR_LPW			BIT(4)
#define OS8104_BXCR_SAN			BIT(3)
#define OS8104_BXCR_SBY			BIT(2)
#define OS8104_BXCR_ABY			BIT(1)
#define OS8104_BXCR_REN			BIT(0)

#define OS8104_REG_BXSR			0x81
#define OS8104_BXSR_FRA			BIT(7)
#define OS8104_BXSR_MSL			BIT(6)
#define OS8104_BXSR_MXL			BIT(5)
#define OS8104_BXSR_ME			BIT(4)
#define OS8104_BXSR_ERR			BIT(3)
#define OS8104_BXSR_FRLR		BIT(2)
#define OS8104_BXSR_ESL			BIT(1)
#define OS8104_BXSR_EXL			BIT(0)

#define OS8104_REG_BSDC1		0x82
#define OS8104_BSDC1_EDG		BIT(7)
#define OS8104_BSDC1_DEL		BIT(6)
#define OS8104_BSDC1_POL		BIT(5)
#define OS8104_BSDC1_IO			BIT(4)
#define OS8104_BSDC1_NBR		BIT(3)
#define OS8104_BSDC1_SPD		BIT(2)
#define OS8104_BSDC1_MT			BIT(1)
#define OS8104_BSDC1_TCE		BIT(0)

#define OS8104_REG_BCM1			0x83
#define OS8104_BCM1_PLD			BIT(7)
#define OS8104_BCM1_RD_MASK		GENMASK(6, 4)
#define OS8104_BCM1_XTL_MASK	GENMASK(3, 2)
#define OS8104_BCM1_MX_MASK		GENMASK(1, 0)

#define OS8104_REG_BMSGC		0x85
#define OS8104_BMSGC_STX		BIT(7)
#define OS8104_BMSGC_RBE		BIT(6)
#define OS8104_BMSGC_SAI		BIT(4)
#define OS8104_BMSGC_RALC		BIT(3)
#define OS8104_BMSGC_RERR		BIT(2)
#define OS8104_BMSGC_RMTX		BIT(1)
#define OS8104_BMSGC_RMRX		BIT(0)

#define OS8104_REG_BMSGS		0x86
#define OS8104_BMSGS_RBS		BIT(7)
#define OS8104_BMSGS_TXR		BIT(6)
#define OS8104_BMSGS_RSVD		(BIT(5) | BIT(4))
#define OS8104_BMSGS_ALC		BIT(3)
#define OS8104_BMSGS_ERR		BIT(2)
#define OS8104_BMSGS_MTX		BIT(1)
#define OS8104_BMSGS_MRX		BIT(0)

#define OS8104_REG_BIE			0x88
#define OS8104_BIE_RSVD			GENMASK(7, 4)
#define OS8104_BIE_IALC			BIT(3)
#define OS8104_BIE_IERR			BIT(2)
#define OS8104_BIE_IMTX			BIT(1)
#define OS8104_BIE_IMRX			BIT(0)
#define OS8104_BIE_DISABLE		0x00

#define OS8104_REG_BGA			0x89
#define OS8104_BGA_MASK			GENMASK(7, 0)

#define OS8104_REG_BNAH			0x8A
#define OS8104_BNAH_MASK		GENMASK(7, 0)

#define OS8104_REG_BNAL			0x8B
#define OS8104_BNAL_MASK		GENMASK(7, 0)

#define OS8104_REG_BMPR			0x90
#define OS8104_REG_BMDR			0x91

#define OS8104_REG_BCM2			0x8E
#define OS8104_BCM2_LOK			BIT(7)

#define OS8104_REG_MRCMB		0xA0
#define OS8104_MRCMB_SIZE		20

#define OS8104_REG_MTCMB		0xC0
#define OS8104_MTCMB_SIZE		21

#define OS8104_REG_VERSION		0xC4				// 0xC4-C6 only returns version information directly after initialisation

#define OS8104_REG_BXTS			0xD5

#define OS8104_REG_BPCTC		0xE2
#define OS8104_BPCTC_RAF		BIT(4)
#define OS8104_BPCTC_RAC		BIT(3)
#define OS8104_BPCTC_RATX		BIT(1)
#define OS8104_BPCTC_RARX		BIT(0)

#define OS8104_REG_BPCTS		0xE3
#define OS8104_BPCTS_AF			BIT(4)
#define OS8104_BPCTS_AC			BIT(3)
#define OS8104_BPCTS_ATX		BIT(1)
#define OS8104_BPCTS_ARX		BIT(0)

#define OS8104_REG_BPSTX		0xEA
#define OS8104_BPSTX_ASTX		BIT(7)

#define OS8104_REG_BPLDT		0xEC
#define OS8104_REG_BPPI			0xF2
#define OS8104_REG_MARP			0x180
#define OS8104_REG_MAXP			0x1C0

#define OS8104_RX_FRAME_SIZE		OS8104_MTCMB_SIZE
#define OS8104_ASYNC_TARGET_ADDR_SIZE	2
#define OS8104_ASYNC_SOURCE_ADDR_SIZE	2
#define OS8104_ASYNC_DATA_MAX		48
#define OS8104_ASYNC_TX_FRAME_SIZE	(OS8104_ASYNC_TARGET_ADDR_SIZE + OS8104_ASYNC_DATA_MAX)
#define OS8104_ASYNC_RX_IMAGE_SIZE	(OS8104_ASYNC_TARGET_ADDR_SIZE + \
					 OS8104_ASYNC_SOURCE_ADDR_SIZE + \
					 OS8104_ASYNC_DATA_MAX)

/*
 * Async RX events expose the raw mARP image because the OS8104 does not report
 * the actual received user-data byte count in hardware. Userspace must parse
 * the packet protocol carried in bARD0..47 to determine payload length.
 */
#define MOST_EVT_FLAG_ASYNC_RX_RAW_MARP	BIT(0)

#define OS8104_IOC_MAGIC		'm'
#define OS8104_IOC_SET_NODE_ADDR _IOW(OS8104_IOC_MAGIC, 0x01, __u16)
#define OS8104_IOC_GET_NODE_ADDR _IOR(OS8104_IOC_MAGIC, 0x02, __u16)
#define OS8104_IOC_SET_GROUP_ADDR _IOW(OS8104_IOC_MAGIC, 0x03, __u8)
#define OS8104_IOC_GET_GROUP_ADDR _IOR(OS8104_IOC_MAGIC, 0x04, __u8)
#define OS8104_IOC_GET_LINK_STATE _IOR(OS8104_IOC_MAGIC, 0x05, __u8)
#define OS8104_IOC_RESET_CHIP    _IO(OS8104_IOC_MAGIC, 0x06)
#define OS8104_IOC_REINIT_LINK   _IO(OS8104_IOC_MAGIC, 0x07)
#define OS8104_IOC_GET_STATS     _IOR(OS8104_IOC_MAGIC, 0x08, struct most_stats)
#define OS8104_IOC_REG_READ      _IOWR(OS8104_IOC_MAGIC, 0x09, struct os8104_reg_io)
#define OS8104_IOC_REG_WRITE     _IOW(OS8104_IOC_MAGIC, 0x0a, struct os8104_reg_io)
#define OS8104_IOC_ASYNC_TX      _IOW(OS8104_IOC_MAGIC, 0x0b, struct os8104_async_tx)

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

struct most_evt {
	__u16 type;
	__u16 flags;
	__u32 seq;
	__u64 ts_ns;
	__s32 status;
	__u16 aux;
	__u16 len;
	__u8 data[256];
};

struct os8104_reg_io {
	__u16 reg;
	__u16 len;
	__u8 data[62];
};

struct os8104_async_tx {
	__u16 target_addr;
	__u8 priority;
	__u8 len;
	__u8 data[OS8104_ASYNC_DATA_MAX];
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

enum most_phy_state {
	MOST_PHY_DOWN = 0,
	MOST_PHY_ACQUIRING,
	MOST_PHY_LOCKED,
	MOST_PHY_DEGRADED,
};

struct most_hw_status {
	bool frlr;
	bool exl;
};

struct os8104_evt_msg {
	struct list_head list;
	struct most_evt evt;
};

struct os8104_ctrl_tx {
	spinlock_t lock;
	struct delayed_work timeout_work;
	bool busy;
	unsigned long deadline_jiffies;
	u8 frame[OS8104_MTCMB_SIZE];
};

struct os8104_async_state {
	struct mutex lock;
	struct delayed_work timeout_work;
	bool busy;
	unsigned long deadline_jiffies;
	u16 target_addr;
	u8 priority;
	u8 len;
	u8 frame[OS8104_ASYNC_TX_FRAME_SIZE];
};

struct os8104 {
	struct device *dev;
	struct spi_device *spi;
	struct regmap *regmap;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *interrupt_gpio;
	struct gpio_desc *aint_gpio;
	struct gpio_desc *fault_gpio;
	struct gpio_desc *status_gpio;
	struct gpio_desc *moststatus_gpio;

	int irq;
	int aint_irq;
	int fault_irq;
	int status_irq;
	int moststatus_irq;

	u16 node_addr;
	u8  group_addr;

	struct mutex bus_lock;
	struct mutex reinit_lock;
	spinlock_t link_lock;

	enum most_phy_state phy_state;
	enum most_phy_state pending_state;
	enum most_phy_state last_notified_state;
	u8 error_streak;
	unsigned long lock_candidate_since_jiffies;
	unsigned long acquiring_since_jiffies;
	unsigned long degraded_since_jiffies;

	struct work_struct link_work;
	struct delayed_work link_health_work;
	struct delayed_work output_work;
	struct mutex output_lock;
	int output_state_cached;
	int output_pending_state;

	bool locked;
	struct delayed_work lock_work;
	bool lock_polling;
	u32 evt_seq;
	struct os8104_ctrl_tx ctrl_tx;
	struct os8104_async_state async;
	struct mutex async_io_lock;
	struct mutex state_lock;
	struct most_stats stats;
	u8 last_err_bmsgs;
	u8 last_err_xsr;
	bool last_err_valid;
	unsigned long last_err_log_jiffies;

	int idx;
	struct miscdevice miscdev;
	struct list_head evt_queue;
	spinlock_t evt_lock;
	wait_queue_head_t evt_wq;
	size_t evt_queued;
	size_t evt_dropped;
	bool device_open;
};

extern const struct regmap_bus os8104_regmap_bus;
extern const struct regmap_config os8104_regmap_cfg;

int os8104_setup_after_reset(struct os8104 *chip);
int os8104_setup_timing_slave(struct os8104 *chip);
void os8104_check_revision(struct os8104 *chip);
void os8104_update_health(struct os8104 *chip);

irqreturn_t os8104_status_irq(int irq, void *data);
irqreturn_t os8104_fault_irq(int irq, void *data);
irqreturn_t os8104_signal_status_irq(int irq, void *data);
irqreturn_t os8104_irq_thread(int irq, void *data);

int os8104_send_ctrl_msg(struct os8104 *chip, const u8 *tcmb, size_t len);
int os8104_recv_ctrl_msg(struct os8104 *chip, u8 *buf, size_t buflen);
	void os8104_link_init(struct os8104 *chip);
	void os8104_link_work(struct work_struct *work);
	void os8104_link_health_work(struct work_struct *work);

int os8104_register_char(struct os8104 *chip);
void os8104_unregister_char(struct os8104 *chip);
int os8104_queue_event(struct os8104 *chip, const struct most_evt *evt);
int os8104_queue_ctrl_rx_event(struct os8104 *chip, const u8 *data, size_t len);
int os8104_queue_async_rx_event(struct os8104 *chip, const u8 *data, size_t len, int status, u16 aux);
void os8104_abort_ctrl_tx(struct os8104 *chip, int status, u16 aux);
int os8104_submit_ctrl_tx(struct os8104 *chip, const u8 *data, size_t len);
void os8104_complete_ctrl_tx(struct os8104 *chip, bool success, int status, u16 aux);
void os8104_ctrl_tx_timeout_work(struct work_struct *work);
void os8104_abort_async_tx(struct os8104 *chip, int status, u16 aux);
int os8104_submit_async_tx(struct os8104 *chip, const struct os8104_async_tx *tx);
void os8104_complete_async_tx(struct os8104 *chip, bool success, int status, u16 aux);
void os8104_async_tx_timeout_work(struct work_struct *work);

void os8104_lock_worker(struct work_struct *work);

void os8104_output_work(struct work_struct *work);
void os8104_schedule_output_update(struct os8104 *chip);
int os8104_set_node_addr(struct os8104 *chip, u16 node_addr);
int os8104_set_group_addr(struct os8104 *chip, u8 group_addr);
int os8104_reset_chip(struct os8104 *chip);
int os8104_reinit_link(struct os8104 *chip);
int os8104_read_mem(struct os8104 *chip, u16 addr, u8 *buf, size_t len);
int os8104_write_mem(struct os8104 *chip, u16 addr, const u8 *buf, size_t len);
int os8104_handle_async_status(struct os8104 *chip);
irqreturn_t os8104_aint_irq(int irq, void *data);

#endif /* _OS8104_H */
