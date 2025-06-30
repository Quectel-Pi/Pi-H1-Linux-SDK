// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2023 Raspberry Pi Ltd
 *
 * Based on panel-raspberrypi-touchscreen by Broadcom
 */

#ifndef _LATTICE_I2C_H_
#define _LATTICE_I2C_H_

struct WriteCommand {
    __u8 address;
    __u8 data;
};

extern int ws_panel_i2c_read(u8 reg);
extern int ws_panel_i2c_write(u8 reg, u8 val);
extern int ws_panel_set_backlight(unsigned int value);
extern void ws_panel_switch_mipi(bool switch_ws);

#endif
