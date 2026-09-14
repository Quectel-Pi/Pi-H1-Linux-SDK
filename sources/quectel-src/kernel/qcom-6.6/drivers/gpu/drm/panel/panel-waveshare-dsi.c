// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2023 Raspberry Pi Ltd
 *
 * Based on panel-raspberrypi-touchscreen by Broadcom
 */

 #include <linux/backlight.h>
 #include <linux/delay.h>
 #include <linux/err.h>
 #include <linux/fb.h>
 #include <linux/i2c.h>
 #include <linux/media-bus-format.h>
 #include <linux/module.h>
 #include <linux/of.h>
 #include <linux/of_device.h>
 #include <linux/of_graph.h>
 #include <linux/pm.h>
 
 #include <drm/drm_crtc.h>
 #include <drm/drm_device.h>
 #include <drm/drm_mipi_dsi.h>
 #include <drm/drm_panel.h>
 
 #include <linux/regulator/consumer.h>
 #include <linux/gpio/consumer.h>
 #include <linux/regulator/consumer.h>

 #define WS_DSI_DRIVER_NAME "ws-ts-dsi"
 
 struct ws_panel {
     struct drm_panel base;
     struct mipi_dsi_device *dsi;
     struct i2c_client *i2c;
     const struct drm_display_mode *mode;
     const struct ws_panel_data *data;
     enum drm_panel_orientation orientation;
     struct regulator *vddio_i2c;
     struct gpio_desc *switch_gpio;
     struct gpio_desc *power_gpio;
     struct gpio_desc *dsi_gpio;
 };

 struct ws_panel_data {
     const struct drm_display_mode *mode;
     int lanes;
     unsigned long mode_flags;
     int width_mm;   /* physical width in mm, 0 = use default */
     int height_mm;  /* physical height in mm, 0 = use default */
 };

 struct lcd_panel_info lcd_info = {
    .detected = -1,
 };
 EXPORT_SYMBOL(lcd_info);

 /* Panel ID definitions */
 enum ws_panel_id {
     WS_PANEL_2_8 = 1,
     WS_PANEL_3_4,
     WS_PANEL_4_0,
     WS_PANEL_7_0_C,
     WS_PANEL_7_9,
     WS_PANEL_8_0,
     WS_PANEL_10_1,
     WS_PANEL_11_9,
     WS_PANEL_4_0_C,
     WS_PANEL_5_0,
     WS_PANEL_6_25,
     WS_PANEL_8_8,
     WS_PANEL_13_3_4LANE,
     WS_PANEL_13_3_2LANE,
 };

 /* Touch configuration for each panel type */
 static const struct ws_touch_config {
     bool invert_x;
     bool invert_y;
     bool swap_xy;
 } ws_panel_touch_configs[] = {
     [WS_PANEL_2_8]      = { false, true, true },   /* 2.8inch: inverted-y, swapped-x-y */
     [WS_PANEL_3_4]      = { false, false, false }, /* 3.4inch: no transform */
     [WS_PANEL_4_0]      = { true, false, true },   /* 4.0inch: inverted-x, swapped-x-y */
     [WS_PANEL_7_0_C]    = { false, false, false }, /* 7.0inch C: no transform */
     [WS_PANEL_7_9]      = { true, false, true },   /* 7.9inch: inverted-x, swapped-x-y */
     [WS_PANEL_8_0]      = { true, false, true },   /* 8.0inch: inverted-x, swapped-x-y */
     [WS_PANEL_10_1]     = { true, false, true },   /* 10.1inch: inverted-x, swapped-x-y */
     [WS_PANEL_11_9]     = { true, false, true },   /* 11.9inch: inverted-x, swapped-x-y */
     [WS_PANEL_4_0_C]    = { false, false, false }, /* 4.0inch C: no transform */
     [WS_PANEL_5_0]      = { true, true, false },   /* 5.0inch: inverted-x, inverted-y */
     [WS_PANEL_6_25]     = { true, true, false },   /* 6.25inch: inverted-x, inverted-y */
     [WS_PANEL_8_8]      = { false, false, false }, /* 8.8inch: no transform */
     [WS_PANEL_13_3_4LANE] = { false, false, false }, /* 13.3inch 4lane: no touch (ILitek) */
     [WS_PANEL_13_3_2LANE] = { false, false, false }, /* 13.3inch 2lane: no touch (ILitek) */
 };

 /* 2.8inch 480x640
  * https://www.waveshare.com/product/raspberry-pi/displays/2.8inch-dsi-lcd.htm
  */
 static const struct drm_display_mode ws_panel_2_8_mode = {
     .clock = 50000,
     .hdisplay = 480,
     .hsync_start = 480 + 150,
     .hsync_end = 480 + 150 + 50,
     .htotal = 480 + 150 + 50 + 150,
     .vdisplay = 640,
     .vsync_start = 640 + 150,
     .vsync_end = 640 + 150 + 50,
     .vtotal = 640 + 150 + 50 + 150,
 };
 
 static const struct ws_panel_data ws_panel_2_8_data = {
     .mode = &ws_panel_2_8_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 /* 3.4inch 800x800 Round
  * https://www.waveshare.com/product/displays/lcd-oled/3.4inch-dsi-lcd-c.htm
  */
 static const struct drm_display_mode ws_panel_3_4_mode = {
     .clock = 50000,
     .hdisplay = 800,
     .hsync_start = 800 + 32,
     .hsync_end = 800 + 32 + 6,
     .htotal = 800 + 32 + 6 + 120,
     .vdisplay = 800,
     .vsync_start = 800 + 8,
     .vsync_end = 800 + 8 + 4,
     .vtotal = 800 + 8 + 4 + 16,
 };
 
 static const struct ws_panel_data ws_panel_3_4_data = {
     .mode = &ws_panel_3_4_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 /* 4.0inch 480x800
  * https://www.waveshare.com/product/raspberry-pi/displays/4inch-dsi-lcd.htm
  */
 static const struct drm_display_mode ws_panel_4_0_mode = {
     .clock = 50000,
     .hdisplay = 480,
     .hsync_start = 480 + 150,
     .hsync_end = 480 + 150 + 100,
     .htotal = 480 + 150 + 100 + 150,
     .vdisplay = 800,
     .vsync_start = 800 + 20,
     .vsync_end = 800 + 20 + 100,
     .vtotal = 800 + 20 + 100 + 20,
 };
 
 static const struct ws_panel_data ws_panel_4_0_data = {
     .mode = &ws_panel_4_0_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 /* 7.0inch C 1024x600
  * https://www.waveshare.com/product/raspberry-pi/displays/lcd-oled/7inch-dsi-lcd-c-with-case-a.htm
  */
 static const struct drm_display_mode ws_panel_7_0_c_mode = {
     .clock = 50000,
     .hdisplay = 1024,
     .hsync_start = 1024 + 100,
     .hsync_end = 1024 + 100 + 100,
     .htotal = 1024 + 100 + 100 + 100,
     .vdisplay = 600,
     .vsync_start = 600 + 10,
     .vsync_end = 600 + 10 + 10,
     .vtotal = 600 + 10 + 10 + 10,
 };
 
 static const struct ws_panel_data ws_panel_7_0_c_data = {
     .mode = &ws_panel_7_0_c_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 /* 7.9inch 400x1280
  * https://www.waveshare.com/product/raspberry-pi/displays/7.9inch-dsi-lcd.htm
  */
 static const struct drm_display_mode ws_panel_7_9_mode = {
     .clock = 50000,
     .hdisplay = 400,
     .hsync_start = 400 + 40,
     .hsync_end = 400 + 40 + 30,
     .htotal = 400 + 40 + 30 + 40,
     .vdisplay = 1280,
     .vsync_start = 1280 + 20,
     .vsync_end = 1280 + 20 + 10,
     .vtotal = 1280 + 20 + 10 + 20,
 };
 
 static const struct ws_panel_data ws_panel_7_9_data = {
     .mode = &ws_panel_7_9_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 /* 8.0inch or 10.1inch 1280x800
  * https://www.waveshare.com/product/raspberry-pi/displays/8inch-dsi-lcd-c.htm
  * https://www.waveshare.com/product/raspberry-pi/displays/10.1inch-dsi-lcd-c.htm
  */
 static const struct drm_display_mode ws_panel_10_1_mode = {
     .clock = 83333,
     .hdisplay = 1280,
     .hsync_start = 1280 + 156,
     .hsync_end = 1280 + 156 + 20,
     .htotal = 1280 + 156 + 20 + 40,
     .vdisplay = 800,
     .vsync_start = 800 + 40,
     .vsync_end = 800 + 40 + 48,
     .vtotal = 800 + 40 + 48 + 40,
 };
 
 static const struct ws_panel_data ws_panel_10_1_data = {
     .mode = &ws_panel_10_1_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };

 /*
  * 8.0inch 1280x800
  * Report a larger-than-real physical size (330x206mm) so that the
  * resulting PPI (~98) stays below the HiDPI threshold and GNOME keeps
  * the display at 100% scaling. 0/0 (unset) below falls back to the
  * legacy hardcoded 154x86mm in ws_panel_get_modes().
  */
 static const struct ws_panel_data ws_panel_8_0_data = {
     .mode = &ws_panel_10_1_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
     .width_mm = 330,
     .height_mm = 206,
 };
 
 /* 11.9inch 320x1480
  * https://www.waveshare.com/product/raspberry-pi/displays/11.9inch-dsi-lcd.htm
  */
 static const struct drm_display_mode ws_panel_11_9_mode = {
     .clock = 50000,
     .hdisplay = 320,
     .hsync_start = 320 + 60,
     .hsync_end = 320 + 60 + 60,
     .htotal = 320 + 60 + 60 + 60,
     .vdisplay = 1480,
     .vsync_start = 1480 + 60,
     .vsync_end = 1480 + 60 + 60,
     .vtotal = 1480 + 60 + 60 + 60,
 };
 
 static const struct ws_panel_data ws_panel_11_9_data = {
     .mode = &ws_panel_11_9_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 static const struct drm_display_mode ws_panel_4_mode = {
     .clock = 50000,
     .hdisplay = 720,
     .hsync_start = 720 + 32,
     .hsync_end = 720 + 32 + 200,
     .htotal = 720 + 32 + 200 + 120,
     .vdisplay = 720,
     .vsync_start = 720 + 8,
     .vsync_end = 720 + 8 + 4,
     .vtotal = 720 + 8 + 4 + 16,
 };
 
 static const struct ws_panel_data ws_panel_4_data = {
     .mode = &ws_panel_4_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 /* 5.0inch 720x1280
  * https://www.waveshare.com/5inch-dsi-lcd-d.htm
  */
 static const struct drm_display_mode ws_panel_5_0_mode = {
     .clock = 83333,
     .hdisplay = 720,
     .hsync_start = 720 + 100,
     .hsync_end = 720 + 100 + 80,
     .htotal = 720 + 100 + 80 + 100,
     .vdisplay = 1280,
     .vsync_start = 1280 + 20,
     .vsync_end = 1280 + 20 + 20,
     .vtotal = 1280 + 20 + 20 + 20,
 };
 
 static const struct ws_panel_data ws_panel_5_0_data = {
     .mode = &ws_panel_5_0_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 /* 6.25inch 720x1560
  * https://www.waveshare.com/6.25inch-dsi-lcd.htm
  */
 static const struct drm_display_mode ws_panel_6_25_mode = {
     .clock = 83333,
     .hdisplay = 720,
     .hsync_start = 720 + 50,
     .hsync_end = 720 + 50 + 50,
     .htotal = 720 + 50 + 50 + 50,
     .vdisplay = 1560,
     .vsync_start = 1560 + 20,
     .vsync_end = 1560 + 20 + 20,
     .vtotal = 1560 + 20 + 20 + 20,
 };
 
 static const struct ws_panel_data ws_panel_6_25_data = {
     .mode = &ws_panel_6_25_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 /* 8.8inch 480x1920
  * https://www.waveshare.com/8.8inch-dsi-lcd.htm
  */
 static const struct drm_display_mode ws_panel_8_8_mode = {
     .clock = 83333,
     .hdisplay = 480,
     .hsync_start = 480 + 50,
     .hsync_end = 480 + 50 + 50,
     .htotal = 480 + 50 + 50 + 50,
     .vdisplay = 1920,
     .vsync_start = 1920 + 20,
     .vsync_end = 1920 + 20 + 20,
     .vtotal = 1920 + 20 + 20 + 20,
 };
 
 static const struct ws_panel_data ws_panel_8_8_data = {
     .mode = &ws_panel_8_8_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS,
 };
 
 static const struct drm_display_mode ws_panel_13_3_4lane_mode = {
     .clock = 148500,
     .hdisplay = 1920,
     .hsync_start = 1920 + 88,
     .hsync_end = 1920 + 88 + 44,
     .htotal = 1920 + 88 + 44 + 148,
     .vdisplay = 1080,
     .vsync_start = 1080 + 4,
     .vsync_end = 1080 + 4 + 5,
     .vtotal = 1080 + 4 + 5 + 36,
 };
 
 static const struct ws_panel_data ws_panel_13_3_4lane_data = {
     .mode = &ws_panel_13_3_4lane_mode,
     .lanes = 4,
     .mode_flags = MIPI_DSI_MODE_VIDEO  | MIPI_DSI_MODE_LPM,
 };
 
 static const struct drm_display_mode ws_panel_13_3_2lane_mode = {
     .clock = 83333,
     .hdisplay = 1920,
     .hsync_start = 1920 + 88,
     .hsync_end = 1920 + 88 + 44,
     .htotal = 1920 + 88 + 44 + 148,
     .vdisplay = 1080,
     .vsync_start = 1080 + 4,
     .vsync_end = 1080 + 4 + 5,
     .vtotal = 1080 + 4 + 5 + 36,
 };
 
 static const struct ws_panel_data ws_panel_13_3_2lane_data = {
     .mode = &ws_panel_13_3_2lane_mode,
     .lanes = 2,
     .mode_flags = MIPI_DSI_MODE_VIDEO  | MIPI_DSI_MODE_LPM,
 };
 
 static struct ws_panel *panel_to_ts(struct drm_panel *panel)
 {
     return container_of(panel, struct ws_panel, base);
 }

 static int ws_panel_i2c_read(struct ws_panel *ts, u8 reg)
 {
     int ret;
 
     ret = i2c_smbus_read_byte_data(ts->i2c, reg);
     if (ret)
         dev_err(&ts->i2c->dev, "I2C read failed: %d\n", ret);
     return ret;
 }

 static int ws_panel_i2c_write(struct ws_panel *ts, u8 reg, u8 val)
 {
     int ret;
 
     ret = i2c_smbus_write_byte_data(ts->i2c, reg, val);
     if (ret)
         dev_err(&ts->i2c->dev, "I2C write failed: %d\n", ret);
     return ret;
 }
 
 static int ws_panel_disable(struct drm_panel *panel)
 {
     struct ws_panel *ts = panel_to_ts(panel);
 
     ws_panel_i2c_write(ts, 0xad, 0x00);
 
     return 0;
 }
 
 static int ws_panel_unprepare(struct drm_panel *panel)
 {
     return 0;
 }
 
 static int ws_panel_prepare(struct drm_panel *panel)
 {
     return 0;
 }
 
 static int ws_panel_enable(struct drm_panel *panel)
 {
     struct ws_panel *ts = panel_to_ts(panel);
 
     if (ts->mode == &ws_panel_13_3_2lane_mode)
         ws_panel_i2c_write(ts, 0xad, 0x02);
     else
         ws_panel_i2c_write(ts, 0xad, 0x01);
 
     return 0;
 }
 
 static int ws_panel_get_modes(struct drm_panel *panel,
                   struct drm_connector *connector)
 {
     static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
     struct ws_panel *ts = panel_to_ts(panel);
     struct drm_display_mode *mode;
 
     mode = drm_mode_duplicate(connector->dev, ts->mode);
     if (!mode) {
         dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
             ts->mode->hdisplay,
             ts->mode->vdisplay,
             drm_mode_vrefresh(ts->mode));
     }
 
     mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
 
     drm_mode_set_name(mode);
 
     drm_mode_probed_add(connector, mode);
 
     connector->display_info.bpc = 8;
     connector->display_info.width_mm = ts->data->width_mm ? ts->data->width_mm : 154;
     connector->display_info.height_mm = ts->data->height_mm ? ts->data->height_mm : 86;
     drm_display_info_set_bus_formats(&connector->display_info,
                      &bus_format, 1);
 
     /*
      * TODO: Remove once all drm drivers call
      * drm_connector_set_orientation_from_panel()
      */
     drm_connector_set_panel_orientation(connector, ts->orientation);
 
     return 1;
 }
 
 static enum drm_panel_orientation ws_panel_get_orientation(struct drm_panel *panel)
 {
     struct ws_panel *ts = panel_to_ts(panel);
 
     return ts->orientation;
 }
 
 static const struct drm_panel_funcs ws_panel_funcs = {
     .disable = ws_panel_disable,
     .unprepare = ws_panel_unprepare,
     .prepare = ws_panel_prepare,
     .enable = ws_panel_enable,
     .get_modes = ws_panel_get_modes,
     .get_orientation = ws_panel_get_orientation,
 };
 
 static int ws_panel_bl_update_status(struct backlight_device *bl)
 {
     struct ws_panel *ts = bl_get_data(bl);
 
     ws_panel_i2c_write(ts, 0xab, 0xff - backlight_get_brightness(bl));
     ws_panel_i2c_write(ts, 0xaa, 0x01);
 
     return 0;
 }
 
 static const struct backlight_ops ws_panel_bl_ops = {
     .update_status = ws_panel_bl_update_status,
 };
 
 static struct backlight_device *
 ws_panel_create_backlight(struct ws_panel *ts)
 {
     struct device *dev = ts->base.dev;
     const struct backlight_properties props = {
         .type = BACKLIGHT_RAW,
         .brightness = 255,
         .max_brightness = 255,
     };
 
     return devm_backlight_device_register(dev, dev_name(dev), dev, ts,
                           &ws_panel_bl_ops, &props);
 }
 
 static int ws_panel_probe(struct i2c_client *i2c)
 {
     struct device *dev = &i2c->dev;
     struct ws_panel *ts;
     struct device_node *endpoint, *dsi_host_node;
     struct mipi_dsi_host *host;
     struct mipi_dsi_device_info info = {
         .type = WS_DSI_DRIVER_NAME,
         .channel = 0,
         .node = NULL,
     };
     const struct ws_panel_data *_ws_panel_data;
     int ret;
     int data;
 
     ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
     if (!ts)
         return -ENOMEM;
 
     _ws_panel_data = of_device_get_match_data(dev);
     if (!_ws_panel_data)
         return -EINVAL;
 
     ts->mode = _ws_panel_data->mode;
     if (!ts->mode)
         return -EINVAL;
     ts->data = _ws_panel_data;

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

     /* enable gpio switch */
     ts->switch_gpio = devm_gpiod_get_optional(dev, "switch", GPIOD_OUT_LOW);
     if (IS_ERR(ts->switch_gpio)) {
        pr_err("failed to acquire switch gpio\n");
        return PTR_ERR(ts->switch_gpio);
     }
     
     ts->power_gpio = devm_gpiod_get_optional(dev, "power", GPIOD_OUT_HIGH);
     if (IS_ERR(ts->power_gpio)) {
         pr_err("failed to acquire switch gpio\n");
         return PTR_ERR(ts->power_gpio);
     }
     
     ts->dsi_gpio = devm_gpiod_get_optional(dev, "dsi", GPIOD_OUT_HIGH);
     if (IS_ERR(ts->dsi_gpio)) {
         pr_err("failed to acquire switch gpio\n");
         return PTR_ERR(ts->dsi_gpio);
     }

     //set route to HDMI for default
     gpiod_set_value_cansleep(ts->switch_gpio, 1);
     //power on panel
     gpiod_set_value_cansleep(ts->power_gpio, 1);
     gpiod_set_value_cansleep(ts->dsi_gpio, 1);
     udelay(800);

     i2c_set_clientdata(i2c, ts);
 
     ts->i2c = i2c;
     data = ws_panel_i2c_read(ts, 0x01);
     if (data < 0) {
        pr_info( "Didn't read REG_ID reg: %d maybe panel is not connected\n", data);
        lcd_info.detected = 2;    
        return -ENODEV;
     }
     pr_info( "panel detected number:%d\n",data);
 
     ws_panel_i2c_write(ts, 0xc0, 0x01);
     ws_panel_i2c_write(ts, 0xc2, 0x01);
     ws_panel_i2c_write(ts, 0xac, 0x01);
 
     ret = of_drm_get_panel_orientation(dev->of_node, &ts->orientation);
     if (ret) {
         dev_err(dev, "%pOF: failed to get orientation %d\n", dev->of_node, ret);
         return ret;
     }
 
     /* Look up the DSI host.  It needs to probe before we do. */
     endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
     if (!endpoint)
         return -ENODEV;
 
     dsi_host_node = of_graph_get_remote_port_parent(endpoint);
     if (!dsi_host_node)
         goto error;
 
     host = of_find_mipi_dsi_host_by_node(dsi_host_node);
     of_node_put(dsi_host_node);
     if (!host) {
         of_node_put(endpoint);
         return -EPROBE_DEFER;
     }
 
     info.node = of_graph_get_remote_port(endpoint);
     if (!info.node)
         goto error;
 
     of_node_put(endpoint);
 
     ts->dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
     if (IS_ERR(ts->dsi)) {
         dev_err(dev, "DSI device registration failed: %ld\n",
             PTR_ERR(ts->dsi));
         return PTR_ERR(ts->dsi);
     }
 
     drm_panel_init(&ts->base, dev, &ws_panel_funcs,
                DRM_MODE_CONNECTOR_DSI);
 
     ts->base.backlight = ws_panel_create_backlight(ts);
     if (IS_ERR(ts->base.backlight)) {
         ret = PTR_ERR(ts->base.backlight);
         dev_err(dev, "Failed to create backlight: %d\n", ret);
         return ret;
     }
 
     /* This appears last, as it's what will unblock the DSI host
      * driver's component bind function.
      */

     //panel detected now switch to LCM
     gpiod_set_value_cansleep(ts->switch_gpio, 0);
     udelay(800);

     drm_panel_add(&ts->base);
 
     ts->dsi->mode_flags = _ws_panel_data->mode_flags;
     ts->dsi->format = MIPI_DSI_FMT_RGB888;
     ts->dsi->lanes = _ws_panel_data->lanes;
 
     ret = devm_mipi_dsi_attach(dev, ts->dsi);
 
     if (ret)
         dev_err(dev, "failed to attach dsi to host: %d\n", ret);
 
     lcd_info.width = ts->mode->hdisplay;
     lcd_info.height = ts->mode->vdisplay;

     /* Set touch configuration based on panel resolution */
     lcd_info.invert_x = false;
     lcd_info.invert_y = false;
     lcd_info.swap_xy = false;

     /* 2.8inch 480x640: inverted-y, swapped-x-y */
     if (lcd_info.width == 480 && lcd_info.height == 640) {
         lcd_info.invert_y = true;
         lcd_info.swap_xy = true;
     }
     /* 3.4inch 800x800: no transform */
     /* 4.0inch 480x800: inverted-x, swapped-x-y */
     else if (lcd_info.width == 480 && lcd_info.height == 800) {
         lcd_info.invert_x = true;
         lcd_info.swap_xy = true;
     }
     /* 7.0inch C 1024x600: no transform */
     /* 7.9inch 400x1280: inverted-x, swapped-x-y */
     else if (lcd_info.width == 400 && lcd_info.height == 1280) {
         lcd_info.invert_x = true;
         lcd_info.swap_xy = true;
     }
     /* 8.0inch 800x1280: inverted-x, swapped-x-y */
     /* 10.1inch 1280x800: inverted-x, swapped-x-y */
     else if ((lcd_info.width == 800 && lcd_info.height == 1280) ||
              (lcd_info.width == 1280 && lcd_info.height == 800)) {
         lcd_info.invert_x = true;
         lcd_info.swap_xy = true;
     }
     /* 11.9inch 320x1480: inverted-x, swapped-x-y */
     else if (lcd_info.width == 320 && lcd_info.height == 1480) {
         lcd_info.invert_x = true;
         lcd_info.swap_xy = true;
     }
     /* 4.0inch C 720x720: no transform */
     /* 5.0inch 720x1280: inverted-x, inverted-y */
     else if (lcd_info.width == 720 && lcd_info.height == 1280) {
         lcd_info.invert_x = true;
         lcd_info.invert_y = true;
     }
     /* 6.25inch 720x1560: inverted-x, inverted-y */
     else if (lcd_info.width == 720 && lcd_info.height == 1560) {
         lcd_info.invert_x = true;
         lcd_info.invert_y = true;
     }
     /* 8.8inch 480x1920: no transform */
     /* 13.3inch: no transform (uses ILitek touch) */
     lcd_info.detected = 1;

     pr_info("LCD panel: %dx%d, touch: invert_x=%d invert_y=%d swap_xy=%d\n",
             lcd_info.width, lcd_info.height,
             lcd_info.invert_x, lcd_info.invert_y, lcd_info.swap_xy);   
     return 0;
 
 error:
     of_node_put(endpoint);                                                                            
     lcd_info.detected = 2;    
     return -ENODEV;
 }
 
 static void ws_panel_remove(struct i2c_client *i2c)
 {
     struct ws_panel *ts = i2c_get_clientdata(i2c);
 
     ws_panel_disable(&ts->base);
 
     drm_panel_remove(&ts->base);
 }
 
 static void ws_panel_shutdown(struct i2c_client *i2c)
 {
     struct ws_panel *ts = i2c_get_clientdata(i2c);
 
     ws_panel_disable(&ts->base);
 }
 
 static const struct of_device_id ws_panel_of_ids[] = {
     {
         .compatible = "waveshare,2.8inch-panel",
         .data = &ws_panel_2_8_data,
     }, {
         .compatible = "waveshare,3.4inch-panel",
         .data = &ws_panel_3_4_data,
     }, {
         .compatible = "waveshare,4.0inch-panel",
         .data = &ws_panel_4_0_data,
     }, {
         .compatible = "waveshare,7.0inch-c-panel",
         .data = &ws_panel_7_0_c_data,
     }, {
         .compatible = "waveshare,7.9inch-panel",
         .data = &ws_panel_7_9_data,
     }, {
         .compatible = "waveshare,8.0inch-panel",
         .data = &ws_panel_8_0_data,
     }, {
         .compatible = "waveshare,10.1inch-panel",
         .data = &ws_panel_10_1_data,
     }, {
         .compatible = "waveshare,11.9inch-panel",
         .data = &ws_panel_11_9_data,
     }, {
         .compatible = "waveshare,4inch-panel",
         .data = &ws_panel_4_data,
     }, {
         .compatible = "waveshare,5.0inch-panel",
         .data = &ws_panel_5_0_data,
     }, {
         .compatible = "waveshare,6.25inch-panel",
         .data = &ws_panel_6_25_data,
     }, {
         .compatible = "waveshare,8.8inch-panel",
         .data = &ws_panel_8_8_data,
     }, {
         .compatible = "waveshare,13.3inch-4lane-panel",
         .data = &ws_panel_13_3_4lane_data,
     }, {
         .compatible = "waveshare,13.3inch-2lane-panel",
         .data = &ws_panel_13_3_2lane_data,
     }, {
         /* sentinel */
     }
 };
 MODULE_DEVICE_TABLE(of, ws_panel_of_ids);
 
 static struct i2c_driver ws_panel_driver = {
     .driver = {
         .name = "ws_touchscreen",
         .of_match_table = ws_panel_of_ids,
     },
     .probe = ws_panel_probe,
     .remove = ws_panel_remove,
     .shutdown = ws_panel_shutdown,
 };
 module_i2c_driver(ws_panel_driver);
 
 MODULE_AUTHOR("Dave Stevenson <dave.stevenson@raspberrypi.com>");
 MODULE_DESCRIPTION("Waveshare DSI panel driver");
 MODULE_LICENSE("GPL");