// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/unaligned.h>

#include "os8104.h"

#define OS8104_DEGRADED_HOLD_MS 1000
#define OS8104_LOCK_STABLE_MS 200
#define OS8104_ACQUIRING_HOLD_MS 500
#define OS8104_HEALTH_RECHECK_MS 100

static const char *os8104_state_name(enum most_phy_state state)
{
	switch (state) {
	case MOST_PHY_DOWN:
		return "down";
	case MOST_PHY_ACQUIRING:
		return "acquiring";
	case MOST_PHY_LOCKED:
		return "up";
	case MOST_PHY_DEGRADED:
		return "degraded";
	}

	return "unknown";
}

/* Initialize link notification state and work item. */
void os8104_link_init(struct os8104 *chip)
{
	spin_lock_init(&chip->link_lock);
	INIT_WORK(&chip->link_work, os8104_link_work);
	INIT_DELAYED_WORK(&chip->link_health_work, os8104_link_health_work);
	chip->pending_state = chip->phy_state;
	chip->last_notified_state = chip->phy_state;
	chip->lock_candidate_since_jiffies = 0;
	chip->acquiring_since_jiffies = 0;
	chip->degraded_since_jiffies = 0;
}

static void os8104_schedule_health_recheck(struct os8104 *chip)
{
	/*
	 * Health cannot become accurate again if we only update it on unrelated
	 * IRQ traffic. When the link is acquiring or degraded, keep a lightweight
	 * polling loop running so transient EXL history can age out even if the
	 * bus goes quiet.
	 */
	mod_delayed_work(system_wq, &chip->link_health_work,
			 msecs_to_jiffies(OS8104_HEALTH_RECHECK_MS));
}

void os8104_link_health_work(struct work_struct *work)
{
	struct os8104 *chip = container_of(to_delayed_work(work), struct os8104,
					 link_health_work);

	os8104_update_health(chip);
}

/* Emit a uevent with MOST link state for userspace consumers. */
static void os8104_send_uevent(struct os8104 *chip, const char *state)
{
	char *envp[2];

    envp[0] = kasprintf(GFP_KERNEL, "MOST_LINK_STATE=%s", state);
    envp[1] = NULL;

    if (!envp[0])
        return;

    dev_info(chip->dev, "sending uevent: MOST_LINK_STATE=%s\n", state);
    kobject_uevent_env(&chip->dev->kobj, KOBJ_CHANGE, envp);
	kfree(envp[0]);
}

/* Workqueue handler: emit coalesced link-state change notification. */
void os8104_link_work(struct work_struct *work)
{
	struct os8104 *chip = container_of(work, struct os8104, link_work);
	enum most_phy_state state, last;
	unsigned long flags;
	const char *name = NULL;
	unsigned int xsr = 0;
	struct most_evt evt = {
		.type = MOST_EVT_LINK_CHANGE,
		.len = 3,
	};

	spin_lock_irqsave(&chip->link_lock, flags);
	state = chip->pending_state;
	last  = chip->last_notified_state;
	if (state != last)
		chip->last_notified_state = state;
	spin_unlock_irqrestore(&chip->link_lock, flags);

	if (state == last)
		return; /* nothing new to report */

	if (regmap_read(chip->regmap, OS8104_REG_BXSR, &xsr))
		xsr = 0;

	evt.aux = ((u16)last << 8) | (u16)state;
	evt.data[0] = (u8)last;
	evt.data[1] = (u8)state;
	evt.data[2] = (u8)xsr;

	mutex_lock(&chip->state_lock);
	if (state == MOST_PHY_DOWN)
		chip->stats.link_down++;
	else if (state == MOST_PHY_DEGRADED)
		chip->stats.link_degraded++;
	else if (state == MOST_PHY_LOCKED && last != MOST_PHY_LOCKED)
		chip->stats.link_recovered++;
	mutex_unlock(&chip->state_lock);

	if (os8104_queue_event(chip, &evt))
		dev_warn(chip->dev, "failed to queue link-change event");

	name = os8104_state_name(state);
	os8104_send_uevent(chip, name);
}

/* Queue link notification work if the pending state changed. */
static void os8104_queue_link_event(struct os8104 *chip, enum most_phy_state new_state)
{
	unsigned long flags;
	bool schedule = false;

	spin_lock_irqsave(&chip->link_lock, flags);
	if (chip->pending_state != new_state) {
		chip->pending_state = new_state;
		schedule = true;
	}
	spin_unlock_irqrestore(&chip->link_lock, flags);

	/* Emit outside lock; workqueue coalesces bursts of state changes. */
	if (schedule)
		schedule_work(&chip->link_work);
}

/* Saturating increment for the error streak counter. */
static inline void streak_inc(u8 *v)
{
	if (*v < 250)   /* leave headroom; prevents wrap to 0 */
        (*v)++;
}

/* Reset a streak counter to zero. */
static inline void streak_reset(u8 *v)
{
    *v = 0;
}

/* Evaluate PHY health bits and update link state + notifications. */
void os8104_update_health(struct os8104 *chip)
{
	enum most_phy_state prev_state = chip->phy_state;
	unsigned int xsr;
	unsigned int bcm2;
	struct most_hw_status hw;
	bool pll_locked;
	bool ref_locked;
	bool network_locked;

	int ret = regmap_read(chip->regmap, OS8104_REG_BXSR, &xsr);
	if (ret) {
		dev_err(chip->dev, "Health XSR read failed.");
		return;
	}

	ret = regmap_read(chip->regmap, OS8104_REG_BCM2, &bcm2);
	if (ret) {
		dev_err(chip->dev, "Health BCM2 read failed.");
		return;
	}

	hw.frlr = xsr & OS8104_BXSR_FRLR;
	hw.exl = xsr & OS8104_BXSR_EXL;
	pll_locked = !(bcm2 & OS8104_BCM2_LOK);
	ref_locked = hw.frlr;
	network_locked = pll_locked && !ref_locked;

	/*
	 * Ignore bXSR.ESL for link-health decisions.
	 *
	 * ESL is a sticky S/PDIF lock-error capture bit. This board uses the
	 * OS8104 source port in I2S mode, not S/PDIF mode, so an asserted ESL
	 * does not tell us anything useful about MOST ring lock or whether the
	 * Raspberry Pi is synchronized to the generated I2S clocks.
	 *
	 * Leaving ESL in the health model causes the driver to report a degraded
	 * or down link based on a source-port condition that is irrelevant for
	 * the intended clocking mode. Raw bXSR is still reported elsewhere for
	 * diagnostics; this only affects the driver's derived PHY-state machine.
	 */

	/*
	 * Base current lock state on bCM2.LOK, not on sticky bXSR capture bits.
	 *
	 * The datasheet defines bCM2.LOK as the current PLL lock indicator:
	 * LOK clear means locked, LOK set means unlocked. By contrast, bXSR.EXL
	 * is only a sticky "lock error occurred" capture bit and can remain set
	 * long after the PLL has recovered. Using EXL or FRA/FRLR as the primary
	 * link-up test made the driver report DOWN while the transceiver was
	 * actually locked and exchanging control traffic.
	 *
	 * FRLR is still relevant, but only to distinguish *what* the PLL is
	 * locked to. In timing-slave mode with the frequency regulator enabled,
	 * FRLR set means the PLL is locked to the reference clock rather than to
	 * Network RX. Treat that as "not network-locked": after a real loss of
	 * link we expose a short ACQUIRING window, but we do not promote a cold
	 * disconnected node out of DOWN based solely on fallback reference lock.
	 */

	switch (chip->phy_state) {
	case MOST_PHY_DOWN:
		if (network_locked) {
			if (!chip->lock_candidate_since_jiffies)
				chip->lock_candidate_since_jiffies = jiffies;
			if (time_after_eq(jiffies, chip->lock_candidate_since_jiffies +
					  msecs_to_jiffies(OS8104_LOCK_STABLE_MS))) {
				chip->phy_state = MOST_PHY_LOCKED;
				chip->lock_candidate_since_jiffies = 0;
				streak_reset(&chip->error_streak);
			}
		} else {
			/*
			 * In timing-slave mode with the Frequency Regulator enabled, a
			 * disconnected ring can still produce LOK=0 together with FRLR=1,
			 * meaning the PLL is merely locked to the fallback reference clock.
			 * Keep that condition DOWN until genuine network lock is stable.
			 */
			chip->lock_candidate_since_jiffies = 0;
		}
		chip->acquiring_since_jiffies = 0;
		chip->degraded_since_jiffies = 0;
		break;

	case MOST_PHY_ACQUIRING:
		if (network_locked) {
			if (!chip->lock_candidate_since_jiffies)
				chip->lock_candidate_since_jiffies = jiffies;
			if (time_after_eq(jiffies, chip->lock_candidate_since_jiffies +
					  msecs_to_jiffies(OS8104_LOCK_STABLE_MS))) {
				chip->phy_state = MOST_PHY_LOCKED;
				chip->lock_candidate_since_jiffies = 0;
				chip->acquiring_since_jiffies = 0;
				streak_reset(&chip->error_streak);
			}
		} else if (!pll_locked) {
			chip->phy_state = MOST_PHY_DOWN;
			chip->lock_candidate_since_jiffies = 0;
			chip->acquiring_since_jiffies = 0;
			streak_reset(&chip->error_streak);
		} else {
			if (!chip->acquiring_since_jiffies)
				chip->acquiring_since_jiffies = jiffies;
			chip->lock_candidate_since_jiffies = 0;
			if (time_after_eq(jiffies, chip->acquiring_since_jiffies +
					  msecs_to_jiffies(OS8104_ACQUIRING_HOLD_MS))) {
				chip->phy_state = MOST_PHY_DOWN;
				chip->acquiring_since_jiffies = 0;
			}
		}
		chip->degraded_since_jiffies = 0;
		break;

	case MOST_PHY_LOCKED:
		chip->lock_candidate_since_jiffies = 0;
		if (!pll_locked) {
			chip->phy_state = MOST_PHY_DOWN;
			chip->acquiring_since_jiffies = 0;
			streak_reset(&chip->error_streak);
		} else if (ref_locked) {
			chip->phy_state = MOST_PHY_ACQUIRING;
			chip->acquiring_since_jiffies = jiffies;
			streak_reset(&chip->error_streak);
		} else if (hw.exl) {
			/*
			 * Only EXL contributes to the degraded state here.
			 *
			 * ME is the "coding errors masked" configuration bit in bXSR, not a
			 * live fault indicator. Treating it as an error source made the link
			 * state degrade permanently on a normal masked configuration.
			 */
			streak_inc(&chip->error_streak);
			if (chip->error_streak >= 5) {
				chip->phy_state = MOST_PHY_DEGRADED;
				chip->degraded_since_jiffies = jiffies;
			}
		} else {
			streak_reset(&chip->error_streak);
		}
		break;

	case MOST_PHY_DEGRADED:
		chip->lock_candidate_since_jiffies = 0;
		if (!pll_locked) {
			chip->phy_state = MOST_PHY_DOWN;
			chip->acquiring_since_jiffies = 0;
			streak_reset(&chip->error_streak);
		} else if (ref_locked) {
			chip->phy_state = MOST_PHY_ACQUIRING;
			chip->acquiring_since_jiffies = jiffies;
			chip->degraded_since_jiffies = 0;
			streak_reset(&chip->error_streak);
		} else if (!hw.exl) {
			/*
			 * Recover after a bounded clean hold time, not after a number of
			 * incidental health samples. Otherwise the link can stay stuck in
			 * DEGRADED simply because no more IRQs arrive after EXL clears.
			 */
			if (!chip->degraded_since_jiffies)
				chip->degraded_since_jiffies = jiffies;
			if (time_after_eq(jiffies, chip->degraded_since_jiffies +
					  msecs_to_jiffies(OS8104_DEGRADED_HOLD_MS))) {
				chip->phy_state = MOST_PHY_LOCKED;
				chip->degraded_since_jiffies = 0;
			}
		} else {
			chip->degraded_since_jiffies = jiffies;
		}
		break;
	}

	if (chip->phy_state == MOST_PHY_ACQUIRING || chip->phy_state == MOST_PHY_DEGRADED ||
	    (chip->phy_state == MOST_PHY_DOWN && ref_locked))
		os8104_schedule_health_recheck(chip);
	else
		cancel_delayed_work(&chip->link_health_work);

	if (prev_state != chip->phy_state) {
		os8104_queue_link_event(chip, chip->phy_state);
		dev_dbg(chip->dev, "link state %s -> %s (XSR=0x%02X BCM2=0x%02X)",
			os8104_state_name(prev_state), os8104_state_name(chip->phy_state),
			(u8)xsr, (u8)bcm2);
	}
}
