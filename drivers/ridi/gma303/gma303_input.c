/*
 * GMA303 Accelerometer input driver
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


struct gma303_input_data {
	struct input_dev *input;
	struct mutex lock;
	struct delayed_work input_work;
	int x;
	int y;
	int z;
	int t;
	int enable;
	int poll_interval;
	int debug;
};

extern int gma303_read_xyz(s16 *x, s16 *y, s16 *z, s16 *t);
extern int is_gma303_probe_done(void);

s16 gma303_cali_x = 0;
s16 gma303_cali_y = 0;
s16 gma303_cali_z = 0;

float gma303_slope_x = 0.0f;
float gma303_slope_y = 0.0f;
float gma303_slope_z = 0.0f;

static int gma303_input_open(struct input_dev *input)
{
	return 0;
}

static void gma303_input_close(struct input_dev *input)
{
	
}

static void gma303_input_report_values(struct gma303_input_data *data)
{
	input_report_abs(data->input, ABS_ACCELEROMETER_X, data->x);
	input_report_abs(data->input, ABS_ACCELEROMETER_Y, data->y);
	input_report_abs(data->input, ABS_ACCELEROMETER_Z, data->z);
	input_sync(data->input);

	if(data->debug)
		printk(KERN_ERR "%s %d %d %d\n", __func__, data->x, data->y, data->z);
}

static void gma303_input_work_func(struct work_struct *work)
{
	struct gma303_input_data *data = container_of((struct delayed_work *)work, struct gma303_input_data, input_work);
	int ret;
	s16 x, y, z, t;
	float temp = 0.0f;

	mutex_lock(&data->lock);
	ret = gma303_read_xyz(&x, &y, &z, &t);
	mutex_unlock(&data->lock);
	if (ret == 0) {
		if((x >> 14) & 0x01) { //minus
			data->x = (0xFFFFC000 | x);
		} else {
			data->x = x;
		}
		if((y >> 14) & 0x01) { //minus
			data->y = (0xFFFFC000 | y);
		} else {
			data->y = y;
		}
		if((z >> 15) & 0x01) { //minus
			data->z = (0xFFFF0000 | z);
		} else {
			data->z = z;
		}
		if((t >> 15) & 0x01) { //minus
			data->t = (0xFFFF0000 | t);
		} else {
			data->t = t;
		}
		//calibration data
		data->x -= gma303_cali_x;
		data->y -= gma303_cali_y;
		data->z -= gma303_cali_z;
		//Temperature compensation
		temp = (float)data->t*2;
		data->x = data->x - (temp * (gma303_slope_x));
		data->y = data->y - (temp * (gma303_slope_y));
		data->z = data->z - (temp * (gma303_slope_z));
		gma303_input_report_values(data);
	}

	schedule_delayed_work(&data->input_work, msecs_to_jiffies(data->poll_interval));
}

static ssize_t gma303_input_show_enable(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->enable);
}

static ssize_t gma303_input_store_enable(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);

	if(!is_gma303_probe_done())
		return len;

	sscanf(&buf[0], "%d", &data->enable);
	if(data->enable) {
		schedule_delayed_work(&data->input_work, msecs_to_jiffies(data->poll_interval));
	} else {
		cancel_delayed_work_sync(&data->input_work);
	}

	return len;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR, gma303_input_show_enable, gma303_input_store_enable);


static ssize_t gma303_input_show_poll_delay(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->poll_interval);
}

static ssize_t gma303_input_store_poll_delay(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	int64_t poll_delay = 0;

	sscanf(&buf[0], "%lld", &poll_delay);
	printk("poll_delay: %lld ns\n", poll_delay);
	data->poll_interval = poll_delay/1000000;

	return len;
}

static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR, gma303_input_show_poll_delay, gma303_input_store_poll_delay);


static ssize_t gma303_input_show_raw_data(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	s16 x, y, z, t;

	mutex_lock(&data->lock);
	gma303_read_xyz(&x, &y, &z, &t);
	if((x >> 14) & 0x01) { //minus
		x |= 0xC000;
	}
	if((y >> 14) & 0x01) { //minus
		y |= 0xC000;
	}
	mutex_unlock(&data->lock);

	return sprintf(buf, "%hd %hd %hd %hd\n", x, y, z, t);
}
static DEVICE_ATTR(raw_data, S_IRUGO | S_IWUSR, gma303_input_show_raw_data, NULL);

static ssize_t gma303_input_show_calibrated_data(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	s16 x, y, z, t;
	float temp = 0.0f;

	mutex_lock(&data->lock);
	gma303_read_xyz(&x, &y, &z, &t);
	if((x >> 14) & 0x01) { //minus
		x |= 0xC000;
	}
	if((y >> 14) & 0x01) { //minus
		y |= 0xC000;
	}
	//calibration data
	x -= gma303_cali_x;
	y -= gma303_cali_y;
	z -= gma303_cali_z;
	//Temperature compensation
	temp = (float)t*2;
	x = x - (temp * (gma303_slope_x));
	y = y - (temp * (gma303_slope_y));
	z = z - (temp * (gma303_slope_z));
	mutex_unlock(&data->lock);

	return sprintf(buf, "%hd %hd %hd %hd\n", x, y, z, t);
}

static DEVICE_ATTR(calibrated_data, S_IRUGO | S_IWUSR, gma303_input_show_calibrated_data, NULL);

static ssize_t gma303_input_show_calibrated_all_data(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	s16 x, y, z, t;
	s16 cx, cy, cz;
	float temp = 0.0f;

	mutex_lock(&data->lock);
	gma303_read_xyz(&x, &y, &z, &t);
	if((x >> 14) & 0x01) { //minus
		x |= 0xC000;
	}
	if((y >> 14) & 0x01) { //minus
		y |= 0xC000;
	}
	//calibration data
	x -= gma303_cali_x;
	y -= gma303_cali_y;
	z -= gma303_cali_z;
	//Temperature compensation
	temp = (float)t*2;
	cx = x - (temp * (gma303_slope_x));
	cy = y - (temp * (gma303_slope_y));
	cz = z - (temp * (gma303_slope_z));
	mutex_unlock(&data->lock);

	return sprintf(buf, "%hd %hd %hd %hd %hd %hd %hd\n", x, y, z, cx, cy, cz, t);
}

static DEVICE_ATTR(calibrated_all_data, S_IRUGO | S_IWUSR, gma303_input_show_calibrated_all_data, NULL);

static ssize_t gma303_input_show_data(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	s16 x, y, z, t;
	float temp = 0.0f;

	mutex_lock(&data->lock);
	gma303_read_xyz(&x, &y, &z, &t);
	if((x >> 14) & 0x01) { //minus
		x |= 0xC000;
	}
	if((y >> 14) & 0x01) { //minus
		y |= 0xC000;
	}
	//calibration data
	x -= gma303_cali_x;
	y -= gma303_cali_y;
	z -= gma303_cali_z;
	//Temperature compensation
	temp = (float)t*2;
	x = x - (temp * (gma303_slope_x));
	y = y - (temp * (gma303_slope_y));
	z = z - (temp * (gma303_slope_z));
	mutex_unlock(&data->lock);

	return sprintf(buf, "%hd %hd %hd\n", x, y, z);
}
static DEVICE_ATTR(data, S_IRUGO | S_IWUSR, gma303_input_show_data, NULL);

static ssize_t gma303_input_show_temp_data(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	s16 x, y, z, t;

	mutex_lock(&data->lock);
	gma303_read_xyz(&x, &y, &z, &t);
	if((x >> 14) & 0x01) { //minus
		x |= 0xC000;
	}
	if((y >> 14) & 0x01) { //minus
		y |= 0xC000;
	}
	mutex_unlock(&data->lock);

	return sprintf(buf, "%hd -> %dC\n", t, 25 + (t * 2));
}
static DEVICE_ATTR(temp_data, S_IRUGO | S_IWUSR, gma303_input_show_temp_data, NULL);

static ssize_t gma303_input_show_debug(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->debug);
}

static ssize_t gma303_input_store_debug(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct gma303_input_data *data = dev_get_drvdata(dev);
	int debug = 0;

	sscanf(&buf[0], "%d", &debug);
	data->debug = !!debug;

	return len;
}
static DEVICE_ATTR(debug, S_IRUGO | S_IWUSR, gma303_input_show_debug, gma303_input_store_debug);


static struct attribute *gma303_input_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_poll_delay.attr,
	&dev_attr_debug.attr,
	&dev_attr_data.attr,
	&dev_attr_raw_data.attr,
	&dev_attr_calibrated_data.attr,
	&dev_attr_calibrated_all_data.attr,
	&dev_attr_temp_data.attr,
	NULL,
};

static struct attribute_group gma303_input_attr_group = {
	.attrs = gma303_input_attrs,
};


#ifdef CONFIG_OF
/*
 * Translate OpenFirmware node properties into platform_data
 */
static struct gma303_input_data *
gma303_input_get_devtree_pdata(struct device *dev)
{
	struct device_node *node;
	struct gma303_input_data *pdata;

	node = dev->of_node;
	if (!node)
		return ERR_PTR(-ENODEV);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	return pdata;
}

static const struct of_device_id gma303_input_of_match[] = {
	{ .compatible = "mems,gma303_input", },
	{ },
};
MODULE_DEVICE_TABLE(of, gma303_input_of_match);

#else

static inline struct gma303_input_data *
gma303_input_get_devtree_pdata(struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

#endif

static int gma303_input_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gma303_input_data *pdata = dev_get_platdata(dev);
	int ret;

	if (!pdata) {
		pdata = gma303_input_get_devtree_pdata(dev);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
	}

	pdata->poll_interval = 500;
	pdata->enable = 0;
	mutex_init(&pdata->lock);

	INIT_DELAYED_WORK(&pdata->input_work, gma303_input_work_func);

	pdata->input = devm_input_allocate_device(dev);
	if (!pdata->input) {
		dev_err(dev, "failed to allocate pdata->input device\n");
		return -ENOMEM;
	}

	pdata->input->name = "GMA303_Accelerometer";
	pdata->input->phys = "gma303-accelerometer/input0";
	pdata->input->dev.parent = &pdev->dev;
	pdata->input->open = gma303_input_open;
	pdata->input->close = gma303_input_close;

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

	ret = sysfs_create_group(&pdev->dev.kobj, &gma303_input_attr_group);
	if (ret) {
		dev_err(dev, "Unable to create group, error: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static int gma303_input_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &gma303_input_attr_group);

	device_init_wakeup(&pdev->dev, 0);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gma303_input_suspend(struct device *dev)
{
	return 0;
}

static int gma303_input_resume(struct device *dev)
{
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(gma303_input_pm_ops, gma303_input_suspend, gma303_input_resume);

static struct platform_driver gma303_input_device_driver = {
	.probe		= gma303_input_probe,
	.remove		= gma303_input_remove,
	.driver		= {
		.name	= "gma303_input-accelerometer",
		.owner	= THIS_MODULE,
		.pm	= &gma303_input_pm_ops,
		.of_match_table = of_match_ptr(gma303_input_of_match),
	}
};

static int __init gma303_input_init(void)
{
	return platform_driver_register(&gma303_input_device_driver);
}

static void __exit gma303_input_exit(void)
{
	platform_driver_unregister(&gma303_input_device_driver);
}

late_initcall(gma303_input_init);
module_exit(gma303_input_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CHOI DongWoon <dw.choi@e-roum.com>");
MODULE_DESCRIPTION("GMA303 accelerometer sensor input driver for RP-400 device");
MODULE_ALIAS("platform:gma303_input-accelerometer");


