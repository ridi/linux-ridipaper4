/*
 * STMicroelectronics accelerometers driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>

#include <linux/iio/common/st_sensors.h>
#include <linux/iio/common/st_sensors_i2c.h>
#include "st_accel.h"

#if defined(CONFIG_MACH_RP400)
#include <linux/delay.h>
#endif

#ifdef CONFIG_OF
static const struct of_device_id st_accel_of_match[] = {
	{
		/* An older compatible */
		.compatible = "st,lis3lv02d",
		.data = LIS3LV02DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis3lv02dl-accel",
		.data = LIS3LV02DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303dlh-accel",
		.data = LSM303DLH_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303dlhc-accel",
		.data = LSM303DLHC_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis3dh-accel",
		.data = LIS3DH_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm330d-accel",
		.data = LSM330D_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm330dl-accel",
		.data = LSM330DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm330dlc-accel",
		.data = LSM330DLC_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis331dl-accel",
		.data = LIS331DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis331dlh-accel",
		.data = LIS331DLH_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303dl-accel",
		.data = LSM303DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303dlm-accel",
		.data = LSM303DLM_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm330-accel",
		.data = LSM330_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lsm303agr-accel",
		.data = LSM303AGR_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis2dh12-accel",
		.data = LIS2DH12_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,h3lis331dl-accel",
		.data = H3LIS331DL_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lis3l02dq",
		.data = LIS3L02DQ_ACCEL_DEV_NAME,
	},
	{
		.compatible = "st,lng2dm-accel",
		.data = LNG2DM_ACCEL_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_accel_of_match);
#else
#define st_accel_of_match NULL
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id st_accel_acpi_match[] = {
	{"SMO8A90", LNG2DM},
	{ },
};
MODULE_DEVICE_TABLE(acpi, st_accel_acpi_match);
#else
#define st_accel_acpi_match NULL
#endif

static const struct i2c_device_id st_accel_id_table[] = {
	{ LSM303DLH_ACCEL_DEV_NAME, LSM303DLH },
	{ LSM303DLHC_ACCEL_DEV_NAME, LSM303DLHC },
	{ LIS3DH_ACCEL_DEV_NAME, LIS3DH },
	{ LSM330D_ACCEL_DEV_NAME, LSM330D },
	{ LSM330DL_ACCEL_DEV_NAME, LSM330DL },
	{ LSM330DLC_ACCEL_DEV_NAME, LSM330DLC },
	{ LIS331DLH_ACCEL_DEV_NAME, LIS331DLH },
	{ LSM303DL_ACCEL_DEV_NAME, LSM303DL },
	{ LSM303DLM_ACCEL_DEV_NAME, LSM303DLM },
	{ LSM330_ACCEL_DEV_NAME, LSM330 },
	{ LSM303AGR_ACCEL_DEV_NAME, LSM303AGR },
	{ LIS2DH12_ACCEL_DEV_NAME, LIS2DH12 },
	{ LIS3L02DQ_ACCEL_DEV_NAME, LIS3L02DQ },
	{ LNG2DM_ACCEL_DEV_NAME, LNG2DM },
	{ H3LIS331DL_ACCEL_DEV_NAME, H3LIS331DL },
	{ LIS331DL_ACCEL_DEV_NAME, LIS331DL },
	{ LIS3LV02DL_ACCEL_DEV_NAME, LIS3LV02DL },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_accel_id_table);

static int st_accel_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct st_sensor_data *adata;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*adata));
	if (!indio_dev)
		return -ENOMEM;

	adata = iio_priv(indio_dev);

#if defined(CONFIG_MACH_RP400)
	adata->vdd28 = devm_regulator_get(&client->dev, "vdd28");
	if (IS_ERR(adata->vdd28)) {
		ret = PTR_ERR(adata->vdd28);
		if (ret != -EPROBE_DEFER)
			dev_err(&client->dev, "Failed to get 'vdd28' regulator: %d\n", ret);
		return ret;
	}

	if(!regulator_is_enabled(adata->vdd28)) {
		regulator_set_voltage(adata->vdd28, 2800000, 2800000);
		regulator_enable(adata->vdd28);
		usleep_range(1000, 2000);
	} else {
		dev_info(&client->dev, "vdd28 already enabled!\n");
	}
#endif

	if (client->dev.of_node) {
		st_sensors_of_name_probe(&client->dev, st_accel_of_match,
					 client->name, sizeof(client->name));
	} else if (ACPI_HANDLE(&client->dev)) {
		ret = st_sensors_match_acpi_device(&client->dev);
		if ((ret < 0) || (ret >= ST_ACCEL_MAX))
			return -ENODEV;

		strncpy(client->name, st_accel_id_table[ret].name,
				sizeof(client->name));
		client->name[sizeof(client->name) - 1] = '\0';
	} else if (!id)
		return -ENODEV;


	st_sensors_i2c_configure(indio_dev, client, adata);

	ret = st_accel_common_probe(indio_dev);
	if (ret < 0)
		return ret;

	return 0;
}

static int st_accel_i2c_remove(struct i2c_client *client)
{
	st_accel_common_remove(i2c_get_clientdata(client));

	return 0;
}

#ifdef CONFIG_MACH_RP400
static int st_accel_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct st_sensor_data *data = iio_priv(indio_dev);

	dev_info(dev, "%s\n", __func__);

	regulator_disable(data->vdd28);

	return 0;
}

static int st_accel_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct st_sensor_data *data = iio_priv(indio_dev);

	dev_info(dev, "%s\n", __func__);

	regulator_enable(data->vdd28);

	return 0;
}
static SIMPLE_DEV_PM_OPS(st_accel_i2c_pm_ops, st_accel_i2c_suspend, st_accel_i2c_resume);
#endif

static struct i2c_driver st_accel_driver = {
	.driver = {
		.name = "st-accel-i2c",
		.of_match_table = of_match_ptr(st_accel_of_match),
		.acpi_match_table = ACPI_PTR(st_accel_acpi_match),
#ifdef CONFIG_MACH_RP400
		.pm = &st_accel_i2c_pm_ops,
#endif
	},
	.probe = st_accel_i2c_probe,
	.remove = st_accel_i2c_remove,
	.id_table = st_accel_id_table,
};
module_i2c_driver(st_accel_driver);

MODULE_AUTHOR("Denis Ciocca <denis.ciocca@st.com>");
MODULE_DESCRIPTION("STMicroelectronics accelerometers i2c driver");
MODULE_LICENSE("GPL v2");
