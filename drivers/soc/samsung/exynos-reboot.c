/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 *
 * Exynos - Support SoC specific Reboot
 * Author: Hosung Kim <hosung0.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/soc/samsung/exynos-soc.h>
#include <linux/debug-snapshot.h>
#ifdef CONFIG_EXYNOS_ACPM
#include <soc/samsung/acpm_ipc_ctrl.h>
#endif
#include <soc/samsung/exynos-pmu.h>
#include <soc/samsung/exynos-debug.h>
#include <linux/mfd/samsung/s2mpu12-regulator.h>

#define SWRESET				(0x2)
#define SYSTEM_CONFIGURATION		(0x3a00)
#define PS_HOLD_CONTROL			(0x030C)
#define EXYNOS_PMU_SYSIP_DAT0		(0x0810)
#define EXYNOS_PMU_DREXCAL7		(0x09BC)

#define REBOOT_MODE_NORMAL		0x00
#define REBOOT_MODE_CHARGE		0x0A
#define REBOOT_MODE_FASTBOOT		0xFC
#define REBOOT_MODE_RECOVERY		0xFF
#define REBOOT_MODE_FACTORY		0xFD
#define REBOOT_MODE_USBRECOVERY		0xFE

extern void (*arm_pm_restart)(enum reboot_mode reboot_mode, const char *cmd);

struct exynos_reboot_variant {
	int ps_hold_reg;
	int ps_hold_data_bit;
	int swreset_reg;
	int swreset_bit;
	int reboot_mode_reg;
	void (*reboot)(enum reboot_mode mode, const char *cmd);
	void (*power_off)(void);
	void (*reset_control)(void);
	int (*power_key_chk)(void);
};

static struct exynos_exynos_reboot {
	struct device *dev;
	const struct exynos_reboot_variant *variant;
	void __iomem *reg_base;
	char reset_reason[SZ_32];
} exynos_reboot;

static const struct exynos_reboot_variant *exynos_reboot_get_variant(struct platform_device *pdev)
{
	const struct exynos_reboot_variant *variant = of_device_get_match_data(&pdev->dev);

	if (!variant) {
		/* Device matched by platform_device_id */
		variant = (const struct exynos_reboot_variant *)
			   platform_get_device_id(pdev)->driver_data;
	}

	return variant;
}

static void exynos_power_off_v1(void)
{
	const struct exynos_reboot_variant *variant = exynos_reboot.variant;
	int poweroff_try = 0;
	unsigned int val = 0;

	dev_info(exynos_reboot.dev, "Power off key(%d)\n", variant->power_key_chk());

	while (1) {
		/* wait for power button release */
		if (!variant->power_key_chk()) {
#ifdef CONFIG_EXYNOS_ACPM
			exynos_acpm_reboot();
#endif
			dbg_snapshot_scratch_clear();
			dev_emerg(exynos_reboot.dev, "Set PS_HOLD Low.\n");
			val = readl((void *)((long)exynos_reboot.reg_base + variant->ps_hold_reg));
			writel(val & ~(1 << variant->ps_hold_data_bit),
					(void *)((long)exynos_reboot.reg_base + variant->ps_hold_reg));

			++poweroff_try;
			dev_emerg(exynos_reboot.dev, "Should not reach here! (poweroff_try:%d)\n", poweroff_try);
		} else {
			/* if power button is not released, wait and check TA again */
			dev_info(exynos_reboot.dev, "PowerButton is not released.\n");
		}
		mdelay(1000);
	}
}

static void exynos_restart_v1(enum reboot_mode mode, const char *cmd)
{
	const struct exynos_reboot_variant *variant = exynos_reboot.variant;
	u32 reboot_mode = REBOOT_MODE_NORMAL;
	u32 soc_id, revision;

#ifdef CONFIG_EXYNOS_ACPM
	exynos_acpm_reboot();
#endif
	dev_info(exynos_reboot.dev, "reboot command: %s\n", cmd);
	if (cmd) {
		if (!strcmp(cmd, "charge"))
			reboot_mode = REBOOT_MODE_CHARGE;
		else if (!strcmp(cmd, "bootloader") || !strcmp(cmd, "bl") ||
				!strcmp(cmd, "fastboot") || !strcmp(cmd, "fb"))
			reboot_mode = REBOOT_MODE_FASTBOOT;
		else if (!strcmp(cmd, "recovery"))
			reboot_mode = REBOOT_MODE_RECOVERY;
		else if (!strcmp(cmd, "sfactory"))
			reboot_mode = REBOOT_MODE_FACTORY;
	}
	writel(reboot_mode, (void *)((long)exynos_reboot.reg_base + variant->reboot_mode_reg));

#ifdef CONFIG_EXYNOS_REBOOT_USB_RECOVERY
	/* Set reboot usb booting mode and Do WDT0 right now */
	if (cmd) {
		if (!strcmp(cmd, "usbrecovery")) {
			dev_emerg(exynos_reboot.dev, "Exynos WDT for USB Boot right now\n");
			exynos_pmu_update(EXYNOS_PMU_DREXCAL7, 0x01, 0x01);
			s3c2410wdt_set_emergency_reset(1, 0);
			while (1);
		}
	}
#endif

	if (variant->reset_control)
		variant->reset_control();
	/* Check by each SoC */
	soc_id = exynos_soc_info.product_id;
	revision = exynos_soc_info.revision;
	dev_info(exynos_reboot.dev, "SOC ID %X. Revision: %x\n", soc_id, revision);

	/* Do S/W Reset */
	dev_emerg(exynos_reboot.dev, "Exynos SoC reset right now\n");
	writel(1 << variant->swreset_bit, (void *)((long)exynos_reboot.reg_base + variant->swreset_reg));

	/* Wait for S/W reset */
	dbg_snapshot_spin_func();
}

static ssize_t reset_reason_show(struct device *dev,
			         struct device_attribute *attr, char *buf)
{
	return snprintf(buf, SZ_32, "%s\n", exynos_reboot.reset_reason);
}
DEVICE_ATTR_RO(reset_reason);

static struct attribute *exynos_reboot_attrs[] = {
	&dev_attr_reset_reason.attr,
	NULL,
};

ATTRIBUTE_GROUPS(exynos_reboot);

static int exynos_reboot_probe(struct platform_device *pdev)
{
	struct resource *res;

	dev_set_socdata(&pdev->dev, "Exynos", "Reboot");

	exynos_reboot.dev = &pdev->dev;
	exynos_reboot.variant = exynos_reboot_get_variant(pdev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	exynos_reboot.reg_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (IS_ERR(exynos_reboot.reg_base)) {
		dev_err(&pdev->dev, "failed ioremap\n");
		return -EINVAL;
	}

	if (!exynos_reboot.variant) {
		dev_err(&pdev->dev, "variant is null\n");
		return -ENODEV;
	}

	if (exynos_reboot.variant->reboot) {
		arm_pm_restart = exynos_reboot.variant->reboot;
		dev_info(&pdev->dev, "Success to register arm_pm_restart\n");
	}

	if (exynos_reboot.variant->power_off) {
		pm_power_off = exynos_reboot.variant->power_off;
		dev_info(&pdev->dev, "Success to register pm_power_off\n");
	}

	if (sysfs_create_groups(&pdev->dev.kobj, exynos_reboot_groups)) {
		dev_err(&pdev->dev, "failed create sysfs\n");
		return -ENOMEM;
	} else {
		dev_info(&pdev->dev, "Success to register sysfs\n");
	}

	return 0;
}

static const struct exynos_reboot_variant drv_data_v1 = {
	.ps_hold_reg = PS_HOLD_CONTROL,
	.ps_hold_data_bit = 8,
	.swreset_reg = SYSTEM_CONFIGURATION,
	.swreset_bit = 1,
	.reboot_mode_reg = EXYNOS_PMU_SYSIP_DAT0,
	.reboot = exynos_restart_v1,
	.power_off = exynos_power_off_v1,
	.power_key_chk = exynos_power_key_pressed_chk,
};

static const struct of_device_id exynos_reboot_match[] = {
	{
		.compatible = "exynos,reboot-v1",
		.data = &drv_data_v1,
	},
	{},
};

static const struct platform_device_id exynos_reboot_ids[] = {
	{
		.name = "exynos-reboot",
		.driver_data = (unsigned long)&drv_data_v1,
	},
	{},
};

static struct platform_driver exynos_reboot_driver = {
	.driver = {
		.name = "exynos-reboot",
		.owner = THIS_MODULE,
		.of_match_table	= of_match_ptr(exynos_reboot_match),
		.groups = exynos_reboot_groups,
	},
	.probe		= exynos_reboot_probe,
	.id_table	= exynos_reboot_ids,
};

static int __init reset_reason_setup(char *str)
{
	strncpy(exynos_reboot.reset_reason, str, strlen(str));

	return 1;
}
__setup("androidboot.bootreason=", reset_reason_setup);

static int __init exynos_reboot_init(void)
{
	return platform_driver_register(&exynos_reboot_driver);
}
subsys_initcall(exynos_reboot_init);
