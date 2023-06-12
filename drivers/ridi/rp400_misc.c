/*
 * RP-400 misc driver
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

#ifndef DEBUG
#define DEBUG
#endif

#include <linux/init.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/iio/consumer.h>
#include <linux/usb.h>
#include <linux/regulator/consumer.h>
#include <linux/pwm.h>
#include <linux/wakelock.h>
#include <linux/muic/s2mu106-muic.h>
#include <linux/power/s2m_chg_manager.h>
#include <linux/firmware.h>

////////////////////////////////////////////////////////////////////

#define DEV_NAME "rp400_misc"

#define IOCTL_RP400_MISC_BASE 0x1000
#define IOCTL_RP400_MISC_CHECK_USB_DETECT (IOCTL_RP400_MISC_BASE + 1)

#define EPD_TEST_IMAGE_PATH "/data/system/epd/wakeup_noti.data"


#define EPD_DISABLED 0
#define EPD_AUTO 1
#define EPD_MANUAL 2

#define EPD_USB_CONN 1
#define EPD_CHARGER_CONN 2
#define EPD_LOW_BATTERY 3
#define EPD_FULL_BATTERY 4
#define EPD_SUSPEND 5
#define EPD_RESUME 6

#define GMA303_CALI_Z 0

extern void it8951DisplayTest0(void);
extern void it8951DisplayTest1(void);
extern void it8951DisplayUserData(u32 x, u32 y, u32 w, u32 h, char *path);
extern void it8951DisplayArea(u32 x, u32 y, u32 w, u32 h, u16 mode);
extern void set_it8951_reset(int reset);
extern void set_it8951_speed(int speed_hz);
extern int get_it8951_reset(void);
extern int get_it8951_hrdy_status(void);
extern void it8951DisplayClear(u8 value);
extern void it8951DisplayInit(void);
extern void it8951DisplayFWflash(u8* buf);
extern void it8951DisplayInitClear(void);
extern void it8951DisplayPowerOnOff(int on);
extern void it8951GetVersion(char* version);
extern int s2mpu12_read_reg(struct i2c_client *i2c, u8 reg, u8 *dest);
extern int s2mpu12_write_reg(struct i2c_client *i2c, u8 reg, u8 value);
extern void rp400_spi_dump_reg(void);
extern int gma303_read_xyz(s16 *x, s16 *y, s16 *z);
extern void dsim_powerup(void);
extern void dsim_powerdown(void);
extern int dsim_power_status(void);
extern void IT8951DisplayWakeup(void);
extern void IT8951DisplaySleep(void);
extern void IT8951DisplaySetVCOM(int vcom);
extern void s2mu106_muic_get_detect_info(struct s2mu106_muic_data *muic_data);
extern int set_rp400_battery_status(struct s2m_chg_manager_info *battery, int status);
extern int set_rp400_charging_current(struct s2m_chg_manager_info *battery, int max, int now);

extern int battery_status;
extern struct s2mu106_muic_data *g_muic_data;
extern struct s2m_chg_manager_info *g_battery;
extern s16 gma303_cali_x;
extern s16 gma303_cali_y;
extern s16 gma303_cali_z;
extern float gma303_slope_x;
extern float gma303_slope_y;
extern float gma303_slope_z;


struct rp400_misc_data {
	struct device *dev;
	dev_t devt;
	struct list_head device_entry;
	int users;

	int epd_test;
	int epd_ready;
	int fb_ready_count;
	int epd_mode;
	int epd_fw_update;
	int epd_speed;
	int epd_pwr_ctrl;
	int epd_spi_sleep;

	struct mutex fb_cnt_lock;
#if 0
	struct work_struct work;
	struct timer_list timer;
#endif
	u8 pmic_ldo_reg_addr;
	u8 pmic_ldo_reg_val;

	struct gpio_desc *gpiod_usb_select;
	struct gpio_desc *gpiod_mipi_enable;
	int usb_select;
	int mipi_enable;

	struct regulator *vldo16;
	struct regulator *vldo31;
	struct regulator *vldo34;

	struct wake_lock wake_lock;
	int wake_lock_on;
};

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static int epd_hw_test_mode = 0;
static int epd_usb_mode = 1; //0->device, 1->host
static int rp400_debug_mode = 0; //1b->Battery
static int epd_fb_count = 0;
static int rp400_usb_detected = 0;
static int rp400_interrupt_event = 0;
static int check_usb_detected_init = 0;
static struct completion check_usb_detected;
static int rp400_usb_lock = 0;
static int rp400_powerkey_lock = 0;

////////////////////////////////////////////////////////////////////

int get_epd_hw_test_mode(void)
{
	return epd_hw_test_mode;
}

int get_epd_usb_role(void)
{
	return epd_usb_mode;
}

void set_epd_usb_role(int mode)
{
	epd_usb_mode = !!mode;
}

int get_rp400_debug_mode(void)
{
	return rp400_debug_mode;
}

void increase_epd_fb_count(void)
{
	epd_fb_count++;
}

void set_rp400_usb_detect(int detect)
{
	if(detect == EPD_LOW_BATTERY) {
		rp400_interrupt_event = EPD_LOW_BATTERY;
		if(check_usb_detected_init)
			complete(&check_usb_detected);
		return;
	}

	if(detect == EPD_FULL_BATTERY) {
		rp400_interrupt_event = EPD_FULL_BATTERY;
		if(check_usb_detected_init)
			complete(&check_usb_detected);
		return;
	}

	if(rp400_usb_detected == detect)
		return;
	rp400_usb_detected = detect;

	if(check_usb_detected_init)
		complete(&check_usb_detected);
}

int get_rp400_usb_detect(void)
{
	return rp400_usb_detected;
}

int get_rp400_usb_lock(void)
{
	return rp400_usb_lock;
}

int get_rp400_powerkey_lock(void)
{
	return rp400_powerkey_lock;
}

////////////////////////////////////////////////////////////////////

static long rp400_misc_drv_ioctl (struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct rp400_misc_data *data;
	int usb_detect = 0;

	data = filp->private_data;

	switch(cmd)
	{
		case IOCTL_RP400_MISC_CHECK_USB_DETECT:
		{
			wait_for_completion_interruptible(&check_usb_detected);

			if(rp400_interrupt_event != 0) {
				if(copy_to_user((int *)arg, &rp400_interrupt_event, sizeof(int))) {
					pr_err("IOCTL_RP400_MISC_CHECK_USB_DETECT copy_from_user error\n");
					ret = -EINVAL;
					break;
				}
				rp400_interrupt_event = 0;
			} else {
				if(!rp400_usb_lock)
					usb_detect = rp400_usb_detected;
				if(copy_to_user((int *)arg, &usb_detect, sizeof(int))) {
					pr_err("IOCTL_RP400_MISC_CHECK_USB_DETECT copy_from_user error\n");
					ret = -EINVAL;
					break;
				}
			}
		}
		break;
		default:
		{
			pr_err("\033[31m\033[1m[%s] unknown cmd : 0x%X\033[0m\r\n",__FUNCTION__,cmd);
			ret = -EINVAL;
		}
	}

	return ret;
}

static int rp400_misc_drv_open(struct inode *inode, struct file *filp)
{
	struct rp400_misc_data *data;
	int status = -ENXIO;
	
	mutex_lock(&device_list_lock);

	list_for_each_entry(data, &device_list, device_entry) {
		if (data->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}

	if(status == 0) {
		data->users++;
		filp->private_data = data;
		nonseekable_open(inode, filp);
	}

	mutex_unlock(&device_list_lock);
	
	return 0;
}

static int rp400_misc_drv_release(struct inode *inode, struct file *filp)
{
	struct rp400_misc_data *data;

	mutex_lock(&device_list_lock);

	data = filp->private_data;
	filp->private_data = NULL;
	data->users--;

	mutex_unlock(&device_list_lock);

	return 0;
}

static  struct file_operations rp400_misc_drv_fops =
{
	.owner				= THIS_MODULE,
	.unlocked_ioctl		= rp400_misc_drv_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= rp400_misc_drv_ioctl,
#endif
	.open				= rp400_misc_drv_open,
	.release 			= rp400_misc_drv_release,
};

static ssize_t rp400_show_epd_test(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->epd_test);
}
static ssize_t rp400_store_epd_test(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);

	sscanf(&buf[0], "%d", &data->epd_test);

	if(data->epd_test == 1) {
		it8951DisplayTest0();
	} else if(data->epd_test == 2) {
		it8951DisplayTest1();
	} else if(data->epd_test == 3) {
		it8951DisplayUserData(48, 282, 32, 700, EPD_TEST_IMAGE_PATH);
	}

	return len;
}
static DEVICE_ATTR(epd_test, S_IRUGO | S_IWUSR, rp400_show_epd_test, rp400_store_epd_test);

static ssize_t rp400_show_epd_hrdy(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", get_it8951_hrdy_status());
}
static DEVICE_ATTR(epd_hrdy, S_IRUGO | S_IWUSR, rp400_show_epd_hrdy, NULL);

static ssize_t rp400_show_epd_power_control(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->epd_pwr_ctrl);
}
static ssize_t rp400_store_epd_power_control(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	int pwr_ctl = 0;
	sscanf(&buf[0], "%d", &pwr_ctl);

	data->epd_pwr_ctrl = !!pwr_ctl;
	it8951DisplayPowerOnOff(data->epd_pwr_ctrl);

	return len;
}
static DEVICE_ATTR(epd_pwr_ctrl, S_IRUGO | S_IWUSR, rp400_show_epd_power_control, rp400_store_epd_power_control);

static ssize_t rp400_show_epd_reset(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", get_it8951_reset());
}
static ssize_t rp400_store_epd_reset(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int reset = 0;
	sscanf(&buf[0], "%d", &reset);

	reset = !!reset;
	set_it8951_reset(reset);

	return len;
}
static DEVICE_ATTR(epd_reset, S_IRUGO | S_IWUSR, rp400_show_epd_reset, rp400_store_epd_reset);

static ssize_t rp400_store_epd_drv_init(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	it8951DisplayInit();
	
	return len;
}
static DEVICE_ATTR(epd_drv_init, S_IRUGO | S_IWUSR, NULL, rp400_store_epd_drv_init);

static ssize_t rp400_store_epd_display_init(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	it8951DisplayInitClear();

	return len;
}
static DEVICE_ATTR(epd_display_init, S_IRUGO | S_IWUSR, NULL, rp400_store_epd_display_init);


static ssize_t rp400_show_epd_ready(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->epd_ready);
}
static ssize_t rp400_store_epd_ready(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	int ready = 0;

	sscanf(&buf[0], "%d", &ready);

	data->epd_ready = !!ready;

	return len;
}
static DEVICE_ATTR(epd_ready, S_IRUGO | S_IWUSR, rp400_show_epd_ready, rp400_store_epd_ready);

static ssize_t rp400_store_epd_clear(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	u8 value;

	sscanf(&buf[0], "%d", &value);

	it8951DisplayClear(value);

	return len;
}
static DEVICE_ATTR(epd_clear, S_IRUGO | S_IWUSR, NULL, rp400_store_epd_clear);

static ssize_t rp400_show_fb_ready_count(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->fb_ready_count);
}
static ssize_t rp400_store_fb_ready_count(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->fb_cnt_lock);
	sscanf(&buf[0], "%d", &data->fb_ready_count);
	mutex_unlock(&data->fb_cnt_lock);

	return len;
}
static DEVICE_ATTR(fb_ready_count, S_IRUGO | S_IWUSR, rp400_show_fb_ready_count, rp400_store_fb_ready_count);

static ssize_t rp400_show_epd_mode(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->epd_mode);
}
static ssize_t rp400_store_epd_mode(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->fb_cnt_lock);
	sscanf(&buf[0], "%d", &data->epd_mode);
	mutex_unlock(&data->fb_cnt_lock);

	return len;
}
static DEVICE_ATTR(epd_mode, S_IRUGO | S_IWUSR, rp400_show_epd_mode, rp400_store_epd_mode);

static ssize_t rp400_show_epd_fw_update(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->epd_fw_update);
}
static ssize_t rp400_store_epd_fw_update(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	int update;

	sscanf(&buf[0], "%d", &update);
	data->epd_fw_update = !!update;

	if(data->epd_fw_update) {
		u8* buf = vmalloc(1024*1024);
		if(!buf) {
			dev_err(data->dev, "not enough memory!\n");
			return len;
		}
		it8951DisplayFWflash(buf);
		vfree(buf);
	}
	data->epd_fw_update = 0;

	return len;
}
static DEVICE_ATTR(epd_fw_update, S_IRUGO | S_IWUSR, rp400_show_epd_fw_update, rp400_store_epd_fw_update);

static ssize_t rp400_show_epd_speed(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->epd_speed);
}
static ssize_t rp400_store_epd_speed(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	int speed;

	sscanf(&buf[0], "%d", &speed);
	if(speed > 0) {
		data->epd_speed = speed;
		set_it8951_speed(speed);
	}

	return len;
}
static DEVICE_ATTR(epd_speed, S_IRUGO | S_IWUSR, rp400_show_epd_speed, rp400_store_epd_speed);

static ssize_t rp400_show_epd_spi_dump_reg(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	rp400_spi_dump_reg();
	return sprintf(buf, "\n");
}
static DEVICE_ATTR(epd_spi_dump_reg, S_IRUGO | S_IWUSR, rp400_show_epd_spi_dump_reg, NULL);

static ssize_t rp400_show_epd_version(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	char version[16] = {0, };
	it8951GetVersion(version);
	return sprintf(buf, "%s\n", version);
}
static DEVICE_ATTR(epd_version, S_IRUGO | S_IWUSR, rp400_show_epd_version, NULL);

static ssize_t rp400_store_debug_message(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	printk("%s", buf);
	return len;
}
static DEVICE_ATTR(debug_msg, S_IRUGO | S_IWUSR, NULL, rp400_store_debug_message);

static ssize_t rp400_show_pmic_ldo_reg(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	struct i2c_client i2c;

	i2c.addr = 0x1;
	s2mpu12_read_reg(&i2c, data->pmic_ldo_reg_addr, &data->pmic_ldo_reg_val);

	return sprintf(buf, "[%02X] = %02X\n", data->pmic_ldo_reg_addr, data->pmic_ldo_reg_val);
}
static ssize_t rp400_store_pmic_ldo_reg(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	struct i2c_client i2c;

	sscanf(&buf[0], "%02x %02x", &data->pmic_ldo_reg_addr, &data->pmic_ldo_reg_val);
	if(len > 3) {
		i2c.addr = 0x1;
		s2mpu12_write_reg(&i2c, data->pmic_ldo_reg_addr, data->pmic_ldo_reg_val);
	}

	return len;
}
static DEVICE_ATTR(pmic_ldo_reg, S_IRUGO | S_IWUSR, rp400_show_pmic_ldo_reg, rp400_store_pmic_ldo_reg);

static ssize_t rp400_show_hw_test_mode(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", epd_hw_test_mode);
}
static ssize_t rp400_store_hw_test_mode(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int hw_test_mode;

	sscanf(&buf[0], "%d", &hw_test_mode);
	epd_hw_test_mode = !!hw_test_mode;

	return len;
}
static DEVICE_ATTR(hw_test_mode, S_IRUGO | S_IWUSR, rp400_show_hw_test_mode, rp400_store_hw_test_mode);

static ssize_t rp400_show_usb_mode(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", epd_usb_mode);
}
static ssize_t rp400_store_usb_mode(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int usb_mode;

	sscanf(&buf[0], "%d", &usb_mode);
	epd_usb_mode = !!usb_mode;

	return len;
}
static DEVICE_ATTR(usb_mode, S_IRUGO | S_IWUSR, rp400_show_usb_mode, rp400_store_usb_mode);

static ssize_t rp400_show_usb_select(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->usb_select);
}
static ssize_t rp400_store_usb_select(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	int usb_select;

	sscanf(&buf[0], "%d", &usb_select);
	data->usb_select = !!usb_select;
	gpiod_set_value(data->gpiod_usb_select, data->usb_select);

	return len;
}
static DEVICE_ATTR(usb_select, S_IRUGO | S_IWUSR, rp400_show_usb_select, rp400_store_usb_select);

static ssize_t rp400_show_mipi_enable(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->mipi_enable);
}
static ssize_t rp400_store_mipi_enable(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	int mipi_enable;

	sscanf(&buf[0], "%d", &mipi_enable);
	data->mipi_enable = !!mipi_enable;
	gpiod_set_value(data->gpiod_mipi_enable, data->mipi_enable);

	return len;
}
static DEVICE_ATTR(mipi_enable, S_IRUGO | S_IWUSR, rp400_show_mipi_enable, rp400_store_mipi_enable);

static ssize_t rp400_show_gma303_xya(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	short x, y, z;

	gma303_read_xyz((u16 *)&x, (u16 *)&y, (u16 *)&z);

	return sprintf(buf, "x: %d, y: %d, z: %d\n", z, y, z);
}
static DEVICE_ATTR(gma303xyz, S_IRUGO | S_IWUSR, rp400_show_gma303_xya, NULL);

static ssize_t rp400_show_debug_mode(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "Battery debug : %s\n", (rp400_debug_mode & 0x01)?"on":"off");
}
static ssize_t rp400_store_debug_mode(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	sscanf(&buf[0], "%d", &rp400_debug_mode);
	return len;
}
static DEVICE_ATTR(debug_mode, S_IRUGO | S_IWUSR, rp400_show_debug_mode, rp400_store_debug_mode);

static ssize_t rp400_show_usb_detected(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", rp400_usb_detected);
}
static ssize_t rp400_store_usb_detected(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int usb_detected = 0;
	sscanf(&buf[0], "%d", &usb_detected);
	set_rp400_usb_detect(usb_detected);
	return len;
}
static DEVICE_ATTR(usb_detected, S_IRUGO | S_IWUSR, rp400_show_usb_detected, rp400_store_usb_detected);

static ssize_t rp400_show_dsim_power(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", dsim_power_status());
}
static ssize_t rp400_store_dsim_power(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int power_on;
	sscanf(&buf[0], "%d", &power_on);
	if(power_on)
		dsim_powerup();
	else
		dsim_powerdown();
	return len;
}
static DEVICE_ATTR(dsim_power, S_IRUGO | S_IWUSR, rp400_show_dsim_power, rp400_store_dsim_power);

static ssize_t rp400_show_epd_spi_sleep(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->epd_spi_sleep);
}
static ssize_t rp400_store_epd_spi_sleep(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	int sleep = 0;
	sscanf(&buf[0], "%d", &sleep);

	data->epd_spi_sleep = !!sleep;
	if(data->epd_spi_sleep)
		IT8951DisplaySleep();
	else
		IT8951DisplayWakeup();

	return len;
}
static DEVICE_ATTR(epd_spi_sleep, S_IRUGO | S_IWUSR, rp400_show_epd_spi_sleep, rp400_store_epd_spi_sleep);

static ssize_t rp400_store_epd_spi_display_area(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int x, y, w, h, mode;

	sscanf(&buf[0], "%d %d %d %d %d", &x, &y, &w, &h, &mode);
	it8951DisplayArea(x, y, w, h, (u16)mode);

	return len;
}
static DEVICE_ATTR(epd_spi_display_area, S_IRUGO | S_IWUSR, NULL, rp400_store_epd_spi_display_area);

static ssize_t rp400_store_epd_spi_set_vcom(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int vcom;

	sscanf(&buf[0], "%d", &vcom);
	IT8951DisplaySetVCOM(vcom);

	return len;
}
static DEVICE_ATTR(epd_spi_set_vcom, S_IRUGO | S_IWUSR, NULL, rp400_store_epd_spi_set_vcom);

static ssize_t rp400_show_wakelock(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->wake_lock_on);
}

static ssize_t rp400_store_wakelock(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	int wake_lock_on;

	sscanf(&buf[0], "%d", &wake_lock_on);
	data->wake_lock_on = !!wake_lock_on;

	if(data->wake_lock_on)
		wake_lock(&data->wake_lock);
	else
		wake_unlock(&data->wake_lock);

	return len;
}
static DEVICE_ATTR(wakelock, S_IRUGO | S_IWUSR, rp400_show_wakelock, rp400_store_wakelock);

static ssize_t rp400_show_battery_status(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", battery_status);
}
static DEVICE_ATTR(battery_status, S_IRUGO | S_IWUSR, rp400_show_battery_status, NULL);

static ssize_t rp400_show_usb_lock(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", rp400_usb_lock);
}
static ssize_t rp400_store_usb_lock(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int usb_lock = 0;
	sscanf(&buf[0], "%d", &usb_lock);
	rp400_usb_lock = !!usb_lock;
	if(rp400_usb_lock == 0) {
		s2mu106_muic_get_detect_info(g_muic_data);

		if(get_epd_usb_role()) {
			union power_supply_propval value;
			struct power_supply *psy;

			psy = power_supply_get_by_name("s2mu106_pmeter");
			if(psy) {
				power_supply_get_property(psy, POWER_SUPPLY_PROP_VCHGIN, &value);
				g_battery->vchg_voltage = value.intval;
				if(g_battery->vchg_voltage < 4000) {
					set_rp400_battery_status(g_battery, POWER_SUPPLY_STATUS_DISCHARGING);
				} else {
					set_rp400_battery_status(g_battery, POWER_SUPPLY_STATUS_CHARGING);
				}
			}

			wake_lock(&g_battery->monitor_wake_lock);
			queue_delayed_work(g_battery->monitor_wqueue, &g_battery->monitor_work, 0);
		}
	}
	return len;
}
static DEVICE_ATTR(usb_lock, S_IRUGO | S_IWUSR, rp400_show_usb_lock, rp400_store_usb_lock);

static ssize_t rp400_show_power_key_lock(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", rp400_powerkey_lock);
}

static ssize_t rp400_store_power_key_lock(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int key_lock;

	sscanf(&buf[0], "%d", &key_lock);
	rp400_powerkey_lock = !!key_lock;

	return len;
}
static DEVICE_ATTR(powerkey_lock, S_IRUGO | S_IWUSR, rp400_show_power_key_lock, rp400_store_power_key_lock);

static ssize_t rp400_store_charging_current(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int max, now;

	sscanf(&buf[0], "%d %d", &max, &now);
	set_rp400_charging_current(g_battery, max, now);

	return len;
}
static DEVICE_ATTR(charging_current, S_IRUGO | S_IWUSR, NULL, rp400_store_charging_current);

static ssize_t rp400_show_chg_full_vcell(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", g_battery->pdata->chg_full_vcell);
}
static DEVICE_ATTR(chg_full_vcell, S_IRUGO | S_IWUSR, rp400_show_chg_full_vcell, NULL);

static ssize_t rp400_show_chg_recharge_vcell(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", g_battery->pdata->chg_recharge_vcell);
}
static DEVICE_ATTR(chg_recharge_vcell, S_IRUGO | S_IWUSR, rp400_show_chg_recharge_vcell, NULL);

static ssize_t rp400_show_chg_full_check_count(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%d\n", g_battery->pdata->full_check_count);
}
static DEVICE_ATTR(chg_full_check_count, S_IRUGO | S_IWUSR, rp400_show_chg_full_check_count, NULL);

static ssize_t rp400_show_gma303_calibration(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%hd %hd %hd 0x%x 0x%x 0x%x\n",
		gma303_cali_x, gma303_cali_y, gma303_cali_z, gma303_slope_x, gma303_slope_y, gma303_slope_z);
}

static ssize_t rp400_store_gma303_calibration(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	int x, y, z;
	int temp_slope_x, temp_slope_y, temp_slope_z;

	sscanf(&buf[0], "%d %d %d %d %d %d", &x, &y, &z, &temp_slope_x, &temp_slope_y, &temp_slope_z);
	gma303_cali_x = x & 0xFFFF;
	gma303_cali_y = y & 0xFFFF;
#if GMA303_CALI_Z
	gma303_cali_z = z & 0xFFFF;
#endif
	gma303_slope_x = (float)temp_slope_x / 1000.0f;
	gma303_slope_y = (float)temp_slope_y / 1000.0f;
#if GMA303_CALI_Z
	gma303_slope_z = (float)temp_slope_z / 1000.0f;
#endif

	pr_err("%s (%d %d %d %d %d %d)\n", __func__, x, y, z, temp_slope_x, temp_slope_y, temp_slope_z);

	return len;
}
static DEVICE_ATTR(gma303_cali, S_IRUGO | S_IWUSR, rp400_show_gma303_calibration, rp400_store_gma303_calibration);

static ssize_t rp400_store_gma303_calibration_trigger(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct rp400_misc_data *data = dev_get_drvdata(dev);
	int x, y, z;
	int temp_slope_x, temp_slope_y, temp_slope_z;
	mm_segment_t old_fs;
	struct file *fp;
	struct firmware *fw;
	const char* cali_raw_path = "/mnt/vendor/efs/gma303_cali";
	int error;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(cali_raw_path, O_RDONLY, S_IRUSR/*|S_IWUSR*/);
	if(IS_ERR(fp)) {
		dev_err(data->dev, "%s file open failed!\n", cali_raw_path);
		set_fs(old_fs);
		return -ENOENT;
	}

	fw = kmalloc(sizeof(struct firmware), GFP_KERNEL);
	fw->size = 0;
	fw->data = kmalloc(256, GFP_KERNEL);
	do {
		error = vfs_read(fp, (char *)&fw->data[fw->size], 256, &fp->f_pos);
		fw->size += error;
	} while(error != 0);

	filp_close(fp, NULL);
	set_fs(old_fs);

	sscanf(fw->data, "%d %d %d %d %d %d", &x, &y, &z, &temp_slope_x, &temp_slope_y, &temp_slope_z);
	gma303_cali_x = x & 0xFFFF;
	gma303_cali_y = y & 0xFFFF;
#if GMA303_CALI_Z
	gma303_cali_z = z & 0xFFFF;
#endif
	gma303_slope_x = (float)temp_slope_x / 1000.0f;
	gma303_slope_y = (float)temp_slope_y / 1000.0f;
#if GMA303_CALI_Z
	gma303_slope_z = (float)temp_slope_z / 1000.0f;
#endif

	pr_err("%s (%d %d %d %d %d %d)\n", __func__, x, y, z, temp_slope_x, temp_slope_y, temp_slope_z);

	kfree(fw->data);
	kfree(fw);
	fw = NULL;

	return len;
}
static DEVICE_ATTR(gma303_cali_trigger, S_IRUGO | S_IWUSR, NULL, rp400_store_gma303_calibration_trigger);

static struct attribute *rp400_misc_attrs[] = {
	&dev_attr_epd_test.attr,
	&dev_attr_epd_hrdy.attr,
	&dev_attr_epd_pwr_ctrl.attr,
	&dev_attr_epd_reset.attr,
	&dev_attr_epd_drv_init.attr,
	&dev_attr_epd_display_init.attr,
	&dev_attr_epd_ready.attr,
	&dev_attr_epd_clear.attr,
	&dev_attr_fb_ready_count.attr,
	&dev_attr_epd_mode.attr,
	&dev_attr_epd_fw_update.attr,
	&dev_attr_epd_speed.attr,
	&dev_attr_epd_spi_dump_reg.attr,
	&dev_attr_epd_version.attr,
	&dev_attr_debug_msg.attr,
	&dev_attr_pmic_ldo_reg.attr,
	&dev_attr_gma303xyz.attr,
	&dev_attr_hw_test_mode.attr,
	&dev_attr_usb_mode.attr,
	&dev_attr_usb_select.attr,
	&dev_attr_mipi_enable.attr,
	&dev_attr_debug_mode.attr,
	&dev_attr_usb_detected.attr,
	&dev_attr_dsim_power.attr,
	&dev_attr_epd_spi_sleep.attr,
	&dev_attr_epd_spi_display_area.attr,
	&dev_attr_epd_spi_set_vcom.attr,
	&dev_attr_wakelock.attr,
	&dev_attr_battery_status.attr,
	&dev_attr_usb_lock.attr,
	&dev_attr_powerkey_lock.attr,
	&dev_attr_charging_current.attr,
	&dev_attr_chg_full_vcell.attr,
	&dev_attr_chg_recharge_vcell.attr,
	&dev_attr_chg_full_check_count.attr,
	&dev_attr_gma303_cali.attr,
	&dev_attr_gma303_cali_trigger.attr,
	NULL,
};

static struct attribute_group rp400_misc_attr_group = {
	.attrs = rp400_misc_attrs,
};

#ifdef CONFIG_OF
static struct rp400_misc_data *
rp400_misc_get_devtree_pdata(struct device *dev)
{
	struct device_node *node;
	struct rp400_misc_data *data;
	//int ret = 0;

	node = dev->of_node;
	if (!node)
		return ERR_PTR(-ENODEV);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	return data;
}

static const struct of_device_id rp400_misc_of_match[] = {
	{ .compatible = "ridi,rp400-misc", },
	{ },
};
MODULE_DEVICE_TABLE(of, rp400_misc_of_match);
#else
static inline struct rp400_misc_data *
rp400_misc_get_devtree_pdata(struct device *dev)
{
	return ERR_PTR(-ENODEV);
}

#endif

static struct class *rp400_misc_class;

#if 0
static void epd_ts_ldo_monitor_work_func(struct work_struct *work)
{
	struct rp400_misc_data *data = container_of(work, struct rp400_misc_data, work);
	struct i2c_client i2c;
	u8 value;
	static int settling_count = 10;
	static int settle_start = 0;
	
	i2c.addr = 0x1;
	s2mpu12_read_reg(&i2c, 0x39, &value);
	printk("EPD Touch LDO(15) : 0x%02x, settling_count: %d\n", value, settling_count);

	if(value != 0xC0) {
		s2mpu12_write_reg(&i2c, 0x39, 0xC0);
		settle_start = 1;
		settling_count = 10;
	} else {
		if(settle_start)
			settling_count--;
	}

	if(settling_count > 0)
		mod_timer(&data->timer, jiffies + HZ);
}

static void epd_ts_ldo_monitor_timeout_handler(unsigned long arg)
{
	struct rp400_misc_data *data = (struct rp400_misc_data *)arg;
	schedule_work(&data->work);
}
#endif

static int rp400_misc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rp400_misc_data *data = dev_get_platdata(dev);
	int ret = 0;
	int majorno;

	if (!data) {
		data = rp400_misc_get_devtree_pdata(dev);
		if (IS_ERR(data))
			return PTR_ERR(data);
	}
	platform_set_drvdata(pdev, data);
	data->dev = dev;

	INIT_LIST_HEAD(&data->device_entry);
	mutex_init(&data->fb_cnt_lock);
	data->epd_pwr_ctrl = 1;

#if 0
	//EPD Touch LDO off issue.. check it later
	INIT_WORK(&data->work, epd_ts_ldo_monitor_work_func);
	setup_timer(&data->timer, epd_ts_ldo_monitor_timeout_handler, (unsigned long)data);
	mod_timer(&data->timer, jiffies + HZ);
#endif

	data->gpiod_usb_select = devm_gpiod_get(dev, "usb_select", GPIOD_OUT_HIGH);
	if (IS_ERR(data->gpiod_usb_select)) {
		ret = PTR_ERR(data->gpiod_usb_select);
		if (ret != -ENOENT && ret != -ENOSYS)
			return ret;

		data->gpiod_usb_select = NULL;
		dev_err(dev, "%s Can not read property data usb_select-gpios\n", __func__);
	} else {
		gpiod_direction_output(data->gpiod_usb_select, 1);
		data->usb_select = 1;
	}

	data->gpiod_mipi_enable = devm_gpiod_get(dev, "mipi_enable", GPIOD_OUT_HIGH);
	if (IS_ERR(data->gpiod_mipi_enable)) {
		ret = PTR_ERR(data->gpiod_mipi_enable);
		if (ret != -ENOENT && ret != -ENOSYS)
			return ret;

		data->gpiod_mipi_enable = NULL;
		dev_err(dev, "%s Can not read property data mipi_enable-gpios\n", __func__);
	} else {
		gpiod_direction_output(data->gpiod_mipi_enable, 1);
		data->mipi_enable = 1;
	}

	data->vldo16 = devm_regulator_get(dev, "vldo16");
	if (IS_ERR(data->vldo16)) {
		ret = PTR_ERR(data->vldo16);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get 'vldo16' regulator: %d\n", ret);
		return ret;
	}

	data->vldo31 = devm_regulator_get(dev, "vldo31");
	if (IS_ERR(data->vldo31)) {
		ret = PTR_ERR(data->vldo31);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get 'vldo31' regulator: %d\n", ret);
		return ret;
	}

	data->vldo34 = devm_regulator_get(dev, "vldo34");
	if (IS_ERR(data->vldo34)) {
		ret = PTR_ERR(data->vldo34);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "Failed to get 'vldo34' regulator: %d\n", ret);
		return ret;
	}

	regulator_set_voltage(data->vldo16, 1800000, 1800000);
	regulator_enable(data->vldo16);
	regulator_set_voltage(data->vldo31, 1800000, 1800000);
	regulator_enable(data->vldo31);
	regulator_set_voltage(data->vldo34, 1800000, 1800000);
	regulator_enable(data->vldo34);

	majorno = register_chrdev(0, DEV_NAME, &rp400_misc_drv_fops);
	if(majorno < 0)
	{
		dev_err(dev, "%s device register error!(%d)\r\n", DEV_NAME, majorno);
		return -1;
	}

	wake_lock_init(&data->wake_lock, WAKE_LOCK_SUSPEND, "rp400_wake");
	init_completion(&check_usb_detected);
	check_usb_detected_init = 1;

	mutex_lock(&device_list_lock);
	rp400_misc_class = class_create(THIS_MODULE, DEV_NAME);
	data->devt = MKDEV(majorno,1);
	if(device_create(rp400_misc_class, data->dev, data->devt, data, DEV_NAME))
		list_add(&data->device_entry, &device_list);
	mutex_unlock(&device_list_lock);
	
	ret = sysfs_create_group(&pdev->dev.kobj, &rp400_misc_attr_group);
	if (ret) {
		dev_err(dev, "Unable to create group, error: %d\n",
			ret);
		return ret;
	}

	dev_info(dev, "%s done.\n", __func__);

	return 0;
}

static int rp400_misc_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rp400_misc_data *data = dev_get_platdata(dev);
	
	sysfs_remove_group(&pdev->dev.kobj, &rp400_misc_attr_group);

	device_init_wakeup(&pdev->dev, 0);

	mutex_lock(&device_list_lock);
	list_del(&data->device_entry);
	device_destroy(rp400_misc_class, data->devt);
	mutex_unlock(&device_list_lock);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rp400_misc_suspend(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	rp400_interrupt_event = EPD_SUSPEND;
	if(check_usb_detected_init)
		complete(&check_usb_detected);

	return 0;
}

static int rp400_misc_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	rp400_interrupt_event = EPD_RESUME;
	if(check_usb_detected_init)
		complete(&check_usb_detected);

	return 0;
}
static SIMPLE_DEV_PM_OPS(rp400_misc_pm_ops, rp400_misc_suspend, rp400_misc_resume);
#endif


static struct platform_driver rp400_misc_device_driver = {
	.probe		= rp400_misc_probe,
	.remove		= rp400_misc_remove,
	.driver		= {
		.name	= "rp400-misc",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM_SLEEP
		.pm	= &rp400_misc_pm_ops,
#endif
		.of_match_table = of_match_ptr(rp400_misc_of_match),
	}
};

static int __init rp400_misc_drv_init(void)
{
	return platform_driver_register(&rp400_misc_device_driver);
}

static void __exit rp400_misc_drv_exit(void)
{
	platform_driver_unregister(&rp400_misc_device_driver);
}


late_initcall(rp400_misc_drv_init);
module_exit(rp400_misc_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Terry Choi <dw.choi@e-roum.com>");
MODULE_DESCRIPTION("misc driver for RIDI RP-400 device");



