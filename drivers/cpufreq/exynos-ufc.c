/*
 * Copyright (c) 2016 Park Bumgyu, Samsung Electronics Co., Ltd <bumgyu.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Exynos ACME(A Cpufreq that Meets Every chipset) driver implementation
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/pm_opp.h>
#include <linux/ems_service.h>

#include <soc/samsung/exynos-cpuhp.h>

#include "exynos-acme.h"
#include "exynos-ufc.h"

static int last_max_limit;
static int last_min_limit;
static int last_min_wo_boost_limit;
static int sse_mode;
//static struct kpp kpp_ta;
//static struct kpp kpp_fg;

/*********************************************************************
 *                          HELPER FUNCTION                          *
 *********************************************************************/

unsigned int get_cpufreq_max_limit(void)
{
	struct list_head *domains = get_domain_list();
	struct exynos_cpufreq_domain *domain;
	unsigned int pm_qos_max;
	int scale = -1;

	if (!domains) {
		pr_err("failed to get domains!\n");
		return -ENXIO;
	}

	list_for_each_entry_reverse(domain, domains, list) {
		scale++;

		/* get value of minimum PM QoS */
		pm_qos_max = pm_qos_request(domain->pm_qos_max_class);
		if (pm_qos_max > 0) {
			pm_qos_max = min(pm_qos_max, domain->max_freq);
			pm_qos_max = max(pm_qos_max, domain->min_freq);

			/*
			 * To manage frequencies of all domains at once,
			 * scale down frequency as multiple of 4.
			 * ex) domain2 = freq
			 *     domain1 = freq /4
			 *     domain0 = freq /16
			 */
			pm_qos_max = pm_qos_max >> (scale * SCALE_SIZE);
			return pm_qos_max;
		}
	}

	/*
	 * If there is no QoS at all domains, it returns minimum
	 * frequency of last domain
	 */
	return first_domain()->min_freq >> (scale * SCALE_SIZE);
}

static struct exynos_cpufreq_domain* first_domain(void)
{
	return list_first_entry(get_domain_list(),
			struct exynos_cpufreq_domain, list);
}

//static inline void ufc_kpp_request(int val)
//{
//	kpp_request(STUNE_TOPAPP, &kpp_ta, val);
//	kpp_request(STUNE_FOREGROUND, &kpp_fg, val);
//}

static void
enable_domain_cpus(struct exynos_cpufreq_domain *domain, struct list_head *list)
{
	struct cpumask mask;

	if (domain == first_domain())
		return;

	cpumask_or(&mask, cpu_online_mask, &domain->cpus);
	exynos_cpuhp_request("ACME", mask, 0);
}

static void
disable_domain_cpus(struct exynos_cpufreq_domain *domain, struct list_head *list)
{
	struct cpumask mask;

	if (domain == first_domain())
		return;

	cpumask_andnot(&mask, cpu_online_mask, &domain->cpus);
	exynos_cpuhp_request("ACME", mask, 0);
}

static void ufc_clear_min(struct list_head *domains)
{
	struct exynos_cpufreq_domain *domain;

	list_for_each_entry_reverse(domain, domains, list)
		pm_qos_update_request(&domain->user_min_qos_req, 0);

	//ufc_kpp_request(0);
}

static void ufc_clear_max(struct list_head *domains)
{
	struct exynos_cpufreq_domain *domain;

	list_for_each_entry_reverse(domain, domains, list) {
		/* If min qos lock with boost, clear minlock and boost off */
		enable_domain_cpus(domain, domains);
		pm_qos_update_request(&domain->user_max_qos_req, domain->max_freq);
	}
}

static void ufc_clear_min_wo_boost(struct list_head *domains)
{
	struct exynos_cpufreq_domain *domain;

	list_for_each_entry_reverse(domain, domains, list)
		pm_qos_update_request(&domain->user_min_qos_wo_boost_req, 0);
}

/* If UFC sse table is valid and sse mode is on, return true */
static inline bool ufc_check_sse_mode(struct exynos_ufc *ufc_sse)
{
	if (sse_mode && ufc_sse)
		return true;

	return false;
}

static inline unsigned int ufc_recover_frequency(int input, int scale)
{
	return input << (scale * SCALE_SIZE);
}

static inline unsigned int ufc_convert_frequency(int input, int scale)
{
	return input >> (scale * SCALE_SIZE);
}

static int ufc_get_proper_table_index(unsigned int freq,
		struct exynos_ufc *ufc)
{
	int index;

	for (index = 0; ufc->info.exynos_ufc_table[0][index] > freq; index++)
		;

	return index;
}

static void ufc_set_min_freq_target_domains(struct list_head *domains,
		struct exynos_cpufreq_domain *target_domain, struct exynos_ufc *ufc,
		unsigned int target_index)
{
	unsigned int col_idx = 0;

	//ufc_kpp_request(target_domain->user_boost);

	/* Set UFC Frequencies to target domains */
	list_for_each_entry_from_reverse(target_domain, domains, list) {
		unsigned int target_freq;

		target_freq = ufc->info.exynos_ufc_table[col_idx++][target_index];
		if (target_freq <= 0)
			target_freq = target_domain->user_default_qos;

		target_freq = min(target_freq, target_domain->max_freq);
		pm_qos_update_request(&target_domain->user_min_qos_req, target_freq);
	}
}

static void ufc_set_min_wo_boost_freq_target_domains(struct list_head *domains,
		struct exynos_cpufreq_domain *domain, struct exynos_ufc *ufc,
		unsigned int target_index)
{
	unsigned int col_idx = 0;

	/* Set UFC Frequencies to target domains */
	list_for_each_entry_from_reverse(domain, domains, list) {
		unsigned int target_freq;

		target_freq = ufc->info.exynos_ufc_table[col_idx++][target_index];
		if (target_freq <= 0)
			target_freq = domain->user_default_qos;

		target_freq = min(target_freq, domain->max_freq);
		pm_qos_update_request(&domain->user_min_qos_wo_boost_req, target_freq);
	}
}

static void ufc_set_max_freq_target_domains(struct list_head *domains,
		struct exynos_cpufreq_domain *domain, struct exynos_ufc *ufc,
		unsigned int target_index)
{
	unsigned int col_idx = 0;

	/* Set UFC Frequencies to target domains */
	list_for_each_entry_from_reverse(domain, domains, list) {
		unsigned int target_freq;

		enable_domain_cpus(domain, domains);

		target_freq = ufc->info.exynos_ufc_table[col_idx++][target_index];
		if (target_freq <= 0)
			target_freq = domain->user_default_qos;

		target_freq = min(target_freq, domain->max_freq);
		pm_qos_update_request(&domain->user_max_qos_req, target_freq);
	}
}

static void ufc_get_proper_table(struct exynos_cpufreq_domain *domain,
		struct exynos_ufc **r_ufc, int pm_qos_class)
{
	struct exynos_ufc *ufc = NULL, *ufc_sse = NULL;

	ufc = list_entry(&domain->ufc_list, struct exynos_ufc, list);
	list_for_each_entry(ufc, &domain->ufc_list, list) {
		if (ufc->info.ctrl_type == pm_qos_class) {
			if (ufc->info.exe_mode == AARCH64_MODE)
				(*r_ufc) = ufc;
			else
				ufc_sse = ufc;
		}
	}

	if (ufc_check_sse_mode(ufc_sse))
		(*r_ufc) = ufc_sse;
}

static unsigned int ufc_get_table_col(struct device_node *child)
{
	unsigned int col;

	of_property_read_u32(child, "table-col", &col);

	return col;
}

static unsigned int ufc_get_table_row(struct device_node *child)
{
	unsigned int size;
	unsigned int col;

	of_property_read_u32(child, "table-col", &col);
	size = of_property_count_u32_elems(child, "table");
	size = (size / col);

	return size;
}

static ssize_t ufc_update_min_limit(int input_freq, int count)
{
	struct list_head *domains = get_domain_list();
	struct exynos_cpufreq_domain *domain;
	struct exynos_ufc *r_ufc = NULL;
	int input = input_freq, scale = 0;
	unsigned int target_ufc_table_index;

	if (!domains) {
		pr_err("failed to get domains!\n");
		return -ENXIO;
	}

	/* If input < 0, Clear the contraints by pm qos min, and return */
	if (input <= 0)	{
		ufc_clear_min(domains);
		return count;
	}

	/* Find target domain, proper ufc frequency table, and index */
	list_for_each_entry_reverse(domain, domains, list) {
		unsigned int freq;

		/* Convert input to original frequency */
		freq = ufc_recover_frequency(input, scale++);

		/* As it is not target domain, release the min lock and get next */
		if (freq < domain->min_freq) {
			pm_qos_update_request(&domain->user_min_qos_req, 0);
			continue;
		}

		/*Get proper ufc table to pm qos min */
		ufc_get_proper_table(domain, &r_ufc, PM_QOS_MIN_LIMIT);

		/* This case means that current domain is target. Break loop */
		if (r_ufc) {
			target_ufc_table_index =
				ufc_get_proper_table_index(freq, r_ufc);
			break;
		}

		/*
		 * This case means that target domain is right, but,
		 * there is no UFC table in device tree.
		 * In this case, just apply the lock on the domain, and
		 * get out of this function
		 */
		pm_qos_update_request(&domain->user_min_qos_req, freq);
		return count;
	}

	/*
	 * If we find proper ufc table, r_ufc isn't NULL.
	 * If it is NULL, 'ufc_set_freq_target_domains()' will be bypassed.
	 */
	if (!r_ufc)
		return count;

	/* Set UFC Frequencies to target domains */
	ufc_set_min_freq_target_domains(domains, domain, r_ufc,
			target_ufc_table_index);
	return count;
}

static ssize_t ufc_update_min_limit_wo_boost(int input_freq, int count)
{
	struct list_head *domains = get_domain_list();
	struct exynos_cpufreq_domain *domain;
	struct exynos_ufc *r_ufc = NULL;
	int input = input_freq, scale = 0;
	unsigned int target_ufc_table_index;

	if (!domains) {
		pr_err("failed to get domains!\n");
		return -ENXIO;
	}

	/* If input <= 0, Clear the contraints by pm qos min, and return */
	if (input <= 0) {
		ufc_clear_min_wo_boost(domains);
		return count;
	}

	/* Find target domain, proper ufc frequency table, and index */
	list_for_each_entry_reverse(domain, domains, list) {
		unsigned int freq;

		/* Convert input to original frequency */
		freq = ufc_recover_frequency(input, scale++);

		/* As it is not target domain, release the min lock and get next */
		if (freq < domain->min_freq) {
			pm_qos_update_request(&domain->user_min_qos_wo_boost_req, 0);
			continue;
		}

		/*Get proper ufc table to pm qos min */
		ufc_get_proper_table(domain, &r_ufc, PM_QOS_MIN_WO_BOOST_LIMIT);

		/* This case means that current domain is target. Break loop */
		if (r_ufc) {
			target_ufc_table_index =
				ufc_get_proper_table_index(freq, r_ufc);
			break;
		}

		/*
		 * This case means that target domain is right, but,
		 * there is no UFC table in device tree.
		 * In this case, just apply the lock on the domain, and
		 * get out of this function
		 */
		pm_qos_update_request(&domain->user_min_qos_wo_boost_req, freq);
		return count;
	}

	/*
	 * If we find proper ufc table, r_ufc isn't NULL.
	 * If it is NULL, 'ufc_set_freq_target_domains()' will be passed.
	 */
	if (!r_ufc)
		return count;

	/* Set UFC Frequencies to target domains */
	ufc_set_min_wo_boost_freq_target_domains(domains, domain, r_ufc,
			target_ufc_table_index);

	return count;
}

static ssize_t ufc_update_max_limit(int input_freq, int count)
{
	struct list_head *domains = get_domain_list();
	struct exynos_cpufreq_domain *domain;
	struct exynos_ufc *r_ufc = NULL;
	int input = input_freq, scale = 0;
	unsigned int target_ufc_table_index;

	if (!domains) {
		pr_err("failed to get domains!\n");
		return -ENXIO;
	}

	/* If input <= 0, Clear the contraints by pm qos min, and return */
	if (input <= 0) {
		ufc_clear_max(domains);
		return count;
	}

	/* Find target domain, proper ufc frequency table, and index */
	list_for_each_entry_reverse(domain, domains, list) {
		unsigned int freq;

		/* Convert input to original frequency */
		freq = ufc_recover_frequency(input, scale++);
		if (freq < domain->min_freq) {
			disable_domain_cpus(domain, domains);
			continue;
		}

		/* Get proper ufc table to pm qos min */
		ufc_get_proper_table(domain, &r_ufc, PM_QOS_MAX_LIMIT);

		/* This case means that current domain is target. Break loop */
		if (r_ufc) {
			target_ufc_table_index =
				ufc_get_proper_table_index(freq, r_ufc);
			break;
		}

		/*
		 * This case means that target domain is right, but,
		 * there is no UFC table in device tree.
		 * In this case, just apply the lock on the domain, and
		 * get out of this function
		 */
		pm_qos_update_request(&domain->user_max_qos_req, freq);
		return count;
	}

	/*
	 * If we find proper ufc table, r_ufc isn't NULL.
	 * If it is NULL, 'ufc_set_freq_target_domains()' will be passed.
	 */
	if (!r_ufc)
		return count;

	/* Set UFC Frequencies to target domains */
	ufc_set_max_freq_target_domains(domains, domain, r_ufc,
			target_ufc_table_index);
	return count;
}

/*********************************************************************
 *                          SYSFS FUNCTION                           *
 *********************************************************************/
static ssize_t ufc_show_cpufreq_table(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct list_head *domains = get_domain_list();
	struct exynos_cpufreq_domain *domain;
	ssize_t count = 0;
	int i, scale = 0;

	list_for_each_entry_reverse(domain, domains, list) {
		for (i = 0; i < domain->table_size; i++) {
			unsigned int freq = domain->freq_table[i].frequency;

			if (freq == CPUFREQ_ENTRY_INVALID)
				continue;

			count += snprintf(&buf[count], 10, "%d ",
					ufc_convert_frequency(freq, scale));
		}
		scale++;
	}

	count += snprintf(&buf[count - 1], 2, "\n");

	return count - 1;
}

static ssize_t ufc_show_cpufreq_min_limit(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct list_head *domains = get_domain_list();
	struct exynos_cpufreq_domain *domain;
	unsigned int pm_qos_min;
	int scale = 0;

	list_for_each_entry_reverse(domain, domains, list) {
		/* get value of minimum PM QoS */
		pm_qos_min = pm_qos_request(domain->pm_qos_min_class);
		if (pm_qos_min > 0) {
			pm_qos_min = min(pm_qos_min, domain->max_freq);
			pm_qos_min = max(pm_qos_min, domain->min_freq);

			/*
			 * To manage frequencies of all domains at once,
			 * scale down frequency as multiple of 8.
			 * ex) domain2 = freq
			 *     domain1 = freq / 8
			 *     domain0 = freq / 64
			 */
			pm_qos_min = ufc_convert_frequency(pm_qos_min, scale++);

			return snprintf(buf, 10, "%u\n", pm_qos_min);
		}
	}

	/*
	 * If there is no QoS at all domains, it returns minimum
	 * frequency of last domain
	 */
	return snprintf(buf, 10, "%u\n",
		first_domain()->min_freq >> (scale * SCALE_SIZE));
}

static ssize_t ufc_store_cpufreq_min_limit(struct kobject *kobj,
				struct kobj_attribute *attr, const char *buf,
				size_t count)
{
	int input;

	if (!sscanf(buf, "%8d", &input))
		return -EINVAL;

	/* Save the input for sse change */
	last_min_limit = input;

	return ufc_update_min_limit(input, count);
}

static ssize_t ufc_store_cpufreq_min_limit_wo_boost(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf,
		size_t count)
{
	int input;

	if (!sscanf(buf, "%8d", &input))
		return -EINVAL;

	/* Save the input for sse change */
	last_min_wo_boost_limit = input;

	return ufc_update_min_limit_wo_boost(input, count);
}

static ssize_t ufc_store_cpufreq_max_limit(struct kobject *kobj, struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	int input;

	if (!sscanf(buf, "%8d", &input))
		return -EINVAL;

	/* Save the input for sse change */
	last_max_limit = input;

	return ufc_update_max_limit(input, count);
}

static ssize_t ufc_show_cpufreq_max_limit(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct list_head *domains = get_domain_list();
	struct exynos_cpufreq_domain *domain;
	unsigned int pm_qos_max;
	int scale = 0;

	if (!domains) {
		pr_err("failed to get domains!\n");
		return -ENXIO;
	}

	list_for_each_entry_reverse(domain, domains, list) {

		/* get value of minimum PM QoS */
		pm_qos_max = pm_qos_request(domain->pm_qos_max_class);
		if (pm_qos_max > 0) {
			pm_qos_max = min(pm_qos_max, domain->max_freq);
			pm_qos_max = max(pm_qos_max, domain->min_freq);

			/*
			 * To manage frequencies of all domains at once,
			 * scale down frequency as multiple of 8.
			 * ex) domain2 = freq
			 *     domain1 = freq /8
			 *     domain0 = freq /64
			 */
			pm_qos_max = ufc_convert_frequency(pm_qos_max, scale++);
			return snprintf(buf, 10, "%u\n", pm_qos_max);
		}
	}

	/*
	 * If there is no QoS at all domains, it returns minimum
	 * frequency of last domain
	 */
	return snprintf(buf, 10, "%u\n",
		first_domain()->min_freq >> (scale * SCALE_SIZE));
}

static ssize_t ufc_show_execution_mode_change(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, 10, "%d\n", sse_mode);
}

static ssize_t ufc_store_execution_mode_change(struct kobject *kobj, struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	int input;
	int prev_mode;

	if (!sscanf(buf, "%8d", &input))
		return -EINVAL;

	prev_mode = sse_mode;
	sse_mode = !!input;

	if (prev_mode != sse_mode) {
		ufc_update_max_limit(last_max_limit, count);
		ufc_update_min_limit(last_min_limit, count);
		ufc_update_min_limit_wo_boost(last_min_wo_boost_limit, count);
	}

	return count;
}

static struct kobj_attribute cpufreq_table =
__ATTR(cpufreq_table, 0444, ufc_show_cpufreq_table, NULL);
static struct kobj_attribute cpufreq_min_limit =
__ATTR(cpufreq_min_limit, 0644,
		ufc_show_cpufreq_min_limit, ufc_store_cpufreq_min_limit);
static struct kobj_attribute cpufreq_min_limit_wo_boost =
__ATTR(cpufreq_min_limit_wo_boost, 0644,
		ufc_show_cpufreq_min_limit, ufc_store_cpufreq_min_limit_wo_boost);
static struct kobj_attribute cpufreq_max_limit =
__ATTR(cpufreq_max_limit, 0644,
		ufc_show_cpufreq_max_limit, ufc_store_cpufreq_max_limit);
static struct kobj_attribute execution_mode_change =
__ATTR(execution_mode_change, 0644,
		ufc_show_execution_mode_change, ufc_store_execution_mode_change);

/*********************************************************************
 *                          INIT FUNCTION                          *
 *********************************************************************/
static __init void ufc_init_sysfs(void)
{
	if (sysfs_create_file(power_kobj, &cpufreq_table.attr))
		pr_err("failed to create cpufreq_table node\n");

	if (sysfs_create_file(power_kobj, &cpufreq_min_limit.attr))
		pr_err("failed to create cpufreq_min_limit node\n");

	if (sysfs_create_file(power_kobj, &cpufreq_min_limit_wo_boost.attr))
		pr_err("failed to create cpufreq_min_limit_wo_boost node\n");

	if (sysfs_create_file(power_kobj, &cpufreq_max_limit.attr))
		pr_err("failed to create cpufreq_max_limit node\n");

	if (sysfs_create_file(power_kobj, &execution_mode_change.attr))
		pr_err("failed to create execution mode change node\n");
}

static int ufc_parse_ctrl_info(struct exynos_cpufreq_domain *domain,
					struct device_node *dn)
{
	unsigned int val;

	if (!of_property_read_u32(dn, "user-default-qos", &val))
		domain->user_default_qos = val;

	if (!of_property_read_u32(dn, "user-boost", &val))
		domain->user_boost = val;

	return 0;
}

static __init void ufc_init_pm_qos(struct exynos_cpufreq_domain *domain)
{
	pm_qos_add_request(&domain->user_min_qos_req,
			domain->pm_qos_min_class, domain->min_freq);
	pm_qos_add_request(&domain->user_max_qos_req,
			domain->pm_qos_max_class, domain->max_freq);
	pm_qos_add_request(&domain->user_min_qos_wo_boost_req,
			domain->pm_qos_min_class, domain->min_freq);
}

static __init int ufc_domain_init(struct exynos_cpufreq_domain *domain,
		struct device_node *cur_dn)
{
	struct device_node *dn = cur_dn, *child;

	/* Initialize list head of ufc list in domain structure */
	INIT_LIST_HEAD(&domain->ufc_list);

	for_each_child_of_node(dn, child) {
		struct exynos_ufc *ufc;
		unsigned int row, col;
		int i, j;

		ufc = kzalloc(sizeof(struct exynos_ufc), GFP_KERNEL);
		if (!ufc)
			return -ENOMEM;

		row = ufc->info.exynos_ufc_table_row = ufc_get_table_row(child);
		col = ufc->info.exynos_ufc_table_col = ufc_get_table_col(child);
		if ((row <= 0) || (col <= 0)) {
			kfree(ufc);
			continue;
		}

		/* UFC Freq table is allocated dynamically */
		ufc->info.exynos_ufc_table = kzalloc(sizeof(u32 *) * col, GFP_KERNEL);
		if (!ufc->info.exynos_ufc_table) {
			kfree(ufc);
			return -ENOMEM;
		}

		for (i = 0; i < col; i++) {
			ufc->info.exynos_ufc_table[i] =
				kzalloc(sizeof(u32) * row, GFP_KERNEL);

			if (!ufc->info.exynos_ufc_table[i]) {
				for (j = 0; ufc->info.exynos_ufc_table[j]; j++)
					kfree(ufc->info.exynos_ufc_table[j]);

				kfree(ufc->info.exynos_ufc_table);
				kfree(ufc);
				return -ENOMEM;
			}
		}

		list_add_tail(&ufc->list, &domain->ufc_list);
	}

	return 0;
}

static int __init ufc_table_parse_init(struct exynos_cpufreq_domain *domain,
		struct exynos_ufc *ufc, struct device_node *child)
{
	unsigned int row, col;
	int ret, i, j;

	row = ufc->info.exynos_ufc_table_row;
	col = ufc->info.exynos_ufc_table_col;

	/* Initialize the frequency of UFC table of target ufc */
	for (i = 0; i < col; i++) {
		for (j = 0; j < row; j++) {
			ret = of_property_read_u32_index(child, "table", (i + (j * col)),
					&(ufc->info.exynos_ufc_table[i][j]));
			if (ret)
				return -EINVAL;
		}
	}

	/* success */
	return 0;
}

static int __init ufc_init_table_dt(struct exynos_cpufreq_domain *domain,
					struct device_node *dn)
{
	struct device_node *child;
	struct exynos_ufc *ufc;
	int ret, i;

	ufc = list_entry(&domain->ufc_list, struct exynos_ufc, list);

	for_each_child_of_node(dn, child) {
		ufc = list_next_entry(ufc, list);

		if (of_property_read_u32(child, "ctrl-type", &ufc->info.ctrl_type))
			continue;

		if (of_property_read_u32(child, "execution-mode", &ufc->info.exe_mode))
			continue;

		/* parse ufc table from DT and init the table */
		ret = ufc_table_parse_init(domain, ufc, child);
		if (ret) {
			for (i = 0; ufc->info.exynos_ufc_table[i]; i++)
				kfree(ufc->info.exynos_ufc_table[i]);

			kfree(ufc->info.exynos_ufc_table);
			kfree(ufc);
			return -ENOMEM;
		}
	}

	return 0;
}

static int __init exynos_ufc_init(void)
{
	struct device_node *dn = NULL;
	const char *buf;
	struct exynos_cpufreq_domain *domain;
	int ret = 0;

	/* Iterate UFC domains */
	while((dn = of_find_node_by_type(dn, "cpufreq-userctrl"))) {
		struct cpumask shared_mask;

		ret = of_property_read_string(dn, "shared-cpus", &buf);
		if (ret) {
			pr_err("failed to get shared-cpus for ufc\n");
			goto exit;
		}

		cpulist_parse(buf, &shared_mask);
		domain = find_domain_cpumask(&shared_mask);
		if (!domain) {
			pr_err("Can't found domain for ufc!\n");
			goto exit;
		}

		/* Initialize UFC structure of target domain and Allocate UFC table*/
		ret = ufc_domain_init(domain, dn);
		if (ret) {
			pr_err("failed to init domain\n");
			goto exit;
		}

		/* Initialize user control information from dt */
		ret = ufc_parse_ctrl_info(domain, dn);
		if (ret) {
			pr_err("failed to get ufc ctrl info\n");
			goto exit;
		}

		/* Parse user frequency ctrl table info from dt */
		ret = ufc_init_table_dt(domain, dn);
		if (ret) {
			pr_err("failed to parse frequency table for ufc ctrl\n");
			goto exit;
		}

		/* Initialize PM QoS */
		ufc_init_pm_qos(domain);
	}

	ufc_init_sysfs();
exit:
	return 0;
}
late_initcall(exynos_ufc_init);
