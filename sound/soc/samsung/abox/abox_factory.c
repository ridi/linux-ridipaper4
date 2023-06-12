#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <sound/pcm_params.h>
#include <sound/samsung/abox.h>

#include "abox_util.h"
#include "abox.h"
#include "abox_cmpnt.h"
#include "abox_if.h"

static int *pcm_buffer;
static int prval_stereo_48khz_1khz[] = {
	0x085f,  0x085f,  0x109a,  0x109a,  0x188c,  0x188c,  0x2013,  0x2013,
	0x270d,  0x270d,  0x2d5c,  0x2d5c,  0x32e5,  0x32e5,  0x378e,  0x378e,
	0x3b44,  0x3b44,  0x3df7,  0x3df7,  0x3f9a,  0x3f9a,  0x4026,  0x4026,
	0x3f9a,  0x3f9a,  0x3df7,  0x3df7,  0x3b44,  0x3b44,  0x378e,  0x378e,
	0x32e5,  0x32e5,  0x2d5c,  0x2d5c,  0x270d,  0x270d,  0x2013,  0x2013,
	0x188c,  0x188c,  0x109a,  0x109a,  0x085f,  0x085f,  0x0000,  0x0000,
	0xf7a1,  0xf7a1,  0xef66,  0xef66,  0xe774,  0xe774,  0xdfed,  0xdfed,
	0xd8f3,  0xd8f3,  0xd2a4,  0xd2a4,  0xcd1b,  0xcd1b,  0xc872,  0xc872,
	0xc4bc,  0xc4bc,  0xc209,  0xc209,  0xc066,  0xc066,  0xbfda,  0xbfda,
	0xc066,  0xc066,  0xc209,  0xc209,  0xc4bc,  0xc4bc,  0xc872,  0xc872,
	0xcd1b,  0xcd1b,  0xd2a4,  0xd2a4,  0xd8f3,  0xd8f3,  0xdfed,  0xdfed,
	0xe774,  0xe774,  0xef66,  0xef66,  0xf7a1,  0xf7a1,  0x0000,  0x0000,
};

static unsigned int abox_uaif0_rdma0_48KHz_16bit_stereo[][2] = {
	{ 0x40,   0x00000021 }, /* ROUTE_CTRL0 */
	{ 0x200,  0x02020204 }, /* SPUS_CTRL_FC0 */
	{ 0x204,  0x00000009 }, /* SPUS_CTRL1 */
	{ 0x218,  0x02020202 }, /* SPUS_CTRL_FC1 */
	{ 0x21C,  0x02020202 }, /* SPUS_CTRL_FC2 */
	{ 0x220,  0x02000000 }, /* SPUS_SBANK_RDMA0 */
	{ 0x224,  0x02000200 }, /* SPUS_SBANK_RDMA1 */
	{ 0x228,  0x02000400 },
	{ 0x22c,  0x02000600 },
	{ 0x230,  0x02000800 },
	{ 0x234,  0x02000a00 },
	{ 0x238,  0x02000c00 },
	{ 0x23C,  0x02000e00 },
	{ 0x240,  0x02001000 },
	{ 0x244,  0x02001200 },
	{ 0x248,  0x02001400 },
	{ 0x24C,  0x02001600 },
	{ 0x250,  0x02001800 },
	{ 0x254,  0x02001a00 },
	{ 0x258,  0x02001c00 },
	{ 0x25C,  0x02001e00 },
	{ 0x260,  0x02002000 },
	{ 0x264,  0x02002200 },
	{ 0x268,  0x02002400 },
	{ 0x26C,  0x02002600 },
	{ 0x280,  0x02002800 }, /* SPUS_SBANK_MIXP */
	{ 0x514,  0x1984f000 }, /* UAIF1_CTRL1 */
	{ 0x1008, 0x14F29000 }, /* RDMA0_BUF_START */
	{ 0x100C, 0x14F29F00 }, /* RDMA0_BUF_END */
	{ 0x1010, 0x000003c0 }, /* RDMA0_BUF_OFFSET */
	{ 0x1014, 0x14F29000 }, /* RDMA0_STR_POINT */
	{ 0x1018, 0x00800000 }, /* RDMA0_VOL_FACTOR */
	{ 0x1024, 0x00000003 }, /* RDMA0_BIT_CTRL0 */
	{ 0x510,  0x01880015 }, /* UAIF1_CTRL0 */
	{ 0x1000, 0x00201921 }, /* RDMA0_CTRL0 */
};

void abox_set_bclk_ratio(unsigned int rate)
{
	struct abox_data *abox_data = abox_get_abox_data();
	struct device *dev = abox_data->dev_if[1];
	struct abox_if_data *data = dev_get_drvdata(dev);
	int clk_width = 96; /* LCM of 24 and 32 */
	int clk_channels = 2;
	unsigned long audif_rate = 384000 * clk_width * clk_channels;
	int j, ret;
	unsigned long i;

	abox_request_cpu_gear(dev, abox_data, ABOX_CPU_GEAR_DAI + 0xFF,
			1, "set_bclk_ratio");

	pinctrl_pm_select_default_state(dev);

	abox_disable_qchannel(dev, abox_data, ABOX_BCLK_UAIF0, 1);

	ret = clk_enable(data->clk_bclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable bclk: %d\n", ret);
		return;
	}

	ret = clk_enable(data->clk_bclk_gate);
	if (ret < 0) {
		dev_err(dev, "Failed to enable bclk_gate: %d\n", ret);
		return;
	}

	ret = clk_set_rate(abox_data->clk_audif, audif_rate);
	if (ret < 0) {
		dev_err(dev, "Failed to set audif clock: %d\n", ret);
		return;
	}

	ret = clk_set_rate(data->clk_bclk, rate * 2 * 16);
	if (ret < 0) {
		dev_err(dev, "bclk set error: %d\n", ret);
		return;
	}

	pcm_buffer = (int *)ioremap(0x14F29000, 0xF00);
	for (i = 0; i < 10; i++) {
		for (j = 0; j < 96; j++)
			*pcm_buffer++ = prval_stereo_48khz_1khz[j];
	}

	for (i = 0; i < ARRAY_SIZE(abox_uaif0_rdma0_48KHz_16bit_stereo); i++) {
		writel(abox_uaif0_rdma0_48KHz_16bit_stereo[i][1],
			abox_data->sfr_base +
			abox_uaif0_rdma0_48KHz_16bit_stereo[i][0]);
		dev_info(abox_data->dev, "offset: %x, value = %x\n",
			abox_uaif0_rdma0_48KHz_16bit_stereo[i][0],
			abox_uaif0_rdma0_48KHz_16bit_stereo[i][1]);
	}
}

void abox_reset_bclk_ratio(void)
{
	struct abox_data *abox_data = abox_get_abox_data();
	struct device *dev = abox_data->dev_if[1];
	struct abox_if_data *data = dev_get_drvdata(dev);
	unsigned long i;

	for (i = 0; i < ARRAY_SIZE(abox_uaif0_rdma0_48KHz_16bit_stereo); i++) {
		writel(0, abox_data->sfr_base +
				abox_uaif0_rdma0_48KHz_16bit_stereo[i][0]);
	}

	clk_disable(data->clk_bclk);
	clk_disable(data->clk_bclk_gate);

	abox_disable_qchannel(dev, abox_data, ABOX_BCLK_UAIF0, 0);

	pinctrl_pm_select_sleep_state(dev);

	iounmap(pcm_buffer);

	abox_request_cpu_gear(dev, abox_data, ABOX_CPU_GEAR_DAI + 0xFF,
			ABOX_CPU_GEAR_MIN, "set_bclk_ratio");
}
