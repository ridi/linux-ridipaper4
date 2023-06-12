#ifndef __EXYNOS_ADV_TRACER_H_
#define __EXYNOS_ADV_TRACER_H_

#ifdef CONFIG_EXYNOS_ADV_TRACER
extern int adv_tracer_arraydump(void);
#else
static inline int adv_tracer_arraydump(void)
{
	return 0;
}
#endif

#endif
