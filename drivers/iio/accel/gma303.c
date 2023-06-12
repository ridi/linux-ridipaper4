/*
 * 3-axis accelerometer driver for GMA303KU Memsic sensor
 *
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef DEBUG
#define DEBUG
#endif

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/acpi.h>
#include <linux/regmap.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

#define GMA303_DRV_NAME		"gma303"
#define GMA303_IRQ_NAME		"gma303_event"

#define GMA303_REG_PID 0x00
#define GMA303_REG_PWR_DN_CTRL 0x01
#define GMA303_REG_ACTION 0x02
#define GMA303_REG_MOTION_THRESHOLD 0x03
#define GMA303_REG_DATA_READ 0x04
#define GMA303_REG_STATUS 0x05
#define GMA303_REG_DATA_X 0x06
#define GMA303_REG_DATA_Y 0x08
#define GMA303_REG_DATA_Z 0x0A
#define GMA303_REG_INT_CONF 0x15
#define GMA303_REG_CTRL1 0x16
#define GMA303_REG_CTRL2 0x17
#define GMA303_REG_CTRL3 0x18
#define GMA303_REG_OVER_SAMPLE_MODE 0x38

#define GMA303_MAX_REG		0x3D

#define GMA303_CALI_Z 0

enum gma303_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
};

enum gma303_range {
	GMA303_RANGE_2G,
	GMA303_RANGE_4G,
	GMA303_RANGE_8G,
};

struct gma303_data {
	struct device *dev;
	struct mutex mutex;
	struct regmap *regmap;
	u8 status;
	struct regulator *vdd28;
	struct regulator *vdd18;
};

static struct gma303_data *gdata;

/*
 * GMA303 can operate in the following ranges:
 * +/- 2G, 4G, 8G (the default +/-2G)
 *
 * (2 + 2) * 9.81 / (2^12 - 1) = 0.009582
 * (4 + 4) * 9.81 / (2^12 - 1) = 0.019164
 * (8 + 8) * 9.81 / (2^12 - 1) = 0.038329
 */
static const struct {
	u8 range;
	int scale;
} gma303_scale_table[] = {
	{GMA303_RANGE_2G, 9582},
	{GMA303_RANGE_4G, 19164},
	{GMA303_RANGE_8G, 38329},
};


static IIO_CONST_ATTR(in_accel_scale_available, "0.009582 0.019164 0.038329");

static struct attribute *gma303_attributes[] = {
	&iio_const_attr_in_accel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group gma303_attrs_group = {
	.attrs = gma303_attributes,
};

static const struct regmap_config gma303_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int gma303_probe_done = 0;

int is_gma303_probe_done(void)
{
	return gma303_probe_done;
}

int gma303_read_xyz(s16 *x, s16 *y, s16 *z, s16 *t)
{
	int ret;
	u8 regs[11];
	u8* data;

	if(!gdata)
		return -ENODEV;

	regmap_bulk_read(gdata->regmap, GMA303_REG_DATA_READ, &regs, sizeof(regs));
	if (ret < 0) {
		dev_err(gdata->dev, "failed to read reg %02x\n", GMA303_REG_DATA_READ);
		return ret;
	}

	gdata->status = regs[2];
	data = &regs[3];

	*x = ((data[1] << 7) | (data[0] >> 1));
	*y = ((data[3] << 7) | (data[2] >> 1));
#if GMA303_CALI_Z
	*z = ((data[5] << 8) | data[4]);
#else
	*z = 0;
#endif
	*t = ((data[7] << 8) | data[6]);

	return 0;
}

static int gma303_read_axis(struct gma303_data *data, unsigned int addr)
{
	u8 regs[11];
	int ret;
	int out_data = 0;

	ret = regmap_bulk_read(data->regmap, GMA303_REG_DATA_READ, &regs, sizeof(regs));
	if (ret < 0) {
		dev_err(data->dev, "failed to read regs %02x\n", addr);
		return ret;
	}

	data->status = regs[2];

	if(addr == GMA303_REG_DATA_X)
		out_data = ((regs[4] << 7) | (regs[3] >> 1));
	else if(addr == GMA303_REG_DATA_Y)
		out_data = ((regs[6] << 7) | (regs[5] >> 1));
	else if(addr == GMA303_REG_DATA_Z)
		out_data = ((regs[8] << 8) | regs[7]);

	return out_data;
}

static int gma303_read_scale(struct gma303_data *data)
{
#if 0
	unsigned int reg;
	int ret;
	int i;

	ret = regmap_read(data->regmap, GMA303_REG_CONTROL, &reg);
	if (ret < 0) {
		dev_err(data->dev, "failed to read reg_control\n");
		return ret;
	}

	i = reg >> GMA303_CONTROL_FSR_SHIFT;

	if (i < 0 || i >= ARRAY_SIZE(gma303_scale_table))
		return -EINVAL;

	return gma303_scale_table[i].scale;
#else
	return gma303_scale_table[0].scale;
#endif
}

static int gma303_set_scale(struct gma303_data *data, int val)
{
	//unsigned int reg;
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(gma303_scale_table); i++) {
		if (gma303_scale_table[i].scale == val) {
#if 0
			reg = i << GMA303_CONTROL_FSR_SHIFT;
			ret = regmap_update_bits(data->regmap,
						 GMA303_REG_CONTROL,
						 GMA303_REG_CONTROL_MASK_FSR,
						 reg);
			if (ret < 0)
				dev_err(data->dev,
					"failed to write reg_control\n");
#else
			return 0;
#endif
			return ret;
		}
	}

	return -EINVAL;
}

static int gma303_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct gma303_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_ACCEL:
			if (iio_buffer_enabled(indio_dev))
				return -EBUSY;

			ret = gma303_read_axis(data, chan->address);
			if (ret < 0)
				return ret;
			if((ret >> 15) & 0x01) { //minus
				ret |= 0xFFFF0000;
			}
			*val = ret;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		ret = gma303_read_scale(data);
		if (ret < 0)
			return ret;

		*val = 0;
		*val2 = ret;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int gma303_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct gma303_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (val != 0)
			return -EINVAL;

		return gma303_set_scale(data, val2);
	default:
		return -EINVAL;
	}
}

static const struct iio_info gma303_info = {
	.driver_module	= THIS_MODULE,
	.read_raw	= gma303_read_raw,
	.write_raw	= gma303_write_raw,
	.attrs		= &gma303_attrs_group,
};

static const unsigned long gma303_scan_masks[] = {
	BIT(AXIS_X) | BIT(AXIS_Y) | BIT(AXIS_Z),
	0
};

#define GMA303_CHANNEL(_axis, _addr) {				\
	.type = IIO_ACCEL,					\
	.modified = 1,						\
	.channel2 = IIO_MOD_##_axis,				\
	.address = _addr,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.scan_index = AXIS_##_axis,				\
	.scan_type = {						\
		.sign = 's',					\
		.realbits = 13,					\
		.storagebits = 16,				\
		.shift = 4,					\
		.endianness = IIO_LE,				\
	},							\
}

static const struct iio_chan_spec gma303_channels[] = {
	GMA303_CHANNEL(X, GMA303_REG_DATA_X),
	GMA303_CHANNEL(Y, GMA303_REG_DATA_Y),
	GMA303_CHANNEL(Z, GMA303_REG_DATA_Z),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static int gma303_chip_init(struct gma303_data *data)
{
	int ret;
	unsigned int reg;

	regmap_write(data->regmap, GMA303_REG_PWR_DN_CTRL, 0x02); //Software reset
	msleep(30);

	ret = regmap_read(data->regmap, GMA303_REG_PID, &reg);
	if (ret < 0) {
		dev_err(data->dev, "failed to read chip id\n");
		return ret;
	}
	dev_dbg(data->dev, "GMA303 chip id %02x\n", reg);

	if (reg != 0xA3)
		return -ENOENT;

	regmap_write(data->regmap, GMA303_REG_MOTION_THRESHOLD, 0x04);//0x3:Set interrupt threshold to 1G.
	regmap_write(data->regmap, GMA303_REG_INT_CONF, 0x19);//0x15:INT pin set to open-drain, active high and disable interrupt.
	regmap_write(data->regmap, GMA303_REG_CTRL1, 0x00);//0x16:Set X, Y, and Z data output as low pass filter.
#if 0 //High sensitivity
	regmap_write(data->regmap, GMA303_REG_OVER_SAMPLE_MODE, 0x9F);//set 16 step
	regmap_write(data->regmap, GMA303_REG_ACTION, 0x02);//
	regmap_write(data->regmap, GMA303_REG_ACTION, 0x00); 
	regmap_write(data->regmap, GMA303_REG_ACTION, 0x04); //continuous mode 
	regmap_write(data->regmap, GMA303_REG_ACTION, 0x00); 
#endif

	return 0;
}

static int gma303_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct gma303_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &gma303_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "failed to initialize regmap\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->dev = &client->dev;
	data->regmap = regmap;

	data->vdd28 = devm_regulator_get(&client->dev, "vdd28");
	if (IS_ERR(data->vdd28)) {
		ret = PTR_ERR(data->vdd28);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "Failed to get 'vdd28' regulator: %d\n", ret);
		return ret;
	}

	data->vdd18 = devm_regulator_get(&client->dev, "vdd18");
	if (IS_ERR(data->vdd18)) {
		ret = PTR_ERR(data->vdd18);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "Failed to get 'vdd18' regulator: %d\n", ret);
		return ret;
	}

	if(!regulator_is_enabled(data->vdd28)) {
		regulator_set_voltage(data->vdd28, 2800000, 2800000);
		regulator_enable(data->vdd28);
		usleep_range(1000, 2000);
	} else {
		dev_info(&client->dev, "vdd28 already enabled!\n");
	}

	if(!regulator_is_enabled(data->vdd18)) {
		regulator_set_voltage(data->vdd18, 1800000, 1800000);
		regulator_enable(data->vdd18);
		usleep_range(1000, 2000);
	} else {
		dev_info(&client->dev, "vdd18 already enabled!\n");
	}

	ret = gma303_chip_init(data);
	if (ret < 0) {
		dev_err(&client->dev, "failed to initialize chip\n");
		return ret;
	}
	gdata = data;

	mutex_init(&data->mutex);

	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = gma303_channels;
	indio_dev->num_channels = ARRAY_SIZE(gma303_channels);
	indio_dev->available_scan_masks = gma303_scan_masks;
	indio_dev->name = GMA303_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &gma303_info;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev,
			"unable to register iio device %d\n", ret);
		goto err_buffer_cleanup;
	}
	dev_info(&client->dev, "registered accelerometer %s\n", indio_dev->name);
	gma303_probe_done = 1;

	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);

	return ret;
}

static int gma303_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	//struct gma303_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gma303_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct gma303_data *data = iio_priv(indio_dev);

	dev_info(dev, "%s\n", __func__);

	regulator_disable(data->vdd28);
	regulator_disable(data->vdd18);

	return 0;
}

static int gma303_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct gma303_data *data = iio_priv(indio_dev);

	dev_info(dev, "%s\n", __func__);

	regulator_enable(data->vdd28);
	usleep_range(1000, 2000);

	regulator_enable(data->vdd18);
	usleep_range(1000, 2000);

	gma303_chip_init(data);

	return 0;
}
static SIMPLE_DEV_PM_OPS(gma303_pm_ops, gma303_suspend, gma303_resume);
#endif


static const struct of_device_id gma303_of_match[] = {
	{ .compatible = "mems,gma303" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, gma303_of_match);



static const struct i2c_device_id gma303_id[] = {
	{"gma303",	0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, gma303_id);

static struct i2c_driver gma303_driver = {
	.driver = {
		.name = GMA303_DRV_NAME,
		.of_match_table = of_match_ptr(gma303_of_match),
#ifdef CONFIG_PM_SLEEP
		.pm = &gma303_pm_ops,
#endif
	},
	.probe		= gma303_probe,
	.remove		= gma303_remove,
	.id_table	= gma303_id,
};

module_i2c_driver(gma303_driver);

MODULE_AUTHOR("Terry Choi <dw.choi@e-roum.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("GMA303 3-axis accelerometer driver");
