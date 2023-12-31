/*
 * Copyright (c) 2018 Park Bumgyu, Samsung Electronics Co., Ltd <bumgyu.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Exynos CPU Power Management driver implementation
 */

#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/tick.h>
#include <linux/psci.h>
#include <linux/cpuhotplug.h>
#include <linux/cpuidle_profiler.h>
#include <linux/cpuidle-moce.h>

#include <soc/samsung/exynos-cpupm.h>
#include <soc/samsung/exynos-powermode.h>
#include <soc/samsung/cal-if.h>
#include <soc/samsung/exynos-pmu.h>

static int cpupm_initialized;

/* Length of power mode name */
#define NAME_LEN	32

/*
 * Power modes
 * In CPUPM, power mode controls the power domain consisting of cpu and enters
 * the power mode by cpuidle. Basically, to enter power mode, all cpus in power
 * domain must be in POWERDOWN state, and sleep length of cpus must be smaller
 * than target_residency.
 */
struct power_mode {
	/* id of this power mode, it is used by cpuidle profiler now */
	int		id;

	/* name of power mode, it is declared in device tree */
	char		name[NAME_LEN];

	/* power mode state, RUN or POWERDOWN */
	int		state;

	/* sleep length criterion of cpus to enter power mode */
	int		target_residency;

	/* PSCI index for entering power mode */
	int		psci_index;

	/* type according to h/w configuration of power domain */
	int		type;

	/* cpus belonging to the power domain */
	struct cpumask	siblings;

	/*
	 * Among siblings, the cpus that can enter the power mode.
	 * Due to H/W constraint, only certain cpus need to enter power mode.
	 */
	struct cpumask	entry_allowed;

	/* disable count */
	atomic_t	disable;

	/*
	 * Some power modes can determine whether to enter power mode
	 * depending on system idle state
	 */
	bool		system_idle;

	/*
	 * kobject attribute for sysfs,
	 * it supports for enabling or disabling this power mode
	 */
	struct kobj_attribute	attr;

	/* user's request for enabling/disabling power mode */
	bool		user_request;
};

/* Maximum number of power modes manageable per cpu */
#define MAX_MODE	5

/*
 * Main struct of CPUPM
 * Each cpu has its own data structure and main purpose of this struct is to
 * manage the state of the cpu and the power modes containing the cpu.
 */
struct exynos_cpupm {
	/* cpu state, RUN or POWERDOWN */
	int			state;

	/* array to manage the power mode that contains the cpu */
	struct power_mode *	modes[MAX_MODE];
};

static DEFINE_PER_CPU(struct exynos_cpupm, cpupm);

/******************************************************************************
 *                                CAL interfaces                              *
 ******************************************************************************/
static void cpu_enable(unsigned int cpu)
{
	cal_cpu_enable(cpu);
}

static void cpu_disable(unsigned int cpu)
{
	cal_cpu_disable(cpu);
}

static DEFINE_PER_CPU(int, vcluster_id);
static void cluster_enable(unsigned int cpu_id)
{
	unsigned int cluster_id = per_cpu(vcluster_id, cpu_id);

	cal_cluster_enable(cluster_id);
}

static void cluster_disable(unsigned int cpu_id)
{
	unsigned int cluster_id = per_cpu(vcluster_id, cpu_id);

	cal_cluster_disable(cluster_id);
}

#ifdef CONFIG_ARM64_EXYNOS_CPUIDLE
/******************************************************************************
 *                                  IDLE_IP                                   *
 ******************************************************************************/
#define PMU_IDLE_IP			0x03E0

#define IDLE_IP_MAX			64
#define FIX_IDLE_IP_MAX			32

static DEFINE_SPINLOCK(idle_ip_list_lock);
static DEFINE_SPINLOCK(idle_ip_bit_field_lock);
static unsigned long long idle_ip_bit_field;

/*
 * list head of idle-ip
 */
LIST_HEAD(idle_ip_list);

static int fix_idle_ip_count;

/*
 * array of fix-idle-ip
 */
struct fix_idle_ip fix_idle_ip_arr[FIX_IDLE_IP_MAX];

static int get_index_last_entry(struct list_head *head)
{
	struct idle_ip *ip;

	if (list_empty(head))
		return -1;

	ip = list_last_entry(head, struct idle_ip, list);

	return ip->index;
}

static bool check_fix_idle_ip(void)
{
	unsigned int val, mask;

	if (fix_idle_ip_count <= 0)
		return false;

	exynos_pmu_read(PMU_IDLE_IP, &val);
	/*
	 * Up to 32 fix-idle-ip are supported.
	 * HW register value is
	 * 			== 0, it means non-idle.
	 * 			== 1, it means idle.
	 */
	mask = (1 << fix_idle_ip_count) - 1;
	val = ~val & mask;
	if (val) {
		cpuidle_profile_fix_idle_ip(val, fix_idle_ip_count);

		return true;
	}

	return false;
}

static bool check_idle_ip(void)
{
	unsigned long flags;

	spin_lock_irqsave(&idle_ip_bit_field_lock, flags);

	if (idle_ip_bit_field) {
		cpuidle_profile_idle_ip(idle_ip_bit_field);
		spin_unlock_irqrestore(&idle_ip_bit_field_lock, flags);

		return true;
	}
	spin_unlock_irqrestore(&idle_ip_bit_field_lock, flags);

	return check_fix_idle_ip();
}

/*
 * @ip_index: index of idle-ip
 * @idle: idle status, (idle == 0)non-idle or (idle == 1)idle
 */
void exynos_update_ip_idle_status(int ip_index, int idle)
{
	unsigned long flags;
	unsigned long long val = 1;

	if (ip_index < 0 || ip_index > get_index_last_entry(&idle_ip_list))
		return;

	spin_lock_irqsave(&idle_ip_bit_field_lock, flags);
	if (idle)
		idle_ip_bit_field &= ~(val << ip_index);
	else
		idle_ip_bit_field |= (val << ip_index);
	spin_unlock_irqrestore(&idle_ip_bit_field_lock, flags);
}

int exynos_get_idle_ip_index(const char *ip_name)
{
	struct idle_ip *ip;
	int ip_index;
	unsigned long flags;

	ip = kzalloc(sizeof(struct idle_ip), GFP_KERNEL);
	if (!ip) {
		pr_err("Faile to allocate idle_ip [failed %s].", ip_name);
		return -1;
	}

	spin_lock_irqsave(&idle_ip_list_lock, flags);
	ip_index = get_index_last_entry(&idle_ip_list) + 1;
	if (ip_index >= IDLE_IP_MAX) {
		spin_unlock_irqrestore(&idle_ip_list_lock, flags);
		pr_err("Up to 64 idle-ip can be supported[failed %s].", ip_name);
		goto free;
	}

	ip->name = ip_name;
	ip->index = ip_index;

	list_add_tail(&ip->list, &idle_ip_list);
	spin_unlock_irqrestore(&idle_ip_list_lock, flags);

	exynos_update_ip_idle_status(ip->index, 0);

	return ip->index;
free:
	kfree(ip);
	return -1;
}

static void __init fix_idle_ip_init(void)
{
	struct device_node *dn = of_find_node_by_path("/cpupm/idle-ip");
	int i;
	const char *ip_name;

	fix_idle_ip_count = of_property_count_strings(dn, "fix-idle-ip");
	if (fix_idle_ip_count <= 0 || fix_idle_ip_count > FIX_IDLE_IP_MAX)
		return;

	for (i = 0; i < fix_idle_ip_count; i++) {
		of_property_read_string_index(dn, "fix-idle-ip", i, &ip_name);
		fix_idle_ip_arr[i].name = ip_name;
		fix_idle_ip_arr[i].reg_index = i;
	}
}

/******************************************************************************
 *                            CPU idle management                             *
 ******************************************************************************/
#define IS_NULL(object)		(object == NULL)

/*
 * State of CPUPM objects
 * All CPUPM objects have 2 states, RUN and POWERDOWN.
 *
 * @RUN
 * a state in which the power domain referred to by the object is turned on.
 *
 * @POWERDOWN
 * a state in which the power domain referred to by the object is turned off.
 * However, the power domain is not necessarily turned off even if the object
 * is in POEWRDOWN state because the cpu may be booting or executing power off
 * sequence.
 */
enum {
	CPUPM_STATE_RUN = 0,
	CPUPM_STATE_POWERDOWN,
};

/* Macros for CPUPM state */
#define set_state_run(object)		((object)->state = CPUPM_STATE_RUN)
#define set_state_powerdown(object)	((object)->state = CPUPM_STATE_POWERDOWN)
#define check_state_run(object)		((object)->state == CPUPM_STATE_RUN)
#define check_state_powerdown(object)	((object)->state == CPUPM_STATE_POWERDOWN)

/*
 * State of each cpu is managed by a structure declared by percpu, so there
 * is no need for protection for synchronization. However, when entering
 * the power mode, it is necessary to set the critical section to check the
 * state of cpus in the power domain, cpupm_lock is used for it.
 */
static spinlock_t cpupm_lock;

/* Nop function to awake cpus */
static void do_nothing(void *unused)
{
}

static void awake_cpus(const struct cpumask *cpus)
{
	int cpu;

	for_each_cpu_and(cpu, cpus, cpu_online_mask)
		smp_call_function_single(cpu, do_nothing, NULL, 1);
}

/*
 * disable_power_mode/enable_power_mode
 * It provides "disable" function to enable/disable power mode as required by
 * user or device driver. To handle multiple disable requests, it use the
 * atomic disable count, and disable the mode that contains the given cpu.
 */
void disable_power_mode(int cpu, int type)
{
	struct exynos_cpupm *pm;
	struct power_mode *mode;
	int i;

	spin_lock(&cpupm_lock);
	pm = &per_cpu(cpupm, cpu);

	for (i = 0; i < MAX_MODE; i++) {
		mode = pm->modes[i];

		if (IS_NULL(mode))
			break;

		/*
		 * There are no entry allowed cpus, it means that mode is
		 * disabled, skip awaking cpus.
		 */
		if (cpumask_empty(&mode->entry_allowed))
			continue;

		if (mode->type == type) {
			/*
			 * The first mode disable request wakes the cpus to
			 * exit power mode
			 */
			if (atomic_inc_return(&mode->disable) > 0) {
				spin_unlock(&cpupm_lock);
				awake_cpus(&mode->siblings);
				return;
			}
		}
	}
	spin_unlock(&cpupm_lock);
}

void enable_power_mode(int cpu, int type)
{
	struct exynos_cpupm *pm;
	struct power_mode *mode;
	int i;

	spin_lock(&cpupm_lock);
	pm = &per_cpu(cpupm, cpu);

	for (i = 0; i < MAX_MODE; i++) {
		mode = pm->modes[i];
		if (IS_NULL(mode))
			break;

		if (mode->type == type) {
			atomic_dec(&mode->disable);
			spin_unlock(&cpupm_lock);
			awake_cpus(&mode->siblings);
			return;
		}
	}
	spin_unlock(&cpupm_lock);
}

/* get sleep length of given cpu from tickless framework */
static s64 get_sleep_length(int cpu)
{
	return ktime_to_us(ktime_sub(*(get_next_event_cpu(cpu)), ktime_get()));
}

static int cpus_busy(int target_residency, const struct cpumask *cpus)
{
	int cpu, moce_ratio;

	/*
	 * If there is even one cpu which is not in POWERDOWN state or has
	 * the smaller sleep length than target_residency, CPUPM regards
	 * it as BUSY.
	 */
	for_each_cpu_and(cpu, cpu_online_mask, cpus) {
		struct exynos_cpupm *pm = &per_cpu(cpupm, cpu);

		if (check_state_run(pm))
			return -EBUSY;

		/*
		 * target residency for system-wide c-state (CPD/SICD) is
		 * re-evaluated with the ratio of moce
		 */
		moce_ratio = exynos_moce_get_ratio(cpu);

		if (get_sleep_length(cpu) < (target_residency * (s64)moce_ratio) / 100)
			return -EBUSY;
	}

	return 0;
}

static int cpus_last_core_detecting(int request_cpu, const struct cpumask *cpus)
{
	int cpu;

	for_each_cpu_and(cpu, cpu_online_mask, cpus) {
		if (cpu == request_cpu)
			continue;

		if (cal_is_lastcore_detecting(cpu))
			return -EBUSY;
	}

	return 0;
}

static int initcall_done;
static int system_busy(void)
{
	/* do not allow system idle util initialization time */
	if (!initcall_done)
		return 1;

	if (check_idle_ip())
		return 1;

	return 0;
}

/*
 * In order to enter the power mode, the following conditions must be met:
 * 1. power mode should not be disabled
 * 2. the cpu attempting to enter must be a cpu that is allowed to enter the
 *    power mode.
 * 3. all cpus in the power domain must be in POWERDOWN state and the sleep
 *    length of the cpus must be less than target_residency.
 */
static int can_enter_power_mode(int cpu, struct power_mode *mode)
{
	if (atomic_read(&mode->disable))
		return 0;

	if (!cpumask_test_cpu(cpu, &mode->entry_allowed))
		return 0;

	if (cpus_busy(mode->target_residency, &mode->siblings))
		return 0;

	if (is_IPI_pending(&mode->siblings))
		return 0;

	if (mode->system_idle && system_busy())
		return 0;

	if (cpus_last_core_detecting(cpu, &mode->siblings))
		return 0;

	return 1;
}

extern struct cpu_topology cpu_topology[NR_CPUS];

static int try_to_enter_power_mode(int cpu, struct power_mode *mode)
{
	/* Check whether mode can be entered */
	if (!can_enter_power_mode(cpu, mode)) {
		/* fail to enter power mode */
		return 0;
	}

	/*
	 * From this point on, it has succeeded in entering the power mode.
	 * It prepares to enter power mode, and makes the corresponding
	 * setting according to type of power mode.
	 */
	switch (mode->type) {
	case POWERMODE_TYPE_CLUSTER:
		cluster_disable(cpu);
		break;
	case POWERMODE_TYPE_SYSTEM:
		if (unlikely(exynos_system_idle_enter()))
			return 0;
		break;
	}

	dbg_snapshot_cpuidle(mode->name, 0, 0, DSS_FLAG_IN);
	set_state_powerdown(mode);

	cpuidle_profile_group_idle_enter(mode->id);

	return 1;
}

static void exit_mode(int cpu, struct power_mode *mode, int cancel)
{
	cpuidle_profile_group_idle_exit(mode->id, cancel);

	/*
	 * Configure settings to exit power mode. This is executed by the
	 * first cpu exiting from power mode.
	 */
	set_state_run(mode);
	dbg_snapshot_cpuidle(mode->name, 0, 0, DSS_FLAG_OUT);

	switch (mode->type) {
	case POWERMODE_TYPE_CLUSTER:
		cluster_enable(cpu);
		break;
	case POWERMODE_TYPE_SYSTEM:
		exynos_system_idle_exit(cancel);
		break;
	}
}

/*
 * Exynos cpuidle driver call exynos_cpu_pm_enter() and exynos_cpu_pm_exit()
 * to handle platform specific configuration to control cpu power domain.
 */
int exynos_cpu_pm_enter(int cpu, int index)
{
	struct exynos_cpupm *pm;
	struct power_mode *mode;
	int i;

	spin_lock(&cpupm_lock);
	pm = &per_cpu(cpupm, cpu);

	/* Configure PMUCAL to power down core */
	cpu_disable(cpu);

	/* Set cpu state to POWERDOWN */
	set_state_powerdown(pm);

	/* Try to enter power mode */
	for (i = 0; i < MAX_MODE; i++) {
		mode = pm->modes[i];
		if (IS_NULL(mode))
			break;

		if (try_to_enter_power_mode(cpu, mode))
			index |= mode->psci_index;
	}

	spin_unlock(&cpupm_lock);

	return index;
}

void exynos_cpu_pm_exit(int cpu, int cancel)
{
	struct exynos_cpupm *pm;
	struct power_mode *mode;
	int i;

	spin_lock(&cpupm_lock);
	pm = &per_cpu(cpupm, cpu);

	/* Make settings to exit from mode */
	for (i = 0; i < MAX_MODE; i++) {
		mode = pm->modes[i];
		if (IS_NULL(mode))
			break;

		if (check_state_powerdown(mode))
			exit_mode(cpu, mode, cancel);
	}

	/* Set cpu state to RUN */
	set_state_run(pm);

	/* Configure PMUCAL to power up core */
	cpu_enable(cpu);

	spin_unlock(&cpupm_lock);
}

static int __init exynos_cpupm_late_init(void)
{
	initcall_done = true;

	return 0;
}
late_initcall(exynos_cpupm_late_init);
#endif /* CONFIG_ARM64_EXYNOS_CPUIDLE */

/******************************************************************************
 *                               sysfs interface                              *
 ******************************************************************************/
static ssize_t show_power_mode(struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	struct power_mode *mode = container_of(attr, struct power_mode, attr);

	return snprintf(buf, PAGE_SIZE, "%s\n",
		atomic_read(&mode->disable) > 0 ? "disabled" : "enabled");
}

static ssize_t store_power_mode(struct kobject *kobj,
			struct kobj_attribute *attr, const char *buf,
			size_t count)
{
	unsigned int val;
	int cpu, type;
	struct power_mode *mode = container_of(attr, struct power_mode, attr);

	if (!sscanf(buf, "%u", &val))
		return -EINVAL;

	cpu = cpumask_any(&mode->siblings);
	type = mode->type;

	val = !!val;
	if (mode->user_request == val)
		return count;

	mode->user_request = val;
	if (val)
		enable_power_mode(cpu, type);
	else
		disable_power_mode(cpu, type);

	return count;
}

static ssize_t show_idle_ip_list(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int ret = 0;
#ifdef CONFIG_ARM64_EXYNOS_CPUIDLE
	struct idle_ip *ip;
	unsigned long flags;
	int i;

	for (i = 0; i < fix_idle_ip_count; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "[fix:%d] %s\n",
				fix_idle_ip_arr[i].reg_index,
				fix_idle_ip_arr[i].name);

	spin_lock_irqsave(&idle_ip_list_lock, flags);

	list_for_each_entry(ip, &idle_ip_list, list)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, "[%d] %s\n",
				ip->index, ip->name);

	spin_unlock_irqrestore(&idle_ip_list_lock, flags);
#endif

	return ret;
}

/*
 * attr_pool is used to create sysfs node at initialization time. Saving the
 * initiailized attr to attr_pool, and it creates nodes of each attr at the
 * time of sysfs creation. 10 is the appropriate value for the size of
 * attr_pool.
 */
static struct attribute *attr_pool[10];

static struct kobj_attribute idle_ip_attr;
static struct kobject *cpupm_kobj;
static struct attribute_group attr_group;

static void cpupm_sysfs_node_init(int attr_count)
{
	attr_group.attrs = kcalloc(attr_count + 1,
			sizeof(struct attribute *), GFP_KERNEL);
	if (!attr_group.attrs)
		return;

	memcpy(attr_group.attrs, attr_pool,
		sizeof(struct attribute *) * attr_count);

	cpupm_kobj = kobject_create_and_add("cpupm", power_kobj);
	if (!cpupm_kobj)
		goto out;

	if (sysfs_create_group(cpupm_kobj, &attr_group))
		goto out;

	return;

out:
	kfree(attr_group.attrs);
}

#define cpupm_attr_init(_attr, _name, _mode, _show, _store)	\
	sysfs_attr_init(&_attr.attr);				\
	_attr.attr.name	= _name;				\
	_attr.attr.mode	= VERIFY_OCTAL_PERMISSIONS(_mode);	\
	_attr.show	= _show;				\
	_attr.store	= _store;


/******************************************************************************
 *                                Initialization                              *
 ******************************************************************************/
static void __init
add_mode(struct power_mode **modes, struct power_mode *new)
{
	struct power_mode *mode;
	int i;

	for (i = 0; i < MAX_MODE; i++) {
		mode = modes[i];
		if (mode == NULL) {
			modes[i] = new;
			return;
		}
	}

	pr_warn("The number of modes exceeded\n");
}

static void __init virtual_cluster_init(void)
{
	struct device_node *dn = of_find_node_by_path("/cpupm/vcpu_topology");

	if (dn) {
		int i, cluster_cnt = 0;

		pr_info("vcluster: Virtual Cluster Info\n");

		of_property_read_u32(dn, "vcluster_cnt", &cluster_cnt);
		for (i = 0 ; i < cluster_cnt ; i++) {
			int cpu;
			char name[20];
			const char *buf;
			struct cpumask sibling;

			snprintf(name, sizeof(name), "vcluster%d_sibling", i);
			if (!of_property_read_string(dn, name, &buf)) {
				cpulist_parse(buf, &sibling);
				cpumask_and(&sibling, &sibling, cpu_possible_mask);

				if (cpumask_empty(&sibling))
					continue;

				for_each_cpu(cpu, &sibling)
					per_cpu(vcluster_id, cpu) = i;

				pr_info("\tCluster%d : CPU%*pbl\n", i, cpumask_pr_args(&sibling));
			}
		}
	} else {
		pr_info("vcluster: No Virtual Cluster Info\n");
	}
}

static int __init cpu_power_mode_init(void)
{
	struct device_node *dn = NULL;
	struct power_mode *mode;
	const char *buf;
	int id = 0, attr_count = 0;

	while ((dn = of_find_node_by_type(dn, "cpupm"))) {
		int cpu;

		/*
		 * Power mode is dynamically generated according to what is defined
		 * in device tree.
		 */
		mode = kzalloc(sizeof(struct power_mode), GFP_KERNEL);
		if (!mode) {
			pr_warn("%s: No memory space!\n", __func__);
			continue;
		}

		mode->id = id++;
		strncpy(mode->name, dn->name, NAME_LEN - 1);

		of_property_read_u32(dn, "target-residency", &mode->target_residency);
		of_property_read_u32(dn, "psci-index", &mode->psci_index);
		of_property_read_u32(dn, "type", &mode->type);

		if (of_property_read_bool(dn, "system-idle"))
			mode->system_idle = true;

		if (!of_property_read_string(dn, "siblings", &buf)) {
			cpulist_parse(buf, &mode->siblings);
			cpumask_and(&mode->siblings, &mode->siblings, cpu_possible_mask);
		}

		if (!of_property_read_string(dn, "entry-allowed", &buf)) {
			cpulist_parse(buf, &mode->entry_allowed);
			cpumask_and(&mode->entry_allowed, &mode->entry_allowed, cpu_possible_mask);
		}

		atomic_set(&mode->disable, 0);

		/*
		 * The users' request is set to enable since initialization state of
		 * power mode is enabled.
		 */
		mode->user_request = true;

		/*
		 * Initialize attribute for sysfs.
		 * The absence of entry allowed cpu is equivalent to this power
		 * mode being disabled. In this case, no attribute is created.
		 */
		if (!cpumask_empty(&mode->entry_allowed)) {
			cpupm_attr_init(mode->attr, mode->name, 0644,
					show_power_mode, store_power_mode);
			attr_pool[attr_count++] = &mode->attr.attr;
		}

		/* Connect power mode to the cpus in the power domain */
		for_each_cpu(cpu, &mode->siblings)
			add_mode(per_cpu(cpupm, cpu).modes, mode);

		cpuidle_profile_group_idle_register(mode->id, mode->name);
	}

	cpupm_attr_init(idle_ip_attr, "idle_ip_list", 0444,
			show_idle_ip_list, NULL);
	attr_pool[attr_count++] = &idle_ip_attr.attr;

	if (attr_count)
		cpupm_sysfs_node_init(attr_count);

	virtual_cluster_init();

	return 0;
}

static int __init exynos_cpupm_init(void)
{
	cpu_power_mode_init();

#ifdef CONFIG_ARM64_EXYNOS_CPUIDLE
	spin_lock_init(&cpupm_lock);
	fix_idle_ip_init();
#endif

	cpupm_initialized = 1;

	return 0;
}
arch_initcall(exynos_cpupm_init);

/******************************************************************************
 *                               CPU HOTPLUG                                  *
 ******************************************************************************/
static int cluster_sibling_weight(unsigned int cpu)
{
	struct exynos_cpupm *pm = &per_cpu(cpupm, cpu);
	struct power_mode *mode;
	struct cpumask mask;
	int pos;

	if (IS_NULL(pm->modes[0])) {
		pr_debug("ERROR: cpupm's node must be declared.\n");
		BUG_ON(1);
	}

	cpumask_clear(&mask);
	for (pos = 0; pos < MAX_MODE; pos++) {
		mode = pm->modes[pos];

		if (mode->type == POWERMODE_TYPE_CLUSTER) {
			if (cpumask_empty(&mode->entry_allowed))
				return -1;

			cpumask_and(&mask, &mode->siblings, cpu_online_mask);
			break;
		}
	}

	return cpumask_weight(&mask);
}

bool exynos_cpuhp_last_cpu(unsigned int cpu)
{
	return cluster_sibling_weight(cpu) == 0;
}

static int cpuhp_cpupm_online(unsigned int cpu)
{
	/*
	 * When CPU become online at very early booting time,
	 * CPUPM is not initiailized yet. Only in this case,
	 * allow duplicate calls to cluster_enable().
	 */
	if (!cpupm_initialized || cluster_sibling_weight(cpu) == 0)
		cluster_enable(cpu);

	cpu_enable(cpu);

	return 0;
}

static int cpuhp_cpupm_offline(unsigned int cpu)
{
	cpu_disable(cpu);

	if (cluster_sibling_weight(cpu) == 0)
		cluster_disable(cpu);

	return 0;
}

static int cpuhp_cpupm_enable_idle(unsigned int cpu)
{
	struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);

	cpuidle_enable_device(dev);

	return 0;
}
static int cpuhp_cpupm_disable_idle(unsigned int cpu)
{
	struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);

	cpuidle_disable_device(dev);

	return 0;
}

static int __init exynos_cpupm_early_init(void)
{
	cpuhp_setup_state(CPUHP_AP_EXYNOS_IDLE_CTRL,
				"AP_EXYNOS_IDLE_CTROL",
				cpuhp_cpupm_enable_idle,
				cpuhp_cpupm_disable_idle);

	cpuhp_setup_state(CPUHP_AP_EXYNOS_CPU_UP_POWER_CONTROL,
				"AP_EXYNOS_CPU_UP_POWER_CONTROL",
				cpuhp_cpupm_online, NULL);
	cpuhp_setup_state(CPUHP_AP_EXYNOS_CPU_DOWN_POWER_CONTROL,
				"AP_EXYNOS_CPU_DOWN_POWER_CONTROL",
				NULL, cpuhp_cpupm_offline);

	return 0;
}
early_initcall(exynos_cpupm_early_init);
