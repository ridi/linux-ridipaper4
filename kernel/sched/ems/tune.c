/*
 * Frequency variant cpufreq driver
 *
 * Copyright (C) 2017 Samsung Electronics Co., Ltd
 * Park Bumgyu <bumgyu.park@samsung.com>
 */

#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/reciprocal_div.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#include <trace/events/ems.h>

#include "../sched.h"
#include "../tune.h"
#include "ems.h"

struct emst_dom {
	int		boost_ratio;	/* boost_ratio of this group at the current freq */
	int		weight;		/* weight of this group for core selction */
	int		idle_weight;	/* idle_weight of this group for core selection */
	int		freq_boost;	/* freq_boost of this group at the current freq */
};

struct emst_group {
	struct cpumask	candidate_cpus; /* candidate_cpus of this group for core selection */

	struct emst_dom	*dom[NR_CPUS];
	struct kobject	kobj;
};

struct emst_mode {
	int idx;
	const char *desc;

	struct emst_group emst_grp[STUNE_GROUP_COUNT];
	struct kobject	  kobj;
};
static struct emst_mode *emst_modes;

bool emst_init_f;
static int cur_mode;
static int max_mode;

char *stune_group_name[] = {
	"root",
	"foreground",
	"background",
	"top-app",
	"rt",
};

static struct kobject *emst_kobj;
DEFINE_PER_CPU(int, emst_freq_boost);	/* maximum boot ratio of cpu */

/**********************************************************************
 * common APIs                                                        *
 **********************************************************************/
extern struct reciprocal_value schedtune_spc_rdiv;
unsigned long emst_boost(int cpu, unsigned long util)
{
	int boost = per_cpu(emst_freq_boost, cpu);
	unsigned long capacity = capacity_cpu(cpu, 0);
	unsigned long boosted_util = 0;
	long long margin = 0;

	if (!boost || util >= capacity)
		return util;

	/*
	 * Signal proportional compensation (SPC)
	 *
	 * The Boost (B) value is used to compute a Margin (M) which is
	 * proportional to the complement of the original Signal (S):
	 *   M = B * (capacity - S)
	 * The obtained M could be used by the caller to "boost" S.
	 */
	if (boost >= 0) {
		margin  = capacity - util;
		margin *= boost;
	} else
		margin = -util * boost;

	margin  = reciprocal_divide(margin, schedtune_spc_rdiv);

	if (boost < 0)
		margin *= -1;

	boosted_util = util + margin;

	trace_emst_boost(cpu, boost, util, boosted_util);

	return boosted_util;
}

/* Update maximum values of boost groups of this cpu */
void emst_cpu_update(int cpu, u64 now)
{
	int idx, freq_boost_max = 0;
	struct emst_group *emst_grp;

	if (unlikely(!emst_init_f))
		return;

	emst_grp = emst_modes[cur_mode].emst_grp;

	/* The root boost group is always active */
	freq_boost_max = emst_grp[0].dom[cpu]->freq_boost;
	for (idx = 1; idx < STUNE_GROUP_COUNT; ++idx) {
		int val;
		if (schedtune_cpu_boost_group_active(idx, cpu, now))
			continue;

		val = emst_grp[idx].dom[cpu]->freq_boost;
		if (freq_boost_max < val)
			freq_boost_max = val;
	}

	/*
	 * Ensures grp_boost_max is non-negative when all cgroup boost values
	 * are neagtive. Avoids under-accounting of cpu capacity which may cause
	 * task stacking and frequency spikes.
	 */
	per_cpu(emst_freq_boost, cpu) = max(freq_boost_max, 0);
}

#define DEFAULT_WEIGHT	(100)
int emst_get_weight(struct task_struct *p, int cpu, int idle)
{
	int st_idx, weight;
	struct emst_group *groups;
	struct emst_dom *dom;

	if (unlikely(!emst_init_f))
		return DEFAULT_WEIGHT;

	st_idx = schedtune_task_group_idx(p);
	groups = emst_modes[cur_mode].emst_grp;
	dom = groups[st_idx].dom[cpu];

	if (idle)
		weight = dom->idle_weight;
	else
		weight = dom->weight;

	return weight;
}

const struct cpumask *emst_get_candidate_cpus(struct task_struct *p)
{
	int st_idx;
	struct emst_group *group;

	if (unlikely(!emst_init_f))
		return cpu_active_mask;

	st_idx = schedtune_task_group_idx(p);
	group = &emst_modes[cur_mode].emst_grp[st_idx];

	if (unlikely(cpumask_empty(&group->candidate_cpus)))
		return cpu_active_mask;

	return &group->candidate_cpus;
}

bool emst_can_migrate_task(struct task_struct *p, int dst_cpu)
{
	return cpumask_test_cpu(dst_cpu, emst_get_candidate_cpus(p));
}

/**********************************************************************
 * Multi requests interface (Platform/Kernel)                         *
 **********************************************************************/
struct emst_mode_object {
	char *name;
	struct miscdevice emst_mode_miscdev;
};
static struct emst_mode_object emst_mode_obj;

static DEFINE_SPINLOCK(emst_mode_lock);
static struct plist_head emst_req_list = PLIST_HEAD_INIT(emst_req_list);

static int emst_get_mode(void)
{
	if (plist_head_empty(&emst_req_list))
		return 0;

	return plist_last(&emst_req_list)->prio;
}

void __emst_update_request(struct emst_mode_request *req, s32 new_value,
			char *func, unsigned int line)
{
	unsigned long flags;
	int next_mode;

	/* ignore if the request is active and the value does not change */
	if (req->active && req->node.prio == new_value)
		return;

	/* ignore if the value is out of range */
	if (new_value < 0 || new_value > (s32)max_mode)
		return;

	spin_lock_irqsave(&emst_mode_lock, flags);

	/*
	 * If the request already added to the list updates the value, remove
	 * the request from the list and add it again.
	 */
	if (req->active)
		plist_del(&req->node, &emst_req_list);
	else {
		req->func = func;
		req->line = line;
		req->active = 1;
	}

	plist_node_init(&req->node, new_value);
	plist_add(&req->node, &emst_req_list);

	next_mode = emst_get_mode();
	if (cur_mode != next_mode)
		cur_mode = next_mode;

	spin_unlock_irqrestore(&emst_mode_lock, flags);
}

static int emst_mode_open(struct inode *inode, struct file *filp)
{
	struct emst_mode_request *req = kzalloc(sizeof(struct emst_mode_request), GFP_KERNEL);

	if (!req)
		return -ENOMEM;

	filp->private_data = req;

	return 0;
}

static int emst_mode_release(struct inode *inode, struct file *filp)
{
	struct emst_mode_request *req;

	req = filp->private_data;
	if (req->active)
		plist_del(&req->node, &emst_req_list);
	kfree(req);

	return 0;
}

static ssize_t emst_mode_read(struct file *filp, char __user *buf,
		size_t count, loff_t *f_pos)
{
	s32 value;
	unsigned long flags;

	spin_lock_irqsave(&emst_mode_lock, flags);
	value = emst_get_mode();
	spin_unlock_irqrestore(&emst_mode_lock, flags);

	return simple_read_from_buffer(buf, count, f_pos, &value, sizeof(s32));
}

static ssize_t emst_mode_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	s32 value;
	struct emst_mode_request *req;

	if (count == sizeof(s32)) {
		if (copy_from_user(&value, buf, sizeof(s32)))
			return -EFAULT;
	} else {
		int ret;

		ret = kstrtos32_from_user(buf, count, 16, &value);
		if (ret)
			return ret;
	}

	req = filp->private_data;
	emst_update_request(req, value);

	return count;
}

static const struct file_operations emst_mode_fops = {
	.write = emst_mode_write,
	.read = emst_mode_read,
	.open = emst_mode_open,
	.release = emst_mode_release,
	.llseek = noop_llseek,
};

static int register_emst_mode_misc(void)
{
	int ret;

	emst_mode_obj.emst_mode_miscdev.minor = MISC_DYNAMIC_MINOR;
	emst_mode_obj.emst_mode_miscdev.name = "mode";
	emst_mode_obj.emst_mode_miscdev.fops = &emst_mode_fops;

	ret = misc_register(&emst_mode_obj.emst_mode_miscdev);

	return ret;
}

/**********************************************************************
 * Sysfs	                                                      *
 **********************************************************************/
struct emst_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *, char *);
	ssize_t (*store)(struct kobject *, const char *, size_t count);
};

#define emst_attr_rw(name)				\
static struct emst_attr name##_attr =			\
__ATTR(name, 0644, show_##name, store_##name)

#define emst_show(name, type)							\
static ssize_t show_##name(struct kobject *k, char *buf)			\
{										\
	struct emst_group *group = container_of(k, struct emst_group, kobj);	\
	int cpu, ret = 0;							\
										\
	for_each_possible_cpu(cpu) {						\
		if (cpu != cpumask_first(cpu_coregroup_mask(cpu)))		\
			continue;						\
		ret += sprintf(buf + ret, "cpu:%d ratio%3d\n",			\
					cpu, group->dom[cpu]->type);		\
	}									\
										\
	return ret;								\
}

#define emst_store(name, type)						\
static ssize_t store_##name(struct kobject *k, const char *buf, size_t count)	\
{										\
	struct emst_group *group = container_of(k, struct emst_group, kobj);	\
	int cpu, val, tmp;							\
										\
	if (sscanf(buf, "%d %d", &cpu, &val) != 2)				\
		return -EINVAL;							\
										\
	if (cpu < 0 || cpu >= NR_CPUS || val < -200 || val > 500)		\
		return -EINVAL;							\
										\
	for_each_cpu(tmp, cpu_coregroup_mask(cpu))				\
		group->dom[tmp]->type = val;					\
	return count;								\
}

#define emst_show_weight(name, type)						\
static ssize_t show_##name(struct kobject *k, char *buf)			\
{										\
	struct emst_group *group = container_of(k, struct emst_group, kobj);	\
	int cpu, ret = 0;							\
										\
	for_each_possible_cpu(cpu) {						\
		ret += sprintf(buf + ret, "cpu:%d weight:%3d\n",		\
					cpu, group->dom[cpu]->type);		\
	}									\
										\
	return ret;								\
}

#define emst_store_weight(name, type)						\
static ssize_t store_##name(struct kobject *k, const char *buf, size_t count)	\
{										\
	struct emst_group *group = container_of(k, struct emst_group, kobj);	\
	int cpu, val;								\
										\
	if (sscanf(buf, "%d %d", &cpu, &val) != 2)				\
		return -EINVAL;							\
										\
	if (cpu < 0 || cpu >= NR_CPUS || val < 0)				\
		return -EINVAL;							\
										\
	group->dom[cpu]->type = val;						\
	return count;								\
}

emst_store(freq_boost, freq_boost);
emst_show(freq_boost, freq_boost);
emst_attr_rw(freq_boost);

emst_store_weight(weight, weight);
emst_show_weight(weight, weight);
emst_attr_rw(weight);

emst_store_weight(idle_weight, idle_weight);
emst_show_weight(idle_weight, idle_weight);
emst_attr_rw(idle_weight);

#define STR_LEN 10
static ssize_t
store_candidate_cpus(struct kobject *k, const char *buf, size_t count)
{
	char str[STR_LEN];
	int i;
	struct cpumask candidate_cpus;
	struct emst_group *group = container_of(k, struct emst_group, kobj);

	if (strlen(buf) >= STR_LEN)
		return -EINVAL;

	if (!sscanf(buf, "%s", str))
		return -EINVAL;

	if (str[0] == '0' && str[1] == 'x') {
		for (i = 0; i+2 < STR_LEN; i++) {
			str[i] = str[i + 2];
			str[i+2] = '\n';
		}
	}

	cpumask_parse(str, &candidate_cpus);
	cpumask_copy(&group->candidate_cpus, &candidate_cpus);

	return count;
}

static ssize_t
show_candidate_cpus(struct kobject *k, char *buf)
{
	unsigned int candidate_cpus;
	struct emst_group *group = container_of(k, struct emst_group, kobj);

	candidate_cpus = *(unsigned int *)cpumask_bits(&group->candidate_cpus);

	return snprintf(buf, 30, "candidate cpu : 0x%x\n", candidate_cpus);
}

emst_attr_rw(candidate_cpus);

static ssize_t show(struct kobject *kobj, struct attribute *at, char *buf)
{
	struct emst_attr *attr = container_of(at, struct emst_attr, attr);
	return attr->show(kobj, buf);
}

static ssize_t store(struct kobject *kobj, struct attribute *at,
					const char *buf, size_t count)
{
	struct emst_attr *attr = container_of(at, struct emst_attr, attr);
	return attr->store(kobj, buf, count);
}

static const struct sysfs_ops emst_sysfs_ops = {
	.show	= show,
	.store	= store,
};

static struct attribute *emst_grp_attrs[] = {
	&freq_boost_attr.attr,
	&weight_attr.attr,
	&idle_weight_attr.attr,
	&candidate_cpus_attr.attr,
	NULL
};

static struct kobj_type ktype_emst_grp = {
	.sysfs_ops	= &emst_sysfs_ops,
	.default_attrs	= emst_grp_attrs,
};

static ssize_t
show_mode_idx(struct kobject *k, char *buf)
{
	struct emst_mode *mode = container_of(k, struct emst_mode, kobj);
	int ret;

	ret = sprintf(buf, "%d\n", mode->idx);

	return ret;
}

static struct emst_attr mode_idx_attr =
__ATTR(idx, 0444, show_mode_idx, NULL);

static ssize_t
show_mode_desc(struct kobject *k, char *buf)
{
	struct emst_mode *mode = container_of(k, struct emst_mode, kobj);
	int ret;

	ret = sprintf(buf, "%s\n", mode->desc);

	return ret;
}

static struct emst_attr mode_desc_attr =
__ATTR(desc, 0444, show_mode_desc, NULL);

static struct attribute *emst_mode_attrs[] = {
	&mode_idx_attr.attr,
	&mode_desc_attr.attr,
	NULL
};

static struct kobj_type ktype_emst_mode = {
	.sysfs_ops	= &emst_sysfs_ops,
	.default_attrs	= emst_mode_attrs,
};

static struct emst_mode_request emst_user_req;
static ssize_t
show_user_mode(struct kobject *k, struct kobj_attribute *attr, char *buf)
{
	int ret;

	if (!emst_user_req.active)
		ret = sprintf(buf, "user request has never come\n");
	else
		ret = sprintf(buf, "req_mode:%d (%s:%d)\n",
					(emst_user_req.node).prio,
					emst_user_req.func,
					emst_user_req.line);

	return ret;
}

static ssize_t
store_user_mode(struct kobject *k, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int mode;

	if (sscanf(buf, "%d", &mode) != 1)
		return -EINVAL;

	/* ignore if requested mode is out of range */
	if (mode < 0 || mode > max_mode)
		return -EINVAL;

	emst_update_request(&emst_user_req, mode);

	return count;
}

static struct kobj_attribute user_mode_attr =
__ATTR(user_mode, 0644, show_user_mode, store_user_mode);

static ssize_t
show_req_modes(struct kobject *k, struct kobj_attribute *attr, char *buf)
{
	struct emst_mode_request *req;
	int ret = 0;
	int tot_reqs = 0;

	plist_for_each_entry(req, &emst_req_list, node) {
		tot_reqs++;
		ret += sprintf(buf + ret, "%d: %d (%s:%d)\n", tot_reqs,
							(req->node).prio,
							req->func,
							req->line);
	}

	ret += sprintf(buf + ret, "Current mode: %d, Requests: total=%d\n",
							emst_get_mode(), tot_reqs);

	return ret;
}
static struct kobj_attribute req_modes_attr =
__ATTR(req_modes, 0444, show_req_modes, NULL);

/**********************************************************************
 * initialization                                                     *
 **********************************************************************/
static get_coregroup_count(void)
{
	int cnt = 0, cpu;

	for_each_possible_cpu(cpu) {
		if (cpu != cpumask_first(cpu_coregroup_mask(cpu)))
			continue;

		cnt++;
	}

	return cnt;
}

#define parse_member(member)								\
static int										\
emst_parse_##member(struct device_node *dn, struct emst_dom **dom,			\
					int coregrp_cnt, char *grp_name)		\
{											\
	int cnt, cpu, tmp;								\
	u32 val[NR_CPUS];								\
											\
	if (of_property_read_u32_array(dn, grp_name, (u32 *)&val, coregrp_cnt)) {	\
		pr_warn("%s: failed to get %s of %s\n",					\
					__func__, dn->name, grp_name);			\
		return -EINVAL;								\
	}										\
											\
	cnt = 0;									\
	for_each_possible_cpu(cpu) {							\
		if (cpu != cpumask_first(cpu_coregroup_mask(cpu)))			\
			continue;							\
											\
		for_each_cpu(tmp, cpu_coregroup_mask(cpu))				\
			dom[tmp]->member = val[cnt];					\
											\
		cnt++;									\
	}										\
											\
	return 0;									\
}

static int
emst_parse_candidate_cpus(struct device_node *dn, struct emst_group *grp, char *grp_name)
{
	const char *val;
	struct cpumask mask;

	if (of_property_read_string(dn, grp_name, &val)) {
		pr_warn("%s: failed to get %s of %s\n",
					__func__, dn->name, grp_name);
		return -EINVAL;
	}

	cpulist_parse(val, &mask);
	cpumask_copy(&grp->candidate_cpus, &mask);

	return 0;
}

parse_member(weight);
parse_member(idle_weight);
parse_member(freq_boost);

static int emst_parse_dt(struct device_node *mode_dn, struct emst_group *emst_grp, int mode_idx)
{
	int idx;
	struct device_node *dn;
	int coregrp_cnt;

	/* Get index of this mode */
	if (of_property_read_u32(mode_dn, "idx", &emst_modes[mode_idx].idx)) {
		pr_warn("%s: no idx member in this mode\n", __func__);
		return -EINVAL;
	}

	/* Get description of this mode */
	if (of_property_read_string(mode_dn, "desc", &emst_modes[mode_idx].desc)) {
		pr_warn("%s: no desc member in this mode\n", __func__);
		return -EINVAL;
	}

	coregrp_cnt = get_coregroup_count();

	for (idx = 0; idx < STUNE_GROUP_COUNT; idx++) {
		char grp_name[16];
		int cpu;
		struct emst_dom **dom = emst_grp[idx].dom;

		/* Get the stune group name */
		snprintf(grp_name, sizeof(grp_name), "%s", stune_group_name[idx]);

		/* Allocate emst_dom */
		for_each_possible_cpu(cpu) {
			struct emst_dom *cur = kzalloc(sizeof(struct emst_dom), GFP_KERNEL);
			if (!cur) {
				pr_warn("%s: failed to allocate emst_dom\n", __func__);
				continue;
			}

			dom[cpu] = cur;
		}

		/* parsing candidate cpus for this cgroup */
		dn = of_find_node_by_name(mode_dn, "candidate-cpus");
		if (!dn) {
			pr_warn("%s: no candidate cpus node in this mode\n", __func__);
			return -EINVAL;
		}
		emst_parse_candidate_cpus(dn, &emst_grp[idx], grp_name);

		/* parsing weight to calculate efficiency */
		dn = of_find_node_by_name(mode_dn, "weight");
		if (!dn) {
			pr_warn("%s: no weight node in this mode\n", __func__);
			return -EINVAL;
		}
		emst_parse_weight(dn, dom, coregrp_cnt, grp_name);

		/* parsing idle weight to calculate efficiency */
		dn = of_find_node_by_name(mode_dn, "idle-weight");
		if (!dn) {
			pr_warn("%s: no idle-weight node in this mode\n", __func__);
			return -EINVAL;
		}
		emst_parse_idle_weight(dn, dom, coregrp_cnt, grp_name);

		/* parsing freq boost to speed up frequency ramp-up */
		dn = of_find_node_by_name(mode_dn, "freq-boost");
		if (!dn) {
			pr_warn("%s: no freq-boost node in this mode\n", __func__);
			return -EINVAL;
		}
		emst_parse_freq_boost(dn, dom, coregrp_cnt, grp_name);
	}

	return 0;
}

static int __init emst_init(void)
{
	struct device_node *emst_dn, *mode_dn;
	int child_count, mode_idx;

	emst_dn = of_find_node_by_path("/ems/ems-tune");
	if (!emst_dn)
		return -EINVAL;

	child_count = of_get_child_count(emst_dn);
	if (!child_count)
		return -EINVAL;

	emst_modes = kzalloc(sizeof(struct emst_mode) * child_count, GFP_KERNEL);
	if (!emst_modes)
		return -ENOMEM;

	emst_kobj = kobject_create_and_add("emst", ems_kobj);
	if (!emst_kobj)
		return -EINVAL;

	if (sysfs_create_file(emst_kobj, &user_mode_attr.attr))
		return -ENOMEM;

	if (sysfs_create_file(emst_kobj, &req_modes_attr.attr))
		return -ENOMEM;

	mode_idx = 0;
	for_each_child_of_node(emst_dn, mode_dn) {
		int idx;
		struct kobject *mode_kobj = &emst_modes[mode_idx].kobj;
		struct emst_group *emst_grp = emst_modes[mode_idx].emst_grp;

		/* Connect kobject for this mode */
		if (kobject_init_and_add(mode_kobj, &ktype_emst_mode, emst_kobj, mode_dn->name))
			return -EINVAL;

		for (idx = 0; idx < STUNE_GROUP_COUNT; idx++) {
			char grp_name[16];
			struct kobject *grp_kobj = &emst_grp[idx].kobj;

			snprintf(grp_name, sizeof(grp_name), "%s", stune_group_name[idx]);
			if (kobject_init_and_add(grp_kobj, &ktype_emst_grp, mode_kobj, grp_name))
				pr_warn("%s: failed to initialize kobject of %s\n", __func__, grp_name);
		}

		if (emst_parse_dt(mode_dn, emst_modes[mode_idx].emst_grp, mode_idx)) {
			/* not allow failure to parse emst dt */
			BUG();
		}

		mode_idx++;
	}

	register_emst_mode_misc();
	max_mode = mode_idx - 1;
	emst_init_f = true;

	return 0;
}
late_initcall(emst_init);
