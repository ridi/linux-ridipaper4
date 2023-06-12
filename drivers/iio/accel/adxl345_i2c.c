/*
 * ADXL345 3-Axis Digital Accelerometer I2C driver
 *
 * Copyright (c) 2017 Eva Rachel Retuya <eraretuya@gmail.com>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * 7-bit I2C slave address: 0x1D (ALT ADDRESS pin tied to VDDIO) or
 * 0x53 (ALT ADDRESS pin grounded)
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "adxl345.h"

#if defined(CONFIG_MACH_RP400)
#include <linux/regulator/consumer.h>
#include <linux/delay.h>

static struct regulator *gvdd28;
static struct regulator *gvdd18;
#endif

static const struct regmap_config adxl345_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int adxl345_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(client, &adxl345_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Error initializing i2c regmap: %ld\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

#if defined(CONFIG_MACH_RP400)
	gvdd28 = devm_regulator_get(&client->dev, "vdd28");
	if (IS_ERR(gvdd28)) {
		ret = PTR_ERR(gvdd28);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "Failed to get 'vdd28' regulator: %d\n", ret);
		return ret;
	}

	gvdd18 = devm_regulator_get(&client->dev, "vdd18");
	if (IS_ERR(gvdd18)) {
		ret = PTR_ERR(gvdd18);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "Failed to get 'vdd18' regulator: %d\n", ret);
		return ret;
	}

	if(!regulator_is_enabled(gvdd18)) {
		regulator_set_voltage(gvdd18, 1800000, 1800000);
		regulator_enable(gvdd18);
		usleep_range(1000, 2000);
	} else {
		dev_info(&client->dev, "vdd28 already enabled!\n");
	}
#endif

	return adxl345_core_probe(&client->dev, regmap, id ? id->name : NULL);

}

static int adxl345_i2c_remove(struct i2c_client *client)
{
	return adxl345_core_remove(&client->dev);
}

#ifdef CONFIG_MACH_RP400
static int adxl345_i2c_suspend(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	regulator_disable(gvdd28);
	regulator_disable(gvdd18);

	return 0;
}

static int adxl345_i2c_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	regulator_enable(gvdd28);
	regulator_enable(gvdd18);

	usleep_range(1000, 2000);

	adxl345_core_resume(dev);

	return 0;
}
static SIMPLE_DEV_PM_OPS(adxl345_i2c_pm_ops, adxl345_i2c_suspend, adxl345_i2c_resume);
#endif


static const struct i2c_device_id adxl345_i2c_id[] = {
	{ "adxl345", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, adxl345_i2c_id);

static const struct of_device_id adxl345_of_match[] = {
	{ .compatible = "adi,adxl345" },
	{ },
};

MODULE_DEVICE_TABLE(of, adxl345_of_match);

static struct i2c_driver adxl345_i2c_driver = {
	.driver = {
		.name	= "adxl345_i2c",
		.of_match_table = adxl345_of_match,
#ifdef CONFIG_MACH_RP400
		.pm = &adxl345_i2c_pm_ops,
#endif
	},
	.probe		= adxl345_i2c_probe,
	.remove		= adxl345_i2c_remove,
	.id_table	= adxl345_i2c_id,
};

module_i2c_driver(adxl345_i2c_driver);

MODULE_AUTHOR("Eva Rachel Retuya <eraretuya@gmail.com>");
MODULE_DESCRIPTION("ADXL345 3-Axis Digital Accelerometer I2C driver");
MODULE_LICENSE("GPL v2");
