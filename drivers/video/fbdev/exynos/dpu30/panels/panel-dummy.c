/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung dummy Panel driver.
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

extern void dsim_powerup(void);

static int dummy_suspend(struct exynos_panel_device *panel)
{
	return 0;
}

static int dummy_displayon(struct exynos_panel_device *panel)
{
    DPU_INFO_PANEL("%s +\n", __func__);
    DPU_INFO_PANEL("%s -\n", __func__);
	return 0;
}

static int dummy_mres(struct exynos_panel_device *panel, int mres_idx)
{
	return 0;
}

static int dummy_doze(struct exynos_panel_device *panel)
{
	return 0;
}

static int dummy_doze_suspend(struct exynos_panel_device *panel)
{
	return 0;
}

static int dummy_dump(struct exynos_panel_device *panel)
{
	return 0;
}

static int dummy_read_state(struct exynos_panel_device *panel)
{
	return 0;
}
static int dummy_set_light(struct exynos_panel_device *panel, u32 br_val)
{
	//u8 data = 0;
	//struct dsim_device *dsim = get_dsim_drvdata(0);

	DPU_INFO_PANEL("%s +\n", __func__);

	mutex_lock(&panel->ops_lock);

	DPU_INFO_PANEL("requested brightness value = %d\n", br_val);
	if(br_val == 0) {
		dsim_powerup();
	}

	mutex_unlock(&panel->ops_lock);

	DPU_INFO_PANEL("%s -\n", __func__);
	return 0;
}
struct exynos_panel_ops panel_dummy_ops = {
	.id		= {0x0000fa},
	.suspend	= dummy_suspend,
	.displayon	= dummy_displayon,
	.mres		= dummy_mres,
	.doze		= dummy_doze,
	.doze_suspend	= dummy_doze_suspend,
	.dump		= dummy_dump,
	.read_state	= dummy_read_state,
    .set_light	= dummy_set_light,
};
