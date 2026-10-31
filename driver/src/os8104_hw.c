// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/spi/spi.h>          /* struct spi_device, spi_driver */
#include <linux/of.h>               /* device tree support */
#include <linux/of_gpio.h>          /* legacy GPIO DT helpers (optional) */
#include <linux/gpio/consumer.h>    /* devm_gpiod_get(), gpiod_to_irq() */
#include <linux/interrupt.h>        /* request_threaded_irq(), IRQ flags */
#include <linux/iopoll.h>           /* read_poll_timeout */
#include <linux/regmap.h>           /* regmap_read/write */

#include <linux/slab.h>             /* devm_kzalloc */
#include <linux/delay.h>            /* udelay(), msleep(), usleep_range() */
#include <linux/jiffies.h>          /* msecs_to_jiffies */
#include <linux/bitfield.h>         /* FIELD_PREP */

#include <linux/unaligned.h>        /* put_unaligned_be16() */

#include "os8104.h"

#define OS8104_SPI_CMD_WRITE    0x00    // LSB of first byte has to be 0 for a write
#define OS8104_SPI_CMD_READ     0x01    // LSB of first byte has to be 1 for a read
#define OS8104_SPI_READ_DUMMY   0x00    // Just something to clock while reading
#define OS8104_OUTPUT_DISABLE_DEBOUNCE_MS 20
#define OS8104_OUTPUT_ENABLE_DEBOUNCE_MS  200
#define OS8104_REG_PAGESEL      0xff

/* revisions table */
static const struct {
    const char *name;
    u8 bytes[3];
} revisions[] = {
    { "OS8104-A",    {0x10, 0x12, 0x97} },
    { "OS8104-B",    {0x11, 0x10, 0x98} },
    { "OS8104-C",    {0x12, 0x06, 0x99} },
    { "OS8104-D",    {0x13, 0x07, 0x00} },
    { "OS8104A-B",   {0x14, 0x12, 0x02} },
    { "OS8104A-C",   {0x16, 0x05, 0x04} },
    { "OS8104A-D",   {0x17, 0x11, 0x04} },
    { "OS8104A-D1B", {0x18, 0x08, 0x05} },
};

static int os8104_write_reg(struct os8104 *chip, u8 addr, const u8 *value, size_t value_len);
static int os8104_read_reg(struct os8104 *chip, u8 addr, u8 *buf, size_t bytes);
static int os8104_select_page_nolock(struct os8104 *chip, u8 page);

static int os8104_clear_packet_status(struct os8104 *chip, u8 pctc)
{
    if (!pctc)
        return 0;

    return os8104_write_mem(chip, OS8104_REG_BPCTC, &pctc, 1);
}

static void os8104_clear_sampled_xsr_bits(struct os8104 *chip, u8 xsr)
{
    u8 cleared = xsr & ~(OS8104_BXSR_ERR | OS8104_BXSR_EXL | OS8104_BXSR_ESL);
    int ret;

    /*
     * bXSR.ERR/EXL/ESL are sticky capture bits. Once we have sampled and
     * reported them, clear them explicitly so the next IRQ sees fresh fault
     * history instead of a stale latched value.
     */
    if (cleared == xsr)
        return;

    ret = regmap_write(chip->regmap, OS8104_REG_BXSR, cleared);
    if (ret)
        dev_warn(chip->dev, "Failed to clear sampled XSR bits: %d", ret);
}

static void os8104_log_err_state(struct os8104 *chip, u8 bmsgs, u8 xsr)
{
    bool changed;
    bool due_repeat;
    enum most_phy_state state;

    mutex_lock(&chip->state_lock);
    changed = !chip->last_err_valid ||
              chip->last_err_bmsgs != bmsgs ||
              chip->last_err_xsr != xsr;
    state = READ_ONCE(chip->phy_state);
    due_repeat = chip->last_err_valid &&
                 !changed &&
                 (state == MOST_PHY_LOCKED || state == MOST_PHY_DEGRADED) &&
                 time_after_eq(jiffies, chip->last_err_log_jiffies + msecs_to_jiffies(5000));

    if (changed || due_repeat) {
        chip->last_err_bmsgs = bmsgs;
        chip->last_err_xsr = xsr;
        chip->last_err_valid = true;
        chip->last_err_log_jiffies = jiffies;
    }
    mutex_unlock(&chip->state_lock);

    if (!changed && !due_repeat)
        return;

    if (changed) {
        dev_warn(chip->dev,
            "ERR interrupt: bMSGS=0x%02X bXSR=0x%02X%s%s%s%s%s%s%s%s",
            bmsgs,
            xsr,
            (xsr & OS8104_BXSR_FRA)  ? " FRA" : "",
            (xsr & OS8104_BXSR_MSL)  ? " MSL" : "",
            (xsr & OS8104_BXSR_MXL)  ? " MXL" : "",
            (xsr & OS8104_BXSR_ME)   ? " ME" : "",
            (xsr & OS8104_BXSR_ERR)  ? " ERR" : "",
            (xsr & OS8104_BXSR_FRLR) ? " FRLR" : "",
            (xsr & OS8104_BXSR_EXL)  ? " EXL" : "",
            (xsr & OS8104_BXSR_ESL)  ? " ESL" : "");
        return;
    }

    dev_warn(chip->dev,
         "ERR interrupt unchanged for 5s: bMSGS=0x%02X bXSR=0x%02X",
         bmsgs, xsr);
}

static void os8104_queue_hw_error_event(struct os8104 *chip, u8 bmsgs, u8 xsr)
{
    struct most_evt evt = {
        .type = MOST_EVT_HW_ERROR,
        .status = -EIO,
        .aux = xsr,
        .len = 3,
    };

    evt.data[0] = bmsgs;
    evt.data[1] = xsr;
    /*
     * Keep the raw bXSR value intact in evt.aux and evt.data[1], but make the
     * compact classification flags reflect the I2S-based deployment.
     *
     * ESL is a sticky S/PDIF lock-error capture bit. On this board the OS8104
     * source port is used in I2S mode, so ESL is not a meaningful indicator of
     * MOST link health or I2S clock validity. Userspace still receives the raw
     * register value and can inspect ESL directly if needed; we only exclude it
     * from the derived summary flags used for coarse error classification.
     */
    evt.data[2] = ((xsr & OS8104_BXSR_EXL) ? BIT(0) : 0) |
                  ((xsr & OS8104_BXSR_ME)  ? BIT(2) : 0) |
                  ((xsr & OS8104_BXSR_ERR) ? BIT(3) : 0);

    mutex_lock(&chip->state_lock);
    chip->stats.irq_err++;
    mutex_unlock(&chip->state_lock);

    {
        int qret = os8104_queue_event(chip, &evt);

        if (qret == -ENOSPC)
            dev_dbg(chip->dev, "dropping hardware-error event: queue full");
        else if (qret)
            dev_warn(chip->dev, "failed to queue hardware-error event: %d", qret);
    }
}

static void os8104_queue_topology_change_event(struct os8104 *chip)
{
    struct most_evt evt = {
        .type = MOST_EVT_TOPOLOGY_CHANGE,
        .len = 2,
    };
    unsigned int mpr = 0, mdr = 0;

    if (regmap_read(chip->regmap, OS8104_REG_BMPR, &mpr))
        mpr = 0;
    if (regmap_read(chip->regmap, OS8104_REG_BMDR, &mdr))
        mdr = 0;

    evt.aux = ((u16)(mpr & 0xff) << 8) | (u16)(mdr & 0xff);
    evt.data[0] = (u8)mpr;
    evt.data[1] = (u8)mdr;

    mutex_lock(&chip->state_lock);
    chip->stats.topology_change++;
    mutex_unlock(&chip->state_lock);

    if (os8104_queue_event(chip, &evt))
        dev_warn(chip->dev, "failed to queue topology-change event");
}

/* OE bit controls optical transmitter output enable. */
static inline void os8104_output_on(struct os8104 *chip)
{
    int ret = regmap_set_bits(chip->regmap, OS8104_REG_BXCR, OS8104_BXCR_OE);
    if (ret)
        dev_err(chip->dev, "MOST output ON failed: %d", ret);
    else
        dev_dbg(chip->dev, "MOST output ON");
}

static inline void os8104_output_off(struct os8104 *chip)
{
    int ret = regmap_clear_bits(chip->regmap, OS8104_REG_BXCR, OS8104_BXCR_OE);
    if (ret)
        dev_err(chip->dev, "MOST output OFF failed: %d", ret);
    else
        dev_dbg(chip->dev, "MOST output OFF");
}

static void os8104_apply_output_state(struct os8104 *chip, int status_level)
{
    if (status_level) {
        dev_dbg(chip->dev, "MOST signal OFF - disabling optical output");
        os8104_output_off(chip);
    } else {
        dev_dbg(chip->dev, "MOST signal present - enabling optical output");
        os8104_output_on(chip);
    }
}

static unsigned int os8104_output_delay_ms(int status_level)
{
    /* Asymmetric hysteresis: drop output quickly, restore only after stability. */
    return status_level ? OS8104_OUTPUT_DISABLE_DEBOUNCE_MS
                        : OS8104_OUTPUT_ENABLE_DEBOUNCE_MS;
}

/*
 * Edge-triggered IRQ path:
 * - Snapshot moststatus
 * - Store latest pending level
 * - Re-arm delayed work (coalesces chatter)
 */
void os8104_schedule_output_update(struct os8104 *chip)
{
    int val = gpiod_get_value_cansleep(chip->moststatus_gpio);

    if (val < 0) {
        dev_warn(chip->dev, "moststatus read failed while scheduling output update: %d", val);
        return;
    }

    mutex_lock(&chip->output_lock);
    chip->output_pending_state = val;
    mutex_unlock(&chip->output_lock);

    mod_delayed_work(system_wq, &chip->output_work,
             msecs_to_jiffies(os8104_output_delay_ms(val)));
}

void os8104_output_work(struct work_struct *work)
{
    struct os8104 *chip = container_of(to_delayed_work(work), struct os8104, output_work);
    int val;
    int apply_state = -1;
    int pending_state;

    val = gpiod_get_value_cansleep(chip->moststatus_gpio);
    if (val < 0) {
        dev_warn(chip->dev, "moststatus read failed in output worker: %d", val);
        return;
    }

    mutex_lock(&chip->output_lock);
    pending_state = chip->output_pending_state;

    if (val != pending_state) {
        /* Status changed during debounce window: restart with new target level. */
        chip->output_pending_state = val;
        mutex_unlock(&chip->output_lock);

        mod_delayed_work(system_wq, &chip->output_work,
                 msecs_to_jiffies(os8104_output_delay_ms(val)));
        return;
    }

    if (chip->output_state_cached != val) {
        /* Avoid redundant register writes when state is unchanged. */
        chip->output_state_cached = val;
        apply_state = val;
    }
    mutex_unlock(&chip->output_lock);

    if (apply_state >= 0)
        os8104_apply_output_state(chip, apply_state);
}

/*
 * Background worker: re-evaluates link health every 100 ms until the PLL and
 * network lock are stable, then clears the stale ERR latch.
 */
void os8104_lock_worker(struct work_struct *work)
{
    struct os8104 *chip = container_of(to_delayed_work(work), struct os8104, lock_work);
    enum most_phy_state state;
    int ret;

    dev_dbg(chip->dev, "OS8104: lock check");

    os8104_update_health(chip);
    state = READ_ONCE(chip->phy_state);

    if (state == MOST_PHY_LOCKED || state == MOST_PHY_DEGRADED) {
        if (!chip->locked)
            dev_dbg(chip->dev, "OS8104: LOCKED (stable phy_state=%u)", state);

        chip->locked = true;
        chip->lock_polling = false;

        /* Clear any stale ERR latch once stable network lock has returned. */
        ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RERR);
        if (ret)
            dev_warn(chip->dev, "lock worker: failed to clear RESET_ERR_INT: %d", ret);

        return;
    }

    schedule_delayed_work(&chip->lock_work, msecs_to_jiffies(100));
}

/* Identify chip revision by reading version registers. */
void os8104_check_revision(struct os8104 *chip)
{
    struct spi_device *spi = chip->spi;
    u8 ver[3] = {0};
    int i;

    /* Read version registers C4/C5/C6 */
    int ret = os8104_read_reg(chip, OS8104_REG_VERSION, ver, 3);
    if(ret){
        dev_err(&spi->dev, "Error reading revision.");
        return;
    }

    /* Detect revision */
    for (i = 0; i < ARRAY_SIZE(revisions); i++) {
        if (!memcmp(ver, revisions[i].bytes, sizeof(ver))) {
            dev_info(&spi->dev, "Detected revision: %s", revisions[i].name);
            break;
        }
    }

    if (i == ARRAY_SIZE(revisions))
        dev_err(&spi->dev, "Unknown revision: %02X %02X %02X", ver[0], ver[1], ver[2]);
}

/* Write up to 62 bytes starting at a register address, without taking the bus lock. */
static int os8104_write_reg_nolock(struct os8104 *chip, u8 addr, const u8 *value, size_t value_len)
{
    struct spi_device *spi = chip->spi;
    int ret;
    size_t len = 2 + value_len;

    if(len > 64)
        return -EINVAL;

    u8 tx[64];
    u8 rx[64];

    tx[0] = OS8104_SPI_CMD_WRITE;
    tx[1] = addr;

    for (size_t i = 0; i < value_len; i++)
        tx[2 + i] = value[i];

    struct spi_transfer t = {
        .tx_buf = tx,
        .rx_buf = rx,
        .len = len,
        .speed_hz = spi->max_speed_hz,
    };

    dev_dbg(&spi->dev, "wreg 0x%02x len=%zu data=0x%*ph", addr, value_len, (int)value_len, value);
    ret = spi_sync_transfer(spi, &t, 1);
    if (ret < 0)
        return ret;
    return 0;
}

static int os8104_select_page_nolock(struct os8104 *chip, u8 page)
{
    return os8104_write_reg_nolock(chip, OS8104_REG_PAGESEL, &page, 1);
}

/* Read one or more bytes starting at addr without taking the bus lock. */
static int os8104_read_reg_nolock(struct os8104 *chip, u8 addr, u8 *buf, size_t bytes)
{
    struct spi_device *spi = chip->spi;
    int ret;
    if (!buf || bytes == 0)
        return -EINVAL;

    /* Phase 1: set internal register pointer (datasheet write+address sequence). */
    u8 tx1[2] = { OS8104_SPI_CMD_WRITE, addr };

    u8 tx2[64];
    u8 rx2[64];

    if (bytes >= sizeof(tx2))
        return -EINVAL;

    /* Phase 2: issue read command and clock out payload (+1 command byte in rx2[0]). */
    tx2[0] = OS8104_SPI_CMD_READ;
    for (size_t i = 1; i <= bytes; i++)
        tx2[i] = OS8104_SPI_READ_DUMMY;

    struct spi_transfer xfers[2] = {
        {
            .tx_buf = tx1,
            .len = 2,
            .speed_hz = spi->max_speed_hz,
            .cs_change = 1,
        },
        {
            .tx_buf = tx2,
            .rx_buf = rx2,
            .len = bytes + 1,
            .speed_hz = spi->max_speed_hz,
            .cs_change = 0,
        }
    };

    ret = spi_sync_transfer(spi, xfers, 2);
    if (ret)
        return ret;

    for (size_t i = 0; i < bytes; i++)
        buf[i] = rx2[i + 1];

    dev_dbg(&spi->dev, "rreg 0x%02x len=%zu data=0x%*ph", addr, bytes, (int)bytes, buf);

    return 0;
}

/* Serialize writes over the shared SPI bus. */
static int os8104_write_reg(struct os8104 *chip, u8 addr, const u8 *value, size_t value_len)
{
    int ret;

    mutex_lock(&chip->bus_lock);
    ret = os8104_write_reg_nolock(chip, addr, value, value_len);
    mutex_unlock(&chip->bus_lock);

    return ret;
}

/* Serialize reads over the shared SPI bus. */
static int os8104_read_reg(struct os8104 *chip, u8 addr, u8 *buf, size_t bytes)
{
    int ret;

    mutex_lock(&chip->bus_lock);
    ret = os8104_read_reg_nolock(chip, addr, buf, bytes);
    mutex_unlock(&chip->bus_lock);

    return ret;
}

int os8104_write_mem(struct os8104 *chip, u16 addr, const u8 *buf, size_t len)
{
    int ret;
    u8 page = (u8)(addr >> 8);
    u8 off = (u8)(addr & 0xff);

    if (!buf || !len)
        return -EINVAL;
    if ((size_t)off + len > 0x100)
        return -ERANGE;

    mutex_lock(&chip->bus_lock);
    ret = os8104_select_page_nolock(chip, page);
    if (!ret)
        ret = os8104_write_reg_nolock(chip, off, buf, len);
    if (page && !ret)
        ret = os8104_select_page_nolock(chip, 0);
    else if (page)
        os8104_select_page_nolock(chip, 0);
    mutex_unlock(&chip->bus_lock);

    return ret;
}

int os8104_read_mem(struct os8104 *chip, u16 addr, u8 *buf, size_t len)
{
    int ret;
    u8 page = (u8)(addr >> 8);
    u8 off = (u8)(addr & 0xff);

    if (!buf || !len)
        return -EINVAL;
    if ((size_t)off + len > 0x100)
        return -ERANGE;

    mutex_lock(&chip->bus_lock);
    ret = os8104_select_page_nolock(chip, page);
    if (!ret)
        ret = os8104_read_reg_nolock(chip, off, buf, len);
    if (page && !ret)
        ret = os8104_select_page_nolock(chip, 0);
    else if (page)
        os8104_select_page_nolock(chip, 0);
    mutex_unlock(&chip->bus_lock);

    return ret;
}

/* Datasheet power-up sequence and address programming after reset. */
int os8104_setup_after_reset(struct os8104 *chip)
{
    struct device *dev = chip->dev;
    int ret, val;

    dev_dbg(dev, "OS8104: executing Figure 16-1 SPI startup");

    /* Step 1: Assert reset >1 ms */
    gpiod_set_value_cansleep(chip->reset_gpio, 0);
    msleep(2);

    /* Step 2: Release reset */
    gpiod_set_value_cansleep(chip->reset_gpio, 1);
    usleep_range(500, 1000);

    /* Step 3: Wait for INT pin low */
    ret = read_poll_timeout(gpiod_get_value_cansleep, val, val == 0, 500, 500000, false, chip->interrupt_gpio);
    if (ret) {
        dev_err(dev, "Timeout waiting for INT pin low");
        return ret;
    }

    /* Step 4: Write 0x10 to bSDC1 (0x82) */
    ret = regmap_write(chip->regmap, OS8104_REG_BSDC1, OS8104_BSDC1_IO);
    if (ret)
        return dev_err_probe(dev, ret, "writing bSDC1 failed");

    /* Step 5: Write 0x04 to bMSGC (0x85) */
    ret = regmap_write(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RERR);
    if (ret)
        return dev_err_probe(dev, ret, "writing bMSGC failed");

    /* Step 6: Program Node Address (8Ah/8Bh) */
    u8 buf[2];
    put_unaligned_be16(chip->node_addr, buf);   // produces [ MSB, LSB ]
    ret = os8104_write_reg(chip, OS8104_REG_BNAH, buf, 2);
    if (ret) return ret;

    /* Step 7: Program Group Address (89h) */
    ret = regmap_write(chip->regmap, OS8104_REG_BGA, chip->group_addr);
    if (ret) return ret;

    dev_dbg(dev, "OS8104: Init OK");

    return 0;
}

/* Program the device for RX-driven timing slave operation. */
int os8104_setup_timing_slave(struct os8104 *chip)
{
    struct device *dev = chip->dev;
    int ret;
    u8 val;

    dev_dbg(dev, "OS8104: entering Timing-Slave configuration");

    /* -----------------------------------------------------------
     * Step 1: Write 74h to bCM1 (0x83)
     *  - PLL enable
     *  - RMCK at 512Fs
     *  - Crystal frequency 384Fs
     *  - PLL driven from network RX
     * ----------------------------------------------------------- */
    val = FIELD_PREP(OS8104_BCM1_RD_MASK, 0x7) |   /* RD[2:0] = 111 */
          FIELD_PREP(OS8104_BCM1_XTL_MASK, 0x1);   /* XTL[1:0] = 01 */
    ret = regmap_write(chip->regmap, OS8104_REG_BCM1, val);
    if (ret) {
        dev_err(dev, "Failed to write bCM1 (0x83)");
        return ret;
    }

    /* -----------------------------------------------------------
     * Step 2: Write 62h to bXCR (0x80)
     *  - Slave mode
     *  - Transmitter output enabled
     * ----------------------------------------------------------- */
    ret = regmap_write(chip->regmap, OS8104_REG_BXCR, OS8104_BXCR_OE | OS8104_BXCR_LB | OS8104_BXCR_ABY);
    if (ret) {
        dev_err(dev, "Failed to write bXCR (0x80)");
        return ret;
    }

    /* -----------------------------------------------------------
     * Step 3: Write 12h to bSDC1 (0x82)
     *  - FSY output
     *  - SCK output
     *  - Source port outputs NOT muted
     * ----------------------------------------------------------- */
    ret = regmap_write(chip->regmap, OS8104_REG_BSDC1, OS8104_BSDC1_IO | OS8104_BSDC1_MT);
    if (ret) {
        dev_err(dev, "Failed to write bSDC1 (0x82)");
        return ret;
    }

    /* -----------------------------------------------------------
     * Step 4: Write 50h to bXSR (0x81)
     *  - Mask S/PDIF lock errors (MSL) and coding errors (ME)
     *  - Leave transceiver lock errors (MXL clear) visible
     *  - Reset sticky capture bits
     *
     * The source port is used in I2S mode on this board, not S/PDIF mode.
     * For that reason, S/PDIF lock errors are intentionally masked here:
     * ESL may still be captured in raw bXSR for diagnostics, but it should
     * not drive the operational health model for the I2S-based design.
     * ----------------------------------------------------------- */
    ret = regmap_write(chip->regmap, OS8104_REG_BXSR, OS8104_BXSR_MSL | OS8104_BXSR_ME);
    if (ret) {
        dev_err(dev, "Failed to write bXSR (0x81)");
        return ret;
    }

    /* -----------------------------------------------------------
     * Step 5: Write bIE (0x88)
     *  - Enable the interrupts the driver services: ALC, ERR, MTX, MRX
     * ----------------------------------------------------------- */
    ret = regmap_write(chip->regmap, OS8104_REG_BIE,
                       OS8104_BIE_IALC | OS8104_BIE_IERR |
                       OS8104_BIE_IMTX | OS8104_BIE_IMRX);
    if (ret) {
        dev_err(dev, "Failed to write interrupt enable (0x88)");
        return ret;
    }

    /* -----------------------------------------------------------
     * Step 6: Write 0Fh to bMSGC (0x85)
     *  - Reset ALL interrupts
     * ----------------------------------------------------------- */
    ret = regmap_write(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RALC | OS8104_BMSGC_RERR | OS8104_BMSGC_RMTX | OS8104_BMSGC_RMRX);
    if (ret) {
        dev_err(dev, "Failed to write bMSGC (0x85)");
        return ret;
    }

    dev_dbg(dev, "OS8104: Timing-Slave mode configuration complete.");
    return 0;
}

/* Reprogram node address registers at runtime. */
int os8104_set_node_addr(struct os8104 *chip, u16 node_addr)
{
    u8 buf[2];

    put_unaligned_be16(node_addr, buf);

    return os8104_write_reg(chip, OS8104_REG_BNAH, buf, sizeof(buf));
}

/* Reprogram group address register at runtime. */
int os8104_set_group_addr(struct os8104 *chip, u8 group_addr)
{
    return regmap_write(chip->regmap, OS8104_REG_BGA, group_addr);
}

/* ------------------------------------------------------------------------- */
/* Custom minimal regmap bus                                                 */
/* ------------------------------------------------------------------------- */

/* regmap -> hardware read adapter; performs unlocked single-byte read. */
static int os8104_regmap_read(void *context, unsigned int reg, unsigned int *val)
{
    struct os8104 *chip = context;
    u8 tmp;
    int ret;

    ret = os8104_read_reg_nolock(chip, (u8)reg, &tmp, 1);

    *val = tmp;
    return ret;
}

/* regmap -> hardware write adapter; performs unlocked single-byte write. */
static int os8104_regmap_write(void *context, unsigned int reg, unsigned int val)
{
    struct os8104 *chip = context;
    int ret;
    u8 tmp = (u8)val;   // regmap gives 32-bit, give hardware 8-bit

    ret = os8104_write_reg_nolock(chip, (u8)reg, &tmp, 1);

    return ret;
}

const struct regmap_bus os8104_regmap_bus = {
    .reg_write = os8104_regmap_write,
    .reg_read  = os8104_regmap_read,
};

/* regmap lock/unlock wired to the shared bus mutex. */
static void os8104_regmap_lock(void *context)
{
    struct os8104 *chip = context;
    mutex_lock(&chip->bus_lock);
}

static void os8104_regmap_unlock(void *context)
{
    struct os8104 *chip = context;
    mutex_unlock(&chip->bus_lock);
}

/* 8-bit register and 8-bit value */
const struct regmap_config os8104_regmap_cfg = {
    .reg_bits         = 8,
    .val_bits         = 8,
    .use_single_read  = true,
    .use_single_write = true,
    .cache_type       = REGCACHE_NONE,
    .lock             = os8104_regmap_lock,
    .unlock           = os8104_regmap_unlock,
    .lock_arg         = NULL,
};

/*
 * Reads one MOST25 control telegram from RCMB. The caller acknowledges MRX
 * (bMSGC.RMRX) once it has queued the message.
 *
 * Returns:
 *   >0   = number of bytes written into buf
 *   0    = no message available (MRX not set)
 *   <0   = error code
 */
int os8104_recv_ctrl_msg(struct os8104 *chip, u8 *buf, size_t buflen)
{
    unsigned int status;
    int ret;

    if (buflen < OS8104_MRCMB_SIZE)
        return -EINVAL;

    /* 1. Read bMSGS to see if MRX (Message Received) is set */
    ret = regmap_read(chip->regmap, OS8104_REG_BMSGS, &status);
    if (ret)
        return ret;

    if (!(status & OS8104_BMSGS_MRX))
        return 0;   /* No message ready */

    /* 2. Read the entire 20-byte RCMB buffer */
    ret = os8104_read_reg(chip, OS8104_REG_MRCMB, buf, OS8104_MRCMB_SIZE);
    if (ret)
        return ret;

    /* 3. Return message length (always 20 bytes for RCMB) */
    return OS8104_MRCMB_SIZE;
}

/* Write a TCMB buffer to hardware and set bMSGC.STX */
int os8104_send_ctrl_msg(struct os8104 *chip, const u8 *tcmb, size_t len)
{
    u8 buf[OS8104_MTCMB_SIZE] = {0};
    int ret;

    if (len > OS8104_MTCMB_SIZE)
        return -EINVAL;

    memcpy(buf, tcmb, len);

    /* 1. Write transmit buffer (TCMB: 0xC0–0xD4) */
    ret = os8104_write_reg(chip, OS8104_REG_MTCMB, buf, OS8104_MTCMB_SIZE);
    if (ret)
        return ret;

    /* 2. Kick TX: set STX bit in bMSGC (write-only) */
    ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_STX);
    if (ret) {
        dev_err(chip->dev, "Failed to set STX: %d", ret);
        return ret;
    }

    return 0;
}

int os8104_handle_async_status(struct os8104 *chip)
{
    u8 bpcts = 0;
    u8 pctc = 0;
    u8 marp[OS8104_ASYNC_RX_IMAGE_SIZE] = {0};
    int ret;

    mutex_lock(&chip->async_io_lock);
    ret = os8104_read_mem(chip, OS8104_REG_BPCTS, &bpcts, 1);
    if (ret)
        goto out_unlock;

    if (!(bpcts & (OS8104_BPCTS_ARX | OS8104_BPCTS_ATX | OS8104_BPCTS_AF | OS8104_BPCTS_AC))) {
        ret = 0;
        goto out_unlock;
    }

    if (bpcts & OS8104_BPCTS_ARX) {
        ret = os8104_read_mem(chip, OS8104_REG_MARP, marp, sizeof(marp));
        if (ret)
            goto out_unlock;

        ret = os8104_queue_async_rx_event(chip, marp, sizeof(marp), 0, bpcts);
        if (ret)
            dev_warn(chip->dev, "failed to queue async RX event");
        pctc |= OS8104_BPCTC_RARX;
    }

    if (bpcts & (OS8104_BPCTS_AF | OS8104_BPCTS_AC)) {
        u16 aux = bpcts & (OS8104_BPCTS_AF | OS8104_BPCTS_AC);
        int status = (bpcts & OS8104_BPCTS_AC) ? -EBADMSG : -ENOSPC;

        ret = os8104_queue_async_rx_event(chip, NULL, 0, status, aux);
        if (ret)
            dev_warn(chip->dev, "failed to queue async RX error event");

        if (bpcts & OS8104_BPCTS_AF)
            pctc |= OS8104_BPCTC_RAF;
        if (bpcts & OS8104_BPCTS_AC)
            pctc |= OS8104_BPCTC_RAC;
    }

    if (bpcts & OS8104_BPCTS_ATX) {
        os8104_complete_async_tx(chip, true, 0, bpcts);
        pctc |= OS8104_BPCTC_RATX;
    }

    ret = os8104_clear_packet_status(chip, pctc);

out_unlock:
    mutex_unlock(&chip->async_io_lock);
    return ret;
}

irqreturn_t os8104_aint_irq(int irq, void *data)
{
    struct os8104 *chip = data;
    int ret;

    ret = os8104_handle_async_status(chip);
    if (ret)
        dev_warn(chip->dev, "AINT status handling failed: %d", ret);

    return IRQ_HANDLED;
}

irqreturn_t os8104_status_irq(int irq, void *data)
{
    struct os8104 *chip = data;

    /* Defer GPIO reaction to worker so we can debounce and coalesce edges. */
    os8104_schedule_output_update(chip);

    return IRQ_HANDLED;
}

irqreturn_t os8104_fault_irq(int irq, void *data)
{
    struct os8104 *chip = data;

    /* Placeholder: keep a hook for dedicated fault handling. */
    dev_dbg(chip->dev, "fault GPIO IRQ");
    os8104_update_health(chip);
    return IRQ_HANDLED;
}

irqreturn_t os8104_signal_status_irq(int irq, void *data)
{
    struct os8104 *chip = data;

    /* Placeholder: keep a hook for dedicated signal-status handling. */
    dev_dbg(chip->dev, "status GPIO IRQ");
    os8104_update_health(chip);
    return IRQ_HANDLED;
}


/* Threaded IRQ handler: service MRX/MTX/ERR/ALC and update link health. */
irqreturn_t os8104_irq_thread(int irq, void *data)
{
    struct os8104 *chip = data;
    unsigned int status;
    int ret;

    ret = regmap_read(chip->regmap, OS8104_REG_BMSGS, &status);
    if (ret) {
        dev_err(chip->dev, "IRQ read bMSGS failed: %d", ret);
        return IRQ_HANDLED;
    }
    dev_dbg(chip->dev, "INT: bMSGS=0x%02x%s%s%s%s%s%s",
        (u8)status,
        (status & OS8104_BMSGS_RBS) ? " RBS" : "",
        (status & OS8104_BMSGS_TXR) ? " TXR" : "",
        (status & OS8104_BMSGS_ALC) ? " ALC" : "",
        (status & OS8104_BMSGS_ERR) ? " ERR" : "",
        (status & OS8104_BMSGS_MTX) ? " MTX" : "",
        (status & OS8104_BMSGS_MRX) ? " MRX" : "");

    os8104_update_health(chip);

	/* --- Handle MRX --- */
		if (status & OS8104_BMSGS_MRX) {
			u8 rcmb[OS8104_RX_FRAME_SIZE] = {0};
			int len = os8104_recv_ctrl_msg(chip, &rcmb[1], OS8104_MRCMB_SIZE);

			if (len > 0) {
				/*
				 * Userspace expects 21-byte frames aligned with TCMB layout,
				 * so prepend one pad byte to the 20-byte RCMB payload.
				 */
				len++;
					int push = os8104_queue_ctrl_rx_event(chip, rcmb, len);
				if (push)
					dev_warn(chip->dev, "RX queue drop (%d)", push);
		}

		ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RMRX | OS8104_BMSGC_RBE);
		if (ret)
			dev_warn(chip->dev, "Failed to ack MRX: %d", ret);
	}

    /* --- Handle MTX --- */
    if (status & OS8104_BMSGS_MTX) {
		if (status & OS8104_BMSGS_TXR) {
			os8104_complete_ctrl_tx(chip, true, 0, 0);

			/* TX completed: clear MTX latch. */
			ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RMTX);
            if (ret)
                dev_warn(chip->dev, "Failed to ack MTX: %d", ret);
        }
		else {
			/* TX failed: sample error status, then still clear MTX latch. */
			unsigned int xts = 0;
            ret = regmap_read(chip->regmap, OS8104_REG_BXTS, &xts);
            if (ret)
                dev_warn(chip->dev, "Failed to read XTS: %d", ret);
            else
                dev_warn(chip->dev, "Message transmit failed: %02x", xts);

			os8104_complete_ctrl_tx(chip, false, -EIO, (u16)xts);

			ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RMTX);
			if (ret)
				dev_warn(chip->dev, "Failed to ack MTX after failure: %d", ret);
		}
    }

    /* --- Handle ERR --- */
    if (status & OS8104_BMSGS_ERR) {
        unsigned int xsr;
        unsigned int bcm2;
        bool pll_unlocked;

        ret = regmap_read(chip->regmap, OS8104_REG_BXSR, &xsr);
        if (ret) {
            dev_warn(chip->dev, "Failed to read XSR during ERR handling: %d", ret);
            ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RERR);
            if (ret)
                dev_warn(chip->dev, "Failed to clear ERR latch after XSR read failure: %d", ret);
            return IRQ_HANDLED;
        }

        ret = regmap_read(chip->regmap, OS8104_REG_BCM2, &bcm2);
        if (ret) {
            dev_warn(chip->dev, "Failed to read BCM2 during ERR handling: %d", ret);
            bcm2 = OS8104_BCM2_LOK;
        }

        os8104_log_err_state(chip, (u8)status, (u8)xsr);

        os8104_queue_hw_error_event(chip, (u8)status, (u8)xsr);
        os8104_clear_sampled_xsr_bits(chip, (u8)xsr);
        os8104_update_health(chip);

        /*
         * Recover based on the live PLL lock indicator, not on sticky EXL.
         *
         * EXL only means a transceiver unlock happened at some point. It can
         * remain set after the PLL has already recovered, so using EXL alone
         * here causes needless convergence restarts. bCM2.LOK is the current
         * lock-status bit: set means unlocked, clear means locked.
         */
        pll_unlocked = bcm2 & OS8104_BCM2_LOK;
        if (pll_unlocked) {
            if (chip->locked) {
                dev_warn(chip->dev, "MOST: lost lock, restarting convergence");
            }

            os8104_abort_ctrl_tx(chip, -ENOLINK, 0);
            os8104_abort_async_tx(chip, -ENOLINK, 0);
            chip->locked = false;
            if (!chip->lock_polling) {
                chip->lock_polling = true;
                schedule_delayed_work(&chip->lock_work, msecs_to_jiffies(100));
            }
            /* Clear ERR latch so a level IRQ can deassert while we recover. */
            ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RERR);
            if (ret)
                dev_warn(chip->dev, "Failed to clear ERR latch: %d", ret);
            return IRQ_HANDLED;
        }

        /*
         * The error latch is separate from the sticky capture bits above.
         * Clear it after sampling so the IRQ line can deassert.
         */
        ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RERR);
        if (ret)
            dev_warn(chip->dev, "Failed to clear ERR latch: %d", ret);
    }

    /* --- Handle ALC once per IRQ --- */
    if (status & OS8104_BMSGS_ALC) {
        os8104_queue_topology_change_event(chip);
        ret = regmap_set_bits(chip->regmap, OS8104_REG_BMSGC, OS8104_BMSGC_RALC);
        if (ret)
            dev_warn(chip->dev, "Failed to clear ALC latch: %d", ret);
    }

    return IRQ_HANDLED;
}
