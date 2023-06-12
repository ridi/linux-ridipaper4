/*
 * Copyright (c) 2016 Park Bumgyu, Samsung Electronics Co., Ltd <bumgyu.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Exynos ACME(A Cpufreq that Meets Every chipset) driver implementation
 */

#include <linux/cpufreq.h>

/*
 * Log2 of the number of scale size. The frequencies are scaled up or
 * down as the multiple of this number.
 */
#define SCALE_SIZE			3
#define EXYNOS_UFC_TYPE_NAME_LEN	16


enum exynos_ufc_ctrl_type {
	PM_QOS_MIN_LIMIT = 0,
	PM_QOS_MIN_WO_BOOST_LIMIT,
	PM_QOS_MAX_LIMIT,
	TYPE_END,
};

enum exynos_ufc_execution_mode {
	AARCH64_MODE = 0,
	AARCH32_MODE,
	MODE_END,
};

struct exynos_ufc_info {
	struct list_head	node;

	int			ctrl_type;

	int			exe_mode;

	u32			**exynos_ufc_table;
	unsigned int		exynos_ufc_table_row;
	unsigned int		exynos_ufc_table_col;
};

struct exynos_ufc {
	struct list_head list;
	struct exynos_ufc_info info;
};
