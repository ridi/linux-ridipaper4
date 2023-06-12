/*
 *  Copyright (C) 2017 Park Bumgyu <bumgyu.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM ems

#if !defined(_TRACE_EMS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EMS_H

#include <linux/sched.h>
#include <linux/tracepoint.h>

/*
 * Tracepoint for wakeup balance
 */
TRACE_EVENT(ems_select_task_rq,

	TP_PROTO(struct task_struct *p, int target_cpu, int wakeup, char *state),

	TP_ARGS(p, target_cpu, wakeup, state),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN	)
		__field(	pid_t,		pid			)
		__field(	int,		target_cpu		)
		__field(	int,		wakeup			)
		__array(	char,		state,		30	)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->target_cpu	= target_cpu;
		__entry->wakeup		= wakeup;
		memcpy(__entry->state, state, 30);
	),

	TP_printk("comm=%s pid=%d target_cpu=%d wakeup=%d state=%s",
		  __entry->comm, __entry->pid, __entry->target_cpu,
		  __entry->wakeup, __entry->state)
);

/*
 * Tracepoint for serching fit cpu
 */
TRACE_EVENT(ems_search_fit_cpus,

	TP_PROTO(struct task_struct *p, int cpu, int nr_running, unsigned long capacity,
			unsigned long cpu_util, unsigned long task_util),

	TP_ARGS(p, cpu, nr_running, capacity, cpu_util, task_util),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN	)
		__field(	pid_t,		pid			)
		__field(	int,		task_cpu		)
		__field(	int,		cpu			)
		__field(	int,		nr_running		)
		__field(	unsigned long,	capacity		)
		__field(	unsigned long,	cpu_util		)
		__field(	unsigned long,	task_util		)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid			= p->pid;
		__entry->task_cpu		= task_cpu(p);
		__entry->cpu			= cpu;
		__entry->nr_running		= nr_running;
		__entry->capacity		= capacity;
		__entry->cpu_util		= cpu_util;
		__entry->task_util		= task_util;
	),

	TP_printk("comm=%s pid=%d task_cpu=%d cpu=%d nr_running=%d capacity=%lu cpu_util=%lu task_util=%lu",
		  __entry->comm, __entry->pid, __entry->task_cpu, __entry->cpu, __entry->nr_running,
		  __entry->capacity, __entry->cpu_util, __entry->task_util)
);

/*
 * Tracepoint for fit cpu selection
 */
TRACE_EVENT(ems_select_fit_cpus,

	TP_PROTO(struct task_struct *p, int wake, unsigned int fit_cpus, unsigned int overutil_cpus),

	TP_ARGS(p, wake, fit_cpus, overutil_cpus),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN	)
		__field(	pid_t,		pid			)
		__field(	int,		wake			)
		__field(	unsigned int,	fit_cpus		)
		__field(	unsigned int,	overutil_cpus		)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid			= p->pid;
		__entry->wake			= wake;
		__entry->fit_cpus		= fit_cpus;
		__entry->overutil_cpus		= overutil_cpus;
	),

	TP_printk("comm=%s pid=%d wake=%d fit_cpus=%#x overutil_cpus=%#x",
		  __entry->comm, __entry->pid, __entry->wake, __entry->fit_cpus, __entry->overutil_cpus)
);

/*
 * Tracepoint for candidate cpu
 */
TRACE_EVENT(ems_candidates,

	TP_PROTO(struct task_struct *p, unsigned int candidates, unsigned int idle_candidates),

	TP_ARGS(p, candidates, idle_candidates),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN	)
		__field(	pid_t,		pid			)
		__field(	unsigned int,	candidates		)
		__field(	unsigned int,	idle_candidates		)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid			= p->pid;
		__entry->candidates		= candidates;
		__entry->idle_candidates	= idle_candidates;
	),

	TP_printk("comm=%s pid=%d candidates=%#x idle_candidates=%#x",
		  __entry->comm, __entry->pid, __entry->candidates, __entry->idle_candidates)
);

/*
 * Tracepoint for computing energy/capacity efficiency
 */
TRACE_EVENT(ems_compute_eff,

	TP_PROTO(struct task_struct *p, int cpu,
		unsigned long cpu_util_with, unsigned int eff_weight,
		unsigned long capacity, unsigned long energy, unsigned long eff),

	TP_ARGS(p, cpu, cpu_util_with, eff_weight, capacity, energy, eff),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN	)
		__field(	pid_t,		pid			)
		__field(	int,		sse			)
		__field(	int,		cpu			)
		__field(	unsigned long,	task_util		)
		__field(	unsigned long,	cpu_util_with		)
		__field(	unsigned int,	eff_weight		)
		__field(	unsigned long,	capacity		)
		__field(	unsigned long,	energy			)
		__field(	unsigned long,	eff			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->sse		= p->sse;
		__entry->cpu		= cpu;
		__entry->task_util	= p->se.ml.util_avg;
		__entry->cpu_util_with	= cpu_util_with;
		__entry->eff_weight	= eff_weight;
		__entry->capacity	= capacity;
		__entry->energy		= energy;
		__entry->eff		= eff;
	),

	TP_printk("comm=%s pid=%d sse=%d cpu=%d task_util=%lu cpu_util_with=%lu eff_weight=%u capacity=%lu energy=%lu eff=%lu",
		__entry->comm, __entry->pid, __entry->sse, __entry->cpu,
		__entry->task_util, __entry->cpu_util_with,
		__entry->eff_weight, __entry->capacity,
		__entry->energy, __entry->eff)
);

/*
 * Tracepoint for computing energy/capacity efficiency
 */
TRACE_EVENT(ems_best_eff,

	TP_PROTO(struct task_struct *p, unsigned int candidates, int idle,
			int best_cpu, unsigned int best_eff),

	TP_ARGS(p, candidates, idle, best_cpu, best_eff),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN	)
		__field(	pid_t,		pid			)
		__field(	unsigned int,	candidates		)
		__field(	int,		idle			)
		__field(	int,		best_cpu		)
		__field(	unsigned int,	best_eff		)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->candidates	= candidates;
		__entry->idle		= idle;
		__entry->best_cpu	= best_cpu;
		__entry->best_eff	= best_eff;
	),

	TP_printk("comm=%s pid=%d candidates=%#x idle=%d best_cpu=%d best_eff=%u",
		__entry->comm, __entry->pid, __entry->candidates, __entry->idle,
		__entry->best_cpu, __entry->best_eff)
);

/*
 * Tracepoint for best cpu
 */
TRACE_EVENT(ems_best_cpu,

	TP_PROTO(struct task_struct *p, int best_cpu, unsigned int best_eff, bool idle),

	TP_ARGS(p, best_cpu, best_eff, idle),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN	)
		__field(	pid_t,		pid			)
		__field(	int,		best_cpu		)
		__field(	unsigned int,	best_eff		)
		__field(	bool,		idle			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->best_cpu	= best_cpu;
		__entry->best_eff	= best_eff;
		__entry->idle		= idle;
	),

	TP_printk("comm=%s pid=%d best_cpu=%d best_eff=%u idle=%d",
		  __entry->comm, __entry->pid, __entry->best_cpu, __entry->best_eff, __entry->idle)
);

/*
 * Tracepoint for finding fit cpus
 */
TRACE_EVENT(ems_ontime_fit_cpus,

	TP_PROTO(struct task_struct *p, int src_cpu, unsigned int fit_cpus),

	TP_ARGS(p, src_cpu, fit_cpus),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN	)
		__field(	pid_t,		pid			)
		__field(	int,		sse			)
		__field(	int,		src_cpu			)
		__field(	unsigned long,	task_runnable		)
		__field(	unsigned int,	fit_cpus		)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->sse		= p->sse;
		__entry->src_cpu	= src_cpu;
		__entry->task_runnable	= p->se.ml.runnable_avg;
		__entry->fit_cpus	= fit_cpus;
	),

	TP_printk("comm=%s pid=%d sse=%d src_cpu=%d task_runnable=%lu fit_cpus=%#x",
		__entry->comm, __entry->pid, __entry->sse, __entry->src_cpu,
		__entry->task_runnable, __entry->fit_cpus)
);

/*
 * Tracepoint for ontime migration
 */
TRACE_EVENT(ems_ontime_migration,

	TP_PROTO(struct task_struct *p, unsigned long load,
		int src_cpu, int dst_cpu, int boost_migration),

	TP_ARGS(p, load, src_cpu, dst_cpu, boost_migration),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN	)
		__field(	pid_t,		pid			)
		__field(	int,		sse			)
		__field(	unsigned long,	load			)
		__field(	int,		src_cpu			)
		__field(	int,		dst_cpu			)
		__field(	int,		bm			)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->sse		= p->sse;
		__entry->load		= load;
		__entry->src_cpu	= src_cpu;
		__entry->dst_cpu	= dst_cpu;
		__entry->bm		= boost_migration;
	),

	TP_printk("comm=%s pid=%d sse=%d ontime_load_avg=%lu src_cpu=%d dst_cpu=%d boost_migration=%d",
		__entry->comm, __entry->pid, __entry->sse, __entry->load,
		__entry->src_cpu, __entry->dst_cpu, __entry->bm)
);

/*
 * Tracepoint for task migration control
 */
TRACE_EVENT(ems_ontime_can_migrate,

	TP_PROTO(struct task_struct *tsk, int src_cpu, int dst_cpu, int migrate, char *label),

	TP_ARGS(tsk, src_cpu, dst_cpu, migrate, label),

	TP_STRUCT__entry(
		__array( char,		comm,	TASK_COMM_LEN	)
		__field( pid_t,		pid			)
		__field( int,		src_cpu			)
		__field( int,		dst_cpu			)
		__field( int,		migrate			)
		__array( char,		label,	64		)
	),

	TP_fast_assign(
		memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
		__entry->pid			= tsk->pid;
		__entry->src_cpu		= src_cpu;
		__entry->dst_cpu		= dst_cpu;
		__entry->migrate		= migrate;
		strncpy(__entry->label, label, 63);
	),

	TP_printk("comm=%s pid=%d src_cpu=%d dst_cpu=%d migrate=%d reason=%s",
		__entry->comm, __entry->pid, __entry->src_cpu, __entry->dst_cpu,
		__entry->migrate, __entry->label)
);

/*
 * Tracepoint for global boost
 */
TRACE_EVENT(ems_global_boost,

	TP_PROTO(char *name, int boost),

	TP_ARGS(name, boost),

	TP_STRUCT__entry(
		__array(	char,	name,	64	)
		__field(	int,	boost		)
	),

	TP_fast_assign(
		memcpy(__entry->name, name, 64);
		__entry->boost		= boost;
	),

	TP_printk("name=%s global_boost=%d", __entry->name, __entry->boost)
);

/*
 * Tracepoint for accounting multi load for tasks.
 */
DECLARE_EVENT_CLASS(multi_load_task,

	TP_PROTO(struct multi_load *ml),

	TP_ARGS(ml),

	TP_STRUCT__entry(
		__array( char,		comm,	TASK_COMM_LEN		)
		__field( pid_t,		pid				)
		__field( int,		cpu				)
		__field( int,		sse				)
		__field( unsigned long,	runnable			)
		__field( unsigned long,	util				)
	),

	TP_fast_assign(
		struct sched_entity *se = container_of(ml, struct sched_entity, ml);
		struct task_struct *tsk = se->my_q ? NULL
					: container_of(se, struct task_struct, se);

		memcpy(__entry->comm, tsk ? tsk->comm : "(null)", TASK_COMM_LEN);
		__entry->pid			= tsk ? tsk->pid : -1;
		__entry->cpu			= tsk ? task_cpu(tsk) : cpu_of(se->my_q->rq);
		__entry->sse			= tsk ? tsk->sse : -1;
		__entry->runnable		= ml->runnable_avg;
		__entry->util			= ml->util_avg;
	),
	TP_printk("comm=%s pid=%d cpu=%d sse=%d runnable=%lu util=%lu",
		  __entry->comm, __entry->pid, __entry->cpu, __entry->sse,
					__entry->runnable, __entry->util)
);

DEFINE_EVENT(multi_load_task, ems_multi_load_task,

	TP_PROTO(struct multi_load *ml),

	TP_ARGS(ml)
);

DEFINE_EVENT(multi_load_task, ems_multi_load_new_task,

	TP_PROTO(struct multi_load *ml),

	TP_ARGS(ml)
);

/*
 * Tracepoint for accounting multi load for cpu.
 */
TRACE_EVENT(ems_multi_load_cpu,

	TP_PROTO(struct multi_load *ml),

	TP_ARGS(ml),

	TP_STRUCT__entry(
		__field( int,		cpu				)
		__field( unsigned long,	runnable			)
		__field( unsigned long,	runnable_s			)
		__field( unsigned long,	util				)
		__field( unsigned long,	util_s				)
	),

	TP_fast_assign(
		struct cfs_rq *cfs_rq = container_of(ml, struct cfs_rq, ml);

		__entry->cpu			= cpu_of(cfs_rq->rq);
		__entry->runnable		= ml->runnable_avg;
		__entry->runnable_s		= ml->runnable_avg_s;
		__entry->util			= ml->util_avg;
		__entry->util_s			= ml->util_avg_s;
	),
	TP_printk("cpu=%d runnable=%lu runnable_s=%lu util=%lu util_s=%lu",
		  __entry->cpu, __entry->runnable, __entry->runnable_s,
					__entry->util, __entry->util_s)
);

/*
 * Tracepoint for tasks' estimated utilization.
 */
TRACE_EVENT(ems_util_est_task,

	TP_PROTO(struct task_struct *tsk, struct multi_load *ml),

	TP_ARGS(tsk, ml),

	TP_STRUCT__entry(
		__array( char,	comm,	TASK_COMM_LEN		)
		__field( pid_t,		pid			)
		__field( int,		cpu			)
		__field( int,		sse			)
		__field( unsigned long,	util_avg		)
		__field( unsigned int,	est_enqueued		)
		__field( unsigned int,	est_ewma		)
	),

	TP_fast_assign(
		memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
		__entry->pid			= tsk->pid;
		__entry->cpu			= task_cpu(tsk);
		__entry->sse			= tsk->sse;
		__entry->util_avg		= ml->util_avg;
		__entry->est_enqueued		= ml->util_est.enqueued;
		__entry->est_ewma		= ml->util_est.ewma;
	),

	TP_printk("comm=%s pid=%d cpu=%d sse=%d util_avg=%lu util_est_ewma=%u util_est_enqueued=%u",
		  __entry->comm,
		  __entry->pid,
		  __entry->cpu,
		  __entry->sse,
		  __entry->util_avg,
		  __entry->est_ewma,
		  __entry->est_enqueued)
);

/*
 * Tracepoint for root cfs_rq's estimated utilization.
 */
TRACE_EVENT(ems_util_est_cpu,

	TP_PROTO(int cpu, struct cfs_rq *cfs_rq),

	TP_ARGS(cpu, cfs_rq),

	TP_STRUCT__entry(
		__field( int,		cpu			)
		__field( unsigned long,	util_avg		)
		__field( unsigned int,	util_est_enqueued	)
		__field( unsigned int,	util_est_enqueued_s	)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->util_avg		= cfs_rq->ml.util_avg;
		__entry->util_est_enqueued	= cfs_rq->ml.util_est.enqueued;
		__entry->util_est_enqueued_s	= cfs_rq->ml.util_est_s.enqueued;
	),

	TP_printk("cpu=%d util_avg=%lu util_est_enqueued=%u util_est_enqueued_s=%u",
		  __entry->cpu,
		  __entry->util_avg,
		  __entry->util_est_enqueued,
		  __entry->util_est_enqueued_s)
);

/*
 * Tracepoint for frequency variant boost
 */
TRACE_EVENT(emst_boost,

	TP_PROTO(int cpu, int ratio, unsigned long util, unsigned long boosted_util),

	TP_ARGS(cpu, ratio, util, boosted_util),

	TP_STRUCT__entry(
		__field( int,		cpu			)
		__field( int,		ratio			)
		__field( unsigned long,	util			)
		__field( unsigned long,	boosted_util		)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->ratio			= ratio;
		__entry->util			= util;
		__entry->boosted_util		= boosted_util;
	),

	TP_printk("cpu=%d ratio=%d util=%lu boosted_util=%lu",
		  __entry->cpu, __entry->ratio, __entry->util, __entry->boosted_util)
);

/*
 * Trace for overutilization flag of LBT
 */
TRACE_EVENT(ems_lbt_overutilized,

	TP_PROTO(int cpu, int level, unsigned long util, unsigned long capacity, bool overutilized),

	TP_ARGS(cpu, level, util, capacity, overutilized),

	TP_STRUCT__entry(
		__field( int,		cpu			)
		__field( int,		level			)
		__field( unsigned long,	util			)
		__field( unsigned long,	capacity		)
		__field( bool,		overutilized		)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->level			= level;
		__entry->util			= util;
		__entry->capacity		= capacity;
		__entry->overutilized		= overutilized;
	),

	TP_printk("cpu=%d level=%d util=%lu capacity=%lu overutilized=%d",
		__entry->cpu, __entry->level, __entry->util,
		__entry->capacity, __entry->overutilized)
);

/*
 * Tracepoint for overutilzed status for lb dst_cpu's sibling
 */
TRACE_EVENT(ems_lb_sibling_overutilized,

	TP_PROTO(int dst_cpu, int level, unsigned long ou),

	TP_ARGS(dst_cpu, level, ou),

	TP_STRUCT__entry(
		__field( int,		dst_cpu			)
		__field( int,		level			)
		__field( unsigned long,	ou			)
	),

	TP_fast_assign(
		__entry->dst_cpu		= dst_cpu;
		__entry->level			= level;
		__entry->ou			= ou;
	),

	TP_printk("dst_cpu=%d level=%d ou=%lu",
				__entry->dst_cpu, __entry->level, __entry->ou)
);

/*
 * Tracepoint for active ratio pattern
 */
TRACE_EVENT(ems_cpu_active_ratio_patten,

	TP_PROTO(int cpu, int p_count, int p_avg, int p_stdev),

	TP_ARGS(cpu, p_count, p_avg, p_stdev),

	TP_STRUCT__entry(
		__field( int,	cpu				)
		__field( int,	p_count				)
		__field( int,	p_avg				)
		__field( int,	p_stdev				)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->p_count		= p_count;
		__entry->p_avg			= p_avg;
		__entry->p_stdev		= p_stdev;
	),

	TP_printk("cpu=%d p_count=%2d p_avg=%4d p_stdev=%4d",
		__entry->cpu, __entry->p_count, __entry->p_avg, __entry->p_stdev)
);

/*
 * Tracepoint for active ratio tracking
 */
TRACE_EVENT(ems_cpu_active_ratio,

	TP_PROTO(int cpu, struct part *pa, char *event),

	TP_ARGS(cpu, pa, event),

	TP_STRUCT__entry(
		__field( int,	cpu				)
		__field( u64,	start				)
		__field( int,	recent				)
		__field( int,	last				)
		__field( int,	avg				)
		__field( int,	max				)
		__field( int,	est				)
		__array( char,	event,		64		)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->start			= pa->period_start;
		__entry->recent			= pa->active_ratio_recent;
		__entry->last			= pa->hist[pa->hist_idx];
		__entry->avg			= pa->active_ratio_avg;
		__entry->max			= pa->active_ratio_max;
		__entry->est			= pa->active_ratio_est;
		strncpy(__entry->event, event, 63);
	),

	TP_printk("cpu=%d start=%llu recent=%4d last=%4d avg=%4d max=%4d est=%4d event=%s",
		__entry->cpu, __entry->start, __entry->recent, __entry->last,
		__entry->avg, __entry->max, __entry->est, __entry->event)
);

/*
 * Tracepoint for active ratio
 */
TRACE_EVENT(ems_cpu_active_ratio_util_stat,

	TP_PROTO(int cpu, unsigned long part_util, unsigned long pelt_util),

	TP_ARGS(cpu, part_util, pelt_util),

	TP_STRUCT__entry(
		__field( int,		cpu					)
		__field( unsigned long,	part_util				)
		__field( unsigned long,	pelt_util				)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->part_util		= part_util;
		__entry->pelt_util		= pelt_util;
	),

	TP_printk("cpu=%d part_util=%lu pelt_util=%lu",
			__entry->cpu, __entry->part_util, __entry->pelt_util)
);

/* Energy Step Wise Governor */
TRACE_EVENT(esg_update_capacity,

	TP_PROTO(int cpu, unsigned long new_freq, unsigned long old_freq, int capacity),

	TP_ARGS(cpu, new_freq, old_freq, capacity),

	TP_STRUCT__entry(
		__field( int,			cpu			)
		__field( unsigned long,		new_freq		)
		__field( unsigned long,		old_freq		)
		__field( int,			capacity		)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->new_freq		= new_freq;
		__entry->old_freq		= old_freq;
		__entry->capacity		= capacity;
	),

	TP_printk("cpu=%d new_freq=%lu old_freq=%lu, capacity=%d",
			__entry->cpu, __entry->new_freq,
			__entry->old_freq, __entry->capacity)
);

TRACE_EVENT(esg_cpu_eutil,

	TP_PROTO(int cpu, int allowed_cap, int active_ratio, int max, int util),

	TP_ARGS(cpu, allowed_cap, active_ratio, max, util),

	TP_STRUCT__entry(
		__field( int,		cpu				)
		__field( int,		allowed_cap			)
		__field( int,		active_ratio			)
		__field( int,		max				)
		__field( int,		util				)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->allowed_cap		= allowed_cap;
		__entry->active_ratio		= active_ratio;
		__entry->max			= max;
		__entry->util			= util;
	),

	TP_printk("cpu=%d allowed_cap=%d active_ratio=%d, max=%d, util=%d",
			__entry->cpu, __entry->allowed_cap, __entry->active_ratio,
			__entry->max, __entry->util)
);

TRACE_EVENT(esg_cpu_util,

	TP_PROTO(int cpu, int eutil, int putil, int max, int util),

	TP_ARGS(cpu, eutil, putil, max, util),

	TP_STRUCT__entry(
		__field( int,		cpu				)
		__field( int,		eutil				)
		__field( int,		putil				)
		__field( int,		max				)
		__field( int,		util				)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->eutil			= eutil;
		__entry->putil			= putil;
		__entry->max			= max;
		__entry->util			= util;
	),

	TP_printk("cpu=%d eutil=%d putil=%d, max=%d, util=%d",
			__entry->cpu, __entry->eutil, __entry->putil,
			__entry->max, __entry->util)
);

TRACE_EVENT(esg_req_freq,

	TP_PROTO(int cpu, int util, int freq),

	TP_ARGS(cpu, util, freq),

	TP_STRUCT__entry(
		__field( int,		cpu				)
		__field( int,		util				)
		__field( int,		freq				)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->util			= util;
		__entry->freq			= freq;
	),

	TP_printk("cpu=%d util=%d, freq=%d",
		__entry->cpu, __entry->util, __entry->freq)
);

TRACE_EVENT(esg_update_limit,

	TP_PROTO(int cpu, int min, int max),

	TP_ARGS(cpu, min, max),

	TP_STRUCT__entry(
		__field( int,		cpu				)
		__field( int,		min				)
		__field( int,		max				)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->min			= min;
		__entry->max			= max;
	),

	TP_printk("cpu=%d min_cap=%d, max_cap=%d",
		__entry->cpu, __entry->min, __entry->max)
);


/*
 * Tracepoint for EMS service
 */
TRACE_EVENT(ems_service,

	TP_PROTO(struct task_struct *p, unsigned long util, int service_cpu, char *event),

	TP_ARGS(p, util, service_cpu, event),

	TP_STRUCT__entry(
		__array( char,		comm,		TASK_COMM_LEN	)
		__field( pid_t,		pid				)
		__field( unsigned long,	util				)
		__field( int,		service_cpu			)
		__array( char,		event,		64		)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid			= p->pid;
		__entry->util			= util;
		__entry->service_cpu		= service_cpu;
		strncpy(__entry->event, event, 63);
	),

	TP_printk("comm=%s pid=%d util=%lu service_cpu=%d event=%s",
			__entry->comm, __entry->pid, __entry->util, __entry->service_cpu, __entry->event)
);

/*
 * Tracepoint for updating spared cpus
 */
TRACE_EVENT(ecs_update_spared_cpus,

	TP_PROTO(unsigned int prev_cpus, unsigned int next_cpus),

	TP_ARGS(prev_cpus, next_cpus),

	TP_STRUCT__entry(
		__field(	unsigned int,	prev_cpus		)
		__field(	unsigned int,	next_cpus		)
	),

	TP_fast_assign(
		__entry->prev_cpus		= prev_cpus;
		__entry->next_cpus		= next_cpus;
	),

	TP_printk("prev_cpus=%#x next_cpus=%#x", __entry->prev_cpus, __entry->next_cpus)
);

/*
 * Tracepoint for updating system status
 */
TRACE_EVENT(ecs_update_system_status,

	TP_PROTO(unsigned int heavy_cpus, unsigned int busy_cpus),

	TP_ARGS(heavy_cpus, busy_cpus),

	TP_STRUCT__entry(
		__field(	unsigned int,	heavy_cpus		)
		__field(	unsigned int,	busy_cpus		)
	),

	TP_fast_assign(
		__entry->heavy_cpus		= heavy_cpus;
		__entry->busy_cpus		= busy_cpus;
	),

	TP_printk("heavy_cpus=%#x busy_cpus=%#x", __entry->heavy_cpus, __entry->busy_cpus)
);

#endif /* _TRACE_EMS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
