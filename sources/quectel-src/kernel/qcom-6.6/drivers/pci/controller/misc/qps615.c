// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pci.h>
#if 1 //Add by Quectel
#include <linux/delay.h>
#include <linux/debugfs.h>
#endif

#define DRV_NAME		"qps615-switch-i2c"

struct pcie_switch_i2c_setting {
	u32 slv_addr;
	u32 reg_addr;
	u32 val;
};

#if 1 //Add by Quectel
struct pcie_i2c_reg_update {
	u32 offset;
	u32 val;
};
#endif

struct qps615_switch_i2c {
	struct i2c_client *client;
	struct regulator *vdda;

#if 1 //Add by Quectel
	struct regulator_bulk_data supplies[2];

	/* client specific register info */
	u32 gpio_config_reg;
	u32 ep_reset_reg;
	u32 ep_reset_gpio_mask;
	u32 ep_reset_ms;
	u32 *dump_regs;
	u32 dump_reg_count;
	struct pcie_i2c_reg_update *reg_update;
	u32 reg_update_count;

	/* client specific callbacks */
	int (*client_i2c_read)(struct i2c_client *client, u32 reg_addr,
			       u32 *val);
	int (*client_i2c_write)(struct i2c_client *client, u32 reg_addr,
				u32 val);
#endif
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

#if 1 //Add by Quectel
/* write 32-bit value to 24 bit register */
static int ntn3_i2c_write(struct i2c_client *client, u32 reg_addr, u32 reg_val)
{
	int ret;

	ret = qps615_switch_i2c_write(client, 0x77, reg_addr, reg_val);
	if (ret) {
		dev_err(&client->dev,
			"qps615 I2c write failed for slv addr:%x at addr:%x with val %x ret %d\n",
			client->addr, reg_addr, reg_val, ret);
	}
	return ret;
}

/* read 32 bit value from 24 bit reg addr */
static int ntn3_i2c_read(struct i2c_client *client, u32 reg_addr, u32 *reg_val)
{
	int ret;

	ret = qps615_switch_i2c_read(client, 0x77, reg_addr, reg_val);
	if (ret) {
		dev_err(&client->dev,
			"qps615 I2c read failed for slv addr:%x at addr:%x with val %x ret %d\n",
			client->addr, reg_addr, *reg_val, ret);
	}
	return ret;
}

static int ntn3_dump_regs(struct seq_file *s, void *data)
{
	struct qps615_switch_i2c *i2c_ctrl = (struct qps615_switch_i2c *)dev_get_drvdata(s->private);
	int i, val;

	if (!i2c_ctrl->client_i2c_read || !i2c_ctrl->dump_reg_count)
		return 0;

	for (i = 0; i < i2c_ctrl->dump_reg_count; i++) {
		if (i2c_ctrl->client_i2c_read(i2c_ctrl->client,
					  i2c_ctrl->dump_regs[i], &val))
			break;
		seq_printf(s, "%08x %08x\n", i2c_ctrl->dump_regs[i], val);
	}

	return 0;
}

static int qps615_gpio_set_value(struct i2c_client *client, int gpio_mask, int value) {
	int ret, rd_val, wr_val;
	struct qps615_switch_i2c *i2c_ctrl = i2c_get_clientdata(client);

	/* set NTN3 GPIO as output */
	ret = i2c_ctrl->client_i2c_read(client, i2c_ctrl->gpio_config_reg, &rd_val);
	if (ret) {
		return ret;
	}

	wr_val = rd_val & ~gpio_mask;
	if (wr_val != rd_val) {
		i2c_ctrl->client_i2c_write(client, i2c_ctrl->gpio_config_reg, wr_val);

		/* read back to flush write - config gpio */
		ret = i2c_ctrl->client_i2c_read(client, i2c_ctrl->gpio_config_reg, &rd_val);
		if (ret) {
			return ret;
		}
	}

	ret = i2c_ctrl->client_i2c_read(client, i2c_ctrl->ep_reset_reg, &rd_val);
	if (ret) {
		return ret;
	}

	if (value)
		wr_val = rd_val | gpio_mask;
	else
		wr_val = rd_val & ~gpio_mask;
	if (wr_val != rd_val) {
		ret = i2c_ctrl->client_i2c_write(client, i2c_ctrl->ep_reset_reg, wr_val);

		/* read back to flush write - reset gpio */
		ret = i2c_ctrl->client_i2c_read(client, i2c_ctrl->ep_reset_reg, &rd_val);
	}

	return ret;
}

int qps615_switch_init_from_pcie(struct i2c_client *client) {
	int i, val, ret;
	struct qps615_switch_i2c *i2c_ctrl = i2c_get_clientdata(client);

	for (i = 0; i < i2c_ctrl->reg_update_count; i++) {
		val = i2c_ctrl->reg_update[i].val;
		if (i2c_ctrl->client_i2c_write(client, i2c_ctrl->reg_update[i].offset, val))
			break;
		/*Read to make sure writes are completed*/
		if (i2c_ctrl->client_i2c_read(client, i2c_ctrl->reg_update[i].offset, &val))
			break;
	}

	qps615_gpio_set_value(client, i2c_ctrl->ep_reset_gpio_mask, 0);

	ret = regulator_bulk_enable(ARRAY_SIZE(i2c_ctrl->supplies), i2c_ctrl->supplies);
	if (ret < 0) {
		dev_err(&client->dev, "cannot enable regulators\n");
		return ret;
	}
	msleep(i2c_ctrl->ep_reset_ms);

	qps615_gpio_set_value(client, i2c_ctrl->ep_reset_gpio_mask, 1);

	return ret;
}
#endif

/*
 * QPS615 switch uses i2c interface to configure its internal registers.
 * The sequence of register writes though i2c is requested through
 * request_firmware API. This firmware bin is parsed and i2c writes
 * are performed to initialize the QPS615 switch.
 */
int qps615_switch_init(struct i2c_client *client)
{
	const struct firmware *fw;
	struct pcie_switch_i2c_setting *set;
	int ret;
	u32 val;
	const u8 *pos, *eof;

	if (!client)
		return 0;

	ret = request_firmware(&fw, "qcom/qps615.bin", &client->dev);
	if (ret < 0) {
		dev_err(&client->dev, "firmware loading failed with ret %d\n", ret);
		return ret;
	}

	if (!fw) {
		ret = -EINVAL;
		goto err;
	}

	pos = fw->data;
	eof = fw->data + fw->size;

	while (pos < (fw->data + fw->size)) {
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
	release_firmware(fw);

	return ret;
}

static void qps615_power_on(struct i2c_client *client)
{
	struct qps615_switch_i2c *qps615 = i2c_get_clientdata(client);
	int ret;

#if 1 //Add by Quectel
	if (!qps615->vdda)
		return;
#endif

	ret = regulator_enable(qps615->vdda);
	if (ret)
		dev_err(&client->dev, "cannot enable vdda regulator\n");

	qps615_switch_init(client);
}

static int qps615_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct qps615_switch_i2c *qps615 = i2c_get_clientdata(client);

#if 1 //Add by Quectel
	if (!qps615->vdda)
		return 0;
#endif

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
	int ret;
#if 1 //Add by Quectel
	int size;
#endif

	qps615 = devm_kzalloc(&client->dev, sizeof(*qps615), GFP_KERNEL);
	if (!qps615)
		return -ENOMEM;

	qps615->client = client;

	i2c_set_clientdata(client, qps615);

#if 1 //Add by Quectel
	qps615->supplies[0].supply = "vddpe0";
	qps615->supplies[1].supply = "vddpe1";
	ret = devm_regulator_bulk_get(&client->dev, ARRAY_SIZE(qps615->supplies),
				      qps615->supplies);
	if (ret) {
		dev_err(&client->dev, "cannot get  vddpe regulator\n");
		return ret;
	}

	of_property_read_u32(client->dev.of_node, "gpio-config-reg",
			     &qps615->gpio_config_reg);
	of_property_read_u32(client->dev.of_node, "ep-reset-reg",
			     &qps615->ep_reset_reg);
	of_property_read_u32(client->dev.of_node, "ep-reset-gpio-mask",
			     &qps615->ep_reset_gpio_mask);
	of_property_read_u32(client->dev.of_node, "ep-reset-ms",
			     &qps615->ep_reset_ms);
	if (of_get_property(client->dev.of_node, "dump-regs", &size)) {
		qps615->dump_regs = devm_kzalloc(&client->dev, size, GFP_KERNEL);
		if (!qps615->dump_regs)
			return -ENOMEM;

		qps615->dump_reg_count = size / sizeof(*qps615->dump_regs);

		ret = of_property_read_u32_array(client->dev.of_node, "dump-regs",
						 qps615->dump_regs,
						 qps615->dump_reg_count);
		if (ret)
			qps615->dump_reg_count = 0;
	}
	if (of_get_property(client->dev.of_node, "reg_update", &size)) {
		qps615->reg_update = devm_kzalloc(&client->dev, size, GFP_KERNEL);
		if (!qps615->reg_update)
			return -ENOMEM;

		qps615->reg_update_count = size / sizeof(*qps615->reg_update);

		ret = of_property_read_u32_array(client->dev.of_node,
						"reg_update",
						(unsigned int *)qps615->reg_update,
						size/sizeof(qps615->reg_update->offset));
		if (ret)
			qps615->reg_update_count = 0;
	}
	qps615->client_i2c_read = ntn3_i2c_read;
	qps615->client_i2c_write = ntn3_i2c_write;
	if (qps615->dump_reg_count) {
		debugfs_create_devm_seqfile(&client->dev, "qps615_reg_dump", NULL, ntn3_dump_regs);
	}

	if (!of_get_child_by_name(client->dev.of_node, "vdda-supply"))
		return 0;
#endif

	qps615->vdda = devm_regulator_get(&client->dev, "vdda");

	ret = regulator_enable(qps615->vdda);
	if (ret)
		dev_err(&client->dev, "cannot enable vdda regulator\n");

	qps615_switch_init(client);
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
