// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk.h>
#include <linux/export.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/mfd/syscon.h>
#include <trace/events/power.h>

#include "clk-regmap.h"
#include "clk-debug.h"

static struct clk_hw *measure;

static DEFINE_SPINLOCK(clk_reg_lock);
static DEFINE_MUTEX(clk_debug_lock);
static LIST_HEAD(clk_hw_debug_mux_list);

#define INVALID_MUX_SEL		0xDEADBEEF

#define TCXO_DIV_4_HZ		4800000
#define SAMPLE_TICKS_1_MS	0x1000
#define SAMPLE_TICKS_27_MS	0x20000

#define XO_DIV4_CNT_DONE	BIT(25)
#define CNT_EN			BIT(20)
#define CLR_CNT			BIT(21)
#define XO_DIV4_TERM_CNT_MASK	GENMASK(19, 0)
#define MEASURE_CNT		GENMASK(24, 0)
#define CBCR_ENA		BIT(0)

/* Sample clock for 'ticks' reference clock ticks. */
static u32 run_measurement(unsigned int ticks, struct regmap *regmap,
		u32 ctl_reg, u32 status_reg)
{
	u32 regval;

	/*
	 * Clear CNT_EN to bring it to good known state and
	 * set CLK_CNT to clear previous count.
	 */
	regmap_update_bits(regmap, ctl_reg, CNT_EN, 0x0);
	regmap_update_bits(regmap, ctl_reg, CLR_CNT, CLR_CNT);

	/*
	 * Wait for timer to become ready
	 * Ideally SW should poll for MEASURE_CNT
	 * but since CLR_CNT is not available across targets
	 * add 1 us delay to let CNT clear /
	 * counter will clear within 3 reference cycle of 4.8 MHz.
	 */
	udelay(1);

	regmap_update_bits(regmap, ctl_reg, CLR_CNT, 0x0);

	/*
	 * Run measurement and wait for completion.
	 */
	regmap_update_bits(regmap, ctl_reg, XO_DIV4_TERM_CNT_MASK,
			   ticks & XO_DIV4_TERM_CNT_MASK);

	regmap_update_bits(regmap, ctl_reg, CNT_EN, CNT_EN);

	regmap_read(regmap, status_reg, &regval);

	while ((regval & XO_DIV4_CNT_DONE) == 0) {
		cpu_relax();
		regmap_read(regmap, status_reg, &regval);
	}

	regmap_update_bits(regmap, ctl_reg, CNT_EN, 0x0);

	regmap_read(regmap, status_reg, &regval);
	regval &= MEASURE_CNT;

	return regval;
}

/*
 * Perform a hardware rate measurement for a given clock.
 * FOR DEBUG USE ONLY: Measurements take ~15 ms!
 */
static unsigned long clk_debug_mux_measure_rate(struct clk_hw *hw)
{
	unsigned long flags, ret = 0;
	u32 gcc_xo4_reg, multiplier = 1;
	u64 raw_count_short, raw_count_full;
	struct clk_debug_mux *meas = to_clk_measure(hw);
	struct measure_clk_data *data = meas->priv;

	clk_prepare_enable(data->cxo);

	spin_lock_irqsave(&clk_reg_lock, flags);

	/* Enable CXO/4 and RINGOSC branch. */
	regmap_read(meas->regmap, data->xo_div4_cbcr, &gcc_xo4_reg);
	gcc_xo4_reg |= BIT(0);
	regmap_write(meas->regmap, data->xo_div4_cbcr, gcc_xo4_reg);

	/*
	 * The ring oscillator counter will not reset if the measured clock
	 * is not running.  To detect this, run a short measurement before
	 * the full measurement.  If the raw results of the two are the same
	 * then the clock must be off.
	 */

	/* Run a short measurement. (~1ms) */
	raw_count_short = run_measurement(SAMPLE_TICKS_1_MS, meas->regmap,
				data->ctl_reg, data->status_reg);

	/* Run a full measurement. (~27ms) */
	raw_count_full = run_measurement(SAMPLE_TICKS_27_MS, meas->regmap,
				data->ctl_reg, data->status_reg);

	gcc_xo4_reg &= ~BIT(0);
	regmap_write(meas->regmap, data->xo_div4_cbcr, gcc_xo4_reg);

	/* Return 0 if the clock is off. */
	if (raw_count_full == raw_count_short)
		ret = 0;
	else {
		/* Compute rate in Hz. */
		raw_count_full = ((raw_count_full * 10) + 15) * TCXO_DIV_4_HZ;
		do_div(raw_count_full, ((SAMPLE_TICKS_27_MS * 10) + 35));
		ret = (raw_count_full * multiplier);
	}

	spin_unlock_irqrestore(&clk_reg_lock, flags);

	clk_disable_unprepare(data->cxo);

	return ret;
}

/**
 * clk_is_debug_mux - Checks if clk is a mux clk
 *
 * @hw: clk to check on
 *
 * Iterate over maintained debug mux clk list to know
 * if concern clk is a debug mux
 *
 * Returns true on success, false otherwise.
 */
static bool clk_is_debug_mux(struct clk_hw *hw)
{
	struct clk_debug_mux *mux;

	if (hw) {
		list_for_each_entry(mux, &clk_hw_debug_mux_list, list)
			if (&mux->hw == hw)
				return true;
	}

	return false;
}

static int clk_find_and_set_parent(struct clk_hw *mux, struct clk_hw *clk)
{
	struct clk_debug_mux *dmux;
	struct clk_hw *parent;
	int i;

	if (!clk || !clk_is_debug_mux(mux))
		return -EINVAL;

	if (mux == clk || !clk_set_parent(mux->clk, clk->clk))
		return 0;

	dmux = to_clk_measure(mux);

	for (i = 0; i < clk_hw_get_num_parents(mux); i++) {
		if (i < dmux->num_mux_sels && dmux->mux_sels[i] == INVALID_MUX_SEL)
			continue;

		parent = clk_hw_get_parent_by_index(mux, i);

		if (!clk_find_and_set_parent(parent, clk))
			return clk_set_parent(mux->clk, parent->clk);
	}

	return -EINVAL;
}

static u8 clk_debug_mux_get_parent(struct clk_hw *hw)
{
	int i, num_parents = clk_hw_get_num_parents(hw);
	struct clk_hw *hw_clk = clk_hw_get_parent(hw);
	struct clk_hw *clk_parent;
	const char *parent;

	if (!hw_clk)
		return 0xFF;

	for (i = 0; i < num_parents; i++) {
		clk_parent = clk_hw_get_parent_by_index(hw, i);
		if (!clk_parent)
			return 0xFF;
		parent = clk_hw_get_name(clk_parent);
		if (!strcmp(parent, clk_hw_get_name(hw_clk))) {
			pr_debug("%s: clock parent - %s, index %d\n", __func__,
				parent, i);
			return i;
		}
	}

	return 0xFF;
}

static int clk_debug_mux_set_mux_sel(struct clk_debug_mux *mux, u32 val)
{
	return regmap_update_bits(mux->regmap, mux->debug_offset,
				  mux->src_sel_mask,
				  val << mux->src_sel_shift);
}

static int clk_debug_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_debug_mux *mux = to_clk_measure(hw);
	int ret;

	if (!mux->mux_sels)
		return 0;

	ret = clk_debug_mux_set_mux_sel(mux, mux->mux_sels[index]);
	if (ret)
		return ret;

	/* Set the mux's post divider bits */
	return regmap_update_bits(mux->regmap, mux->post_div_offset,
				 mux->post_div_mask,
				 (mux->post_div_val - 1) << mux->post_div_shift);
}

static int clk_debug_mux_init(struct clk_hw *hw)
{
	struct clk_debug_mux *mux;
	struct clk_hw *parent;
	unsigned int i;

	mux = to_clk_measure(hw);

	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		parent = clk_hw_get_parent_by_index(hw, i);

		if (!parent && i < mux->num_mux_sels) {
			mux->mux_sels[i] = INVALID_MUX_SEL;
			pr_debug("%s: invalidating %s mux_sel %d\n", __func__,
				 clk_hw_get_name(hw), i);
		}
	}

	return 0;
}

const struct clk_ops clk_debug_mux_ops = {
	.get_parent = clk_debug_mux_get_parent,
	.set_parent = clk_debug_mux_set_parent,
	.debug_init = clk_debug_measure_add,
	.determine_rate = clk_hw_determine_rate_no_reparent,
	.init = clk_debug_mux_init,
};
EXPORT_SYMBOL_GPL(clk_debug_mux_ops);

static int enable_debug_clks(struct clk_hw *mux)
{
	struct clk_debug_mux *meas = to_clk_measure(mux);
	struct clk_hw *parent;
	int ret;

	if (!clk_is_debug_mux(mux))
		return 0;

	parent = clk_hw_get_parent(mux);
	ret = enable_debug_clks(parent);
	if (ret)
		return ret;

	meas->en_mask = meas->en_mask ? meas->en_mask : CBCR_ENA;

	/* Not all muxes have a DEBUG clock. */
	if (meas->cbcr_offset != U32_MAX)
		regmap_update_bits(meas->regmap, meas->cbcr_offset,
				   meas->en_mask, meas->en_mask);

	return 0;
}

static void disable_debug_clks(struct clk_hw *mux)
{
	struct clk_debug_mux *meas = to_clk_measure(mux);
	struct clk_hw *parent;

	if (!clk_is_debug_mux(mux))
		return;

	meas->en_mask = meas->en_mask ? meas->en_mask : CBCR_ENA;

	if (meas->cbcr_offset != U32_MAX)
		regmap_update_bits(meas->regmap, meas->cbcr_offset,
					meas->en_mask, 0);

	parent = clk_hw_get_parent(mux);
	disable_debug_clks(parent);
}

static u32 get_mux_divs(struct clk_hw *mux)
{
	struct clk_debug_mux *meas = to_clk_measure(mux);
	struct clk_hw *parent;
	u32 div_val;

	if (!clk_is_debug_mux(mux))
		return 1;

	WARN_ON(!meas->post_div_val);
	div_val = meas->post_div_val;

	if (meas->pre_div_vals) {
		int i = clk_debug_mux_get_parent(mux);

		div_val *= meas->pre_div_vals[i];
	}
	parent = clk_hw_get_parent(mux);
	return div_val * get_mux_divs(parent);
}

static int clk_debug_measure_set(void *data, u64 val)
{
	struct clk_debug_mux *mux;
	struct clk_hw *hw = data;
	int ret;

	if (!clk_is_debug_mux(hw))
		return 0;

	mux = to_clk_measure(hw);

	mutex_lock(&clk_debug_lock);

	clk_debug_mux_set_mux_sel(mux, val);

	/*
	 * Setting the debug mux select value directly in HW invalidates the
	 * framework parent. Orphan the debug mux so that subsequent set_parent
	 * calls don't short-circuit when new_parent == old_parent. Otherwise,
	 * subsequent reads of "clk_measure" from old_parent will use stale HW
	 * mux select values and report invalid frequencies.
	 */
	ret = clk_set_parent(hw->clk, NULL);
	if (ret)
		pr_err("Failed to orphan debug mux.\n");

	mutex_unlock(&clk_debug_lock);

	return ret;
}

static int clk_debug_measure_get(void *data, u64 *val)
{
	struct clk_debug_mux *mux = NULL;
	struct clk_hw *hw = data;
	struct clk_hw *parent;
	int ret = 0;
	u32 regval;

	if (!measure)
		return -EINVAL;

	mutex_lock(&clk_debug_lock);

	ret = clk_find_and_set_parent(measure, hw);
	if (ret) {
		pr_err("Failed to set the debug mux's parent.\n");
		goto exit;
	}

	parent = clk_hw_get_parent(measure);
	if (parent && clk_is_debug_mux(parent))
		mux = to_clk_measure(parent);

	if (mux && !mux->mux_sels) {
		regmap_read(mux->regmap, mux->period_offset, &regval);
		if (!regval) {
			pr_err("Error reading mccc period register\n");
			goto exit;
		}
		*val = 1000000000000UL;
		do_div(*val, regval);
	} else {
		ret = enable_debug_clks(measure);
		if (ret)
			goto exit;

		*val = clk_debug_mux_measure_rate(measure);

		/* recursively calculate actual freq */
		*val *= get_mux_divs(measure);
		disable_debug_clks(measure);
	}

exit:
	mutex_unlock(&clk_debug_lock);
	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(clk_measure_fops, clk_debug_measure_get,
			 clk_debug_measure_set, "%lld\n");

void clk_debug_measure_add(struct clk_hw *hw, struct dentry *dentry)
{
	debugfs_create_file("clk_measure", 0444, dentry, hw, &clk_measure_fops);
}
EXPORT_SYMBOL_GPL(clk_debug_measure_add);


int devm_clk_register_debug_mux(struct device *pdev, struct clk_debug_mux *mux)
{
	struct clk *clk;

	if (!mux)
		return -EINVAL;

	clk = devm_clk_register(pdev, &mux->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	mutex_lock(&clk_debug_lock);
	list_add(&mux->list, &clk_hw_debug_mux_list);
	mutex_unlock(&clk_debug_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_clk_register_debug_mux);

int clk_debug_measure_register(struct clk_hw *hw)
{
	if (IS_ERR_OR_NULL(measure)) {
		if (clk_is_debug_mux(hw)) {
			measure = hw;
			return 0;
		}
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(clk_debug_measure_register);

/**
 * map_debug_bases - maps each debug mux based on phandle
 * @pdev: the platform device used to find phandles
 * @base: regmap base name used to look up phandle
 * @mux: debug mux that requires a regmap
 *
 * This function attempts to look up and map a regmap for a debug mux
 * using syscon_regmap_lookup_by_phandle if the base name property exists
 * and assigns an appropriate regmap.
 *
 * Returns 0 on success, -EBADR when it can't find base name, -EERROR otherwise.
 */
int map_debug_bases(struct platform_device *pdev, const char *base,
		    struct clk_debug_mux *mux)
{
	if (!of_get_property(pdev->dev.of_node, base, NULL))
		return -EBADR;

	mux->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						     base);
	if (IS_ERR(mux->regmap)) {
		pr_err("Failed to map %s (ret=%ld)\n", base,
				PTR_ERR(mux->regmap));
		return PTR_ERR(mux->regmap);
	}

	/*
	 * syscon_regmap_lookup_by_phandle prepares the 0th clk handle provided
	 * in the device node. The debug clock controller prepares/enables/
	 * disables the required clock, thus detach the clock.
	 */
	regmap_mmio_detach_clk(mux->regmap);

	return 0;
}
EXPORT_SYMBOL_GPL(map_debug_bases);

#define to_clk_fixed_rate(_hw) container_of(_hw, struct clk_fixed_rate, hw)

static unsigned long clk_fixed_rate_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return to_clk_fixed_rate(hw)->fixed_rate;
}

const struct clk_ops clk_fixed_rate_measure_ops = {
	.recalc_rate = clk_fixed_rate_recalc_rate,
	.debug_init = clk_debug_measure_add,
};
EXPORT_SYMBOL_GPL(clk_fixed_rate_measure_ops);

