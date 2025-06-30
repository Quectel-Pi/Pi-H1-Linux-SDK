#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/slab.h>

struct power_data {
    struct gpio_desc *wlan_en;
    struct gpio_desc *bt_en;
};

static ssize_t wlan_en_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
    int val;
    struct power_data *data = dev_get_drvdata(dev);

    if (!data || !data->wlan_en) {
        dev_warn(dev, "wlan_en GPIO not available\n");
        return -ENODEV;
    }

    if (kstrtoint(buf, 10, &val) == 0)
        gpiod_set_value(data->wlan_en, val ? 1 : 0);

    dev_info(dev, "wlan_en has been set to %d\n", val);
    return count;
}

static ssize_t bt_en_store(struct device *dev, struct device_attribute *attr,
                           const char *buf, size_t count)
{
    int val;
    struct power_data *data = dev_get_drvdata(dev);

    if (!data || !data->bt_en) {
        dev_warn(dev, "bt_en GPIO not available\n");
        return -ENODEV;
    }

    if (kstrtoint(buf, 10, &val) == 0)
        gpiod_set_value(data->bt_en, val ? 1 : 0);

    dev_info(dev, "bt_en has been set to %d\n", val);
    return count;
}

static DEVICE_ATTR_WO(wlan_en);
static DEVICE_ATTR_WO(bt_en);

static struct attribute *power_attrs[] = {
    &dev_attr_wlan_en.attr,
    &dev_attr_bt_en.attr,
    NULL,
};

static struct attribute_group power_attr_group = {
    .attrs = power_attrs,
};

static int power_probe(struct platform_device *pdev)
{
    struct power_data *data;
    int ret;

    dev_info(&pdev->dev, "quec RF power ctrl probe\n");

    data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->wlan_en = devm_gpiod_get_optional(&pdev->dev, "wlan_en", GPIOD_OUT_LOW);
    if (IS_ERR(data->wlan_en)) {
        dev_err(&pdev->dev, "Warning: failed to get wlan_en GPIO\n");
        data->wlan_en = NULL;
    }

    data->bt_en = devm_gpiod_get_optional(&pdev->dev, "bt_en", GPIOD_OUT_LOW);
    if (IS_ERR(data->bt_en)) {
        dev_err(&pdev->dev, "Warning: failed to get bt_en GPIO\n");
        data->bt_en = NULL;
    }

    platform_set_drvdata(pdev, data);

    ret = sysfs_create_group(&pdev->dev.kobj, &power_attr_group);
    if (ret) {
        dev_err(&pdev->dev, "failed to create sysfs group\n");
        return ret;
    }

    dev_info(&pdev->dev, "quec RF power ctrl probe finished\n");
    return 0;
}

static int power_remove(struct platform_device *pdev)
{
    struct power_data *data = platform_get_drvdata(pdev);
    dev_info(&pdev->dev, "quec RF power ctrl remove\n");
    sysfs_remove_group(&pdev->dev.kobj, &power_attr_group);

    return 0;
}

static const struct of_device_id power_of_match[] = {
    { .compatible = "quec,rfkill" },
    { },
};
MODULE_DEVICE_TABLE(of, power_of_match);

static struct platform_driver power_driver = {
    .driver = {
        .name = "rfkill",
        .of_match_table = power_of_match,
    },
    .probe = power_probe,
    .remove = power_remove,
};

module_platform_driver(power_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Igni.Li");
MODULE_DESCRIPTION("WLAN/BT power control via GPIO and sysfs");