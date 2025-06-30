#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/extcon-provider.h>
#include <linux/usb/role.h>
#include <linux/usb/typec_mux.h>

enum usb_mode {USB_MODE_NONE = 0, USB_MODE_SRC, USB_MODE_SNK, USB_MODE_ACCESSORY, USB_MODE_INIT};

struct sgm7220_info {
	struct typec_switch *typec_switch;
	struct usb_role_switch *usb_role_sw;
	struct i2c_client *client;

	struct gpio_desc *ccid_gpio;
	struct gpio_desc *usbint_gpio;
	int usbint_irq;

	enum usb_mode mode;

	unsigned long debounce_jiffies;
	struct delayed_work usbirq_work;
};

static int sgm7220_i2c_write(struct i2c_client *client,
			    uint8_t aregaddr,
			    uint8_t avalue)
{
	uint8_t data[2] = { aregaddr, avalue };
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = I2C_M_IGNORE_NAK,
		.len = 2,
		.buf = (uint8_t *)data
	};
	int error;

	error = i2c_transfer(client->adapter, &msg, 1);
	return error < 0 ? error : 0;
}

static int sgm7220_i2c_read(struct i2c_client *client,
			   uint8_t aregaddr, uint8_t *value)
{
	uint8_t data[2] = { aregaddr };
	struct i2c_msg msg_set[2] = {
		{
			.addr = client->addr,
			.flags = I2C_M_REV_DIR_ADDR,
			.len = 1,
			.buf = (uint8_t *)data
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD | I2C_M_NOSTART,
			.len = 1,
			.buf = (uint8_t *)data
		}
	};
	int error;

	error = i2c_transfer(client->adapter, msg_set, 2);
	if (error < 0)
		return error;
	*value = data[0];
	return 0;
}

static irqreturn_t sgm7220_intirq_handler(int irq, void *dev_id)
{
	struct sgm7220_info *sgm7220 = dev_id;

	schedule_delayed_work(&sgm7220->usbirq_work, sgm7220->debounce_jiffies);

	return IRQ_HANDLED;
}

static void sgm7220_usbint_delayed_work(struct work_struct *work)
{
	uint8_t value;
	struct sgm7220_info *sgm7220 = container_of(to_delayed_work(work),
						    struct sgm7220_info,
						    usbirq_work);
	enum usb_mode usb_mode;
	enum typec_orientation orientation = TYPEC_ORIENTATION_NONE;
	enum usb_role role = USB_ROLE_NONE;

	if (sgm7220_i2c_read(sgm7220->client, 0x09, &value) < 0)
		return;
	sgm7220_i2c_write(sgm7220->client, 0x09, value|0x10); //clear interrupt

	usb_mode = (enum usb_mode)((value >> 6) & 0x3);
	if (sgm7220->mode == usb_mode)
		return;
	sgm7220->mode = usb_mode;

	orientation = (value & BIT(5)) ? TYPEC_ORIENTATION_REVERSE : TYPEC_ORIENTATION_NORMAL;
	switch (sgm7220->mode)
	{
		case USB_MODE_SRC:
			role = USB_ROLE_HOST;
		break;
		case USB_MODE_SNK:
			role = USB_ROLE_DEVICE;
		break;
		case USB_MODE_ACCESSORY:
			role = USB_ROLE_NONE;
		break;
		case USB_MODE_NONE:
		default:
			role = USB_ROLE_NONE;
			orientation = TYPEC_ORIENTATION_NONE;
		break;
	}

	if (sgm7220->usb_role_sw)
		usb_role_switch_set_role(sgm7220->usb_role_sw, role);
	if (sgm7220->typec_switch)
		typec_switch_set(sgm7220->typec_switch, orientation);

	dev_info(&sgm7220->client->dev, "role=%d, orientation=%d\n", role, orientation);
}

static void sgm7220_usb_role_switch_put(void *data)
{
	usb_role_switch_put(data);
}

static void sgm7220_typec_switch_put(void *data)
{
	typec_switch_put(data);
}

static int sgm7220_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sgm7220_info *sgm7220;
	int ret, i, option_state;
	uint8_t value;
	const uint8_t device_id[] = {0x30, 0x32, 0x33, 0x42, 0x53, 0x55, 0x54, 0x00};

	struct gpio_desc *option_gpio = devm_gpiod_get_optional(dev, "quec,option", GPIOD_IN);
	if (!IS_ERR_OR_NULL(option_gpio)) {
		option_state = gpiod_get_value(option_gpio);
		dev_info(dev, "get option-gpio value %d\n", option_state);
		devm_gpiod_put(dev, option_gpio);
	}

	for (i = 0; i < ARRAY_SIZE(device_id); i++) {
		ret = sgm7220_i2c_read(client, i, &value);
		if (ret || value != device_id[i]) {
			dev_err(&client->dev, "sgm7220[%d] = 0x%02x ret %d\n", i, value, ret);
			return -ENODEV;
		}
	}

	sgm7220 = devm_kzalloc(&client->dev, sizeof(struct sgm7220_info), GFP_KERNEL);
	if (!sgm7220) {
		return -ENOMEM;
	}

	sgm7220->client = client;
	sgm7220->mode = USB_MODE_INIT;
	sgm7220->debounce_jiffies = msecs_to_jiffies(100);
	INIT_DELAYED_WORK(&sgm7220->usbirq_work, sgm7220_usbint_delayed_work);

	if (option_state) {
		sgm7220->typec_switch = typec_switch_get(dev);
		if (IS_ERR(sgm7220->typec_switch)) {
			return dev_err_probe(dev, PTR_ERR(sgm7220->typec_switch),
					     "failed to acquire orientation-switch\n");
		}
		devm_add_action_or_reset(dev, sgm7220_typec_switch_put,
					       sgm7220->typec_switch);

		sgm7220->usb_role_sw = usb_role_switch_get(dev);
		if (IS_ERR(sgm7220->usb_role_sw)) {
			return dev_err_probe(dev, PTR_ERR(sgm7220->usb_role_sw),
					     "failed to acquire role-switch\n");
		}
		devm_add_action_or_reset(dev, sgm7220_usb_role_switch_put,
					       sgm7220->usb_role_sw);
	}

	if (!sgm7220->typec_switch && !sgm7220->usb_role_sw) {
		if (!sgm7220_i2c_read(client, 0x45, &value)) {
			//sgm7220_i2c_write(client, 0x45, value|BIT(2)); //DISABLE_RD_RP
		}
	}

//active low interrupt signal for indicating changes in I2C registers.
	sgm7220->usbint_gpio = devm_gpiod_get(dev, "sgm7220,int", GPIOD_IN);
	if (IS_ERR(sgm7220->usbint_gpio)) {
		return dev_err_probe(dev, PTR_ERR(sgm7220->usbint_gpio),
					     "failed to acquire int-gpio\n");
	}

	sgm7220->usbint_irq = gpiod_to_irq(sgm7220->usbint_gpio);
	if (sgm7220->usbint_irq < 0) {
		return dev_err_probe(dev, sgm7220->usbint_irq,
					"failed to acquire int-irq\n");
	}

	ret = devm_request_threaded_irq(dev, sgm7220->usbint_irq, NULL, sgm7220_intirq_handler,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "sgm7220_intirq", sgm7220);
	if (ret < 0) {
		return dev_err_probe(dev, ret,
					"failed to request int-irq\n");
	}

//Asserted low when the CC pins detect device attachment when port is  a source (DFP), or dual role (DRP) acting as source (DFP).
	sgm7220->ccid_gpio = devm_gpiod_get_optional(dev, "sgm7220,cc-id", GPIOD_IN);
	if (IS_ERR(sgm7220->ccid_gpio)) {
		dev_err_probe(dev, PTR_ERR(sgm7220->ccid_gpio),
			"get ccid-gpio failed\n");
	}

	dev_set_drvdata(dev, sgm7220);
	device_init_wakeup(dev, true);

	enable_irq_wake(sgm7220->usbint_irq);
	schedule_delayed_work(&sgm7220->usbirq_work, 0);

	return 0;
}

static void sgm7220_remove(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sgm7220_info *sgm7220 = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&sgm7220->usbirq_work);
	device_init_wakeup(dev, false);
}

static struct i2c_device_id sgm7220_id[] = {
	{ "quec,sgm7220", 0 },
	{ /* sentinel */ }
};

static const struct of_device_id sgm7220_dt_match[] = {
	{ .compatible = "quec,sgm7220", },
	{ }
};

MODULE_DEVICE_TABLE(of, sgm7220_dt_match);

static struct i2c_driver sgm7220_driver = {
	.driver = {
		.name = "quec,sgm7220",
		.of_match_table = sgm7220_dt_match,
	},
	.probe = sgm7220_probe,
	.remove	= sgm7220_remove,
	.id_table = sgm7220_id,
};

static int sgm7220_init(void)
{
	return i2c_add_driver(&sgm7220_driver);
}

static void sgm7220_exit(void)
{
	i2c_del_driver(&sgm7220_driver);
}

module_init(sgm7220_init);
module_exit(sgm7220_exit);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("quectel");
MODULE_DESCRIPTION("usb host/device detect driver at 2022/06/29");
