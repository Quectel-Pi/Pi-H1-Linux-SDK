// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved. */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pci.h>

#define DRV_NAME		"qps615-switch-i2c"

struct pcie_switch_i2c_setting {
	u32 slv_addr;
	u32 reg_addr;
	u32 val;
};

struct qps615_switch_i2c {
	struct i2c_client *client;
	struct regulator *vdda;
	u8 *fw_data;
	size_t size;
};

static const struct of_device_id qps615_switch_of_match[] = {
	{.compatible = "qcom,switch-i2c" },
	{ },
};
MODULE_DEVICE_TABLE(of, qps615_switch_of_match);

/* write 32-bit value to 24 bit register */
static int qps615_switch_i2c_write(struct i2c_client *client, u32 slv_addr, u32 reg_addr,
				   u32 reg_val)
{
	struct i2c_msg msg;
	u8 msg_buf[7];
	int ret;

	msg.addr = slv_addr;
	msg.len = 7;
	msg.flags = 0;

	/* Big Endian for reg addr */
	msg_buf[0] = (u8)(reg_addr >> 16);
	msg_buf[1] = (u8)(reg_addr >> 8);
	msg_buf[2] = (u8)reg_addr;

	/* Little Endian for reg val */
	msg_buf[3] = (u8)(reg_val);
	msg_buf[4] = (u8)(reg_val >> 8);
	msg_buf[5] = (u8)(reg_val >> 16);
	msg_buf[6] = (u8)(reg_val >> 24);

	msg.buf = msg_buf;
	ret = i2c_transfer(client->adapter, &msg, 1);
	return ret == 1 ? 0 : ret;
}

/* read 32 bit value from 24 bit reg addr */
static int qps615_switch_i2c_read(struct i2c_client *client, u32 slv_addr, u32 reg_addr,
				  u32 *reg_val)
{
	u8 wr_data[3], rd_data[4] = {0};
	struct i2c_msg msg[2];
	int ret;

	msg[0].addr = slv_addr;
	msg[0].len = 3;
	msg[0].flags = 0;

	/* Big Endian for reg addr */
	wr_data[0] = (u8)(reg_addr >> 16);
	wr_data[1] = (u8)(reg_addr >> 8);
	wr_data[2] = (u8)reg_addr;

	msg[0].buf = wr_data;

	msg[1].addr = slv_addr;
	msg[1].len = 4;
	msg[1].flags = I2C_M_RD;

	msg[1].buf = rd_data;

	ret = i2c_transfer(client->adapter, &msg[0], 2);
	if (ret != 2)
		return ret;

	*reg_val = (rd_data[3] << 24) | (rd_data[2] << 16) | (rd_data[1] << 8) | rd_data[0];

	return 0;
}

/*
 * QPS615 switch uses i2c interface to configure its internal registers.
 * The sequence of register writes though i2c is requested through
 * request_firmware API. This firmware bin is parsed and i2c writes
 * are performed to initialize the QPS615 switch.
 */
int qps615_switch_init(struct i2c_client *client, struct qps615_switch_i2c *qps615)
{
	struct pcie_switch_i2c_setting *set;
	const u8 *pos, *eof;
	int ret = 0;
	u32 val;

	if (!client || !qps615)
		return -EINVAL;

	if (qps615->size % sizeof(struct pcie_switch_i2c_setting) != 0) {
		dev_err(&client->dev, "Invalid firmware format: size %zu is not a multiple of %zu\n",
			qps615->size, sizeof(struct pcie_switch_i2c_setting));
		return -EINVAL;
	}

	if (qps615->fw_data && qps615->size) {
		pos = qps615->fw_data;
		eof = qps615->fw_data + qps615->size;
	} else {
		dev_err(&client->dev, "No FW data found, continue without I2C configs\n");
		return 0;
	}

	while (pos < (qps615->fw_data + qps615->size)) {
		set = (struct pcie_switch_i2c_setting *)pos;

		ret = qps615_switch_i2c_write(client, set->slv_addr, set->reg_addr, set->val);
		if (ret) {
			dev_err(&client->dev,
				"I2c write failed for slv addr:%x at addr%x with val %x ret %d\n",
				set->slv_addr, set->reg_addr, set->val, ret);
			goto err;
		}

		ret = qps615_switch_i2c_read(client,  set->slv_addr, set->reg_addr, &val);
		if (ret) {
			dev_err(&client->dev, "I2c read failed for slv addr:%x at addr%x ret %d\n",
				set->slv_addr, set->reg_addr, ret);
			goto err;
		}

		if (set->val != val) {
			dev_err(&client->dev,
				"I2c read's mismatch for slv:%x at addr%x exp%d got%d\n",
				set->slv_addr, set->reg_addr, set->val, val);
			goto err;
		}
		pos += sizeof(struct pcie_switch_i2c_setting);
	}

err:

	return ret;
}

static void qps615_power_on(struct i2c_client *client)
{
	struct qps615_switch_i2c *qps615 = i2c_get_clientdata(client);
	int ret;

	ret = regulator_enable(qps615->vdda);
	if (ret) {
		dev_err(&client->dev, "cannot enable vdda regulator\n");
		return;
	}

	ret = qps615_switch_init(client, qps615);
	if (ret)
		dev_err(&client->dev, "switch initialization failed: %d\n", ret);
}

static int qps615_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct qps615_switch_i2c *qps615 = i2c_get_clientdata(client);

	/* disable power of qps615 switch */
	regulator_disable(qps615->vdda);

	return 0;
}

static int qps615_resume_noirq(struct device *dev)
{
	qps615_power_on(to_i2c_client(dev));

	return 0;
}

static int qps615_switch_probe(struct i2c_client *client)
{
	struct qps615_switch_i2c *qps615;
	const struct firmware *fw;
	int ret;

	qps615 = devm_kzalloc(&client->dev, sizeof(*qps615), GFP_KERNEL);
	if (!qps615)
		return -ENOMEM;

	qps615->client = client;

	i2c_set_clientdata(client, qps615);

	qps615->vdda = devm_regulator_get(&client->dev, "vdda");
	if (IS_ERR(qps615->vdda)) {
		dev_err(&client->dev, "Failed to get vdda regulator\n");
		return PTR_ERR(qps615->vdda);
	}

	ret = regulator_enable(qps615->vdda);
	if (ret) {
		dev_err(&client->dev, "cannot enable vdda regulator\n");
		return ret;
	}

	ret = request_firmware(&fw, "qcom/qps615.bin", &client->dev);
	if (ret < 0) {
		dev_err(&client->dev, "firmware loading failed with ret %d, continue without it\n",
			ret);
		goto skip_firmware;
	}

	qps615->fw_data = devm_kzalloc(&client->dev, fw->size, GFP_KERNEL);
	if (!qps615->fw_data) {
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(qps615->fw_data, fw->data, fw->size);
	qps615->size = fw->size;

	release_firmware(fw);

	/*
	 * Do not fail the probe if qps615_switch_init() fails.
	 * The switch remains functional even without I2C writes.
	 */
	qps615_switch_init(client, qps615);
skip_firmware:
	return 0;
}

static const struct i2c_device_id qps615_switch_id[] = {
	{DRV_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, qps615_switch_id);

static const struct dev_pm_ops qps615_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(qps615_suspend_noirq, qps615_resume_noirq)
};
static struct i2c_driver qps615_switch_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &qps615_pm_ops,
		.of_match_table = qps615_switch_of_match,
	},
	.probe = qps615_switch_probe,
	.id_table = qps615_switch_id,
};

static __init int qps615_i2c_init(void)
{
	int ret = -ENODEV;

	ret = i2c_add_driver(&qps615_switch_driver);
	if (ret)
		pr_err("qps615 driver failed to register with i2c framework %d\n", ret);

	return ret;
}
module_init(qps615_i2c_init);

static void qcom_pcie_resume_early(struct pci_dev *pdev)
{
	pci_restore_state(pdev);
}

DECLARE_PCI_FIXUP_CLASS_RESUME_EARLY(PCI_ANY_ID,
				PCI_ANY_ID, PCI_CLASS_BRIDGE_PCI_NORMAL, 0,
				qcom_pcie_resume_early);

static void qcom_pcie_suspend_late(struct pci_dev *pdev)
{
	pci_save_state(pdev);
}

DECLARE_PCI_FIXUP_CLASS_SUSPEND_LATE(PCI_ANY_ID,
				PCI_ANY_ID, PCI_CLASS_BRIDGE_PCI_NORMAL, 0,
				qcom_pcie_suspend_late);

MODULE_AUTHOR("Krishna Chaitanya Chundru <quic_krichai@quicinc.com>");
MODULE_DESCRIPTION("QPS615 PCIE Switch driver");
MODULE_LICENSE("GPL");
