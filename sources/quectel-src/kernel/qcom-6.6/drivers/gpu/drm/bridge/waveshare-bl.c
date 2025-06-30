// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2023 Raspberry Pi Ltd
 *
 * Based on panel-raspberrypi-touchscreen by Broadcom
 */

#include <linux/backlight.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/err.h>

struct ws_panel {
    struct i2c_client *i2c;
    struct backlight_device *bl_dev;
    struct regulator *vddio_i2c;
    struct gpio_desc *switch_gpio;
};

static struct i2c_client *i2c_client;
struct gpio_desc *switch_gpio_t;

// 写读存器函数
int ws_panel_i2c_read(u8 reg)
{
    int ret;

    if (!i2c_client) {
        pr_err("[%s:%d]: i2c_client NULL!\n", __func__, __LINE__);
        return -EINVAL;
    }

    ret = i2c_smbus_read_byte_data(i2c_client, reg);
    if (ret) {
        pr_err("[%s:%d]: I2C read failed at reg 0x%02x: %d\n", __func__, __LINE__, reg, ret);
        return ret;
    }
    
    return ret;
}
EXPORT_SYMBOL_GPL(ws_panel_i2c_read);

// 写寄存器函数
int ws_panel_i2c_write(u8 reg, u8 val)
{
	int ret;

    if (!i2c_client) {
        pr_err("[%s:%d]: i2c_client NULL!\n", __func__, __LINE__);
        return -EINVAL;
    }

	ret = i2c_smbus_write_byte_data(i2c_client, reg, val);
    if (ret) {
        pr_err("[%s:%d]: I2C write failed at reg 0x%02x: %d\n", __func__, __LINE__, reg, ret);
        return ret;
    }

    return ret;
}
EXPORT_SYMBOL_GPL(ws_panel_i2c_write);

int ws_panel_set_backlight(unsigned int value)
{
    int ret = 0;

    ret = ws_panel_i2c_write(0xab, 0xff - value);
    if (ret != 0)
    {
        pr_err("[%s:%d]: I2C write failed at reg 0xab: %d\n", __func__, __LINE__, ret);
        return ret;
    }

    ret = ws_panel_i2c_write(0xaa, 0x01);
    if (ret != 0)
    {
        pr_err("[%s:%d]: I2C write failed at reg 0xaa: %d\n", __func__, __LINE__, ret);
        return ret;
    }

    return ret;
}
EXPORT_SYMBOL_GPL(ws_panel_set_backlight);

// 更新背光状态的回调函数
static int ws_panel_bl_update_status(struct backlight_device *bl)
{
    struct ws_panel *ts = bl_get_data(bl);
    int brightness = backlight_get_brightness(bl);

    // 假设亮度值需要反转（0xff - brightness），根据具体硬件可能不同
    ws_panel_i2c_write(0xab, 0xff - brightness);
    ws_panel_i2c_write(0xaa, 0x01);

    return 0;
}

static const struct backlight_ops ws_panel_bl_ops = {
    .update_status = ws_panel_bl_update_status,
};

// 在探测函数中调用此函数来注册背光设备
static struct backlight_device *ws_panel_create_backlight(struct ws_panel *ts)
{
    struct device *dev = &ts->i2c->dev;
    const struct backlight_properties props = {
        .type = BACKLIGHT_RAW,
        .brightness = 255, // 默认亮度
        .max_brightness = 255, // 最大亮度
    };

    // 使用devm_backlight_device_register注册背光设备
    return devm_backlight_device_register(dev, dev_name(dev), dev, ts,
                                          &ws_panel_bl_ops, &props);
}

void ws_panel_switch_mipi(bool switch_ws)
{
    if (switch_gpio_t) {
	if (switch_ws) {
            gpiod_set_value_cansleep(switch_gpio_t, 0);
	} else {
	    gpiod_set_value_cansleep(switch_gpio_t, 1);
	}
    }
}
EXPORT_SYMBOL_GPL(ws_panel_switch_mipi);

static int ws_panel_probe(struct i2c_client *i2c)
{
    int ret;
    struct device *dev = &i2c->dev;
    struct ws_panel *ts;
    i2c_client = i2c;

    pr_info("[%s:%d]: waveshare_8.0inch-panel probe start\n", __func__, __LINE__);

    ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
    if (!ts) {
        pr_err("%s: Failed to allocate memory for ws_panel\n", __func__);
        return -ENOMEM;
    }

    ts->i2c = i2c;

    ts->switch_gpio = devm_gpiod_get_optional(dev, "switch", GPIOD_OUT_LOW);
    if (IS_ERR(ts->switch_gpio)) {
	pr_err("failed to acquire switch gpio\n");
	return PTR_ERR(ts->switch_gpio);
    }
    switch_gpio_t = ts->switch_gpio;

    /* enable vddio_i2c regulator */
    ts->vddio_i2c = regulator_get(dev, "vddio_i2c");
    if (IS_ERR(ts->vddio_i2c)) {
        pr_err("%s: Failed to get vddio_i2c regulator\n", __func__);
        return PTR_ERR(ts->vddio_i2c);
    }

    ret = regulator_enable(ts->vddio_i2c);
    if (ret) {
        pr_err("%s: Failed to enable vddio_i2c regulator, ret=%d\n", __func__, ret);
        regulator_put(ts->vddio_i2c);
        return ret;
    }

#if 0
    /* 注册背光设备 */
    ts->bl_dev = ws_panel_create_backlight(ts);
    if (IS_ERR(ts->bl_dev)) {
        return PTR_ERR(ts->bl_dev);
    }
#endif

    pr_info("[%s:%d]: waveshare_8.0inch-panel probe end\n", __func__, __LINE__);

    return 0;
}

static void ws_panel_remove(struct i2c_client *i2c)
{
    struct ws_panel *ts = i2c_get_clientdata(i2c);

    // 释放背光设备
    // backlight_device_unregister(ts->bl_dev);

    // 其他的清理代码...

}

static const struct of_device_id ws_panel_of_ids[] = {
    {
		.compatible = "waveshare,8.0inch-panel",
    },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ws_panel_of_ids);

static struct i2c_driver ws_panel_driver = {
	.driver = {
		.name = "ws_touchscreen",
		.of_match_table = ws_panel_of_ids,
	},
	.probe = ws_panel_probe,
	.remove = ws_panel_remove,
};
module_i2c_driver(ws_panel_driver);

MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.com>");
MODULE_DESCRIPTION("Waveshare DSI panel driver");
MODULE_LICENSE("GPL");
