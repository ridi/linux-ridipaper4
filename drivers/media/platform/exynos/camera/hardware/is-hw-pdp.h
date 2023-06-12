/*
 * Samsung Exynos SoC series Pablo driver
 *
 * Copyright (c) 2019 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef IS_PDP_H
#define IS_PDP_H

#include "is-subdev-ctrl.h"
#include "is-work.h"

extern int debug_pdp;

#define dbg_pdp(level, fmt, pdp, args...) \
	dbg_common((debug_pdp) >= (level), "[PDP%d]", fmt, pdp->id, ##args)

enum is_pdp_state {
	IS_PDP_SET_PARAM,
	IS_PDP_VOTF_ONESHOT_FIRST_FRAME,
};

struct pdp_lic_lut {
	u32				mode;
	u32				param0;
	u32				param1;
	u32				param2;
};

enum pdp_irq_src {
	PDP_INT1,
	PDP_INT2,
	PDP_INT_MAX,
};

struct is_pdp {
	struct device			*dev;
	const char			*aclk_name;
	ulong				freq; /* Hz */
	u32				id;
	u32				max_num;
	u32				prev_instance;
	void __iomem			*base;
	void __iomem			*cmn_base;
	resource_size_t			regs_start;
	resource_size_t			regs_end;
	int				irq[PDP_INT_MAX];
	struct mutex			control_lock;

	void __iomem			*mux_base; /* select CSIS ch(e.g. CSIS0~CSIS5) */
	u32				*mux_val;
	u32				mux_elems;

	void __iomem			*vc_mux_base; /* select VC ch in single CSIS(e.g. VC0~VC3) */
	u32	 			*vc_mux_val;
	u32				vc_mux_elems;

	void __iomem			*en_base; /* enable CSIS ch gate(e.g. CSIS0~CSIS5)*/
	u32				*en_val;
	u32				en_elems;

	unsigned long			state;

	struct is_pdp_ops		*pdp_ops;
	struct v4l2_subdev		*subdev; /* connected module subdevice */

	spinlock_t			slock_paf_action;
	struct list_head		list_of_paf_action;

	atomic_t			frameptr_stat0;

	struct workqueue_struct		*wq_stat;
	struct work_struct		work_stat[WORK_PDP_MAX];
	struct is_work_list		work_list[WORK_PDP_MAX];
	struct is_framemgr              *stat_framemgr;

	const char			*int1_str[BITS_PER_LONG];
	const char			*int2_str[BITS_PER_LONG];

	struct pdp_lic_lut		*lic_lut;
	bool				stat_enable;

	int				vc_ext_sensor_mode;
	u32				err_cnt_oneshot;
};


#endif
