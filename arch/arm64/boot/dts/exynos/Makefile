# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_ARCH_EXYNOS) += \
	exynos5433-tm2.dtb	\
	exynos5433-tm2e.dtb	\
	exynos7-espresso.dtb

dtb-$(CONFIG_SOC_EXYNOS9610) += exynos9610.dtb \
				exynos9610-erd9610.dtbo

dtb-$(CONFIG_SOC_EXYNOS9630) += exynos9630.dtb \
				exynos9630-smdk9630.dtbo \
				exynos9630-erd9630.dtbo \

dtb-$(CONFIG_SOC_EXYNOS3830) += exynos3830.dtb \
				exynos3830-smdk3830.dtbo \
				exynos3830-erd3830_r00.dtbo \
				exynos3830-erd3830_r01.dtbo \
				exynos3830-erd3830_r02.dtbo \
				exynos3830-erd3830_r03.dtbo \
				exynos3830-erd3830_r04.dtbo \

dtb-$(CONFIG_SOC_EXYNOS3830) += exynos3830-cramfs-smdk3830.dtb

#dw.choi
dtb-$(CONFIG_SOC_EXYNOS3830) += exynos3830-rp400_evm.dtbo \
				exynos3830-rp400_ws.dtbo \
				exynos3830-rp400_es.dtbo

always		:= $(dtb-y) $(dtbo-y)
subdir-y	:= $(dts-dirs)
clean-files	:= *.dtb *.dtbo
