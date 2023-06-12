/*
 *  lt9211.c - LT9211 MIPI DSI/CSI-2/Dual-Port LVDS
 *
 *  Copyright (c) 2020 Han Ye <yanghs0702@thundersoft.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include "lt9211.h"
static u8 mipi_fmt = 0;
struct lt9211_dev *lt9211;
static struct video_timing *pVideo_Format;
unsigned int g_sys1p8;
unsigned int g_sys3p3;

//hfp, hs, hbp, hact, htotal, vfp, vs, vbp, vact, vtotal, pixclk
//static struct video_timing video_640x480_60Hz     ={ 8, 96,  40, 640,   800, 33,  2,  10, 480,   525,  25000};
//static struct video_timing video_720x480_60Hz     ={16, 62,  60, 720,   858,  9,  6,  30, 480,   525,  27000};
static struct video_timing video_1280x720_60Hz =
    { 110, 40, 220, 1280, 1650, 5, 5, 20, 720, 750, 74250 };
//static struct video_timing video_1280x720_30Hz    ={110,40, 220,1280,  1650,  5,  5,  20, 720,   750,  74250};
static struct video_timing video_1366x768_60Hz =
    { 26, 110, 110, 1366, 1592, 13, 6, 13, 768, 800, 81000 };
//static struct video_timing video_1920x720_60Hz    ={148,44, 88, 1920,  2200, 28,  5,  12, 720,   765,  88000};
//static struct video_timing video_1920x1080_30Hz   ={88, 44, 148,1920,  2200,  4,  5,  36, 1080, 1125,  74250};
//static struct video_timing video_1920x1080_60Hz =
  //  { 88, 44, 148, 1920, 2200, 4, 5, 36, 1080, 1125, 148500 };

static struct video_timing video_1920x1080_60Hz =
    { 180, 60, 160, 1920, 2320, 15, 4, 10, 1080, 1109, 191040 };

//static struct video_timing video_1920x1200_60Hz =
 //   { 48, 32, 80, 1920, 2080, 3, 6, 26, 1200, 1235, 154000 };
//static struct video_timing video_3840x2160_30Hz   ={176,88, 296,3840,  4400,  8,  10, 72, 2160, 2250, 297000};

//static struct video_timing video_1920x1200_60Hz =
 //   { 180, 60, 160, 1200, 1600, 15, 4, 10, 1920, 1949, 191040 };
   //hfp;  hs; hbp; hact; htotal;vfp;vs;vbp;vact;vtotal;pclk_khz;
static struct video_timing video_1200x1920_60Hz = { 180, 60, 160, 1200, 1600, 4, 5, 4, 1920, 1933, 191040 };
   //static void lt9211_hardware_rst(struct i2c_client *client);


static int lt9211_read_reg(struct i2c_client *client, u8 reg, u8 * dest)
{
	int ret;
	//struct lt9211_dev *lt9211 = i2c_get_clientdata(client);

	//mutex_lock(&lt9211->i2c_lock);
	ret = i2c_smbus_read_byte_data(client, reg);
	//mutex_unlock(&lt9211->i2c_lock);
	if (ret < 0) {
		pr_err
		    ("read wrong:%s reg(0x%x), ret(%d)\n", __func__, reg, ret);
		return ret;
	}
	//ret &= 0xff;
	*dest = ret & 0xff;
	return ret;
}

extern int lt9611_disable_irq(void);
/*
void sys1p_gpio_up()
{
	pr_info("%s enter \n", __func__);
	if (gpio_is_valid(g_sys1p8)) {
		gpio_request(g_sys1p8, "sys1p8");
		gpio_direction_output(g_sys1p8, 1);
		gpio_free(g_sys1p8);
    }

	if (gpio_is_valid(g_sys3p3)) {
		gpio_request(g_sys3p3, "sys3p3");
		gpio_direction_output(g_sys3p3, 1);
		gpio_free(g_sys3p3);
	}

	//disable_irq(lt9211->irq);
}

void sys1p_gpio_down()
{
	pr_info("%s enter \n", __func__);
	//disable_irq(lt9211->irq);
	lt9611_disable_irq();

	if (gpio_is_valid(g_sys1p8)) {
		gpio_request(g_sys1p8, "sys1p8");
		gpio_direction_output(g_sys1p8, 0);
		gpio_free(g_sys1p8);
	}

	if (gpio_is_valid(g_sys3p3)) {
		gpio_request(g_sys3p3, "sys3p3");
		gpio_direction_output(g_sys3p3, 0);
		gpio_free(g_sys3p3);
	}
}
*/

static int lt9211_write_reg(struct i2c_client *client, u8 reg, u8 value)
{

	int ret;
	//struct lt9211_dev *lt9211 = i2c_get_clientdata(client);

	//mutex_lock(&lt9211->i2c_lock);
	ret = i2c_smbus_write_byte_data(client, reg, value);
	//mutex_unlock(&lt9211->i2c_lock);
	if (ret < 0)
		pr_err
		    ("write wrong:%s reg(0x%x), ret(%d)\n", __func__, reg, ret);

	return ret;
}

/** lvds rx logic rst **/
void lt9211_mipirx_logic_rst(struct i2c_client *client)
{
	lt9211_write_reg(client, 0xff, 0x81);
	lt9211_write_reg(client, 0x0a, 0xc0);
	lt9211_write_reg(client, 0x20, 0xbf);	//mipi rx div logic reset,for portb input
	mdelay(10);
	lt9211_write_reg(client, 0x0a, 0xc1);
	lt9211_write_reg(client, 0x20, 0xff);
}

static int LT9211_MipiClkCheck(struct i2c_client *client)
{
	u8 porta_clk_state, value = 0;

	lt9211_write_reg(client, 0xff, 0x86);
	lt9211_write_reg(client, 0x00, 0x01);
	mdelay(50);
	lt9211_read_reg(client, 0x08, &value);
	porta_clk_state = value & (0x20);
	pr_info("\r\n LT9211_MipiClkCheck: %d  0x%2x ", value, porta_clk_state);

	if (porta_clk_state) {
		return 1;
	}
	return 0;
}

void LT9211_MipiByteClkDebug(struct i2c_client *client)
{
#ifdef _uart_debug_
	u32 fm_value;
	u8 value = 0;

	lt9211_write_reg(client, 0xff, 0x86);
#if 1				//def INPUT_PORTA
	lt9211_write_reg(client, 0x00, 0x01);
#endif
#ifdef INPUT_PORTB
	lt9211_write_reg(client, 0x00, 0x02);
#endif
	mdelay(100);
	fm_value = 0;

	lt9211_read_reg(client, 0x08, &value);
	fm_value = (value & (0x0f));
	fm_value = (fm_value << 8);
	lt9211_read_reg(client, 0x09, &value);
	fm_value = fm_value + value;
	fm_value = (fm_value << 8);
	lt9211_read_reg(client, 0x0a, &value);
	fm_value = fm_value + value;
	pr_info("\r\nmipi byteclk: %d ", fm_value);
#endif
}

void LT9211_VideoCheckDebug(struct i2c_client *client)
{
#ifdef _uart_debug_
	u8 sync_polarity;
	u16 hact, vact;
	u8 value = 0;
	u16 hs, vs;
	u16 hbp, vbp;
	u16 htotal, vtotal;
	u16 hfp, vfp;

	lt9211_write_reg(client, 0xff, 0x86);
	lt9211_write_reg(client, 0x20, 0x00);

	lt9211_read_reg(client, 0x70, &sync_polarity);
	lt9211_read_reg(client, 0x71, (u8 *) (&vs));

	lt9211_read_reg(client, 0x72, (u8 *) (&hs));
	lt9211_read_reg(client, 0x73, &value);
	hs = (hs << 8) + value;

	lt9211_read_reg(client, 0x74, (u8 *) (&vbp));
	lt9211_read_reg(client, 0x75, (u8 *) (&vfp));

	lt9211_read_reg(client, 0x76, (u8 *) (&hbp));
	lt9211_read_reg(client, 0x77, &value);
	hbp = (hbp << 8) + value;

	lt9211_read_reg(client, 0x78, (u8 *) (&hfp));
	lt9211_read_reg(client, 0x79, &value);
	hfp = (hfp << 8) + value;

	lt9211_read_reg(client, 0x7A, (u8 *) (&vtotal));
	lt9211_read_reg(client, 0x7B, &value);
	vtotal = (vtotal << 8) + value;

	lt9211_read_reg(client, 0x7C, (u8 *) (&htotal));
	lt9211_read_reg(client, 0x7D, &value);
	htotal = (htotal << 8) + value;

	lt9211_read_reg(client, 0x7E, (u8 *) (&vact));
	lt9211_read_reg(client, 0x7F, &value);
	vact = (vact << 8) + value;

	lt9211_read_reg(client, 0x80, (u8 *) & hact);
	lt9211_read_reg(client, 0x81, &value);
	hact = (hact << 8) + value;

	pr_info("\r\nsync_polarity = %x", sync_polarity);
	pr_info("\r\nhfp:%d, hs:%d, hbp:%d, hact:%d, htotal:%d ", hfp, hs, hbp,
		hact, htotal);
	pr_info("\r\nvfp:%d, vs:%d, vbp:%d, vact:%d, vtotal:%d ", vfp, vs, vbp,
		vact, vtotal);

#endif
}

static int lt9211_system_init(struct i2c_client *client)
{
	int ret = 0;
        pr_info("lt9211 system init begin\n");
	lt9211_write_reg(client, 0xff, 0x82);
	lt9211_write_reg(client, 0x01, 0x18);

	lt9211_write_reg(client, 0xff, 0x86);
	lt9211_write_reg(client, 0x06, 0x61);

	lt9211_write_reg(client, 0x07, 0xa8);

	lt9211_write_reg(client, 0xff, 0x87);
	lt9211_write_reg(client, 0x14, 0x08);
	lt9211_write_reg(client, 0x15, 0x00);
	lt9211_write_reg(client, 0x18, 0x0f);
	lt9211_write_reg(client, 0x22, 0x08);
	lt9211_write_reg(client, 0x23, 0x00);
	lt9211_write_reg(client, 0x26, 0x0f);
	//pre-emphasis set
	//lt9211_write_reg(client, 0xff, 0x82);
	//lt9211_write_reg(client, 0x46, 0xff);
	//lt9211_write_reg(client, 0x47, 0xd5);

	lt9211_write_reg(client, 0xff, 0x81);
	lt9211_write_reg(client, 0x0B, 0xFE);	//rpt reset
        pr_info("lt9211 system init end\n");
	return ret;
}

/**
 *Port A or B mode enable with init.
 */
static int lt9211_mipirxrhy(struct i2c_client *client, bool port)
{
	int ret = 0;
        pr_info("lt9211_mipirxrhy port:%d \n",port);
	if (port == PORTA) {
		pr_info("\r\nPort A PHY Config\n");
		lt9211_write_reg(client, 0xff, 0x82);
		lt9211_write_reg(client, 0x02, 0x44);	//A/B: 8bit mode enable,MIPI mode use this.
		lt9211_write_reg(client, 0x04, 0xa0);	//Additional current 250uA;
		//Calibration should be done for 41 times;
		lt9211_write_reg(client, 0x05, 0x22);	//0x26 25uA;input clk from outer path.
		//Port A mipi rx rterm turn-off accelerate function enable
		lt9211_write_reg(client, 0x07, 0x9f);	//bandwidth:50M;2-inverter delay;
		//4 lanes clk enable
		lt9211_write_reg(client, 0x08, 0xfc);	//600mV;
		lt9211_write_reg(client, 0x09, 0x01);	//2-UI delay compared with input link clk
		lt9211_write_reg(client, 0x17, 0x0c);	//
		//lt9211_write_reg(client, 0xa9, 0x00);

		lt9211_write_reg(client, 0xff, 0x86);
		lt9211_write_reg(client, 0x33, 0x1b);	//Logic channel1-3 from physical channel1-3;
		//lt9211_write_reg(client, 0x49, 0x28); //Clock select ptb_mux_byte_clk
	} else {
		pr_info("\r\nPort B PHY Config\n");
		lt9211_write_reg(client, 0xff, 0x82);
		lt9211_write_reg(client, 0x02, 0x44);
		lt9211_write_reg(client, 0x04, 0xa1);
		lt9211_write_reg(client, 0x05, 0x26);
		lt9211_write_reg(client, 0x0d, 0x26);
		lt9211_write_reg(client, 0x07, 0x9f);
		lt9211_write_reg(client, 0x0f, 0x9f);
		lt9211_write_reg(client, 0x10, 0xfc);
		lt9211_write_reg(client, 0x11, 0x01);
		lt9211_write_reg(client, 0x17, 0x0c);
		lt9211_write_reg(client, 0x1d, 0x0c);
		//lt9211_write_reg(client, 0xa9, 0x00);

		lt9211_write_reg(client, 0xff, 0x86);
		lt9211_write_reg(client, 0x34, 0x1b);
		//lt9211_write_reg(client, 0x49, 0x28);
	}
	lt9211_write_reg(client, 0xff, 0x81);
	lt9211_write_reg(client, 0x20, 0x7f);
	lt9211_write_reg(client, 0x20, 0xff);	//Mlrx calib control logic soft reset.

	pr_info("lt9211_mipirxrhy end \n");
	return ret;
}

static int lt9211_mipirxdigital(struct i2c_client *client, bool port)
{
	int ret = 0;
        pr_info("lt9211_mipirxdigital port:%d\n",port);
	lt9211_write_reg(client, 0xff, 0x86);

	if (port == PORTA) {

/* Input MIPI data;Logic port A lp data from physical port A,
 * Logic port B lp data is from physical port B
 * Logic port A is from physical port A
 * Logic port B is from physical port B  //from 4A : 0x00
 */
		lt9211_write_reg(client, 0x30, 0x85);
	} else {

/* Input MIPI data;Logic port A lp data is from physical port B,
 * Logic port B lp data is from physical port B
 * Logic port A is from physical port B
 * Logic port B is from physical port B
 */
		lt9211_write_reg(client, 0x30, 0x8f);	//Input MIPI data;
	}

	//lt9211_write_reg(client, 0xff, 0x85); //0xd0
	//lt9211_write_reg(client, 0x88, 0x50); //MIPI settle setting

#ifdef MIPI_CSI
	pr_info("\r\nSet to CSI Mode");
	lt9211_write_reg(client, 0xff, 0xd0);	//CSI_EN
	lt9211_write_reg(client, 0x04, 0x10);
	lt9211_write_reg(client, 0x21, 0xc6);	//CSI_SEL
#else
	pr_info("\r\nSet to DSI Mode");
#endif

	lt9211_write_reg(client, 0xff, 0xd0);
	//lt9211_write_reg(client, 0x00, 0x00); //4Lane:0x00, 2Lane:0x02, 1Lane:0x01
	lt9211_write_reg(client, 0x02, 0x10);	//settle

	pr_info("lt9211_mipirxdigital end\n");
	return ret;
}

void LT9211_SetVideoTiming(struct i2c_client *client,
			   struct video_timing *video_format)
{
	lt9211_write_reg(client, 0xff, 0xd0);
	lt9211_write_reg(client, 0x0d, (u8) (video_format->vtotal >> 8));	//vtotal[15:8]
	lt9211_write_reg(client, 0x0e, (u8) (video_format->vtotal));	//vtotal[7:0]
	lt9211_write_reg(client, 0x0f, (u8) (video_format->vact >> 8));	//vactive[15:8]
	lt9211_write_reg(client, 0x10, (u8) (video_format->vact));	//vactive[7:0]
	lt9211_write_reg(client, 0x15, (u8) (video_format->vs));	//vs[7:0]
	lt9211_write_reg(client, 0x17, (u8) (video_format->vfp >> 8));	//vfp[15:8]
	lt9211_write_reg(client, 0x18, (u8) (video_format->vfp));	//vfp[7:0]

	lt9211_write_reg(client, 0x11, (u8) (video_format->htotal >> 8));	//htotal[15:8]
	lt9211_write_reg(client, 0x12, (u8) (video_format->htotal));	//htotal[7:0]
	lt9211_write_reg(client, 0x13, (u8) (video_format->hact >> 8));	//hactive[15:8]
	lt9211_write_reg(client, 0x14, (u8) (video_format->hact));	//hactive[7:0]
	lt9211_write_reg(client, 0x16, (u8) (video_format->hs));	//hs[7:0]
	lt9211_write_reg(client, 0x19, (u8) (video_format->hfp >> 8));	//hfp[15:8]
	lt9211_write_reg(client, 0x1a, (u8) (video_format->hfp));	//hfp[7:0]

}

void LT9211_TimingSet(struct i2c_client *client)
{
	u16 hact;
	u16 vact;
	u8 pa_lpn = 0;
	u8 value1, value2 = 0;

	lt9211_mipirx_logic_rst(client);
	mdelay(50);

	lt9211_write_reg(client, 0xff, 0xd0);
	lt9211_read_reg(client, 0x82, &value1);
	lt9211_read_reg(client, 0x83, &value2);
	hact = (value1 << 8) + value2;
	lt9211_read_reg(client, 0x84, &value1);
	mipi_fmt = (value1 & 0x0f);
	lt9211_read_reg(client, 0x85, &value1);
	lt9211_read_reg(client, 0x86, &value2);
	vact = (value1 << 8) + value2;
	lt9211_read_reg(client, 0x9c, &pa_lpn);
	if (mipi_fmt == 0x03) {
		pr_info("\r\nInput MIPI FMT: CSI_YUV422_16");
		hact = hact / 2;
	} else if (mipi_fmt == 0x0a) {
		pr_info("\r\nInput MIPI FMT: RGB888");
		hact = hact / 3;
	}

	pr_info("\nLT9211_TimingSet hact = %d vact=%d mipi_fmt=%d pa_lpn=%d ",
		hact, vact, mipi_fmt, pa_lpn);
	mdelay(50);
	if ((hact == video_1280x720_60Hz.hact)
	    && (vact == video_1280x720_60Hz.vact)) {
		pVideo_Format = &video_1280x720_60Hz;
		LT9211_SetVideoTiming(client, &video_1280x720_60Hz);
	} else if ((hact == video_1366x768_60Hz.hact)
		   && (vact == video_1366x768_60Hz.vact)) {
		pVideo_Format = &video_1366x768_60Hz;
		LT9211_SetVideoTiming(client, &video_1366x768_60Hz);
	} else if ((hact == video_1920x1080_60Hz.hact)
		   && (vact == video_1920x1080_60Hz.vact)) {
		pVideo_Format = &video_1920x1080_60Hz;
		LT9211_SetVideoTiming(client, &video_1920x1080_60Hz);
	} else if ((hact == video_1200x1920_60Hz.hact)
		   && (vact == video_1200x1920_60Hz.vact)) {
		pVideo_Format = &video_1200x1920_60Hz;
		LT9211_SetVideoTiming(client, &video_1200x1920_60Hz);
	} else {
		pVideo_Format = &video_1200x1920_60Hz;
		LT9211_SetVideoTiming(client, &video_1200x1920_60Hz);
		pr_info("\r\nvideo_none");
	}
}

void LT9211_DesscPll(struct i2c_client *client)
{
	lt9211_write_reg(client, 0xff, 0x82);
	lt9211_write_reg(client, 0x2d, 0x48);
	lt9211_write_reg(client, 0x35, 0x81);
	pr_info("\n pclk_khz: %d \n", pVideo_Format->pclk_khz);
}

void LT9211_MipiPcr(struct i2c_client *client)
{
	u8 loopx;
	u8 value;
	lt9211_write_reg(client, 0xff, 0xd0);
	lt9211_write_reg(client, 0x24, 0x70);
	lt9211_write_reg(client, 0x25, 0x30);
	lt9211_write_reg(client, 0x1e, 0x11);
	lt9211_write_reg(client, 0x0a, 0x02);
	lt9211_write_reg(client, 0x26, 0x1e);
	lt9211_write_reg(client, 0x27, 0x40);
	lt9211_write_reg(client, 0x2d, 0x60);	//PCR M overflow limit setting.
	lt9211_write_reg(client, 0x31, 0x10);	//PCR M underflow limit setting.

	lt9211_write_reg(client, 0x23, 0x20);
	lt9211_write_reg(client, 0x38, 0x10);
	lt9211_write_reg(client, 0x39, 0x20);
	lt9211_write_reg(client, 0x3a, 0x40);
	lt9211_write_reg(client, 0x3b, 0x60);
	lt9211_write_reg(client, 0x3f, 0x10);
	lt9211_write_reg(client, 0x40, 0x20);
	lt9211_write_reg(client, 0x41, 0x40);
	lt9211_write_reg(client, 0x42, 0x80);
	mdelay(50);
	lt9211_write_reg(client, 0xff, 0x81);
	lt9211_write_reg(client, 0x0B, 0xEE);
	lt9211_write_reg(client, 0x0B, 0xFE);

	mdelay(150);
	for (loopx = 0; loopx < 15; loopx++)	//Check pcr_stable
	{
		mdelay(200);
		lt9211_write_reg(client, 0xff, 0xd0);
		lt9211_read_reg(client, 0x87, &value);
		if (value & 0x08) {
			pr_info("\r\nLT9211 pcr stable");
			break;
		} else {
			pr_info("\r\nLT9211 pcr unstable!!!! value=%d ", value);
		}
	}
}

/*
static void LT9211_CheckInputMipiClock(struct i2c_client *client)
{
    u32 fm_value = 0;
    u8 value = 0;
    u32 fm_value_pre = 0;
    lt9211_write_reg(client,0xff,0x86);
    lt9211_write_reg(client,0x00,0x01);
    while(1)
    {
        fm_value_pre = fm_value;
        mdelay(500);
	 lt9211_read_reg(client, 0x08, &value);
        fm_value = (value & (0x0f));
        fm_value = (fm_value<<8) ;

	 lt9211_read_reg(client, 0x09, &value);
        fm_value = fm_value + value;
        fm_value = (fm_value<<8);

	 lt9211_read_reg(client, 0x0a, &value);
        fm_value = fm_value + value;
        pr_info("\r\n LT9211_CheckInputMipiClock portb clock: %d %d",fm_value,fm_value_pre);
        if( (fm_value_pre>1000) && (fm_value>1000) ) {
            break;
        }
    }
    pr_info("\r\n mipi Input!!!!! ok");
}*/
static int lt9211_mipitxpll(struct i2c_client *client)
{
	int ret = 0;
	u8 value;
	u8 loopx;
	pr_info("lt9211_mipitxpll begin\n");
	lt9211_write_reg(client, 0xff, 0x82);
	lt9211_write_reg(client, 0x36, 0x03);	//b7:txpll_pd
	lt9211_write_reg(client, 0x37, 0x28);
	lt9211_write_reg(client, 0x38, 0x44);
	lt9211_write_reg(client, 0x3a, 0x98);
	// lt9211_write_reg(client,0x3b,0x44);

	lt9211_write_reg(client, 0xff, 0x87);
	lt9211_write_reg(client, 0x13, 0x00);
	lt9211_write_reg(client, 0x13, 0x80);
	mdelay(100);
        pr_info("lt9211_mipitxpll \n");
	for (loopx = 0; loopx < 10; loopx++) {
		lt9211_write_reg(client, 0xff, 0x87);
		lt9211_read_reg(client, 0x1f, &value);
		if (value & BIT_7) {
			lt9211_read_reg(client, 0x20, &value);
			if (!(value & BIT_7)) {
				pr_info("\r\nLT9211 tx pll unlocked\n");
			} else {
				pr_info("\r\nLT9211 tx pll cal done value:%d\n",
					value);
				break;
			}
		} else {
			pr_info("\r\nLT9211 tx pll unlocked value: %d \n",
				value);
		}
	}

	return ret;
}

static int lt9211_mipitxphy(struct i2c_client *client)
{
	int ret = 0;

	lt9211_write_reg(client, 0xff, 0x82);
	lt9211_write_reg(client, 0x62, 0x00);	//ttl output disable
	lt9211_write_reg(client, 0x3b, 0x32);	//mipi en
	lt9211_write_reg(client, 0x46, 0xaa);	//pre-emphasis
	lt9211_write_reg(client, 0x47, 0x95);	//pre-emphasis

	lt9211_write_reg(client, 0xff, 0x85);
	lt9211_write_reg(client, 0x88, 0x50);

	lt9211_write_reg(client, 0xff, 0x86);
	lt9211_write_reg(client, 0x40, 0x80);	//tx_src_sel
	/*port src sel */
	lt9211_write_reg(client, 0x41, 0x01);
	lt9211_write_reg(client, 0x42, 0x23);
	lt9211_write_reg(client, 0x43, 0x40);	//Port A MIPI Lane Swap
	lt9211_write_reg(client, 0x44, 0x12);
	lt9211_write_reg(client, 0x45, 0x34);	//Port B MIPI Lane Swap
	pr_info("lt9211_mipitxphy end\n");
	return ret;
}

static int lt9211_mipitxdigital(struct i2c_client *client)
{
	int ret = 0;
	pr_info("lt9211_mipitxdigital begin\n");
	lt9211_write_reg(client, 0xff, 0xd4);
	lt9211_write_reg(client, 0x1c, 0x30);	//hs_rqst_pre
	lt9211_write_reg(client, 0x1d, 0x0a);	//lpx
	lt9211_write_reg(client, 0x1e, 0x06);	//prpr
	lt9211_write_reg(client, 0x1f, 0x0a);	//trail
	lt9211_write_reg(client, 0x21, 0x00);	//[5]byte_swap,[0]burst_clk

	lt9211_write_reg(client, 0xff, 0xd4);
	lt9211_write_reg(client, 0x16, 0x55);
	lt9211_write_reg(client, 0x10, 0x01);
	lt9211_write_reg(client, 0x11, 0x00);	//read delay
	lt9211_write_reg(client, 0x13, 0x0f);	//bit[5:4]:lane num, bit[2]:bllp,bit[1:0]:vid_mode
	lt9211_write_reg(client, 0x14, 0x20);	//bit[5:4]:data typ,bit[2:0]:fmt sel 000:rgb888
	lt9211_write_reg(client, 0x21, 0x00);
	pr_info("lt9211_mipitxdigital end\n");
	return ret;
}

void LT9211_SetTxTiming(struct i2c_client *client)
{
	u16 hact, vact;
	u16 hs, vs;
	u16 hbp, vbp;
	u16 htotal, vtotal;
	u16 hfp, vfp;

	hact = pVideo_Format->hact;
	vact = pVideo_Format->vact;
	htotal = pVideo_Format->htotal;
	vtotal = pVideo_Format->vtotal;
	hs = pVideo_Format->hs;
	vs = pVideo_Format->vs;
	hfp = pVideo_Format->hfp;
	vfp = pVideo_Format->vfp;
	hbp = pVideo_Format->hbp;
	vbp = pVideo_Format->vbp;

	lt9211_write_reg(client, 0xff, 0xd4);
	lt9211_write_reg(client, 0x04, 0x08);	//hs[7:0] not care
	lt9211_write_reg(client, 0x05, 0x08);	//hbp[7:0] not care
	lt9211_write_reg(client, 0x06, 0x08);	//hfp[7:0] not care
	lt9211_write_reg(client, 0x07, (u8) (hact >> 8));	//hactive[15:8]
	lt9211_write_reg(client, 0x08, (u8) (hact));	//hactive[7:0]

	lt9211_write_reg(client, 0x09, (u8) (vs));	//vfp[7:0]
	lt9211_write_reg(client, 0x0a, 0x00);	//bit[3:0]:vbp[11:8]
	lt9211_write_reg(client, 0x0b, (u8) (vbp));	//vbp[7:0]
	lt9211_write_reg(client, 0x0c, (u8) (vact >> 8));	//vcat[15:8]
	lt9211_write_reg(client, 0x0d, (u8) (vact));	//vcat[7:0]
	lt9211_write_reg(client, 0x0e, (u8) (vfp >> 8));	//vfp[11:8]
	lt9211_write_reg(client, 0x0f, (u8) (vfp));	//vfp[7:0]
}

void LT9211_DebugInfo(struct i2c_client *client)
{
#ifdef _uart_debug_
	u32 fm_value;
	u8 value = 0;

	lt9211_write_reg(client, 0xff, 0x86);
	lt9211_write_reg(client, 0x00, 0x12);
	mdelay(100);
	fm_value = 0;
	lt9211_read_reg(client, 0x08, &value);
	fm_value = (value & (0x0f));
	fm_value = (fm_value << 8);
	lt9211_read_reg(client, 0x09, &value);
	fm_value = fm_value + value;
	fm_value = (fm_value << 8);
	lt9211_read_reg(client, 0x0a, &value);
	fm_value = fm_value + value;
	pr_info("\r\nmipi output byte clock: %d", fm_value);

	lt9211_write_reg(client, 0xff, 0x86);
	lt9211_write_reg(client, 0x00, 0x0a);
	mdelay(100);
	fm_value = 0;
	lt9211_read_reg(client, 0x08, &value);
	fm_value = (value & (0x0f));
	fm_value = (fm_value << 8);
	lt9211_read_reg(client, 0x09, &value);
	fm_value = fm_value + value;
	fm_value = (fm_value << 8);
	lt9211_read_reg(client, 0x0a, &value);
	fm_value = fm_value + value;
	pr_info("\n output pixclk:  %d", fm_value);
#endif
}

#if 0
int lt9211_mipicheckdebug(struct i2c_client *client)
{
	int ret = 0;
	u_char hactive, vactive, value;

	mdelay(200);

	lt9211_write_reg(client, 0xff, 0xd0);

	lt9211_read_reg(client, 0x82, &hactive);
	value = hactive << 8;
	lt9211_read_reg(client, 0x83, &hactive);
	hactive = value + hactive;
	hactive = hactive / 3;

	lt9211_read_reg(client, 0x85, &vactive);
	value = vactive << 8;
	lt9211_read_reg(client, 0x86, &vactive);
	vactive = value + vactive;

	pr_info("\n lt9211_mipicheckdebug hactive, vactive = %d , %d\n",
		hactive, vactive);

	return ret;
}

int lt9211_clockcheckdebug(struct i2c_client *client)
{
	int ret = 0;
	u32 fm_value;
	u_char value;

	lt9211_write_reg(client, 0xff, 0x86);
	lt9211_write_reg(client, 0x00, 0x01);

	mdelay(200);

	lt9211_read_reg(client, 0x08, &value);
	fm_value = (value) & (0x0f);

	lt9211_read_reg(client, 0x09, &value);
	fm_value = (fm_value << 8) + value;

	lt9211_read_reg(client, 0x0a, &value);
	fm_value = (fm_value << 8) + value;

	pr_info("\r\nmrx byte clock: %d\n", fm_value);

	lt9211_write_reg(client, 0x00, 0x11);

	mdelay(200);

	lt9211_read_reg(client, 0x08, &value);
	fm_value = (value) & (0x0f);

	lt9211_read_reg(client, 0x09, &value);
	fm_value = (fm_value << 8) + value;

	lt9211_read_reg(client, 0x0a, &value);
	fm_value = (fm_value << 8) + value;

	pr_info("\r\nmtx byte clock: %d \n", fm_value);

	return ret;
}
#endif

static int lt9211_ttlrx_d_clk(struct i2c_client *client)
{
	int ret = 0;
	u32 fm_value = 0;
	u_char value = 0;

	lt9211_write_reg(client, 0xff, 0x86);
	lt9211_write_reg(client, 0x00, 0x14);

	mdelay(50);

	lt9211_read_reg(client, 0x08, &value);
	fm_value = (value) & (0x0f);

	lt9211_read_reg(client, 0x09, &value);
	fm_value = (fm_value << 8) + value;

	lt9211_read_reg(client, 0x0a, &value);
	fm_value = (fm_value << 8) + value;

	pr_info("\r\nttlrx_d_clk: %d\n", fm_value);

	return ret;
}

void lt9211_mipitxcheck(struct i2c_client *client)
{
	u8 i = 0;
	u_char vtotalcnt = 0;
	u32 value32 = 0;
	u_char htotalcnt = 0;
	u16 value16 = 0;

	lt9211_write_reg(client, 0xff, 0x82);
	lt9211_write_reg(client, 0x24, 0x04);
	lt9211_write_reg(client, 0x25, 0x04);
	lt9211_write_reg(client, 0x29, 0x00);
	lt9211_write_reg(client, 0x2a, 0x00);
	lt9211_write_reg(client, 0x2b, 0xa0);
	lt9211_write_reg(client, 0x61, 0x05);
	lt9211_write_reg(client, 0x3b, 0x3b);
	lt9211_write_reg(client, 0x5c, 0xaa);
	lt9211_write_reg(client, 0x5d, 0xbf);
	lt9211_write_reg(client, 0x25, 0x00);
	lt9211_write_reg(client, 0xa9, 0x00);

	lt9211_ttlrx_d_clk(client);

	mdelay(50);

	pr_info("\r\nmipi tx check start:\n");

	for (i = 0; i < 10; i++) {
		lt9211_write_reg(client, 0xff, 0x86);

		lt9211_read_reg(client, 0x66, &vtotalcnt);
		value32 = (vtotalcnt << 8);
		lt9211_read_reg(client, 0x67, &vtotalcnt);
		vtotalcnt = value32 + vtotalcnt;

		value32 = (vtotalcnt << 8);
		lt9211_read_reg(client, 0x68, &vtotalcnt);
		vtotalcnt = value32 + vtotalcnt;

		lt9211_read_reg(client, 0x7C, &htotalcnt);
		value16 = (htotalcnt << 8);
		lt9211_read_reg(client, 0x7D, &htotalcnt);
		htotalcnt = value16 + htotalcnt;

		if ((vtotalcnt < 50) || (htotalcnt < 50)) {
			pr_info("\r\nGet the abnormall value!!");
			pr_info("\r\nvtotalcnt = %d; htotalcnt = %d", vtotalcnt,
				htotalcnt);
		}
	}
	if (i == 10) {
		pr_info("\r\nRead 10 times, correct!!\n");
		pr_info("\r\nvtotalcnt = %d; htotalcnt = %d\n", vtotalcnt,
			htotalcnt);
		lt9211_write_reg(client, 0xff, 0x82);
		lt9211_write_reg(client, 0x25, 0x09);
		lt9211_write_reg(client, 0x3b, 0x33);
		lt9211_write_reg(client, 0x5c, 0x00);
		lt9211_write_reg(client, 0x5d, 0x00);
	} else {
//  reset mipi tx, recheck
		pr_info("\r\nReset mipi tx, recheck\n");
		lt9211_write_reg(client, 0xff, 0x81);
		lt9211_write_reg(client, 0x20, 0xFD);
		mdelay(5);
		lt9211_write_reg(client, 0x20, 0xFF);
		mdelay(10);
	}
}

static ssize_t lt9211_addr_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct lt9211_dev *lt9211 = i2c_get_clientdata(client);

	return sprintf(buf, "addr=0x%2x\n", lt9211->addr);
}

static ssize_t lt9211_addr_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct lt9211_dev *lt9211 = i2c_get_clientdata(client);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;
	lt9211->addr = value;

	i2c_set_clientdata(client, lt9211);
	return len;
}

static DEVICE_ATTR(addr, S_IRUGO | S_IWUSR,
		   lt9211_addr_show, lt9211_addr_store);

static ssize_t lt9211_data_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct lt9211_dev *lt9211 = i2c_get_clientdata(client);
	u8 data = 0;
	u8 ret;

	ret = lt9211_read_reg(lt9211->client, lt9211->addr, &data);

	return sprintf(buf, "data=0x%2x\n", data);
}

static ssize_t lt9211_data_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct lt9211_dev *lt9211 = i2c_get_clientdata(client);
	int ret;
	unsigned int value;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;

	ret = lt9211_write_reg(lt9211->client, lt9211->addr, value);
	if (ret) {
		pr_info("write addr 0x%2x is wrong\n", lt9211->addr);
		return ret;
	}

	return len;
}

static DEVICE_ATTR(data, S_IRUGO | S_IWUSR,
		   lt9211_data_show, lt9211_data_store);

static ssize_t lt9211_port_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct lt9211_dev *lt9211 = i2c_get_clientdata(client);
	int ret;
	unsigned int i,j, value=0;
	unsigned char data=0;
	//unsigned char chipid[3] = { 0 };

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;
	#if 1//for debug test
	for(j=0;j<value;j++){
		lt9211_write_reg(lt9211->client, 0xff, 0x86);
		lt9211_write_reg(lt9211->client, 0x20, 0x22);
		for(i = 0; i < 5; i ++) {
			lt9211_read_reg(lt9211->client, 0x86+i, &data);
			pr_info("LT9211_MipiPcr times:%d reg:0x%2x,value:0x%2x \n", j, 0x86+i, data);
			mdelay(10);
			// 0x86 0x87: һ��ռ��ϵͳʱ�� 0x88 0x89 0x8a : һ֡��ʱ��
		}
		mdelay(50);
	}
	#else

	lt9211->port = value;
	lt9211_hardware_rst(lt9211->client);

	//read chip id
	lt9211_write_reg(lt9211->client, 0xff, 0x81);
	lt9211_read_reg(lt9211->client, 0x00, &chipid[0]);
	lt9211_read_reg(lt9211->client, 0x01, &chipid[1]);
	lt9211_read_reg(lt9211->client, 0x02, &chipid[2]);
	lt9211->chipid = (chipid[1] << 8) | chipid[0];
	pr_info("lt9211_port_store chipid = 0x%6x \n", lt9211->chipid);
	lt9211_system_init(lt9211->client);
	lt9211_mipirxrhy(lt9211->client, lt9211->port);
	lt9211_mipirxdigital(lt9211->client, lt9211->port);
	lt9211_mipitxpll(lt9211->client);
	lt9211_mipitxphy(lt9211->client);
	lt9211_mipitxdigital(lt9211->client);

	//lt9211_mipicheckdebug(lt9211->client);
	//lt9211_clockcheckdebug(lt9211->client);
	i2c_set_clientdata(client, lt9211);
	#endif
	return len;
}

static ssize_t lt9211_port_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct lt9211_dev *lt9211 = i2c_get_clientdata(client);

	if ((lt9211->port) == 0)
		return sprintf(buf, "port = PORTA\n");
	else
		return sprintf(buf, "port = PORTB\n");
}

static DEVICE_ATTR(port, S_IRUGO | S_IWUSR,
		   lt9211_port_show, lt9211_port_store);

static struct attribute *lt9211_attributes[] = {
	&dev_attr_addr.attr,
	&dev_attr_data.attr,
	&dev_attr_port.attr,
	NULL,
};

static const struct attribute_group lt9211_attribute_group = {
	.attrs = lt9211_attributes,
};

static int of_lt9211_dt(struct device *dev)
{
	struct device_node *np_lt9211 = dev->of_node;
//	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
//	struct lt9211_dev *lt9211 = i2c_get_clientdata(client);
	unsigned int sys1p8, sys3p3;
	int ret = 0;

	if (!np_lt9211) {
		dev_err(dev, "%s:No np_lt9211 [%d]\n", __func__);
		return -EINVAL;
	}

	sys1p8 = of_get_named_gpio(np_lt9211, "lontium,sys1p8-gpio", 0);

	g_sys1p8 = sys1p8;
	if (gpio_is_valid(sys1p8)) {
		ret =
			gpio_request(sys1p8, "sys1p8");
			gpio_direction_output(sys1p8, 1);
			gpio_free(sys1p8);
		if (ret) {
			dev_err(dev, "%s:Unable to request sys1p8 [%d]\n",
				__func__, sys1p8);
		}
	} else {
		dev_err(dev, "%s:Failed to get sys1p8 gpio\n", __func__);
		//return -EINVAL;
	}

	sys3p3 = of_get_named_gpio(np_lt9211, "lontium,sys3p3-gpio", 0);

	g_sys3p3 = sys3p3;
	if (gpio_is_valid(sys3p3)) {
		ret =
			gpio_request(sys3p3, "sys3p3");
			gpio_direction_output(sys3p3, 1);
			gpio_free(sys3p3);
		if (ret) {
			dev_err(dev, "%s:Unable to request sys3p3 [%d]\n",
				__func__, sys3p3);
		}
	} else {
		dev_err(dev, "%s:Failed to get sys3p3 gpio\n", __func__);
		//return -EINVAL;
	}
	mdelay(20);

//	lt9211->irq_gpio = of_get_named_gpio(np_lt9211, "lt9211,irq-gpio", 0);
/*
	if (gpio_is_valid(lt9211->irq_gpio)) {
		ret =
		    gpio_request_one(lt9211->irq_gpio, GPIOF_DIR_IN,
				     "lt9211,tsp-int");
		if (ret) {
			dev_err(dev, "%s:Unable to request tsp_int [%d]\n",
				__func__, lt9211->irq_gpio);
			ret = -EINVAL;
			goto err_gpio;
		}
	} else {
		dev_err(dev, "%s:Failed to get irq gpio\n", __func__);
		return -EINVAL;
	}

	client->irq = gpio_to_irq(lt9211->irq_gpio);
	lt9211->irq = client->irq;

	pr_info("%s: irq=%d, irq->gpio=%d\n", __func__,
		lt9211->irq, lt9211->irq_gpio);

	lt9211->irq_type = IRQF_TRIGGER_LOW | IRQF_ONESHOT;

	ret = request_threaded_irq(lt9211->irq, NULL,
				   NULL,
				   lt9211->irq_type, "lt9211-irq", lt9211);

	if (ret) {
		dev_err(lt9211->dev, "Failed to request IRQ %d: %d\n",
			lt9211->irq, ret);
		ret = -EINVAL;
		goto err_gpio;
	}
*/
//	i2c_set_clientdata(client, lt9211);

	return ret;
}
/*
static void lt9211_hardware_rst(struct i2c_client *client)
{
	struct pinctrl *pinctrl_rst;

	pinctrl_rst = devm_pinctrl_get_select(&client->dev, "on_rst");
	mdelay(20);
	pinctrl_rst = devm_pinctrl_get_select(&client->dev, "off_rst");

	if (IS_ERR(pinctrl_rst))
		dev_err(&client->dev, "%s: Failed to configure rst pin\n",
			__func__);

	mdelay(50);
	pinctrl_rst = devm_pinctrl_get_select(&client->dev, "on_rst");

	if (IS_ERR(pinctrl_rst))
		dev_err(&client->dev, "%s: Failed to configure rst pin\n",
			__func__);
}
*/
void lt9211_init_late(void);
void lt9211_init(void)		//(struct work_struct *work)
{
	struct i2c_client *client = NULL;
	//unsigned char chipid[3] = { 0 };
	if (IS_ERR(lt9211)) {
		pr_err("%s:lt9211 is error !!!\n");
		return;
	}

	mutex_lock(&lt9211->i2c_lock);
	client = lt9211->client;
#if IS_INIT_LT9211_IN_KERNEL
	pr_info("lt9211 chipid = 0x%6x \n", lt9211->chipid);
	if (lt9211->chipid == LT9211_CHIP_ID) {
		lt9211_mipirx_logic_rst(lt9211->client);
		lt9211_system_init(lt9211->client);
		lt9211_mipirxrhy(lt9211->client, lt9211->port);
		lt9211_mipirxdigital(lt9211->client, lt9211->port);
		//lt9211_mipitxpll(lt9211->client);

		LT9211_MipiClkCheck(lt9211->client);
		//LT9211_CheckInputMipiClock(lt9211->client);

		LT9211_TimingSet(lt9211->client);

		if (pVideo_Format != NULL) {
			//LT9211_MipiByteClkDebug(lt9211->client);
			LT9211_DesscPll(lt9211->client);
			LT9211_MipiPcr(lt9211->client);

			/********MIPI OUTPUT CONFIG********/
			lt9211_mipitxpll(lt9211->client);
			lt9211_mipitxphy(lt9211->client);
			LT9211_SetTxTiming(lt9211->client);

		}
		//lt9211_mipicheckdebug(lt9211->client);
		//lt9211_clockcheckdebug(lt9211->client);
		// lt9211_mipitxcheck(lt9211->client);
	}

#endif
//      return 0;
	lt9211_init_late();
	mutex_unlock(&lt9211->i2c_lock);
}

EXPORT_SYMBOL(lt9211_init);

void lt9211_init_late(void)
{
	struct pinctrl *pinctrl_irq;

	if (lt9211->chipid == LT9211_CHIP_ID) {

		lt9211_mipitxdigital(lt9211->client);

		LT9211_DebugInfo(lt9211->client);
		LT9211_VideoCheckDebug(lt9211->client);
	}
        pinctrl_irq = devm_pinctrl_get_select(&lt9211->client->dev, "lcd_bl_on");
	if (IS_ERR(pinctrl_irq))
		dev_err(&lt9211->client->dev, " Failed to configure lcd_bl_on pin\n");
}

static int lt9211_probe(struct i2c_client *client,
			const struct i2c_device_id *dev_id)
{
	int ret = 0;
	#if IS_INIT_LT9211_IN_KERNEL
	unsigned char chipid[3] = { 0 };
	#endif
	struct pinctrl *pinctrl_irq;

	pr_info("Enter lt9211_probe\n");

	lt9211 =
	    /*devm_kzalloc */ kzalloc(sizeof(struct lt9211_dev), GFP_KERNEL);
	if (!lt9211) {
		dev_err(&client->dev, "%s: Failed to alloc mem for lt9211\n",
			__func__);
		ret = -ENOMEM;
		goto err;
	}

	lt9211->addr = 0x00;
	lt9211->data = 0x00;

	lt9211->irq_gpio = 0;
	lt9211->irq_type = 0x00;
	lt9211->irq = 0;

	lt9211->port = PORTA;

	lt9211->dev = &client->dev;
	lt9211->client = client;

	mutex_init(&lt9211->i2c_lock);
	mutex_init(&lt9211->init_lock);

	i2c_set_clientdata(client, lt9211);

	if (client->dev.of_node) {
		ret = of_lt9211_dt(&client->dev);
		if (ret < 0) {
			dev_err(&client->dev, "Failed to get device of_node\n");
			goto err;
		}
	}
#if 0
	lt9211_hardware_rst(client);

#endif
	//read chip id
	lt9211_write_reg(lt9211->client, 0xff, 0x81);
	lt9211_read_reg(lt9211->client, 0x00, &chipid[0]);
	lt9211_read_reg(lt9211->client, 0x01, &chipid[1]);
	lt9211_read_reg(lt9211->client, 0x02, &chipid[2]);

	lt9211->chipid =  (chipid[1] << 8) | chipid[0];
	pr_info("lt9211 chipid = 0x%6x \n", lt9211->chipid);
#if 0//IS_INIT_LT9211_IN_KERNEL
	if (lt9211->chipid == LT9211_CHIP_ID) {
		lt9211_system_init(lt9211->client);
		lt9211_mipirxrhy(lt9211->client, lt9211->port);
		lt9211_mipirxdigital(lt9211->client, lt9211->port);
		lt9211_mipitxpll(lt9211->client);

		LT9211_MipiClkCheck(lt9211->client);

		LT9211_TimingSet(lt9211->client);

		if (pVideo_Format != NULL) {
			LT9211_MipiByteClkDebug(lt9211->client);
			LT9211_DesscPll(lt9211->client);
			LT9211_MipiPcr(lt9211->client);

			/********MIPI OUTPUT CONFIG********/
			lt9211_mipitxphy(lt9211->client);
			lt9211_mipitxpll(lt9211->client);
			LT9211_SetTxTiming(lt9211->client);
			lt9211_mipitxdigital(lt9211->client);
			LT9211_DebugInfo(lt9211->client);
			LT9211_VideoCheckDebug(lt9211->client);
		}
		//lt9211_mipicheckdebug(lt9211->client);
		//lt9211_clockcheckdebug(lt9211->client);
		// lt9211_mipitxcheck(lt9211->client);
	}
#endif
	i2c_set_clientdata(client, lt9211);

	ret = sysfs_create_group(&client->dev.kobj, &lt9211_attribute_group);
	if (ret < 0) {
		dev_err(&client->dev, "Sysfs registration failed\n");
		goto err_sysfs;
	}

        pinctrl_irq = devm_pinctrl_get_select(&client->dev, "off_state");
	if (IS_ERR(pinctrl_irq))
		dev_err(&client->dev, " Failed to configure irq pin\n");

        return ret;

err_sysfs:
	sysfs_remove_group(&lt9211->dev->kobj, &lt9211_attribute_group);
err:
	kfree(lt9211);
	i2c_set_clientdata(client, NULL);

	return ret;
}

static int lt9211_remove(struct i2c_client *client)
{

	struct lt9211_dev *lt9211 = i2c_get_clientdata(client);
	struct pinctrl *pinctrl_irq;

	pinctrl_irq = devm_pinctrl_get_select(&client->dev, "off_state");
	if (IS_ERR(pinctrl_irq))
		dev_err(&client->dev, " Failed to configure irq pin\n");

	gpio_free(lt9211->irq_gpio);
	free_irq(lt9211->irq, lt9211);

	sysfs_remove_group(&lt9211->dev->kobj, &lt9211_attribute_group);

	kfree(lt9211);
	pr_info("%s: kfree \n", __func__);

	return 0;
}
/*
static int lt9211_suspend(struct device *dev)
{

	struct pinctrl *pinctrl_irq;
	//struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	//struct lt9211_dev *lt9211 = i2c_get_clientdata(client);

	//if (device_may_wakeup(dev))
		//enable_irq_wake(lt9211->irq);
	pr_info("%s: enter \n", __func__);
	pinctrl_irq = devm_pinctrl_get_select(dev, "off_state");
	if (IS_ERR(pinctrl_irq))
		dev_err(dev, " Failed to configure irq pin\n");

	return 0;
}

static int lt9211_resume(struct device *dev)
{

	struct pinctrl *pinctrl_irq;
	//struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	//struct lt9211_dev *lt9211 = i2c_get_clientdata(client);

	//if (device_may_wakeup(dev))
		//disable_irq_wake(lt9211->irq);
	pr_info("%s: enter \n", __func__);

	pinctrl_irq = devm_pinctrl_get_select(dev, "on_state");
	if (IS_ERR(pinctrl_irq))
		dev_err(dev, " Failed to configure irq pin\n");

	return 0;
}

const struct dev_pm_ops lt9211_pm_ops = {
	.suspend = lt9211_suspend,
	.resume = lt9211_resume,
};
*/
static const struct of_device_id lt9211_id_table[] = {
	{.compatible = "lontium,lt9211",},
	{},
};

static struct i2c_driver lt9211_i2c_driver = {
	.driver = {
		   .name = "lt9211",
		   .owner = THIS_MODULE,
//		   .pm = &lt9211_pm_ops,
		   .of_match_table = lt9211_id_table,
		   },
	.probe = lt9211_probe,
	.remove = lt9211_remove,
};

static int __init lt9211_i2c_init(void)
{
	pr_info("%s:%s\n", "lt9211", __func__);
	return i2c_add_driver(&lt9211_i2c_driver);
}

device_initcall(lt9211_i2c_init);

static void __exit lt9211_i2c_exit(void)
{
	i2c_del_driver(&lt9211_i2c_driver);
}

module_exit(lt9211_i2c_exit);

MODULE_DESCRIPTION
    ("Lontium lt9211 MIPI DSI/CSI-2/Dual-Port LVDS and TTL convertor driver");
MODULE_AUTHOR("Han Ye <yanghs0702@thundersoft.com>");
MODULE_LICENSE("GPL v2");
