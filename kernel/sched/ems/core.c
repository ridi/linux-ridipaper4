/*
 * Core Exynos Mobile Scheduler
 *
 * Copyright (C) 2018 Samsung Electronics Co., Ltd
 * Park Bumgyu <bumgyu.park@samsung.com>
 */


#include "../sched.h"
#include "../tune.h"
#include "ems.h"

#define CREATE_TRACE_POINTS
#include <trace/events/ems.h>

static void select_fit_cpus(struct tp_env *env)
{
	struct cpumask *fit_cpus = &env->fit_cpus;
	struct task_struct *p = env->p;
	struct cpumask overutil_cpus;
	int cpu;

	/*
	 * Get fit cpus from ontime migration.
	 * The fit cpus is determined by the size of task runnable.
	 *
	 * fit_cpus = fit_cpus from ontime
	 */
	ontime_select_fit_cpus(p, fit_cpus);

	/*
	 * Exclude overutilized cpu from fit cpus.
	 * If assigning a task to a cpu and the cpu becomes overutilized, then
	 * the cpu may not be able to process the task properly, and
	 * performance may drop. Therefore, overutil cpu is excluded.
	 *
	 * However, if cpu is overutilized but there is no running task on the
	 * cpu, this cpu is also a candidate because the cpu is likely to become
	 * idle.
	 *
	 * fit_cpus = fit_cpus & ~(running overutilized cpus)
	 */
	cpumask_clear(&overutil_cpus);
	for_each_cpu(cpu, cpu_active_mask) {
		unsigned long capacity = capacity_cpu(cpu, p->sse);
		unsigned long new_util = _ml_cpu_util(cpu, p->sse);

		if (task_cpu(p) != cpu)
			new_util += ml_task_util(p);

		if (cpu_overutilized(capacity, new_util) && cpu_rq(cpu)->nr_running)
			cpumask_set_cpu(cpu, &overutil_cpus);
	}

	cpumask_andnot(fit_cpus, fit_cpus, &overutil_cpus);

	/*
	 * If all fit cpus are overutilized, fit cpus become empty.
	 * But in task migration sequence, it does not need to consider whether
	 * fit cpus is empty or not because the cpu is already busy, there is no
	 * performance gain even if migrating tasks to faster cpu.
	 */
	if (!env->wake) {
		/*
		 * Do not migrate tasks to cpu that is used more than 12.5%.
		 * (12.5% : this percentage is heuristically obtained)
		 *
		 * fit_cpus = fit_cpus with util < 12.5%
		 */
		for_each_cpu(cpu, cpu_active_mask) {
			int threshold = capacity_cpu(cpu, p->sse) >> 3;

			if (_ml_cpu_util(cpu, p->sse) >= threshold)
				cpumask_clear_cpu(cpu, fit_cpus);
		}

		goto skip;
	}

	/*
	 * Unfortunately, if all of the cpus are overutilized, significantly
	 * low-utilized cpus(< ~1.56%) become inevitably candidates.
	 *
	 * fit_cpus = cpus with util < 1.56%
	 */
	if (unlikely(cpumask_equal(cpu_active_mask, &overutil_cpus))) {
		cpumask_clear(fit_cpus);
		for_each_cpu(cpu, cpu_active_mask) {
			int threshold = capacity_cpu(cpu, p->sse) >> 6;

			if (_ml_cpu_util(cpu, p->sse) < threshold)
				cpumask_set_cpu(cpu, fit_cpus);
		}

		goto skip;
	}

	/*
	 * If fit cpus are empty, ignore fit cpus found in the previous step
	 * and all non-overutilized cpus become fit cpus.
	 *
	 * fit_cpus = all cpus & ~(overutilized cpus)
	 */
	if (cpumask_empty(fit_cpus))
		cpumask_andnot(fit_cpus, cpu_active_mask, &overutil_cpus);

skip:
	cpumask_and(fit_cpus, fit_cpus, emst_get_candidate_cpus(env->p));
	cpumask_and(fit_cpus, fit_cpus, ecs_sparing_cpus());
	cpumask_and(fit_cpus, fit_cpus, cpu_active_mask);
	cpumask_and(fit_cpus, fit_cpus, &env->p->cpus_allowed);

	trace_ems_select_fit_cpus(env->p, env->wake,
		*(unsigned int *)cpumask_bits(fit_cpus),
		*(unsigned int *)cpumask_bits(&overutil_cpus));
}

extern void sync_entity_load_avg(struct sched_entity *se);

int exynos_select_task_rq(struct task_struct *p, int prev_cpu,
				int sd_flag, int sync, int wake)
{
	int target_cpu = -1;
	struct tp_env env = {
		.p = p,
		.prefer_perf = global_boost() || schedtune_prefer_perf(p),
		.prefer_idle = schedtune_prefer_idle(p),
		.wake = wake,
	};

	/*
	 * Update utilization of waking task to apply "sleep" period
	 * before selecting cpu.
	 */
	if (!(sd_flag & SD_BALANCE_FORK))
		sync_entity_load_avg(&p->se);

	if (wake) {
		target_cpu = select_service_cpu(p);
		if (target_cpu >= 0) {
			trace_ems_select_task_rq(p, target_cpu, wake, "service");
			return target_cpu;
		}
	}

	select_fit_cpus(&env);

	if (sysctl_sched_sync_hint_enable && sync) {
		target_cpu = smp_processor_id();
		if (cpumask_test_cpu(target_cpu, &env.fit_cpus)) {
			trace_ems_select_task_rq(p, target_cpu, wake, "sync");
			return target_cpu;
		}
	}

	/* There is no fit cpus */
	if (cpumask_empty(&env.fit_cpus)) {
		trace_ems_select_task_rq(p, prev_cpu, wake, "no fit cpu");
		return prev_cpu;
	}

	if (!wake) {
		struct cpumask mask;

		/*
		 * 'wake = 0' means that running task is migrated to faster
		 * cpu by ontime migration. If there are fit faster cpus,
		 * current coregroup and env.fit_cpus are exclusive.
		 */
		cpumask_and(&mask, cpu_coregroup_mask(prev_cpu), &env.fit_cpus);
		if (cpumask_weight(&mask)) {
			trace_ems_select_task_rq(p, -1, wake, "no fit faster cpu");
			return -1;
		}
	}

	target_cpu = find_best_cpu(&env);
	if (target_cpu >= 0) {
		trace_ems_select_task_rq(p, target_cpu, wake, "best_cpu");
		return target_cpu;
	}

	target_cpu = prev_cpu;
	trace_ems_select_task_rq(p, target_cpu, wake, "no benefit");

	return target_cpu;
}

static ssize_t show_sched_topology(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int cpu;
	struct sched_domain *sd;
	int ret = 0;

	rcu_read_lock();
	for_each_possible_cpu(cpu) {
		int sched_domain_level = 0;

		sd = rcu_dereference_check_sched_domain(cpu_rq(cpu)->sd);
		while (sd->parent) {
			sched_domain_level++;
			sd = sd->parent;
		}

		for_each_lower_domain(sd) {
			ret += snprintf(buf + ret, 70,
					"[lv%d] cpu%d: flags=%#x sd->span=%#x sg->span=%#x\n",
					sched_domain_level, cpu, sd->flags,
					*(unsigned int *)cpumask_bits(sched_domain_span(sd)),
					*(unsigned int *)cpumask_bits(sched_group_span(sd->groups)));
			sched_domain_level--;
		}
		ret += snprintf(buf + ret,
				50, "----------------------------------------\n");
	}
	rcu_read_unlock();

	return ret;
}

static struct kobj_attribute sched_topology_attr =
__ATTR(sched_topology, 0444, show_sched_topology, NULL);

struct kobject *ems_kobj;

static int __init init_ems_core(void)
{
	int ret;

	ems_kobj = kobject_create_and_add("ems", kernel_kobj);

	ret = sysfs_create_file(ems_kobj, &sched_topology_attr.attr);
	if (ret)
		pr_warn("%s: failed to create sysfs\n", __func__);

	return 0;
}
core_initcall(init_ems_core);

void __init init_ems(void)
{
	init_part();
}
