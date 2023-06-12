/*
 * Samsung Exynos SoC series Pablo driver
 *
 * Copyright (c) 2019 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IS_HW_PDP_H
#define IS_HW_PDP_H

#include "is-device-sensor.h"
#include "is-hw-api-common.h"

#define COREX_OFFSET 0x8000

enum pdp_event_type {
	PE_START,
	PE_END,
	PE_PAF_STAT0,
	PE_PAF_STAT1,
	PE_PAF_STAT2,
	PE_ERR_INT1,
	PE_ERR_INT2,
	PE_PAF_OVERFLOW,
};

enum pdp_input_path_type {
	OTF = 0,
	STRGEN = 1,
	DMA = 2,
};

/* status */
unsigned int pdp_hw_g_idle_state(void __iomem *base);

/* config */
void pdp_hw_s_global_enable(void __iomem *base, bool enable);
int pdp_hw_s_one_shot_enable(void __iomem *base);
void pdp_hw_s_corex_enable(void __iomem *base, bool enable);
void pdp_hw_s_core(void __iomem *base, bool pd_enable,
	u32 img_width, u32 img_height, u32 img_hwformat, u32 img_pixelsize,
	u32 pd_width, u32 pd_height, u32 pd_hwformat,
	u32 sensor_type, u32 path, int sensor_mode, u32 fps, u32 en_sdc, u32 en_votf,
	u32 num_buffers, ulong freq);
void pdp_hw_s_init(void __iomem *base);
void pdp_hw_s_reset(void __iomem *base);
void pdp_hw_s_global(void __iomem *base, u32 ch, u32 path, void *data);
void pdp_hw_s_context(void __iomem *base, u32 curr_ch, u32 curr_path);
void pdp_hw_s_path(void __iomem *base, u32 path);
void pdp_hw_s_wdma_init(void __iomem *base);
void pdp_hw_s_wdma_enable(void __iomem *base, dma_addr_t address);
void pdp_hw_s_wdma_disable(void __iomem *base);
void pdp_hw_s_rdma_init(void __iomem *base, u32 width, u32 height, u32 hwformat, u32 pixelsize,
	u32 rmo, u32 en_sdc, u32 en_votf, u32 en_dma, ulong freq);
void pdp_hw_s_af_rdma_init(void __iomem *base, u32 width, u32 height, u32 hwformat, u32 rmo,
	u32 en_votf, u32 en_dma);
void pdp_hw_s_af_rdma_tail_count_reset(void __iomem *base);
void pdp_hw_s_rdma_addr(void __iomem *base, dma_addr_t *address, u32 num_buffers);
void pdp_hw_s_af_rdma_addr(void __iomem *base, dma_addr_t *address, u32 num_buffers);
void pdp_hw_s_post_frame_gap(void __iomem *base, u32 interval);
void pdp_hw_s_fro(void __iomem *base, u32 num_buffers);
int pdp_hw_wait_idle(void __iomem *base);

/* stat */
int pdp_hw_g_stat0(void __iomem *base, void *buf, size_t len);
void pdp_hw_g_pdstat_size(u32 *width, u32 *height, u32 *bytes_per_pixel);
void pdp_hw_s_pdstat_path(void __iomem *base, bool enable);
void pdp_hw_s_line_row(void __iomem *base, bool pd_enable, int sensor_mode);

/* sensor mode */
void pdp_hw_s_sensor_type(void __iomem *base, u32 sensor_type);
bool pdp_hw_to_sensor_type(u32 pd_mode, u32 *sensor_type);

/* interrupt */
unsigned int pdp_hw_g_int1_state(void __iomem *base, bool clear);
unsigned int pdp_hw_g_int1_mask(void __iomem *base);
unsigned int pdp_hw_g_int2_state(void __iomem *base, bool clear);
unsigned int pdp_hw_g_int2_mask(void __iomem *base);
unsigned int pdp_hw_is_occured(unsigned int state, enum pdp_event_type type);
void pdp_hw_g_int1_str(const char **int_str);
void pdp_hw_g_int2_str(const char **int_str);

/* debug */
void pdp_hw_s_config_default(void __iomem *base);
int pdp_hw_dump(void __iomem *base);
#endif
