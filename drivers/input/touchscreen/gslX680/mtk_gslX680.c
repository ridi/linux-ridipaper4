/*
 * gsl touchscreen driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GSL's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Version: 0.0.0.1
 * Release Date: 2020/10/27
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/byteorder/generic.h>
#include <linux/regulator/consumer.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <linux/fs.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/input/mt.h>
#include "mtk_gslX680_FHD.h"

#define GTP_RST_PORT    101
#define GTP_INT_PORT    0
#define GTP_I2C_NAME		"gsl-ts"
//#define GSL_DEBUG
//#define GSL_MONITOR
#define GSL9XX_CHIP
#define GSLX680_NAME	"gslX680_tp"
#define GSLX680_ADDR	0x40
#define MAX_FINGERS	  	10
#define MAX_CONTACTS	10
#define DMA_TRANS_LEN	0x20
#define SMBUS_TRANS_LEN	0x01
#define GSL_PAGE_REG		0xf0
#define ADD_I2C_DEVICE_ANDROID_4_0
//#define HIGH_SPEED_I2C
//#define FILTER_POINT
#ifdef FILTER_POINT
#define FILTER_MAX	9
#endif

#ifdef GSL_IDENTY_TP
static int gsl_tp_type = 0;
static int gsl_identify_tp(struct i2c_client *client);
#endif

#define TPD_PROC_DEBUG
#ifdef TPD_PROC_DEBUG
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/seq_file.h>
#define GSL_CONFIG_PROC_FILE "gsl_config"
#define CONFIG_LEN 31
static char gsl_read[CONFIG_LEN];
static u8 gsl_data_proc[8] = { 0 };

static u8 gsl_proc_flag = 0;
#endif

struct gsl_ts_data *g_ts = NULL;
static int tpd_flag = 0;
static int tpd_halt = 0;
static char eint_flag = 0;
//static struct tpd_device *tpd;
//static struct i2c_client *i2c_client = NULL;
static struct task_struct *thread = NULL;
#ifdef GSL_MONITOR
static struct delayed_work gsl_monitor_work;
static struct workqueue_struct *gsl_monitor_workqueue = NULL;
static u8 int_1st[4] = { 0 };
static u8 int_2nd[4] = { 0 };

static char b0_counter = 0;
static char bc_counter = 0;
static char i2c_lock_flag = 0;
#endif

static u32 id_sign[MAX_CONTACTS + 1] = { 0 };
static u8 id_state_flag[MAX_CONTACTS + 1] = { 0 };
static u8 id_state_old_flag[MAX_CONTACTS + 1] = { 0 };
static u16 x_old[MAX_CONTACTS + 1] = { 0 };
static u16 y_old[MAX_CONTACTS + 1] = { 0 };

static u16 x_new = 0;
static u16 y_new = 0;

#define TPD_KEY_COUNT	3
#define TPD_KEYS		{KEY_BACK, KEY_HOMEPAGE,KEY_MENU }
#define TPD_KEYS_DIM	{{90,1340,60,60},{270,1340,60,60},{450,1340,60,60}}

static DECLARE_WAIT_QUEUE_HEAD(waiter);
static irqreturn_t tpd_eint_interrupt_handler(int irq, void *dev_id);
unsigned int tpd_rst_gpio_number = 0;
unsigned int tpd_int_gpio_number = 1;
unsigned int touch_irq = 0;

#ifdef GSL_DEBUG
#define print_info(fmt, args...)   \
        do{                              \
                printk(fmt, ##args);     \
        }while(0)
#else
#define print_info(fmt, args...)
#endif

#ifdef TPD_HAVE_BUTTON
//static int tpd_keys_local[TPD_KEY_COUNT] = TPD_KEYS;
//static int tpd_keys_dim_local[TPD_KEY_COUNT][4] = TPD_KEYS_DIM;
#endif

#if (defined(TPD_WARP_START) && defined(TPD_WARP_END))
static int tpd_wb_start_local[TPD_WARP_CNT] = TPD_WARP_START;
static int tpd_wb_end_local[TPD_WARP_CNT] = TPD_WARP_END;
#endif
#if (defined(TPD_HAVE_CALIBRATION) && !defined(TPD_CUSTOM_CALIBRATION))
static int tpd_calmat_local[8] = TPD_CALIBRATION_MATRIX;
static int tpd_def_calmat_local[8] = TPD_CALIBRATION_MATRIX;
#endif

static void startup_chip(struct i2c_client *client)
{
	u8 write_buf = 0x00;
	int ret = 1;
	ret = i2c_smbus_write_i2c_block_data(client, 0xe0, 1, &write_buf);
	if (ret >= 0) {
		//printk("I write reg 0xe0\n");
	} else {
		printk("I write gsl----------erro \n");
		return;
	}

#ifdef GSL_NOID_VERSION
	gsl_DataInit(gsl_config_data_id);
#endif

	msleep(10);
}

#ifdef GSL9XX_CHIP
static void gsl_io_control(struct i2c_client *client)
{
	u8 buf[4] = { 0 };
	int i;
	int ret = 1;

	for (i = 0; i < 2; i++) {
		buf[0] = 0;
		buf[1] = 0;
		buf[2] = 0xfe;
		buf[3] = 0x1;
		ret = i2c_smbus_write_i2c_block_data(client, 0xf0, 4, buf);
		if (ret >= 0) {
			//printk("I write reg 0xf0\n");
		} else {
			printk("I write gsl----------erro \n");
			return;
		}
		buf[0] = 0x5;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0x80;
		ret = i2c_smbus_write_i2c_block_data(client, 0x78, 4, buf);
		if (ret >= 0) {
			//printk("I write reg 0x78\n");
		} else {
			printk("I write gsl----------erro \n");
			return;
		}
		msleep(5);
	}
	msleep(50);

}
#endif

static void reset_chip(struct i2c_client *client)
{
	u8 write_buf[4] = { 0 };
	int ret = 1;
	write_buf[0] = 0x88;
	ret = i2c_smbus_write_i2c_block_data(client, 0xe0, 1, &write_buf[0]);
	if (ret >= 0) {
		//printk("I write reg 0xe0\n");
	} else {
		printk("I write gsl----------erro \n");
		return;
	}
	msleep(20);

	write_buf[0] = 0x04;
	ret = i2c_smbus_write_i2c_block_data(client, 0xe4, 1, &write_buf[0]);
	if (ret >= 0) {
		//printk("I write reg 0xe4\n");
	} else {
		printk("I write gsl----------erro \n");
		return;
	}
	msleep(10);

	write_buf[0] = 0x00;
	write_buf[1] = 0x00;
	write_buf[2] = 0x00;
	write_buf[3] = 0x00;
	ret = i2c_smbus_write_i2c_block_data(client, 0xbc, 4, write_buf);
	if (ret >= 0) {
		//printk("I write reg 0xbc\n");

	} else {
		printk("I write gsl----------erro \n");
		return;
	}
	msleep(10);
#ifdef GSL9XX_CHIP
	gsl_io_control(client);
#endif

}

static void clr_reg(struct i2c_client *client)
{
	u8 write_buf[4] = { 0 };
	int ret = 1;
	write_buf[0] = 0x88;
	ret = i2c_smbus_write_i2c_block_data(client, 0xe0, 1, &write_buf[0]);
	if (ret >= 0) {
		//printk("I write reg 0xe0\n");
	} else {
		printk("I write gsl----------erro \n");
		return;
	}
	msleep(20);

	write_buf[0] = 0x03;
	ret = i2c_smbus_write_i2c_block_data(client, 0x80, 1, &write_buf[0]);
	if (ret >= 0) {
		//printk("I write reg 0x80 \n");
		return;
	} else {
		printk("I write gsl----------erro \n");
		return;
	}
	msleep(5);

	write_buf[0] = 0x04;
	ret = i2c_smbus_write_i2c_block_data(client, 0xe4, 1, &write_buf[0]);
	if (ret >= 0) {
		//printk("I write reg 0xe4\n");
	} else {
		printk("I write gsl----------erro \n");
		return;
	}
	msleep(5);

	write_buf[0] = 0x00;
	ret = i2c_smbus_write_i2c_block_data(client, 0xe0, 1, &write_buf[0]);
	if (ret >= 0) {
		//printk("I write reg 0xe0\n");
	} else {
		printk("I write gsl----------erro \n");
		return;
	}
	msleep(20);
}

#ifdef HIGH_SPEED_I2C
static u32 gsl_read_interface(struct i2c_client *client, u8 reg, u8 * buf,
			      u32 num)
{
	struct i2c_msg xfer_msg[2];

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = &reg;
	xfer_msg[0].timing = 400;

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags |= I2C_M_RD;
	xfer_msg[1].buf = buf;
	xfer_msg[1].timing = 400;

	if (reg < 0x80) {
		i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg));
		msleep(5);
	}

	return i2c_transfer(client->adapter, xfer_msg,
			    ARRAY_SIZE(xfer_msg)) ==
	    ARRAY_SIZE(xfer_msg) ? 0 : -EFAULT;
}

static u32 gsl_write_interface(struct i2c_client *client, const u8 reg,
			       u8 * buf, u32 num)
{
	struct i2c_msg xfer_msg[1];

	buf[0] = reg;

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	xfer_msg[0].flags = client->flags & I2C_M_TEN;
	xfer_msg[0].buf = buf;
	xfer_msg[0].timing = 400;

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static __inline__ void fw2buf(u8 * buf, const u32 * fw)
{
	u32 *u32_buf = (int *)buf;
	*u32_buf = *fw;
}

static void gsl_load_fw(struct i2c_client *client)
{
	u8 buf[DMA_TRANS_LEN * 4 + 1] = { 0 };
	u8 send_flag = 1;
	u8 *cur = buf + 1;
	u32 source_line = 0;
	u32 source_len;
	struct fw_data *ptr_fw;

	printk("=============gsl_load_fw start==============\n");

	ptr_fw = GSLX680_FW;
	source_len = ARRAY_SIZE(GSLX680_FW);
	for (source_line = 0; source_line < source_len; source_line++) {
		/* init page trans, set the page val */
		if (GSL_PAGE_REG == ptr_fw[source_line].offset) {
			fw2buf(cur, &ptr_fw[source_line].val);
			gsl_write_interface(client, GSL_PAGE_REG, buf, 4);
			send_flag = 1;
		} else {
			if (1 ==
			    send_flag % (DMA_TRANS_LEN <
					 0x20 ? DMA_TRANS_LEN : 0x20))
				buf[0] = (u8) ptr_fw[source_line].offset;

			fw2buf(cur, &ptr_fw[source_line].val);
			cur += 4;

			if (0 ==
			    send_flag % (DMA_TRANS_LEN <
					 0x20 ? DMA_TRANS_LEN : 0x20)) {
				gsl_write_interface(client, buf[0], buf,
						    cur - buf - 1);
				cur = buf + 1;
			}

			send_flag++;
		}
	}

	printk("=============gsl_load_fw end==============\n");

}

#else
#ifdef GSL_IDENTY_TP
static void gsl_load_fw(struct i2c_client *client,
			const struct fw_data *GSL_DOWNLOAD_DATA, int data_len)
#else
static void gsl_load_fw(struct i2c_client *client)
#endif
{
	u8 buf[SMBUS_TRANS_LEN * 4] = { 0 };
	u8 reg = 0, send_flag = 1, cur = 0;

	u32 source_len;
	int ret = 1;
	struct fw_data *ptr_fw1;
	unsigned int source_line = 0;
	ptr_fw1 = GSLX680_FW;
	source_len = ARRAY_SIZE(GSLX680_FW);

	//printk("=============gsl_load_fw start==============\n");

	for (source_line = 0; source_line < source_len; source_line++) {
		if (1 == SMBUS_TRANS_LEN) {
			reg = ptr_fw1[source_line].offset;
			memcpy(&buf[0], &ptr_fw1[source_line].val, 4);
			ret = i2c_smbus_write_i2c_block_data(client, reg, 4, buf);
			if (ret >= 0) {
			} else {
				printk("I write gsl----------erro \n");
				return;
			}
		} else {
			/* init page trans, set the page val */
			if (GSL_PAGE_REG == ptr_fw1[source_line].offset) {
				buf[0] =
				    (u8) (ptr_fw1[source_line].val &
					  0x000000ff);
				ret = i2c_smbus_write_i2c_block_data(client, GSL_PAGE_REG,
									1, &buf[0]);
				if (ret >= 0) {
				} else {
					printk("I write gsl----------erro \n");
					return;
				}
				send_flag = 1;
			} else {
				if (1 ==
				    send_flag % (SMBUS_TRANS_LEN <
						 0x08 ? SMBUS_TRANS_LEN : 0x08))
					reg = ptr_fw1[source_line].offset;

				memcpy(&buf[cur], &ptr_fw1[source_line].val, 4);
				cur += 4;

				if (0 ==
				    send_flag % (SMBUS_TRANS_LEN <
						 0x08 ? SMBUS_TRANS_LEN : 0x08))
				{
					ret = i2c_smbus_write_i2c_block_data(client, reg,
								       SMBUS_TRANS_LEN* 4, buf);
					if (ret >= 0) {
					} else {
						printk("I write gsl----------erro \n");
						return;
					}
					cur = 0;
				}

				send_flag++;

			}
		}
	}

	//printk("=============gsl_load_fw end==============\n");

}
#endif


static int test_i2c(struct i2c_client *client)
{
	u8 read_buf = 0;
	u8 write_buf = 0x12;
	int ret, rc = 1;

	ret = i2c_smbus_read_i2c_block_data(client, 0xf0, 1, &read_buf);
	if (ret < 0) {
		rc--;
		printk("I read gsl----------erro \n");
	} else
		//printk("I read reg 0xf0 is %x\n", read_buf);

	msleep(2);
	ret = i2c_smbus_write_i2c_block_data(client, 0xf0, 1, &write_buf);
	if (ret >= 0)
	{
		//printk("I write reg 0xf0 0x12\n");
	} else
	{
		printk("I write gsl----------erro \n");
	}

	msleep(2);
	ret = i2c_smbus_read_i2c_block_data(client, 0xf0, 1, &read_buf);
	if (ret < 0) {
		rc--;
		printk("I read gsl----------erro \n");
	} else
	{
		//printk("I read reg 0xf0 is 0x%x\n", read_buf);
	}

	return rc;
}

int pt_set_rst_gpio(u8 value)
{
	int rst_gpio = g_ts->rst_gpio;
	int rc = 0;

	rc = gpio_request(rst_gpio, NULL);
	if (rc < 0) {
		gpio_free(rst_gpio);
		rc = gpio_request(rst_gpio, NULL);
	}
	if (!rc) {
		rc = gpio_direction_output(rst_gpio, value);
		gpio_free(rst_gpio);
	}

	return rc;
}

int pt_set_int_gpio_mode(void)
{
	int irq_gpio = g_ts->irq_gpio;
	int rc = 0;

	if (!g_ts)
		return -EINVAL;

	rc = gpio_request(irq_gpio, NULL);
	if (rc < 0) {
		gpio_free(irq_gpio);
		rc = gpio_request(irq_gpio, NULL);
	}

	if (!rc) {
		rc = gpio_direction_input(irq_gpio);
		gpio_free(irq_gpio);
	}

	return rc;
}

static void init_chip(struct i2c_client *client)
{
	int rc;

	rc = pt_set_int_gpio_mode();
	if (rc < 0) {
		printk("------pt_set_int_gpio_mode error------\n");
	}

	pt_set_rst_gpio(0);
	msleep(100);
	pt_set_rst_gpio(1);
	msleep(100);


	rc = test_i2c(client);
	if (rc < 0) {
		printk("------gslX680 test_i2c error------\n");
		return;
	}

	clr_reg(client);
	reset_chip(client);

	gsl_load_fw(client);
	startup_chip(client);
	reset_chip(client);
	startup_chip(client);
}

/*
static void check_mem_data(struct i2c_client *client)
{
	u8 read_buf[4] = { 0 };
	int ret = 1;

	msleep(30);
	ret =
	    i2c_smbus_read_i2c_block_data(client, 0xb0, sizeof(read_buf),
					  read_buf);
	if (ret < 0) {
		printk("check_mem_data----------erro \n");
		return;
	} else {
		//printk("I read reg 0xb0 is %x\n", read_buf);
	}

	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a
	    || read_buf[0] != 0x5a) {
		printk
		    ("gsl-----------#########check mem read 0xb0 = %x %x %x %x #########\n",
		     read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
	}
}
*/

#ifdef TPD_PROC_DEBUG
static int char_to_int(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	else
		return (ch - 'a' + 10);
}

static int gsl_config_read_proc(struct seq_file *m, void *v)
{
	char temp_data[5] = { 0 };
	unsigned int tmp = 0;
	int ret = 1;
	if ('v' == gsl_read[0] && 's' == gsl_read[1]) {
#ifdef GSL_NOID_VERSION
		tmp = gsl_version_id();
#else
		tmp = 0x20121215;
#endif
		seq_printf(m, "version:%x\n", tmp);
	} else if ('r' == gsl_read[0] && 'e' == gsl_read[1]) {
		if ('i' == gsl_read[3]) {
#ifdef GSL_NOID_VERSION
			tmp = (gsl_data_proc[5] << 8) | gsl_data_proc[4];
			//ptr +=sprintf(ptr,"gsl_config_data_id[%d] = ",tmp);
			if (tmp >= 0 && tmp < ARRAY_SIZE(gsl_config_data_id)) {
				//ptr +=sprintf(ptr,"%d\n",gsl_config_data_id[tmp]);
				seq_printf(m, "%d\n", gsl_config_data_id[tmp]);
			}
#endif
		} else {
			ret = i2c_smbus_write_i2c_block_data(g_ts->client, 0xf0, 4,
							   &gsl_data_proc[4]);
			if (ret >= 0) {
				//printk("I write reg 0xf0 is %x\n", gsl_data_proc[4]);
			} else {
				printk("I write gsl----------erro \n");
				return -1;
			}

			if (gsl_data_proc[0] < 0x80)
				ret = i2c_smbus_read_i2c_block_data(g_ts->client,
								    gsl_data_proc
								    [0], 4,
								    temp_data);
			if (ret < 0) {
				printk("I read gsl----------erro \n");
				return -1;
			} else {
				//printk("I read reg %x is %x\n",
				//       gsl_data_proc[0], temp_data);
			}

			seq_printf(m, "offset : {0x%02x,0x", gsl_data_proc[0]);
			seq_printf(m, "%02x", temp_data[3]);
			seq_printf(m, "%02x", temp_data[2]);
			seq_printf(m, "%02x", temp_data[1]);
			seq_printf(m, "%02x};\n", temp_data[0]);
		}
	}
	return 0;
}

static ssize_t gsl_config_write_proc(struct file *file, const char *buffer,
				     unsigned long count, long long *data)
{
	u8 buf[8] = { 0 };
	char temp_buf[CONFIG_LEN];
	char *path_buf;
	//char temp_data[5] = {0};
	int tmp2 = 0;
	int tmp1 = 0;
	int ret = 1;
	print_info("[tp-gsl][%s] \n", __func__);
	if (count > 512) {
		print_info("size not match [%d:%ld]\n", CONFIG_LEN, count);
		return -EFAULT;
	}
	path_buf = kzalloc(count, GFP_KERNEL);
	if (!path_buf) {
		printk("alloc path_buf memory error \n");
		return -1;
	}
	if (copy_from_user(path_buf, buffer, count)) {
		print_info("copy from user fail\n");
		goto exit_write_proc_out;
	}
	memcpy(temp_buf, path_buf, (count < CONFIG_LEN ? count : CONFIG_LEN));
	print_info("[tp-gsl][%s][%s]\n", __func__, temp_buf);
	buf[3] = char_to_int(temp_buf[14]) << 4 | char_to_int(temp_buf[15]);
	buf[2] = char_to_int(temp_buf[16]) << 4 | char_to_int(temp_buf[17]);
	buf[1] = char_to_int(temp_buf[18]) << 4 | char_to_int(temp_buf[19]);
	buf[0] = char_to_int(temp_buf[20]) << 4 | char_to_int(temp_buf[21]);

	buf[7] = char_to_int(temp_buf[5]) << 4 | char_to_int(temp_buf[6]);
	buf[6] = char_to_int(temp_buf[7]) << 4 | char_to_int(temp_buf[8]);
	buf[5] = char_to_int(temp_buf[9]) << 4 | char_to_int(temp_buf[10]);
	buf[4] = char_to_int(temp_buf[11]) << 4 | char_to_int(temp_buf[12]);
	if ('v' == temp_buf[0] && 's' == temp_buf[1]) {	//version //vs
		memcpy(gsl_read, temp_buf, 4);
		printk("gsl version\n");
	} else if ('s' == temp_buf[0] && 't' == temp_buf[1]) {	//start //st
#ifdef GSL_MONITOR
		cancel_delayed_work_sync(&gsl_monitor_work);
#endif
		gsl_proc_flag = 1;
		reset_chip(g_ts->client);	//gsl_reset_core
	} else if ('e' == temp_buf[0] && 'n' == temp_buf[1]) {	//end //en
		msleep(20);
		reset_chip(g_ts->client);	//gsl_reset_core
		startup_chip(g_ts->client);	// gsl_start_core
		gsl_proc_flag = 0;
	} else if ('r' == temp_buf[0] && 'e' == temp_buf[1]) {	//read buf //
		memcpy(gsl_read, temp_buf, 4);
		memcpy(gsl_data_proc, buf, 8);
	} else if ('w' == temp_buf[0] && 'r' == temp_buf[1]) {	//write buf
		ret = i2c_smbus_write_i2c_block_data(g_ts->client, buf[4], 4, buf);
		if (ret >= 0) {
		} else {
			printk("I write gsl----------erro \n");
			return -1;
		}
	}
#ifdef GSL_NOID_VERSION
	else if ('i' == temp_buf[0] && 'd' == temp_buf[1]) {	//write id config //
		tmp1 = (buf[7] << 24) | (buf[6] << 16) | (buf[5] << 8) | buf[4];
		tmp2 = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
		if (tmp1 >= 0 && tmp1 < 512) {
			gsl_config_data_id[tmp1] = tmp2;
		}
	}
#endif
exit_write_proc_out:
	kfree(path_buf);
	return count;
}

static int gsl_server_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, gsl_config_read_proc, NULL);
}

static const struct file_operations gsl_seq_fops = {
	.open = gsl_server_list_open,
	.read = seq_read,
	.release = single_release,
	.write = gsl_config_write_proc,
	.owner = THIS_MODULE,
};
#endif

#ifdef FILTER_POINT
static void filter_point(u16 x, u16 y, u8 id)
{
	u16 x_err = 0;
	u16 y_err = 0;
	u16 filter_step_x = 0, filter_step_y = 0;

	id_sign[id] = id_sign[id] + 1;
	if (id_sign[id] == 1) {
		x_old[id] = x;
		y_old[id] = y;
	}

	x_err = x > x_old[id] ? (x - x_old[id]) : (x_old[id] - x);
	y_err = y > y_old[id] ? (y - y_old[id]) : (y_old[id] - y);

	if ((x_err > FILTER_MAX && y_err > FILTER_MAX / 3)
	    || (x_err > FILTER_MAX / 3 && y_err > FILTER_MAX)) {
		filter_step_x = x_err;
		filter_step_y = y_err;
	} else {
		if (x_err > FILTER_MAX)
			filter_step_x = x_err;
		if (y_err > FILTER_MAX)
			filter_step_y = y_err;
	}

	if (x_err <= 2 * FILTER_MAX && y_err <= 2 * FILTER_MAX) {
		filter_step_x >>= 2;
		filter_step_y >>= 2;
	} else if (x_err <= 3 * FILTER_MAX && y_err <= 3 * FILTER_MAX) {
		filter_step_x >>= 1;
		filter_step_y >>= 1;
	} else if (x_err <= 4 * FILTER_MAX && y_err <= 4 * FILTER_MAX) {
		filter_step_x = filter_step_x * 3 / 4;
		filter_step_y = filter_step_y * 3 / 4;
	}

	x_new =
	    x >
	    x_old[id] ? (x_old[id] + filter_step_x) : (x_old[id] -
						       filter_step_x);
	y_new =
	    y >
	    y_old[id] ? (y_old[id] + filter_step_y) : (y_old[id] -
						       filter_step_y);

	x_old[id] = x_new;
	y_old[id] = y_new;
}
#else

static void record_point(u16 x, u16 y, u8 id)
{
	u16 x_err = 0;
	u16 y_err = 0;

	id_sign[id] = id_sign[id] + 1;

	if (id_sign[id] == 1) {
		x_old[id] = x;
		y_old[id] = y;
	}

	x = (x_old[id] + x) / 2;
	y = (y_old[id] + y) / 2;

	if (x > x_old[id]) {
		x_err = x - x_old[id];
	} else {
		x_err = x_old[id] - x;
	}

	if (y > y_old[id]) {
		y_err = y - y_old[id];
	} else {
		y_err = y_old[id] - y;
	}

	if ((x_err > 3 && y_err > 1) || (x_err > 1 && y_err > 3)) {
		x_new = x;
		x_old[id] = x;
		y_new = y;
		y_old[id] = y;
	} else {
		if (x_err > 3) {
			x_new = x;
			x_old[id] = x;
		} else
			x_new = x_old[id];
		if (y_err > 3) {
			y_new = y;
			y_old[id] = y;
		} else
			y_new = y_old[id];
	}

	if (id_sign[id] == 1) {
		x_new = x_old[id];
		y_new = y_old[id];
	}

}
#endif

#ifdef GSL_IDENTY_TP
#define GSL_C		100
#define GSL_CHIP_1	0xfc000021	//SG
#define GSL_CHIP_2	0xfc000200	//DX
#define GSL_CHIP_3	0xfc000000	//DZ

static unsigned int gsl_count_one(unsigned int flag)
{
	unsigned int tmp = 0;
	int i = 0;

	for (i = 0; i < 32; i++) {
		if (flag & (0x1 << i)) {
			tmp++;
		}
	}
	return tmp;
}

static int gsl_identify_tp(struct i2c_client *client)
{
	u8 buf[4];
	int i, err = 1;
	int flag = 0;
	unsigned int tmp, tmp0;
	unsigned int tmp1, tmp2, tmp3;
	u32 num;

identify_tp_repeat:
	clr_reg(client);
	reset_chip(client);
	num = ARRAY_SIZE(GSL_TP_CHECK);
	gsl_load_fw(client, GSL_TP_CHECK, num);
	startup_chip(client);
	msleep(200);
	i2c_smbus_read_i2c_block_data(client, 0xb4, 4, buf);
	print_info("the test 0xb4 = {0x%02x%02x%02x%02x}\n", buf[3], buf[2],
		   buf[1], buf[0]);

	if (((buf[3] << 8) | buf[2]) > 1) {
		print_info("[TP-GSL][%s] is start ok\n", __func__);
		msleep(20);
		i2c_smbus_read_i2c_block_data(client, 0xb8, 4, buf);
		tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
		print_info("the test 0xb8 = {0x%02x%02x%02x%02x}\n", buf[3],
			   buf[2], buf[1], buf[0]);

		tmp1 = gsl_count_one(GSL_CHIP_1 ^ tmp);
		tmp0 = gsl_count_one((tmp & GSL_CHIP_1) ^ GSL_CHIP_1);
		tmp1 += tmp0 * GSL_C;
		print_info("[TP-GSL] tmp1 = %d\n", tmp1);

		tmp2 = gsl_count_one(GSL_CHIP_2 ^ tmp);
		tmp0 = gsl_count_one((tmp & GSL_CHIP_2) ^ GSL_CHIP_2);
		tmp2 += tmp0 * GSL_C;
		print_info("[TP-GSL] tmp2 = %d\n", tmp2);

		tmp3 = gsl_count_one(GSL_CHIP_3 ^ tmp);
		tmp0 = gsl_count_one((tmp & GSL_CHIP_3) ^ GSL_CHIP_3);
		tmp3 += tmp0 * GSL_C;
		print_info("[TP-GSL] tmp3 = %d\n", tmp3);

		if (0xffffffff == GSL_CHIP_1) {
			tmp1 = 0xffff;
		}
		if (0xffffffff == GSL_CHIP_2) {
			tmp2 = 0xffff;
		}
		if (0xffffffff == GSL_CHIP_3) {
			tmp3 = 0xffff;
		}

		print_info("[TP-GSL] tmp1 = %d\n", tmp1);
		print_info("[TP-GSL] tmp2 = %d\n", tmp2);
		print_info("[TP-GSL] tmp3 = %d\n", tmp3);

		tmp = tmp1;
		if (tmp1 > tmp2) {
			tmp = tmp2;
		}
		if (tmp > tmp3) {
			tmp = tmp3;
		}

		if (tmp == tmp1) {

			gsl_tp_type = 1;
		} else if (tmp == tmp2) {

			gsl_tp_type = 2;
		} else if (tmp == tmp3) {

			gsl_tp_type = 3;
		} else {

			gsl_tp_type = 1;
		}

		err = 1;
	} else {
		flag++;
		if (flag < 1) {
			goto identify_tp_repeat;
		}
		err = 0;
	}
	return err;
}
#endif

u8 rs_value1 = 0;
#ifdef TOUCHSWAP
int touchswap = 0;		//TOUCHSWAP;
#endif

void tpd_down(int id, int x, int y, int p)
{
    //x = MAX_X_RES - x;
    y = MAX_X_RES - y;
	//printk("gsl----------------tpd_down2 id: %d, x:%d, y:%d------ \n", id, x, y);
	input_report_key(g_ts->input_dev, BTN_TOUCH, 1);
	input_report_abs(g_ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
	input_report_abs(g_ts->input_dev, ABS_MT_POSITION_X, y);
	input_report_abs(g_ts->input_dev, ABS_MT_POSITION_Y, x);
	input_report_abs(g_ts->input_dev, ABS_MT_TRACKING_ID, id);
	input_mt_sync(g_ts->input_dev);
}

void tpd_up(void)
{
	print_info("------tpd_up------ \n");

	input_report_key(g_ts->input_dev, BTN_TOUCH, 0);
	input_mt_sync(g_ts->input_dev);

}

static void report_data_handle(void)
{
	u8 touch_data[MAX_FINGERS * 4 + 4] = { 0 };
	u8 buf[4] = { 0 };
	unsigned char id, point_num = 0;
	unsigned int x, y, temp_a, temp_b, i;
	int ret = 1;
#ifdef GSL_NOID_VERSION
	struct gsl_touch_info cinfo = { {0}, {0}, {0}, 0 };
	int tmp1 = 0;
#endif
	//printk("i2c_lock_flag:%d, gsl_proc_flag:%d\n", i2c_lock_flag, gsl_proc_flag);
#ifdef GSL_MONITOR
	if (i2c_lock_flag != 0)
		return;
	else
		i2c_lock_flag = 1;
#endif

#ifdef TPD_PROC_DEBUG
	if (gsl_proc_flag == 1)
		return;
#endif
	//printk("tp-gsl-------------  finger_num--------report_data_handle= \n");
	ret = i2c_smbus_read_i2c_block_data(g_ts->client, 0x80, 4, &touch_data[0]);
	if (ret < 0) {
		printk("I read gsl----------erro \n");
		return;
	} else {
		//printk("I read reg 0x80 is %x\n", touch_data[0]);
	}

	point_num = touch_data[0];
	if (point_num > 0){
			ret = i2c_smbus_read_i2c_block_data(g_ts->client, 0x84, 8,
								&touch_data[4]);
		if (ret < 0) {
			printk("I read gsl----------erro \n");
			return;
		} else {
		//	printk("I read reg 0x84 is %x\n", touch_data[4]);
		}
	}


	if (point_num > 2){
		ret = i2c_smbus_read_i2c_block_data(g_ts->client, 0x8c, 8,
				&touch_data[12]);
		if (ret < 0) {
			printk("I read gsl----------erro \n");
			return;
		} else {
		//	printk("I read reg 0x8c is %x\n", touch_data[12]);
		}
	}

	if (point_num > 4){
		ret = i2c_smbus_read_i2c_block_data(g_ts->client, 0x94, 8,
						    &touch_data[20]);
		if (ret < 0) {
			printk("I read gsl----------erro \n");
			return;
		} else {
		//	printk("I read reg 0x94 is %x\n", touch_data[20]);
		}
	}

	if (point_num > 6){
		ret = i2c_smbus_read_i2c_block_data(g_ts->client, 0x9c, 8,
						    &touch_data[28]);
		if (ret < 0) {
			printk("I read gsl----------erro \n");
			return;
		} else {
		//	printk("I read reg 0x9c is %x\n", touch_data[28]);
		}
	}

	if (point_num > 8){
		ret = i2c_smbus_read_i2c_block_data(g_ts->client, 0xa4, 8,
						    &touch_data[36]);
		if (ret < 0) {
			printk("I read gsl----------erro \n");
			return;
		} else {
		//	printk("I read reg 0xa4 is %x\n", touch_data[36]);
		}
	}

#ifdef GSL_NOID_VERSION
	cinfo.finger_num = point_num;
	//printk("tp-gsl-------------  finger_num = %d\n",cinfo.finger_num);
	for (i = 0; i < (point_num < MAX_CONTACTS ? point_num : MAX_CONTACTS);
	     i++) {
		temp_a = touch_data[(i + 1) * 4 + 3] & 0x0f;
		temp_b = touch_data[(i + 1) * 4 + 2];
		cinfo.x[i] = temp_a << 8 | temp_b;
		temp_a = touch_data[(i + 1) * 4 + 1];
		temp_b = touch_data[(i + 1) * 4 + 0];
		cinfo.y[i] = temp_a << 8 | temp_b;
		cinfo.id[i] = ((touch_data[(i + 1) * 4 + 3] & 0xf0) >> 4);
		//printk("tp-gsl-------------  before: x[%d] = %d, y[%d] = %d, id[%d] = %d \n",i,cinfo.x[i],i,cinfo.y[i],i,cinfo.id[i]);
	}
	cinfo.finger_num = (touch_data[3] << 24) | (touch_data[2] << 16) |
	    (touch_data[1] << 8) | touch_data[0];
	gsl_alg_id_main(&cinfo);
	tmp1 = gsl_mask_tiaoping();
	print_info("[tp-gsl] tmp1=%x\n", tmp1);
	if (tmp1 > 0 && tmp1 < 0xffffffff) {
		buf[0] = 0xa;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		ret = i2c_smbus_write_i2c_block_data(g_ts->client, 0xf0, 4, buf);
		if (ret >= 0) {
		} else {
					printk("I write gsl 0x8-------erro \n");
					return;
		}
		buf[0] = (u8) (tmp1 & 0xff);
		buf[1] = (u8) ((tmp1 >> 8) & 0xff);
		buf[2] = (u8) ((tmp1 >> 16) & 0xff);
		buf[3] = (u8) ((tmp1 >> 24) & 0xff);
		print_info
		    ("tmp1=%08x,buf[0]=%02x,buf[1]=%02x,buf[2]=%02x,buf[3]=%02x\n",
		     tmp1, buf[0], buf[1], buf[2], buf[3]);
		ret = i2c_smbus_write_i2c_block_data(g_ts->client, 0x8, 4, buf);
		if (ret >= 0) {
		} else {
					printk("I write gsl 0x8-------erro \n");
					return;
		}
	}
	point_num = cinfo.finger_num;
#endif

	for (i = 1; i <= MAX_CONTACTS; i++) {
		if (point_num == 0)
			id_sign[i] = 0;
		id_state_flag[i] = 0;
	}
	for (i = 0; i < (point_num < MAX_FINGERS ? point_num : MAX_FINGERS);
	     i++) {
#ifdef GSL_NOID_VERSION
		id = cinfo.id[i];
		x = cinfo.x[i];
		y = cinfo.y[i];
#else
		id = touch_data[(i + 1) * 4 + 3] >> 4;
		temp_a = touch_data[(i + 1) * 4 + 3] & 0x0f;
		temp_b = touch_data[(i + 1) * 4 + 2];
		x = temp_a << 8 | temp_b;
		temp_a = touch_data[(i + 1) * 4 + 1];
		temp_b = touch_data[(i + 1) * 4 + 0];
		y = temp_a << 8 | temp_b;
#endif

		if (1 <= id && id <= MAX_CONTACTS) {
#ifdef FILTER_POINT
			filter_point(x, y, id);
#else
			record_point(x, y, id);
#endif
			tpd_down(id, x_new, y_new, 10);
			id_state_flag[id] = 1;
		}
	}
	for (i = 1; i <= MAX_CONTACTS; i++) {
		if ((0 == point_num)
		    || ((0 != id_state_old_flag[i])
			&& (0 == id_state_flag[i]))) {
			id_sign[i] = 0;
		}
		id_state_old_flag[i] = id_state_flag[i];
	}
	if (0 == point_num) {
		tpd_up();
	}
	input_sync(g_ts->input_dev);
#ifdef GSL_MONITOR
	i2c_lock_flag = 0;
#endif
}

#ifdef GSL_MONITOR
static void gsl_monitor_worker(struct work_struct *work)
{
	//u8 write_buf[4] = {0};
	u8 read_buf[4] = { 0 };
	char init_chip_flag = 0;
	int ret = 1;
	print_info("----------------gsl_monitor_worker-----------------\n");

	if (i2c_lock_flag != 0)
		goto queue_monitor_work;
	else
		i2c_lock_flag = 1;

	ret = i2c_smbus_read_i2c_block_data(g_ts->client, 0xb0, 4, read_buf);
	if (ret < 0) {
		printk("I read gsl----------erro \n");
		return;
	} else
	//	printk("I read reg 0xb0 is %x\n", read_buf);
	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a
	    || read_buf[0] != 0x5a)
		b0_counter++;
	else
		b0_counter = 0;

	if (b0_counter > 1) {
		//printk("======read 0xb0: %x %x %x %x ======\n",read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		init_chip_flag = 1;
		b0_counter = 0;
		goto queue_monitor_init_chip;
	}

	i2c_smbus_read_i2c_block_data(g_ts->client, 0xb4, 4, read_buf);

	int_2nd[3] = int_1st[3];
	int_2nd[2] = int_1st[2];
	int_2nd[1] = int_1st[1];
	int_2nd[0] = int_1st[0];
	int_1st[3] = read_buf[3];
	int_1st[2] = read_buf[2];
	int_1st[1] = read_buf[1];
	int_1st[0] = read_buf[0];

	if (int_1st[3] == int_2nd[3] && int_1st[2] == int_2nd[2]
	    && int_1st[1] == int_2nd[1] && int_1st[0] == int_2nd[0]) {
		//printk("======int_1st: %x %x %x %x , int_2nd: %x %x %x %x ======\n",int_1st[3], int_1st[2], int_1st[1], int_1st[0], int_2nd[3], int_2nd[2],int_2nd[1],int_2nd[0]);
		init_chip_flag = 1;
		goto queue_monitor_init_chip;
	}
#if 1				//version 1.4.0 or later than 1.4.0 read 0xbc for esd checking
	ret = i2c_smbus_read_i2c_block_data(g_ts->client, 0xbc, 4, read_buf);
	if (ret < 0) {
		printk("I read gsl----------erro \n");
	} else
	//	printk("I read reg 0xbc is %x\n", read_buf);
	if (read_buf[3] != 0 || read_buf[2] != 0 || read_buf[1] != 0
	    || read_buf[0] != 0)
		bc_counter++;
	else
		bc_counter = 0;
	if (bc_counter > 1) {
		//printk("======read 0xbc: %x %x %x %x======\n",read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		init_chip_flag = 1;
		bc_counter = 0;
	}
#else
	write_buf[3] = 0x01;
	write_buf[2] = 0xfe;
	write_buf[1] = 0x10;
	write_buf[0] = 0x00;
	i2c_smbus_write_i2c_block_data(g_ts->client, 0xf0, 4, write_buf);
	i2c_smbus_read_i2c_block_data(g_ts->client, 0x10, 4, read_buf);
	i2c_smbus_read_i2c_block_data(g_ts->client, 0x10, 4, read_buf);

	if (read_buf[3] < 10 && read_buf[2] < 10 && read_buf[1] < 10
	    && read_buf[0] < 10)
		dac_counter++;
	else
		dac_counter = 0;

	if (dac_counter > 1) {
		printk("======read DAC1_0: %x %x %x %x ======\n", read_buf[3],
		       read_buf[2], read_buf[1], read_buf[0]);
		init_chip_flag = 1;
		dac_counter = 0;
	}
#endif
queue_monitor_init_chip:
	if (init_chip_flag)
		init_chip(g_ts->client);

	i2c_lock_flag = 0;

queue_monitor_work:
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 100);
}
#endif

#ifdef CONFIG_EMDOOR_TP_ATA_TEST
int tp_check_flag;
//extern void eboda_support_tp_check_put(int loadcapacity);

#if defined(ATA_TP_ADDR)
#define RAWDATA_ADDR	ATA_TP_ADDR
#else
#define RAWDATA_ADDR	0x5f80
#endif

#if defined(ATA_TP_DRV_NUM)
#define DRV_NUM			ATA_TP_DRV_NUM
#else
#define DRV_NUM			15
#endif

#if defined(ATA_TP_RAWDATA_THRESHOLD)
#define RAWDATA_THRESHOLD			ATA_TP_RAWDATA_THRESHOLD
#else
#define RAWDATA_THRESHOLD			6000
#endif
#if defined(ATA_TP_DAC_THRESHOLD)
#define DAC_THRESHOLD			ATA_TP_DAC_THRESHOLD
#else
#define DAC_THRESHOLD			20
#endif

#if defined(ATA_TP_SEN_NUM)
#define SEN_NUM			ATA_TP_SEN_NUM
#else
#define SEN_NUM			10
#endif

#if defined(ATA_TP_SEN_ORDER)
#define SEN_ORDER			ATA_TP_SEN_ORDER
#else
#define SEN_ORDER			{0,1,2,3,4,5,6,7,8,9,10,11,12,13}
#endif

static const u8 sen_order[SEN_NUM] = SEN_ORDER;

int ctp_factory_test(void)
{
	u8 buf[4];
	int i, offset;
	u32 rawdata_value, dac_value;

#if defined(ATA_TP_T8880_MISS_A_DRV)
	u8 cnt = 0;
#endif
	struct i2c_client *client = g_ts->client;

	printk("=====>RAWDATA_ADDR=0x%x=====>\n", RAWDATA_ADDR);
	printk("=====>DRV_NUM=%d=====>\n", DRV_NUM);
	printk("=====>RAWDATA_THRESHOLD=%d=====>\n", RAWDATA_THRESHOLD);
	printk("=====>DAC_THRESHOLD=%d=====>\n", DAC_THRESHOLD);
	printk("=====>SEN_NUM=%d=====>\n", SEN_NUM);

	if (!client) {
		printk("err ,client is NULL,ctp_factory_test\n");
		return -1;
	}

	msleep(800);
	for (i = 0; i < DRV_NUM; i++) {
		buf[3] = 0;
		buf[2] = 0;
		buf[1] = 0;
		buf[0] = (RAWDATA_ADDR + SEN_NUM * 2 * i) / 0x80;
		offset = (RAWDATA_ADDR + SEN_NUM * 2 * i) % 0x80;
		i2c_smbus_write_i2c_block_data(client, 0xf0, 4, buf);
		i2c_smbus_read_i2c_block_data(client, offset, 4, buf);
		i2c_smbus_read_i2c_block_data(client, offset, 4, buf);
		rawdata_value = (buf[1] << 8) + buf[0];
		printk(KERN_EMERG "%s,rawdata_value = %d\n", __func__,
		       rawdata_value);
		if (rawdata_value > RAWDATA_THRESHOLD) {
			rawdata_value = (buf[3] << 8) + buf[2];
			printk(KERN_EMERG "%s,===>rawdata_value = %d\n",
			       __func__, rawdata_value);

			if (rawdata_value > RAWDATA_THRESHOLD) {
				printk(KERN_EMERG
				       "%s, ############>rawdata_value = %d\n",
				       __func__, rawdata_value);
#if defined(ATA_TP_T8880_MISS_A_DRV)
				cnt++;
				if (cnt <= ATA_TP_T8880_MISS_A_DRV)
					continue;
#endif
				return -1;	//fail
			}
		}
	}

	for (i = 0; i < SEN_NUM; i++) {
		buf[3] = 0x01;
		buf[2] = 0xfe;
		buf[1] = 0x10;
		buf[0] = 0x00;
		offset = 0x10 + (sen_order[i] / 4) * 4;
		i2c_smbus_write_i2c_block_data(client, 0xf0, 4, buf);
		i2c_smbus_read_i2c_block_data(client, offset, 4, buf);
		i2c_smbus_read_i2c_block_data(client, offset, 4, buf);

		dac_value = buf[sen_order[i] % 4];
		printk(KERN_EMERG
		       "================dac_value = %d DAC_THRESHOLD = %d===================\n",
		       dac_value, DAC_THRESHOLD);
		if (dac_value < DAC_THRESHOLD)
			return -1;	//fail
	}

	return 0;		//pass
}
#endif

static int touch_event_handler(void *unused)
{
	struct sched_param param = {.sched_priority = 1 };
	//   struct sched_param param;
	sched_setscheduler(current, SCHED_RR, &param);
	printk("gsl-----------===touch_event_handler, task running===\n");

	do {
		set_current_state(TASK_INTERRUPTIBLE);
		set_current_state(TASK_RUNNING);
		wait_event_interruptible(waiter, tpd_flag != 0);
		tpd_flag = 0;
		//       TPD_DEBUG_SET_TIME;
		set_current_state(TASK_RUNNING);
		//printk("gsl-----------===touch_event_handler, task running===\n");

		eint_flag = 0;
		report_data_handle();

	} while (!kthread_should_stop());

	return 0;
}

static irqreturn_t tpd_eint_interrupt_handler(int irq, void *dev_id)
{
        if(g_ts->status == 0)
        {
                pr_info("%s gsl->status is not init\n", __func__);
	        return IRQ_HANDLED;
        }

	eint_flag = 1;
	tpd_flag = 1;
	wake_up_interruptible(&waiter);
	return IRQ_HANDLED;
}

static int gtp_parse_dt(struct device *dev, struct gsl_ts_data *ts)
{
	struct device_node *np = dev->of_node;
	ts->irq_gpio = of_get_named_gpio(np, "irq-gpios", 0);
	//dev_info(dev, "ts->irq_gpio %d\n", ts->irq_gpio);
	if (!gpio_is_valid(ts->irq_gpio))
		dev_err(dev, "No valid irq gpio");

	ts->rst_gpio = of_get_named_gpio(np, "reset-gpios", 0);
	//dev_info(dev, "ts>reset-gpios %d\n", ts->rst_gpio);
	if (!gpio_is_valid(ts->rst_gpio))
		dev_err(dev, "No valid rst gpio");

	return 0;
}

/*
static int tpd_i2c_detect(struct i2c_client *client, struct i2c_board_info *info)
{
    strcpy(info->type, TPD_DEVICE);
    return 0;
}
*/
static int tpd_irq_registration(struct gsl_ts_data *ts, int on)
{
	int rc = 0;

	if (on) {
		/* Initialize IRQ */
		ts->irq_num = gpio_to_irq(ts->irq_gpio);

		if (ts->irq_num < 0)
			return -EINVAL;

		//printk("initialize threaded irq=%d\n", ts->irq_num);

		/* use edge triggered interrupts */

		rc = request_irq(ts->irq_num, (irq_handler_t)tpd_eint_interrupt_handler,
				IRQF_TRIGGER_FALLING, "TOUCH_PANEL-eint", NULL);
		if (rc < 0)
			printk("%s: Error, could not request irq\n");
	} else {
		disable_irq_nosync(ts->irq_num);
		free_irq(ts->irq_num, ts);
	}
	return rc;
}

static int gsl_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err = 0;
	int ret = -1;
	int retval;
	struct gsl_ts_data *ts;
	printk("==tpd_i2c_probe==i2c\n");
	ts = kzalloc(sizeof(struct gsl_ts_data), GFP_KERNEL);
	if (!ts) {
		print_info("alloc ts memory error\n");
		return -ENOMEM;
	}
        memset(ts, 0, sizeof(struct gsl_ts_data));
	g_ts = ts;

	ts->client = client;
	ts->reg = regulator_get(&client->dev, "vdd_ldo27");
	retval = regulator_enable(ts->reg);
	if (retval != 0) {
		printk("Failed to enable reg voltage: %d\n", retval);
		return -1;
	}

	ret = gtp_parse_dt(&client->dev, ts);
	if (ret) {
		dev_err(&client->dev, "Failed parse dts\n");
		return -1;
	}

	if (ts->client->addr != GSLX680_ADDR) {
		printk("==gsl--------------erro----------addr=0x%x\n",
		       client->addr);
		ts->client->addr = GSLX680_ADDR;

	}

        ts->pinctrl = devm_pinctrl_get(&client->dev);
        if (IS_ERR(ts->pinctrl)) {
                pr_err("failed to get irq pinctrl");
                ts->pinctrl = NULL;
		return -EINVAL;
        }

        ts->irq_on = pinctrl_lookup_state(ts->pinctrl, "irq_on");
        if (IS_ERR(ts->irq_on)) {
                pr_err("failed to get irq_on pin state\n");
        }
        ts->irq_off = pinctrl_lookup_state(ts->pinctrl, "irq_off");
        if (IS_ERR(ts->irq_off)) {
                pr_err("failed to get irq_off pin state\n");
        }
        if (ts->pinctrl && ts->irq_on) {
                if (pinctrl_select_state(ts->pinctrl, ts->irq_on)) {
                        pr_err("failed to turn on irq\n");
                }
        }

	msleep(100);
	init_chip(ts->client);
	//check_mem_data(ts->client);
	/* register input device */
	ts->input_dev = input_allocate_device();
	if (!ts->input_dev) {
		printk("Failed to allocate input device\n");
		return -EINVAL;
	}
	ts->input_dev->phys = "gslX680_tp/input1";
	ts->input_dev->name = GSLX680_NAME;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->dev.parent = &ts->client->dev;
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, ts->input_dev->keybit);
	set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
        input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, MAX_X_RES, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, MAX_Y_RES, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, (MAX_CONTACTS + 1), 0, 0);

	retval = input_register_device(ts->input_dev);
	if (retval) {
		printk("Register %s input device failed\n", ts->input_dev->name);
		input_free_device(ts->input_dev);
		return -EINVAL;
	}

	tpd_irq_registration(ts, 1);

	thread = kthread_run(touch_event_handler, 0, "GLSTP");
	if (IS_ERR(thread)) {
		err = PTR_ERR(thread);
		printk(" failed to create kernel thread: %d\n", err);
	}
#ifdef GSL_MONITOR
	//printk("tpd_i2c_probe () : queue gsl_monitor_workqueue\n");
	INIT_DELAYED_WORK(&gsl_monitor_work, gsl_monitor_worker);
	gsl_monitor_workqueue =
	    create_singlethread_workqueue("gsl_monitor_workqueue");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 1000);
#endif
#ifdef TPD_PROC_DEBUG
	proc_create(GSL_CONFIG_PROC_FILE, 0666, NULL, &gsl_seq_fops);
	gsl_proc_flag = 0;
#endif
	printk("==tpd_i2c_probe end==\n");
        g_ts->status = 1;
	return 0;
}

#ifdef MTK_GSL
static struct i2c_board_info __initdata gslX680_i2c_tpd =
    { I2C_BOARD_INFO(GSL_I2C_NAME, (GSLX680_ADDR)) };
#else
static unsigned short force[] =
    { 0, (GSLX680_ADDR << 1), I2C_CLIENT_END, I2C_CLIENT_END };
static const unsigned short *const forces[] = { force, NULL };
#endif

/* Function to manage low power suspend */

static int gsl_pm_suspend(struct device *h)
{
	printk("==gsl_suspend==\n");
	tpd_halt = 1;
	disable_irq(g_ts->irq_num);
#ifdef GSL_MONITOR
	printk("gsl_ts_suspend () : cancel gsl_monitor_work\n");
	cancel_delayed_work_sync(&gsl_monitor_work);
#endif
	pt_set_rst_gpio(0);
        if (g_ts->pinctrl && g_ts->irq_off) {
                if (pinctrl_select_state(g_ts->pinctrl, g_ts->irq_off)) {
                        pr_err("failed to turn off tp irq\n");
                }
        }
	return 0;
}

/* Function to manage power-on resume */
static int gsl_pm_resume(struct device *h)
{
	printk("==gls_resume==\n");
	pt_set_rst_gpio(1);
	msleep(20);
	reset_chip(g_ts->client);
	startup_chip(g_ts->client);
	//check_mem_data(g_ts->client);
#ifdef GSL_MONITOR
	printk("gsl_ts_resume () : queue gsl_monitor_work\n");
	queue_delayed_work(gsl_monitor_workqueue, &gsl_monitor_work, 300);
#endif
        if (g_ts->pinctrl && g_ts->irq_on) {
                if (pinctrl_select_state(g_ts->pinctrl, g_ts->irq_on)) {
                        pr_err("failed to turn on ts irq\n");
                }
        }
	enable_irq(g_ts->irq_num);
	tpd_halt = 0;
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id gsl_dt_ids[] = {
	{.compatible = "gslx680"},
	{}
};
#endif

static int gsl_remove(struct i2c_client *client)
{
	printk("==tpd_i2c_remove==\n");
	return 0;
}

static const struct dev_pm_ops gsl_pm_ops = {
	.suspend = gsl_pm_suspend,
	.resume = gsl_pm_resume,
};

static struct i2c_driver gsl_driver = {
	.driver = {
		   .name = GTP_I2C_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(gsl_dt_ids),
		   .pm = &gsl_pm_ops,
		   },
	.probe = gsl_probe,
	.remove = gsl_remove,
};

/* called when loaded into kernel */
static int __init gsl_driver_init(void)
{

	//printk("Sileadinc gslX680 touch panel driver init\n");
	return i2c_add_driver(&gsl_driver);
}

/* should never be called */
static void __exit gsl_driver_exit(void)
{
	//printk("Sileadinc gslX680 touch panel driver exit\n");
	i2c_del_driver(&gsl_driver);
}

module_init(gsl_driver_init);
module_exit(gsl_driver_exit);

MODULE_AUTHOR("Jingbo Lu <lujb0727@thundersoft>");
MODULE_DESCRIPTION("GSL I2C Touch Driver");
MODULE_LICENSE("GPL v2");
