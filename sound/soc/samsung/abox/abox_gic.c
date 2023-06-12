/* sound/soc/samsung/abox_v2/abox_gic.c
 *
 * ALSA SoC Audio Layer - Samsung ABOX GIC driver
 *
 * Copyright (c) 2016 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/smc.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/delay.h>
#include <linux/smc.h>

#include "abox_util.h"
#include "abox_gic.h"

#define GICD_PA_BASE 0x14AF1000
#define GICC_PA_BASE 0x14AF2000

int abox_gicd_write(unsigned int offset, unsigned int value)
{
	int ret;
	ret = exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(GICD_PA_BASE + offset), value, 0);
	if (ret)
		pr_err("ret (%d) : Fail to write %x to ABOX GICD + %x\n", ret, value, offset);

	return ret;
}

int abox_gicc_write(unsigned int offset, unsigned int value)
{
	int ret;
	ret = exynos_smc(SMC_CMD_REG, SMC_REG_ID_SFR_W(GICC_PA_BASE + offset), value, 0);
	if (ret)
		pr_err("ret (%d) : Fail to write %x to ABOX GICC + %x\n", ret, value, offset);

	return ret;
}

static u32 abox_gicd_read(unsigned int offset)
{
	u32 value;
	unsigned long val;
	int ret;

	ret = exynos_smc_readsfr((GICD_PA_BASE + offset), &val);
	if (ret < 0) {
		pr_err("ret (%d) : Fail to read from ABOX GICD + %x\n", ret, offset);
		return 0;
	}

	value = (u32)val;

	return value;
}

void abox_gicd_dump(struct device *dev, char *dump, size_t off, size_t size)
{
	struct abox_gic_data *data = dev_get_drvdata(dev);
	size_t limit = min(off + size, data->gicd_size);
	u32 *buf = (u32 *)dump;

	for (; off < limit; off += 4)
		*buf++ = abox_gicd_read(off);
}

void abox_gic_enable(struct device *dev, unsigned int irq, bool en)
{
	unsigned int base = en ? GIC_DIST_ENABLE_SET : GIC_DIST_ENABLE_CLEAR;
	unsigned int offset = base + (irq / 32 * 4);
	unsigned int shift = irq % 32;
	unsigned int mask = 0x1 << shift;
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;

	spin_lock_irqsave(&lock, flags);
	abox_gicd_write(offset, mask);
	spin_unlock_irqrestore(&lock, flags);
}

void abox_gic_target(struct device *dev, unsigned int irq,
		enum abox_gic_target target)
{
	unsigned int offset = GIC_DIST_TARGET + (irq & 0xfffffffc);
	unsigned int shift = (irq & 0x3) * 8;
	unsigned int mask = 0xff << shift;
	unsigned long long_val;
	unsigned int val;
	static DEFINE_SPINLOCK(lock);
	unsigned long flags;

	dev_dbg(dev, "%s(%d, %d)\n", __func__, irq, target);

	spin_lock_irqsave(&lock, flags);
	exynos_smc_readsfr((GICD_PA_BASE + offset), &long_val);
	val = (unsigned int)long_val;
	val &= ~mask;
	val |= ((0x1 << target) << shift) & mask;
	abox_gicd_write(offset, val);
	spin_unlock_irqrestore(&lock, flags);
}

void abox_gic_generate_interrupt(struct device *dev, unsigned int irq)
{
	dev_dbg(dev, "%s(%d)\n", __func__, irq);

	abox_gicd_write(GIC_DIST_SOFTINT, (0x1 << 16) | (irq & 0xf));
}
EXPORT_SYMBOL(abox_gic_generate_interrupt);

int abox_gic_register_irq_handler(struct device *dev, unsigned int irq,
		irq_handler_t handler, void *dev_id)
{
	struct abox_gic_data *data = dev_get_drvdata(dev);

	dev_dbg(dev, "%s(%u, %ps)\n", __func__, irq, handler);

	if (irq >= ARRAY_SIZE(data->handler)) {
		dev_err(dev, "invalid irq: %d\n", irq);
		return -EINVAL;
	}

	WRITE_ONCE(data->handler[irq].handler, handler);
	WRITE_ONCE(data->handler[irq].dev_id, dev_id);

	return 0;
}
EXPORT_SYMBOL(abox_gic_register_irq_handler);

int abox_gic_unregister_irq_handler(struct device *dev, unsigned int irq)
{
	struct abox_gic_data *data = dev_get_drvdata(dev);

	dev_dbg(dev, "%s(%u)\n", __func__, irq);

	if (irq >= ARRAY_SIZE(data->handler)) {
		dev_err(dev, "invalid irq: %d\n", irq);
		return -EINVAL;
	}

	WRITE_ONCE(data->handler[irq].handler, NULL);
	WRITE_ONCE(data->handler[irq].dev_id, NULL);

	return 0;
}
EXPORT_SYMBOL(abox_gic_unregister_irq_handler);

static irqreturn_t __abox_gic_irq_handler(struct abox_gic_data *data, u32 irqnr)
{
	irq_handler_t handler;
	void *dev_id;

	if (irqnr >= ARRAY_SIZE(data->handler))
		return IRQ_NONE;

	dev_id = READ_ONCE(data->handler[irqnr].dev_id);
	handler = READ_ONCE(data->handler[irqnr].handler);
	if (!handler)
		return IRQ_NONE;

	return handler(irqnr, dev_id);
}

static irqreturn_t abox_gic_irq_handler(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	struct abox_gic_data *data = dev_get_drvdata(dev);
	irqreturn_t ret = IRQ_NONE;
	unsigned long long_irqstat;
	u32 irqstat, irqnr;

	dev_dbg(dev, "%s\n", __func__);

	do {
		exynos_smc_readsfr((GICC_PA_BASE + GIC_CPU_INTACK), &long_irqstat);
		irqstat = (unsigned int)long_irqstat;
		irqnr = irqstat & GICC_IAR_INT_ID_MASK;
		dev_dbg(dev, "IAR: %08X\n", irqstat);

		if (irqnr < 16) {
			abox_gicc_write(GIC_CPU_EOI, irqstat);
			abox_gicc_write(GIC_CPU_DEACTIVATE, irqstat);
			ret |= __abox_gic_irq_handler(data, irqnr);
			continue;
		} else if (irqnr > 15 && irqnr < 1021) {
			abox_gicc_write(GIC_CPU_EOI, irqstat);
			ret |= __abox_gic_irq_handler(data, irqnr);
			continue;
		}
		break;
	} while (1);

	return ret;
}

static void abox_gicd_enable(struct device *dev, bool en)
{
	if (en) {
		abox_gicd_write(GIC_DIST_CTRL, 0x1);
		abox_gicd_write(GIC_DIST_IGROUP + 0x0, 0x0);
		abox_gicd_write(GIC_DIST_IGROUP + 0x4, 0x0);
		abox_gicd_write(GIC_DIST_IGROUP + 0x8, 0x0);
		abox_gicd_write(GIC_DIST_IGROUP + 0xc, 0x0);
	} else {
		abox_gicd_write(GIC_DIST_CTRL, 0x0);
	}
}

void abox_gic_init_gic(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);

	abox_gicc_write(GIC_CPU_PRIMASK, 0xff);
	abox_gicd_write(GIC_DIST_CTRL, 0x3);
	abox_gicc_write(GIC_CPU_CTRL, 0x3);
}
EXPORT_SYMBOL(abox_gic_init_gic);

int abox_gic_enable_irq(struct device *dev)
{
	struct abox_gic_data *data = dev_get_drvdata(dev);

	if (likely(data->disabled)) {
		dev_info(dev, "%s\n", __func__);

		data->disabled = false;
		enable_irq(data->irq);
		abox_gicd_enable(dev, true);
	}
	return 0;
}

int abox_gic_disable_irq(struct device *dev)
{
	struct abox_gic_data *data = dev_get_drvdata(dev);

	if (likely(!data->disabled)) {
		dev_dbg(dev, "%s\n", __func__);

		data->disabled = true;
		disable_irq(data->irq);
	}

	return 0;
}

static int samsung_abox_gic_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct abox_gic_data *data;
	int ret;

	dev_info(dev, "%s\n", __func__);

	data = devm_kzalloc(dev, sizeof(struct abox_gic_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	platform_set_drvdata(pdev, data);

	data->gicd_base = devm_get_request_ioremap(pdev, "gicd",
			&data->gicd_base_phys, &data->gicd_size);
	if (IS_ERR(data->gicd_base))
		return PTR_ERR(data->gicd_base);

	data->gicc_base = devm_get_request_ioremap(pdev, "gicc",
			&data->gicc_base_phys, &data->gicc_size);
	if (IS_ERR(data->gicc_base))
		return PTR_ERR(data->gicc_base);

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		dev_err(dev, "Failed to get irq\n");
		return data->irq;
	}

	ret = devm_request_irq(dev, data->irq, abox_gic_irq_handler,
			IRQF_TRIGGER_RISING | IRQF_GIC_MULTI_TARGET,
			pdev->name, dev);
	if (ret < 0) {
		dev_err(dev, "Failed to request irq\n");
		return ret;
	}

	ret = enable_irq_wake(data->irq);
	if (ret < 0)
		dev_err(dev, "Failed to enable irq wake\n");

	dev_info(dev, "%s: probe complete\n", __func__);

	return 0;
}

static int samsung_abox_gic_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);

	return 0;
}

static const struct of_device_id samsung_abox_gic_of_match[] = {
	{
		.compatible = "samsung,abox-gic",
	},
	{},
};
MODULE_DEVICE_TABLE(of, samsung_abox_gic_of_match);

static struct platform_driver samsung_abox_gic_driver = {
	.probe  = samsung_abox_gic_probe,
	.remove = samsung_abox_gic_remove,
	.driver = {
		.name = "samsung-abox-gic",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(samsung_abox_gic_of_match),
	},
};

module_platform_driver(samsung_abox_gic_driver);

/* Module information */
MODULE_AUTHOR("Gyeongtaek Lee, <gt82.lee@samsung.com>");
MODULE_DESCRIPTION("Samsung ASoC A-Box GIC Driver");
MODULE_ALIAS("platform:samsung-abox-gic");
MODULE_LICENSE("GPL");
