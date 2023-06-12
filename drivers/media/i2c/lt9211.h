/*
 *  lt9211.h
 *
 *  Copyright (c) 2020 Han Ye <yanghs0702@thundersoft.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation.
 */
#ifndef _LT9211_H
#define _LT9211_H

#define PORTA    0
#define PORTB    1
#define LT9211_CHIP_ID    0x0118
#define BIT_7    0x80

#define IS_INIT_LT9211_IN_KERNEL	1
//#define _uart_debug_

struct lt9211_dev {
	struct device *dev;
	struct i2c_client *client;	/* 7bit device addr = 0x2D */
	struct mutex i2c_lock;
	struct mutex init_lock;

	int irq_type;
	unsigned irq_gpio;
	int irq;

	bool port;		/*port a or b */

	unsigned char addr;
	unsigned char data;
	unsigned int chipid;
};

//////////option for debug///////////
struct video_timing {
	unsigned short hfp;
	unsigned short hs;
	unsigned short hbp;
	unsigned short hact;
	unsigned short htotal;
	unsigned short vfp;
	unsigned short vs;
	unsigned short vbp;
	unsigned short vact;
	unsigned short vtotal;
	unsigned int pclk_khz;
};
#endif
