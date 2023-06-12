/*
 *  lt9611.c - LT9211 MIPI to HDMI1.4 Converter
 *
 *  Copyright (c) 2020 Han Ye <yanghs0702@thundersoft.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/kernel.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include"lt9611.h"

struct Lontium_IC_Mode lt9611_set = {
	.mipi_port_cnt = single_port_mipi,	//single_port_mipi or dual_port_mipi
	.mipi_lane_cnt = lane_cnt_4,	//mipi_lane_cnt; //1 or 2 or 4
	.mipi_mode = dsi,	//mipi_mode;     //dsi or csi
	.video_mode = Burst_Mode,	//Non_Burst_Mode_with_Sync_Events,
	.audio_out = audio_i2s,	//audio_out      //audio_i2s or audio_spdif
	.hdmi_coupling_mode = dc_mode,	//hdmi_coupling_mode;//ac_mode or dc_mode
	.hdcp_encryption = hdcp_disable,	//hdcp_enable,  //hdcp_encryption //hdcp_enable or hdcp_diable
	.hdmi_mode = HDMI,	//HDMI or DVI
	.input_color_space = RGB888	//RGB888 or YUV422
};

struct lt9611_data *lt9611;
//DTV                                                                         // hfp, hs, hbp,hact,htotal,vfp, vs, vbp,vact,vtotal, hfp, hs, hbp,hact,htotal,vfp, vs, vbp,vact,vtotal, h_polary, v_polary, vic, pclk_khz
struct video_timing video_640x480_60Hz =
    { 16, 96, 48, 640, 800, 10, 2, 33, 480, 525, 0, 0, 1, AR_4_3, 25000 };
struct video_timing video_720x480_60Hz =
    { 16, 62, 60, 720, 858, 9, 6, 30, 480, 525, 0, 0, 2, AR_4_3, 27000 };
struct video_timing video_720x576_50Hz =
    { 12, 64, 68, 720, 864, 5, 5, 39, 576, 625, 0, 0, 17, AR_4_3, 27000 };

struct video_timing video_1280x720_60Hz =
    { 110, 40, 220, 1280, 1650, 5, 5, 20, 720, 750, 1, 1, 4, AR_16_9, 74250 };
struct video_timing video_1280x720_50Hz =
    { 440, 40, 220, 1280, 1980, 5, 5, 20, 720, 750, 1, 1, 19, AR_16_9, 74250 };
struct video_timing video_1280x720_30Hz =
    { 1760, 40, 220, 1280, 3300, 5, 5, 20, 720, 750, 1, 1, 0, AR_16_9, 74250 };

struct video_timing video_1920x1080_60Hz =
    { 88, 44, 148, 1920, 2200, 4, 5, 36, 1080, 1125, 1, 1, 16, AR_16_9,
	148500
};

struct video_timing video_1920x1080_50Hz =
    { 528, 44, 148, 1920, 2640, 4, 5, 36, 1080, 1125, 1, 1, 31, AR_16_9,
	148500
};
struct video_timing video_1920x1080_30Hz =
    { 88, 44, 148, 1920, 2200, 4, 5, 36, 1080, 1125, 1, 1, 34, AR_16_9, 74250 };
struct video_timing video_1920x1080_25Hz =
    { 528, 44, 148, 1920, 2640, 4, 5, 36, 1080, 1125, 1, 1, 33, AR_16_9,
	74250
};

struct video_timing video_1920x1080_24Hz =
    { 638, 44, 148, 1920, 2750, 4, 5, 36, 1080, 1125, 1, 1, 32, AR_16_9,
	74250
};

struct video_timing video_3840x2160_30Hz =
    { 176, 88, 296, 3840, 4400, 8, 10, 72, 2160, 2250, 1, 1, 95, AR_16_9,
	297000
};

struct video_timing video_3840x2160_25Hz =
    { 1056, 88, 296, 3840, 5280, 8, 10, 72, 2160, 2250, 1, 1, 94, AR_16_9,
	297000
};

struct video_timing video_3840x2160_24Hz =
    { 1276, 88, 296, 3840, 5500, 8, 10, 72, 2160, 2250, 1, 1, 93, AR_16_9,
	297000
};

struct video_timing video_wrong =
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

//others
struct video_timing video_1200x1920_60Hz =
    { 180, 60, 160, 1200, 1600, 4, 5, 4, 1920, 1933, 1, 1, 0, AR_16_9,
	191040
};
struct video_timing video_1920x720_60Hz =
    { 88, 44, 148, 1920, 2200, 5, 5, 20, 720, 750, 1, 1, 0, AR_16_9, 100000 };

unsigned char Sink_EDID[256];
bool Tx_HPD = 0;

//enum VideoFormat Video_Format;
struct cec_msg lt9611_cec_msg;

static void lt9611_read_chip_id(struct i2c_client *client)
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	unsigned char chipid[3] = { 0 };

	lt9611_write_reg(client, 0xff, 0x80);
	lt9611_write_reg(client, 0xee, 0x01);
	lt9611_read_reg(client, 0x00, &chipid[0]);
	lt9611_read_reg(client, 0x01, &chipid[1]);
	lt9611_read_reg(client, 0x02, &chipid[2]);
	lt9611_write_reg(client, 0xff, 0x81);
	lt9611_write_reg(client, 0x01, 0x18);
	lt9611_write_reg(client, 0xff, 0x80);
	lt9611->chipid =  (chipid[1] << 8) | chipid[0];
	i2c_set_clientdata(client, lt9611);
	pr_info("chipid = 0x%6x\n", lt9611->chipid);
}

/*
static void lt9611_rst_pd_init(struct i2c_client *client)
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);

	//power consumption for standby
	lt9611_write_reg(client, 0xFF, 0x81);
	lt9611_write_reg(client, 0x02, 0x48);
	lt9611_write_reg(client, 0x23, 0x80);
	lt9611_write_reg(client, 0x30, 0x00);
	lt9611_write_reg(client, 0x01, 0x00);	//i2s stop work
}
*/
static void lt9611_lowpower_mode(struct i2c_client *client, bool on)
{

	/* only hpd irq is working for low power consumption */
	if (on) {
		lt9611_write_reg(client, 0xFF, 0x81);
		lt9611_write_reg(client, 0x02, 0x49);
		lt9611_write_reg(client, 0x23, 0x80);
		lt9611_write_reg(client, 0x30, 0x00);

		lt9611_write_reg(client, 0xff, 0x80);
		lt9611_write_reg(client, 0x11, 0x0a);
		pr_info("LT9611_LowPower_mode: enter low power mode\n");
	} else {
		lt9611_write_reg(client, 0xFF, 0x81);
		lt9611_write_reg(client, 0x02, 0x12);
		lt9611_write_reg(client, 0x23, 0x40);
		lt9611_write_reg(client, 0x30, 0xea);

		lt9611_write_reg(client, 0xff, 0x80);
		lt9611_write_reg(client, 0x11, 0xfa);
		pr_info("LT9611_LowPower_mode: exit low power mode\n");
	}
}

static void lt9611_system_init(struct i2c_client *client)
{

	lt9611_write_reg(client, 0xFF, 0x82);
	lt9611_write_reg(client, 0x51, 0x11);
	//Timer for Frequency meter
	lt9611_write_reg(client, 0xFF, 0x82);
	lt9611_write_reg(client, 0x1b, 0x69);	//Timer 2
	lt9611_write_reg(client, 0x1c, 0x78);
	lt9611_write_reg(client, 0xcb, 0x69);	//Timer 1
	lt9611_write_reg(client, 0xcc, 0x78);

	/*power consumption for work */
	lt9611_write_reg(client, 0xff, 0x80);
	lt9611_write_reg(client, 0x04, 0xf0);
	lt9611_write_reg(client, 0x06, 0xf0);
	lt9611_write_reg(client, 0x0a, 0x80);
	lt9611_write_reg(client, 0x0b, 0x46);	//csc clk
	lt9611_write_reg(client, 0x0d, 0xef);
	lt9611_write_reg(client, 0x11, 0xfa);
}

static void lt9611_mipi_input_analog(struct i2c_client *client)
{

	//mipi mode
	lt9611_write_reg(client, 0xff, 0x81);
	lt9611_write_reg(client, 0x06, 0x60);	//port A rx current
	lt9611_write_reg(client, 0x07, 0x3f);	//eq
	lt9611_write_reg(client, 0x08, 0x3f);	//eq

	lt9611_write_reg(client, 0x0a, 0xfe);	//port A ldo voltage set
	lt9611_write_reg(client, 0x0b, 0xbf);	//enable port A lprx

	lt9611_write_reg(client, 0x11, 0x60);	//port B rx current
	lt9611_write_reg(client, 0x12, 0x3f);	//e
	lt9611_write_reg(client, 0x13, 0x3f);	//eq
	lt9611_write_reg(client, 0x15, 0xfe);	//port B ldo voltage set
	lt9611_write_reg(client, 0x16, 0xbf);	//enable port B lprx

	lt9611_write_reg(client, 0x1c, 0x03);	//PortA clk lane no-LP mode.
	lt9611_write_reg(client, 0x20, 0x03);	//PortB clk lane no-LP mode.
}

static void lt9611_mipi_input_digtal(struct i2c_client *client)
{

	u8 lanes = lt9611_set.mipi_lane_cnt;
	pr_info("lt9611 set mipi lanes = %d\n", lanes);

	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0x4f, 0x80);	//[7] = Select ad_txpll_d_clk.

	//mipi video data output; ml rx A and B; only A with 0x11
	lt9611_write_reg(client, 0x50, 0x10);
	//lt9611_write_reg(client, 0x50, 0x11);

	lt9611_write_reg(client, 0xff, 0x83);
	lt9611_write_reg(client, 0x00, lanes);
	lt9611_write_reg(client, 0x02, 0x08);	//PortA settle
	lt9611_write_reg(client, 0x06, 0x08);	//PortB settle

	if (P10) {
		lt9611_write_reg(client, 0x0a, 0x00);	//1=dual_lr, 0=dual_en
	} else {
		lt9611_write_reg(client, 0x0a, 0x03);	//1=dual_lr, 0=dual_en
	}
	pr_info("lt9611 set mipi ports= %d\n", P10);

#if 1
	if (lt9611_set.mipi_mode == csi) {
		pr_info("\nLT9611_MIPI_Input_Digtal: LT9611.mipi_mode = csi");
		lt9611_write_reg(client, 0xff, 0x83);
		lt9611_write_reg(client, 0x08, 0x10);	//csi_en
		lt9611_write_reg(client, 0x2c, 0x40);	//csi_sel

		if (lt9611_set.input_color_space == RGB888) {
			lt9611_write_reg(client, 0xff, 0x83);
			lt9611_write_reg(client, 0x1c, 0x01);
		}
	} else
		pr_info("\nLT9611_MIPI_Input_Digtal: LT9611.mipi_mode = dsi");
#endif

}

static void lt9611_video_check(struct i2c_client *client)
{
	u8 mipi_video_format, value = 0x00;
	u16 h_act, h_act_a, h_act_b, v_act, v_tal;
	u16 h_total_sysclk;


	lt9611_write_reg(client, 0xff, 0x82);	// top video check module
	lt9611_read_reg(client, 0x86, &value);
	h_total_sysclk = (u16) value;

	lt9611_read_reg(client, 0x87, &value);
	h_total_sysclk = (h_total_sysclk << 8) + value;
	pr_info
	    ("\n-----------------------------------------------------------------------------");
	pr_info("\nLT9611_Video_Check: h_total_sysclk = %d", h_total_sysclk);

	lt9611_read_reg(client, 0x82, &value);
	v_act = (u16) value;
	lt9611_read_reg(client, 0x83, &value);
	v_act = (v_act << 8) + value;

	lt9611_read_reg(client, 0x6c, &value);
	v_tal = (u16) value;
	lt9611_read_reg(client, 0x6d, &value);
	v_tal = (v_tal << 8) + value;

	lt9611_write_reg(client, 0xff, 0x83);
	lt9611_read_reg(client, 0x82, &value);
	h_act_a = (u16) value;
	lt9611_read_reg(client, 0x83, &value);
	h_act_a = (h_act_a << 8) + value;

	lt9611_read_reg(client, 0x86, &value);
	h_act_b = (u16) value;
	lt9611_read_reg(client, 0x87, &value);
	h_act_b = (h_act_b << 8) + value;

	if (lt9611_set.input_color_space == YUV422) {
		pr_info
		    ("\nLT9611_Video_Check: lt9611.input_color_space = YUV422");
		h_act_a /= 2;
		h_act_b /= 2;
	} else if (lt9611_set.input_color_space == RGB888) {
		pr_info
		    ("\nLT9611_Video_Check: lt9611.input_color_space = RGB888");
		h_act_a /= 3;
		h_act_b /= 3;
	}

	lt9611_read_reg(client, 0x88, &mipi_video_format);

	pr_info
	    ("\nLT9611_Video_Check: h_act_a, h_act_b, v_act, v_tal: %d, %d, %d, %d, ",
	     h_act_a, h_act_b, v_act, v_tal);
	pr_info("\nLT9611_Video_Check: mipi_video_format: 0x%x",
		mipi_video_format);

	if (0)			//(P10 == 0) //dual port.
		h_act = h_act_a + h_act_b;
	else
		h_act = h_act_a;

	pr_info("\r\nLT9611_Video_Check: Video_Format =");
	///////////////////////formate detect///////////////////////////////////

	//DTV
	if ((h_act == video_640x480_60Hz.hact)
	    && (v_act == video_640x480_60Hz.vact)) {
		pr_info(" video_640x480_60Hz ");
		lt9611->videoformat = video_640x480_60Hz_vic1;
		lt9611->video = &video_640x480_60Hz;
	}

	else if ((h_act == (video_720x480_60Hz.hact))
		 && (v_act == video_720x480_60Hz.vact)) {
		pr_info(" video_720x480_60Hz ");
		lt9611->videoformat = video_720x480_60Hz_vic3;
		lt9611->video = &video_720x480_60Hz;
	}

	else if ((h_act == (video_720x576_50Hz.hact))
		 && (v_act == video_720x576_50Hz.vact)) {
		pr_info(" video_720x576_50Hz ");
		lt9611->videoformat = video_720x576_50Hz_vic;
		lt9611->video = &video_720x576_50Hz;
	}

	else if ((h_act == video_1280x720_60Hz.hact)
		 && (v_act == video_1280x720_60Hz.vact)) {
		if (h_total_sysclk < 630) {
			pr_info(" video_1280x720_60Hz ");
			lt9611->videoformat = video_1280x720_60Hz_vic4;
			lt9611->video = &video_1280x720_60Hz;

		} else if (h_total_sysclk < 750) {
			pr_info(" video_1280x720_50Hz ");
			lt9611->videoformat = video_1280x720_50Hz_vic;
			lt9611->video = &video_1280x720_50Hz;

		} else if (h_total_sysclk < 1230) {
			pr_info(" video_1280x720_30Hz ");
			lt9611->videoformat = video_1280x720_30Hz_vic;
			lt9611->video = &video_1280x720_30Hz;
		}
	}

	else if ((h_act == video_1920x1080_60Hz.hact) && (v_act == video_1920x1080_60Hz.vact))	//1080P
	{
		if (h_total_sysclk < 430) {
			pr_info(" video_1920x1080_60Hz ");
			lt9611->videoformat = video_1920x1080_60Hz_vic16;
			lt9611->video = &video_1920x1080_60Hz;
		}

		else if (h_total_sysclk < 510) {
			pr_info(" video_1920x1080_50Hz ");
			lt9611->videoformat = video_1920x1080_50Hz_vic;
			lt9611->video = &video_1920x1080_50Hz;
		}

		else if (h_total_sysclk < 830) {
			pr_info(" video_1920x1080_30Hz ");
			lt9611->videoformat = video_1920x1080_30Hz_vic;
			lt9611->video = &video_1920x1080_30Hz;
		}

		else if (h_total_sysclk < 980) {
			pr_info(" video_1920x1080_25Hz ");
			lt9611->videoformat = video_1920x1080_25Hz_vic;
			lt9611->video = &video_1920x1080_25Hz;
		}

		else if (h_total_sysclk < 1030) {
			pr_info(" video_1920x1080_24Hz ");
			lt9611->videoformat = video_1920x1080_24Hz_vic;
			lt9611->video = &video_1920x1080_24Hz;
		}
	} else if ((h_act == video_3840x2160_30Hz.hact) && (v_act == video_3840x2160_30Hz.vact))	//2160P
	{
		if (h_total_sysclk < 430) {
			pr_info(" video_3840x2160_30Hz ");
			lt9611->videoformat = video_3840x2160_30Hz_vic;
			lt9611->video = &video_3840x2160_30Hz;
		} else if (h_total_sysclk < 490) {
			pr_info(" video_3840x2160_25Hz ");
			lt9611->videoformat = video_3840x2160_25Hz_vic;
			lt9611->video = &video_3840x2160_25Hz;
		} else if (h_total_sysclk < 520) {
			pr_info(" video_3840x2160_24Hz ");
			lt9611->videoformat = video_3840x2160_24Hz_vic;
			lt9611->video = &video_3840x2160_24Hz;
		}
	}
	//DMT
	else if ((h_act == video_1200x1920_60Hz.hact) && (v_act == video_1200x1920_60Hz.vact))	//&&
	{
		pr_info(" video_1200x1920_60Hz ");
		lt9611->videoformat = video_other;
		lt9611->video = &video_1200x1920_60Hz;
	} else if ((h_act == video_1920x720_60Hz.hact) && (v_act == video_1920x720_60Hz.vact))	//&&
	{
		pr_info(" video_1920x720_60Hz ");
		lt9611->videoformat = video_other;
		lt9611->video = &video_1920x720_60Hz;
	} else {
		lt9611->videoformat = video_none;
		lt9611->video = &video_1200x1920_60Hz;
		pr_info(" unknown video format ");
	}
	pr_info("\n-----------------------------------------------");


	i2c_set_clientdata(client, lt9611);
}

static void lt9611_mipi_video_timing(struct i2c_client *client,
				     struct video_timing *video_format)
{
	lt9611_write_reg(client, 0xff, 0x83);
	lt9611_write_reg(client, 0x0d, (u8) (video_format->vtotal / 256));
	lt9611_write_reg(client, 0x0e, (u8) (video_format->vtotal % 256));	//vtotal
	lt9611_write_reg(client, 0x0f, (u8) (video_format->vact / 256));
	lt9611_write_reg(client, 0x10, (u8) (video_format->vact % 256));	//vactive
	lt9611_write_reg(client, 0x11, (u8) (video_format->htotal / 256));
	lt9611_write_reg(client, 0x12, (u8) (video_format->htotal % 256));	//htotal
	lt9611_write_reg(client, 0x13, (u8) (video_format->hact / 256));
	lt9611_write_reg(client, 0x14, (u8) (video_format->hact % 256));	//hactive
	lt9611_write_reg(client, 0x15, (u8) (video_format->vs % 256));	//vsa
	lt9611_write_reg(client, 0x16, (u8) (video_format->hs % 256));	//hsa
	lt9611_write_reg(client, 0x17, (u8) (video_format->vfp % 256));	//vfp
	lt9611_write_reg(client, 0x18, (u8) ((video_format->vs + video_format->vbp) % 256));	//vss
	lt9611_write_reg(client, 0x19, (u8) (video_format->hfp % 256));	//hfp
	lt9611_write_reg
	    (client, 0x1a,
	     (u8) (((video_format->hfp / 256) << 4) +
		   (video_format->hs + video_format->hbp) / 256));
	lt9611_write_reg(client, 0x1b, (u8) ((video_format->hs + video_format->hbp) % 256));	//hss
}

static void lt9611_mipi_pcr(struct i2c_client *client,
			    struct video_timing *video_format)
{
	u8 pol;
	u16 hact;

	pr_info("%s enter ++", __func__);
	hact = video_format->hact;
	pol = (video_format->h_polarity) * 0x02 + (video_format->v_polarity);
	pol = ~pol;
	pol &= 0x03;
	//print("\r\n pol = %x, %x",pol,pol);
	lt9611_write_reg(client, 0xff, 0x83);

	if (P10 == 0)		//dual port.
	{
		hact = (hact >> 2);
		hact += 0x50;
		hact = (0x3e0 > hact ? hact : 0x3e0);

		lt9611_write_reg(client, 0x0b, (u8) (hact >> 8));	//vsync mode
		lt9611_write_reg(client, 0x0c, (u8) hact);	//=1/4 hact
		//hact -=0x40;
		lt9611_write_reg(client, 0x48, (u8) (hact >> 8));	//de mode delay
		lt9611_write_reg(client, 0x49, (u8) (hact));	//
	} else {
		lt9611_write_reg(client, 0x0b, 0x01);	//vsync read delay(reference value)
		lt9611_write_reg(client, 0x0c, 0x10);	//

		lt9611_write_reg(client, 0x48, 0x00);	//de mode delay
		lt9611_write_reg(client, 0x49, 0x81);	//=1/4 hact
	}

	/* stage 1 */
	lt9611_write_reg(client, 0x21, 0x4a);	//bit[3:0] step[11:8]
	//lt9611_write_reg(0x22,0x40);//step[7:0]

	lt9611_write_reg(client, 0x24, 0x71);	//bit[7:4]v/h/de mode; line for clk stb[11:8]
	lt9611_write_reg(client, 0x25, 0x30);	//line for clk stb[7:0]

	lt9611_write_reg(client, 0x2a, 0x01);	//clk stable in

	/* stage 2 */
	lt9611_write_reg(client, 0x4a, 0x40);	//offset //0x10
	lt9611_write_reg(client, 0x1d, (0x10 | pol));	//PCR de mode step setting.

	/* MK limit */
	switch (lt9611->videoformat) {
	case video_3840x1080_60Hz_vic:
	case video_3840x2160_30Hz_vic:
	case video_3840x2160_25Hz_vic:
	case video_3840x2160_24Hz_vic:
	case video_2560x1600_60Hz_vic:
	case video_2560x1440_60Hz_vic:
	case video_2560x1080_60Hz_vic:
		break;
	case video_1920x1080_60Hz_vic16:
	case video_1920x1080_30Hz_vic:
	case video_1280x720_60Hz_vic4:
	case video_1280x720_30Hz_vic:
		break;
	case video_720x480_60Hz_vic3:
	case video_640x480_60Hz_vic1:
		lt9611_write_reg(client, 0xff, 0x83);
		lt9611_write_reg(client, 0x0b, 0x02);
		lt9611_write_reg(client, 0x0c, 0x40);	//MIPI read delay setting
		lt9611_write_reg(client, 0x48, 0x01);
		lt9611_write_reg(client, 0x49, 0x10);	//PCR de mode delay.
		lt9611_write_reg(client, 0x24, 0x70);	//PCR vsync hsync de mode enable
		lt9611_write_reg(client, 0x25, 0x80);	//PCR line limit for clock stable
		lt9611_write_reg(client, 0x2a, 0x10);	//PCR clock stable in setting.
		lt9611_write_reg(client, 0x2b, 0x80);	//PCR clock stable out setting.
		lt9611_write_reg(client, 0x23, 0x28);	//PCR capital data when clock stable.
		lt9611_write_reg(client, 0x4a, 0x10);	//PCR de mode delay offset.
		lt9611_write_reg(client, 0x1d, 0xf3);	//PCR de mode step setting 0xf;
		//Mipi hsync vsync Nagetive.Mipi de pol plus.
		pr_info("lt9611_mipi_pcr: 640x480_60Hz\n");
		break;

	case video_540x960_60Hz_vic:
	case video_1024x600_60Hz_vic:
		lt9611_write_reg(client, 0x24, 0x70);	//bit[7:4]v/h/de mode;line for clk stb[11:8]
		lt9611_write_reg(client, 0x25, 0x80);	//line for clk stb[7:0]
		lt9611_write_reg(client, 0x2a, 0x10);	//clk stable in

		/* stage 2 */
		//lt9611_write_reg(0x23,0x04); //pcr h mode step
		//lt9611_write_reg(0x4a,0x10); //offset //0x10
		lt9611_write_reg(client, 0x1d, 0xf0);	//PCR de mode step setting.
		break;

	default:
		pr_info("lt9611_mipi_pcr is none!!!!!!!!\n");
		break;
	}

	lt9611_mipi_video_timing(client, lt9611->video);

	lt9611_write_reg(client, 0xff, 0x83);
	lt9611_write_reg(client, 0x26, lt9611->pcr_m);

	lt9611_write_reg(client, 0xff, 0x80);
	lt9611_write_reg(client, 0x11, 0x5a);	//Pcr reset
	lt9611_write_reg(client, 0x11, 0xfa);

	pr_info("%s enter --", __func__);
}

/*In first part, most regiter information not appear at regiter list.*/
static void lt9611_pll(struct i2c_client *client,
		       struct video_timing *video_format)
{
	u32 pclk;
	u8 pll_lock_flag, cal_done_flag, band_out = 0;
	u8 hdmi_post_div;
	u8 i;

	pclk = video_format->pclk_khz;
	pr_info("lt9611_pll: set rx pll = %ld\n", pclk);

	lt9611_write_reg(client, 0xff, 0x81);
	lt9611_write_reg(client, 0x23, 0x40);
	lt9611_write_reg(client, 0x24, 0x62);	//0x62, LG25UM58 issue, 20180824
	lt9611_write_reg(client, 0x25, 0x80);	//pre-divider
	lt9611_write_reg(client, 0x26, 0x55);
	lt9611_write_reg(client, 0x2c, 0x37);
	//lt9611_write_reg(0x2d,0x99); //txpll_divx_set&da_txpll_freq_set
	//lt9611_write_reg(0x2e,0x01);
	lt9611_write_reg(client, 0x2f, 0x01);
	//lt9611_write_reg(client, 0x26, 0x55);
	lt9611_write_reg(client, 0x27, 0x66);
	lt9611_write_reg(client, 0x28, 0x88);

	lt9611_write_reg(client, 0x2a, 0x20);	//for U3.
	if (pclk > 150000) {
		lt9611_write_reg(client, 0x2d, 0x88);
		hdmi_post_div = 0x01;
	} else if (pclk > 80000) {
		lt9611_write_reg(client, 0x2d, 0x99);
		hdmi_post_div = 0x02;
	} else {
		lt9611_write_reg(client, 0x2d, 0xaa);	//0xaa
		hdmi_post_div = 0x04;
	}
	lt9611->pcr_m = (u8) ((pclk * 5 * hdmi_post_div) / 27000);
	lt9611->pcr_m--;
	pr_info("\r\nLT9611_PLL: pcr_m = 0x%x, hdmi_post_div = %d", lt9611->pcr_m, hdmi_post_div);	//Hex

	lt9611_write_reg(client, 0xff, 0x83);
	lt9611_write_reg(client, 0x2d, 0x40);	//M up limit
	lt9611_write_reg(client, 0x31, 0x08);	//M down limit
	lt9611_write_reg(client, 0x26, 0x80 | (lt9611->pcr_m));	/* fixed M is to let pll locked */

	pclk = pclk / 2;
	lt9611_write_reg(client, 0xff, 0x82);	//13.5M
	lt9611_write_reg(client, 0xe3, pclk / 65536);
	pclk = pclk % 65536;
	lt9611_write_reg(client, 0xe4, pclk / 256);
	lt9611_write_reg(client, 0xe5, pclk % 256);

	lt9611_write_reg(client, 0xde, 0x20);	// pll cal en, start calibration
	lt9611_write_reg(client, 0xde, 0xe0);

	lt9611_write_reg(client, 0xff, 0x80);
	lt9611_write_reg(client, 0x11, 0x5a);	/* Pcr clk reset */
	lt9611_write_reg(client, 0x11, 0xfa);
	lt9611_write_reg(client, 0x16, 0xf2);	/* pll cal digital reset */
	lt9611_write_reg(client, 0x18, 0xdc);	/* pll analog reset */
	lt9611_write_reg(client, 0x18, 0xfc);
	lt9611_write_reg(client, 0x16, 0xf3);	/*start calibration */

	/* pll lock status */
	for (i = 0; i < 6; i++) {
		lt9611_write_reg(client, 0xff, 0x80);
		lt9611_write_reg(client, 0x16, 0xe3);	/* pll lock logic reset */
		lt9611_write_reg(client, 0x16, 0xf3);
		lt9611_write_reg(client, 0xff, 0x82);
		lt9611_read_reg(client, 0xe7, &cal_done_flag);
		lt9611_read_reg(client, 0xe6, &band_out);
		lt9611_read_reg(client, 0x15, &pll_lock_flag);

		if ((pll_lock_flag & 0x80) && (cal_done_flag & 0x80)
		    && (band_out != 0xff)) {
			pr_info
			    ("\r\nLT9611_PLL: HDMI pll locked,band out: 0x%x",
			     band_out);
			break;
		} else {
			lt9611_write_reg(client, 0xff, 0x80);
			lt9611_write_reg(client, 0x11, 0x5a);	/* Pcr clk reset */
			lt9611_write_reg(client, 0x11, 0xfa);
			lt9611_write_reg(client, 0x16, 0xf2);	/* pll cal digital reset */
			lt9611_write_reg(client, 0x18, 0xdc);	/* pll analog reset */
			lt9611_write_reg(client, 0x18, 0xfc);
			lt9611_write_reg(client, 0x16, 0xf3);	/*start calibration */
			pr_info("\r\nLT9611_PLL: HDMI pll unlocked, reset pll");
		}
	}

	i2c_set_clientdata(client, lt9611);
}

static void lt9611_hdmi_tx_phy(struct i2c_client *client)
{
	lt9611_write_reg(client, 0xff, 0x81);
	lt9611_write_reg(client, 0x30, 0x6a);
	if (lt9611_set.hdmi_coupling_mode == ac_mode) {
		lt9611_write_reg(client, 0x31, 0x73);	//DC: 0x44, AC:0x73
		pr_info("lt9611_hdmi_tx_phy: AC couple\n");
	} else {
		lt9611_write_reg(client, 0x31, 0x44);
		pr_info("lt9611_hdmi_tx_phy: DC couple\n");
	}

	lt9611_write_reg(client, 0x32, 0x4a);
	lt9611_write_reg(client, 0x33, 0x0b);
	lt9611_write_reg(client, 0x34, 0x00);
	lt9611_write_reg(client, 0x35, 0x00);
	lt9611_write_reg(client, 0x36, 0x00);
	lt9611_write_reg(client, 0x37, 0x44);
	lt9611_write_reg(client, 0x3f, 0x0f);
	lt9611_write_reg(client, 0x40, 0x98);	//clk swing
	lt9611_write_reg(client, 0x41, 0x98);	//D0 swing
	lt9611_write_reg(client, 0x42, 0x98);	//D1 swing
	lt9611_write_reg(client, 0x43, 0x98);	//D2 swing
	lt9611_write_reg(client, 0x44, 0x0a);
}

static void LT9611_HDCP_Init(struct i2c_client *client)	//luodexing
{
	lt9611_write_reg(client, 0xff, 0x85);
	lt9611_write_reg(client, 0x07, 0x1f);
	lt9611_write_reg(client, 0x13, 0xfe);
	lt9611_write_reg(client, 0x17, 0x0f);
	lt9611_write_reg(client, 0x15, 0x05);
}

static void LT9611_HDCP_Enable(struct i2c_client *client)	//luodexing
{
	lt9611_write_reg(client, 0xff, 0x80);
	lt9611_write_reg(client, 0x14, 0x7f);
	lt9611_write_reg(client, 0x14, 0xff);
	lt9611_write_reg(client, 0xff, 0x85);
	lt9611_write_reg(client, 0x15, 0x01);	//disable HDCP
	lt9611_write_reg(client, 0x15, 0x71);	//enable HDCP
	lt9611_write_reg(client, 0x15, 0x65);	//enable HDCP

	pr_info("\r\nLT9611_HDCP_Enable: On");
}

static void LT9611_HDCP_Disable(struct i2c_client *client)	//luodexing
{
	lt9611_write_reg(client, 0xff, 0x85);
	lt9611_write_reg(client, 0x15, 0x45);	//enable HDCP
}

static void lt9611_hdmi_out_enable(struct i2c_client *client)
{
	lt9611_write_reg(client, 0xff, 0x81);
	lt9611_write_reg(client, 0x23, 0x40);

	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0xde, 0x20);
	lt9611_write_reg(client, 0xde, 0xe0);

	lt9611_write_reg(client, 0xff, 0x80);
	lt9611_write_reg(client, 0x18, 0xdc);	/* txpll sw rst */
	lt9611_write_reg(client, 0x18, 0xfc);
	lt9611_write_reg(client, 0x16, 0xf1);	/* txpll calibration rest */
	lt9611_write_reg(client, 0x16, 0xf3);

	lt9611_write_reg(client, 0x11, 0x5a);	//Pcr reset
	lt9611_write_reg(client, 0x11, 0xfa);

	lt9611_write_reg(client, 0xff, 0x81);
	lt9611_write_reg(client, 0x30, 0xea);
	if (lt9611_set.hdcp_encryption == hdcp_enable) {
		LT9611_HDCP_Enable(client);
	} else {
		LT9611_HDCP_Disable(client);
	}

	pr_info("lt9611_hdmi_out_enable\n");

}

static void lt9611_hdmi_out_disable(struct i2c_client *client)
{
	lt9611_write_reg(client, 0xff, 0x81);
	lt9611_write_reg(client, 0x30, 0x00);	/* Txphy PD */
	lt9611_write_reg(client, 0x23, 0x80);	/* Txpll PD */
	LT9611_HDCP_Disable(client);
	pr_info("LT9611_HDMI_Out_Disable\n");
}

static void lt9611_hdmi_tx_digital(struct i2c_client *client,
				   struct video_timing *video_format)
{
	//struct lt9611_data *lt9611 = i2c_get_clientdata(client);

	u8 vic = video_format->vic;
	u8 AR = video_format->aspact_ratio;
	u8 pb0, pb2, pb4;
	u8 infoFrame_en;

	infoFrame_en = (AIF_PKT_EN | AVI_PKT_EN | SPD_PKT_EN);
	//pr_info("\r\nlt9611_hdmi_tx_digital: AR = %d",AR);
	pb2 = (AR << 4) + 0x08;
	pb4 = vic;

	pb0 = ((pb2 + pb4) <= 0x5f) ? (0x5f - pb2 - pb4) : (0x15f - pb2 - pb4);

	lt9611_write_reg(client, 0xff, 0x82);
	if (lt9611_set.hdmi_mode == HDMI) {
		lt9611_write_reg(client, 0xd6, 0x8e);	//sync polarity
		pr_info("lt9611_hdmi_tx_digital: HMDI mode = HDMI\n");
	} else if (lt9611_set.hdmi_mode == DVI) {
		lt9611_write_reg(client, 0xd6, 0x0e);	//sync polarity
		pr_info("lt9611_hdmi_tx_digital: HMDI mode = DVI\n");
	}
	if (lt9611_set.audio_out == audio_i2s) {
		lt9611_write_reg(client, 0xd7, 0x04);
	}

	if (lt9611_set.audio_out == audio_spdif) {
		lt9611_write_reg(client, 0xd7, 0x80);
	}
	//AVI
	lt9611_write_reg(client, 0xff, 0x84);
	lt9611_write_reg(client, 0x43, pb0);	//AVI_PB0

	//lt9611_write_reg(client,0x44,0x10);//AVI_PB1
	lt9611_write_reg(client, 0x45, pb2);	//AVI_PB2
	lt9611_write_reg(client, 0x47, pb4);	//AVI_PB4

	lt9611_write_reg(client, 0xff, 0x84);
	lt9611_write_reg(client, 0x10, 0x02);	//data iland
	lt9611_write_reg(client, 0x12, 0x64);	//act_h_blank

	//VS_IF, 4k 30hz need send VS_IF packet.
	if (vic == 95) {
		lt9611_write_reg(client, 0xff, 0x84);
		lt9611_write_reg(client, 0x3d, infoFrame_en | UD0_PKT_EN);	//UD1 infoframe enable //revise on 20200715

		lt9611_write_reg(client, 0x74, 0x81);	//HB0
		lt9611_write_reg(client, 0x75, 0x01);	//HB1
		lt9611_write_reg(client, 0x76, 0x05);	//HB2
		lt9611_write_reg(client, 0x77, 0x49);	//PB0
		lt9611_write_reg(client, 0x78, 0x03);	//PB1
		lt9611_write_reg(client, 0x79, 0x0c);	//PB2
		lt9611_write_reg(client, 0x7a, 0x00);	//PB3
		lt9611_write_reg(client, 0x7b, 0x20);	//PB4
		lt9611_write_reg(client, 0x7c, 0x01);	//PB5
	} else {
		lt9611_write_reg(client, 0xff, 0x84);
		lt9611_write_reg(client, 0x3d, infoFrame_en);	//UD1 infoframe enable
	}

	if (infoFrame_en & SPD_PKT_EN) {
		lt9611_write_reg(client, 0xff, 0x84);
		lt9611_write_reg(client, 0xc0, 0x83);	//HB0
		lt9611_write_reg(client, 0xc1, 0x01);	//HB1
		lt9611_write_reg(client, 0xc2, 0x19);	//HB2

		lt9611_write_reg(client, 0xc3, 0x00);	//PB0
		lt9611_write_reg(client, 0xc4, 0x01);	//PB1
		lt9611_write_reg(client, 0xc5, 0x02);	//PB2
		lt9611_write_reg(client, 0xc6, 0x03);	//PB3
		lt9611_write_reg(client, 0xc7, 0x04);	//PB4
		lt9611_write_reg(client, 0xc8, 0x00);	//PB5
	}
}

static void lt9611_csc(struct i2c_client *client)
{
	if (lt9611_set.input_color_space == YUV422) {
		lt9611_write_reg(client, 0xff, 0x82);
		lt9611_write_reg(client, 0xb9, 0x18);
		pr_info("lt9611_csc: Ypbpr 422 to RGB888\n");
	}
}

static void lt9611_audio_init(struct i2c_client *client)
{

	if (lt9611_set.audio_out == audio_i2s) {
		pr_info("Audio inut = I2S 2ch\n");
		lt9611_write_reg(client, 0xff, 0x84);
		lt9611_write_reg(client, 0x06, 0x08);
		lt9611_write_reg(client, 0x07, 0x10);

		//48K sampling frequency
		lt9611_write_reg(client, 0x0f, 0x2b);	//0x2b: 48K, 0xab:96K
		lt9611_write_reg(client, 0x34, 0xd4);	//CTS_N 20180823 0xd5: sclk = 32fs, 0xd4: sclk = 64fs

		lt9611_write_reg(client, 0x35, 0x00);	// N value = 6144
		lt9611_write_reg(client, 0x36, 0x18);
		lt9611_write_reg(client, 0x37, 0x00);

		//96K sampling frequency
		//lt9611_write_reg(0x0f,0xab); //0x2b: 48K, 0xab:96K
		//lt9611_write_reg(0x34,0xd4); //CTS_N 20180823 0xd5: sclk = 32fs, 0xd4: sclk = 64fs

		//lt9611_write_reg(0x35,0x00); // N value = 12288
		//lt9611_write_reg(0x36,0x30);
		//lt9611_write_reg(0x37,0x00);
	}

	if (lt9611_set.audio_out == audio_spdif) {
		pr_info("Audio inut = SPDIF\n");

		lt9611_write_reg(client, 0xff, 0x84);
		lt9611_write_reg(client, 0x06, 0x0c);
		lt9611_write_reg(client, 0x07, 0x10);

		lt9611_write_reg(client, 0x34, 0xd4);	//CTS_N
	}
}

#if 0
static void lt9611_read_edid(struct i2c_client *client)
{
#ifdef _enable_read_edid_
	u8 i, j, edid_data, value = 0;
	u8 extended_flag = 00;
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);

	memset(Sink_EDID, 0, sizeof Sink_EDID);

	lt9611_write_reg(client, 0xff, 0x85);
	//lt9611_write_reg(client, 0x02,0x0a); //I2C 100K
	lt9611_write_reg(client, 0x03, 0xc9);
	lt9611_write_reg(client, 0x04, 0xa0);	//0xA0 is EDID device address
	lt9611_write_reg(client, 0x05, 0x00);	//0x00 is EDID offset address
	lt9611_write_reg(client, 0x06, 0x20);	//length for read
	lt9611_write_reg(client, 0x14, 0x7f);

	for (i = 0; i < 8; i++)	// block 0 & 1
	{
		lt9611_write_reg(client, 0x05, i * 32);	//0x00 is EDID offset address
		lt9611_write_reg(client, 0x07, 0x36);
		lt9611_write_reg(client, 0x07, 0x34);	//0x31
		lt9611_write_reg(client, 0x07, 0x37);	//0x37
		mdelay(5);	// wait 5ms for reading edid data.
		lt9611_read_reg(client, 0x40, &value);
		if (value & 0x02)	//KEY_DDC_ACCS_DONE=1
		{
			lt9611_read_reg(client, 0x40, &value);
			if (value & 0x50)	//DDC No Ack or Abitration lost
			{
				pr_info("\r\nread edid failed: no ack");
				goto end;
			} else {
				pr_info("\r\n");
				for (j = 0; j < 32; j++) {
					lt9611_read_reg(client, 0x83,
							&edid_data);
					Sink_EDID[i * 32 + j] = edid_data;	// write edid data to Sink_EDID[];
					if ((i == 3) && (j == 30))
						extended_flag =
						    edid_data & 0x03;

					pr_info("%02x,", edid_data);
				}
				if (i == 3) {
					if (extended_flag < 1)	//no block 1, stop reading edid.
						goto end;
				}
			}
		} else {
			pr_info("\r\nread edid failed: accs not done");
			goto end;
		}
	}

	if (extended_flag < 2)	//no block 2, stop reading edid.
		goto end;

	for (i = 0; i < 8; i++)	//  // block 2 & 3
	{
		lt9611_write_reg(client, 0x05, i * 32);	//0x00 is EDID offset address
		lt9611_write_reg(client, 0x07, 0x76);	//0x31
		lt9611_write_reg(client, 0x07, 0x74);	//0x31
		lt9611_write_reg(client, 0x07, 0x77);	//0x37
		mdelay(5);	// wait 5ms for reading edid data.
		lt9611_read_reg(client, 0x40, &value);
		if (value & 0x02)	//KEY_DDC_ACCS_DONE=1
		{
			lt9611_read_reg(client, 0x40, &value);
			if (value & 0x50)	//DDC No Ack or Abitration lost
			{
				pr_info("\r\nread edid failed: no ack");
				goto end;
			} else {
				pr_info("\r\n");
				for (j = 0; j < 32; j++) {
					lt9611_read_reg(client, 0x83,
							&edid_data);
					//Sink_EDID[256+i*32+j]= edid_data; // write edid data to Sink_EDID[];
					pr_info("%02x,", edid_data);
				}
				if (i == 3) {
					if (extended_flag < 3)	//no block 1, stop reading edid.
					{
						goto end;
					}
				}
			}
		} else {
			pr_info("\r\nread edid failed: accs not done");
			goto end;
		}
	}
end:
	lt9611_write_reg(client, 0x03, 0xc2);
	lt9611_write_reg(client, 0x07, 0x1f);

#endif
}
#endif

void LT9611_load_hdcp_key(struct i2c_client *client)	//luodexing
{
	u8 value = 00;

	lt9611_write_reg(client, 0xff, 0x85);
	lt9611_write_reg(client, 0x00, 0x85);
	//lt9611_write_reg(client,0x02,0x0a); //I2C 100K
	lt9611_write_reg(client, 0x03, 0xc0);
	lt9611_write_reg(client, 0x03, 0xc3);
	lt9611_write_reg(client, 0x04, 0xa0);	//0xA0 is eeprom device address
	lt9611_write_reg(client, 0x05, 0x00);	//0x00 is eeprom offset address
	lt9611_write_reg(client, 0x06, 0x20);	//length for read
	lt9611_write_reg(client, 0x14, 0xff);

	lt9611_write_reg(client, 0x07, 0x11);	//0x31
	lt9611_write_reg(client, 0x07, 0x17);	//0x37
	mdelay(50);		// wait 5ms for loading key.

	//pr_info("\r\nLT9611_load_hdcp_key: 0x%02x",HDMI_ReadI2C_Byte(0x40));
	lt9611_read_reg(client, 0x40, &value);
	if ((value & 0x81) == 0x81)
		pr_info("\r\nLT9611_load_hdcp_key: external key valid");
	else
		pr_info
		    ("\r\nLT9611_load_hdcp_key: external key unvalid, using internal test key!");

	lt9611_write_reg(client, 0x03, 0xc2);
	lt9611_write_reg(client, 0x07, 0x1f);
}

static bool lt9611_get_hpd_status(struct i2c_client *client)
{
	u8 value;

	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_read_reg(client, 0x5e, &value);

	if (value & 0x04) {
		//tx_hpd=1;
		return 1;
	} else {
		//tx_hpd=0;
		return 0;
	}
}

static void lt9611_hdmi_cec_on(struct i2c_client *client, bool enable)
{
	if (enable) {
		/* cec init */
		lt9611_write_reg(client, 0xff, 0x80);
		lt9611_write_reg(client, 0x0d, 0xff);
		lt9611_write_reg(client, 0x15, 0xf1);	//reset cec logic
		lt9611_write_reg(client, 0x15, 0xf9);
		lt9611_write_reg(client, 0xff, 0x86);
		lt9611_write_reg(client, 0xfe, 0xa5);	//clk div
	} else {
		lt9611_write_reg(client, 0xff, 0x80);
		lt9611_write_reg(client, 0x15, 0xf1);
	}
}

static void lt9611_cec_logical_reset(struct i2c_client *client)
{
	lt9611_write_reg(client, 0xff, 0x80);
	lt9611_write_reg(client, 0x15, 0xf1);	//reset cec logic
	lt9611_write_reg(client, 0x15, 0xf9);
}

static void lt9611_cec_set_logical_address(struct i2c_client *client,
					   struct cec_msg *cec_msg)
{
	//struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	u8 logical_address;

/*
    0xf8, 0xf7   //Register
    0x00, 0x01,  //LA 0
    0x00, 0x02,  //LA 1
    0x00, 0x03,  //LA 2
    0x00, 0x04,  //LA 3
    0x00, 0x10,  //LA 4
    0x00, 0x20,  //LA 5
    0x00, 0x30,  //LA 6
    0x00, 0x40,  //LA 7
    0x01, 0x00,  //LA 8
    0x02, 0x00,  //LA 9
    0x03, 0x00,  //LA 10
    0x04, 0x00,  //LA 11
    0x10, 0x00,  //LA 12
    0x20, 0x00,  //LA 13
    0x30, 0x00,  //LA 14
    0x40, 0x00	 //LA 15
*/
	if (!cec_msg->la_allocation_done)
		logical_address = 15;
	else
		logical_address = cec_msg->logical_address;

	if (logical_address > 15) {
		pr_info("\n LA error!");
		return;
	}

	lt9611_write_reg(client, 0xff, 0x86);
	switch (logical_address) {
	case 0:
		lt9611_write_reg(client, 0xf7, 0x01);
		lt9611_write_reg(client, 0xf8, 0x00);
		break;

	case 1:
		lt9611_write_reg(client, 0xf7, 0x02);
		lt9611_write_reg(client, 0xf8, 0x00);
		break;

	case 2:
		lt9611_write_reg(client, 0xf7, 0x03);
		lt9611_write_reg(client, 0xf8, 0x00);
		break;

	case 3:
		lt9611_write_reg(client, 0xf7, 0x04);
		lt9611_write_reg(client, 0xf8, 0x00);
		break;

	case 4:
		lt9611_write_reg(client, 0xf7, 0x10);
		lt9611_write_reg(client, 0xf8, 0x00);
		break;

	case 5:
		lt9611_write_reg(client, 0xf7, 0x20);
		lt9611_write_reg(client, 0xf8, 0x00);
		break;

	case 6:
		lt9611_write_reg(client, 0xf7, 0x30);
		lt9611_write_reg(client, 0xf8, 0x00);
		break;

	case 7:
		lt9611_write_reg(client, 0xf7, 0x40);
		lt9611_write_reg(client, 0xf8, 0x00);
		break;

	case 8:
		lt9611_write_reg(client, 0xf7, 0x00);
		lt9611_write_reg(client, 0xf8, 0x01);
		break;

	case 9:
		lt9611_write_reg(client, 0xf7, 0x00);
		lt9611_write_reg(client, 0xf8, 0x02);
		break;

	case 10:
		lt9611_write_reg(client, 0xf7, 0x00);
		lt9611_write_reg(client, 0xf8, 0x03);
		break;

	case 11:
		lt9611_write_reg(client, 0xf7, 0x00);
		lt9611_write_reg(client, 0xf8, 0x04);
		break;

	case 12:
		lt9611_write_reg(client, 0xf7, 0x00);
		lt9611_write_reg(client, 0xf8, 0x10);
		break;

	case 13:
		lt9611_write_reg(client, 0xf7, 0x00);
		lt9611_write_reg(client, 0xf8, 0x20);
		break;

	case 14:
		lt9611_write_reg(client, 0xf7, 0x00);
		lt9611_write_reg(client, 0xf8, 0x30);
		break;

	case 15:
		lt9611_write_reg(client, 0xf7, 0x00);
		lt9611_write_reg(client, 0xf8, 0x40);
		break;
	default:
		break;
	}
}

static int do_checksum(const unsigned char *x, u8 len)
{
	unsigned char check = x[len];
	unsigned char sum = 0;
	int i;

	pr_info("\nChecksum: 0x%x", check);

	for (i = 0; i < len; i++)
		sum += x[i];

	if ((unsigned char)(check + sum) != 0) {
		pr_info(" (should be 0x%x)\n", -sum & 0xff);
		return 0;
	}

	pr_info(" (valid)\n");
	return 1;
}

int lt9611_parse_physical_address(struct cec_msg *cec_msg, u8 * edid)	// parse edid data from edid.
{
	//int ret = 0;
	int version;
	int offset = 0;
	int offset_d = 0;
	int tag_code;
	u16 physical_address;

	version = edid[0x81];
	offset_d = edid[0x82];

	if (!do_checksum(edid, 255))
		return 0;	//prase_physical_address fail.

	if (version < 3)
		return 0;	//prase_physical_address fail.

	if (offset_d < 5)
		return 0;	//prase_physical_address fail.

	tag_code = (edid[0x84 + offset] & 0xe0) >> 5;

	while (tag_code != 0x03) {
		if ((edid[0x84 + offset] & 0x1f) == 0)
			return 0;
		offset += edid[0x84 + offset] & 0x1f;
		offset++;

		if (offset > (offset_d - 4))
			return 0;

		tag_code = (edid[0x84 + offset] & 0xe0) >> 5;
	}

	pr_info("\nvsdb: 0x%x,0x%x,0x%x", edid[0x84 + offset],
		edid[0x85 + offset], edid[0x86 + offset]);

	if ((edid[0x84 + offset + 1] == 0x03) &&
	    (edid[0x84 + offset + 2] == 0x0c) &&
	    (edid[0x84 + offset + 3] == 0x00)) {
		physical_address = edid[0x84 + offset + 4];
		physical_address =
		    (physical_address << 8) + edid[0x84 + offset + 5];

		cec_msg->physical_address = physical_address;
		pr_info("\nprase physical address success! %x",
			physical_address);
		return 1;
	}
	return 0;
}

static void lt9611_hdmi_cec_read(struct i2c_client *client, struct cec_msg *cec_msg)	// transfer cec msg from LT9611 regisrer to rx_buffer.
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	u8 size, i;
	lt9611_write_reg(client, 0xff, 0x86);
	lt9611_write_reg(client, 0xf5, 0x01);	//lock rx data buff
	lt9611_read_reg(client, 0xd3, &size);
	cec_msg->rx_data_buff[0] = size;
	//pr_info("\r\ncec rec: ");
	for (i = 1; i <= size; i++) {
		lt9611_read_reg(client, 0xd3 + i, &cec_msg->rx_data_buff[i]);
		//pr_info("0x%02x, ",cec_msg->rx_data_buff[i]);
	}
	lt9611_write_reg(client, 0xf5, 0x00);	//unlock rx data buff

	i2c_set_clientdata(client, lt9611);
}

static void lt9611_hdmi_cec_write(struct i2c_client *client,
				  struct cec_msg *cec_msg)
{
	u8 size, i;
	size = cec_msg->tx_data_buff[0];
	cec_msg->retries_times = 0;

	lt9611_write_reg(client, 0xff, 0x86);
	lt9611_write_reg(client, 0xf5, 0x01);	//lock rx data buff
	lt9611_write_reg(client, 0xf4, size);
	for (i = 0; i <= size; i++) {
		lt9611_write_reg(client, 0xe4 + i,
				 cec_msg->tx_data_buff[1 + i]);
	}
	lt9611_write_reg(client, 0xf9, 0x03);	//start send msg
	mdelay(25 * i);		//wait HDMI
	lt9611_write_reg(client, 0xf5, 0x00);	//unlock rx data buff
}

#if 0
static void lt9611_cec_write_demo(struct i2c_client *client)
{

	lt9611->cec_txdata_buff[0] = 0x05;	//data counter to be send
	lt9611->cec_txdata_buff[1] = 0x40;	//first cec data, in spec, sender id = 0x05
	//receiver id = 0x00
	lt9611->cec_txdata_buff[2] = 0x84;	//second cec data(in spec, it is opcode =0x84)
	lt9611->cec_txdata_buff[3] = 0x10;	//parameter of opcode
	lt9611->cec_txdata_buff[4] = 0x00;	//parameter of opcode
	lt9611->cec_txdata_buff[5] = 0x05;	//parameter of opcode
	//lt9611_hdmi_cec_write(client, lt9611->cec_txdata_buff);

	i2c_set_clientdata(client, lt9611);
}
#endif

static void lt9611_cec_la_allocation(struct i2c_client *client, struct cec_msg *cec_msg)	//polling  logical address.
{
	u8 logical_address;

	logical_address = cec_msg->logical_address;
	cec_msg->tx_data_buff[0] = 0x01;	//data counter to be send
	cec_msg->tx_data_buff[1] = (logical_address << 4) | logical_address;
	lt9611_hdmi_cec_write(client, cec_msg);
}

void lt9611_cec_report_physical_address(struct i2c_client *client, struct cec_msg *cec_msg)	// report physical address.
{
	cec_msg->tx_data_buff[0] = 0x05;	//data counter to be send
	cec_msg->tx_data_buff[1] = (cec_msg->logical_address << 4) | 0x0f;
	//first cec data([7:4]=initiator ;[7:4]= destintion)
	cec_msg->tx_data_buff[2] = 0x84;	//opcode
	cec_msg->tx_data_buff[3] = (u8) (cec_msg->physical_address >> 8);	//parameter of opcode
	cec_msg->tx_data_buff[4] = (u8) (cec_msg->physical_address);	//parameter of opcode
	cec_msg->tx_data_buff[5] = 0x04;	//device type = playback device

	//pr_info("\nPA:%x, %x",cec_msg->tx_data_buff[3],cec_msg->tx_data_buff[4]);

	lt9611_hdmi_cec_write(client, cec_msg);
}

void lt9611_cec_menu_activate(struct i2c_client *client, struct cec_msg *cec_msg)	// report physical address.
{
	cec_msg->tx_data_buff[0] = 0x04;	//data counter to be send
	cec_msg->tx_data_buff[1] =
	    (cec_msg->logical_address << 4) | cec_msg->destintion;
	//first cec data([7:4]=initiator ;[7:4]= destintion)
	cec_msg->tx_data_buff[2] = 0x8e;	//opcode
	cec_msg->tx_data_buff[3] = 0x00;	//parameter of opcode

	//pr_info("\nPA:%x, %x",cec_msg->tx_data_buff[3],cec_msg->tx_data_buff[4]);

	lt9611_hdmi_cec_write(client, cec_msg);
}

void lt9611_cec_feature_abort(struct i2c_client *client, struct cec_msg *cec_msg, u8 reason)	// report feature abort
{
	cec_msg->tx_data_buff[0] = 0x03;	//data counter to be send
	cec_msg->tx_data_buff[1] =
	    (cec_msg->logical_address << 4) | cec_msg->destintion;
	cec_msg->tx_data_buff[2] = 0x00;	//opcode
	cec_msg->tx_data_buff[3] = reason;	//parameter1 of opcode

	lt9611_hdmi_cec_write(client, cec_msg);
}

void lt9611_cec_frame_retransmission(struct i2c_client *client,
				     struct cec_msg *cec_msg)
{

	if (cec_msg->retries_times < 5) {
		lt9611_write_reg(client, 0xff, 0x86);
		lt9611_write_reg(client, 0xf9, 0x02);
		lt9611_write_reg(client, 0xf9, 0x03);	//start send msg
	}
	cec_msg->retries_times++;
}

void lt9611_cec_device_polling(struct i2c_client *client,
			       struct cec_msg *cec_msg)
{
	static u8 i;
	if (!cec_msg->la_allocation_done) {
		cec_msg->tx_data_buff[0] = 0x01;	//data counter to be send
		cec_msg->tx_data_buff[1] = i;	//first cec data(in spec, sender id = 0x04,
		//receiver id = 0x04;
		lt9611_hdmi_cec_write(client, cec_msg);
		if (i > 13)
			cec_msg->la_allocation_done = 1;
		(i > 13) ? (i = 0) : (i++);
	}
}

void lt9611_cec_msg_tx_handle(struct i2c_client *client,
			      struct cec_msg *cec_msg)
{
	u8 cec_status;
	u8 header;
	u8 opcode;
	u8 i;
	cec_status = cec_msg->cec_status;

//    if( cec_msg ->send_msg_done) //There is no tx msg to be handled
//        return;

	if (cec_status & CEC_ERROR_INITIATOR) {
		pr_info("\nCEC_ERROR_INITIATOR.");
		lt9611_cec_logical_reset(client);
		return;
	}

	if (cec_status & CEC_ARB_LOST) {
		pr_info("\nCEC_ARB_LOST.");	//lost arbitration
		return;
	}

	if (cec_status & (CEC_SEND_DONE | CEC_NACK | CEC_ERROR_FOLLOWER)) {
		do {

			pr_info("\ntx_date: ");
			for (i = 0; i < cec_msg->tx_data_buff[0]; i++)
				pr_info("0x%02x, ",
					cec_msg->tx_data_buff[i + 1]);

			if (cec_status & CEC_SEND_DONE)
				pr_info("CEC_SEND_DONE >>");
			if (cec_status & CEC_NACK)
				pr_info("NACK >>");

			header = cec_msg->tx_data_buff[1];

			if ((header == 0x44) || (header == 0x88) || (header == 0xbb))	//logical address allocation
			{
				if (cec_status & CEC_NACK) {
					cec_msg->logical_address =
					    header & 0x0f;
					pr_info("la_allocation_done.");
					lt9611_cec_set_logical_address(client,
								       cec_msg);
					lt9611_cec_report_physical_address
					    (client, cec_msg);
				}

				if (cec_status & CEC_SEND_DONE) {

					if (cec_msg->logical_address == 4)	// go to next la.
						cec_msg->logical_address = 8;
					else if (cec_msg->logical_address == 8)	// go to next la.
						cec_msg->logical_address = 11;
					else if (cec_msg->logical_address == 11)	// go to next la.
						cec_msg->logical_address = 4;

					lt9611_cec_la_allocation(client,
								 cec_msg);
				}

				break;
			}

			if (cec_status & (CEC_NACK | CEC_ERROR_FOLLOWER)) {
				lt9611_cec_frame_retransmission(client,
								cec_msg);
			}

			if (cec_msg->tx_data_buff[0] < 2)	//check tx data length
				break;

			opcode = cec_msg->tx_data_buff[2];
			if (opcode == 0x84) {
				cec_msg->report_physical_address_done = 1;
				pr_info("report_physical_address.");
			}

			if (opcode == 0x00) {
				pr_info("feature abort");
			}

		} while (0);
	}
}

void lt9611_cec_msg_rx_parse(struct i2c_client *client, struct cec_msg *cec_msg)
{
	u8 cec_status;
	u8 header;
	u8 opcode;
	u8 initiator;
	u8 destintion;
	u8 i;

	cec_status = cec_msg->cec_status;

//    if( cec_msg ->parse_msg_done) //There is no Rx msg to be parsed
//        return;

	if (cec_status & CEC_ERROR_FOLLOWER) {
		pr_info("\nCEC_ERROR_FOLLOWER.");
		return;
	}

	if (!(cec_status & CEC_REC_DATA)) {
		return;
	}

	lt9611_hdmi_cec_read(client, &lt9611_cec_msg);

	if (cec_msg->rx_data_buff[0] < 1)	//check rx data length
		return;

	pr_info("\nrx_date: ");
	for (i = 0; i < cec_msg->rx_data_buff[0]; i++)
		pr_info("0x%02x, ", cec_msg->rx_data_buff[i + 1]);

	pr_info("parse <<");
	header = cec_msg->rx_data_buff[1];
	destintion = header & 0x0f;
	initiator = (header & 0xf0) >> 4;
	//cec_msg ->parse_msg_done = 1;

	if (header == 0x4f) {
		pr_info("lt9611 broadcast msg.");
	}

	if (cec_msg->rx_data_buff[0] < 2)	//check rx data length
		return;

	opcode = cec_msg->rx_data_buff[2];

// CECT 12 Invalid Msg Tests
	if ((header & 0x0f) == 0x0f) {
		if ((opcode == 0x00) ||
		    (opcode == 0x83) ||
		    (opcode == 0x8e) || (opcode == 0x90) || (opcode == 0xff)) {
			pr_info("Invalid msg, destination address error");	//these msg should not be broadcast msg, but they do.
			return;
		}
	} else {
		if ((opcode == 0x84) || (opcode == 0x84) || (opcode == 0x84)) {
			pr_info("Invalid msg, destination address error");	//these msg should be broadcast msg, but they not.
			return;
		}
	}

	if (opcode == 0xff)	//abort
	{
		pr_info("abort.");
		if (destintion == 0x0f)	//ignor broadcast abort msg.
			return;
		cec_msg->destintion = initiator;
		lt9611_cec_feature_abort(client, cec_msg, CEC_ABORT_REASON_0);
	}

	if (opcode == 0x83)	//give physical address
	{
		pr_info("give physical address.");
		lt9611_cec_report_physical_address(client, cec_msg);
	}

	if (opcode == 0x90)	//report power status
	{
		pr_info("report power status.");
		if (cec_msg->rx_data_buff[0] < 3) {
			pr_info("<error:parameters missing");
			return;	//parameters missing, ignor this msg.
		}
	}

	if (opcode == 0x8e)	//menu status
	{
		pr_info("menu status.");
		if (cec_msg->rx_data_buff[0] < 3) {
			pr_info("<error:parameters missing");
			return;	//parameters missing, ignor this msg.
		}
	}

	if (opcode == 0x00)	//feature abort
	{
		pr_info("feature abort.");
		if (cec_msg->rx_data_buff[0] < 3) {
			pr_info("<error:parameters missing");
			return;	//parameters missing, ignor this msg.
		}
	}

	if (opcode == 0x9e)	//cec version
	{
		pr_info("cec version.");
		if (cec_msg->rx_data_buff[0] < 3) {
			pr_info("<error:parameters missing");
			return;	//parameters missing, ignor this msg.
		}
	}

	if (opcode == 0x84)	//report physical address
	{
		pr_info("report physical address.");
		if (cec_msg->rx_data_buff[0] < 5) {
			pr_info("<error:parameters missing");
			return;	//parameters missing, ignor this msg.
		}
	}

	if (opcode == 0x86)	//set stream path
	{
		pr_info("set stream path.");
		if (cec_msg->rx_data_buff[0] < 4) {
			pr_info("<error:parameters missing");
			return;	//parameters missing, ignor this msg.
		}
		lt9611_cec_report_physical_address(client, cec_msg);
		mdelay(120);
		lt9611_cec_menu_activate(client, cec_msg);
	}
}

void lt9611_cec_msg_init(struct i2c_client *client, struct cec_msg *cec_msg)
{
	lt9611_hdmi_cec_on(client, 1);
	cec_msg->physical_address = 0x2000;
	cec_msg->logical_address = 4;
	cec_msg->report_physical_address_done = 0;
	cec_msg->la_allocation_done = 0;
	lt9611_cec_set_logical_address(client, cec_msg);
}

//CEC: end
/////////////////////////////////////////////////////////////
//These function for debug: start
/////////////////////////////////////////////////////////////
static void lt9611_frequency_meter_byte_clk(struct i2c_client *client)
{
	u8 temp;
	u32 reg = 0x00;

	/* port A byte clk meter */
	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0xc7, 0x03);	//PortA

	mdelay(50);
	lt9611_read_reg(client, 0xcd, &temp);
	if ((temp & 0x60) == 0x60) {	/* clk stable */
		reg = (u32) (temp & 0x0f) * 65536;
		lt9611_read_reg(client, 0xce, &temp);
		reg = reg + (u16) temp *256;
		lt9611_read_reg(client, 0xcf, &temp);
		reg = reg + temp;
		pr_info("port A byte clk = %ld\n", reg);
	} else			/* clk unstable */
		pr_info("port A byte clk unstable\n");

#if 1				/* PortB is unused */
	/* port B byte clk meter */
	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0xc7, 0x04);
	mdelay(50);
	lt9611_read_reg(client, 0xcd, &temp);
	if ((temp & 0x60) == 0x60) {	/* clk stable */
		reg = (u32) (temp & 0x0f) * 65536;
		lt9611_read_reg(client, 0xce, &temp);
		reg = reg + (u16) temp *256;
		lt9611_read_reg(client, 0xcf, &temp);
		reg = reg + temp;
		pr_info("port B byte clk = %ld\n", reg);
	} else			/* clk unstable */
		pr_info("port B byte clk unstable\n");
#endif
}

static void lt9611_htotal_sysclk(struct i2c_client *client)
{
#ifdef _htotal_stable_check_
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	u16 reg;
	u8 loopx;
	u8 value_0 = 0, value_1 = 0;

	mutex_lock(&lt9611->i2c_lock);
	for (loopx = 0; loopx < 10; loopx++) {
		lt9611_write_reg(client, 0xff, 0x82);
		lt9611_read_reg(client, 0x86, &value_0);
		lt9611_read_reg(client, 0x87, &value_1);
		reg = (value_0 * 256) + value_1;
		pr_info("Htotal_Sysclk = %d\n", reg);
		//printdec_u32(reg);
	}
	//pr_info("Htotal_Sysclk = %d\n", reg);

#endif /* HTOTAL_STABLE_CHECK */
}

static void lt9611_pcr_mk_print(struct i2c_client *client)
{
#ifdef _pcr_mk_printf_
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	u8 loopx;
	u8 value = 0;

	mutex_lock(&lt9611->i2c_lock);
	for (loopx = 0; loopx < 8; loopx++) {
		lt9611_write_reg(client, 0xff, 0x83);

		lt9611_read_reg(client, 0x97, &value);
		pr_info("M:0x%x", value);
		lt9611_read_reg(client, 0xb4, &value);
		pr_info(" 0xb4: 0x%x", value);
		lt9611_read_reg(client, 0xb5, &value);
		pr_info(" 0xb5: 0x%x", value);
		lt9611_read_reg(client, 0xb6, &value);
		pr_info("0xb6: 0x%x", value);
		lt9611_read_reg(client, 0xb7, &value);
		pr_info("0xb7: 0x%x\n", value);

		mdelay(1000);
	}
#endif /* PCR_MK_PRINTF */
}

static void lt9611_dphy_debug(struct i2c_client *client)
{
#ifdef _mipi_Dphy_debug_
	u8 temp = 0;
	u8 value = 0;

	lt9611_write_reg(client, 0xff, 0x83);
	lt9611_read_reg(client, 0xbc, &temp);
	if (temp == 0x55)
		pr_info("port A lane PN is right\n");
	else
		pr_info("port A lane PN error 0x83bc = 0x%x\n", temp);

	lt9611_read_reg(client, 0x99, &temp);
	if (temp == 0xb8)
		pr_info("port A lane 0 sot right \n");
	else
		pr_info("port A lane 0 sot error = 0x%x\n", temp);

	lt9611_read_reg(client, 0x9b, &temp);
	if (temp == 0xb8)
		pr_info("port A lane 1 sot right ");
	else
		pr_info("port A lane 1 sot error = 0x%x\n", temp);

	lt9611_read_reg(client, 0x9d, &temp);
	if (temp == 0xb8)
		pr_info("port A lane 2 sot right \n");
	else
		pr_info("port A lane 2 sot error = 0x%x\n", temp);

	lt9611_read_reg(client, 0x9f, &temp);
	if (temp == 0xb8)
		pr_info("port A lane 3 sot right \n");
	else
		pr_info("port A lane 3 sot error = 0x%x\n", temp);

	lt9611_read_reg(client, 0x9a, &value);
	pr_info("port A lane 0 settle = 0x%x\n", value);
	lt9611_read_reg(client, 0x9c, &value);
	pr_info("port A lane 1 settle = 0x%x\n", value);
	lt9611_read_reg(client, 0x9e, &value);
	pr_info("port A lane 2 settle = 0x%x\n", value);
	lt9611_read_reg(client, 0xa0, &value);
	pr_info("port A lane 3 settle = 0x%x\n", value);
#endif /* MIPI_DPHY_DEBUG */
}

/////////////////////////////////////////////////////////////
//These function for debug: end
/////////////////////////////////////////////////////////////

static int lt9611_irq_init(struct i2c_client *client)
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	int ret;

	if (!lt9611->irq_gpio) {
		dev_warn(lt9611->dev, "No interrupt specified.\n");
		lt9611->irq_type = 0;
		return 0;
	}

	client->irq = gpio_to_irq(lt9611->irq_gpio);
	lt9611->irq = client->irq;

	i2c_set_clientdata(client, lt9611);

	pr_info("lt9611_irq_init: lt9611->irq == %d, irq->gpio=%d\n",
		lt9611->irq, lt9611->irq_gpio);

	ret = request_threaded_irq(lt9611->irq, NULL,
				   lt9611_irq,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "lt9611-irq", lt9611);

	if (ret) {
		dev_err(lt9611->dev, "Failed to request IRQ %d: %d\n",
			lt9611->irq, ret);
		return ret;
	} else
		pr_info(" request_threaded_irq %d \n", lt9611->irq);
/*
	//int hpd interrupt
	lt9611_write_reg(client, 0xff, 0x82);
	//lt9611_write_reg(0x10,0x00); //Output low level active;
	lt9611_write_reg(client, 0x58, 0x0a);	//Det HPD
	lt9611_write_reg(client, 0x59, 0x80);	//HPD debounce width

	//intial vid change interrupt
	//Video check module output for sys_clk interrupt clear.
	lt9611_write_reg(client, 0x9e, 0xf7);
*/
	return ret;
}

/*
static void lt9611_irq_exit(struct lt9611_data *lt9611)
{
	if (lt9611->irq)
		free_irq(lt9611->irq, lt9611);
}
*/

#if 0
static void LT9611_Globe_Interrupts(struct i2c_client *client, bool on)
{
	if (on) {
		lt9611_write_reg(client, 0xff, 0x81);
		lt9611_write_reg(client, 0x51, 0x10);	//hardware mode irq pin out
	} else {
		lt9611_write_reg(client, 0xff, 0x81);	//software mode irq pin out = 1;
		lt9611_write_reg(client, 0x51, 0x30);
	}
}
#endif

static void lt9611_enable_interrputs(struct i2c_client *client, u8 interrupts,
				     bool on)
{
	if (interrupts == HPD_INTERRUPT_ENABLE) {
		if (on) {
			lt9611_write_reg(client, 0xff, 0x82);
			lt9611_write_reg(client, 0x07, 0xff);	//clear3
			lt9611_write_reg(client, 0x07, 0x3f);	//clear3
			lt9611_write_reg(client, 0x03, 0x3f);	//mask3  //Tx_det
			pr_info("lt9611_enable_interrputs: hpd_irq_enable\n");
		} else {
			lt9611_write_reg(client, 0xff, 0x82);
			lt9611_write_reg(client, 0x07, 0xff);	//clear3
			lt9611_write_reg(client, 0x03, 0xff);	//mask3  //Tx_det
			pr_info("lt9611_enable_interrputs: hpd_irq_disable\n");
		}
	}

	if (interrupts == VID_CHG_INTERRUPT_ENABLE) {
		if (on) {
			lt9611_write_reg(client, 0xff, 0x82);
			lt9611_write_reg(client, 0x9e, 0xff);	//clear vid chk irq
			lt9611_write_reg(client, 0x9e, 0xf7);
			lt9611_write_reg(client, 0x04, 0xff);	//clear0
			lt9611_write_reg(client, 0x04, 0xfe);	//clear0
			lt9611_write_reg(client, 0x00, 0xfe);	//mask0 vid_chk_IRQ
			pr_info
			    ("lt9611_enable_interrputs: vid_chg_irq_enable\n");
		} else {
			lt9611_write_reg(client, 0xff, 0x82);
			lt9611_write_reg(client, 0x04, 0xff);	//clear0
			lt9611_write_reg(client, 0x00, 0xff);	//mask0 vid_chk_IRQ
			pr_info
			    ("lt9611_enable_interrputs: vid_chg_irq_disable\n");
		}
	}

	if (interrupts == CEC_INTERRUPT_ENABLE) {
		if (on) {
			lt9611_write_reg(client, 0xff, 0x86);
			lt9611_write_reg(client, 0xfa, 0x00);	//cec interrup mask
			lt9611_write_reg(client, 0xfc, 0x7f);	//cec irq clr
			lt9611_write_reg(client, 0xfc, 0x00);

			/* cec irq init */
			lt9611_write_reg(client, 0xff, 0x82);
			lt9611_write_reg(client, 0x01, 0x7f);	//mask bit[7]
			lt9611_write_reg(client, 0x05, 0xff);	//clr bit[7]
			lt9611_write_reg(client, 0x05, 0x7f);
		} else {
			lt9611_write_reg(client, 0xff, 0x86);
			lt9611_write_reg(client, 0xfa, 0xff);	//cec interrup mask
			lt9611_write_reg(client, 0xfc, 0x7f);	//cec irq clr

			/* cec irq init */
			lt9611_write_reg(client, 0xff, 0x82);
			lt9611_write_reg(client, 0x01, 0xff);	//mask bit[7]
			lt9611_write_reg(client, 0x05, 0xff);	//clr bit[7]
		}

	}
}

static void lt9611_hdp_interrupt_handle(struct i2c_client *client)
{
	bool tx_hpd;

	tx_hpd = lt9611_get_hpd_status(client);

	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0x07, 0xff);	//clear3
	lt9611_write_reg(client, 0x07, 0x3f);	//clear3

	if (gpio_is_valid(lt9611->irq_gpio)) {
                if(gpio_get_value_cansleep(lt9611->irq_gpio) == 0)//clear irq gpio
                {
                        lt9611_write_reg(client, 0xff, 0x82);
	                lt9611_write_reg(client, 0x07, 0xff);	//clear3
	                lt9611_write_reg(client, 0x07, 0x3f);	//clear3
		        pr_info("%s: hpd detect pin is low \n", __func__);
                }
        }
	if (tx_hpd) {
		pr_info("lt9611_hdp_interrupt_handle: HDMI connected\n");
		lt9611_lowpower_mode(client, 0);
		lt9611_enable_interrputs(client, VID_CHG_INTERRUPT_ENABLE, 1);
		//lt9611_hdcp_enable(client);
		//lt9611_hdmi_out_enable(client);
		mdelay(100);
		//lt9611_read_edid(client);
#ifdef cec_on
		lt9611_parse_physical_address(&lt9611_cec_msg, Sink_EDID);
		lt9611_cec_la_allocation(client, &lt9611_cec_msg);
#endif
		lt9611_video_check(client);
		if (lt9611->videoformat != video_none) {
			lt9611_pll(client, lt9611->video);
			lt9611_mipi_pcr(client, lt9611->video);
			lt9611_hdmi_tx_digital(client, lt9611->video);
			i2c_set_clientdata(client, lt9611);
			lt9611_hdmi_out_enable(client);
			//lt9611_hdcp_enable(client);
		} else {
			lt9611_hdmi_out_disable(client);
			pr_info
			    ("lt9611_hdp_interrupt_handle: no mipi video, disable hdmi output\n");
		}
	} else {
		pr_info("lt9611_hdp_interrupt_handle: HDMI disconnected\n");
		lt9611_enable_interrputs(client, VID_CHG_INTERRUPT_ENABLE, 0);
		lt9611_lowpower_mode(client, 1);
#ifdef cec_on
		lt9611_cec_msg_init(client, &lt9611_cec_msg);
#endif
	}
        if (gpio_is_valid(lt9611->irq_gpio)) {
                if(gpio_get_value_cansleep(lt9611->irq_gpio) == 0)//clear irq gpio
                {
                        lt9611_write_reg(client, 0xff, 0x82);
	                lt9611_write_reg(client, 0x07, 0xff);	//clear3
	                lt9611_write_reg(client, 0x07, 0x3f);	//clear3
		        pr_info("%s: 22hpd detect pin is low \n", __func__);
                }
        }
}

static void lt9611_vid_chg_interrupt_handle(struct i2c_client *client)
{
	//struct lt9611_data *lt9611 = i2c_get_clientdata(client);

	pr_info("lt9611_vid_chg_interrupt_handle: \n");
#if 1
	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0x9e, 0xff);	//clear vid chk irq
	lt9611_write_reg(client, 0x9e, 0xf7);

	lt9611_write_reg(client, 0x04, 0xff);	//clear0 irq
	lt9611_write_reg(client, 0x04, 0xfe);
#endif
	mdelay(100);
	lt9611_video_check(client);

	if (lt9611->videoformat != video_none) {
		lt9611_pll(client, lt9611->video);
		lt9611_mipi_pcr(client, lt9611->video);
		lt9611_hdmi_tx_digital(client, lt9611->video);
		i2c_set_clientdata(client, lt9611);
		lt9611_hdmi_out_enable(client);
		//lt9611_hdcp_enable(client);
	} else {
		//pr_info("\r\nlt9611_vid_chg_interrupt_handle: no mipi video");
		lt9611_hdmi_out_disable(client);
	}
        if (gpio_is_valid(lt9611->irq_gpio)) {
                if(gpio_get_value_cansleep(lt9611->irq_gpio) == 0)//clear irq gpio
                {
                        lt9611_write_reg(client, 0xff, 0x82);
	                lt9611_write_reg(client, 0x07, 0xff);	//clear3
	                lt9611_write_reg(client, 0x07, 0x3f);	//clear3
		        pr_info("%s: 22hpd detect pin is low \n", __func__);
                        #if 1
	                lt9611_write_reg(client, 0xff, 0x82);
	                lt9611_write_reg(client, 0x9e, 0xff);	//clear vid chk irq
	                lt9611_write_reg(client, 0x9e, 0xf7);

	                lt9611_write_reg(client, 0x04, 0xff);	//clear0 irq
	                lt9611_write_reg(client, 0x04, 0xfe);

                        lt9611_write_reg(client, 0xff, 0x86);
	                lt9611_write_reg(client, 0xfc, 0x7f);	//cec irq clr
	                lt9611_write_reg(client, 0xfc, 0x00);

	                lt9611_write_reg(client, 0xff, 0x82);
	                lt9611_write_reg(client, 0x05, 0xff);	//clear3
	                lt9611_write_reg(client, 0x05, 0x7f);	//clear3
                        #endif
                }
        }
}

static void lt9611_cec_interrupt_handle(struct i2c_client *client,
					struct cec_msg *cec_msg)
{
	//struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	u8 cec_status = 0;

	lt9611_write_reg(client, 0xff, 0x86);
	lt9611_read_reg(client, 0xd2, &cec_status);
	cec_msg->cec_status = cec_status;
	pr_info("\nIRQ cec_status: 0x%02x", cec_status);

	if (cec_status & 0x01)	// the done status
	{
		pr_info("send data done. \n");
	}

	lt9611_write_reg(client, 0xff, 0x86);
	lt9611_write_reg(client, 0xfc, 0x7f);	//cec irq clr
	lt9611_write_reg(client, 0xfc, 0x00);

	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0x05, 0xff);	//clear3
	lt9611_write_reg(client, 0x05, 0x7f);	//clear3

	lt9611_cec_msg_tx_handle(client, cec_msg);
	lt9611_cec_msg_rx_parse(client, cec_msg);
}

/////////////////////////////////////////////////////////////
//These function for Pattern output: start
/////////////////////////////////////////////////////////////
#if 0
static void lt9611_pattern_gcm(struct i2c_client *client,
			       struct video_timing *video_format)
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	u8 pol;

	pol =
	    (video_format->h_polarity) * 0x10 +
	    (video_format->v_polarity) * 0x20;
	pol = ~pol;
	pol &= 0x30;

	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0xa3, (u8) ((video_format->hs + video_format->hbp) / 256));	//de_delay
	lt9611_write_reg(client, 0xa4,
			 (u8) ((video_format->hs + video_format->hbp) % 256));
	lt9611_write_reg(client, 0xa5, (u8) ((video_format->vs + video_format->vbp) % 256));	//de_top
	lt9611_write_reg(client, 0xa6, (u8) (video_format->hact / 256));
	lt9611_write_reg(client, 0xa7, (u8) (video_format->hact % 256));	//de_cnt
	lt9611_write_reg(client, 0xa8, (u8) (video_format->vact / 256));
	lt9611_write_reg(client, 0xa9, (u8) (video_format->vact % 256));	//de_line
	lt9611_write_reg(client, 0xaa, (u8) (video_format->htotal / 256));
	lt9611_write_reg(client, 0xab, (u8) (video_format->htotal % 256));	//htotal
	lt9611_write_reg(client, 0xac, (u8) (video_format->vtotal / 256));
	lt9611_write_reg(client, 0xad, (u8) (video_format->vtotal % 256));	//vtotal
	lt9611_write_reg(client, 0xae, (u8) (video_format->hs / 256));
	lt9611_write_reg(client, 0xaf, (u8) (video_format->hs % 256));	//hvsa
	lt9611_write_reg(client, 0xb0, (u8) (video_format->vs % 256));	//vsa

	lt9611_write_reg(client, 0x47, (u8) (pol | 0x07));	//sync polarity
}

static void lt9611_pattern_pixel_clk(struct i2c_client *client,
				     struct video_timing *video_format)
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	u32 pclk;

	pclk = video_format->pclk_khz;
	pr_info("set pixel clk = %ld\n", pclk);
	//printdec_u32(pclk); //Dec
	lt9611_write_reg(client, 0xff, 0x83);
	lt9611_write_reg(client, 0x2d, 0x50);

	if (pclk == 297000) {
		lt9611_write_reg(client, 0x26, 0xb6);
		lt9611_write_reg(client, 0x27, 0xf0);
	}
	if (pclk == 148500) {
		lt9611_write_reg(client, 0x26, 0xb7);
	}
	if (pclk == 74250) {
		lt9611_write_reg(client, 0x26, 0x9c);
	}
	lt9611_write_reg(client, 0xff, 0x80);
	lt9611_write_reg(client, 0x11, 0x5a);	//Pcr reset
	lt9611_write_reg(client, 0x11, 0xfa);
}

static void lt9611_pattern_en(struct i2c_client *client)
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);

	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0x4f, 0x80);	//[7] = Select ad_txpll_d_clk.
	lt9611_write_reg(client, 0x50, 0x20);
}

static void lt9611_pattern(struct i2c_client *client)
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);

	//DTV
	//video = &video_640x480_60Hz;
	//video = &video_720x480_60Hz;
	//video = &video_1280x720_60Hz;
	lt9611->vdata = &video_1920x1080_60Hz;
	//video = &video_3840x2160_30Hz;

	//DMT
	//video = &video_1024x600_60Hz;
	//video = &video_1024x600_60Hz;

	i2c_set_clientdata(client, lt9611);

	lt9611_read_chip_id(client);
	lt9611_system_init(client);
	lt9611_pattern_en(client);

	//lt9611_mipi_pcr(client,video); //weiguo
	lt9611_pll(client, lt9611->vdata);
	i2c_set_clientdata(client, lt9611);

	//lt9611_pattern_pixel_clk(client,video);
	lt9611_dhcp_init(client);

	lt9611_pattern_gcm(client, lt9611->vdata);
	lt9611_hdmi_tx_digital(client, lt9611->vdata);
	i2c_set_clientdata(client, lt9611);

	lt9611_hdmi_tx_phy(client);

	//lt9611_hdcp_enable(client);
	lt9611_hdmi_out_enable(client);
	lt9611_pcr_mk_print(client);

/*
	//Audio pattern
	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_write_reg(client, 0xd6, 0x8c);
	lt9611_write_reg(client, 0xd7, 0x06);	//sync polarity

	lt9611_write_reg(client, 0xff, 0x84);
	lt9611_write_reg(client, 0x06, 0x0c);
	lt9611_write_reg(client, 0x07, 0x10);
	lt9611_write_reg(client, 0x16, 0x01);

	lt9611_write_reg(client, 0x34, 0xd4);	//CTS_N
*/
	lt9611_audio_init(client);
}
#endif
/////////////////////////////////////////////////////////////
//These function for Pattern output: end
/////////////////////////////////////////////////////////////
static void lt9611_irq_task(struct i2c_client *client)
{
	u8 irq_flag3 = 0;
	u8 irq_flag0 = 0;
	u8 irq_flag1 = 0;

	struct lt9611_data *lt9611;
	pr_info("lt9611_irq_task :IRQ Task\n");

	lt9611 = i2c_get_clientdata(client);

	mutex_lock(&lt9611->i2c_lock);
	lt9611_write_reg(client, 0xff, 0x82);
	lt9611_read_reg(client, 0x0c, &irq_flag0);
	lt9611_read_reg(client, 0x0d, &irq_flag1);
	lt9611_read_reg(client, 0x0f, &irq_flag3);

	if ((irq_flag1 & 0x80) == 0x80) {
		lt9611_cec_interrupt_handle(client, &lt9611_cec_msg);
	}

	if (irq_flag3 & 0xc0)	//HPD interrupt
	{
		lt9611_hdp_interrupt_handle(client);
	}

	if (irq_flag0 & 0x01)	//vid_chk
	{
		lt9611_vid_chg_interrupt_handle(client);
	}

	mutex_unlock(&lt9611->i2c_lock);
}

static irqreturn_t lt9611_irq(int irq, void *irq_data)
{
	struct lt9611_data *lt9611;
	pr_info("lt9611_irq irq=%d irq_data=%x\n", irq, irq_data);

	lt9611 = (struct lt9611_data *)irq_data;

	lt9611_irq_task(lt9611->client);

#if 0
	////////////////cec ////////////////
	if (lt9611->flag_cec_data_received) {
		if (lt9611->cec_dxdata_buff[2] == 0x85) {
			lt9611_cec_write_demo(lt9611->client);
		}
		lt9611->flag_cec_data_received = 0;
	}
#endif

	return IRQ_HANDLED;
}

void lt9611_hardware_rst(struct i2c_client *client);
void lt9611_init_ext(void)
{
	if (IS_ERR(lt9611)) {
		pr_err("%s lt9611 is null \n", __func__);
		return;
	}
	lt9611_read_chip_id(lt9611->client);
	pr_info("lt9611_init_ext chipid = 0x%6x\n", lt9611->chipid);
	if (lt9611->chipid == LT9611_CHIP_ID) {
		lt9611_init(lt9611->client);
	}
}
EXPORT_SYMBOL(lt9611_init_ext);

bool lt9611_get_hpd_status_ext(void)
{
	u8 value;

	if (IS_ERR(lt9611)) {
		pr_err("%s lt9611 is null \n", __func__);
		return -1;
	}
	lt9611_write_reg(lt9611->client, 0xff, 0x82);
	lt9611_read_reg(lt9611->client, 0x5e, &value);
	if (value & 0x04) {
		//tx_hpd=1;
		return 1;
	} else {
		//tx_hpd=0;
		return 0;
	}
}

EXPORT_SYMBOL(lt9611_get_hpd_status_ext);

//PortA input : MIPI ; output : HDMI ===== No portB input, no audio input
static void lt9611_init(struct i2c_client *client)
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	struct pinctrl *pinctrl_irq;

	mutex_lock(&lt9611->i2c_lock);
	//disable_irq(lt9611->irq);
	//RESET_LT9611();
	lt9611_system_init(client);

	//lt9611_rst_pd_init(client);
	lt9611_mipi_input_analog(client);
	lt9611_mipi_input_digtal(client);

	mdelay(100);

	lt9611_video_check(client);
	pr_info("############CHECK OVER##################\n");
	lt9611_pll(client, lt9611->video);
	lt9611_mipi_pcr(client, lt9611->video);	//pcr setup
	//i2c_set_clientdata(client, lt9611);

	lt9611_audio_init(client);
	lt9611_csc(client);
	LT9611_HDCP_Init(client);
	lt9611_hdmi_tx_digital(client, lt9611->video);
	i2c_set_clientdata(client, lt9611);

	lt9611_hdmi_tx_phy(client);

//change by xutao for suspend and resume
	//int hpd interrupt
	lt9611_write_reg(client, 0xff, 0x82);
	//lt9611_write_reg(0x10,0x00); //Output low level active;
	lt9611_write_reg(client, 0x58, 0x0a);	//Det HPD
	lt9611_write_reg(client, 0x59, 0x80);	//HPD debounce width

	//intial vid change interrupt
	//Video check module output for sys_clk interrupt clear.
	lt9611_write_reg(client, 0x9e, 0xf7);
// change by xutao for suspend and resume

	//lt9611_irq_init(client);	/* about 850 and hpd irq */
	//lt9611_read_edid(client);

	//lt9611_hdmi_cec_on(client, 1);        /* Include cec irq */
	//lt9611_cec_set_logical_address(client);
	lt9611_cec_msg_init(client, &lt9611_cec_msg);

	lt9611_enable_interrputs(client, HPD_INTERRUPT_ENABLE, 1);
	lt9611_enable_interrputs(client, VID_CHG_INTERRUPT_ENABLE, 0);
	lt9611_enable_interrputs(client, CEC_INTERRUPT_ENABLE, 1);

	lt9611_frequency_meter_byte_clk(client);
	lt9611_dphy_debug(client);	/*lt9611.h _mipi_Dphy_debug_ */
	lt9611_htotal_sysclk(client);	/*lt9611.h _htotal_stable_check_ */
	lt9611_pcr_mk_print(client);	/*lt9611.h _pcr_mk_pr_info_ */
	lt9611_lowpower_mode(client, 1);
	pr_info("############LT9611 Initial End##################\n");
	//mdelay(200);
	if(lt9611_get_hpd_status(client)) {
		pr_info("hdmi device in on!!!!\n");
		lt9611_hdp_interrupt_handle(client);
	}
	mutex_unlock(&lt9611->i2c_lock);
	pinctrl_irq = devm_pinctrl_get_select(&client->dev, "on_state");
    if (IS_ERR(pinctrl_irq))
		dev_err(&client->dev, " Failed to configure irq pin\n");
	enable_irq(lt9611->irq);
	pr_info("lt9611 enable irq and select pinctrl_irq\n");
}

static int lt9611_read_reg(struct i2c_client *client, u8 reg, u8 * dest)
{
	int ret;
	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		pr_err("%s:%s reg(0x%x), ret(%d)\n", DEV_NAME,
		       __func__, reg, ret);
		return ret;
	}

	*dest = ret & 0xff;
	return 0;
}

static int lt9611_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	int ret;
	ret = i2c_smbus_write_byte_data(client, reg, value);
	if (ret < 0)
		pr_err("%s:%s reg(0x%x), ret(%d)\n",
		       DEV_NAME, __func__, reg, ret);

	return ret;
}

static int of_lt9611_dt(struct device *dev)
{
	struct device_node *np_lt9611 = dev->of_node;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);
	int ret = 0;

	if (!np_lt9611) {
		dev_err(dev, "%s:No np_lt9611\n", __func__);
		return -EINVAL;
	}

	lt9611->irq_gpio = of_get_named_gpio(np_lt9611, "lt9611,irq-gpio", 0);
	i2c_set_clientdata(client, lt9611);

	if (gpio_is_valid(lt9611->irq_gpio)) {
		ret =
		    gpio_request_one(lt9611->irq_gpio, GPIOF_DIR_IN,
				     "lt9611,tsp-int");
		if (ret) {
			dev_err(dev, "%s:Unable to request tsp_int [%d]\n",
				__func__, lt9611->irq_gpio);
			ret = -EINVAL;
			goto err_gpio;
		}
	} else {
		dev_err(dev, "%s:Failed to get irq gpio\n", __func__);
		return -EINVAL;
	}

	return ret;

err_gpio:
	gpio_free(lt9611->irq_gpio);

	return ret;
}

void lt9611_hardware_rst(struct i2c_client *client)
{
	struct pinctrl *pinctrl_rst, *pinctrl_irq;
	pinctrl_irq = devm_pinctrl_get_select(&client->dev, "hpd_on");
	mdelay(30);

	pinctrl_rst = devm_pinctrl_get_select(&client->dev, "on_rst");
	mdelay(30);
	pinctrl_rst = devm_pinctrl_get_select(&client->dev, "off_rst");
	if (IS_ERR(pinctrl_rst))
		dev_err(&client->dev, "%s: Failed to configure rst pin\n",
			__func__);

	mdelay(50);
	pinctrl_rst = devm_pinctrl_get_select(&client->dev, "on_rst");
	mdelay(30);

	if (IS_ERR(pinctrl_rst))
		dev_err(&client->dev, "%s: Failed to configure rst pin\n",
			__func__);

	pinctrl_irq = devm_pinctrl_get_select(&client->dev, "on_state");
	if (IS_ERR(pinctrl_irq))
		dev_err(&client->dev, " Failed to configure irq pin\n");
}

static int lt9611_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *dev_id)
{

	int ret = 0;

	pr_info("Enter lt9611_i2c_probe\n");

	lt9611 = kzalloc(sizeof(struct lt9611_data), GFP_KERNEL);

	if (!lt9611) {
		dev_err(&client->dev, "%s: Failed to alloc mem for lt9611\n",
			__func__);
		ret = -ENOMEM;
		goto err;
	}

	lt9611->irq_gpio = 0;
	lt9611->irq_type = 0x00;
	lt9611->irq = 0;

	mutex_init(&lt9611->i2c_lock);
	i2c_set_clientdata(client, lt9611);
	lt9611->client = client;

	if (client->dev.of_node) {
		ret = of_lt9611_dt(&client->dev);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to get device of_node\n");
			goto err;
		}
	}

	lt9611_hardware_rst(client);

	lt9611_read_chip_id(client);

	/*e00217 */
	//lt9611->chipid = LT9611_CHIP_ID;
	//if (lt9611->chipid == LT9611_CHIP_ID) {
	//	lt9611_init(client);
	//}
	i2c_set_clientdata(client, lt9611);
	lt9611_irq_init(client);	/* about 850 and hpd irq */
	//disable_irq(lt9611->irq);

	return ret;

err:
	kfree(lt9611);
	i2c_set_clientdata(client, NULL);

	return ret;
}

static int lt9611_i2c_remove(struct i2c_client *client)
{
	struct lt9611_data *lt9611 = i2c_get_clientdata(client);

	gpio_free(lt9611->irq_gpio);
	free_irq(lt9611->irq, lt9611);

	kfree(lt9611);
	i2c_set_clientdata(client, NULL);
	pr_info("%s: kfree \n", __func__);

	return 0;
}

int lt9611_disable_irq(void)
{
	if(IS_ERR(lt9611)){
		pr_err("%s lt9611 is null \n", __func__);
		return -1;
	}

	disable_irq(lt9611->irq);
	pr_info("%s: lt9611 disable_irq \n", __func__);
	return 0;
}

int lt9611_i2c_suspend(void)
{
	struct pinctrl *pinctrl_irq;
	//struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	//struct lt9611_data *lt9611 = i2c_get_clientdata(client);

	pr_info("%s: enter \n", __func__);
	//disable_irq(lt9611->irq);
	pinctrl_irq = devm_pinctrl_get_select(&lt9611->client->dev, "off_state");
	if (IS_ERR(pinctrl_irq))
		dev_err(&lt9611->client->dev, " Failed to configure irq pin\n");

	return 0;
}

int lt9611_i2c_resume(void)
{
	struct pinctrl *pinctrl_irq;
	//struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	//struct lt9611_data *lt9611 = i2c_get_clientdata(client);

	pr_info("%s: enter \n", __func__);
	//enable_irq_wake(lt9611->irq);
	pinctrl_irq = devm_pinctrl_get_select(&lt9611->client->dev, "on_state");
	if (IS_ERR(pinctrl_irq))
		dev_err(&lt9611->client->dev, " Failed to configure irq pin\n");

	return 0;
}

static struct of_device_id lt9611_i2c_dt_ids[] = {
	{.compatible = "lontium,lt9611",},
	{},
};
/*
static const struct dev_pm_ops lt9611_pm = {
	.suspend = lt9611_i2c_suspend,
	.resume = lt9611_i2c_resume,
};
*/
static struct i2c_driver lt9611_i2c_driver = {
	.driver = {
		   .name = DEV_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = lt9611_i2c_dt_ids,
		   },
	.probe = lt9611_i2c_probe,
	.remove = lt9611_i2c_remove,
};

static int __init lt9611_i2c_init(void)
{
	pr_info("%s:%s\n", "lt9611", __func__);
	return i2c_add_driver(&lt9611_i2c_driver);
}

device_initcall(lt9611_i2c_init);

static void __exit lt9611_i2c_exit(void)
{
	i2c_del_driver(&lt9611_i2c_driver);
}

module_exit(lt9611_i2c_exit);

MODULE_DESCRIPTION("Lontium lt9611 MIPI to HDMI1.4 Converter driver");
MODULE_AUTHOR("Han Ye <yanghs0702@thundersoft.com>");
MODULE_LICENSE("GPL v2");
