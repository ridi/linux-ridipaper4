/*
 * s2mpu10-irq.c - Interrupt controller support for S2MPU10
 *
 * Copyright (C) 2016 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/

#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/mfd/samsung/s2mpu10.h>
#include <linux/mfd/samsung/s2mpu10-regulator.h>
#include <linux/mfd/samsung/s2mpu11-regulator.h>
#include <linux/of_irq.h>

#define S2MPU10_IBI_CNT			2
#define S2MPU11_CODEC_IRQ_CNT	6

static const u8 s2mpu10_mask_reg[] = {
	[PMIC_INT1] = 	S2MPU10_PMIC_REG_INT1M,
	[PMIC_INT2] =	S2MPU10_PMIC_REG_INT2M,
	[PMIC_INT3] = 	S2MPU10_PMIC_REG_INT3M,
	[PMIC_INT4] =	S2MPU10_PMIC_REG_INT4M,
	[PMIC_INT5] = 	S2MPU10_PMIC_REG_INT5M,
	[PMIC_INT6] = 	S2MPU10_PMIC_REG_INT6M,
};

static struct i2c_client *get_i2c(struct s2mpu10_dev *s2mpu10,
				enum s2mpu10_irq_source src)
{
	switch (src) {
	case PMIC_INT1 ... PMIC_INT6:
		return s2mpu10->pmic;
	default:
		return ERR_PTR(-EINVAL);
	}
}

struct s2mpu10_irq_data {
	int mask;
	enum s2mpu10_irq_source group;
};

#define DECLARE_IRQ(idx, _group, _mask)		\
	[(idx)] = { .group = (_group), .mask = (_mask) }
static const struct s2mpu10_irq_data s2mpu10_irqs[] = {
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_PWRONR_INT1,	PMIC_INT1, 1 << 1),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_PWRONF_INT1,	PMIC_INT1, 1 << 0),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_JIGONBF_INT1,	PMIC_INT1, 1 << 2),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_JIGONBR_INT1,	PMIC_INT1, 1 << 3),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_ACOKF_INT1,	PMIC_INT1, 1 << 4),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_ACOKR_INT1,	PMIC_INT1, 1 << 5),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_PWRON1S_INT1,	PMIC_INT1, 1 << 6),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_MRB_INT1,		PMIC_INT1, 1 << 7),

	DECLARE_IRQ(S2MPU10_PMIC_IRQ_RTC60S_INT2,	PMIC_INT2, 1 << 0),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_RTCA1_INT2,	PMIC_INT2, 1 << 1),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_RTCA0_INT2,	PMIC_INT2, 1 << 2),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_SMPL_INT2,		PMIC_INT2, 1 << 3),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_RTC1S_INT2,	PMIC_INT2, 1 << 4),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_WTSR_INT2,		PMIC_INT2, 1 << 5),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_ADCDONE_INT2,	PMIC_INT2, 1 << 6),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_WTSRB_INT2,	PMIC_INT2, 1 << 7),

	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB1_INT3,	PMIC_INT3, 1 << 0),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB2_INT3,	PMIC_INT3, 1 << 1),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB3_INT3,	PMIC_INT3, 1 << 2),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB4_INT3,	PMIC_INT3, 1 << 3),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB5_INT3,	PMIC_INT3, 1 << 4),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB6_INT3,	PMIC_INT3, 1 << 5),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB7_INT3,	PMIC_INT3, 1 << 6),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB8_INT3,	PMIC_INT3, 1 << 7),

	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB9_INT4,	PMIC_INT4, 1 << 0),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OCPB10_INT4,	PMIC_INT4, 1 << 1),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_UV_BB_INT4,	PMIC_INT4, 1 << 2),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_BB_NTR_DET_INT4,	PMIC_INT4, 1 << 3),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_SC_LDO1_INT4,	PMIC_INT4, 1 << 4),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_SC_LDO2_INT4,	PMIC_INT4, 1 << 5),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_SC_LDO7_INT4,	PMIC_INT4, 1 << 6),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_SC_LDO19_INT4,	PMIC_INT4, 1 << 7),

	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_B1_INT5,	PMIC_INT5, 1 << 0),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_B2_INT5,	PMIC_INT5, 1 << 1),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_B3_INT5,	PMIC_INT5, 1 << 2),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_B4_INT5,	PMIC_INT5, 1 << 3),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_B5_INT5,	PMIC_INT5, 1 << 4),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_B6_INT5,	PMIC_INT5, 1 << 5),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_B7_INT5,	PMIC_INT5, 1 << 6),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_B8_INT5,	PMIC_INT5, 1 << 7),

	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_B9_INT6,	PMIC_INT6, 1 << 0),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_OI_BB_INT6,	PMIC_INT6, 1 << 1),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_INT120C_INT6,	PMIC_INT6, 1 << 2),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_INT140C_INT6,	PMIC_INT6, 1 << 3),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_TSD_INT6,		PMIC_INT6, 1 << 4),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_TIMEOUT2_INT6,	PMIC_INT6, 1 << 5),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_TIMEOUT3_INT6,	PMIC_INT6, 1 << 6),
	DECLARE_IRQ(S2MPU10_PMIC_IRQ_SUB_SMPL_INT6,	PMIC_INT6, 1 << 7),
};

static void s2mpu10_irq_lock(struct irq_data *data)
{
	struct s2mpu10_dev *s2mpu10 = irq_get_chip_data(data->irq);

	mutex_lock(&s2mpu10->irqlock);
}

static void s2mpu10_irq_sync_unlock(struct irq_data *data)
{
	struct s2mpu10_dev *s2mpu10 = irq_get_chip_data(data->irq);
	int i;

	for (i = 0; i < S2MPU10_IRQ_GROUP_NR; i++) {
		u8 mask_reg = s2mpu10_mask_reg[i];
		struct i2c_client *i2c = get_i2c(s2mpu10, i);

		if (mask_reg == S2MPU10_REG_INVALID ||
				IS_ERR_OR_NULL(i2c))
			continue;
		s2mpu10->irq_masks_cache[i] = s2mpu10->irq_masks_cur[i];

		s2mpu10_write_reg(i2c, s2mpu10_mask_reg[i],
				s2mpu10->irq_masks_cur[i]);
	}

	mutex_unlock(&s2mpu10->irqlock);
}

static const inline struct s2mpu10_irq_data *
irq_to_s2mpu10_irq(struct s2mpu10_dev *s2mpu10, int irq)
{
	return &s2mpu10_irqs[irq - s2mpu10->irq_base];
}

static void s2mpu10_irq_mask(struct irq_data *data)
{
	struct s2mpu10_dev *s2mpu10 = irq_get_chip_data(data->irq);
	const struct s2mpu10_irq_data *irq_data =
	    irq_to_s2mpu10_irq(s2mpu10, data->irq);

	if (irq_data->group >= S2MPU10_IRQ_GROUP_NR)
		return;

	s2mpu10->irq_masks_cur[irq_data->group] |= irq_data->mask;
}

static void s2mpu10_irq_unmask(struct irq_data *data)
{
	struct s2mpu10_dev *s2mpu10 = irq_get_chip_data(data->irq);
	const struct s2mpu10_irq_data *irq_data =
	    irq_to_s2mpu10_irq(s2mpu10, data->irq);

	if (irq_data->group >= S2MPU10_IRQ_GROUP_NR)
		return;

	s2mpu10->irq_masks_cur[irq_data->group] &= ~irq_data->mask;
}

static struct irq_chip s2mpu10_irq_chip = {
	.name			= MFD_DEV_NAME,
	.irq_bus_lock		= s2mpu10_irq_lock,
	.irq_bus_sync_unlock	= s2mpu10_irq_sync_unlock,
	.irq_mask		= s2mpu10_irq_mask,
	.irq_unmask		= s2mpu10_irq_unmask,
};

static irqreturn_t s2mpu10_irq_thread(int irq, void *data)
{
	struct s2mpu10_dev *s2mpu10 = data;
	u8 ibi_src[S2MPU10_IBI_CNT] = {0};
	u8 irq_reg[S2MPU10_IRQ_GROUP_NR] = {0};
	int i, ret;

	ret = s2mpu10_bulk_read(s2mpu10->i2c, S2MPU10_PMIC_REG_IBI0,
			S2MPU10_IBI_CNT, &ibi_src[0]);

	if (ret) {
		pr_err("%s:%s Failed to read IBI source: %d\n",
			MFD_DEV_NAME, __func__, ret);
		return IRQ_NONE;
	}

	pr_info("%s: IBI source(0x%02x, 0x%02x)\n", __func__, ibi_src[0], ibi_src[1]);

	if (ibi_src[0] & S2MPU10_IBI0_PMIC_M) {
		pr_info("%s : IBI from main pmic\n", __func__);
		ret = s2mpu10_bulk_read(s2mpu10->pmic, S2MPU10_PMIC_REG_INT1,
				S2MPU10_NUM_IRQ_PMIC_REGS, &irq_reg[PMIC_INT1]);
		if (ret) {
			pr_err("%s:%s Failed to read pmic interrupt: %d\n",
				MFD_DEV_NAME, __func__, ret);
			return IRQ_NONE;
		}

		pr_info("%s: master pmic interrupt(0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x)\n",
			 __func__, irq_reg[PMIC_INT1], irq_reg[PMIC_INT2], irq_reg[PMIC_INT3],
			 irq_reg[PMIC_INT4], irq_reg[PMIC_INT5], irq_reg[PMIC_INT6]);
	}

	if (ibi_src[0] & S2MPU10_IBI0_PMIC_S) {
		pr_info("%s: IBI from slave pmic\n", __func__);
		s2mpu11_call_notifier();
	}

	if (ibi_src[0] & S2MPU10_IBI0_CODEC) {
		pr_info("%s: IBI from codec\n", __func__);
		/* TODO : IRQ from s2mpu11 codec */
	}

	/* Apply masking */
	for (i = 0; i < S2MPU10_IRQ_GROUP_NR; i++)
		irq_reg[i] &= ~s2mpu10->irq_masks_cur[i];

	/* Report */
	for (i = 0; i < S2MPU10_IRQ_NR; i++) {
		if (irq_reg[s2mpu10_irqs[i].group] & s2mpu10_irqs[i].mask)
			handle_nested_irq(s2mpu10->irq_base + i);
	}

	return IRQ_HANDLED;
}

int s2mpu10_irq_init(struct s2mpu10_dev *s2mpu10)
{
	int i;
	int ret;
	u8 i2c_data;
	int cur_irq;

	if (!s2mpu10->irq_base) {
		dev_err(s2mpu10->dev, "No interrupt base specified.\n");
		return 0;
	}

	mutex_init(&s2mpu10->irqlock);

	/* Mask individual interrupt sources */
	for (i = 0; i < S2MPU10_IRQ_GROUP_NR; i++) {
		struct i2c_client *i2c;

		s2mpu10->irq_masks_cur[i] = 0xff;
		s2mpu10->irq_masks_cache[i] = 0xff;

		i2c = get_i2c(s2mpu10, i);

		if (IS_ERR_OR_NULL(i2c))
			continue;
		if (s2mpu10_mask_reg[i] == S2MPU10_REG_INVALID)
			continue;

		s2mpu10_write_reg(i2c, s2mpu10_mask_reg[i], 0xff);
	}

	/* Register with genirq */
	for (i = 0; i < S2MPU10_IRQ_NR; i++) {
		cur_irq = i + s2mpu10->irq_base;
		irq_set_chip_data(cur_irq, s2mpu10);
		irq_set_chip_and_handler(cur_irq, &s2mpu10_irq_chip,
					 handle_level_irq);
		irq_set_nested_thread(cur_irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(cur_irq, IRQF_VALID);
#else
		irq_set_noprobe(cur_irq);
#endif
	}

	s2mpu10_write_reg(s2mpu10->i2c, S2MPU10_PMIC_REG_IRQM, 0xff);
	/* Unmask s2mpu10 interrupt */
	ret = s2mpu10_read_reg(s2mpu10->i2c, S2MPU10_PMIC_REG_IRQM,
			  &i2c_data);
	if (ret) {
		pr_err("%s:%s fail to read intsrc mask reg\n",
					 MFD_DEV_NAME, __func__);
		return ret;
	}

	i2c_data &= ~(S2MPU10_IRQSRC_PMIC);	/* Unmask pmic interrupt */
	s2mpu10_write_reg(s2mpu10->i2c, S2MPU10_PMIC_REG_IRQM,
			   i2c_data);

	pr_info("%s:%s s2mpu10_PMIC_REG_IRQM=0x%02x\n",
			MFD_DEV_NAME, __func__, i2c_data);

	/* register irq to gic */
	s2mpu10->irq = irq_of_parse_and_map(s2mpu10->dev->of_node, 0);
	ret = request_threaded_irq(s2mpu10->irq, NULL, s2mpu10_irq_thread,
				   IRQF_ONESHOT,
				   "s2mpu10-irq", s2mpu10);

	if (ret) {
		dev_err(s2mpu10->dev, "Failed to request IRQ %d: %d\n",
			s2mpu10->irq, ret);
		return ret;
	}

	return 0;
}

void s2mpu10_irq_exit(struct s2mpu10_dev *s2mpu10)
{
	if (s2mpu10->irq)
		free_irq(s2mpu10->irq, s2mpu10);
}
