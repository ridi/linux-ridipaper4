/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung kd101n65 Panel driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <video/mipi_display.h>
#include "exynos_panel_drv.h"
#include "../dsim.h"

#define   LCM_DSI_CMD_MODE	0

#define REGFLAG_DELAY             							0XFE
#define REGFLAG_END_OF_TABLE      							0xEFF   // END OF REGISTERS MARKER
extern int exynos_panel_pwm(struct exynos_panel_device *panel, int is_on);
#if !defined(CONFIG_MACH_RP400)
//extern void sys1p_gpio_up(void);
#endif
/*
struct LCM_setting_table
{
    unsigned cmd;
    unsigned char count;
    unsigned char para_list[64];
};

static struct LCM_setting_table lcm_initialization_setting[] =
{

    {0xFE,1,{0x01}},
    {0x5F,1,{0x12}},	//3lane

    {0xFE,1,{0x00}},
    {0x58,1,{0xAD}},
    {0x35,1,{0x01}},

    {0x11,1,{0x00}},  	// SLPOUT
    {REGFLAG_DELAY, 120, {}},
    {0x29,1,{0x00}},  	// DSPON
    {REGFLAG_DELAY, 120, {}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_sleep_out_setting[] =
{
    // Sleep Out
    {0x11, 1, {0x00}},
    {REGFLAG_DELAY, 120, {}},

    // Display ON
    {0x29, 1, {0x00}},
    {REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_deep_sleep_mode_in_setting[] =
{
    // Display off sequence
    {0x28, 1, {0x00}},

    // Sleep Mode On
    {0x10, 1, {0x00}},

    {REGFLAG_END_OF_TABLE, 0x00, {}}
};

static void push_table(struct dsim_device *dsim, struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
    unsigned int i;
    printk("[Kernel/LCM A00] push_table() enter\n");
    for(i = 0; i < count; i++)
    {
        unsigned cmd;
        cmd = table[i].cmd;

        switch (cmd)
        {
            case REGFLAG_DELAY :
                mdelay(table[i].count);
                break;
            case REGFLAG_END_OF_TABLE :
                break;
            default:
                //dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
                if (dsim_wr_data(dsim, MIPI_DSI_DCS_LONG_WRITE, table[i].para_list, table[i].count, force_update) < 0)
					dsim_err("fail to write reg=0x%x command.\n", cmd);
        }
    }
}
*/
#if !defined(CONFIG_MACH_RP400)
extern int lt9611_disable_irq(void);
extern int lt9611_i2c_suspend(void);
extern int lt9611_i2c_resume(void);
#endif
static int kd101n65_suspend(struct exynos_panel_device *panel)
{

        DPU_INFO_PANEL("%s +\n", __func__);
#if !defined(CONFIG_MACH_RP400)
        lt9611_disable_irq();
#endif
	exynos_panel_pwm(panel, 0);
#if !defined(CONFIG_MACH_RP400)
        /*
        if (pinctrl_select_state(panel->res.pinctrl,
						panel->res.irq_input)) {
				DPU_ERR_PANEL("failed to config lt9211/lt9611 interrupt pin state \n");
	} else {
				DPU_DEBUG_PANEL("pinctrl select lt9611/lt9211 interrupt pin state success");
	}
*/
	if (pinctrl_select_state(panel->res.pinctrl,
						panel->res.panel_off)) {
				DPU_ERR_PANEL("failed to turn on panel panel off\n");
	} else {
				DPU_DEBUG_PANEL("pinctrl select panel_off state success");
	}

        lt9611_i2c_suspend();
#endif
	return 0;
}
int kd101n65_panel_init(void)
{
	struct dsim_device *dsim = get_dsim_drvdata(0);
	u8 data[2];

	DPU_INFO_PANEL("%s +\n", __func__);

	data[0] = 0x11;
	data[1] = 0x00;
	//SET MIPI CMD enable
	if (dsim_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, data, 2, 1) < 0)
		dsim_err("fail to send 0x11 display on command.\n");

	mdelay(300);
	data[0] = 0x29;
	data[1] = 0x00;
	//SET MIPI CMD enable
	if (dsim_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, data, 2, 1) < 0)
		dsim_err("fail to send 0x11 display on command.\n");
	mdelay(300);

	//exynos_panel_pwm(panel, 1);
	DPU_INFO_PANEL("%s -\n", __func__);
	return 0;
}

EXPORT_SYMBOL(kd101n65_panel_init);

static int kd101n65_displayon(struct exynos_panel_device *panel)
{
	struct dsim_device *dsim = get_dsim_drvdata(0);
	u8 data[2];

	DPU_INFO_PANEL("%s +\n", __func__);
#if !defined(CONFIG_MACH_RP400)
	if (pinctrl_select_state(panel->res.pinctrl,
						panel->res.panel_on)) {
				DPU_ERR_PANEL("failed to turn on panel panel on\n");
	} else {
				DPU_DEBUG_PANEL("pinctrl select panel_on state success");
	}
        lt9611_i2c_resume();
	//sys1p_gpio_up();
#endif
/*	data[0] = 0xb0;
	data[1] = 0x5a;
	//SET MIPI CMD enable
	if (dsim_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, data, 2, 1) < 0)
		dsim_err("fail to send 0x11 display on command.\n");

    data[0] = 0xb1;
	data[1] = 0x00;
	//SET MIPI CMD enable
	if (dsim_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, data, 2, 1) < 0)
		dsim_err("fail to send 0x11 display on command.\n");

    data[0] = 0x91;
	data[1] = 0x16;
	//SET MIPI CMD enable
	if (dsim_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, data, 2, 1) < 0)
		dsim_err("fail to send 0x11 display on command.\n");
*/

    data[0] = 0x11;
	data[1] = 0x00;
	//SET MIPI CMD enable
	if (dsim_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, data, 2, 1) < 0)
		dsim_err("fail to send 0x11 display on command.\n");

	mdelay(10);
    data[0] = 0x29;
	data[1] = 0x00;
	//SET MIPI CMD enable
	if (dsim_wr_data(dsim, MIPI_DSI_DCS_SHORT_WRITE_PARAM, data, 2, 1) < 0)
		dsim_err("fail to send 0x11 display on command.\n");
	mdelay(10);
    DPU_INFO_PANEL("%s -\n", __func__);
	return 0;
}

static int kd101n65_mres(struct exynos_panel_device *panel, int mres_idx)
{
    DPU_INFO_PANEL("%s \n", __func__);
	return 0;
}

static int kd101n65_doze(struct exynos_panel_device *panel)
{
	return 0;
}

static int kd101n65_doze_suspend(struct exynos_panel_device *panel)
{
	return 0;
}

static int kd101n65_dump(struct exynos_panel_device *panel)
{
	return 0;
}

static int kd101n65_read_state(struct exynos_panel_device *panel)
{
	return 0;
}
/*
void register_pwm_device(void)
{
	struct file *f_export = NULL;
	mm_segment_t old_fs;
	ssize_t write_byte = 0;
	f_export = filp_open("/sys/class/pwm/pwmchip0/export", O_WRONLY, 0);
	if (IS_ERR(f_export)) {
		pr_err("%s:filp_open backlight pwm chip failed !!!", __func__);
	} else {
		old_fs = get_fs();
		set_fs(get_ds());
		//enable pwm1 chip
		write_byte = f_export->f_op->write(f_export, "1", 1, &f_export->f_pos);
		if(write_byte != 1)
		{
			pr_err("enable pwm1 failed \n");
		}
		set_fs(old_fs);
	}
}

static void backlight_pwm_config(uint8_t value)
{
	struct file *f_period = NULL;
	struct file *f_enable = NULL;
	struct file *f_duty = NULL;
	mm_segment_t old_fs;
	ssize_t write_byte = 0;
	char duty_cycle[16];
	int str_len = 0;

	f_period = filp_open("/sys/class/pwm/pwmchip0/pwm1/period", O_RDWR, 0);
	if (IS_ERR_OR_NULL(f_period)) {
		pr_err("%s:filp_open backlight pwm1 period file failed !!!", __func__);
		goto out_err2;
	}
	old_fs = get_fs();
	set_fs(get_ds());
	write_byte = f_period->f_op->write(f_period, "2000", 4, &f_period->f_pos);
	if(write_byte != 4)
	{
		pr_err("enable pwm1 period failed \n");
		goto out_err2;
	}
	set_fs(old_fs);
	f_duty = filp_open("/sys/class/pwm/pwmchip0/pwm1/duty_cycle", O_RDWR, 0);
	if (IS_ERR_OR_NULL(f_duty)) {
		pr_err("%s:filp_open backlight pwm1 duty_cycle file failed !!!", __func__);
		goto out_err2;
	}
	value = (2000 * value) / 255;
	pr_err("value=%d \n", value);
	str_len = sprintf(duty_cycle, "%s", value);
	pr_err("duty_cycle=%s", duty_cycle);
	old_fs = get_fs();
	set_fs(get_ds());
	write_byte = f_duty->f_op->write(f_duty, duty_cycle, str_len, &f_duty->f_pos);
	if(write_byte != str_len)
	{
		pr_err("set duty_cycle vale failed \n");
		goto out_err3;
	}
	set_fs(old_fs);
	f_enable = filp_open("/sys/class/pwm/pwmchip0/pwm1/enable", O_RDWR, 0);
	if (IS_ERR_OR_NULL(f_enable)) {
		pr_err("%s:filp_open backlight pwm1 f_enable file failed !!!", __func__);
		goto out_err3;
	}
	old_fs = get_fs();
	set_fs(get_ds());
	write_byte = f_enable->f_op->write(f_enable, "1", 1, &f_enable->f_pos);
	if(write_byte != 1)
	{
		pr_err("set enable vale failed \n");
		goto out_err4;
	}
	set_fs(old_fs);
out_err4:
	filp_close(f_enable, NULL);
out_err3:
	filp_close(f_duty, NULL);
out_err2:
	filp_close(f_period, NULL);
}
*/
static int kd101n65_set_light(struct exynos_panel_device *panel, u32 br_val)
{
	//u8 data = 0;
	//struct dsim_device *dsim = get_dsim_drvdata(0);

	DPU_INFO_PANEL("%s +\n", __func__);

	mutex_lock(&panel->ops_lock);

	DPU_INFO_PANEL("requested brightness value = %d\n", br_val);

	/* WRDISBV(8bit): 1st DBV[7:0] */
	//data = br_val & 0xFF;
	//dsim_write_data_seq(dsim, 12, 0x51, data);
	if(br_val >= 10){
		exynos_panel_pwm(panel, 1);
	//	backlight_pwm_config(br_val);
	}
	else
		exynos_panel_pwm(panel, 0);

	mutex_unlock(&panel->ops_lock);

	DPU_INFO_PANEL("%s -\n", __func__);
	return 0;
}
struct exynos_panel_ops panel_kd101n65_ops = {
	.id		= {0x0000ff},
	.suspend	= kd101n65_suspend,
	.displayon	= kd101n65_displayon,
	.mres		= kd101n65_mres,
	.doze		= kd101n65_doze,
	.doze_suspend	= kd101n65_doze_suspend,
	.dump		= kd101n65_dump,
	.read_state	= kd101n65_read_state,
    .set_light	= kd101n65_set_light,
};
