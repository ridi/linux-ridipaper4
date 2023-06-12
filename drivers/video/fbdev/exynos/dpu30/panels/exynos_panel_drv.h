/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Header file for Samsung EXYNOS Panel Driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __EXYNOS_PANEL_DRV_H__
#define __EXYNOS_PANEL_DRV_H__

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/backlight.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <media/v4l2-subdev.h>
#include "exynos_panel.h"
#include "../dsim.h"
#include "../decon.h"

extern int dpu_panel_log_level;

#define DPU_DEBUG_PANEL(fmt, args...)					\
	do {								\
		if (dpu_panel_log_level >= 7)				\
			dpu_debug_printk("PANEL", fmt,  ##args);	\
	} while (0)

#define DPU_INFO_PANEL(fmt, args...)					\
	do {								\
		if (dpu_panel_log_level >= 6)				\
			dpu_debug_printk("PANEL", fmt,  ##args);	\
	} while (0)

#define DPU_ERR_PANEL(fmt, args...)					\
	do {								\
		if (dpu_panel_log_level >= 3)				\
			dpu_debug_printk("PANEL", fmt, ##args);		\
	} while (0)

#define MAX_REGULATORS		3
#define MAX_PANEL_SUPPORT	10
#define MAX_PANEL_ID_NUM	3

#define DEFAULT_MAX_BRIGHTNESS	255
#define DEFAULT_BRIGHTNESS	20

extern struct exynos_panel_device *panel_drvdata;
extern struct exynos_panel_ops panel_s6e3ha9_ops;
extern struct exynos_panel_ops panel_s6e3ha8_ops;
extern struct exynos_panel_ops panel_s6e3fa0_ops;
extern struct exynos_panel_ops panel_kd101n65_ops;
#if defined(CONFIG_MACH_RP400)
extern struct exynos_panel_ops panel_dummy_ops;
#endif

struct exynos_panel_resources {
	int lcd_reset;
	int lcd_pwr_en;
	int lcd_bl_en;
	int lcd_power[2];
	struct regulator *regulator[MAX_REGULATORS];
	struct pinctrl *pinctrl;
	struct pinctrl_state *bl_pwm_on;
	struct pinctrl_state *bl_gpio_on;
	struct pinctrl_state *bl_pwm_off;
	struct pinctrl_state *panel_off;
	struct pinctrl_state *panel_on;
	//struct pinctrl_state *irq_input;//for lt9611/lt9211 interrupt pin state when system is suspend
	//struct pinctrl_state *irq_state;//for lt9611/lt9211 interrupt pin state when system is resume
};

struct exynos_panel_ops {
	u32 id[MAX_PANEL_ID_NUM];
	int (*suspend)(struct exynos_panel_device *panel);
	int (*displayon)(struct exynos_panel_device *panel);
	int (*mres)(struct exynos_panel_device *panel, int mres_idx);
	int (*doze)(struct exynos_panel_device *panel);
	int (*doze_suspend)(struct exynos_panel_device *panel);
	int (*dump)(struct exynos_panel_device *panel);
	int (*read_state)(struct exynos_panel_device *panel);
	int (*set_cabc_mode)(struct exynos_panel_device *panel, int mode);
	int (*set_light)(struct exynos_panel_device *panel, u32 br_val);
};

/*
 * cabc_mode[1:0] must be re-mapped according to DDI command
 * 3FA0 is OLED, so CABC command is not supported.
 * Following values represent for ACL2 control of 3FA0.
 * - [2'b00] ACL off
 * - [2'b01] ACL low
 * - [2'b10] ACL mid
 * - [2'b11] ACL high
 */
enum cabc_mode {
	CABC_OFF = 0,
	CABC_USER_IMAGE,
	CABC_STILL_PICTURE,
	CABC_MOVING_IMAGE,
};

enum power_mode {
	POWER_SAVE_OFF = 0,
	POWER_SAVE_LOW = 1,
	POWER_SAVE_MEDIUM = 2,
	POWER_SAVE_HIGH = 3,
	POWER_SAVE_MAX = 4,
	POWER_MODE_READ = 0x80,
};

struct exynos_panel_device {
	u32 id;		/* panel device id */
	u32 id_index;
	bool found;	/* found connected panel or not */
	struct device *dev;
	struct v4l2_subdev sd;
	struct mutex ops_lock;
	struct exynos_panel_resources res;
	struct backlight_device *bl;
	struct exynos_panel_info lcd_info;
	struct exynos_panel_ops *ops;
	bool cabc_enabled;
	enum power_mode power_mode;
	struct pwm_device *pwm;
	int pwmid;//index of pwm out
	u32 period;
	u32 max_duty;
	int enabled;
};

static inline struct exynos_panel_device *get_panel_drvdata(void)
{
	return panel_drvdata;
}

#define call_panel_ops(q, op, args...)				\
	(((q)->ops->op) ? ((q)->ops->op(args)) : 0)

#define EXYNOS_PANEL_IOC_REGISTER	_IOW('P', 0, u32)
#define EXYNOS_PANEL_IOC_POWERON	_IOW('P', 1, u32)
#define EXYNOS_PANEL_IOC_POWEROFF	_IOW('P', 2, u32)
#define EXYNOS_PANEL_IOC_RESET		_IOW('P', 3, u32)
#define EXYNOS_PANEL_IOC_DISPLAYON	_IOW('P', 4, u32)
#define EXYNOS_PANEL_IOC_SUSPEND	_IOW('P', 5, u32)
#define EXYNOS_PANEL_IOC_MRES		_IOW('P', 6, u32)
#define EXYNOS_PANEL_IOC_DOZE		_IOW('P', 7, u32)
#define EXYNOS_PANEL_IOC_DOZE_SUSPEND	_IOW('P', 8, u32)
#define EXYNOS_PANEL_IOC_DUMP		_IOW('P', 9, u32)
#define EXYNOS_PANEL_IOC_READ_STATE	_IOR('P', 10, u32)
#define EXYNOS_PANEL_IOC_SET_LIGHT	_IOW('P', 11, u32)

#endif /* __EXYNOS_PANEL_DRV_H__ */
