/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Device Tree binding constants for emulator clock controller.
 */

#ifndef _DT_BINDINGS_CLOCK_EXYNOS_9630_H
#define _DT_BINDINGS_CLOCK_EXYNOS_9630_H

#define NONE                                    (0 + 0)
#define OSCCLK                                  (0 + 1)

/* NUMBER FOR APM DRIVER STARTS FROM 10 */
#define CLK_APM_BASE				(10)
#define GATE_APM_CMU_APM_QCH			(CLK_APM_BASE + 0)
#define GATE_DBGCORE_UART_QCH			(CLK_APM_BASE + 1)
#define GATE_GREBEINTEGRATION_QCH_GREBE		(CLK_APM_BASE + 2)
#define GATE_GREBEINTEGRATION_QCH_DBG		(CLK_APM_BASE + 3)
#define GATE_I3C_APM_PMIC_QCH_P			(CLK_APM_BASE + 4)
#define GATE_I3C_APM_PMIC_QCH_S			(CLK_APM_BASE + 5)
#define GATE_INTMEM_QCH				(CLK_APM_BASE + 6)
#define GATE_MAILBOX_APM_AP_QCH			(CLK_APM_BASE + 7)
#define GATE_MAILBOX_APM_CHUB_QCH		(CLK_APM_BASE + 8)
#define GATE_MAILBOX_APM_CP_QCH			(CLK_APM_BASE + 9)
#define GATE_MAILBOX_APM_GNSS_QCH		(CLK_APM_BASE + 10)
#define GATE_MAILBOX_APM_VTS_QCH 		(CLK_APM_BASE + 11)
#define GATE_MAILBOX_APM_WLBT_QCH		(CLK_APM_BASE + 12)
#define GATE_MAILBOX_AP_CHUB_QCH		(CLK_APM_BASE + 13)
#define GATE_MAILBOX_AP_CP_QCH			(CLK_APM_BASE + 14)
#define GATE_MAILBOX_AP_CP_S_QCH		(CLK_APM_BASE + 15)
#define GATE_MAILBOX_AP_DBGCORE_QCH		(CLK_APM_BASE + 16)
#define GATE_MAILBOX_AP_GNSS_QCH		(CLK_APM_BASE + 17)
#define GATE_MAILBOX_AP_WLBT_QCH		(CLK_APM_BASE + 18)
#define GATE_MAILBOX_CP_CHUB_QCH		(CLK_APM_BASE + 19)
#define GATE_MAILBOX_CP_GNSS_QCH		(CLK_APM_BASE + 20)
#define GATE_MAILBOX_CP_WLBT_QCH		(CLK_APM_BASE + 21)
#define GATE_MAILBOX_GNSS_CHUB_QCH		(CLK_APM_BASE + 22)
#define GATE_MAILBOX_GNSS_WLBT_QCH		(CLK_APM_BASE + 23)
#define GATE_MAILBOX_VTS_CHUB_QCH		(CLK_APM_BASE + 24)
#define GATE_MAILBOX_WLBT_ABOX_QCH		(CLK_APM_BASE + 25)
#define GATE_MAILBOX_WLBT_CHUB_QCH		(CLK_APM_BASE + 26)
#define GATE_SS_DBGCORE_QCH_GREBE		(CLK_APM_BASE + 27)
#define GATE_SS_DBGCORE_QCH_DBG			(CLK_APM_BASE + 28)
#define GATE_WDT_APM_QCH			(CLK_APM_BASE + 29)

#define CLK_ABOX_BASE				(50)
#define GATE_ABOX_QCH_ACLK			(CLK_ABOX_BASE + 0)
#define GATE_ABOX_QCH_BCLK_DSIF			(CLK_ABOX_BASE + 1)
#define GATE_ABOX_QCH_BCLK0			(CLK_ABOX_BASE + 2)
#define GATE_ABOX_QCH_BCLK1			(CLK_ABOX_BASE + 3)
#define GATE_ABOX_QCH_BCLK2			(CLK_ABOX_BASE + 4)
#define GATE_ABOX_QCH_BCLK3			(CLK_ABOX_BASE + 5)
#define GATE_ABOX_QCH_CPU			(CLK_ABOX_BASE + 6)
#define GATE_ABOX_QCH_BCLK4			(CLK_ABOX_BASE + 7)
#define GATE_ABOX_QCH_CNT			(CLK_ABOX_BASE + 8)
#define GATE_ABOX_QCH_CCLK_ASB			(CLK_ABOX_BASE + 9)
#define GATE_ABOX_QCH_FM			(CLK_ABOX_BASE + 10)
#define GATE_ABOX_QCH_BCLK5			(CLK_ABOX_BASE + 11)
#define GATE_ABOX_QCH_BCLK6			(CLK_ABOX_BASE + 12)
#define GATE_ABOX_QCH_SCLK			(CLK_ABOX_BASE + 13)
#define GATE_DFTMUX_AUD_QCH			(CLK_ABOX_BASE + 14)
#define GATE_MAILBOX_AUD0_QCH			(CLK_ABOX_BASE + 15)
#define GATE_MAILBOX_AUD1_QCH			(CLK_ABOX_BASE + 16)
#define GATE_SYSMMU_AUD_QCH_S1			(CLK_ABOX_BASE + 17)
#define GATE_SYSMMU_AUD_QCH_S2			(CLK_ABOX_BASE + 18)
#define GATE_WDT_AUD_QCH			(CLK_ABOX_BASE + 19)
#define DOUT_CLK_AUD_AUDIF			(CLK_ABOX_BASE + 20)
#define DOUT_CLK_AUD_CPU_PCLKDBG		(CLK_ABOX_BASE + 21)
#define DOUT_CLK_AUD_FM_SPDY			(CLK_ABOX_BASE + 22)
#define DOUT_CLK_AUD_UAIF0			(CLK_ABOX_BASE + 23)
#define DOUT_CLK_AUD_UAIF1			(CLK_ABOX_BASE + 24)
#define DOUT_CLK_AUD_UAIF2			(CLK_ABOX_BASE + 25)
#define DOUT_CLK_AUD_UAIF3			(CLK_ABOX_BASE + 26)
#define DOUT_CLK_AUD_CPU_ACLK			(CLK_ABOX_BASE + 27)
#define DOUT_CLK_AUD_BUS			(CLK_ABOX_BASE + 28)
#define DOUT_CLK_AUD_BUSP			(CLK_ABOX_BASE + 29)
#define DOUT_CLK_AUD_CNT			(CLK_ABOX_BASE + 30)
#define DOUT_CLK_AUD_UAIF4			(CLK_ABOX_BASE + 31)
#define DOUT_CLK_AUD_DSIF			(CLK_ABOX_BASE + 32)
#define DOUT_CLK_AUD_FM				(CLK_ABOX_BASE + 33)
#define DOUT_CLK_AUD_UAIF5			(CLK_ABOX_BASE + 34)
#define DOUT_CLK_AUD_UAIF6			(CLK_ABOX_BASE + 35)
#define DOUT_CLK_AUD_SCLK			(CLK_ABOX_BASE + 36)
#define DOUT_CLK_AUD_MCLK			(CLK_ABOX_BASE + 37)
#define PLL_OUT_AUD				(CLK_ABOX_BASE + 38)

#define CLK_BUSC_BASE				(100)
#define GATE_CMU_BUSC_CMUREF_QCH		(CLK_BUSC_BASE + 0)
#define GATE_PDMA_BUSC_QCH			(CLK_BUSC_BASE + 1)
#define GATE_SPDMA_BUSC_QCH			(CLK_BUSC_BASE + 2)
#define GATE_SYSMMU_AXI_D_BUSC_QCH		(CLK_BUSC_BASE + 3)

#define CLK_CHUB_BASE				(110)
#define GATE_CM4_CHUB_QCH_CPU			(CLK_CHUB_BASE + 0)
#define GATE_I2C_CHUB0_QCH			(CLK_CHUB_BASE + 1)
#define GATE_I2C_CHUB1_QCH			(CLK_CHUB_BASE + 2)
#define GATE_I2C_CHUB2_QCH			(CLK_CHUB_BASE + 3)
#define GATE_PDMA_CHUB_QCH			(CLK_CHUB_BASE + 4)
#define GATE_PWM_CHUB_QCH			(CLK_CHUB_BASE + 5)
#define GATE_SWEEPER_D_CHUB_QCH			(CLK_CHUB_BASE + 6)
#define GATE_TIMER_CHUB_QCH			(CLK_CHUB_BASE + 7)
#define GATE_USI_CHUB0_QCH			(CLK_CHUB_BASE + 8)
#define GATE_USI_CHUB1_QCH			(CLK_CHUB_BASE + 9)
#define GATE_USI_CHUB2_QCH			(CLK_CHUB_BASE + 10)
#define GATE_WDT_CHUB_QCH			(CLK_CHUB_BASE + 11)
#define DOUT_CLK_CHUB_USI0			(CLK_CHUB_BASE + 12)
#define DOUT_CLK_CHUB_USI1			(CLK_CHUB_BASE + 13)
#define DOUT_CLK_CHUB_USI2			(CLK_CHUB_BASE + 14)
#define DOUT_CLK_CHUB_I2C			(CLK_CHUB_BASE + 15)
#define DOUT_CLK_CHUB_BUS			(CLK_CHUB_BASE + 16)
#define	UMUX_CLK_CHUB_BUS			(CLK_CHUB_BASE + 17)

#define CLK_CMGP_BASE				(150)
#define GATE_ADC_CMGP_QCH_S0			(CLK_CMGP_BASE + 0)
#define GATE_ADC_CMGP_QCH_S1			(CLK_CMGP_BASE + 1)
#define GATE_GPIO_CMGP_QCH			(CLK_CMGP_BASE + 2)
#define GATE_I2C_CMGP0_QCH			(CLK_CMGP_BASE + 3)
#define GATE_I2C_CMGP1_QCH			(CLK_CMGP_BASE + 4)
#define GATE_I2C_CMGP2_QCH			(CLK_CMGP_BASE + 5)
#define GATE_I2C_CMGP3_QCH			(CLK_CMGP_BASE + 6)
#define GATE_I3C_CMGP_QCH			(CLK_CMGP_BASE + 7)
#define GATE_USI_CMGP0_QCH			(CLK_CMGP_BASE + 8)
#define GATE_USI_CMGP1_QCH			(CLK_CMGP_BASE + 9)
#define GATE_USI_CMGP2_QCH			(CLK_CMGP_BASE + 10)
#define GATE_USI_CMGP3_QCH			(CLK_CMGP_BASE + 11)
#define DOUT_CLK_USI_CMGP0			(CLK_CMGP_BASE + 12)
#define DOUT_CLK_USI_CMGP1			(CLK_CMGP_BASE + 13)
#define DOUT_CLK_USI_CMGP2			(CLK_CMGP_BASE + 14)
#define DOUT_CLK_USI_CMGP3			(CLK_CMGP_BASE + 15)
#define DOUT_CLK_I2C_CMGP			(CLK_CMGP_BASE + 16)
#define DOUT_CLK_I3C_CMGP			(CLK_CMGP_BASE + 17)

#define CLK_TOP_BASE				(180)
#define GATE_DFTMUX_TOP_QCH_CIS_CLK0		(CLK_TOP_BASE + 0)
#define GATE_DFTMUX_TOP_QCH_CIS_CLK1		(CLK_TOP_BASE + 1)
#define GATE_DFTMUX_TOP_QCH_CIS_CLK2		(CLK_TOP_BASE + 2)
#define GATE_DFTMUX_TOP_QCH_CIS_CLK3		(CLK_TOP_BASE + 3)
#define GATE_DFTMUX_TOP_QCH_CIS_CLK4		(CLK_TOP_BASE + 4)
#define GATE_OTP_QCH				(CLK_TOP_BASE + 5)
#define CIS_CLK0				(CLK_TOP_BASE + 6)
#define CIS_CLK1				(CLK_TOP_BASE + 7)
#define CIS_CLK2				(CLK_TOP_BASE + 8)
#define CIS_CLK3				(CLK_TOP_BASE + 9)
#define CIS_CLK4				(CLK_TOP_BASE + 10)

#define CLK_CORE_BASE				(200)
#define GATE_BDU_QCH				(CLK_CORE_BASE + 0)
#define GATE_CCI_550_QCH			(CLK_CORE_BASE + 1)
#define GATE_CMU_CORE_CMUREF_QCH		(CLK_CORE_BASE + 2)
#define GATE_CORE_CMU_CORE_QCH			(CLK_CORE_BASE + 3)
#define GATE_DIT_QCH				(CLK_CORE_BASE + 4)
#define GATE_GIC_QCH				(CLK_CORE_BASE + 5)
#define GATE_PUF_QCH				(CLK_CORE_BASE + 6)
#define GATE_RTIC_QCH				(CLK_CORE_BASE + 7)
#define GATE_SIREX_QCH				(CLK_CORE_BASE + 8)
#define GATE_SSS_QCH				(CLK_CORE_BASE + 9)
#define GATE_SYSMMU_ACEL_D_DIT_QCH		(CLK_CORE_BASE + 10)
#define GATE_SYSMMU_AXI_D_CORE1_QCH		(CLK_CORE_BASE + 11)
#define GATE_BPS_CPUCL0_QCH			(CLK_CORE_BASE + 12)
#define GATE_BUSIF_HPMCPUCL0_QCH		(CLK_CORE_BASE + 13)
#define GATE_CLUSTER0_QCH_SCLK			(CLK_CORE_BASE + 14)
#define GATE_CLUSTER0_QCH_ATCLK			(CLK_CORE_BASE + 15)
#define GATE_CLUSTER0_QCH_PDBGCLK		(CLK_CORE_BASE + 16)
#define GATE_CLUSTER0_QCH_GIC			(CLK_CORE_BASE + 17)
#define GATE_CLUSTER0_QCH_DBG_PD		(CLK_CORE_BASE + 18)
#define GATE_CLUSTER0_QCH_PCLK			(CLK_CORE_BASE + 19)
#define GATE_CLUSTER0_QCH_PERIPHCLK		(CLK_CORE_BASE + 20)
#define GATE_CMU_CPUCL0_CMUREF_QCH		(CLK_CORE_BASE + 21)
#define GATE_CMU_CPUCL0_SHORTSTOP_QCH		(CLK_CORE_BASE + 22)
#define GATE_CPUCL0_CMU_CPUCL0_QCH		(CLK_CORE_BASE + 23)
#define GATE_CSSYS_QCH				(CLK_CORE_BASE + 24)
#define GATE_CMU_CPUCL1_CMUREF_QCH		(CLK_CORE_BASE + 25)
#define GATE_CPUCL1_QCH_BIG			(CLK_CORE_BASE + 26)

#define CLK_CSIS_BASE				(250)
#define GATE_CSIS_PDP_QCH_C2_CSIS		(CLK_CSIS_BASE + 0)
#define GATE_CSIS_PDP_QCH_CSIS0			(CLK_CSIS_BASE + 1)
#define GATE_CSIS_PDP_QCH_CSIS1			(CLK_CSIS_BASE + 2)
#define GATE_CSIS_PDP_QCH_CSIS2			(CLK_CSIS_BASE + 3)
#define GATE_CSIS_PDP_QCH_CSIS3			(CLK_CSIS_BASE + 4)
#define GATE_CSIS_PDP_QCH_CSIS4			(CLK_CSIS_BASE + 5)
#define GATE_CSIS_PDP_QCH_CSIS_DMA		(CLK_CSIS_BASE + 6)
#define GATE_CSIS_PDP_QCH_PDP_TOP		(CLK_CSIS_BASE + 7)
#define GATE_OIS_MCU_TOP_QCH_A			(CLK_CSIS_BASE + 8)
#define GATE_SYSMMU_D0_CSIS_QCH_S1		(CLK_CSIS_BASE + 9)
#define GATE_SYSMMU_D0_CSIS_QCH_S2		(CLK_CSIS_BASE + 10)
#define GATE_SYSMMU_D1_CSIS_QCH_S1		(CLK_CSIS_BASE + 11)
#define GATE_SYSMMU_D1_CSIS_QCH_S2		(CLK_CSIS_BASE + 12)

#define CLK_DNC_BASE				(280)
#define GATE_IP_DSPC_QCH			(CLK_DNC_BASE + 0)
#define GATE_IP_NPUC_QCH_ACLK			(CLK_DNC_BASE + 1)
#define GATE_IP_NPUC_QCH_PCLK			(CLK_DNC_BASE + 2)
#define GATE_SYSMMU_DNC0_QCH_S1			(CLK_DNC_BASE + 3)
#define GATE_SYSMMU_DNC0_QCH_S2			(CLK_DNC_BASE + 4)
#define GATE_SYSMMU_DNC1_QCH_S1			(CLK_DNC_BASE + 5)
#define GATE_SYSMMU_DNC1_QCH_S2			(CLK_DNC_BASE + 6)
#define GATE_SYSMMU_DNC2_QCH_S1			(CLK_DNC_BASE + 7)
#define GATE_SYSMMU_DNC2_QCH_S2			(CLK_DNC_BASE + 8)

#define CLK_DNS_BASE				(300)
#define GATE_DNS_QCH				(CLK_DNS_BASE + 0)
#define GATE_SYSMMU_DNS_QCH_S1			(CLK_DNS_BASE + 1)
#define GATE_SYSMMU_DNS_QCH_S2			(CLK_DNS_BASE + 2)

#define CLK_DPU_BASE				(310)
#define UMUX_CLKCMU_DPU_BUS			(CLK_DPU_BASE + 0)
#define GATE_DPU_QCH_DPU			(CLK_DPU_BASE + 1)
#define GATE_DPU_QCH_DPU_DMA			(CLK_DPU_BASE + 2)
#define GATE_DPU_QCH_DPU_DPP			(CLK_DPU_BASE + 3)
#define GATE_SYSMMU_DPU_QCH_S1			(CLK_DPU_BASE + 4)
#define GATE_SYSMMU_DPU_QCH_S2			(CLK_DPU_BASE + 5)

#define CLK_DSP_BASE				(320)
#define GATE_IP_DSP_QCH				(CLK_DSP_BASE + 0)

#define CLK_G2D_BASE				(330)
#define GATE_G2D_QCH				(CLK_G2D_BASE + 0)
#define GATE_JPEG_QCH				(CLK_G2D_BASE + 1)
#define GATE_M2M_QCH				(CLK_G2D_BASE + 2)
#define GATE_SYSMMU_D0_G2D_QCH_S1		(CLK_G2D_BASE + 3)
#define GATE_SYSMMU_D0_G2D_QCH_S2		(CLK_G2D_BASE + 4)
#define GATE_SYSMMU_D1_G2D_QCH_S1		(CLK_G2D_BASE + 5)
#define GATE_SYSMMU_D1_G2D_QCH_S2		(CLK_G2D_BASE + 6)

#define CLK_G3D_BASE				(350)
#define GATE_BUSIF_HPMG3D_QCH			(CLK_G3D_BASE + 0)
#define GATE_GPU_QCH				(CLK_G3D_BASE + 1)
#define GATE_SYSMMU_D0_G3D_QCH			(CLK_G3D_BASE + 2)
#define GATE_SYSMMU_D1_G3D_QCH			(CLK_G3D_BASE + 3)

#define CLK_HSI_BASE				(370)
#define GATE_GPIO_HSI_QCH			(CLK_HSI_BASE + 0)
#define GATE_HSI_CMU_HSI_QCH			(CLK_HSI_BASE + 1)
#define GATE_MMC_EMBD_QCH			(CLK_HSI_BASE + 2)
#define GATE_S2MPU_D_HSI_QCH			(CLK_HSI_BASE + 3)
#define GATE_UFS_EMBD_QCH			(CLK_HSI_BASE + 4)
#define GATE_UFS_EMBD_QCH_FMP			(CLK_HSI_BASE + 5)
#define MMC_EMBD				(CLK_HSI_BASE + 6)
#define UFS_EMBD				(CLK_HSI_BASE + 7)

#define CLK_IPP_BASE				(390)
#define GATE_SIPU_IPP_QCH			(CLK_IPP_BASE + 0)
#define GATE_SIPU_IPP_QCH_C2_STAT		(CLK_IPP_BASE + 1)
#define GATE_SIPU_IPP_QCH_C2_YDS		(CLK_IPP_BASE + 2)
#define GATE_SYSMMU_IPP_QCH_S1			(CLK_IPP_BASE + 3)
#define GATE_SYSMMU_IPP_QCH_S2			(CLK_IPP_BASE + 4)

#define CLK_ITP_BASE				(400)
#define GATE_ITP_QCH				(CLK_ITP_BASE + 0)

#define CLK_MCSC_BASE				(410)
#define GATE_C2AGENT_D0_MCSC_QCH		(CLK_MCSC_BASE + 0)
#define GATE_C2AGENT_D1_MCSC_QCH		(CLK_MCSC_BASE + 1)
#define GATE_C2AGENT_D2_MCSC_QCH		(CLK_MCSC_BASE + 2)
#define GATE_GDC_QCH				(CLK_MCSC_BASE + 3)
#define GATE_GDC_QCH_C2				(CLK_MCSC_BASE + 4)
#define GATE_MCSC_QCH				(CLK_MCSC_BASE + 5)
#define GATE_MCSC_QCH_C2			(CLK_MCSC_BASE + 6)
#define GATE_MCSC_CMU_MCSC_QCH			(CLK_MCSC_BASE + 7)
#define GATE_SYSMMU_D0_MCSC_QCH_S1		(CLK_MCSC_BASE + 8)
#define GATE_SYSMMU_D0_MCSC_QCH_S2		(CLK_MCSC_BASE + 9)
#define GATE_SYSMMU_D1_MCSC_QCH_S1		(CLK_MCSC_BASE + 10)
#define GATE_SYSMMU_D1_MCSC_QCH_S2		(CLK_MCSC_BASE + 11)

#define CLK_MFC_BASE				(450)
#define GATE_MFC_QCH				(CLK_MFC_BASE + 0)
#define GATE_SYSMMU_MFCD0_QCH_S1		(CLK_MFC_BASE + 1)
#define GATE_SYSMMU_MFCD0_QCH_S2		(CLK_MFC_BASE + 2)
#define GATE_SYSMMU_MFCD1_QCH_S1		(CLK_MFC_BASE + 3)
#define GATE_SYSMMU_MFCD1_QCH_S2		(CLK_MFC_BASE + 4)
#define GATE_WFD_QCH				(CLK_MFC_BASE + 5)

#define CLK_MIF_BASE				(470)
#define GATE_CMU_MIF_CMUREF_QCH			(CLK_MIF_BASE + 0)
#define GATE_DMC_QCH				(CLK_MIF_BASE + 1)

#define CLK_NPU_BASE				(500)
#define GATE_NPUD_UNIT0_QCH			(CLK_NPU_BASE + 0)
#define GATE_NPUD_UNIT1_QCH			(CLK_NPU_BASE + 1)

#define CLK_PERI_BASE				(510)
#define UMUX_CLKCMU_PERI_BUS_USER		(CLK_PERI_BASE + 0)
#define UMUX_CLKCMU_PERI_UART_DBG		(CLK_PERI_BASE + 1)
#define GATE_BC_EMUL_QCH			(CLK_PERI_BASE + 2)
#define GATE_GPIO_MMCCARD_QCH			(CLK_PERI_BASE + 3)
#define GATE_GPIO_PERI_QCH			(CLK_PERI_BASE + 4)
#define GATE_I2C_OIS_QCH			(CLK_PERI_BASE + 5)
#define GATE_MCT_QCH				(CLK_PERI_BASE + 6)
#define GATE_MMC_CARD_QCH			(CLK_PERI_BASE + 7)
#define GATE_OTP_CON_BIRA_QCH			(CLK_PERI_BASE + 8)
#define GATE_OTP_CON_TOP_QCH			(CLK_PERI_BASE + 9)
#define GATE_PWM_QCH				(CLK_PERI_BASE + 10)
#define GATE_S2MPU_D_PERI_QCH			(CLK_PERI_BASE + 11)
#define GATE_SPI_OIS_QCH			(CLK_PERI_BASE + 12)
#define GATE_TMU_QCH				(CLK_PERI_BASE + 13)
#define GATE_UART_DBG_QCH			(CLK_PERI_BASE + 14)
#define GATE_USI00_I2C_QCH			(CLK_PERI_BASE + 15)
#define GATE_USI00_USI_QCH			(CLK_PERI_BASE + 16)
#define GATE_USI01_I2C_QCH			(CLK_PERI_BASE + 17)
#define GATE_USI01_USI_QCH			(CLK_PERI_BASE + 18)
#define GATE_USI02_I2C_QCH			(CLK_PERI_BASE + 19)
#define GATE_USI02_USI_QCH			(CLK_PERI_BASE + 20)
#define GATE_USI03_I2C_QCH			(CLK_PERI_BASE + 21)
#define GATE_USI03_USI_QCH			(CLK_PERI_BASE + 22)
#define GATE_USI04_I2C_QCH			(CLK_PERI_BASE + 23)
#define GATE_USI04_USI_QCH			(CLK_PERI_BASE + 24)
#define GATE_USI05_I2C_QCH			(CLK_PERI_BASE + 25)
#define GATE_USI05_USI_QCH			(CLK_PERI_BASE + 26)
#define GATE_WDT0_QCH				(CLK_PERI_BASE + 27)
#define GATE_WDT1_QCH				(CLK_PERI_BASE + 28)
#define DOUT_CLK_PERI_USI00_USI			(CLK_PERI_BASE + 29)
#define DOUT_CLK_PERI_USI01_USI			(CLK_PERI_BASE + 30)
#define DOUT_CLK_PERI_USI02_USI			(CLK_PERI_BASE + 31)
#define DOUT_CLK_PERI_USI03_USI			(CLK_PERI_BASE + 32)
#define DOUT_CLK_PERI_USI04_USI			(CLK_PERI_BASE + 33)
#define DOUT_CLK_PERI_USI05_USI			(CLK_PERI_BASE + 34)
#define UART_DBG				(CLK_PERI_BASE + 35)
#define DOUT_CLK_PERI_SPI_OIS			(CLK_PERI_BASE + 36)
#define UMUX_CLKCMU_PERI_USI_I2C_USER		(CLK_PERI_BASE + 37)
#define DOUT_CLK_PERI_USI_I2C			(CLK_PERI_BASE + 38)

#define CLK_SSP_BASE				(570)
#define GATE_USS_SSPCORE_QCH			(CLK_SSP_BASE + 0)

#define CLK_TNR_BASE				(580)
#define GATE_ORBMCH_QCH_ACLK			(CLK_TNR_BASE + 0)
#define GATE_ORBMCH_QCH_C2CLK			(CLK_TNR_BASE + 1)
#define GATE_SYSMMU_D0_TNR_QCH_S1		(CLK_TNR_BASE + 2)
#define GATE_SYSMMU_D0_TNR_QCH_S2		(CLK_TNR_BASE + 3)
#define GATE_SYSMMU_D1_TNR_QCH_S1		(CLK_TNR_BASE + 4)
#define GATE_SYSMMU_D1_TNR_QCH_S2		(CLK_TNR_BASE + 5)
#define GATE_TNR_QCH				(CLK_TNR_BASE + 6)

#define CLK_USB_BASE				(600)
#define UMUX_CLKCMU_USB_BUS_USER		(CLK_USB_BASE + 0)
#define UMUX_CLKCMU_USB_USB31DRD_USER		(CLK_USB_BASE + 1)
#define UMUX_CLKCMU_USB_USBDP_DEBUG_USER	(CLK_USB_BASE + 2)
#define UMUX_CLKCMU_USB_DPGTC_USER		(CLK_USB_BASE + 3)
#define GATE_DP_LINK_QCH_PCLK			(CLK_USB_BASE + 4)
#define GATE_DP_LINK_QCH_GTC_CLK		(CLK_USB_BASE + 5)
#define GATE_USB31DRD_QCH_REF			(CLK_USB_BASE + 6)
#define GATE_USB31DRD_QCH_SLV_CTRL		(CLK_USB_BASE + 7)
#define GATE_USB31DRD_QCH_SLV_LINK		(CLK_USB_BASE + 8)
#define GATE_USB31DRD_QCH_APB			(CLK_USB_BASE + 9)
#define GATE_USB31DRD_QCH_PCS			(CLK_USB_BASE + 10)
#define USB31DRD				(CLK_USB_BASE + 11)

#define CLK_VRA_BASE				(630)
#define GATE_SYSMMU_VRA_QCH_S1			(CLK_VRA_BASE + 0)
#define GATE_SYSMMU_VRA_QCH_S2			(CLK_VRA_BASE + 1)
#define GATE_VRA_QCH				(CLK_VRA_BASE + 2)
#define UMUX_CLKCMU_VRA_BUS_USER		(CLK_VRA_BASE + 3)
#define UMUX_CLKCMU_VRA_CLAHE_USER		(CLK_VRA_BASE + 4)
#define GATE_CLAHE_QCH				(CLK_VRA_BASE + 5)
#define DOUT_CLK_VRA_BUSP			(CLK_VRA_BASE + 6)

#define CLK_VTS_BASE				(650)
#define GATE_CORTEXM4INTEGRATION_QCH_CPU	(CLK_VTS_BASE + 0)
#define GATE_DMIC_AHB0_QCH_PCLK			(CLK_VTS_BASE + 1)
#define GATE_DMIC_AHB1_QCH_PCLK			(CLK_VTS_BASE + 2)
#define GATE_DMIC_AHB2_QCH_PCLK			(CLK_VTS_BASE + 3)
#define GATE_DMIC_AHB3_QCH_PCLK			(CLK_VTS_BASE + 4)
#define GATE_DMIC_IF_QCH_PCLK			(CLK_VTS_BASE + 5)
#define GATE_DMIC_IF_QCH_DMIC			(CLK_VTS_BASE + 6)
#define GATE_DMIC_IF_3RD_QCH_PCLK		(CLK_VTS_BASE + 7)
#define GATE_DMIC_IF_3RD_QCH_DMIC		(CLK_VTS_BASE + 8)
#define GATE_GPIO_VTS_QCH			(CLK_VTS_BASE + 9)
#define GATE_HWACG_SYS_DMIC0_QCH		(CLK_VTS_BASE + 10)
#define GATE_HWACG_SYS_DMIC1_QCH		(CLK_VTS_BASE + 11)
#define GATE_HWACG_SYS_DMIC2_QCH		(CLK_VTS_BASE + 12)
#define GATE_HWACG_SYS_DMIC3_QCH		(CLK_VTS_BASE + 13)
#define GATE_MAILBOX_ABOX_VTS_QCH		(CLK_VTS_BASE + 14)
#define GATE_MAILBOX_AP_VTS_QCH			(CLK_VTS_BASE + 15)
#define GATE_SS_VTS_GLUE_QCH_DMIC_IF_PAD	(CLK_VTS_BASE + 16)
#define GATE_SS_VTS_GLUE_QCH_DMIC_3RD_IF_PAD	(CLK_VTS_BASE + 17)
#define GATE_SWEEPER_C_VTS_QCH			(CLK_VTS_BASE + 18)
#define GATE_TIMER_QCH				(CLK_VTS_BASE + 19)
#define GATE_WDT_VTS_QCH			(CLK_VTS_BASE + 20)

#define CLK_CLKOUT_BASE				(700)
#define	OSC_NFC					(CLK_CLKOUT_BASE + 0)
#define	OSC_AUD					(CLK_CLKOUT_BASE + 1)

/* must be greater than maximal clock id */
#define CLK_NR_CLKS				(800)

#define ACPM_DVFS_MIF				(0x0B040000)
#define ACPM_DVFS_INT				(0x0B040001)
#define ACPM_DVFS_CPUCL0			(0x0B040002)
#define ACPM_DVFS_CPUCL1			(0x0B040003)
#define ACPM_DVFS_MFC				(0x0B040004)
#define ACPM_DVFS_NPU				(0x0B040005)
#define ACPM_DVFS_DISP				(0x0B040006)
#define ACPM_DVFS_DSP				(0x0B040007)
#define ACPM_DVFS_AUD				(0x0B040008)
#define ACPM_DVS_CP				(0x0B040009)
#define ACPM_DVFS_G3D				(0x0B04000A)
#define ACPM_DVFS_INTCAM			(0x0B04000B)
#define ACPM_DVFS_CAM				(0x0B04000C)
#define ACPM_DVFS_TNR				(0x0B04000D)
#define ACPM_DVFS_DNC				(0x0B04000E)

#define EWF_CMU_APM				(0)
#define EWF_CMU_AUD				(1)
#define EWF_CMU_BUSC				(3)
#define EWF_CMU_VRA2				(4)
#define EWF_CMU_CMGP				(5)
#define EWF_CMU_CORE				(6)
#define EWF_CMU_CPUCL0				(7)
#define EWF_CMU_CPUCL1				(8)
#define EWF_CMU_CPUCL2				(9)
#define EWF_CMU_NPU0				(10)
#define EWF_CMU_NPU1				(11)
#define EWF_CMU_DPU				(12)
#define EWF_CMU_DSPM				(13)
#define EWF_CMU_DSPS				(14)
#define EWF_CMU_FSYS0				(15)
#define EWF_CMU_FSYS1				(16)
#define EWF_CMU_G2D				(17)
#define EWF_CMU_G3D				(18)
#define EWF_CMU_ISPHQ				(19)
#define EWF_CMU_ISPLP				(20)
#define EWF_CMU_ISPPRE				(21)
#define EWF_CMU_IVA				(22)
#define EWF_CMU_MFC				(23)
#define EWF_CMU_MIF0				(24)
#define EWF_CMU_MIF1				(25)
#define EWF_CMU_MIF2				(26)
#define EWF_CMU_MIF3				(27)
#define EWF_CMU_PERIC0				(28)
#define EWF_CMU_PERIC1				(29)
#define EWF_CMU_PERIS				(30)
#define EWF_CMU_VTS				(31)

#endif	/* _DT_BINDINGS_CLOCK_EXYNOS_9630_H */