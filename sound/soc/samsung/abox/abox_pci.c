/* sound/soc/samsung/abox_v2/abox_pci.c
 *
 * ALSA SoC Audio Layer - Samsung Abox PCI driver
 *
 * Copyright (c) 2019 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* #define DEBUG */
#include <linux/io.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/iommu.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>
#include <linux/sched/clock.h>
#include <linux/mm_types.h>
#include <asm/cacheflush.h>
#include "abox_pci.h"

static struct reserved_mem *abox_pci_rmem;

static int __init abox_pci_rmem_setup(struct reserved_mem *rmem)
{
	pr_info("%s: base=%pa, size=%pa\n", __func__, &rmem->base, &rmem->size);
	abox_pci_rmem = rmem;
	return 0;
}

RESERVEDMEM_OF_DECLARE(abox_rmem, "exynos,abox_pci_rmem", abox_pci_rmem_setup);

static void *abox_rmem_pci_vmap(struct reserved_mem *rmem)
{
	phys_addr_t phys = rmem->base;
	size_t size = rmem->size;
	unsigned int num_pages = (unsigned int)DIV_ROUND_UP(size, PAGE_SIZE);
	pgprot_t prot = pgprot_writecombine(PAGE_KERNEL);
	struct page **pages, **page;
	void *vaddr = NULL;

	pages = kcalloc(num_pages, sizeof(pages[0]), GFP_KERNEL);
	if (!pages)
		goto out;

	for (page = pages; (page - pages < num_pages); page++) {
		*page = phys_to_page(phys);
		phys += PAGE_SIZE;
	}

	vaddr = vmap(pages, num_pages, VM_MAP, prot);
	kfree(pages);
out:
	return vaddr;
}

static int samsung_abox_pci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct abox_pci_data *data;
	int ret = 0;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);
	data->dev = dev;

	data->dev_abox = pdev->dev.parent;
	if (!data->dev_abox) {
		dev_err(dev, "Failed to get abox device\n");
		return -EPROBE_DEFER;
	}
	data->abox_data = dev_get_drvdata(data->dev_abox);

	if (abox_pci_rmem) {
		data->pci_dram_base = abox_rmem_pci_vmap(abox_pci_rmem);
		abox_iommu_map(data->dev_abox, IOVA_VSS_PCI,
				abox_pci_rmem->base, abox_pci_rmem->size,
				data->pci_dram_base);
		memset(data->pci_dram_base, 0x0, abox_pci_rmem->size);
	}

	dev_info(dev, "%s (%d)\n", __func__, __LINE__);
	dev_info(data->dev_abox, "%s (%d)\n", __func__, __LINE__);

	return ret;
}

static int samsung_abox_pci_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "%s\n", __func__);

	return 0;
}

static const struct of_device_id samsung_abox_pci_match[] = {
	{
		.compatible = "samsung,abox-pci",
	},
	{},
};
MODULE_DEVICE_TABLE(of, samsung_abox_pci_match);

static struct platform_driver samsung_abox_pci_driver = {
	.probe  = samsung_abox_pci_probe,
	.remove = samsung_abox_pci_remove,
	.driver = {
		.name = "samsung-abox-pci",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(samsung_abox_pci_match),
	},
};

module_platform_driver(samsung_abox_pci_driver);

MODULE_AUTHOR("Pilsun Jang, <pilsun.jang@samsung.com>");
MODULE_DESCRIPTION("Samsung ASoC A-Box PCI Driver");
MODULE_ALIAS("platform:samsung-abox-pci");
MODULE_LICENSE("GPL");
