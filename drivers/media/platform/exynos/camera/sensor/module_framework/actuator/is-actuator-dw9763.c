/*
 * Samsung Exynos9 SoC series Actuator driver
 *
 *
 * Copyright (c) 2018 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>

#include "is-actuator-dw9763.h"
#include "is-device-sensor.h"
#include "is-device-sensor-peri.h"
#include "is-core.h"
#include "is-helper-actuator-i2c.h"

#define ACTUATOR_NAME		"DW9763"

// regs start
#define REG_IC_INFO		0x00 /*[7:4] IC Manufacturer ID, [3:0] IC Model*/
#define REG_IC_VER		0x01 /*[3:0] Design Round*/

/*
 * Default 0x00,
 * [1] 0: Direct mode, 1: Ringing control mode
 * [0] 0: Normal operation mode, 1: Power down mode (active HIGH)
 */
#define REG_CTRL_MODE	0x02 /*[1] RING, [0] PD*/

/*
 * 10bit pos, pos[9:0]
 * DAC data input Output current = (D[9:0]/1023) X 100mA
 */
#define REG_POS_MSB	    0x03 /*[1:0], pos[9:8]*/
#define REG_POS_LSB	    0x04 /*[7:0], pos[7:0]*/

#define REG_STATUS	    0x05 /*[1]MBUSY, [0]VBUSY*/

/*PRESC0, SACT5 = Default value is ‘1’*/
#define REG_RING_MODE   0x06 /*[7:5]-->[SAC2:SAC0], [2:0]-->[PRESC2:PRESC0]*/
#define REG_RESONANCE_FREQ   0x07 /*[5:0]-->[SACT5:SACT0]*/
// regs end

#define DEF_DW9763_FIRST_POSITION		15
#define DEF_DW9763_FIRST_DELAY			10

extern struct is_sysfs_actuator sysfs_actuator;

static int sensor_dw9763_write_position(struct i2c_client *client, u32 val)
{
	int ret = 0;
	u8 val_high = 0, val_low = 0;

	FIMC_BUG(!client);

	if (!client->adapter) {
		err("Could not find adapter!\n");
		ret = -ENODEV;
		goto p_err;
	}

	if (val > DW9763_POS_MAX_SIZE) {
		err("Invalid af position(position : %d, Max : %d).\n",
					val, DW9763_POS_MAX_SIZE);
		ret = -EINVAL;
		goto p_err;
	}

	/*
     * pos[9:0]
	 */
	val_high = (val & 0x300) >> 8;
	val_low = val & 0xFF;

	ret = is_sensor_addr8_write8(client, REG_POS_MSB, val_high);
	ret |= is_sensor_addr8_write8(client, REG_POS_LSB, val_low);
	if (ret < 0) {
		err("fail to set VCM pos\n");
	}

p_err:
	return ret;
}

static int sensor_dw9763_valid_check(struct i2c_client * client)
{
	int i;

	FIMC_BUG(!client);

	if (sysfs_actuator.init_step > 0) {
		for (i = 0; i < sysfs_actuator.init_step; i++) {
			if (sysfs_actuator.init_positions[i] < 0) {
				warn("invalid position value, default setting to position");
				return 0;
			} else if (sysfs_actuator.init_delays[i] < 0) {
				warn("invalid delay value, default setting to delay");
				return 0;
			}
		}
	} else
		return 0;

	return sysfs_actuator.init_step;
}

static void sensor_dw9763_print_log(int step)
{
	int i;

	if (step > 0) {
		dbg_actuator("initial position ");
		for (i = 0; i < step; i++) {
			dbg_actuator(" %d", sysfs_actuator.init_positions[i]);
		}
		dbg_actuator(" setting");
	}
}


static int sensor_dw9763_init_position(struct i2c_client *client,
		struct is_actuator *actuator)
{
	int i;
	int ret = 0;
	int init_step = 0;

	init_step = sensor_dw9763_valid_check(client);

	if (init_step > 0) {
		for (i = 0; i < init_step; i++) {
			ret = sensor_dw9763_write_position(client, sysfs_actuator.init_positions[i]);
			if (ret < 0)
				goto p_err;

			msleep(sysfs_actuator.init_delays[i]);
		}

		actuator->position = sysfs_actuator.init_positions[i];

		sensor_dw9763_print_log(init_step);

	} else {
		ret = sensor_dw9763_write_position(client, DEF_DW9763_FIRST_POSITION);
		if (ret < 0)
			goto p_err;

		msleep(DEF_DW9763_FIRST_DELAY);

		actuator->position = DEF_DW9763_FIRST_POSITION;

		dbg_actuator("initial position %d setting\n", DEF_DW9763_FIRST_POSITION);
	}

p_err:
	return ret;
}

int sensor_dw9763_actuator_init(struct v4l2_subdev *subdev, u32 val)
{
	int ret = 0;
	u8 reg_val = 0;
	struct is_actuator *actuator;
	struct i2c_client *client = NULL;
#ifdef DEBUG_ACTUATOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);

	dbg_actuator("%s\n", __func__);

	actuator = (struct is_actuator *)v4l2_get_subdevdata(subdev);
	if (!actuator) {
		err("actuator is not detect!\n");
		goto p_err;
	}

	client = actuator->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	usleep_range(200, 500);
	ret = is_sensor_addr8_write8(client, REG_CTRL_MODE, 0x01);
	ret = is_sensor_addr8_write8(client, REG_CTRL_MODE, 0x00);
	if (ret < 0)
		goto p_err;

	usleep_range(200, 500);

	/* read chip id */
	ret = is_sensor_addr8_read8(client, REG_IC_INFO, &reg_val);
	if (ret < 0) {
		err("read dw9763 chip info failed\n");
		goto p_err;
	} else {
		pr_info("dw9763 chip info: 0x%x", reg_val);
	}

	ret = sensor_dw9763_init_position(client, actuator);
	if (ret <0)
		goto p_err;

#ifdef DEBUG_ACTUATOR_TIME
	do_gettimeofday(&end);
	pr_info("[%s] time %lu us", __func__, (end.tv_sec - st.tv_sec) * 1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_dw9763_actuator_get_status(struct v4l2_subdev *subdev, u32 *info)
{
	int ret = 0;
	u8 val = 0;
	struct is_actuator *actuator = NULL;
	struct i2c_client *client = NULL;
#ifdef DEBUG_ACTUATOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	dbg_actuator("%s\n", __func__);

	FIMC_BUG(!subdev);
	FIMC_BUG(!info);

	actuator = (struct is_actuator *)v4l2_get_subdevdata(subdev);
	FIMC_BUG(!actuator);

	client = actuator->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	/*
	 * [1]MBUSY, [0]VBUSY
	 * MBUSY bit must be checked ‘0’ before eFlash Write or Erase command
	 * During eFlash Write/Erase operation, MBUSY bit keep ‘1’
	 * While MBUSY = ‘1’, the I2C command for eFlash is ignored.
	 *
	 * VBUSY bit must be checked ‘0’ before “VCM MSB and LSB” registers are written
	 * During SRC (SAC) operation, VBUSY bit keep ‘1’
	 * While VBUSY = ‘1’, the I2C command for VCM is ignored.
	 */
	ret = is_sensor_addr8_read8(client, REG_STATUS, &val);

	// to judge VBUSY
	*info = (((val) & 0x01) == 0) ? ACTUATOR_STATUS_NO_BUSY : ACTUATOR_STATUS_BUSY;
#ifdef DEBUG_ACTUATOR_TIME
	do_gettimeofday(&end);
	pr_info("[%s] time %lu us", __func__, (end.tv_sec - st.tv_sec) * 1000000 + (end.tv_usec - st.tv_usec));
#endif

p_err:
	return ret;
}

int sensor_dw9763_actuator_set_position(struct v4l2_subdev *subdev, u32 *info)
{
	int ret = 0;
	struct is_actuator *actuator;
	struct i2c_client *client;
	u32 position = 0;
#ifdef DEBUG_ACTUATOR_TIME
	struct timeval st, end;
	do_gettimeofday(&st);
#endif

	FIMC_BUG(!subdev);
	FIMC_BUG(!info);

	actuator = (struct is_actuator *)v4l2_get_subdevdata(subdev);
	FIMC_BUG(!actuator);

	client = actuator->client;
	if (unlikely(!client)) {
		err("client is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	position = *info;
	if (position > DW9763_POS_MAX_SIZE) {
		err("Invalid af position(position : %d, Max : %d).\n",
					position, DW9763_POS_MAX_SIZE);
		ret = -EINVAL;
		goto p_err;
	}

	/* debug option : fixed position testing */
	if (sysfs_actuator.enable_fixed)
		position = sysfs_actuator.fixed_position;

	/* position Set */
	ret = sensor_dw9763_write_position(client, position);
	if (ret <0)
		goto p_err;
	actuator->position = position;

	dbg_actuator("%s: position(%d)\n", __func__, position);

#ifdef DEBUG_ACTUATOR_TIME
	do_gettimeofday(&end);
	pr_info("[%s] time %lu us", __func__, (end.tv_sec - st.tv_sec) * 1000000 + (end.tv_usec - st.tv_usec));
#endif
p_err:
	return ret;
}

static int sensor_dw9763_actuator_g_ctrl(struct v4l2_subdev *subdev, struct v4l2_control *ctrl)
{
	int ret = 0;
	u32 val = 0;

	switch(ctrl->id) {
	case V4L2_CID_ACTUATOR_GET_STATUS:
		ret = sensor_dw9763_actuator_get_status(subdev, &val);
		if (ret < 0) {
			err("err!!! ret(%d), actuator status(%d)", ret, val);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	default:
		err("err!!! Unknown CID(%#x)", ctrl->id);
		ret = -EINVAL;
		goto p_err;
	}

	ctrl->value = val;

p_err:
	return ret;
}

static int sensor_dw9763_actuator_s_ctrl(struct v4l2_subdev *subdev, struct v4l2_control *ctrl)
{
	int ret = 0;

	switch(ctrl->id) {
	case V4L2_CID_ACTUATOR_SET_POSITION:
		ret = sensor_dw9763_actuator_set_position(subdev, &ctrl->value);
		if (ret) {
			err("failed to actuator set position: %d, (%d)\n", ctrl->value, ret);
			ret = -EINVAL;
			goto p_err;
		}
		break;
	default:
		err("err!!! Unknown CID(%#x)", ctrl->id);
		ret = -EINVAL;
		goto p_err;
	}

p_err:
	return ret;
}

static const struct v4l2_subdev_core_ops core_ops = {
	.init = sensor_dw9763_actuator_init,
	.g_ctrl = sensor_dw9763_actuator_g_ctrl,
	.s_ctrl = sensor_dw9763_actuator_s_ctrl,
};

static const struct v4l2_subdev_ops subdev_ops = {
	.core = &core_ops,
};

static int sensor_dw9763_actuator_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = 0;
	struct is_core *core= NULL;
	struct v4l2_subdev *subdev_actuator = NULL;
	struct is_actuator *actuator = NULL;
	struct is_device_sensor *device = NULL;
	struct is_device_sensor_peri *sensor_peri = NULL;
	u32 sensor_id = 0;
	u32 place = 0;
	struct device *dev;
	struct device_node *dnode;

	FIMC_BUG(!is_dev);
	FIMC_BUG(!client);

	core = (struct is_core *)dev_get_drvdata(is_dev);
	if (!core) {
		probe_err("core device is not yet probed");
		ret = -EPROBE_DEFER;
		goto p_err;
	}

	dev = &client->dev;
	dnode = dev->of_node;

	ret = of_property_read_u32(dnode, "id", &sensor_id);
	if (ret) {
		probe_err("id read is fail(%d)", ret);
		goto p_err;
	}

	ret = of_property_read_u32(dnode, "place", &place);
	if (ret) {
		probe_err("place read is fail(%d)", ret);
		place = 0;
	}
	probe_info("%s sensor_id(%d) actuator_place(%d)\n", __func__, sensor_id, place);

	device = &core->sensor[sensor_id];
	if (!test_bit(IS_SENSOR_PROBE, &device->state)) {
		probe_err("sensor device is not yet probed");
		ret = -EPROBE_DEFER;
		goto p_err;
	}

	sensor_peri = find_peri_by_act_id(device, ACTUATOR_NAME_DW9763);
	if (!sensor_peri) {
		probe_err("sensor peri is net yet probed");
		return -EPROBE_DEFER;
	}

	actuator = kzalloc(sizeof(struct is_actuator), GFP_KERNEL);
	if (!actuator) {
		probe_err("acuator is NULL");
		ret = -ENOMEM;
		goto p_err;
	}

	subdev_actuator = kzalloc(sizeof(struct v4l2_subdev), GFP_KERNEL);
	if (!subdev_actuator) {
		probe_err("subdev_actuator is NULL");
		ret = -ENOMEM;
		goto p_err;
	}
	sensor_peri->subdev_actuator = subdev_actuator;

	/* This name must is match to sensor_open_extended actuator name */
	actuator->id = ACTUATOR_NAME_DW9763;
	actuator->subdev = subdev_actuator;
	actuator->device = sensor_id;
	actuator->client = client;
	actuator->position = 0;
	actuator->max_position = DW9763_POS_MAX_SIZE;
	actuator->pos_size_bit = DW9763_POS_SIZE_BIT;
	actuator->pos_direction = DW9763_POS_DIRECTION;

	device->subdev_actuator[sensor_id] = subdev_actuator;
	device->actuator[sensor_id] = actuator;

	v4l2_i2c_subdev_init(subdev_actuator, client, &subdev_ops);
	v4l2_set_subdevdata(subdev_actuator, actuator);
	v4l2_set_subdev_hostdata(subdev_actuator, device);

	set_bit(IS_SENSOR_ACTUATOR_AVAILABLE, &sensor_peri->peri_state);

	snprintf(subdev_actuator->name, V4L2_SUBDEV_NAME_SIZE, "actuator-subdev.%d", actuator->id);

	probe_info("%s done\n", __func__);
	return ret;

p_err:
	if (subdev_actuator)
		kzfree(subdev_actuator);

	return ret;
}

static const struct of_device_id sensor_actuator_dw9763_match[] = {
	{
		.compatible = "samsung,exynos-is-actuator-dw9763",
	},
	{},
};
MODULE_DEVICE_TABLE(of, sensor_actuator_dw9763_match);

static const struct i2c_device_id sensor_actuator_dw9763_idt[] = {
	{ ACTUATOR_NAME, 0 },
	{},
};

static struct i2c_driver sensor_actuator_dw9763_driver = {
	.probe  = sensor_dw9763_actuator_probe,
	.driver = {
		.name	= ACTUATOR_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = sensor_actuator_dw9763_match,
		.suppress_bind_attrs = true,
	},
	.id_table = sensor_actuator_dw9763_idt,
};

static int __init sensor_actuator_dw9763_init(void)
{
	int ret;

	ret = i2c_add_driver(&sensor_actuator_dw9763_driver);
	if (ret)
		err("failed to add %s driver: %d\n",
			sensor_actuator_dw9763_driver.driver.name, ret);

	return ret;
}
late_initcall_sync(sensor_actuator_dw9763_init);
