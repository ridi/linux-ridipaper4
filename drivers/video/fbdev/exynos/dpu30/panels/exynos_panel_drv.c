/*
 * linux/drivers/video/fbdev/exynos/dpu30/panels/exynos_panel_drv.c
 *
 * Exynos Common Panel Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/backlight.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>

#include "exynos_panel_drv.h"

int dpu_panel_log_level = 7;

struct exynos_panel_device *panel_drvdata;
EXPORT_SYMBOL(panel_drvdata);

static struct exynos_panel_ops *panel_list[MAX_PANEL_SUPPORT];

static struct of_device_id exynos_panel_of_match[] = {
	{ .compatible = "samsung,exynos-panel" },
	{ }
};

MODULE_DEVICE_TABLE(of, exynos_panel_of_match);
#if 1
#if !defined(CONFIG_MACH_RP400)
extern void lt9211_init(void);
extern void lt9211_init_late(void);
#endif
extern int kd101n65_panel_init(void);
static void pwm_backlight_power_on(struct exynos_panel_device *panel)
{

	DPU_INFO_PANEL("%s: enter \n", __func__);
	if (panel->enabled)
		return;
#if !defined(CONFIG_MACH_RP400)
	pwm_enable(panel->pwm);
#endif
	panel->enabled = true;

}

static void pwm_backlight_power_off(struct exynos_panel_device *panel)
{
	if (!panel->enabled)
		return;

	DPU_INFO_PANEL("%s: enter \n", __func__);
#if !defined(CONFIG_MACH_RP400)
	pwm_config(panel->pwm, 0, panel->period);
	pwm_disable(panel->pwm);
#endif

	panel->enabled = false;
}
static int exynos_backlight_get_brightness(struct backlight_device *bl)
{
	return bl->props.brightness;
}

static int exynos_backlight_update_status(struct backlight_device *bl)
{
	u32 brightness = bl->props.brightness;
	int duty_cycle;
	struct exynos_panel_device *panel;

	DPU_INFO_PANEL("%s: brightness = %d\n", __func__, brightness);
	panel = dev_get_drvdata(&bl->dev);
#if 0
	if (bl->props.power != FB_BLANK_UNBLANK ||
			bl->props.fb_blank != FB_BLANK_UNBLANK ||
			bl->props.state & BL_CORE_FBBLANK)
		brightness = 0;
#endif
	if (brightness > 0) {
		duty_cycle = (brightness) * (panel->period/bl->props.max_brightness);
		DPU_INFO_PANEL("duty_cycle=%d,brightness=%d,max_duty=%d,max_brightness=%d", duty_cycle, brightness, panel->period, bl->props.max_brightness);
                duty_cycle = panel->period - duty_cycle;
#if !defined(CONFIG_MACH_RP400)
		pwm_config(panel->pwm, duty_cycle, panel->period);
#endif
		pwm_backlight_power_on(panel);
		if(!IS_ERR(panel->ops)) {
			panel->ops->set_light(panel, brightness);
		}
	} else {
		/* DO update brightness using dsim_wr_data */
		/* backlight_off ??? */
		DPU_INFO_PANEL("duty_cycle=%d,brightness=%d", 0, brightness);
#if !defined(CONFIG_MACH_RP400)
		//pwm_config(panel->pwm, 0, panel->period);
#endif
		pwm_backlight_power_off(panel);
		if(!IS_ERR(panel->ops)) {
			panel->ops->set_light(panel, 0);
		}
	}

	return 0;

}

static const struct backlight_ops exynos_backlight_ops = {
	.get_brightness	= exynos_backlight_get_brightness,
	.update_status	= exynos_backlight_update_status,
};
#endif
static int exynos_panel_parse_gpios(struct exynos_panel_device *panel)
{
#if !defined(CONFIG_MACH_RP400)
	struct device_node *n = panel->dev->of_node;
	struct exynos_panel_resources *res = &panel->res;

	DPU_INFO_PANEL("%s +\n", __func__);

	if (of_get_property(n, "gpios", NULL) != NULL)  {
		/* panel reset */
		res->lcd_reset = of_get_gpio(n, 0);
		if (res->lcd_reset < 0) {
			DPU_ERR_PANEL("failed to get lcd reset GPIO");
			return -ENODEV;
		}
		res->lcd_power[0] = of_get_gpio(n, 1);
		if (res->lcd_power[0] < 0) {
			res->lcd_power[0] = -1;
			DPU_INFO_PANEL("not support LCD power GPIO");
		}
		res->lcd_power[1] = 0;

		//lcd pwr gpio enable
		res->lcd_pwr_en = of_get_gpio(n, 2);
		if (res->lcd_pwr_en < 0) {
			res->lcd_pwr_en = -1;
			DPU_INFO_PANEL("not lcd_pwr_en GPIO");
		}
		//lcd bl gpio enable
		res->lcd_bl_en = of_get_gpio(n, 3);
		if (res->lcd_bl_en < 0) {
			res->lcd_bl_en = -1;
			DPU_INFO_PANEL("not lcd_bl_en GPIO");
		}
	}

	DPU_INFO_PANEL("%s -\n", __func__);
#endif
	return 0;
}

static int exynos_panel_parse_pwm(struct exynos_panel_device *panel)
{
	int ret = 0;

	DPU_INFO_PANEL("%s +\n", __func__);
#if !defined(CONFIG_MACH_RP400)
	panel->res.pinctrl = devm_pinctrl_get(panel->dev);
	if (IS_ERR(panel->res.pinctrl)) {
		decon_err("failed to get panel-%d pinctrl\n", panel->id);
		ret = PTR_ERR(panel->res.pinctrl);
		panel->res.pinctrl = NULL;
		goto err;
	}

	panel->res.bl_gpio_on = pinctrl_lookup_state(panel->res.pinctrl, "bl_gpio_on");
	if (IS_ERR(panel->res.bl_gpio_on)) {
		decon_err("failed to get bl_gpio_on pin state\n");
		ret = PTR_ERR(panel->res.bl_gpio_on);
		panel->res.bl_gpio_on = NULL;
		goto err;
	}

	panel->res.bl_pwm_on = pinctrl_lookup_state(panel->res.pinctrl, "bl_pwm_on");
	if (IS_ERR(panel->res.bl_pwm_on)) {
		decon_err("failed to get bl_pwm_on pin state\n");
		ret = PTR_ERR(panel->res.bl_pwm_on);
		panel->res.bl_pwm_on = NULL;
		goto err;
	}
	panel->res.bl_pwm_off = pinctrl_lookup_state(panel->res.pinctrl, "bl_pwm_off");
	if (IS_ERR(panel->res.bl_pwm_off)) {
		decon_err("failed to get bl_pwm_off pin state\n");
		ret = PTR_ERR(panel->res.bl_pwm_off);
		panel->res.bl_pwm_off = NULL;
		goto err;
	}

	panel->res.panel_off = pinctrl_lookup_state(panel->res.pinctrl, "panel_off");
	if (IS_ERR(panel->res.panel_off)) {
		decon_err("failed to get panel_off pin state\n");
		ret = PTR_ERR(panel->res.panel_off);
		panel->res.panel_off = NULL;
		goto err;
	}

	panel->res.panel_on = pinctrl_lookup_state(panel->res.pinctrl, "panel_on");
	if (IS_ERR(panel->res.panel_on)) {
		decon_err("failed to get panel_on pin state\n");
		ret = PTR_ERR(panel->res.panel_on);
		panel->res.panel_on = NULL;
		goto err;
	}
/*
        panel->res.irq_input = pinctrl_lookup_state(panel->res.pinctrl, "irq_input");
	if (IS_ERR(panel->res.irq_input)) {
		decon_err("failed to get irq_input pin state\n");
		ret = PTR_ERR(panel->res.irq_input);
		panel->res.irq_input = NULL;
		goto err;
	}
*/
err:
#endif
	DPU_INFO_PANEL("%s -\n", __func__);
	return ret;
}

static int exynos_panel_parse_regulators(struct exynos_panel_device *panel)
{
#if !defined(CONFIG_MACH_RP400)
	struct device *dev = panel->dev;
	struct exynos_panel_resources *res = &panel->res;
	char *str_regulator[MAX_REGULATORS];
	int i;

	for (i = 0; i < MAX_REGULATORS; ++i) {
		res->regulator[i] = NULL;
		str_regulator[i] = NULL;
	}

	if(!of_property_read_string(dev->of_node, "regulator_1p8v",
				(const char **)&str_regulator[0])) {
		res->regulator[0] = regulator_get_exclusive(dev, str_regulator[0]);
		if (IS_ERR(res->regulator[0])) {
			DPU_ERR_PANEL("panel regulator 1.8V get failed\n");
			res->regulator[0] = NULL;
		}
	}

	if(!of_property_read_string(dev->of_node, "regulator_3p3v",
				(const char **)&str_regulator[1])) {
		res->regulator[1] = regulator_get(dev, str_regulator[1]);
		if (IS_ERR(res->regulator[1])) {
			DPU_ERR_PANEL("panel regulator 3.3V get failed\n");
			res->regulator[1] = NULL;
		}
	}
#endif
    return 0;
}

static int exynos_panel_reset(struct exynos_panel_device *panel)
{
#if !defined(CONFIG_MACH_RP400)
	struct exynos_panel_resources *res = &panel->res;
	int ret;

	DPU_DEBUG_PANEL("%s +\n", __func__);

	ret = gpio_request_one(res->lcd_reset, GPIOF_OUT_INIT_HIGH, "lcd_reset");
	if (ret < 0) {
		DPU_ERR_PANEL("failed to get LCD reset GPIO\n");
		return -EINVAL;
	}

	usleep_range(5000, 6000);
	gpio_set_value(res->lcd_reset, 0);
	usleep_range(5000, 6000);
	gpio_set_value(res->lcd_reset, 1);

	gpio_free(res->lcd_reset);

	usleep_range(10000, 11000);

	DPU_DEBUG_PANEL("%s -\n", __func__);
#endif
	return 0;
 }

int exynos_panel_pwm(struct exynos_panel_device *panel, int is_on)
{
#if !defined(CONFIG_MACH_RP400)
	struct exynos_panel_resources *res = &panel->res;
	int ret;

	DPU_DEBUG_PANEL("%s +\n", __func__);

	//lcd_pwr_en
	ret = gpio_request_one(res->lcd_pwr_en, GPIOF_OUT_INIT_HIGH, "lcd_pwr_en");
	if (ret < 0) {
		DPU_ERR_PANEL("failed to get LCD lcd_pwr_en GPIO\n");
		return -EINVAL;
	}
	gpio_set_value(res->lcd_pwr_en, is_on);
	gpio_free(res->lcd_pwr_en);

	//lcd_bl_en
	ret = gpio_request_one(res->lcd_bl_en, GPIOF_OUT_INIT_HIGH, "lcd_bl_en");
	if (ret < 0) {
		DPU_ERR_PANEL("failed to get LCD lcd_bl_en GPIO\n");
		return -EINVAL;
	}
	gpio_set_value(res->lcd_bl_en, is_on);
	gpio_free(res->lcd_bl_en);

	//lcd_pwm
	if(panel->res.pinctrl != NULL){
		if(is_on) {
			if (pinctrl_select_state(panel->res.pinctrl,
						panel->res.bl_pwm_on)) {
				DPU_ERR_PANEL("failed to turn on panel bl pwm\n");
			} else {
				DPU_DEBUG_PANEL("pinctrl select bl pwm on state success");
			}
		} else {
			if (pinctrl_select_state(panel->res.pinctrl,
						panel->res.bl_pwm_off)) {
				DPU_ERR_PANEL("failed to turn on panel bl pwm\n");
			} else {
				DPU_DEBUG_PANEL("pinctrl select bl pwm off state success");
			}
		}
	} else {
		DPU_ERR_PANEL("pinctrl is null \n");
	}
	DPU_DEBUG_PANEL("%s -\n", __func__);
#endif

	return 0;
}

static int exynos_panel_parser_backlight(struct exynos_panel_device *panel)
{
	struct device_node *n = panel->dev->of_node;
	int ret = 0;
	u32 value = -1;

	DPU_DEBUG_PANEL("%s +\n", __func__);
	panel->pwmid = value;
	ret = of_property_read_u32(n, "pwmid", &value);
	if (ret < 0) {
		DPU_ERR_PANEL("of property read pwmid failed \n");
		return ret;
	}
	panel->pwmid = value;

	ret = of_property_read_u32(n, "period", &value);
	if (ret < 0) {
		DPU_ERR_PANEL("of property read period failed \n");
		return ret;
	}
	panel->period = value;

	ret = of_property_read_u32(n, "max_duty", &value);
	if (ret < 0) {
		DPU_ERR_PANEL("of property read period failed \n");
		return ret;
	}
	panel->max_duty = value;
	DPU_DEBUG_PANEL("%s -\n", __func__);

	return ret;
}
static int exynos_panel_set_power(struct exynos_panel_device *panel, bool on)
{
#if !defined(CONFIG_MACH_RP400)
	struct exynos_panel_resources *res = &panel->res;
	int ret;

	DPU_DEBUG_PANEL("%s(%d) +\n", __func__, on);

	if (on) {
		if (res->lcd_power[0] > 0) {
			ret = gpio_request_one(res->lcd_power[0],
					GPIOF_OUT_INIT_HIGH, "lcd_power0");
			if (ret < 0) {
				DPU_ERR_PANEL("failed LCD power on\n");
				return -EINVAL;
			}
			gpio_free(res->lcd_power[0]);
			usleep_range(10000, 11000);
		}

		if (res->lcd_power[1] > 0) {
			ret = gpio_request_one(res->lcd_power[1],
					GPIOF_OUT_INIT_HIGH, "lcd_power1");
			if (ret < 0) {
				DPU_ERR_PANEL("failed 2nd LCD power on\n");
				return -EINVAL;
			}
			gpio_free(res->lcd_power[1]);
			usleep_range(10000, 11000);
		}
		if (res->regulator[0] > 0) {
			ret = regulator_enable(res->regulator[0]);
			if (ret) {
				DPU_ERR_PANEL("panel regulator 1.8V enable failed\n");
				return ret;
			}
			usleep_range(5000, 6000);
		}

		if (res->regulator[1] > 0) {
			ret = regulator_enable(res->regulator[1]);
			if (ret) {
				DPU_ERR_PANEL("panel regulator 3.3V enable failed\n");
				return ret;
			}
		}
	} else {
		ret = gpio_request_one(res->lcd_reset, GPIOF_OUT_INIT_LOW,
				"lcd_reset");
		if (ret < 0) {
			DPU_ERR_PANEL("failed LCD reset off\n");
			return -EINVAL;
		}
		gpio_free(res->lcd_reset);

		if (res->lcd_power[0] > 0) {
			ret = gpio_request_one(res->lcd_power[0],
					GPIOF_OUT_INIT_LOW, "lcd_power0");
			if (ret < 0) {
				DPU_ERR_PANEL("failed LCD power off\n");
				return -EINVAL;
			}
			gpio_free(res->lcd_power[0]);
			usleep_range(5000, 6000);
		}

		if (res->lcd_power[1] > 0) {
			ret = gpio_request_one(res->lcd_power[1],
					GPIOF_OUT_INIT_LOW, "lcd_power1");
			if (ret < 0) {
				DPU_ERR_PANEL("failed 2nd LCD power off\n");
				return -EINVAL;
			}
			gpio_free(res->lcd_power[1]);
			usleep_range(5000, 6000);
		}
		if (res->regulator[0] > 0) {
			ret = regulator_disable(res->regulator[0]);
			if (ret) {
				DPU_ERR_PANEL("panel regulator 1.8V disable failed\n");
				return ret;
			}
		}

		if (res->regulator[1] > 0) {
			ret = regulator_disable(res->regulator[1]);
			if (ret) {
				DPU_ERR_PANEL("panel regulator 3.3V disable failed\n");
				return ret;
			}
		}
	}

	DPU_DEBUG_PANEL("%s(%d) -\n", __func__, on);
#endif

	return 0;
}

static int exynos_panel_calc_slice_width(u32 dsc_cnt, u32 slice_num, u32 xres)
{
	u32 slice_width;
	u32 width_eff;
	u32 slice_width_byte_unit, comp_slice_width_byte_unit;
	u32 comp_slice_width_pixel_unit;
	u32 compressed_slice_w = 0;
	u32 i, j;

	if (dsc_cnt == 2)
		width_eff = xres >> 1;
	else
		width_eff = xres;

	if (slice_num / dsc_cnt == 2)
		slice_width = width_eff >> 1;
	else
		slice_width = width_eff;

	/* 3bytes per pixel */
	slice_width_byte_unit = slice_width * 3;
	/* integer value, /3 for 1/3 compression */
	comp_slice_width_byte_unit = slice_width_byte_unit / 3;
	/* integer value, /3 for pixel unit */
	comp_slice_width_pixel_unit = comp_slice_width_byte_unit / 3;

	i = comp_slice_width_byte_unit % 3;
	j = comp_slice_width_pixel_unit % 2;

	if (i == 0 && j == 0) {
		compressed_slice_w = comp_slice_width_pixel_unit;
	} else if (i == 0 && j != 0) {
		compressed_slice_w = comp_slice_width_pixel_unit + 1;
	} else if (i != 0) {
		while (1) {
			comp_slice_width_pixel_unit++;
			j = comp_slice_width_pixel_unit % 2;
			if (j == 0)
				break;
		}
		compressed_slice_w = comp_slice_width_pixel_unit;
	}

	return compressed_slice_w;
}

static void exynos_panel_get_timing_info(struct exynos_panel_info *info,
		struct device_node *np)
{
	u32 res[14];

	of_property_read_u32_array(np, "timing,h-porch", res, 3);
	info->hbp = res[0];
	info->hfp = res[1];
	info->hsa = res[2];
	DPU_DEBUG_PANEL("hbp(%d), hfp(%d), hsa(%d)\n", res[0], res[1], res[2]);

	of_property_read_u32_array(np, "timing,v-porch", res, 3);
	info->vbp = res[0];
	info->vfp = res[1];
	info->vsa = res[2];
	DPU_DEBUG_PANEL("vbp(%d), vfp(%d), vsa(%d)\n", res[0], res[1], res[2]);

	of_property_read_u32(np, "timing,dsi-hs-clk", &info->hs_clk);
	of_property_read_u32(np, "timing,dsi-escape-clk", &info->esc_clk);
	DPU_DEBUG_PANEL("requested hs clk(%d), esc clk(%d)\n", info->hs_clk,
			info->esc_clk);

#if defined(CONFIG_EXYNOS_DSIM_DITHER)
	of_property_read_u32_array(np, "timing,pmsk", res, 14);
#else
	of_property_read_u32_array(np, "timing,pmsk", res, 4);
#endif
	info->dphy_pms.p = res[0];
	info->dphy_pms.m = res[1];
	info->dphy_pms.s = res[2];
	info->dphy_pms.k = res[3];
	DPU_DEBUG_PANEL("PMSK(%d %d %d %d)\n", res[0], res[1], res[2], res[3]);
#if defined(CONFIG_EXYNOS_DSIM_DITHER)
	info->dphy_pms.mfr = res[4];
	info->dphy_pms.mrr = res[5];
	info->dphy_pms.sel_pf = res[6];
	info->dphy_pms.icp = res[7];
	info->dphy_pms.afc_enb = res[8];
	info->dphy_pms.extafc = res[9];
	info->dphy_pms.feed_en = res[10];
	info->dphy_pms.fsel = res[11];
	info->dphy_pms.fout_mask = res[12];
	info->dphy_pms.rsel = res[13];
	DPU_DEBUG_PANEL(" mfr(%d), mrr(0x%x), sel_pf(%d), icp(%d)\n",
			res[4], res[5], res[6], res[7]);
	DPU_DEBUG_PANEL(" afc_enb(%d), extafc(%d), feed_en(%d), fsel(%d)\n",
			res[8], res[9], res[10], res[11]);
	DPU_DEBUG_PANEL(" fout_mask(%d), rsel(%d)\n", res[12], res[13]);
#endif

	of_property_read_u32(np, "data_lane", &info->data_lane);
	DPU_INFO_PANEL("data lane count(%d)\n", info->data_lane);

	if (info->mode == DECON_VIDEO_MODE) {
		of_property_read_u32(np, "vt_compensation", &info->vt_compensation);
		DPU_INFO_PANEL("vt_compensation(%d)\n", info->vt_compensation);
	}
}

static void exynos_panel_get_dsc_info(struct exynos_panel_info *info,
		struct device_node *np)
{
	of_property_read_u32(np, "dsc_en", &info->dsc.en);
	DPU_INFO_PANEL("DSC %s\n", info->dsc.en ? "enabled" : "disabled");

	if (info->dsc.en) {
		of_property_read_u32(np, "dsc_cnt", &info->dsc.cnt);
		of_property_read_u32(np, "dsc_slice_num", &info->dsc.slice_num);
		of_property_read_u32(np, "dsc_slice_h", &info->dsc.slice_h);

		info->dsc.enc_sw = exynos_panel_calc_slice_width(info->dsc.cnt,
					info->dsc.slice_num, info->xres);
		info->dsc.dec_sw = info->xres / info->dsc.slice_num;

		DPU_INFO_PANEL(" DSC cnt(%d), slice cnt(%d), slice height(%d)\n",
				info->dsc.cnt, info->dsc.slice_num,
				info->dsc.slice_h);
		DPU_INFO_PANEL(" DSC enc_sw(%d), dec_sw(%d)\n",
				info->dsc.enc_sw, info->dsc.dec_sw);
	}
}

static void exynos_panel_get_mres_info(struct exynos_panel_info *info,
		struct device_node *np)
{
	u32 num;
	u32 w[3] = {0, };
	u32 h[3] = {0, };
	u32 dsc_w[3] = {0, };
	u32 dsc_h[3] = {0, };
	u32 dsc_en[3] = {0, };
	int i;

	of_property_read_u32(np, "mres_en", &info->mres.en);
	DPU_INFO_PANEL("Multi Resolution LCD %s\n",
			info->mres.en ? "enabled" : "disabled");

	if (info->mres.en) {
		info->mres_mode = 0; /* 0=WQHD, 1=FHD, 2=HD */
		info->mres.number = 1; /* default = 1 */
		of_property_read_u32(np, "mres_number", &num);
		info->mres.number = num;

		of_property_read_u32_array(np, "mres_width", w, num);
		of_property_read_u32_array(np, "mres_height", h, num);
		of_property_read_u32_array(np, "mres_dsc_width", dsc_w, num);
		of_property_read_u32_array(np, "mres_dsc_height", dsc_h, num);
		of_property_read_u32_array(np, "mres_dsc_en", dsc_en, num);

		for (i = 0; i < num; ++i) {
			info->mres.res_info[i].width = w[i];
			info->mres.res_info[i].height = h[i];
			info->mres.res_info[i].dsc_en = dsc_en[i];
			info->mres.res_info[i].dsc_width = dsc_w[i];
			info->mres.res_info[i].dsc_height = dsc_h[i];
			info->dsc_slice.dsc_enc_sw[i] =
				exynos_panel_calc_slice_width(info->dsc.cnt,
						info->dsc.slice_num, w[i]);
			info->dsc_slice.dsc_dec_sw[i] = w[i] / info->dsc.slice_num;

			if (info->mode == DECON_MIPI_COMMAND_MODE)
				of_property_read_u32_array(np, "cmd_underrun_cnt",
						info->cmd_underrun_cnt,
						info->mres.number);

			DPU_INFO_PANEL(" [%dx%d]: DSC(%d) enc/dec sw(%d %d) lp(%d)\n",
					info->mres.res_info[i].width,
					info->mres.res_info[i].height,
					info->mres.res_info[i].dsc_en,
					info->dsc_slice.dsc_enc_sw[i],
					info->dsc_slice.dsc_dec_sw[i],
					info->cmd_underrun_cnt[i]);
		}
	} else {
		if (info->mode == DECON_MIPI_COMMAND_MODE) {
			of_property_read_u32(np, "cmd_underrun_cnt",
					&info->cmd_underrun_cnt[0]);
			DPU_INFO_PANEL("lp(%d)\n", info->cmd_underrun_cnt[0]);
		}
	}
}

static void exynos_panel_get_hdr_info(struct exynos_panel_info *info,
		struct device_node *np)
{
	u32 hdr_num = 0;
	u32 hdr_type[HDR_CAPA_NUM] = {0, };
	u32 max, avg, min;
	int i;

	/* HDR info */
	of_property_read_u32(np, "hdr_num", &hdr_num);
	info->hdr.num = hdr_num;
	DPU_INFO_PANEL("hdr_num(%d)\n", hdr_num);

	if (hdr_num != 0) {
		of_property_read_u32_array(np, "hdr_type", hdr_type, hdr_num);
		for (i = 0; i < hdr_num; i++) {
			info->hdr.type[i] = hdr_type[i];
			DPU_INFO_PANEL(" hdr_type[%d] = %d\n", i, hdr_type[i]);
		}

		of_property_read_u32(np, "hdr_max_luma", &max);
		of_property_read_u32(np, "hdr_max_avg_luma", &avg);
		of_property_read_u32(np, "hdr_min_luma", &min);
		info->hdr.max_luma = max;
		info->hdr.max_avg_luma = avg;
		info->hdr.min_luma = min;
		DPU_INFO_PANEL(" luma max/avg/min(%d %d %d)\n", max, avg, min);
	}
}

static void exynos_panel_parse_lcd_info(struct exynos_panel_device *panel,
		struct device_node *np)
{
	u32 res[2];
	struct exynos_panel_info *lcd_info = &panel->lcd_info;
	u32 max_br, dft_br;

	of_property_read_u32(np, "mode", &lcd_info->mode);
	of_property_read_u32_array(np, "resolution", res, 2);
	lcd_info->xres = res[0];
	lcd_info->yres = res[1];
	of_property_read_u32(np, "timing,refresh", &lcd_info->fps);
	DPU_INFO_PANEL("LCD(%s) resolution: %dx%d@%d, %s mode\n",
			np->name, lcd_info->xres, lcd_info->yres,
			lcd_info->fps, lcd_info->mode ? "command" : "video");

	of_property_read_u32_array(np, "size", res, 2);
	lcd_info->width = res[0];
	lcd_info->height = res[1];
	of_property_read_u32(np, "type_of_ddi", &lcd_info->ddi_type);
	DPU_DEBUG_PANEL("LCD size(%dx%d), DDI type(%d)\n", res[0], res[1],
			lcd_info->ddi_type);


	panel->bl->props.max_brightness = DEFAULT_MAX_BRIGHTNESS;
	panel->bl->props.brightness = DEFAULT_BRIGHTNESS;
	if (!of_property_read_u32(np, "max-brightness", &max_br))
		panel->bl->props.max_brightness = max_br;
	if (!of_property_read_u32(np, "dft-brightness", &dft_br))
		panel->bl->props.brightness = dft_br;

	DPU_INFO_PANEL("max brightness : %d\n", panel->bl->props.max_brightness);
	DPU_INFO_PANEL("default brightness : %d\n", panel->bl->props.brightness);

	exynos_panel_get_timing_info(lcd_info, np);
	exynos_panel_get_dsc_info(lcd_info, np);
	exynos_panel_get_mres_info(lcd_info, np);
	exynos_panel_get_hdr_info(lcd_info, np);
}

static void exynos_panel_list_up(void)
{
	panel_list[0] = &panel_s6e3ha8_ops;
	panel_list[1] = &panel_s6e3ha9_ops;
	panel_list[2] = &panel_s6e3fa0_ops;
	panel_list[3] = &panel_kd101n65_ops;
#if defined(CONFIG_MACH_RP400)
	panel_list[4] = &panel_dummy_ops;
#endif
}

static int exynos_panel_register_ops(struct exynos_panel_device *panel)
{
	int i, j;
	bool found = false;

	for (i = 0; i < MAX_PANEL_SUPPORT; ++i) {
		for (j = 0; j < MAX_PANEL_ID_NUM; j++) {
			if (panel_list[i]->id[j] == panel->lcd_info.id) {
				panel->ops = panel_list[i];
				panel->id_index = j;
				found = true;
				DPU_INFO_PANEL("panel ops is found in panel list\n");
				break;
			}

		}
		if (found == true)
			break;
	}

	if (!found) {
		DPU_ERR_PANEL("panel ops is not found in panel list\n");
		return -EINVAL;
	}

	return 0;
}

static int exynos_panel_register(struct exynos_panel_device *panel, u32 id)
{
	struct device_node *n = panel->dev->of_node;
	struct device_node *np;
	struct property *prop;
	int panel_id, i;
	const __be32 *cur;

	for (i = 0; i < MAX_PANEL_SUPPORT; ++i) {
		np = of_parse_phandle(n, "lcd_info", i);
		if (!np) {
			DPU_INFO_PANEL("panel is not matched\n");
			return -EINVAL;
		}

		of_property_for_each_u32(np, "id", prop, cur, panel_id) {
			DPU_INFO_PANEL("finding... %s(0x%x)\n", np->name, panel_id);

			/*
			 * TODO: The mode(video or command) should be compared
			 * for distinguishing panel DT information which has same DDI ID
			 */
			if (id == panel_id) {
				panel->found = true;
				DPU_INFO_PANEL("matched panel is found(%s:0x%x)\n",
						np->name, id);

				/* parsing lcd info */
				panel->lcd_info.id = id;
				exynos_panel_parse_lcd_info(panel, np);
				if (exynos_panel_register_ops(panel))
					BUG();

				break;
			}
		}

		if(panel->found == true)
			break;
	}

	return 0;
}

static void exynos_panel_find_id(struct exynos_panel_device *panel)
{
	struct device_node *n = panel->dev->of_node;
	int id;

	panel->found = false;

	if(of_property_read_u32(n, "ddi_id", &id)) {
		DPU_INFO_PANEL("connected panel id is not defined in DT\n");
		return;
	}

	//id = 0xff;

	DPU_INFO_PANEL("%s: panel id(0x%x) in DT\n", __func__, id);

	exynos_panel_register(panel, id);

	DPU_INFO_PANEL("%s -\n", __func__);
}

static int exynos_panel_parse_dt(struct exynos_panel_device *panel)
{
	int ret = 0;

	if (IS_ERR_OR_NULL(panel->dev->of_node)) {
		DPU_ERR_PANEL("no device tree information of exynos panel\n");
		return -EINVAL;
	}

	panel->id = of_alias_get_id(panel->dev->of_node, "panel");
	DPU_INFO_PANEL("panel-%d parsing DT...\n", panel->id);

	ret = exynos_panel_parse_gpios(panel);
	if (ret)
		goto err;

	ret = exynos_panel_parse_regulators(panel);
	if (ret)
		goto err;

	ret = exynos_panel_parse_pwm(panel);

	ret = exynos_panel_parser_backlight(panel);

err:
	return ret;
}

static long exynos_panel_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct exynos_panel_device *panel;
	int ret = 0;

	panel = container_of(sd, struct exynos_panel_device, sd);


	switch (cmd) {
	case EXYNOS_PANEL_IOC_REGISTER:
		ret = exynos_panel_register(panel, *(u32 *)arg);
		break;
	case EXYNOS_PANEL_IOC_POWERON:
		ret = exynos_panel_set_power(panel, true);
		break;
	case EXYNOS_PANEL_IOC_POWEROFF:
		ret = exynos_panel_set_power(panel, false);
		break;
	case EXYNOS_PANEL_IOC_RESET:
		ret = exynos_panel_reset(panel);
		break;
	case EXYNOS_PANEL_IOC_DISPLAYON:
		call_panel_ops(panel, displayon, panel);
		break;
	case EXYNOS_PANEL_IOC_SUSPEND:
		call_panel_ops(panel, suspend, panel);
		break;
	case EXYNOS_PANEL_IOC_MRES:
		call_panel_ops(panel, mres, panel, *(int *)arg);
		break;
	case EXYNOS_PANEL_IOC_DOZE:
		call_panel_ops(panel, doze, panel);
		break;
	case EXYNOS_PANEL_IOC_DOZE_SUSPEND:
		call_panel_ops(panel, doze_suspend, panel);
		break;
	case EXYNOS_PANEL_IOC_DUMP:
		call_panel_ops(panel, dump, panel);
		break;
	case EXYNOS_PANEL_IOC_READ_STATE:
		ret = call_panel_ops(panel, read_state, panel);
		break;
	case EXYNOS_PANEL_IOC_SET_LIGHT:
		call_panel_ops(panel, set_light, panel, *(int *)arg);
		break;
	default:
		DPU_ERR_PANEL("not supported ioctl by panel driver\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_subdev_core_ops exynos_panel_sd_core_ops = {
	.ioctl = exynos_panel_ioctl,
};

static const struct v4l2_subdev_ops exynos_panel_subdev_ops = {
	.core = &exynos_panel_sd_core_ops,
};

static void exynos_panel_init_subdev(struct exynos_panel_device *panel)
{
	struct v4l2_subdev *sd = &panel->sd;

	v4l2_subdev_init(sd, &exynos_panel_subdev_ops);
	sd->owner = THIS_MODULE;
	sd->grp_id = panel->id;
	snprintf(sd->name, sizeof(sd->name), "%s.%d", "panel-sd", panel->id);
	v4l2_set_subdevdata(sd, panel);
	DPU_INFO_PANEL("%s: panel sd name(%s)\n", __func__, sd->name);
}

static ssize_t panel_cabc_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	int ret = 0;
	struct exynos_panel_device *panel = dev_get_drvdata(dev);

	ret = call_panel_ops(panel, set_cabc_mode,
				panel, POWER_MODE_READ);

	count = snprintf(buf, PAGE_SIZE, "power_mode = %d, ret = %d\n",
			panel->power_mode, ret);

	return count;
}

static ssize_t panel_cabc_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	unsigned int value = 0;
	struct exynos_panel_device *panel = dev_get_drvdata(dev);

	ret = kstrtouint(buf, 0, &value);
	if (ret < 0)
		return ret;

	panel->power_mode = value;

	DPU_INFO_PANEL("%s: %d\n", __func__, value);

	call_panel_ops(panel, set_cabc_mode,
			panel, panel->power_mode);

	return count;
}

static DEVICE_ATTR(cabc_mode, 0660, panel_cabc_mode_show,
			panel_cabc_mode_store);

static int exynos_panel_probe(struct platform_device *pdev)
{
	struct exynos_panel_device *panel;
	int ret = 0;

	DPU_DEBUG_PANEL("%s +\n", __func__);

	panel = devm_kzalloc(&pdev->dev, sizeof(struct exynos_panel_device),
			GFP_KERNEL);
	if (IS_ERR_OR_NULL(panel)) {
		DPU_ERR_PANEL("failed to allocate panel structure\n");
		ret = -ENOMEM;
		goto err;
	}

	panel->dev = &pdev->dev;
	panel_drvdata = panel;

	mutex_init(&panel->ops_lock);

	if (IS_ENABLED(CONFIG_EXYNOS_DECON_LCD_CABC))
		panel->cabc_enabled = true;
	else
		panel->cabc_enabled = false;

	if (panel->cabc_enabled) {
		ret = device_create_file(panel->dev, &dev_attr_cabc_mode);
		if (ret) {
			DPU_ERR_PANEL("failed to create cabc sysfs\n");
			goto err;
		}

		panel->power_mode = POWER_SAVE_OFF;
	}

	ret = exynos_panel_parse_dt(panel);
	if (ret) {
		goto err_dev_file;
	}

#if !defined(CONFIG_MACH_RP400)
	if (panel->pwmid >= 0) {
		panel->pwm = pwm_request(panel->pwmid, "pwm-backlight");
		if (IS_ERR(panel->pwm)) {
			dev_err(&pdev->dev, "unable to request PWM\n");
			goto err;
		}
	}
#endif

	panel->bl = devm_backlight_device_register(panel->dev,
			dev_name(panel->dev), NULL, panel,
			&exynos_backlight_ops, NULL);
	if (IS_ERR(panel->bl)) {
		DPU_ERR_PANEL("failed to register backlight device\n");
		ret = PTR_ERR(panel->bl);
		goto err_dev_file;
	}

#if !defined(CONFIG_MACH_RP400)
        if (pinctrl_select_state(panel->res.pinctrl, panel->res.bl_gpio_on)) {
				DPU_ERR_PANEL("failed to turn on panel bl gpio\n");
        }
#endif

	exynos_panel_list_up();
	exynos_panel_find_id(panel);

	exynos_panel_init_subdev(panel);
	platform_set_drvdata(pdev, panel);

	DPU_DEBUG_PANEL("%s -\n", __func__);
	return ret;

err_dev_file:
	device_remove_file(panel->dev, &dev_attr_cabc_mode);

err:
	DPU_DEBUG_PANEL("%s -\n", __func__);
	return ret;
}

static void exynos_panel_shutdown(struct platform_device *pdev)
{
	struct exynos_panel_device *panel = platform_get_drvdata(pdev);

	backlight_device_unregister(panel->bl);
}

static struct platform_driver exynos_panel_driver = {
	.probe		= exynos_panel_probe,
	.shutdown	= exynos_panel_shutdown,
	.driver		= {
		.name		= "exynos-panel",
		.of_match_table	= of_match_ptr(exynos_panel_of_match),
		.suppress_bind_attrs = true,
	},
};

static int __init exynos_panel_init(void)
{
	int ret = platform_driver_register(&exynos_panel_driver);
	if (ret)
		pr_err("exynos_panel_driver register failed\n");

	return ret;
}
device_initcall(exynos_panel_init);

static void __exit exynos_panel_exit(void)
{
	platform_driver_unregister(&exynos_panel_driver);
}
module_exit(exynos_panel_exit);

MODULE_DESCRIPTION("Exynos Common Panel Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jiun Yu <jiun.yu@samsung.com>");
