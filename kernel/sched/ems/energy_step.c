/*
 * CPUFreq governor based on Energy-Step-Data And Scheduler-Event.
 *
 * Copyright (C) 2019,Samsung Electronic Corporation
 * Author: Youngtae Lee <yt0729.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kthread.h>
#include <linux/cpufreq.h>
#include <uapi/linux/sched/types.h>
#include <linux/slab.h>
#include <linux/cpu_pm.h>
#include <linux/pm_qos.h>

#include <trace/events/ems.h>

#include "../sched.h"
#include "../tune.h"
#include "ems.h"

struct esgov_policy {
	struct cpufreq_policy	*policy;
	struct cpumask		cpus;
	struct kobject		kobj;
	raw_spinlock_t		update_lock;
	bool			enabled;	/* whether esg is current cpufreq governor or not */

	unsigned int		target_freq;	/* target frequency at the current status */
	int			util;		/* target util  */
	u64			last_freq_update_time;

	/* The next fields are for the tunnables */
	int			step;
	unsigned long		step_power;	/* allowed energy at a step */
	s64			rate_delay_ns;

	/* Tracking min max information */
	int			min_cap;	/* allowed max capacity */
	int			max_cap;	/* allowed min capacity */

	int                     min;            /* min freq */
	int                     max;            /* max freq */

	unsigned int		qos_min_class;
	unsigned int		qos_max_class;
	struct notifier_block	qos_min_notifier;
	struct notifier_block	qos_max_notifier;

	/* The next fields are for frequency change work */
	bool			work_in_progress;
	struct			irq_work irq_work;
	struct			kthread_work work;
	struct			mutex work_lock;
	struct			kthread_worker worker;
	struct task_struct	*thread;
};

struct esgov_cpu {
	struct update_util_data	update_util;

	struct esgov_policy	*esg_policy;
	unsigned int		cpu;
	int			util;		/* target util */
	int			sutil;		/* scheduler util */
	int			eutil;		/* esg util */

	int			capacity;
	int			last_idx;
};

struct kobject *esg_kobj;
DEFINE_PER_CPU(struct esgov_policy *, esgov_policy);
DEFINE_PER_CPU(struct esgov_cpu, esgov_cpu);

/*************************************************************************/
/*			       HELPER FUNCTION				 */
/************************************************************************/
static struct esgov_policy * esg_find_esg_pol_qos_class(int class)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct esgov_policy *esg_policy = per_cpu(esgov_policy, cpu);

		if (unlikely(!esg_policy))
			continue;

		if ((esg_policy->qos_min_class == class) ||
			(esg_policy->qos_max_class == class))
			return esg_policy;
	}

	return NULL;
}

static void esg_update_step(struct esgov_policy *esg_policy, int step)
{
	int cpu = cpumask_first(&esg_policy->cpus);

	esg_policy->step = step;
	esg_policy->step_power = find_step_power(cpu, esg_policy->step);
}

static void esg_update_freq_range(struct cpufreq_policy *policy)
{
	unsigned long flags;
	unsigned int qos_min, qos_max, new_min, new_max, new_min_idx, new_max_idx;
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, policy->cpu);

	if (unlikely((!esg_policy) || !esg_policy->enabled))
		return;

	raw_spin_lock_irqsave(&esg_policy->update_lock, flags);

	qos_min = pm_qos_request(esg_policy->qos_min_class);
	qos_max = pm_qos_request(esg_policy->qos_max_class);

	new_min = max(policy->min, qos_min);
	new_max = min(policy->max, qos_max);

	if (esg_policy->min == new_min && esg_policy->max == new_max)	{
		raw_spin_unlock_irqrestore(&esg_policy->update_lock, flags);
		return;
	}

	esg_policy->min = new_min;
	esg_policy->max = new_max;

	new_min_idx = cpufreq_frequency_table_target(
				policy, new_min, CPUFREQ_RELATION_L);
	new_max_idx = cpufreq_frequency_table_target(
				policy, new_max, CPUFREQ_RELATION_H);

	new_min = esg_policy->policy->freq_table[new_min_idx].frequency;
	new_max = esg_policy->policy->freq_table[new_max_idx].frequency;

	esg_policy->min_cap = find_allowed_capacity(policy->cpu, new_min, 0);
	esg_policy->max_cap = find_allowed_capacity(policy->cpu, new_max, 0);
	esg_policy->min_cap = min(esg_policy->max_cap, esg_policy->min_cap);

	trace_esg_update_limit(policy->cpu, esg_policy->min_cap, esg_policy->max_cap);

	raw_spin_unlock_irqrestore(&esg_policy->update_lock, flags);
}

static int esg_cpufreq_policy_callback(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;

	if (event == CPUFREQ_NOTIFY)
		esg_update_freq_range(policy);

	return NOTIFY_OK;
}

static struct notifier_block esg_cpufreq_policy_notifier = {
	.notifier_call = esg_cpufreq_policy_callback,
};

static int esg_cpufreq_pm_qos_callback(struct notifier_block *nb,
					unsigned long val, void *v)
{
	int pm_qos_class = *((int *)v);
	struct esgov_policy *esg_pol;

	esg_pol = esg_find_esg_pol_qos_class(pm_qos_class);
	if (unlikely(!esg_pol))
		return NOTIFY_OK;

	esg_update_freq_range(esg_pol->policy);

	return NOTIFY_OK;
}

struct esg_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *, char *);
	ssize_t (*store)(struct kobject *, const char *, size_t count);
};

#define esg_attr_rw(name)				\
static struct esg_attr name##_attr =			\
__ATTR(name, 0644, show_##name, store_##name)

#define esg_show(name, related_val)						\
static ssize_t show_##name(struct kobject *k, char *buf)			\
{										\
	struct esgov_policy *esg_policy =					\
			container_of(k, struct esgov_policy, kobj);		\
										\
	return sprintf(buf, "step: %d (energy: %lu)\n",				\
			esg_policy->name, esg_policy->related_val);		\
}

#define esg_store(name)								\
static ssize_t store_##name(struct kobject *k, const char *buf, size_t count)	\
{										\
	struct esgov_policy *esg_policy =					\
			container_of(k, struct esgov_policy, kobj);		\
	int data;								\
										\
	if (!sscanf(buf, "%d", &data))						\
		return -EINVAL;							\
										\
	esg_update_##name(esg_policy, data);					\
										\
	return count;								\
}

esg_show(step, step_power);
esg_store(step);
esg_attr_rw(step);

static ssize_t show(struct kobject *kobj, struct attribute *at, char *buf)
{
	struct esg_attr *fvattr = container_of(at, struct esg_attr, attr);
	return fvattr->show(kobj, buf);
}

static ssize_t store(struct kobject *kobj, struct attribute *at,
					const char *buf, size_t count)
{
	struct esg_attr *fvattr = container_of(at, struct esg_attr, attr);
	return fvattr->store(kobj, buf, count);
}

static const struct sysfs_ops esg_sysfs_ops = {
	.show	= show,
	.store	= store,
};

static struct attribute *esg_attrs[] = {
	&step_attr.attr,
	NULL
};

static struct kobj_type ktype_esg = {
	.sysfs_ops	= &esg_sysfs_ops,
	.default_attrs	= esg_attrs,
};

static struct esgov_policy *esgov_policy_alloc(struct cpufreq_policy *policy)
{
	struct device_node *dn = NULL;
	struct esgov_policy *esg_policy;
	const char *buf;
	char name[15];
	int val;

	/* find DT for this policy */
	snprintf(name, sizeof(name), "esgov-pol%d", policy->cpu);
	dn = of_find_node_by_type(dn, name);
	if (!dn)
		goto init_failed;

	/* allocate esgov_policy */
	esg_policy = kzalloc(sizeof(struct esgov_policy), GFP_KERNEL);
	if (!esg_policy)
		goto init_failed;

	/* initialize shared data of esgov_policy */
	if (of_property_read_string(dn, "shared-cpus", &buf))
		goto free_allocation;

	cpulist_parse(buf, &esg_policy->cpus);
	cpumask_and(&esg_policy->cpus, &esg_policy->cpus, policy->cpus);
	if (cpumask_weight(&esg_policy->cpus) == 0)
		goto free_allocation;

	/* Init tunable knob */
	if (of_property_read_u32(dn, "step", &val))
		goto free_allocation;

	esg_update_step(esg_policy, val);
	esg_policy->rate_delay_ns = 4 * NSEC_PER_MSEC;

	/* Init Sysfs */
	if (kobject_init_and_add(&esg_policy->kobj, &ktype_esg, esg_kobj,
			"coregroup%d", cpumask_first(&esg_policy->cpus)))
		goto free_allocation;

	/* init spin lock */
	raw_spin_lock_init(&esg_policy->update_lock);

	/* init pm qos */
	if (of_property_read_u32(dn, "pm_qos-min-class", &esg_policy->qos_min_class))
		goto free_allocation;
	if (of_property_read_u32(dn, "pm_qos-max-class", &esg_policy->qos_max_class))
		goto free_allocation;

	esg_policy->qos_min_notifier.notifier_call = esg_cpufreq_pm_qos_callback;
	esg_policy->qos_min_notifier.priority = INT_MAX;
	esg_policy->qos_max_notifier.notifier_call = esg_cpufreq_pm_qos_callback;
	esg_policy->qos_max_notifier.priority = INT_MAX;

	pm_qos_add_notifier(esg_policy->qos_min_class, &esg_policy->qos_min_notifier);
	pm_qos_add_notifier(esg_policy->qos_max_class, &esg_policy->qos_max_notifier);

	esg_policy->policy = policy;

	return esg_policy;

free_allocation:
	kfree(esg_policy);

init_failed:
	pr_warn("%s: Failed esgov_init(cpu%d)\n", __func__, policy->cpu);

	return NULL;
}

static void esgov_work(struct kthread_work *work)
{
	struct esgov_policy *esg_policy = container_of(work, struct esgov_policy, work);
	unsigned int freq;
	unsigned long flags;

	raw_spin_lock_irqsave(&esg_policy->update_lock, flags);
	freq = esg_policy->target_freq;
	esg_policy->work_in_progress = false;
	raw_spin_unlock_irqrestore(&esg_policy->update_lock, flags);

	down_write(&esg_policy->policy->rwsem);
	mutex_lock(&esg_policy->work_lock);
	__cpufreq_driver_target(esg_policy->policy, freq, CPUFREQ_RELATION_L);
	mutex_unlock(&esg_policy->work_lock);
	up_write(&esg_policy->policy->rwsem);
}

static void esgov_irq_work(struct irq_work *irq_work)
{
	struct esgov_policy *esg_policy;

	esg_policy = container_of(irq_work, struct esgov_policy, irq_work);

	kthread_queue_work(&esg_policy->worker, &esg_policy->work);
}

static int esgov_kthread_create(struct esgov_policy *esg_policy)
{
	struct task_struct *thread;
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO / 2 };
	struct cpufreq_policy *policy = esg_policy->policy;
	int ret;

	kthread_init_work(&esg_policy->work, esgov_work);
	kthread_init_worker(&esg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &esg_policy->worker,
				"esgov:%d", cpumask_first(policy->related_cpus));
	if (IS_ERR(thread)) {
		pr_err("failed to create esgov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setscheduler_nocheck(thread, SCHED_FIFO, &param);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set SCHED_CLASS\n", __func__);
		return ret;
	}

	esg_policy->thread = thread;
	init_irq_work(&esg_policy->irq_work, esgov_irq_work);
	mutex_init(&esg_policy->work_lock);

	wake_up_process(thread);

	return 0;
}

static void esgov_policy_free(struct esgov_policy *esg_policy)
{
	kfree(esg_policy);
}

static int esgov_init(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy;
	int ret = 0;
	int cpu;

	if (policy->governor_data)
		return -EBUSY;

	esg_policy = per_cpu(esgov_policy, policy->cpu);
	if (per_cpu(esgov_policy, policy->cpu)) {
		pr_info("%s: Already allocated esgov_policy\n", __func__);
		goto complete_esg_init;
	}

	esg_policy = esgov_policy_alloc(policy);
	if (!esg_policy) {
		ret = -ENOMEM;
		goto failed_to_init;
	}

	ret = esgov_kthread_create(esg_policy);
	if (ret)
		goto free_esg_policy;

	for_each_cpu(cpu, &esg_policy->cpus)
		per_cpu(esgov_policy, cpu) = esg_policy;

complete_esg_init:
	policy->governor_data = esg_policy;
	esg_policy->min = policy->min;
	esg_policy->max = policy->max;
	esg_policy->min_cap = find_allowed_capacity(policy->cpu, policy->min, 0);
	esg_policy->max_cap = find_allowed_capacity(policy->cpu, policy->max, 0);
	esg_policy->enabled = true;;

	return 0;

free_esg_policy:
	esgov_policy_free(esg_policy);

failed_to_init:
	pr_err("initialization failed (error %d)\n", ret);

	return ret;
}

static void esgov_exit(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, policy->cpu);

	esg_policy->enabled = false;;
	policy->governor_data = NULL;
}

static unsigned int get_next_freq(struct esgov_policy *esg_policy, unsigned long util)
{
	struct cpufreq_policy *policy = esg_policy->policy;
	int max = arch_scale_cpu_capacity(NULL, policy->cpu);
	unsigned int freq;

	freq = (policy->max * util) / max;

	return cpufreq_driver_resolve_freq(policy, freq);
}

static unsigned long esgov_get_eutil(struct esgov_cpu *esg_cpu, unsigned long max)
{
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, esg_cpu->cpu);
	struct rq *rq = cpu_rq(esg_cpu->cpu);
	struct part *pa = &rq->pa;
	unsigned long freq;
	int active_ratio, util, prev_idx;

	if (unlikely(!esg_policy || !esg_policy->step))
		return 0;

	if (esg_cpu->last_idx == pa->hist_idx)
		return esg_cpu->eutil;

	/* update and get the active ratio */
	update_cpu_active_ratio(rq, NULL, EMS_PART_UPDATE);

	esg_cpu->last_idx = pa->hist_idx;
	prev_idx = esg_cpu->last_idx ? (esg_cpu->last_idx - 1) : 9;
	active_ratio = (pa->hist[esg_cpu->last_idx] + pa->hist[prev_idx]) >> 1;

	/* update the capacity */
	freq = (esg_cpu->eutil * esg_policy->policy->max) / max;
	esg_cpu->capacity = find_allowed_capacity(esg_cpu->cpu, freq, esg_policy->step_power);

	/* calculate eutil */
	util = (esg_cpu->capacity * active_ratio) >> SCHED_CAPACITY_SHIFT;

	trace_esg_cpu_eutil(esg_cpu->cpu, esg_cpu->capacity, active_ratio, max, util);

	return util;
}

unsigned long ml_boosted_cpu_util(int cpu);
static void esgov_update_cpu_util(struct esgov_cpu *esg_cpu)
{
	struct rq *rq = cpu_rq(esg_cpu->cpu);
	int max = arch_scale_cpu_capacity(NULL, esg_cpu->cpu);
	int util, eutil, sutil;

	sutil = ml_boosted_cpu_util(esg_cpu->cpu) + sched_get_rt_rq_util(rq->cpu);
	sutil = sutil + (sutil >> 2);
	esg_cpu->sutil = sutil;

	eutil = esgov_get_eutil(esg_cpu, max);
	esg_cpu->eutil = eutil;

	util = max(sutil, eutil);
	util = emst_boost(esg_cpu->cpu, util);
	esg_cpu->util = util;

	trace_esg_cpu_util(esg_cpu->cpu, eutil, sutil, max, util);
}

static bool esgov_check_rate_delay(struct esgov_policy *esg_policy, u64 time)
{
	s64 delta_ns = time - esg_policy->last_freq_update_time;

	if (delta_ns < esg_policy->rate_delay_ns)
		return false;

	return true;
}

/* returns target util of the cluster of this cpu */
static unsigned int esgov_get_target_util(struct esgov_cpu *esg_cpu)
{
	struct esgov_policy *esg_policy = esg_cpu->esg_policy;
	struct cpufreq_policy *policy = esg_policy->policy;
	unsigned long max_util = 0;
	unsigned int cpu;

	/* get max util in the cluster */
	for_each_cpu(cpu, policy->cpus) {
		struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);
		unsigned long cpu_util;

		/* If cpu is tickless status, ignore eutil */
		cpu_util = test_bit(NOHZ_TICK_STOPPED, nohz_flags(cpu)) ?
				esg_cpu->sutil : esg_cpu->util;

		if (cpu_util > max_util)
			max_util = cpu_util;
	}

	return max_util;
}

static void
esgov_update(struct update_util_data *hook, u64 time, unsigned int flags)
{
	struct esgov_cpu *esg_cpu = container_of(hook, struct esgov_cpu, update_util);
	struct esgov_policy *esg_policy = esg_cpu->esg_policy;
	unsigned int target_util, target_freq;

	/* update this cpu util */
	esgov_update_cpu_util(esg_cpu);

	if (!esgov_check_rate_delay(esg_policy, time))
		return;

	target_util = esgov_get_target_util(esg_cpu);
	/* update target util of the cluster of this cpu */
	if (esg_policy->util == target_util)
		return;

	/* get target freq for new target util */
	target_freq = get_next_freq(esg_policy, target_util);
	if (esg_policy->target_freq == target_freq)
		return;

	if (!esg_policy->work_in_progress) {
		esg_policy->work_in_progress = true;
		esg_policy->util = target_util;
		esg_policy->target_freq = target_freq;
		esg_policy->last_freq_update_time = time;
		trace_esg_req_freq(esg_policy->policy->cpu,
			esg_policy->util, esg_policy->target_freq);
		irq_work_queue(&esg_policy->irq_work);
	}
}

static int esgov_start(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy = policy->governor_data;
	unsigned int cpu;

	/* TODO: We SHOULD implement FREQVAR-RATE-DELAY Base on SchedTune */
	esg_policy->last_freq_update_time = 0;
	esg_policy->target_freq = 0;
	esg_policy->work_in_progress = false;

	for_each_cpu(cpu, policy->cpus) {
		struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);
		esg_cpu->esg_policy = esg_policy;
		esg_cpu->cpu = cpu;
	}

	for_each_cpu(cpu, policy->cpus) {
		struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);
		cpufreq_add_update_util_hook(cpu, &esg_cpu->update_util,
							esgov_update);
	}

	return 0;
}

static void esgov_stop(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_sched();
	irq_work_sync(&esg_policy->irq_work);
}

static void esgov_limits(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy = policy->governor_data;

	mutex_lock(&esg_policy->work_lock);
	cpufreq_policy_apply_limits(policy);
	mutex_unlock(&esg_policy->work_lock);
}

struct cpufreq_governor energy_step_gov = {
	.name			= "energy_step",
	.owner			= THIS_MODULE,
	.dynamic_switching	= true,
	.init			= esgov_init,
	.exit			= esgov_exit,
	.start			= esgov_start,
	.stop			= esgov_stop,
	.limits			= esgov_limits,
};

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ENERGYSTEP
int get_gov_next_cap(int dst_cpu, struct task_struct *p)
{
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, dst_cpu);
	int cpu, next_cap = 0;
	int task_util;

	if (unlikely(!esg_policy || !esg_policy->enabled))
		return -ENODEV;

	/* get task util and convert to uss */
	task_util = ml_task_util(p);
	if (p->sse)
		task_util *= capacity_ratio(task_cpu(p), USS);

	/* get max util of the cluster of this cpu */
	for_each_cpu(cpu, esg_policy->policy->cpus) {
		struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);
		int cpu_util, sutil = esg_cpu->sutil;

		/* calculate cpu util with{out} task */
		if (cpu == dst_cpu && cpu != task_cpu(p))
			sutil += task_util;
		else if (cpu != dst_cpu && cpu == task_cpu(p))
			sutil -= task_util;

		cpu_util = max(sutil, esg_cpu->eutil);
		if (cpu_util > next_cap)
			next_cap = cpu_util;
	}

	/* max floor depends on CPUFreq min/max lock */
	next_cap = max(esg_policy->min_cap, next_cap);
	next_cap = min(esg_policy->max_cap, next_cap);

	return next_cap;
}

unsigned long cpufreq_governor_get_util(unsigned int cpu)
{
	struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);

	if (!esg_cpu)
		return 0;

	return esg_cpu->util;
}

unsigned int cpufreq_governor_get_freq(int cpu)
{
	struct esgov_policy *esg_policy;
	unsigned int freq;
	unsigned long flags;

	if (cpu < 0)
		return 0;

	esg_policy = per_cpu(esgov_policy, cpu);
	if (!esg_policy)
		return 0;

	raw_spin_lock_irqsave(&esg_policy->update_lock, flags);
	freq = esg_policy->target_freq;
	raw_spin_unlock_irqrestore(&esg_policy->update_lock, flags);

	return freq;
}

struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &energy_step_gov;
}
#endif

static int __init esgov_register(void)
{
	esg_kobj = kobject_create_and_add("energy_step", ems_kobj);
	if (!esg_kobj)
		return -EINVAL;

	cpufreq_register_notifier(&esg_cpufreq_policy_notifier,
					CPUFREQ_POLICY_NOTIFIER);

	return cpufreq_register_governor(&energy_step_gov);
}
fs_initcall(esgov_register);

