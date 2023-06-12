#include "clkout_exynos9630.h"

#define CLKOUT0_OFFSET		(0xA00)
#define CLKOUT0_SEL_SHIFT	(8)
#define CLKOUT0_SEL_WIDTH	(6)
#define CLKOUT0_SEL_TCXO	(0x1)
#define CLKOUT0_ENABLE_SHIFT	(0)
#define CLKOUT0_ENABLE_WIDTH	(1)
#define CLKOUT0_SEL_ENABLE	(1)

#define CLKOUT1_OFFSET		(0xA0C)
#define CLKOUT1_SEL_SHIFT	(8)
#define CLKOUT1_SEL_WIDTH	(6)
#define CLKOUT1_SEL_TCXO	(0x0)
#define CLKOUT1_ENABLE_SHIFT	(0)
#define CLKOUT1_ENABLE_WIDTH	(1)
#define CLKOUT1_SEL_ENABLE	(1)

struct cmucal_clkout cmucal_clkout_list[] = {
	CLKOUT(VCLK_CLKOUT0, CLKOUT0_OFFSET, CLKOUT0_SEL_SHIFT, CLKOUT0_SEL_WIDTH, CLKOUT0_SEL_TCXO, CLKOUT0_ENABLE_SHIFT, CLKOUT0_ENABLE_WIDTH, CLKOUT0_SEL_ENABLE),
	CLKOUT(VCLK_CLKOUT1, CLKOUT1_OFFSET, CLKOUT1_SEL_SHIFT, CLKOUT1_SEL_WIDTH, CLKOUT1_SEL_TCXO, CLKOUT1_ENABLE_SHIFT, CLKOUT1_ENABLE_WIDTH, CLKOUT1_SEL_ENABLE),
};

unsigned int cmucal_clkout_size = 2;
