/*
 * IT8951 SPI driver
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
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/acpi.h>
#include <linux/gpio.h>
#include <linux/time.h>
#include <linux/vmalloc.h>

#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/power/s2m_chg_manager.h>

#include "it8951_display.h"


#define IT8951_MAJOR			230	/* assigned */
#define N_SPI_MINORS			32	/* ... up to 256 */
static DECLARE_BITMAP(minors, N_SPI_MINORS);

#define EPD_DISP_WIDTH 1680
#define EPD_DISP_HEIGHT 1264

#define IOCTL_IT8951_BASE 0xA000
#define IOCTL_IT8951_SET_SPEED (IOCTL_IT8951_BASE + 1)

#define DEFAULT_MAX_SPEED 12000000

//#define TEST_MODE

struct epd_data_info
{
	int x;
	int y;
	int w;
	int h;
	int flag;
	int rotation;
};

struct it8951_data {
	dev_t			devt;
	spinlock_t		spi_lock;
	struct spi_device	*spi;
	struct list_head	device_entry;

	/* TX/RX buffers are NULL unless this device is open (users > 0) */
	struct mutex		buf_lock;
	unsigned		users;
	u8			*tx_buffer;
	u8			*rx_buffer;
	u32			speed_hz;

	u8* gray_pixel;
	struct mutex disp_lock;

	struct regulator* regulator_vcc33a;
	struct regulator* regulator_vcc18a;

	struct gpio_desc *gpiod_epd_hrdy;
	struct gpio_desc *gpiod_epd_cs;
	struct gpio_desc *gpiod_epd_rst;
	struct gpio_desc *gpiod_epd_pwr_en;

	int epd_hrdy;
	int epd_cs;
	int epd_rst;

	I80IT8951DevInfo stI80DevInfo;
	u32 imgBufAddr;
	u32 bufIdx;
	struct epd_data_info data_info;

	struct work_struct work;
	struct timer_list timer;

	int disp_init_done;
};

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static struct it8951_data *gdata;
static unsigned bufsiz = 8192;
static bool power_off_charging = false;

extern int disp_dump;
extern struct s2m_chg_manager_info *g_battery;

static void it8951_power_onoff(struct it8951_data *data, int on);


/*-------------------------------------------------------------------------*/


static ssize_t spidev_sync(struct it8951_data *data, struct spi_message *message)
{
	int status;
	struct spi_device *spi;

	spin_lock_irq(&data->spi_lock);
	spi = data->spi;
	spin_unlock_irq(&data->spi_lock);

	if (spi == NULL)
		status = -ESHUTDOWN;
	else
		status = spi_sync(spi, message);

	if (status == 0)
		status = message->actual_length;

	return status;
}

static inline ssize_t spi_sync_write(struct it8951_data *data, size_t len, u8 bpw)
{
	struct spi_transfer	t = {
			.tx_buf		= data->tx_buffer,
			.len		= len,
			.speed_hz	= data->speed_hz,
			.bits_per_word = bpw,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spidev_sync(data, &m);
}

static inline ssize_t spi_sync_read(struct it8951_data *data, size_t len)
{
	struct spi_transfer	t = {
			.rx_buf		= data->rx_buffer,
			.len		= len,
			.speed_hz	= data->speed_hz,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spidev_sync(data, &m);
}

void it8951_spi_write(u8 *cmd, u32 size, u8 bpw)
{
	mutex_lock(&gdata->buf_lock);

	gdata->tx_buffer = cmd;
	spi_sync_write(gdata, size, bpw);

	mutex_unlock(&gdata->buf_lock);
}

void it8951_spi_read(u8 *cmd, u32 size)
{
	mutex_lock(&gdata->buf_lock);

	gdata->rx_buffer = cmd;
	spi_sync_read(gdata, size);

	mutex_unlock(&gdata->buf_lock);
}

/*-------------------------------------------------------------------------*/

int get_it8951_hrdy_status(void)
{
	return gpiod_get_value(gdata->gpiod_epd_hrdy);
}

void set_it8951_spi_cs(int status)
{
	gpiod_set_value(gdata->gpiod_epd_cs, status);
}

void set_it8951_reset(int reset)
{
	gpiod_set_value(gdata->gpiod_epd_rst, reset);
	gdata->epd_rst = reset;
}

void set_it8951_speed(int speed_hz)
{
	gdata->spi->max_speed_hz = speed_hz;
	gdata->speed_hz = gdata->spi->max_speed_hz;
}

int get_it8951_reset(void)
{
	return gdata->epd_rst;
}

int get_it8951_display_init_done(void)
{
	return (gdata)?gdata->disp_init_done:0;
}

void it8951DisplayTest0(void)
{
	IT8951DisplayTest0(&gdata->stI80DevInfo, gdata->imgBufAddr, gdata->gray_pixel);
}

void it8951DisplayTest1(void)
{
	IT8951DisplayTest1(&gdata->stI80DevInfo, gdata->imgBufAddr, gdata->gray_pixel);
}

void it8951DisplayUserData(u32 x, u32 y, u32 w, u32 h, char *path)
{
	IT8951DisplayUserData(&gdata->stI80DevInfo, gdata->imgBufAddr, gdata->gray_pixel, x, y, w, h, IT8951_MODE_DU, path);
}

void it8951DisplayArea(u32 x, u32 y, u32 w, u32 h, u16 mode)
{
	IT8951DisplayPartial(&gdata->stI80DevInfo, gdata->imgBufAddr, gdata->gray_pixel, x, y, w, h, mode);
}

void it8951DisplayClear(u8 value)
{
	IT8951DisplayClear(&gdata->stI80DevInfo, gdata->imgBufAddr, gdata->gray_pixel, value);
}

void it8951DisplayInit(void)
{
	IT8951_Drv_Init(&gdata->stI80DevInfo, &gdata->imgBufAddr);
}

void it8951DisplayFWflash(u8* buf)
{
	IT8951DisplayFWflash(buf, gdata->imgBufAddr);
}

void it8951DisplayInitClear(void)
{
	IT8951DisplayInitClear(&gdata->stI80DevInfo, gdata->imgBufAddr, gdata->gray_pixel);
}

void it8951DisplayPowerOnOff(int on)
{
	it8951_power_onoff(gdata, on);
}

void it8951GetVersion(char* version)
{
	memcpy(version, gdata->stI80DevInfo.usFWVersion, sizeof(gdata->stI80DevInfo.usFWVersion));
}

/*-------------------------------------------------------------------------*/


/*-------------------------------------------------------------------------*/
static void it8951_power_onoff(struct it8951_data *data, int on)
{
	if(on) {
		regulator_enable(data->regulator_vcc18a);
		regulator_set_voltage(data->regulator_vcc18a, 1800000, 1800000);
		usleep_range(1000, 2000);
		regulator_enable(data->regulator_vcc33a);
		regulator_set_voltage(data->regulator_vcc33a, 3300000, 3300000);
	} else {
		regulator_disable(data->regulator_vcc33a);
		regulator_disable(data->regulator_vcc18a);
	}
}

static void it8951_booting_work_func(struct work_struct *work)
{
	//struct it8951_data *data = container_of(work, struct it8951_data, work);
}

static void it8951_booting_timeout_handler(unsigned long arg)
{
	struct it8951_data *data = (struct it8951_data *)arg;
	schedule_work(&data->work);
}


static long it8951_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct it8951_data *data = filp->private_data;

	switch(cmd) {
		case IOCTL_IT8951_SET_SPEED:
		{
			u32 speed_hz;
			if(copy_from_user(&speed_hz,(u32 *)arg, sizeof(u32))) {
				dev_err(&data->spi->dev, "IOCTL_IT8951_SET_SPEED copy_from_user error\n");
				ret = -EINVAL;
				break;
			}
			data->speed_hz = speed_hz;
		}
		break;
		default:
			ret = -EINVAL;
	}

	return ret;
}

static ssize_t it8951_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct it8951_data *data;
	ssize_t status = 0;
	unsigned long missing;

	/* chipselect only toggles at start or end of operation */
	if (count > bufsiz)
		return -EMSGSIZE;

	data = filp->private_data;

	mutex_lock(&data->buf_lock);
	missing = copy_from_user(&data->gray_pixel[data->bufIdx], buf, count);
	if (missing == 0) {
		data->bufIdx += count;
		status = count;
	} else {
		status = -EFAULT;
	}
	mutex_unlock(&data->buf_lock);

	return status;
}


static ssize_t it8951_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	return 0;
}

static int it8951_open(struct inode *inode, struct file *filp)
{
	struct it8951_data *data;
	int status = -ENXIO;

	mutex_lock(&device_list_lock);

	list_for_each_entry(data, &device_list, device_entry) {
		if (data->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}

	if (status) {
		pr_debug("it8951: nothing for minor %d\n", iminor(inode));
		goto err_find_dev;
	}

	data->users++;
	data->bufIdx = 0;
	filp->private_data = data;
	nonseekable_open(inode, filp);

	mutex_unlock(&device_list_lock);

	return 0;

err_find_dev:
	mutex_unlock(&device_list_lock);
	
	return status;
}

static int it8951_release(struct inode *inode, struct file *filp)
{
	struct it8951_data *data;
#if 0
#if defined(TEST_MODE)
	struct timespec ts;
	u32 t0 = 0;
	u32 t1 = 0;
#else
	struct epd_data_info data_info;
	u8* pixel;
	u8 mode = 0;
	u8 i;
#endif
#endif

	mutex_lock(&device_list_lock);
	data = filp->private_data;

#if 0
#if defined(TEST_MODE)
	getnstimeofday(&ts);
	t0 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	data->tx_buffer = data->gray_pixel;
	spi_sync_write(data, data->bufIdx, 32);
	getnstimeofday(&ts);
	t1 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	pr_info("Test Display time: %d ms, %d bytes[%02x %02x %02x %02x]\n", t1-t0, data->bufIdx,
		data->tx_buffer[0], data->tx_buffer[1], data->tx_buffer[2], data->tx_buffer[3]);
#else
	memcpy(&data_info, data->gray_pixel, sizeof(struct epd_data_info));
	for(i=0; i<IT8951_MODE_MAX; i++) {
		if((data_info.flag >> i) & 0x01) {
			mode = i;
			break;
		}
	}
	pixel = data->gray_pixel + sizeof(struct epd_data_info);
	IT8951DisplayBuffer(&data->stI80DevInfo, data->imgBufAddr, pixel, 
		data_info.x, data_info.y, data_info.w, data_info.h, mode);
#endif
#endif
	filp->private_data = NULL;

	/* last close? */
	data->users--;
	if (!data->users) {
		int dofree;

		spin_lock_irq(&data->spi_lock);
		if (data->spi)
			data->speed_hz = data->spi->max_speed_hz;

		/* ... after we unbound from the underlying device? */
		dofree = (data->spi == NULL);
		spin_unlock_irq(&data->spi_lock);

		if (dofree)
			kfree(data);
	}
	mutex_unlock(&device_list_lock);

	return 0;
}


static const struct file_operations it8951_fops = {
	.owner =	THIS_MODULE,
	.write =	it8951_write,
	.read =		it8951_read,
	.open =		it8951_open,
	.release =	it8951_release,
	.llseek =	no_llseek,
	.unlocked_ioctl	= it8951_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = it8951_ioctl,
#endif
};


/*-------------------------------------------------------------------------*/
static struct class *it8951_class;

#ifdef CONFIG_OF
static const struct of_device_id it8951_dt_ids[] = {
	{ .compatible = "ite,it8951e" },
	{},
};
MODULE_DEVICE_TABLE(of, it8951_dt_ids);
#endif

static int it8951_probe(struct spi_device *spi)
{
	struct it8951_data	*data;
	int status;
	unsigned long		minor;
	struct device_node *np = spi->dev.of_node;
	struct device *dev = &spi->dev;
	const char* str_regulator;

	dev_info(dev, "############ IT8951 Initial Start ##################");

	/*
	 * data should never be referenced in DT without a specific
	 * compatible string, it is a Linux implementation thing
	 * rather than a description of the hardware.
	 */
	if (np && !of_match_device(it8951_dt_ids, dev)) {
		dev_err(dev, "buggy DT: data listed directly in DT\n");
		WARN_ON(np &&
			!of_match_device(it8951_dt_ids, dev));
	}

	/* Allocate driver data */
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	gdata = data;

	/* Initialize the driver data */
	data->spi = spi;
	spin_lock_init(&data->spi_lock);
	mutex_init(&data->buf_lock);

	INIT_LIST_HEAD(&data->device_entry);

	/* If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *dev;

		data->devt = MKDEV(IT8951_MAJOR, minor);
		dev = device_create(it8951_class, dev, data->devt,
				    data, "it8951%d.%d",
				    spi->master->bus_num, spi->chip_select);
		status = PTR_ERR_OR_ZERO(dev);
	} else {
		dev_dbg(dev, "no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&data->device_entry, &device_list);
	}

	spi->max_speed_hz = DEFAULT_MAX_SPEED;
	spi->mode = 0;
	spi->bits_per_word = 16;
	
	mutex_unlock(&device_list_lock);

	data->speed_hz = spi->max_speed_hz;

	if (status == 0)
		spi_set_drvdata(spi, data);
	else
		kfree(data);

	mutex_init(&data->disp_lock);

	data->gray_pixel = kmalloc (EPD_DISP_WIDTH*EPD_DISP_HEIGHT+1024, GFP_KERNEL);
	dev_info(dev, "gray_pixel: %p\n", data->gray_pixel);
	if (!data->gray_pixel)
		return -ENOMEM;

	if (of_property_read_string(np, "epd_regulator_vcc33a", &str_regulator)) {
		dev_err(dev, "Failed to get epd_regulator_vcc33a name property\n");
		return -EINVAL;
	} else {
		data->regulator_vcc33a = regulator_get(dev, str_regulator);
		if(IS_ERR(data->regulator_vcc33a)) {
			dev_err(dev, "IT8951 regulator 3.3V get failed");
			data->regulator_vcc33a = NULL;
		}
	}

	if (of_property_read_string(np, "epd_regulator_vcc18a", &str_regulator)) {
		dev_err(dev, "Failed to get regulator_vcc18a name property\n");
		return -EINVAL;
	} else {
		data->regulator_vcc18a = regulator_get(dev, str_regulator);
		if(IS_ERR(data->regulator_vcc18a)) {
			dev_err(dev, "IT8951 regulator 1.8V get failed");
			data->regulator_vcc18a = NULL;
		}
	}

	data->gpiod_epd_hrdy = devm_gpiod_get(dev, "epd_hrdy", GPIOD_OUT_HIGH);
	if (IS_ERR(data->gpiod_epd_hrdy)) {
		status = PTR_ERR(data->gpiod_epd_hrdy);
		if (status != -ENOENT && status != -ENOSYS)
			return status;

		data->gpiod_epd_hrdy = NULL;
		dev_err(dev, "%s Can not read property data epd_hrdy-gpios\n", __func__);
	} else {
		gpiod_direction_input(data->gpiod_epd_hrdy);
		data->epd_hrdy = gpiod_get_value(data->gpiod_epd_hrdy);
	}

	data->gpiod_epd_cs = devm_gpiod_get(dev, "epd_cs", GPIOD_OUT_HIGH);
	if (IS_ERR(data->gpiod_epd_cs)) {
		status = PTR_ERR(data->gpiod_epd_cs);
		if (status != -ENOENT && status != -ENOSYS)
			return status;

		data->gpiod_epd_cs = NULL;
		dev_err(dev, "%s Can not read property data epd_cs-gpios\n", __func__);
	} else {
		gpiod_direction_output(data->gpiod_epd_cs, 1);
		data->epd_cs = 1;
	}

	data->gpiod_epd_rst = devm_gpiod_get(dev, "epd_reset", GPIOD_OUT_LOW);
	if (IS_ERR(data->gpiod_epd_rst)) {
		status = PTR_ERR(data->gpiod_epd_rst);
		if (status != -ENOENT && status != -ENOSYS)
			return status;

		data->gpiod_epd_rst = NULL;
		dev_err(dev, "%s Can not read property data epd_rst-gpios\n", __func__);
	} else {
		gpiod_direction_output(data->gpiod_epd_rst, 0);
		data->epd_rst = 0;
	}

	data->gpiod_epd_pwr_en = devm_gpiod_get(dev, "epd_power_en", GPIOD_OUT_HIGH);
	if (IS_ERR(data->gpiod_epd_pwr_en)) {
		status = PTR_ERR(data->gpiod_epd_pwr_en);
		if (status != -ENOENT && status != -ENOSYS)
			return status;

		data->gpiod_epd_pwr_en = NULL;
		dev_err(dev, "%s Can not read property data epd_rst-gpios\n", __func__);
	} else {
		gpiod_direction_output(data->gpiod_epd_pwr_en, 1);
		data->epd_rst = 0;
	}

	//Reset IT8951
	if(data->regulator_vcc18a && data->regulator_vcc33a) {
		it8951_power_onoff(data, 1);
		usleep_range(1000, 2000);
		gpiod_set_value(data->gpiod_epd_rst, 1);
		data->epd_rst = 1;
		usleep_range(10000, 20000);

		if(!power_off_charging) {
			IT8951_Drv_Init(&data->stI80DevInfo, &data->imgBufAddr);
			IT8951DisplaySetVCOM(1200);
			IT8951DisplayInitClear(&data->stI80DevInfo, data->imgBufAddr, data->gray_pixel);
			data->disp_init_done = 1;
		}

		INIT_WORK(&data->work, it8951_booting_work_func);
		setup_timer(&data->timer, it8951_booting_timeout_handler, (unsigned long)data);
		//mod_timer(&data->timer, jiffies+(HZ/4));
	}

	dev_info(dev, "############ IT8951 Initial End ##################");

	return status;
}

static int it8951_remove(struct spi_device *spi)
{
	struct it8951_data	*it8951 = spi_get_drvdata(spi);

	/* make sure ops on existing fds can abort cleanly */
	spin_lock_irq(&it8951->spi_lock);
	it8951->spi = NULL;
	spin_unlock_irq(&it8951->spi_lock);

	/* prevent new opens */
	mutex_lock(&device_list_lock);
	list_del(&it8951->device_entry);
	device_destroy(it8951_class, it8951->devt);
	clear_bit(MINOR(it8951->devt), minors);
	if (it8951->users == 0)
		kfree(it8951);
	mutex_unlock(&device_list_lock);

	return 0;
}

static void it8951_shutdown(struct spi_device *spi)
{
	pr_info("+- %s\n", __func__);
	it8951DisplayPowerOnOff(0);
}

static struct spi_driver it8951_spi_driver = {
	.driver = {
		.name =		"it8951",
		.of_match_table = of_match_ptr(it8951_dt_ids),
	},
	.probe =	it8951_probe,
	.remove =	it8951_remove,
	.shutdown = it8951_shutdown,
};

/*-------------------------------------------------------------------------*/

static int __init it8951_init(void)
{
	int status;

	BUILD_BUG_ON(N_SPI_MINORS > 256);
	status = register_chrdev(IT8951_MAJOR, "it8951", &it8951_fops);
	if (status < 0)
		return status;

	it8951_class = class_create(THIS_MODULE, "it8951");
	if (IS_ERR(it8951_class)) {
		unregister_chrdev(IT8951_MAJOR, it8951_spi_driver.driver.name);
		return PTR_ERR(it8951_class);
	}

	status = spi_register_driver(&it8951_spi_driver);
	if (status < 0) {
		class_destroy(it8951_class);
		unregister_chrdev(IT8951_MAJOR, it8951_spi_driver.driver.name);
	}

	return status;
}
module_init(it8951_init);

static void __exit it8951_exit(void)
{
	spi_unregister_driver(&it8951_spi_driver);
	class_destroy(it8951_class);
	unregister_chrdev(IT8951_MAJOR, it8951_spi_driver.driver.name);
}
module_exit(it8951_exit);

static int __init is_poweroff_charging_mode(char *str)
{
	if (strncmp("charger", str, 7) == 0)
		power_off_charging = true;
	else
		power_off_charging = false;
	return 0;
} early_param("androidboot.mode", is_poweroff_charging_mode);


MODULE_AUTHOR("Terry Choi <dw.choi@e-roum.com>");


