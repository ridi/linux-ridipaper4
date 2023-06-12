/*
* Simple driver for Texas Instruments LM3642 LED Flash driver chip
* Copyright (C) 2012 Texas Instruments
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
*/
#ifndef DEBUG
#define DEBUG
#endif

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pwm.h>


#define LM3630A_NAME "leds-lm3630a"

enum lm3630a_leda_ctrl {
	LM3630A_LEDA_DISABLE = 0x00,
	LM3630A_LEDA_ENABLE = 0x04,
	LM3630A_LEDA_ENABLE_LINEAR = 0x14,
};

enum lm3630a_ledb_ctrl {
	LM3630A_LEDB_DISABLE = 0x00,
	LM3630A_LEDB_ON_A = 0x01,
	LM3630A_LEDB_ENABLE = 0x02,
	LM3630A_LEDB_ENABLE_LINEAR = 0x0A,
};

#define LM3630A_MAX_BRIGHTNESS 255

#define REG_CTRL	0x00
#define REG_BOOST	0x02
#define REG_CONFIG	0x01
#define REG_BRT_A	0x03
#define REG_BRT_B	0x04
#define REG_I_A		0x05
#define REG_I_B		0x06
#define REG_ON_OFF_RAMP	0x07
#define REG_INT_STATUS	0x09
#define REG_INT_EN	0x0A
#define REG_FAULT	0x0B
#define REG_PWM_OUTLOW	0x12
#define REG_PWM_OUTHIGH	0x13
#define REG_FILTER_STRENGTH	0x50
#define REG_MAX		0x50

#define LM3630A_MAX_PWM_PERIOD 20000 //50kHz
#define LM3630A_MAX_PWM_DUTY 3000 //15%
#define LM3630A_MIN_PWM_DUTY 200 //1%
#define LM3630A_MAX_DUTY_CYCLE 15 //%

struct lm3630a_chip_data {
	struct device *dev;

	struct led_classdev cdev_bank_a;
	struct led_classdev cdev_bank_b;

	u8 br_bank_a;
	u8 br_bank_b;

	struct regmap *regmap;
	struct mutex lock;

	struct gpio_desc *asel_gpio;
	struct gpio_desc *enable_gpio;
	struct gpio_desc *pwm_gpio;
	struct regulator *vled;
	struct regulator *vio;

	struct pinctrl *pinctrl;
	struct pinctrl_state *pwm_on;
	struct pinctrl_state *pwm_off;

	struct pwm_device *pwm;
	int pwm_duty;
	int pwm_period;
	int pwm_en;
	int pwm_max_duty_cycle;

	int leda_init_brt;
	int leda_max_brt;
	enum lm3630a_leda_ctrl leda_ctrl;

	int ledb_init_brt;
	int ledb_max_brt;
	enum lm3630a_ledb_ctrl ledb_ctrl;

	u8 cr_bank_a;
	u8 cr_bank_b;
	u8 cr_on_ramp;
	u8 cr_off_ramp;
};

/* i2c access */
static int lm3630a_read(struct lm3630a_chip_data *chip, unsigned int reg)
{
	int rval;
	unsigned int reg_val;

	rval = regmap_read(chip->regmap, reg, &reg_val);
	if (rval < 0)
		return rval;
	return reg_val & 0xFF;
}

static int lm3630a_write(struct lm3630a_chip_data *chip, unsigned int reg, unsigned int data)
{
	return regmap_write(chip->regmap, reg, data);
}

static int lm3630a_update(struct lm3630a_chip_data *chip, unsigned int reg, unsigned int mask,
			  unsigned int data)
{
	return regmap_update_bits(chip->regmap, reg, mask, data);
}

static void lm3630a_pwm_ctrl(struct lm3630a_chip_data *chip, int on)
{
#if 0
	int ret;
	
	if(on) {
		pwm_config(chip->pwm, chip->pwm_duty, chip->pwm_period);
		if(chip->pwm_en != on) {
			ret = pwm_enable(chip->pwm);
			dev_err(chip->dev, "pwm_enable: %d\n", ret);
		}
	} else {
		pwm_config(chip->pwm, 0, chip->pwm_period);
		if(chip->pwm_en != on) {
			pwm_disable(chip->pwm);
		}
	}
#endif
	chip->pwm_en = on;
}

/* chip initialize */
static int lm3630a_chip_init(struct lm3630a_chip_data *chip)
{
	int ret;

	usleep_range(1000, 2000);
	/* set Filter Strength Register */
	ret = lm3630a_write(chip, REG_FILTER_STRENGTH, 0x03);
	/* set Cofig. register */
	ret |= lm3630a_write(chip, REG_CONFIG, 0x18);
	/* set boost control */
	ret |= lm3630a_write(chip, REG_BOOST, 0x78);
	/* set current A */
	ret |= lm3630a_write(chip, REG_I_A, 0x07);
	/* set current B */
	ret |= lm3630a_write(chip, REG_I_B, 0x07);
	/* set control */
	ret |= lm3630a_update(chip, REG_CTRL, 0x14, chip->leda_ctrl);
	ret |= lm3630a_update(chip, REG_CTRL, 0x0B, chip->ledb_ctrl);
	ret |= lm3630a_write(chip, REG_ON_OFF_RAMP, 0x09);
	usleep_range(1000, 2000);
	/* set brightness A and B */
	ret |= lm3630a_write(chip, REG_BRT_A, chip->leda_init_brt);
	ret |= lm3630a_write(chip, REG_BRT_B, chip->ledb_init_brt);

	return ret;
}

/* bank_a */
static int lm3630a_bank_a_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	struct lm3630a_chip_data *chip =
	    container_of(cdev, struct lm3630a_chip_data, cdev_bank_a);
	int ret;
	int pwm_duty = 0;

	mutex_lock(&chip->lock);
	chip->br_bank_a = brightness;

	if((chip->br_bank_a == 0x0) && (chip->br_bank_b == 0x0)) {
		lm3630a_pwm_ctrl(chip, 0);
	} else {
		pwm_duty = (float)LM3630A_MAX_PWM_PERIOD * ((float)chip->pwm_max_duty_cycle * 0.01f);
		//dev_err(chip->dev, "pwm_duty : %d\n", pwm_duty);
		chip->pwm_duty = chip->br_bank_a*((pwm_duty-LM3630A_MIN_PWM_DUTY)/LM3630A_MAX_BRIGHTNESS)+LM3630A_MIN_PWM_DUTY;
		lm3630a_pwm_ctrl(chip, 1);
	}
	
	/* disable sleep */
	ret = lm3630a_update(chip, REG_CTRL, 0x80, 0x00);
	if (ret < 0)
		goto out_i2c_err;
	usleep_range(1000, 2000);
	/* minimum brightness is 0x04 */
	if (chip->br_bank_a == 0x0) {
		ret |= lm3630a_update(chip, REG_CTRL, LM3630A_LEDA_ENABLE, 0);
		if (ret < 0)
			goto out_i2c_err;
	} else {
		ret = lm3630a_write(chip, REG_BRT_A, chip->br_bank_a);
		ret |= lm3630a_update(chip, REG_CTRL, LM3630A_LEDA_ENABLE, LM3630A_LEDA_ENABLE);
		if (ret < 0)
			goto out_i2c_err;
	}
	mutex_unlock(&chip->lock);
	dev_err(chip->dev, "%s %d\n", __func__, chip->br_bank_a);
	return 0;

out_i2c_err:
	mutex_unlock(&chip->lock);
	dev_err(chip->dev, "i2c failed to access\n");

	return ret;
}


/* bank_b */
static int lm3630a_bank_b_brightness_set(struct led_classdev *cdev,
					enum led_brightness brightness)
{
	struct lm3630a_chip_data *chip =
		container_of(cdev, struct lm3630a_chip_data, cdev_bank_b);
	int ret;
	int pwm_duty;

	mutex_lock(&chip->lock);
	chip->br_bank_b = brightness;

	if((chip->br_bank_a == 0x0) && (chip->br_bank_b == 0x0)) {
		lm3630a_pwm_ctrl(chip, 0);
	} else {
		pwm_duty = (float)LM3630A_MAX_PWM_PERIOD * ((float)chip->pwm_max_duty_cycle * 0.01f);
		//dev_err(chip->dev, "pwm_duty : %d\n", pwm_duty);
		chip->pwm_duty = chip->br_bank_b*((pwm_duty-LM3630A_MIN_PWM_DUTY)/LM3630A_MAX_BRIGHTNESS)+LM3630A_MIN_PWM_DUTY;
		lm3630a_pwm_ctrl(chip, 1);
	}

	/* disable sleep */
	ret = lm3630a_update(chip, REG_CTRL, 0x80, 0x00);
	if (ret < 0)
		goto out_i2c_err;
	usleep_range(1000, 2000);
	/* minimum brightness is 0x04 */
	if (chip->br_bank_b == 0x0) {
		ret |= lm3630a_update(chip, REG_CTRL, LM3630A_LEDB_ENABLE, 0);
		if (ret < 0)
			goto out_i2c_err;
	} else {
		ret = lm3630a_write(chip, REG_BRT_B, chip->br_bank_b);
		ret |= lm3630a_update(chip, REG_CTRL, LM3630A_LEDB_ENABLE, LM3630A_LEDB_ENABLE);
		if (ret < 0)
			goto out_i2c_err;
	}
	mutex_unlock(&chip->lock);
	dev_err(chip->dev, "%s %d\n", __func__, chip->br_bank_b);
	return 0;

out_i2c_err:
	mutex_unlock(&chip->lock);
	dev_err(chip->dev, "i2c failed to access\n");

	return ret;
}

static ssize_t lm3630a_show_sleep_status(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);
	int enable;

	enable = lm3630a_read(chip, REG_CTRL);

	return sprintf(buf, "%d\n", (enable&0x80)?1:0);
}
static ssize_t lm3630a_store_sleep_status(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);
	int enable;

	sscanf(&buf[0], "%d", &enable);

	if(enable) {
		lm3630a_update(chip, REG_CTRL, 0x80, 0x80);
		lm3630a_pwm_ctrl(chip, 0);
	} else {
		lm3630a_pwm_ctrl(chip, 1);
		lm3630a_update(chip, REG_CTRL, 0x80, 0x00);
	}

	return len;
}
static DEVICE_ATTR(sleep_status, S_IRUGO | S_IWUSR, lm3630a_show_sleep_status, lm3630a_store_sleep_status);

static ssize_t lm3630a_show_pwm_config(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	if(!chip->pwm) {
		return sprintf(buf, "PWM is not supported\n");
	}

	return sprintf(buf, "duty: %d, period: %d\n", chip->pwm_duty, chip->pwm_period);
}
static ssize_t lm3630a_store_pwm_config(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	sscanf(&buf[0], "%d %d", &chip->pwm_duty, &chip->pwm_period);
	if(chip->pwm) {
		pwm_config(chip->pwm, chip->pwm_duty, chip->pwm_period);
	}

	return len;
}
static DEVICE_ATTR(pwm_cfg, S_IRUGO | S_IWUSR, lm3630a_show_pwm_config, lm3630a_store_pwm_config);

static ssize_t lm3630a_show_pwm_max_duty_cycle(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	if(!chip->pwm) {
		return sprintf(buf, "PWM is not supported\n");
	}

	return sprintf(buf, "duty cycle: %d%%\n", chip->pwm_max_duty_cycle);
}
static ssize_t lm3630a_store_max_duty_cycle(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	sscanf(&buf[0], "%d", &chip->pwm_max_duty_cycle);

	return len;
}
static DEVICE_ATTR(pwm_duty_cycle, S_IRUGO | S_IWUSR, lm3630a_show_pwm_max_duty_cycle, lm3630a_store_max_duty_cycle);

static ssize_t lm3630a_show_current_A(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	chip->cr_bank_a = lm3630a_read(chip, REG_I_A) & 0x1F;

	return sprintf(buf, "Current Bank A: 0x%02X\n", chip->cr_bank_a);
}
static ssize_t lm3630a_store_current_A(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	if(!strncmp(buf, "0x", 2)) {
		sscanf(&buf[2], "%x", &chip->cr_bank_a);
	} else {
		sscanf(&buf[0], "%d", &chip->cr_bank_a);
	}

	lm3630a_update(chip, REG_I_A, 0x1F, chip->cr_bank_a);

	return len;
}
static DEVICE_ATTR(current_a, S_IRUGO | S_IWUSR, lm3630a_show_current_A, lm3630a_store_current_A);

static ssize_t lm3630a_show_current_B(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	chip->cr_bank_b = lm3630a_read(chip, REG_I_B) & 0x1F;

	return sprintf(buf, "Current Bank B: 0x%02X\n", chip->cr_bank_b);
}
static ssize_t lm3630a_store_current_B(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	if(!strncmp(buf, "0x", 2)) {
		sscanf(&buf[2], "%x", &chip->cr_bank_b);
	} else {
		sscanf(&buf[0], "%d", &chip->cr_bank_b);
	}

	lm3630a_update(chip, REG_I_B, 0x1F, chip->cr_bank_b);

	return len;
}
static DEVICE_ATTR(current_b, S_IRUGO | S_IWUSR, lm3630a_show_current_B, lm3630a_store_current_B);

static ssize_t lm3630a_show_on_ramping(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	chip->cr_on_ramp = (lm3630a_read(chip, REG_ON_OFF_RAMP) >> 3) & 0x07;

	return sprintf(buf, "Current On Ramp: 0x%02X\n", chip->cr_on_ramp);
}
static ssize_t lm3630a_store_on_ramping(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	if(!strncmp(buf, "0x", 2)) {
		sscanf(&buf[2], "%x", &chip->cr_on_ramp);
	} else {
		sscanf(&buf[0], "%d", &chip->cr_on_ramp);
	}

	lm3630a_update(chip, REG_ON_OFF_RAMP, 0x38, chip->cr_on_ramp << 3);

	return len;
}
static DEVICE_ATTR(current_on_ramp, S_IRUGO | S_IWUSR, lm3630a_show_on_ramping, lm3630a_store_on_ramping);

static ssize_t lm3630a_show_off_ramping(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	chip->cr_off_ramp = lm3630a_read(chip, REG_ON_OFF_RAMP) & 0x07;

	return sprintf(buf, "Current Off Ramp: 0x%02X\n", chip->cr_off_ramp);
}
static ssize_t lm3630a_store_off_ramping(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	if(!strncmp(buf, "0x", 2)) {
		sscanf(&buf[2], "%x", &chip->cr_off_ramp);
	} else {
		sscanf(&buf[0], "%d", &chip->cr_off_ramp);
	}

	lm3630a_update(chip, REG_ON_OFF_RAMP, 0x07, chip->cr_off_ramp);

	return len;
}
static DEVICE_ATTR(current_off_ramp, S_IRUGO | S_IWUSR, lm3630a_show_off_ramping, lm3630a_store_off_ramping);


static struct attribute *lm3630a_attrs[] = {
	&dev_attr_sleep_status.attr,
	&dev_attr_pwm_cfg.attr,
	&dev_attr_pwm_duty_cycle.attr,
	&dev_attr_current_a.attr,
	&dev_attr_current_b.attr,
	&dev_attr_current_on_ramp.attr,
	&dev_attr_current_off_ramp.attr,
	NULL,
};

static struct attribute_group lm3630a_attr_group = {
	.attrs = lm3630a_attrs,
};


static const struct regmap_config lm3630a_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
};

static int lm3630a_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct lm3630a_chip_data *chip;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c functionality check fail.\n");
		return -EOPNOTSUPP;
	}

	chip = devm_kzalloc(&client->dev,
			    sizeof(struct lm3630a_chip_data), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &client->dev;

	chip->regmap = devm_regmap_init_i2c(client, &lm3630a_regmap);
	if (IS_ERR(chip->regmap)) {
		err = PTR_ERR(chip->regmap);
		dev_err(&client->dev, "Failed to allocate register map: %d\n",
			err);
		return err;
	}

	chip->asel_gpio = devm_gpiod_get(&client->dev, "asel", GPIOD_OUT_LOW);
	if (IS_ERR(chip->asel_gpio)) {
		err = PTR_ERR(chip->asel_gpio);
		dev_err(&client->dev, "Faichip to get asel gpio: %d\n", err);
		return err;
	}
	gpiod_set_value(chip->asel_gpio, 0); //I2C Address -> 36h

	chip->enable_gpio = devm_gpiod_get(&client->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(chip->enable_gpio)) {
		err = PTR_ERR(chip->enable_gpio);
		dev_err(&client->dev, "Faichip to get enable gpio: %d\n", err);
		return err;
	}
	gpiod_set_value(chip->enable_gpio, 0);

	chip->vled = devm_regulator_get(&client->dev, "vled");
	if (IS_ERR(chip->vled)) {
		err = PTR_ERR(chip->vled);
		dev_err(&client->dev, "Failed to get regulator vled %d\n", err);
		return err;
	}

	chip->vio = devm_regulator_get(&client->dev, "vio");
	if (IS_ERR(chip->vio)) {
		err = PTR_ERR(chip->vio);
		dev_err(&client->dev, "Failed to get regulator vio %d\n", err);
		return err;
	}

	/* default values */
#if 0 //default exponential
	chip->leda_ctrl = LM3630A_LEDA_ENABLE_LINEAR;
	chip->ledb_ctrl = LM3630A_LEDB_ENABLE_LINEAR;
#endif
	chip->leda_max_brt = LM3630A_MAX_BRIGHTNESS;
	chip->ledb_max_brt = LM3630A_MAX_BRIGHTNESS;
	chip->leda_init_brt = 0;
	chip->ledb_init_brt = 0;

	regulator_set_voltage(chip->vled, 3300000, 3300000);
	regulator_enable(chip->vled);
	regulator_set_voltage(chip->vio, 1800000, 1800000);
	regulator_enable(chip->vio);
	gpiod_set_value(chip->enable_gpio, 1);

	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);

	err = lm3630a_chip_init(chip);
	if (err < 0)
		return err;

	/* Bank A  */
	chip->cdev_bank_a.name = "Bank_A";
	chip->cdev_bank_a.max_brightness = LM3630A_MAX_BRIGHTNESS;
	chip->cdev_bank_a.brightness_set_blocking = lm3630a_bank_a_brightness_set;
	chip->cdev_bank_a.default_trigger = "bank_a";
	err = led_classdev_register((struct device *)&client->dev, &chip->cdev_bank_a);
	if (err < 0) {
		dev_err(chip->dev, "failed to register bank_a\n");
		return err;
	}

	/* Bank B */
	chip->cdev_bank_b.name = "Bank_B";
	chip->cdev_bank_b.max_brightness = LM3630A_MAX_BRIGHTNESS;
	chip->cdev_bank_b.brightness_set_blocking = lm3630a_bank_b_brightness_set;
	chip->cdev_bank_b.default_trigger = "bank_b";
	err = led_classdev_register((struct device *)&client->dev, &chip->cdev_bank_b);
	if (err < 0) {
		dev_err(chip->dev, "failed to register bank_b\n");
		return err;
	}

	err = sysfs_create_group(&client->dev.kobj, &lm3630a_attr_group);
	if (err) {
		dev_err(&client->dev, "Unable to create group, error: %d\n", err);
		return err;
	}

	//PWM Duty cycle 50kHz
	chip->pwm_en = 0;
	chip->pwm_duty = LM3630A_MAX_PWM_DUTY;
	chip->pwm_period = LM3630A_MAX_PWM_PERIOD;
	chip->pwm_max_duty_cycle = LM3630A_MAX_DUTY_CYCLE;
#if 0
	chip->pinctrl = devm_pinctrl_get(&client->dev);
	if(IS_ERR(chip->pinctrl)) {
		dev_err(&client->dev, "failed to get pwm-0 pinctrl\n");
	} else {
		chip->pwm_on = pinctrl_lookup_state(chip->pinctrl, "pwm_on");
		chip->pwm_off = pinctrl_lookup_state(chip->pinctrl, "pwm_off");
		if(!IS_ERR(chip->pwm_on) && !IS_ERR(chip->pwm_off)) {
			if(pinctrl_select_state(chip->pinctrl, chip->pwm_on)) {
				dev_err(&client->dev, "failed to turn on pwm\n");
			} else {
				chip->pwm = pwm_request(0, "lm3630a_pwm");
				if(IS_ERR(chip->pwm)) {
					err = PTR_ERR(chip->pwm);
					dev_err(&client->dev, "unable to request PWM for LED, error: %d\n", err);
					chip->pwm = NULL;
				}
			}
		} else {
			dev_err(&client->dev, "failed to get pwm_on or pwm_off pin state\n");
		}
	}
#else
	chip->pwm_gpio = devm_gpiod_get(&client->dev, "pwm", GPIOD_IN);
	if (IS_ERR(chip->pwm_gpio)) {
		err = PTR_ERR(chip->pwm_gpio);
		dev_err(&client->dev, "Faichip to get pwm gpio: %d\n", err);
		return err;
	}
#endif

	dev_info(&client->dev, "LM3630A is initialized\n");
	return 0;
}

static int lm3630a_remove(struct i2c_client *client)
{
	struct lm3630a_chip_data *chip = i2c_get_clientdata(client);

	led_classdev_unregister(&chip->cdev_bank_b);
	led_classdev_unregister(&chip->cdev_bank_a);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int lm3630a_suspend(struct device *dev)
{
	struct lm3630a_chip_data *chip = dev_get_drvdata(dev);

	dev_info(dev, "%s\n", __func__);

	/* enable sleep */
	lm3630a_update(chip, REG_CTRL, 0x80, 0x80);
	lm3630a_pwm_ctrl(chip, 0);

	return 0;
}

static int lm3630a_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops lm3630a_pm_ops = {
	.suspend		= lm3630a_suspend,
	.resume			= lm3630a_resume,
};
#endif

static const struct i2c_device_id lm3630a_id[] = {
	{LM3630A_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, lm3630a_id);

static const struct of_device_id of_lm3630a_leds_match[] = {
	{ .compatible = "ti,lm3630a", },
	{},
};
MODULE_DEVICE_TABLE(of, of_lm3630a_leds_match);


static struct i2c_driver lm3630a_i2c_driver = {
	.driver = {
		   .name = LM3630A_NAME,
#ifdef CONFIG_PM_SLEEP
		   .pm = &lm3630a_pm_ops,
#endif
		   .of_match_table = of_lm3630a_leds_match,
		   },
	.probe = lm3630a_probe,
	.remove = lm3630a_remove,
	.id_table = lm3630a_id,
};

module_i2c_driver(lm3630a_i2c_driver);

MODULE_DESCRIPTION("Texas Instruments Flash Lighting driver for LM3630A");
MODULE_AUTHOR("Daniel Jeong <daniel.jeong@ti.com>");
MODULE_AUTHOR("G.Shark Jeong <gshark.jeong@gmail.com>");
MODULE_AUTHOR("Terry Choi <dw.choi@e-roum.com>");
MODULE_LICENSE("GPL v2");
