/*
 * Copyright (c) 2017 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/plist.h>
#include <linux/sched/idle.h>
#include <linux/sched/topology.h>

struct gb_qos_request {
	struct plist_node node;
	char *name;
	bool active;
};

struct emst_mode_request {
	struct plist_node node;
	bool active;
	char *func;
	unsigned int line;
};

#define emst_update_request(req, new_value)	do {				\
	__emst_update_request(req, new_value, (char *)__func__, __LINE__);	\
} while(0);

struct rq;

enum {
	STATES_FREQ = 0,
	STATES_PMQOS,
	NUM_OF_REQUESTS,
};

#ifdef CONFIG_SCHED_EMS
/*
 * core
 */
extern int
exynos_select_task_rq(struct task_struct *p, int prev_cpu, int sd_flag, int sync, int wakeup);
extern void init_ems(void);


/*
 * init util
 */
extern void post_init_entity_multi_load(struct sched_entity *se, u64 now);


/*
 * energy model
 */
extern void init_sched_energy_table(struct cpumask *cpus, int table_size,
				unsigned long *f_table, unsigned int *v_table,
				int max_f, int min_f);
extern void rebuild_sched_energy_table(struct cpumask *cpus, int clipped_freq,
						int max_freq, int type);


/*
 * multi load
 */
extern unsigned long ml_boosted_cpu_util(int cpu);
extern void init_multi_load(struct sched_entity *se);

extern void set_task_rq_multi_load(struct sched_entity *se, struct cfs_rq *prev, struct cfs_rq *next);
extern void update_tg_cfs_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se, struct cfs_rq *gcfs_rq);
extern int update_cfs_rq_multi_load(u64 now, struct cfs_rq *cfs_rq);
extern void attach_entity_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se);
extern void detach_entity_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se);
extern int update_multi_load_se(u64 now, struct cfs_rq *cfs_rq, struct sched_entity *se);
extern void sync_entity_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se);
extern void remove_entity_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se);
extern void init_cfs_rq_multi_load(struct cfs_rq *cfs_rq);
extern void migrate_entity_multi_load(struct sched_entity *se);

extern void util_est_enqueue_multi_load(struct cfs_rq *cfs_rq, struct task_struct *p);
extern void util_est_dequeue_multi_load(struct cfs_rq *cfs_rq, struct task_struct *p, bool task_sleep);
extern void util_est_update(struct task_struct *p, int prev_util_est, int next_util_est);
extern void set_part_period_start(struct rq *rq);
extern void update_cpu_active_ratio(struct rq *rq, struct task_struct *p, int type);
extern void part_cpu_active_ratio(unsigned long *util, unsigned long *max, int cpu);


/*
 * ontime migration
 */
extern int ontime_can_migrate_task(struct task_struct *p, int dst_cpu);
extern void ontime_migration(void);


/*
 * global boost
 */
extern void gb_qos_update_request(struct gb_qos_request *req, u32 new_value);


/*
 * load balance
 */
extern struct list_head *lb_cfs_tasks(struct rq *rq, int sse);
extern void lb_add_cfs_task(struct rq *rq, struct sched_entity *se);
extern int lb_check_priority(int src_cpu, int dst_cpu);
extern struct list_head *lb_prefer_cfs_tasks(int src_cpu, int dst_cpu);
extern int lb_need_active_balance(enum cpu_idle_type idle,
				struct sched_domain *sd, int src_cpu, int dst_cpu);
extern bool lb_sibling_overutilized(int dst_cpu, struct sched_domain *sd,
					struct cpumask *lb_cpus);
extern bool lbt_overutilized(int cpu, int level);
extern void update_lbt_overutil(int cpu, unsigned long capacity);
extern void lb_update_misfit_status(struct task_struct *p, struct rq *rq, unsigned long task_h_load);

/*
 * Core sparing
 */
extern void ecs_update(void);
extern int ecs_is_sparing_cpu(int cpu);

/*
 * EMStune
 */
extern void __emst_update_request(struct emst_mode_request *req, s32 new_value, char *func, unsigned int line);
extern bool emst_can_migrate_task(struct task_struct *p, int dst_cpu);
#else /* CONFIG_SCHED_EMS */

/*
 * core
 */
static inline int
exynos_select_task_rq(struct task_struct *p, int prev_cpu, int sd_flag, int sync, int wakeup)
{
	return -1;
}
static inline void init_ems(void) { }


/*
 * init util
 */
static inline void post_init_entity_multi_load(struct sched_entity *se, u64 now) { }


/*
 * energy model
 */
static inline void init_sched_energy_table(struct cpumask *cpus, int table_size,
				unsigned long *f_table, unsigned int *v_table,
				int max_f, int min_f) { }
static inline void rebuild_sched_energy_table(struct cpumask *cpus, int clipped_freq,
						int max_freq, int type) { }

/*
 * multi load
 */
static inline unsigned long ml_boosted_cpu_util(int cpu) { return 0; }
static inline void init_multi_load(struct sched_entity *se) { }

static inline void set_task_rq_multi_load(struct sched_entity *se, struct cfs_rq *prev, struct cfs_rq *next) { }
static inline void update_tg_cfs_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se, struct cfs_rq *gcfs_rq) { }
static inline int update_cfs_rq_multi_load(u64 now, struct cfs_rq *cfs_rq) { return 0; }
static inline void attach_entity_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se) { }
static inline void detach_entity_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se) { }
static inline int update_multi_load_se(u64 now, struct cfs_rq *cfs_rq, struct sched_entity *se) { return 0; }
static inline void sync_entity_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se) { }
static inline void remove_entity_multi_load(struct cfs_rq *cfs_rq, struct sched_entity *se) { }
static inline void init_cfs_rq_multi_load(struct cfs_rq *cfs_rq) { }
static inline void migrate_entity_multi_load(struct sched_entity *se) { }

static inline void util_est_enqueue_multi_load(struct cfs_rq *cfs_rq, struct task_struct *p) { }
static inline void util_est_dequeue_multi_load(struct cfs_rq *cfs_rq, struct task_struct *p, bool task_sleep) { }
static inline void util_est_update(struct task_struct *p, int prev_util_est, int next_util_est) { }
static inline void set_part_period_start(struct rq *rq) { }
static inline void update_cpu_active_ratio(struct rq *rq, struct task_struct *p, int type) { }
static inline void part_cpu_active_ratio(unsigned long *util, unsigned long *max, int cpu) { }


/*
 * ontime migration
 */
static inline int ontime_can_migrate_task(struct task_struct *p, int dst_cpu) { return 1; }
static inline void ontime_migration(void) { }


/*
 * global boost
 */
static inline void gb_qos_update_request(struct gb_qos_request *req, u32 new_value) { }


/*
 * load balance
 */
static inline void lb_add_cfs_task(struct rq *rq, struct sched_entity *se) { }
static inline int lb_check_priority(int src_cpu, int dst_cpu)
{
	return 0;
}
static inline struct list_head *lb_prefer_cfs_tasks(int src_cpu, int dst_cpu)
{
	return NULL;
}
static inline int lb_need_active_balance(enum cpu_idle_type idle,
				struct sched_domain *sd, int src_cpu, int dst_cpu)
{
	return 0;
}
static inline bool lb_sibling_overutilized(int dst_cpu, struct sched_domain *sd,
					struct cpumask *lb_cpus)
{
	return true;
}
static inline bool lbt_overutilized(int cpu, int level)
{
	return false;
}
static inline void update_lbt_overutil(int cpu, unsigned long capacity) { }
static inline void lb_update_misfit_status(struct task_struct *p, struct rq *rq, unsigned long task_h_load) { }

/*
 * Core sparing
 */
static inline void ecs_update(void) { }
static inline int ecs_is_sparing_cpu(int cpu) { return 0; }

/*
 * EMStune
 */
static void __emst_update_request(struct emst_mode_request *req, s32 new_value, char *func, unsigned int line) { }
static bool emst_can_migrate_task(struct task_struct *p, int dst_cpu) { return true; }
#endif /* CONFIG_SCHED_EMS */

extern unsigned int frt_disable_cpufreq;

#if defined(CONFIG_SCHED_EMS) && defined (CONFIG_SCHED_TUNE)
enum stune_group {
	STUNE_ROOT,
	STUNE_FOREGROUND,
	STUNE_BACKGROUND,
	STUNE_TOPAPP,
	STUNE_RT,
	STUNE_GROUP_COUNT,
};
void emst_cpu_update(int cpu, u64 now);
unsigned long emst_boost(int cpu, unsigned long util);
#else
static inline void emst_cpu_update(int cpu, u64 now) { };
static inline unsigned long emst_boost(int cpu, unsigned long util) { return util; };
#endif
