// SPDX-License-Identifier: GPL-2.0
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/minmax.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>

#include "os8104.h"

#define OS8104_EVT_MAX 256
#define OS8104_CTRL_TX_TIMEOUT_MS 500
#define OS8104_ASYNC_TX_TIMEOUT_MS 1500

static DEFINE_IDA(os8104_ida);

static void os8104_ctrl_tx_clear_locked(struct os8104_ctrl_tx *tx)
{
	tx->busy = false;
	tx->deadline_jiffies = 0;
}

static void os8104_async_clear_locked(struct os8104_async_state *async)
{
	async->busy = false;
	async->deadline_jiffies = 0;
	async->target_addr = 0;
	async->priority = 0;
	async->len = 0;
	memset(async->frame, 0, sizeof(async->frame));
}

void os8104_abort_ctrl_tx(struct os8104 *chip, int status, u16 aux)
{
	struct os8104_ctrl_tx *tx = &chip->ctrl_tx;
	struct most_evt evt = {
		.type = MOST_EVT_CTRL_TX_FAIL,
		.status = status,
		.aux = aux,
		.len = OS8104_MTCMB_SIZE,
	};
	unsigned long flags;
	bool queued = false;

	spin_lock_irqsave(&tx->lock, flags);
	if (tx->busy) {
		memcpy(evt.data, tx->frame, sizeof(tx->frame));
		os8104_ctrl_tx_clear_locked(tx);
		queued = true;
	}
	spin_unlock_irqrestore(&tx->lock, flags);

	if (!queued)
		return;

	mutex_lock(&chip->state_lock);
	chip->stats.ctrl_tx_fail++;
	mutex_unlock(&chip->state_lock);

	if (os8104_queue_event(chip, &evt))
		dev_warn(chip->dev, "failed to queue aborted control TX event");
}

void os8104_abort_async_tx(struct os8104 *chip, int status, u16 aux)
{
	struct os8104_async_state *async = &chip->async;
	struct most_evt evt = {
		.type = MOST_EVT_ASYNC_TX_FAIL,
		.status = status,
		.aux = aux,
	};
	bool queued = false;

	mutex_lock(&async->lock);
	if (async->busy) {
		evt.len = OS8104_ASYNC_TARGET_ADDR_SIZE + async->len;
		memcpy(evt.data, async->frame, evt.len);
		os8104_async_clear_locked(async);
		queued = true;
	}
	mutex_unlock(&async->lock);

	if (!queued)
		return;

	mutex_lock(&chip->state_lock);
	chip->stats.async_tx_fail++;
	mutex_unlock(&chip->state_lock);

	if (os8104_queue_event(chip, &evt))
		dev_warn(chip->dev, "failed to queue aborted async TX event");
}

static int os8104_async_tx_link_ready(struct os8104 *chip)
{
	unsigned int bcm2;
	enum most_phy_state state = READ_ONCE(chip->phy_state);
	int ret;

	ret = regmap_read(chip->regmap, OS8104_REG_BCM2, &bcm2);
	if (ret)
		return ret;

	/*
	 * The datasheet warns against starting async packet traffic while the
	 * device is unlocked. Require both the live PLL lock bit and the driver's
	 * converged link model before programming mAXP/bPSTX.
	 */
	if ((bcm2 & OS8104_BCM2_LOK) || state != MOST_PHY_LOCKED)
		return -ENOLINK;

	return 0;
}

static int os8104_reinit_chip_locked(struct os8104 *chip)
{
	int ret;

	regmap_write(chip->regmap, OS8104_REG_BIE, OS8104_BIE_DISABLE);
	regmap_write(chip->regmap, OS8104_REG_BMSGC,
		     OS8104_BMSGC_RALC | OS8104_BMSGC_RERR |
		     OS8104_BMSGC_RMTX | OS8104_BMSGC_RMRX);

	disable_irq(chip->irq);
	disable_irq(chip->aint_irq);
	disable_irq(chip->fault_irq);
	disable_irq(chip->status_irq);
	disable_irq(chip->moststatus_irq);

	cancel_delayed_work_sync(&chip->ctrl_tx.timeout_work);
	cancel_delayed_work_sync(&chip->async.timeout_work);
	os8104_abort_ctrl_tx(chip, -ECANCELED, 0);
	os8104_abort_async_tx(chip, -ECANCELED, 0);

	ret = os8104_setup_after_reset(chip);
	if (ret)
		goto out_enable_irqs;

	ret = os8104_setup_timing_slave(chip);
	if (ret)
		goto out_enable_irqs;

	os8104_update_health(chip);
	os8104_schedule_output_update(chip);

	ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC,
			      OS8104_BMSGC_RALC | OS8104_BMSGC_RERR |
			      OS8104_BMSGC_RMTX | OS8104_BMSGC_RMRX);
	if (ret)
		dev_warn(chip->dev, "Failed to clear bMSGC latches after reinit: %d", ret);

out_enable_irqs:
	enable_irq(chip->moststatus_irq);
	enable_irq(chip->status_irq);
	enable_irq(chip->fault_irq);
	enable_irq(chip->aint_irq);
	enable_irq(chip->irq);
	return ret;
}

/* misc_open starts with file->private_data=miscdevice; switch it to our chip. */
static int os8104_misc_open(struct inode *inode, struct file *file)
{
	struct miscdevice *mdev = file->private_data;
	struct os8104 *chip = container_of(mdev, struct os8104, miscdev);
	int ret = 0;

	mutex_lock(&chip->state_lock);
	if (chip->device_open)
		ret = -EBUSY;
	else
		chip->device_open = true;
	mutex_unlock(&chip->state_lock);

	if (ret)
		return ret;

	file->private_data = chip;
	return 0;
}

static int os8104_misc_release(struct inode *inode, struct file *file)
{
	struct os8104 *chip = file->private_data;

	if (!chip)
		return 0;

	mutex_lock(&chip->state_lock);
	chip->device_open = false;
	mutex_unlock(&chip->state_lock);

	return 0;
}

/* Pop exactly one queued event to userspace; caller must provide full event size. */
static ssize_t os8104_misc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct os8104 *chip = file->private_data;
	struct os8104_evt_msg *msg;
	unsigned long flags;
	int ret;

	/* Enforce whole-event reads to keep framing simple and stable. */
retry:
	spin_lock_irqsave(&chip->evt_lock, flags);
	if (list_empty(&chip->evt_queue)) {
		spin_unlock_irqrestore(&chip->evt_lock, flags);
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(chip->evt_wq,
					       !list_empty(&chip->evt_queue));
		if (ret)
			return ret;
		goto retry;
	}

	msg = list_first_entry(&chip->evt_queue, struct os8104_evt_msg, list);
	if (count < sizeof(msg->evt)) {
		spin_unlock_irqrestore(&chip->evt_lock, flags);
		return -EMSGSIZE;
	}

	list_del(&msg->list);
	chip->evt_queued--;
	spin_unlock_irqrestore(&chip->evt_lock, flags);

	if (copy_to_user(buf, &msg->evt, sizeof(msg->evt)))
		ret = -EFAULT;
	else
		ret = sizeof(msg->evt);

	kfree(msg);
	return ret;
}

/* Userspace write path: one complete 21-byte TCMB frame per write call. */
static ssize_t os8104_misc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct os8104 *chip = file->private_data;
	u8 tcmb[OS8104_MTCMB_SIZE] = {0};
	int ret;

	if (count != OS8104_MTCMB_SIZE)
		return -EINVAL;

	if (copy_from_user(tcmb, buf, OS8104_MTCMB_SIZE))
		return -EFAULT;

	ret = os8104_submit_ctrl_tx(chip, tcmb, sizeof(tcmb));
	return ret ? ret : OS8104_MTCMB_SIZE;
}

/* poll()/select() support for readable queued events. */
static __poll_t os8104_misc_poll(struct file *file, poll_table *wait)
{
	struct os8104 *chip = file->private_data;
	unsigned long flags;
	__poll_t mask = 0;

	poll_wait(file, &chip->evt_wq, wait);

	spin_lock_irqsave(&chip->evt_lock, flags);
	if (!list_empty(&chip->evt_queue))
		mask |= POLLIN | POLLRDNORM;
	spin_unlock_irqrestore(&chip->evt_lock, flags);

	return mask;
}

/* Character-device ioctls: addressing, link state/reset, stats, raw register
 * and CP-page access, async TX. */
static long os8104_misc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct os8104 *chip = file->private_data;
	int ret = 0;

	switch (cmd) {
	case OS8104_IOC_GET_NODE_ADDR: {
		u16 val = chip->node_addr;
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			ret = -EFAULT;
		break;
	}
	case OS8104_IOC_SET_NODE_ADDR: {
		u16 val;
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		ret = os8104_set_node_addr(chip, val);
		if (!ret)
			chip->node_addr = val;
		break;
	}
	case OS8104_IOC_GET_GROUP_ADDR: {
		u8 val = chip->group_addr;
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			ret = -EFAULT;
		break;
	}
	case OS8104_IOC_SET_GROUP_ADDR: {
		u8 val;
		if (copy_from_user(&val, (void __user *)arg, sizeof(val)))
			return -EFAULT;
		ret = os8104_set_group_addr(chip, val);
		if (!ret)
			chip->group_addr = val;
		break;
	}
	case OS8104_IOC_GET_LINK_STATE: {
		/*
		 * Expose the driver's current PHY state as a compact userspace
		 * enum value (0..3), matching most_phy_state ordering.
		 */
		u8 val = (u8)READ_ONCE(chip->phy_state);
		if (copy_to_user((void __user *)arg, &val, sizeof(val)))
			ret = -EFAULT;
		break;
	}
	case OS8104_IOC_RESET_CHIP:
		ret = os8104_reset_chip(chip);
		break;
	case OS8104_IOC_REINIT_LINK:
		ret = os8104_reinit_link(chip);
		break;
	case OS8104_IOC_GET_STATS: {
		struct most_stats stats;

		mutex_lock(&chip->state_lock);
		stats = chip->stats;
		stats.evt_dropped = chip->evt_dropped;
		mutex_unlock(&chip->state_lock);

		if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
			ret = -EFAULT;
		break;
	}
	case OS8104_IOC_REG_READ: {
		struct os8104_reg_io reg_io;

		if (copy_from_user(&reg_io, (void __user *)arg, sizeof(reg_io)))
			return -EFAULT;
		if (reg_io.len > sizeof(reg_io.data))
			return -EINVAL;

		ret = os8104_read_mem(chip, reg_io.reg, reg_io.data, reg_io.len);
		if (ret)
			break;
		if (copy_to_user((void __user *)arg, &reg_io, sizeof(reg_io)))
			ret = -EFAULT;
		break;
	}
	case OS8104_IOC_REG_WRITE: {
		struct os8104_reg_io reg_io;

		if (copy_from_user(&reg_io, (void __user *)arg, sizeof(reg_io)))
			return -EFAULT;
		if (reg_io.len > sizeof(reg_io.data))
			return -EINVAL;

		ret = os8104_write_mem(chip, reg_io.reg, reg_io.data, reg_io.len);
		break;
	}
	case OS8104_IOC_ASYNC_TX: {
		struct os8104_async_tx tx;

		if (copy_from_user(&tx, (void __user *)arg, sizeof(tx)))
			return -EFAULT;

		ret = os8104_submit_async_tx(chip, &tx);
		break;
	}
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static const struct file_operations os8104_fops = {
	.owner          = THIS_MODULE,
	.open           = os8104_misc_open,
	.release        = os8104_misc_release,
	.read           = os8104_misc_read,
	.write          = os8104_misc_write,
	.poll           = os8104_misc_poll,
	.unlocked_ioctl = os8104_misc_ioctl,
	.compat_ioctl   = os8104_misc_ioctl,
	.llseek         = noop_llseek,
};

/* Push one fully-formed event onto the userspace event queue. */
int os8104_queue_event(struct os8104 *chip, const struct most_evt *evt)
{
	struct os8104_evt_msg *msg;
	unsigned long flags;

	if (!evt || evt->len > sizeof(evt->data))
		return -EINVAL;

	msg = kmalloc(sizeof(*msg), GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	msg->evt = *evt;

	spin_lock_irqsave(&chip->evt_lock, flags);
	if (chip->evt_queued >= OS8104_EVT_MAX) {
		chip->evt_dropped++;
		spin_unlock_irqrestore(&chip->evt_lock, flags);
		kfree(msg);
		return -ENOSPC;
	}

	msg->evt.seq = ++chip->evt_seq;
	msg->evt.ts_ns = ktime_get_ns();
	list_add_tail(&msg->list, &chip->evt_queue);
	chip->evt_queued++;
	spin_unlock_irqrestore(&chip->evt_lock, flags);

	wake_up_interruptible(&chip->evt_wq);
	return 0;
}

/* Wrap a received RCMB frame as a typed control-RX event. */
int os8104_queue_ctrl_rx_event(struct os8104 *chip, const u8 *data, size_t len)
{
	struct most_evt evt = {
		.type = MOST_EVT_CTRL_RX,
		.len = len,
	};

	if (len > sizeof(evt.data))
		return -EINVAL;

	memcpy(evt.data, data, len);
	mutex_lock(&chip->state_lock);
	chip->stats.ctrl_rx++;
	mutex_unlock(&chip->state_lock);
	return os8104_queue_event(chip, &evt);
}

int os8104_queue_async_rx_event(struct os8104 *chip, const u8 *data, size_t len, int status, u16 aux)
{
	struct most_evt evt = {
		.type = MOST_EVT_ASYNC_RX,
		.flags = MOST_EVT_FLAG_ASYNC_RX_RAW_MARP,
		.status = status,
		.aux = aux,
		.len = len,
	};

	if (len > sizeof(evt.data))
		return -EINVAL;

	if (data && len)
		memcpy(evt.data, data, len);

	mutex_lock(&chip->state_lock);
	if (status)
		chip->stats.async_rx_err++;
	else
		chip->stats.async_rx++;
	mutex_unlock(&chip->state_lock);

	return os8104_queue_event(chip, &evt);
}

int os8104_submit_ctrl_tx(struct os8104 *chip, const u8 *data, size_t len)
{
	struct os8104_ctrl_tx *tx = &chip->ctrl_tx;
	unsigned long flags;
	int ret;

	if (len != OS8104_MTCMB_SIZE)
		return -EINVAL;

	spin_lock_irqsave(&tx->lock, flags);
	if (tx->busy) {
		spin_unlock_irqrestore(&tx->lock, flags);
		return -EBUSY;
	}

	tx->busy = true;
	tx->deadline_jiffies = jiffies + msecs_to_jiffies(OS8104_CTRL_TX_TIMEOUT_MS);
	memcpy(tx->frame, data, len);
	spin_unlock_irqrestore(&tx->lock, flags);

	mod_delayed_work(system_wq, &tx->timeout_work,
			 msecs_to_jiffies(OS8104_CTRL_TX_TIMEOUT_MS));

	ret = os8104_send_ctrl_msg(chip, data, len);
	if (ret) {
		cancel_delayed_work_sync(&tx->timeout_work);

		spin_lock_irqsave(&tx->lock, flags);
		os8104_ctrl_tx_clear_locked(tx);
		spin_unlock_irqrestore(&tx->lock, flags);
	}

	return ret;
}

void os8104_complete_ctrl_tx(struct os8104 *chip, bool success, int status, u16 aux)
{
	struct os8104_ctrl_tx *tx = &chip->ctrl_tx;
	struct most_evt evt = {
		.type = success ? MOST_EVT_CTRL_TX_DONE : MOST_EVT_CTRL_TX_FAIL,
		.status = status,
		.aux = aux,
		.len = OS8104_MTCMB_SIZE,
	};
	unsigned long flags;
	bool queued = false;

	spin_lock_irqsave(&tx->lock, flags);
	if (tx->busy) {
		memcpy(evt.data, tx->frame, sizeof(tx->frame));
		os8104_ctrl_tx_clear_locked(tx);
		queued = true;
	}
	spin_unlock_irqrestore(&tx->lock, flags);

	cancel_delayed_work(&tx->timeout_work);

	if (!queued) {
		dev_warn(chip->dev, "control TX completion without in-flight request");
		return;
	}

	mutex_lock(&chip->state_lock);
	if (success)
		chip->stats.ctrl_tx_ok++;
	else
		chip->stats.ctrl_tx_fail++;
	mutex_unlock(&chip->state_lock);

	if (os8104_queue_event(chip, &evt))
		dev_warn(chip->dev, "failed to queue control TX completion event");
}

void os8104_ctrl_tx_timeout_work(struct work_struct *work)
{
	struct os8104_ctrl_tx *tx = container_of(to_delayed_work(work), struct os8104_ctrl_tx, timeout_work);
	struct os8104 *chip = container_of(tx, struct os8104, ctrl_tx);
	struct most_evt evt = {
		.type = MOST_EVT_CTRL_TX_FAIL,
		.status = -ETIMEDOUT,
		.len = OS8104_MTCMB_SIZE,
	};
	unsigned long flags;
	bool timed_out = false;

	spin_lock_irqsave(&tx->lock, flags);
	if (tx->busy && time_after_eq(jiffies, tx->deadline_jiffies)) {
		memcpy(evt.data, tx->frame, sizeof(tx->frame));
		os8104_ctrl_tx_clear_locked(tx);
		timed_out = true;
	}
	spin_unlock_irqrestore(&tx->lock, flags);

	if (!timed_out)
		return;

	mutex_lock(&chip->state_lock);
	chip->stats.ctrl_tx_timeout++;
	mutex_unlock(&chip->state_lock);

	dev_warn(chip->dev, "control TX timed out waiting for MTX");
	if (os8104_queue_event(chip, &evt))
		dev_warn(chip->dev, "failed to queue control TX timeout event");
}

int os8104_submit_async_tx(struct os8104 *chip, const struct os8104_async_tx *tx)
{
	struct os8104_async_state *async = &chip->async;
	u8 pldt;
	u8 astx = OS8104_BPSTX_ASTX;
	int ret;

	if (!tx)
		return -EINVAL;
	if (tx->len > OS8104_ASYNC_DATA_MAX)
		return -EINVAL;

	ret = os8104_async_tx_link_ready(chip);
	if (ret)
		return ret;

	mutex_lock(&chip->async_io_lock);
	mutex_lock(&async->lock);
	if (async->busy) {
		mutex_unlock(&async->lock);
		mutex_unlock(&chip->async_io_lock);
		return -EBUSY;
	}

	async->busy = true;
	async->deadline_jiffies = jiffies + msecs_to_jiffies(OS8104_ASYNC_TX_TIMEOUT_MS);
	async->target_addr = tx->target_addr;
	async->priority = tx->priority;
	async->len = tx->len;
	memset(async->frame, 0, sizeof(async->frame));
	async->frame[0] = (u8)(tx->target_addr >> 8);
	async->frame[1] = (u8)(tx->target_addr & 0xff);
	memcpy(&async->frame[2], tx->data, tx->len);

	/*
	 * bPLDT counts the user data bytes plus the two source-address bytes
	 * inserted by the OS8104 on transmit. It does not include the target
	 * address prefix stored in mAXP or the trailing CRC generated in hardware.
	 */
	pldt = DIV_ROUND_UP(tx->len + OS8104_ASYNC_SOURCE_ADDR_SIZE, 4);
	if (!pldt || pldt > 0x0d) {
		os8104_async_clear_locked(async);
		mutex_unlock(&async->lock);
		mutex_unlock(&chip->async_io_lock);
		return -EINVAL;
	}

	ret = os8104_write_mem(chip, OS8104_REG_MAXP, async->frame, OS8104_ASYNC_TX_FRAME_SIZE);
	if (ret)
		goto err_clear;

	ret = os8104_write_mem(chip, OS8104_REG_BPLDT, &pldt, 1);
	if (ret)
		goto err_clear;

	ret = os8104_write_mem(chip, OS8104_REG_BPPI, &async->priority, 1);
	if (ret)
		goto err_clear;

	ret = os8104_write_mem(chip, OS8104_REG_BPSTX, &astx, 1);
	if (ret)
		goto err_clear;

	mod_delayed_work(system_wq, &async->timeout_work,
			 msecs_to_jiffies(OS8104_ASYNC_TX_TIMEOUT_MS));
	mutex_unlock(&async->lock);
	mutex_unlock(&chip->async_io_lock);
	return 0;

err_clear:
	os8104_async_clear_locked(async);
	mutex_unlock(&async->lock);
	mutex_unlock(&chip->async_io_lock);
	return ret;
}

void os8104_complete_async_tx(struct os8104 *chip, bool success, int status, u16 aux)
{
	struct os8104_async_state *async = &chip->async;
	struct most_evt evt = {
		.type = success ? MOST_EVT_ASYNC_TX_DONE : MOST_EVT_ASYNC_TX_FAIL,
		.status = status,
		.aux = aux,
	};
	bool queued = false;

	mutex_lock(&async->lock);
	if (async->busy) {
		evt.len = OS8104_ASYNC_TARGET_ADDR_SIZE + async->len;
		memcpy(evt.data, async->frame, evt.len);
		os8104_async_clear_locked(async);
		queued = true;
	}
	mutex_unlock(&async->lock);

	if (!queued)
		return;

	cancel_delayed_work(&async->timeout_work);

	mutex_lock(&chip->state_lock);
	if (success)
		chip->stats.async_tx_ok++;
	else
		chip->stats.async_tx_fail++;
	mutex_unlock(&chip->state_lock);

	if (os8104_queue_event(chip, &evt))
		dev_warn(chip->dev, "failed to queue async TX completion event");
}

void os8104_async_tx_timeout_work(struct work_struct *work)
{
	struct os8104_async_state *async = container_of(to_delayed_work(work), struct os8104_async_state, timeout_work);
	struct os8104 *chip = container_of(async, struct os8104, async);
	struct most_evt evt = {
		.type = MOST_EVT_ASYNC_TX_FAIL,
		.status = -ETIMEDOUT,
	};
	bool timed_out = false;

	mutex_lock(&async->lock);
	if (async->busy && time_after_eq(jiffies, async->deadline_jiffies)) {
		evt.len = OS8104_ASYNC_TARGET_ADDR_SIZE + async->len;
		memcpy(evt.data, async->frame, evt.len);
		os8104_async_clear_locked(async);
		timed_out = true;
	}
	mutex_unlock(&async->lock);

	if (timed_out) {
		mutex_lock(&chip->state_lock);
		chip->stats.async_tx_timeout++;
		mutex_unlock(&chip->state_lock);

		if (os8104_queue_event(chip, &evt))
			dev_warn(chip->dev, "failed to queue async TX timeout event");
	}
}

int os8104_reset_chip(struct os8104 *chip)
{
	int ret;

	mutex_lock(&chip->reinit_lock);
	ret = os8104_reinit_chip_locked(chip);
	mutex_unlock(&chip->reinit_lock);

	return ret;
}

int os8104_reinit_link(struct os8104 *chip)
{
	return os8104_reset_chip(chip);
}

/* Drain and free all queued events during shutdown/unregister. */
static void os8104_evt_flush(struct os8104 *chip)
{
	struct os8104_evt_msg *msg, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&chip->evt_lock, flags);
	list_for_each_entry_safe(msg, tmp, &chip->evt_queue, list) {
		list_del(&msg->list);
		kfree(msg);
	}
	chip->evt_queued = 0;
	spin_unlock_irqrestore(&chip->evt_lock, flags);
}

/* Register /dev/mostN and initialize queue state shared with file ops. */
int os8104_register_char(struct os8104 *chip)
{
	int ret;

	INIT_LIST_HEAD(&chip->evt_queue);
	spin_lock_init(&chip->evt_lock);
	init_waitqueue_head(&chip->evt_wq);
	chip->evt_queued = 0;
	chip->evt_dropped = 0;
	chip->evt_seq = 0;
	chip->idx = -1;

	ret = ida_alloc_min(&os8104_ida, 0, GFP_KERNEL);
	if (ret < 0)
		return ret;
	chip->idx = ret;

	chip->miscdev.minor = MISC_DYNAMIC_MINOR;
	chip->miscdev.name = devm_kasprintf(chip->dev, GFP_KERNEL, "most%d", chip->idx);
	if (!chip->miscdev.name) {
		ret = -ENOMEM;
		goto err_ida;
	}

	chip->miscdev.fops = &os8104_fops;
	chip->miscdev.parent = chip->dev;

	ret = misc_register(&chip->miscdev);
	if (ret)
		goto err_ida;

	return 0;

err_ida:
	if (chip->idx >= 0)
		ida_free(&os8104_ida, chip->idx);
	return ret;
}

/* Remove /dev/mostN and release pending RX data for existing readers. */
void os8104_unregister_char(struct os8104 *chip)
{
	misc_deregister(&chip->miscdev);
	if (chip->idx >= 0) {
		ida_free(&os8104_ida, chip->idx);
		chip->idx = -1;
	}
	os8104_evt_flush(chip);
}

/* Probe: allocate/initialize chip state, regmap, misc chardev, and IRQ. */
static int os8104_probe(struct spi_device *spi)
{
	struct os8104 *chip;
	int ret;
	u32 tmp;

	chip = devm_kzalloc(&spi->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &spi->dev;
	chip->spi = spi;

	dev_dbg(&spi->dev, "os8104 SPI probe\n");

	mutex_init(&chip->bus_lock);
	mutex_init(&chip->reinit_lock);
	mutex_init(&chip->output_lock);
	mutex_init(&chip->async_io_lock);
	mutex_init(&chip->state_lock);
	chip->output_state_cached = -1;
	chip->output_pending_state = -1;
	INIT_DELAYED_WORK(&chip->output_work, os8104_output_work);
	spin_lock_init(&chip->ctrl_tx.lock);
	INIT_DELAYED_WORK(&chip->ctrl_tx.timeout_work, os8104_ctrl_tx_timeout_work);
	chip->ctrl_tx.busy = false;
	chip->ctrl_tx.deadline_jiffies = 0;
	mutex_init(&chip->async.lock);
	INIT_DELAYED_WORK(&chip->async.timeout_work, os8104_async_tx_timeout_work);
	os8104_async_clear_locked(&chip->async);
	chip->last_err_valid = false;
	chip->last_err_log_jiffies = 0;
	chip->device_open = false;

	/* Initialise link health state */
	chip->phy_state    = MOST_PHY_DOWN;
	chip->error_streak = 0;
	chip->lock_candidate_since_jiffies = 0;
	chip->acquiring_since_jiffies = 0;
	chip->degraded_since_jiffies = 0;
	os8104_link_init(chip);
	chip->lock_polling = false;

	ret = of_property_read_u32(spi->dev.of_node, "node-addr", &tmp);
	if (ret)
		return dev_err_probe(&spi->dev, ret == -EINVAL ? -EINVAL : ret, "missing/invalid node-addr DT property");
	chip->node_addr = (u16)tmp;

	ret = of_property_read_u32(spi->dev.of_node, "group-addr", &tmp);
	if (ret)
		return dev_err_probe(&spi->dev, ret == -EINVAL ? -EINVAL : ret, "missing/invalid group-addr DT property");
	chip->group_addr = (u8)tmp;


	/* Create regmap instance; share bus_lock with regmap via lock_arg */
	{
		struct regmap_config cfg = os8104_regmap_cfg;

		cfg.lock_arg = chip;
		chip->regmap = devm_regmap_init(&spi->dev, &os8104_regmap_bus, chip, &cfg);
	}

	if (IS_ERR(chip->regmap))
		return dev_err_probe(&spi->dev, PTR_ERR(chip->regmap), "Failed to init regmap\n");

	spi_set_drvdata(spi, chip);

	chip->reset_gpio = devm_gpiod_get(&spi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(chip->reset_gpio))
		return dev_err_probe(&spi->dev, PTR_ERR(chip->reset_gpio), "failed to get reset-gpios");

	chip->fault_gpio = devm_gpiod_get(&spi->dev, "fault", GPIOD_IN);
	if (IS_ERR(chip->fault_gpio))
		return dev_err_probe(&spi->dev, PTR_ERR(chip->fault_gpio), "failed to get fault-gpios");

	chip->status_gpio = devm_gpiod_get(&spi->dev, "status", GPIOD_IN);
	if (IS_ERR(chip->status_gpio))
		return dev_err_probe(&spi->dev, PTR_ERR(chip->status_gpio), "failed to get status-gpios");

	chip->moststatus_gpio = devm_gpiod_get(&spi->dev, "moststatus", GPIOD_IN);
	if (IS_ERR(chip->moststatus_gpio))
		return dev_err_probe(&spi->dev, PTR_ERR(chip->moststatus_gpio), "failed to get moststatus-gpios");

	chip->interrupt_gpio = devm_gpiod_get(&spi->dev, "interrupt", GPIOD_IN);
	if (IS_ERR(chip->interrupt_gpio))
		return dev_err_probe(&spi->dev, PTR_ERR(chip->interrupt_gpio), "failed to get interrupt-gpios");

	chip->aint_gpio = devm_gpiod_get(&spi->dev, "aint", GPIOD_IN);
	if (IS_ERR(chip->aint_gpio))
		return dev_err_probe(&spi->dev, PTR_ERR(chip->aint_gpio), "failed to get aint-gpios");

	chip->irq = gpiod_to_irq(chip->interrupt_gpio);
	if (chip->irq < 0)
		return dev_err_probe(&spi->dev, chip->irq, "Failed to map IRQ\n");

	chip->aint_irq = gpiod_to_irq(chip->aint_gpio);
	if (chip->aint_irq < 0)
		return dev_err_probe(&spi->dev, chip->aint_irq, "Failed to map AINT IRQ\n");

	chip->moststatus_irq = gpiod_to_irq(chip->moststatus_gpio);
	if (chip->moststatus_irq < 0)
		return dev_err_probe(&spi->dev, chip->moststatus_irq, "Failed to map moststatus IRQ\n");

	chip->fault_irq = gpiod_to_irq(chip->fault_gpio);
	if (chip->fault_irq < 0)
		return dev_err_probe(&spi->dev, chip->fault_irq, "Failed to map fault IRQ\n");

	chip->status_irq = gpiod_to_irq(chip->status_gpio);
	if (chip->status_irq < 0)
		return dev_err_probe(&spi->dev, chip->status_irq, "Failed to map status IRQ\n");

	unsigned long irqflags;
	irqflags = irq_get_trigger_type(chip->irq);
	dev_dbg(&spi->dev, "IRQ trigger type: %lu", irqflags);
	/* No trigger type configured on the GPIO IRQ yet: default to falling edge. */
	if(!irqflags)
		irqflags = IRQF_TRIGGER_FALLING;

	INIT_DELAYED_WORK(&chip->lock_work, os8104_lock_worker);
	chip->locked = false;

	ret = os8104_setup_after_reset(chip);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "setup_after_reset failed");
	ret = os8104_setup_timing_slave(chip);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "setup_timing_slave failed");

	os8104_check_revision(chip);
	os8104_update_health(chip);

	ret = os8104_register_char(chip);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "failed to register misc device");

	ret = devm_request_threaded_irq(&spi->dev, chip->irq, NULL, os8104_irq_thread, irqflags | IRQF_ONESHOT, "os8104-int", chip);
	if (ret) {
		dev_err_probe(&spi->dev, ret, "Failed to request IRQ");
		goto err_unregister_char;
	}

	ret = devm_request_threaded_irq(&spi->dev, chip->moststatus_irq, NULL, os8104_status_irq,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"os8104-moststatus", chip);
	if (ret) {
		dev_err_probe(&spi->dev, ret, "Failed to request moststatus IRQ");
		goto err_unregister_char;
	}

	ret = devm_request_threaded_irq(&spi->dev, chip->fault_irq, NULL, os8104_fault_irq,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"os8104-fault", chip);
	if (ret) {
		dev_err_probe(&spi->dev, ret, "Failed to request fault IRQ");
		goto err_unregister_char;
	}

	ret = devm_request_threaded_irq(&spi->dev, chip->status_irq, NULL, os8104_signal_status_irq,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"os8104-status", chip);
	if (ret) {
		dev_err_probe(&spi->dev, ret, "Failed to request status IRQ");
		goto err_unregister_char;
	}

	ret = devm_request_threaded_irq(&spi->dev, chip->aint_irq, NULL, os8104_aint_irq,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"os8104-aint", chip);
	if (ret) {
		dev_err_probe(&spi->dev, ret, "Failed to request AINT IRQ");
		goto err_unregister_char;
	}

	os8104_schedule_output_update(chip);
	
	/* Clear stale latches before first live interrupt handling. */
	ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RALC | OS8104_BMSGC_RERR | OS8104_BMSGC_RMTX | OS8104_BMSGC_RMRX);
	if (ret)
		dev_warn(&spi->dev, "Failed to clear bMSGC latches: %d", ret);

	return 0;

err_unregister_char:
	os8104_unregister_char(chip);
	return ret;
}

/* Remove: tear down the misc device; devm handles the rest. */
static void os8104_remove(struct spi_device *spi)
{
	struct os8104 *chip = spi_get_drvdata(spi);

	dev_dbg(&spi->dev, "os8104 SPI remove\n");

	/* Stop interrupts first so the threaded handler can't run mid-teardown. */
	regmap_write(chip->regmap, OS8104_REG_BIE, OS8104_BIE_DISABLE);
	regmap_write(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RALC | OS8104_BMSGC_RERR | OS8104_BMSGC_RMTX | OS8104_BMSGC_RMRX);
    disable_irq_nosync(chip->irq);
    disable_irq_nosync(chip->fault_irq);
    disable_irq_nosync(chip->status_irq);
    disable_irq_nosync(chip->moststatus_irq);
	disable_irq_nosync(chip->aint_irq);
    synchronize_irq(chip->irq);
    synchronize_irq(chip->fault_irq);
    synchronize_irq(chip->status_irq);
    synchronize_irq(chip->moststatus_irq);
	synchronize_irq(chip->aint_irq);

	cancel_delayed_work_sync(&chip->lock_work);
	cancel_delayed_work_sync(&chip->link_health_work);
	cancel_delayed_work_sync(&chip->output_work);
	cancel_delayed_work_sync(&chip->ctrl_tx.timeout_work);
	cancel_delayed_work_sync(&chip->async.timeout_work);
	cancel_work_sync(&chip->link_work);
	os8104_unregister_char(chip);

	/* Nothing else to clean up because devm_ handles regmap + memory */
}

static const struct of_device_id os8104_of_match[] = {
	{ .compatible = "microchip,os8104" },
	{}
};
MODULE_DEVICE_TABLE(of, os8104_of_match);

static struct spi_driver os8104_driver = {
	.driver = {
		.name           = "os8104",
		.of_match_table = os8104_of_match,
	},
	.probe  = os8104_probe,
	.remove = os8104_remove,
};

module_spi_driver(os8104_driver);

MODULE_AUTHOR("libmost project");
MODULE_DESCRIPTION("Microchip OS8104 MOST25 transceiver (SPI)");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.4.1");
