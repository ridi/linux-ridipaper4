/*
 * ADXL344 Accelerometer input driver
 *
 * Copyright (C) 2021 E-ROUM Co., Ltd.
 * Author: Terry Choi <dw.choi@e-roum.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/input.h>

#define ABS_ACCELEROMETER_X   0x30
#define ABS_ACCELEROMETER_Y   0x31
#define ABS_ACCELEROMETER_Z   0x32


struct adxl344_input_data {
	struct input_dev *input;
	struct mutex lock;
	struct delayed_work input_work;
	int x;
	int y;
	int z;
	int enable;
	int poll_interval;
	int debug;
};

extern int adxl344_read_xyz(int* x, int* y, int* z);
extern int is_adxl344_probe_done(void);

static int adxl344_input_open(struct input_dev *input)
{
	return 0;
}

static void adxl344_input_close(struct input_dev *input)
{
	
}

static void adxl344_input_report_values(struct adxl344_input_data *data)
{
	input_report_abs(data->input, ABS_ACCELEROMETER_X, data->x);
	input_report_abs(data->input, ABS_ACCELEROMETER_Y, data->y);
	input_report_abs(data->input, ABS_ACCELEROMETER_Z, data->z);
	input_sync(data->input);

	if(data->debug)
		printk(KERN_ERR "%s %d %d %d\n", __func__, data->x, data->y, data->z);
}

static void adxl344_input_work_func(struct work_struct *work)
{
	struct adxl344_input_data *data = container_of((struct delayed_work *)work, struct adxl344_input_data, input_work);
	int ret;

	mutex_lock(&data->lock);
	ret = adxl344_read_xyz(&data->x, &data->y, &data->z);
	mutex_unlock(&data->lock);
	if (ret == 0) {
		adxl344_input_report_values(data);
	}

	schedule_delayed_work(&data->input_work, msecs_to_jiffies(data->poll_interval));
}

static ssize_t adxl344_input_show_enable(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct adxl344_input_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->enable);
}

static ssize_t adxl344_input_store_enable(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct adxl344_input_data *data = dev_get_drvdata(dev);

	if(!is_adxl344_probe_done())
		return len;

	sscanf(&buf[0], "%d", &data->enable);
	if(data->enable) {
		schedule_delayed_work(&data->input_work, msecs_to_jiffies(data->poll_interval));
	} else {
		cancel_delayed_work_sync(&data->input_work);
	}

	return len;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, adxl344_input_show_enable, adxl344_input_store_enable);


static ssize_t adxl344_input_show_poll_delay(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct adxl344_input_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->poll_interval);
}

static ssize_t adxl344_input_store_poll_delay(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct adxl344_input_data *data = dev_get_drvdata(dev);
	int64_t poll_delay = 0;

	sscanf(&buf[0], "%lld", &poll_delay);
	printk("poll_delay: %lld ns\n", poll_delay);
	data->poll_interval = poll_delay/1000000;

	return len;
}

static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR, adxl344_input_show_poll_delay, adxl344_input_store_poll_delay);


static ssize_t adxl344_input_show_data(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct adxl344_input_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d %d %d\n", data->x, data->y, data->z);
}
static DEVICE_ATTR(data, S_IRUGO | S_IWUSR, adxl344_input_show_data, NULL);

static ssize_t adxl344_input_show_debug(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct adxl344_input_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->debug);
}

static ssize_t adxl344_input_store_debug(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct adxl344_input_data *data = dev_get_drvdata(dev);
	int debug = 0;

	sscanf(&buf[0], "%d", &debug);
	data->debug = !!debug;

	return len;
}
static DEVICE_ATTR(debug, S_IRUGO | S_IWUSR, adxl344_input_show_debug, adxl344_input_store_debug);


static struct attribute *adxl344_input_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_poll_delay.attr,
	&dev_attr_debug.attr,
	&dev_attr_data.attr,
	NULL,
};

static struct attribute_group adxl344_input_attr_group = {
	.attrs = adxl344_input_attrs,
};


#ifdef CONFIG_OF
/*
 * Translate OpenFirmware node properties into platform_data
 */
static struct adxl344_input_data *
adxl344_input_get_devtree_pdata(struct device *dev)
{
	struct device_node *node;
	struct adxl344_input_data *pdata;

	node = dev->of_node;
	if (!node)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	return pdata;
}

static const struct of_device_id adxl344_input_of_match[] = {
	{ .compatible = "adi,adxl344_input", },
	{ },
};
MODULE_DEVICE_TABLE(of, adxl344_input_of_match);

#else

static inline struct adxl344_input_data *
adxl344_input_get_devtree_pdata(struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

#endif

static int adxl344_input_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct adxl344_input_data *pdata = dev_get_platdata(dev);
	int ret;

	if (!pdata) {
		pdata = adxl344_input_get_devtree_pdata(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	pdata->poll_interval = 500;
	pdata->enable = 0;
	mutex_init(&pdata->lock);

	INIT_DELAYED_WORK(&pdata->input_work, adxl344_input_work_func);

	pdata->input = devm_input_allocate_device(dev);
	if (!pdata->input) {
		dev_err(dev, "failed to allocate pdata->input device\n");
		return -ENOMEM;
	}

	pdata->input->name = "ADXL344_Accelerometer";
	pdata->input->phys = "adxl344-accelerometer/input0";
	pdata->input->dev.parent = &pdev->dev;
	pdata->input->open = adxl344_input_open;
	pdata->input->close = adxl344_input_close;

	pdata->input->id.bustype = BUS_I2C;
	pdata->input->id.vendor = 0x0001;
	pdata->input->id.product = 0x0001;
	pdata->input->id.version = 0x0100;

	platform_set_drvdata(pdev, pdata);
	input_set_drvdata(pdata->input, pdata);
	input_set_capability(pdata->input, EV_ABS, ABS_ACCELEROMETER_X);
	input_set_capability(pdata->input, EV_ABS, ABS_ACCELEROMETER_Y);
	input_set_capability(pdata->input, EV_ABS, ABS_ACCELEROMETER_Z);
	set_bit(EV_ABS, pdata->input->evbit);
	ret = input_register_device(pdata->input);
	if(ret < 0) {
		dev_err(dev, "Unable to register input device, error: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &adxl344_input_attr_group);
	if (ret) {
		dev_err(dev, "Unable to create group, error: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int adxl344_input_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &adxl344_input_attr_group);

	device_init_wakeup(&pdev->dev, 0);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int adxl344_input_suspend(struct device *dev)
{
	return 0;
}

static int adxl344_input_resume(struct device *dev)
{
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(adxl344_input_pm_ops, adxl344_input_suspend, adxl344_input_resume);

static struct platform_driver adxl344_input_device_driver = {
	.probe		= adxl344_input_probe,
	.remove		= adxl344_input_remove,
	.driver		= {
		.name	= "adxl344_input-accelerometer",
		.owner	= THIS_MODULE,
		.pm	= &adxl344_input_pm_ops,
		.of_match_table = of_match_ptr(adxl344_input_of_match),
	}
};

static int __init adxl344_input_init(void)
{
	return platform_driver_register(&adxl344_input_device_driver);
}

static void __exit adxl344_input_exit(void)
{
	platform_driver_unregister(&adxl344_input_device_driver);
}

late_initcall(adxl344_input_init);
module_exit(adxl344_input_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CHOI DongWoon <dw.choi@e-roum.com>");
MODULE_DESCRIPTION("ADXL344 accelerometer sensor input driver for RP-400 device");
MODULE_ALIAS("platform:adxl344_input-accelerometer");


