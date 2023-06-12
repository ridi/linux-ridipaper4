/*
 * BU52072GWZ Magnetic input driver
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


extern void send_power_key_event(void);
extern void send_sleep_key_event(void);
extern void send_wakeup_key_event(void);
extern int dsim_status(void);

#define ABS_MAGNETIC_DATA   0x33

struct bu52072_data {
	struct device *dev;
	struct input_dev *input;
	struct mutex lock;
	struct delayed_work input_work;
	int out_data;
	int enable;
	int poll_interval;
	int debug;
	int intr_gpio;
	int irq;
	int test;
};

static int bu52072_open(struct input_dev *input)
{
	return 0;
}

static void bu52072_close(struct input_dev *input)
{
	
}

static void bu52072_report_values(struct bu52072_data *data)
{
	input_report_abs(data->input, ABS_MAGNETIC_DATA, data->out_data);
	input_sync(data->input);

	if(data->debug)
		printk("%s %d\n", __func__, data->out_data);
}

static void bu52072_input_work_func(struct work_struct *work)
{
	struct bu52072_data *data = container_of((struct delayed_work *)work, struct bu52072_data, input_work);

	mutex_lock(&data->lock);
	data->out_data = gpio_get_value(data->intr_gpio);
	mutex_unlock(&data->lock);
	bu52072_report_values(data);

	schedule_delayed_work(&data->input_work, msecs_to_jiffies(data->poll_interval));
}

static ssize_t bu52072_show_enable(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct bu52072_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->enable);
}

static ssize_t bu52072_store_enable(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct bu52072_data *data = dev_get_drvdata(dev);

	sscanf(&buf[0], "%d", &data->enable);
	if(data->enable) {
		schedule_delayed_work(&data->input_work, msecs_to_jiffies(data->poll_interval));
	} else {
		cancel_delayed_work_sync(&data->input_work);
	}

	return len;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, bu52072_show_enable, bu52072_store_enable);


static ssize_t bu52072_show_poll_delay(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct bu52072_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->poll_interval);
}

static ssize_t bu52072_store_poll_delay(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct bu52072_data *data = dev_get_drvdata(dev);
	int64_t poll_delay = 0;

	sscanf(&buf[0], "%lld", &poll_delay);
	printk("poll_delay: %lld ns\n", poll_delay);
	data->poll_interval = poll_delay/1000000;

	return len;
}

static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR, bu52072_show_poll_delay, bu52072_store_poll_delay);


static ssize_t bu52072_show_data(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct bu52072_data *data = dev_get_drvdata(dev);
	mutex_lock(&data->lock);
	data->out_data = gpio_get_value(data->intr_gpio);
	mutex_unlock(&data->lock);
	return sprintf(buf, "%d\n", data->out_data);
}
static DEVICE_ATTR(data, S_IRUGO | S_IWUSR, bu52072_show_data, NULL);

static ssize_t bu52072_show_debug(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct bu52072_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->debug);
}

static ssize_t bu52072_store_debug(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct bu52072_data *data = dev_get_drvdata(dev);
	int debug = 0;

	sscanf(&buf[0], "%d", &debug);
	data->debug = !!debug;

	return len;
}
static DEVICE_ATTR(debug, S_IRUGO | S_IWUSR, bu52072_show_debug, bu52072_store_debug);

static ssize_t bu52072_show_test(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct bu52072_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->test);
}

static ssize_t bu52072_store_test(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct bu52072_data *data = dev_get_drvdata(dev);
	int test = 0;

	sscanf(&buf[0], "%d", &test);
	data->test = !!test;

	if(data->test == 0) {
		send_sleep_key_event();
	} else {
		send_wakeup_key_event();
	}

	return len;
}
static DEVICE_ATTR(test, S_IRUGO | S_IWUSR, bu52072_show_test, bu52072_store_test);

static struct attribute *bu52072_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_poll_delay.attr,
	&dev_attr_data.attr,
	&dev_attr_debug.attr,
	&dev_attr_test.attr,
	NULL,
};

static struct attribute_group bu52072_attr_group = {
	.attrs = bu52072_attrs,
};

static irqreturn_t bu52072_irq(int irq, void *_dev)
{
	struct bu52072_data *data = (struct bu52072_data *)_dev;
	int out_data;

	out_data = gpio_get_value(data->intr_gpio);
	dev_err(data->dev, "%s %d\n", __func__, out_data);

	if(out_data == 0) {
		send_sleep_key_event();
	} else {
		send_wakeup_key_event();
	}

	return IRQ_HANDLED;
}


#ifdef CONFIG_OF
/*
 * Translate OpenFirmware node properties into platform_data
 */
static struct bu52072_data *
bu52072_get_devtree_pdata(struct device *dev)
{
	struct device_node *node;
	struct bu52072_data *data;
	int status;

	node = dev->of_node;
	if (!node)
		return ERR_PTR(-ENODEV);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	data->dev = dev;

	data->intr_gpio = of_get_named_gpio_flags(dev->of_node, "out_data-gpios", 0, NULL);
	dev_info(dev, "intr_gpio = %d\n", data->intr_gpio);
	if(!gpio_is_valid(data->intr_gpio)) {
		dev_err(dev, "failed to get intr gpio\n");
	} else {
		data->irq = gpio_to_irq(data->intr_gpio);
		dev_info(dev, "irq num = %d\n", data->irq);
	}

	gpio_request(data->intr_gpio, "magnetic_irq");
	gpio_direction_input(data->intr_gpio);
	data->out_data = gpio_get_value(data->intr_gpio);

	status = devm_request_threaded_irq(dev, data->irq,
                                      NULL, bu52072_irq,
                                      IRQF_TRIGGER_FALLING |IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                                      "bu52072", data);

	return data;
}

static const struct of_device_id bu52072_of_match[] = {
	{ .compatible = "rohm,bu52072gwz", },
	{ },
};
MODULE_DEVICE_TABLE(of, bu52072_of_match);

#else

static inline struct bu52072_data *
bu52072_get_devtree_pdata(struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

#endif

static int bu52072_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bu52072_data *pdata = dev_get_platdata(dev);
	int ret;

	if (!pdata) {
		pdata = bu52072_get_devtree_pdata(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	pdata->poll_interval = 500;
	pdata->enable = 0;
	mutex_init(&pdata->lock);

	INIT_DELAYED_WORK(&pdata->input_work, bu52072_input_work_func);

	pdata->input = devm_input_allocate_device(dev);
	if (!pdata->input) {
		dev_err(dev, "failed to allocate pdata->input device\n");
		return -ENOMEM;
	}

	pdata->input->name = "ROHM_BU52072_Magnetic";
	pdata->input->phys = "bu52072-megnetic/input0";
	pdata->input->dev.parent = &pdev->dev;
	pdata->input->open = bu52072_open;
	pdata->input->close = bu52072_close;

	pdata->input->id.bustype = BUS_I2C;
	pdata->input->id.vendor = 0x0001;
	pdata->input->id.product = 0x0001;
	pdata->input->id.version = 0x0100;

	platform_set_drvdata(pdev, pdata);
	input_set_drvdata(pdata->input, pdata);
	input_set_capability(pdata->input, EV_ABS, ABS_MAGNETIC_DATA);
	set_bit(EV_ABS, pdata->input->evbit);
	ret = input_register_device(pdata->input);
	if(ret < 0) {
		dev_err(dev, "Unable to register input device, error: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &bu52072_attr_group);
	if (ret) {
		dev_err(dev, "Unable to create group, error: %d\n",
			ret);
		return ret;
	}

	device_init_wakeup(dev, 1);

	return 0;
}

static int bu52072_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &bu52072_attr_group);

	device_init_wakeup(&pdev->dev, 0);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bu52072_suspend(struct device *dev)
{
	struct bu52072_data *data = dev_get_drvdata(dev);

	if(device_may_wakeup(dev)) {
		enable_irq_wake(data->irq);
	}

	return 0;
}

static int bu52072_resume(struct device *dev)
{
	struct bu52072_data *data = dev_get_drvdata(dev);

	if(device_may_wakeup(dev)) {
		disable_irq_wake(data->irq);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(bu52072_pm_ops, bu52072_suspend, bu52072_resume);

static struct platform_driver bu52072_device_driver = {
	.probe		= bu52072_probe,
	.remove		= bu52072_remove,
	.driver		= {
		.name	= "bu52072-megnetic",
		.owner	= THIS_MODULE,
		.pm	= &bu52072_pm_ops,
		.of_match_table = of_match_ptr(bu52072_of_match),
	}
};

static int __init bu52072_init(void)
{
	return platform_driver_register(&bu52072_device_driver);
}

static void __exit bu52072_exit(void)
{
	platform_driver_unregister(&bu52072_device_driver);
}

late_initcall(bu52072_init);
module_exit(bu52072_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CHOI DongWoon <dw.choi@e-roum.com>");
MODULE_DESCRIPTION("BU52072GWZ magnetic sensor input driver for RP-400 device");
MODULE_ALIAS("platform:bu52072-megnetic");


