/*
 * IT8951 Display driver
 *
 * Copyright (C) 2021 E-ROUM Co., Ltd.
 * Author: Terry Choi <dw.choi@e-roum.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef DEBUG
#define DEBUG
#endif

#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/acpi.h>
#include <linux/gpio.h>
#include <linux/time.h>
#include <linux/uaccess.h>

#include "it8951_display.h"

#define DEBUG_FPS

extern void it8951_spi_write(u8 *cmd, u32 size, u8 bpw);
extern void it8951_spi_read(u8 *cmd, u32 size);
extern int get_it8951_hrdy_status(void);
extern void set_it8951_spi_cs(int status);

#define CS(X) set_it8951_spi_cs(X)
#define HRDY get_it8951_hrdy_status()

#define DEFAULT_BPW 16
#define PIXEL_BPW 32
#define DEFAULT_SIZE 2

static DEFINE_MUTEX(display_lock);

static int display_skip = 0;


/*-------------------------------------------------------------------------*/

//-----------------------------------------------------------
//Wait for host data Bus Ready
//-----------------------------------------------------------
static void LCDWaitForReady(void)
{
	//Regarding to HRDY
	//you may need to use a GPIO pin connected to HRDY of IT8951
	u32 ulData = (u32)HRDY;
	while(ulData == 0)
	{
		//Get status of HRDY
		ulData = (u32)HRDY;
	}
}

//-------------------------------------------------------------------
// SPI Interface basic function
//-------------------------------------------------------------------
#define  MY_WORD_SWAP(x) ( ((x & 0xff00)>>8) | ((x & 0x00ff)<<8) )

//-------------------------------------------------------------------
//  Write Command code
//-------------------------------------------------------------------

static void LCDWriteCmdCode (u16 wCmd)
{	
	short wPreamble = 0; 

	//pr_err("%s() 0x%x\n", __func__, wCmd);

	//Set Preamble for Write Command
	wPreamble = 0x6000; 
	
	//Send Preamble
	wPreamble	= MY_WORD_SWAP(wPreamble);
	LCDWaitForReady();
	CS(0);
	it8951_spi_write((u8*)&wPreamble, DEFAULT_SIZE, DEFAULT_BPW);

	//Send Command
	wCmd		= MY_WORD_SWAP(wCmd);
	LCDWaitForReady();
	it8951_spi_write((u8*)&wCmd, DEFAULT_SIZE, DEFAULT_BPW);
	CS(1);
}
//-------------------------------------------------------------------
//  Write 1 Word Data(2-bytes)
//-------------------------------------------------------------------
static void LCDWriteData(u16 usData)
{
	short wPreamble	= 0;

	//set type
	wPreamble = 0x0000;
	
	//Send Preamble
	wPreamble = MY_WORD_SWAP(wPreamble);
	LCDWaitForReady();
	CS(0);
	it8951_spi_write((u8*)&wPreamble, DEFAULT_SIZE, DEFAULT_BPW);
	//Send Data
	usData = MY_WORD_SWAP(usData);
	LCDWaitForReady();
	it8951_spi_write((u8*)&usData, DEFAULT_SIZE, DEFAULT_BPW);
	CS(1);
}   
//-------------------------------------------------------------------
//  Burst Write Data
//-------------------------------------------------------------------
static void LCDWriteNData(u16* pwBuf, u32 ulSizeWordCnt)
{
	short wPreamble	= 0;

	//set type
	wPreamble = 0x0000;
	//Send Preamble
	wPreamble = MY_WORD_SWAP(wPreamble);
	LCDWaitForReady();
	CS(0);
	it8951_spi_write((u8*)&wPreamble, DEFAULT_SIZE, DEFAULT_BPW);
	
	//Send Data
	LCDWaitForReady();
	it8951_spi_write((u8*)pwBuf, ulSizeWordCnt*2, PIXEL_BPW);
	CS(1);
}
//-------------------------------------------------------------------
// Read 1 Word Data
//-------------------------------------------------------------------
static u16 LCDReadData(void)
{
	u16 wPreamble	= 0;
    u16 wRData = 0; 
	u16 wDummy = 0;

	//set type and direction
	wPreamble = 0x1000;
	
	//Send Preamble before reading data
	wPreamble = MY_WORD_SWAP(wPreamble);
	LCDWaitForReady();
	CS(0);
	it8951_spi_write((u8*)&wPreamble, DEFAULT_SIZE, DEFAULT_BPW);

	//Read Dummy (under IT8951 SPI to I80 spec)
	LCDWaitForReady();
	it8951_spi_read((u8*)&wDummy, DEFAULT_SIZE);
	
	//Read Data
	LCDWaitForReady();
	it8951_spi_read((u8*)&wRData, DEFAULT_SIZE);
	CS(1);
	
    wRData = MY_WORD_SWAP(wRData);
	return wRData;
}
//-------------------------------------------------------------------
//  Read Burst N words Data
//-------------------------------------------------------------------
static void LCDReadNData(u16* pwBuf, u32 ulSizeWordCnt)
{
	u16 wPreamble	= 0;
	u16 wDummy;
	u32 i;

	//set type and direction
	wPreamble = 0x1000;
	
	//Send Preamble before reading data
	wPreamble = MY_WORD_SWAP(wPreamble);
	LCDWaitForReady();
	CS(0);
	it8951_spi_write((u8*)&wPreamble, DEFAULT_SIZE, DEFAULT_BPW);

	//Read Dummy (under IT8951 SPI to I80 spec)
	LCDWaitForReady();
	it8951_spi_read((u8*)&wDummy, DEFAULT_SIZE);
	
	//Read Data
	LCDWaitForReady();
	it8951_spi_read((u8*)pwBuf, ulSizeWordCnt * 2);
	CS(1);

    //Convert Endian (depends on your host)
	for(i=0;i< ulSizeWordCnt ; i++)
    {
        pwBuf[i] = MY_WORD_SWAP(pwBuf[i]);
    }
}

//-----------------------------------------------------------
//Write command to host data Bus with aruments
//-----------------------------------------------------------
static void LCDSendCmdArg(u16 usCmdCode,u16* pArg, u16 usNumArg)
{
     u16 i;
     //Send Cmd code
     LCDWriteCmdCode(usCmdCode);
     //Send Data
     for(i=0;i<usNumArg;i++)
     {
         LCDWriteData(pArg[i]);
     }
}

//-----------------------------------------------------------
//Host Cmd 1 ¡V SYS_RUN
//-----------------------------------------------------------
static void IT8951SystemRun(void)
{
    LCDWriteCmdCode(IT8951_TCON_SYS_RUN);
}
#if 0
//-----------------------------------------------------------
//Host Cmd 2 - STANDBY
//-----------------------------------------------------------
static void IT8951StandBy(void)
{
    //LCDWriteCmdCode(IT8951_TCON_STANDBY);
}
#endif
//-----------------------------------------------------------
//Host Cmd 3 - SLEEP
//-----------------------------------------------------------
static void IT8951Sleep(void)
{
    LCDWriteCmdCode(IT8951_TCON_SLEEP);
}

//-----------------------------------------------------------
//REG_RD
//-----------------------------------------------------------
static u16 IT8951ReadReg(u16 usRegAddr)
{
    u16 usData;
    //----------I80 Mode-------------
    //Send Cmd and Register Address
    LCDWriteCmdCode(IT8951_TCON_REG_RD);
    LCDWriteData(usRegAddr);
    //Read data from Host Data bus
    usData = LCDReadData();
    return usData;
}
//-----------------------------------------------------------
//REG_WR
//-----------------------------------------------------------
static void IT8951WriteReg(u16 usRegAddr,u16 usValue)
{
    //I80 Mode
    //Send Cmd , Register Address and Write Value
    LCDWriteCmdCode(IT8951_TCON_REG_WR);
    LCDWriteData(usRegAddr);
    LCDWriteData(usValue);
}
#if 0
//-----------------------------------------------------------
//MEM_BST_RD_T
//-----------------------------------------------------------
static void IT8951MemBurstReadTrigger(u32 ulMemAddr , u32 ulReadSize)
{
    u16 usArg[4];
    //Setting Arguments for Memory Burst Read
    usArg[0] = (u16)(ulMemAddr & 0x0000FFFF); //addr[15:0]
    usArg[1] = (u16)( (ulMemAddr >> 16) & 0x0000FFFF ); //addr[25:16]
    usArg[2] = (u16)(ulReadSize & 0x0000FFFF); //Cnt[15:0]
    usArg[3] = (u16)( (ulReadSize >> 16) & 0x0000FFFF ); //Cnt[25:16]
    //Send Cmd and Arg
    LCDSendCmdArg(IT8951_TCON_MEM_BST_RD_T , usArg , 4);
}
//-----------------------------------------------------------
//MEM_BST_RD_S
//-----------------------------------------------------------
static void IT8951MemBurstReadStart(void)
{
    LCDWriteCmdCode(IT8951_TCON_MEM_BST_RD_S);
}

//-----------------------------------------------------------
//LD_IMG
//-----------------------------------------------------------
static void IT8951LoadImgStart(IT8951LdImgInfo* pstLdImgInfo)
{
    u16 usArg;
    //Setting Argument for Load image start
    usArg = (pstLdImgInfo->usEndianType << 8 )
    |(pstLdImgInfo->usPixelFormat << 4)
    |(pstLdImgInfo->usRotate);
    //Send Cmd
    LCDWriteCmdCode(IT8951_TCON_LD_IMG);
    //Send Arg
    LCDWriteData(usArg);
}
#endif

//-----------------------------------------------------------
//MEM_BST_WR
//-----------------------------------------------------------
static void IT8951MemBurstWrite(u32 ulMemAddr , u32 ulWriteSize)
{
    u16 usArg[4];
    //Setting Arguments for Memory Burst Write
    usArg[0] = (u16)(ulMemAddr & 0x0000FFFF); //addr[15:0]
    usArg[1] = (u16)( (ulMemAddr >> 16) & 0x0000FFFF ); //addr[25:16]
    usArg[2] = (u16)(ulWriteSize & 0x0000FFFF); //Cnt[15:0]
    usArg[3] = (u16)( (ulWriteSize >> 16) & 0x0000FFFF ); //Cnt[25:16]
    //Send Cmd and Arg
    LCDSendCmdArg(IT8951_TCON_MEM_BST_WR, usArg, 4);
}
//-----------------------------------------------------------
//MEM_BST_END
//-----------------------------------------------------------
static void IT8951MemBurstEnd(void)
{
    LCDWriteCmdCode(IT8951_TCON_MEM_BST_END);
}

static void IT8951MemBurstWriteProc(u32 ulMemAddr, u32 ulWriteSize, u16* pSrcBuf)
{
    u32 i;
 
    //Send Burst Write Start Cmd and Args
    IT8951MemBurstWrite(ulMemAddr, ulWriteSize);
 
    //Burst Write Data
    for(i=0;i<ulWriteSize;i++)
    {
        LCDWriteData(pSrcBuf[i]);
    }
 
    //Send Burst End Cmd
    IT8951MemBurstEnd();
}


//-----------------------------------------------------------
//LD_IMG_AREA
//-----------------------------------------------------------
static void IT8951LoadImgAreaStart(IT8951LdImgInfo* pstLdImgInfo ,IT8951AreaImgInfo* pstAreaImgInfo)
{
    u16 usArg[5];
    //Setting Argument for Load image start
    usArg[0] = (pstLdImgInfo->usEndianType << 8 )
    |(pstLdImgInfo->usPixelFormat << 4)
    |(pstLdImgInfo->usRotate);
    usArg[1] = pstAreaImgInfo->usX;
    usArg[2] = pstAreaImgInfo->usY;
    usArg[3] = pstAreaImgInfo->usWidth;
    usArg[4] = pstAreaImgInfo->usHeight;
    //Send Cmd and Args
    LCDSendCmdArg(IT8951_TCON_LD_IMG_AREA , usArg , 5);
}
//-----------------------------------------------------------
//LD_IMG_END
//-----------------------------------------------------------
static void IT8951LoadImgEnd(void)
{
    LCDWriteCmdCode(IT8951_TCON_LD_IMG_END);
}

static void GetIT8951SystemInfo(void* pBuf)
{
    u16* pusWord = (u16*)pBuf;
    I80IT8951DevInfo* pstDevInfo;
    int i;

    //Send I80 CMD
    LCDWriteCmdCode(USDEF_I80_CMD_GET_DEV_INFO);

    //Burst Read Request for SPI interface only
    LCDReadNData(pusWord, sizeof(I80IT8951DevInfo)/2);//Polling HRDY for each words(2-bytes) if possible

    //Show Device information of IT8951
    pstDevInfo = (I80IT8951DevInfo*)pBuf;
    pr_info("Panel(W,H) = (%d,%d)\n",
    pstDevInfo->usPanelW, pstDevInfo->usPanelH );
    pr_info("Image Buffer Address = %X\n",
    pstDevInfo->usImgBufAddrL | (pstDevInfo->usImgBufAddrH << 16));

    //Show Firmware and LUT Version
    for(i=0; i<8; i++) {
        pstDevInfo->usFWVersion[i] = MY_WORD_SWAP(pstDevInfo->usFWVersion[i]);
        pstDevInfo->usLUTVersion[i] = MY_WORD_SWAP(pstDevInfo->usLUTVersion[i]);
    }
    pr_info("FW Version = %s\n", (u8 *)pstDevInfo->usFWVersion);
    pr_info("LUT Version = %s\n", (u8 *)pstDevInfo->usLUTVersion);
}
//-----------------------------------------------------------
//Set Image buffer base address
//-----------------------------------------------------------
static void IT8951SetImgBufBaseAddr(u32 ulImgBufAddr)
{
    u16 usWordH = (u16)((ulImgBufAddr >> 16) & 0x0000FFFF);
    u16 usWordL = (u16)( ulImgBufAddr & 0x0000FFFF);
    //Write LISAR Reg
    IT8951WriteReg(LISAR + 2 ,usWordH);
    IT8951WriteReg(LISAR ,usWordL);
}

//-----------------------------------------------------------
//Wait for LUT Engine Finish
//Polling Display Engine Ready by LUTNo
//-----------------------------------------------------------
static void IT8951WaitForDisplayReady(void)
{
    //Check IT8951 Register LUTAFSR => NonZero ¡V Busy, 0 - Free
    while(IT8951ReadReg(LUTAFSR));
}
//-----------------------------------------------------------
//Load Image Area process
//-----------------------------------------------------------
static void IT8951HostAreaPackedPixelWrite(IT8951LdImgInfo* pstLdImgInfo,IT8951AreaImgInfo* pstAreaImgInfo)
{
    //u32 i;
    //Source buffer address of Host
    u16* pusFrameBuf = pstLdImgInfo->ulStartFBAddr;
    u32 size = 0;

    //Set Image buffer(IT8951) Base address
    IT8951SetImgBufBaseAddr(pstLdImgInfo->ulImgBufBaseAddr);
    //Send Load Image start Cmd
    IT8951LoadImgAreaStart(pstLdImgInfo , pstAreaImgInfo);
    //Host Write Data
#if 0
    for(i=0; i<pstAreaImgInfo->usHeight; i+=16)
    {
    	LCDWriteNData(pusFrameBuf, pstAreaImgInfo->usWidth*8);
		pusFrameBuf += pstAreaImgInfo->usWidth*8;
    }
#else
	size = ((pstAreaImgInfo->usX%2)+((pstAreaImgInfo->usX+pstAreaImgInfo->usWidth)%2)+pstAreaImgInfo->usWidth)*pstAreaImgInfo->usHeight;
	LCDWriteNData(pusFrameBuf, size/2);
#endif
    //Send Load Img End Command
    IT8951LoadImgEnd();
}
//-----------------------------------------------------------
//Application for Display panel Area
//-----------------------------------------------------------
static void IT8951DisplayArea(u16 usX, u16 usY, u16 usW, u16 usH, u16 usDpyMode)
{
    //Send I80 Display Command (User defined command of IT8951)
    LCDWriteCmdCode(USDEF_I80_CMD_DPY_AREA); //0x0034
    //Write arguments
    LCDWriteData(usX);
    LCDWriteData(usY);
    LCDWriteData(usW);
    LCDWriteData(usH);
    LCDWriteData(usDpyMode);
}

#if 0
//Display Area with bitmap on EPD
//-----------------------------------------------------------
// Display Function 4 - for Display Area for 1-bpp mode format
//   the bitmap(1bpp) mode will be enable when Display
//   and restore to Default setting (disable) after displaying finished
//-----------------------------------------------------------
static void IT8951DisplayArea1bpp(u16 usX, u16 usY, u16 usW, u16 usH, u16 usDpyMode, u8 ucBGGrayVal, u8 ucFGGrayVal)
{
    //Set Display mode to 1 bpp mode - Set 0x18001138 Bit[18](0x1800113A Bit[2])to 1
    IT8951WriteReg(UP1SR+2, IT8951ReadReg(UP1SR+2) | (1<<2));
    
    //Set BitMap color table 0 and 1 , => Set Register[0x18001250]:
    //Bit[7:0]: ForeGround Color(G0~G15)  for 1
    //Bit[15:8]:Background Color(G0~G15)  for 0
    IT8951WriteReg(BGVR, (ucBGGrayVal<<8) | ucFGGrayVal);
    
    //Display
    IT8951DisplayArea( usX, usY, usW, usH, usDpyMode);
    IT8951WaitForDisplayReady();
    
    //Restore to normal mode
    IT8951WriteReg(UP1SR+2, IT8951ReadReg(UP1SR+2) & ~(1<<2));
}

//-------------------------------------------------------------------------------------------------------------
// 	Command - 0x0037 for Display Base addr by User 
//  u32 ulDpyBufAddr - Host programmer need to indicate the Image buffer address of IT8951
//                                         In current case, there is only one image buffer in IT8951 so far.
//                                         So Please set the Image buffer address you got  in initial stage.
//                                         (gulImgBufAddr by Get device information 0x0302 command)
//
//-------------------------------------------------------------------------------------------------------------
static void IT8951DisplayAreaBuf(u16 usX, u16 usY, u16 usW, u16 usH, u16 usDpyMode, u32 ulDpyBufAddr)
{
    //Send I80 Display Command (User defined command of IT8951)
    LCDWriteCmdCode(USDEF_I80_CMD_DPY_BUF_AREA); //0x0037
    
    //Write arguments
    LCDWriteData(usX);
    LCDWriteData(usY);
    LCDWriteData(usW);
    LCDWriteData(usH);
    LCDWriteData(usDpyMode);
    LCDWriteData((u16)ulDpyBufAddr);       //Display Buffer Base address[15:0]
    LCDWriteData((u16)(ulDpyBufAddr>>16)); //Display Buffer Base address[26:16]
 
}
#endif

static int Load_UserFile(char* path, u8* buf)
{
	mm_segment_t old_fs;
	struct file *fp;
	int index = 0;
	int ret;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	fp = filp_open(path, O_RDONLY, S_IRUSR/*|S_IWUSR*/);
	if(IS_ERR(fp)) {
		pr_err("%s file open failed!\n", path);
		set_fs(old_fs);
		return 0;
	}

	do
	{
		ret = vfs_read(fp, buf+index, 1024, &fp->f_pos);
		index += ret;
	} while(ret > 0);

	filp_close(fp, NULL);
	set_fs(old_fs);

	return index;
}

static void IT8951FWUpgrade(u16 bufAddrL, u16 bufAddrH, u16 fwSizeL, u16 fwSizeH, u16 sfiAddrL, u16 sfiAddrH)
{
	u16 usArg[6];
	//Setting Arguments for Memory Burst Write
	usArg[0] = (u16)(bufAddrL & 0x0000FFFF); 
	usArg[1] = (u16)(bufAddrH & 0x0000FFFF); 
	usArg[2] = (u16)(fwSizeL & 0x0000FFFF);
	usArg[3] = (u16)(fwSizeH & 0x0000FFFF);
	usArg[4] = (u16)(sfiAddrL & 0x0000FFFF);
	usArg[5] = (u16)(sfiAddrH & 0x0000FFFF);
	// Send Cmd and Arg
	LCDSendCmdArg(IT8951_TCON_FW_UPGRADE, usArg, 6);
	// Wait FW update Done
	LCDWaitForReady();
}


//-----------------------------------------------------------
//Initial flow for testing
//-----------------------------------------------------------
void IT8951_Drv_Init(I80IT8951DevInfo* stI80DevInfo, u32* imgBufAddr)
{
	GetIT8951SystemInfo(stI80DevInfo);
	if(stI80DevInfo->usFWVersion[0] > 0) {
		*imgBufAddr = stI80DevInfo->usImgBufAddrL | (stI80DevInfo->usImgBufAddrH << 16);
		//Set to Enable I80 Packed mode
	    IT8951WriteReg(I80CPCR, 0x0001);
	}
}

//-----------------------------------------------------------
//Example of Display Flow
//-----------------------------------------------------------
void IT8951DisplayTest0(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel)
{
    IT8951LdImgInfo stLdImgInfo;
    IT8951AreaImgInfo stAreaImgInfo;
#ifdef DEBUG_FPS
	struct timespec ts;
	u32 t0 = 0;
	u32 t1 = 0;
#endif

	mutex_lock(&display_lock);
	if(display_skip) {
		mutex_unlock(&display_lock);
		return;
	}
    
    //Prepare image
    //Write pixel 0xF0(White) to Frame Buffer
    memset(pixel, 0xF0, stI80DevInfo->usPanelW * stI80DevInfo->usPanelH);
    
    //Check TCon is free ? Wait TCon Ready (optional)
    IT8951WaitForDisplayReady();
    
    //--------------------------------------------------------------------------------------------
    //      initial display - Display white only
    //--------------------------------------------------------------------------------------------
    //Load Image and Display
    //Setting Load image information
    stLdImgInfo.ulStartFBAddr    = (u16*)pixel;
    stLdImgInfo.usEndianType     = IT8951_LDIMG_L_ENDIAN;
    stLdImgInfo.usPixelFormat    = IT8951_8BPP;
    stLdImgInfo.usRotate         = IT8951_ROTATE_0;
    stLdImgInfo.ulImgBufBaseAddr = imgBufAddr;
    //Set Load Area
    stAreaImgInfo.usX      = 0;
    stAreaImgInfo.usY      = 0;
    stAreaImgInfo.usWidth  = stI80DevInfo->usPanelW;
    stAreaImgInfo.usHeight = stI80DevInfo->usPanelH;

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t0 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif    
    
    //Load Image from Host to IT8951 Image Buffer
    IT8951HostAreaPackedPixelWrite(&stLdImgInfo, &stAreaImgInfo);//Display function 2
    //Display Area ¡V (x,y,w,h) with mode 0 for initial White to clear Panel
    IT8951DisplayArea(0,0, stI80DevInfo->usPanelW, stI80DevInfo->usPanelH, IT8951_MODE_INIT);

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t1 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	pr_err("EPD Display time: %d ms\n", t1-t0);
#endif
    
    //--------------------------------------------------------------------------------------------
    //      Regular display - Display Any Gray colors with Mode 2 or others
    //--------------------------------------------------------------------------------------------
    //Preparing buffer to All black (8 bpp image)
    //or you can create your image pattern here..
    memset(pixel, 0x00, stI80DevInfo->usPanelW * stI80DevInfo->usPanelH);
     
    IT8951WaitForDisplayReady();
    
    //Setting Load image information
    stLdImgInfo.ulStartFBAddr    = (u16*)pixel;
    stLdImgInfo.usEndianType     = IT8951_LDIMG_L_ENDIAN;
    stLdImgInfo.usPixelFormat    = IT8951_8BPP; 
    stLdImgInfo.usRotate         = IT8951_ROTATE_0;
    stLdImgInfo.ulImgBufBaseAddr = imgBufAddr;
    //Set Load Area
    stAreaImgInfo.usX      = 0;
    stAreaImgInfo.usY      = 0;
    stAreaImgInfo.usWidth  = stI80DevInfo->usPanelW;
    stAreaImgInfo.usHeight = stI80DevInfo->usPanelH;

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t0 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif
    //Load Image from Host to IT8951 Image Buffer
    IT8951HostAreaPackedPixelWrite(&stLdImgInfo, &stAreaImgInfo);//Display function 2
    //Display Area ¡V (x,y,w,h) with mode 2 for fast gray clear mode - depends on current waveform 
    IT8951DisplayArea(0,0, stI80DevInfo->usPanelW, stI80DevInfo->usPanelH, IT8951_MODE_DU);
    
#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t1 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	pr_err("EPD Display time: %d ms\n", t1-t0);
#endif

	mutex_unlock(&display_lock);
}

void IT8951DisplayTest1(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel)
{
	IT8951LdImgInfo stLdImgInfo;
	IT8951AreaImgInfo stAreaImgInfo;
	
	uint16_t width = stI80DevInfo->usPanelW;
	uint16_t hight = stI80DevInfo->usPanelH/16;

#ifdef DEBUG_FPS
	struct timespec ts;
	u32 t0 = 0;
	u32 t1 = 0;
#endif

	mutex_lock(&display_lock);
	if(display_skip) {
		mutex_unlock(&display_lock);
		return;
	}

	//--------------------------------------------------------------------------------------------
	//      Regular display - Display Any Gray colors with Mode 2 or others
	//--------------------------------------------------------------------------------------------
	//Preparing buffer to All black (8 bpp image)
	//or you can create your image pattern here..
	memset(pixel                  ,  0x00, width * hight * 1);
	memset(pixel+width * hight * 1,  0x11, width * hight * 1);
	memset(pixel+width * hight * 2,  0x22, width * hight * 1);
	memset(pixel+width * hight * 3,  0x33, width * hight * 1);
	memset(pixel+width * hight * 4,  0x44, width * hight * 1);
	memset(pixel+width * hight * 5,  0x55, width * hight * 1);
	memset(pixel+width * hight * 6,  0x66, width * hight * 1);
	memset(pixel+width * hight * 7,  0x77, width * hight * 1);
	memset(pixel+width * hight * 8,  0x88, width * hight * 1);
	memset(pixel+width * hight * 9,  0x99, width * hight * 1);
	memset(pixel+width * hight * 10, 0xaa, width * hight * 1);
	memset(pixel+width * hight * 11, 0xbb, width * hight * 1);
	memset(pixel+width * hight * 12, 0xcc, width * hight * 1);
	memset(pixel+width * hight * 13, 0xdd, width * hight * 1);
	memset(pixel+width * hight * 14, 0xee, width * hight * 1);
	memset(pixel+width * hight * 15, 0xff, width * hight * 1);
	
	IT8951WaitForDisplayReady();
	
	//Setting Load image information
	stLdImgInfo.ulStartFBAddr    = (u16*)pixel;
	stLdImgInfo.usEndianType     = IT8951_LDIMG_L_ENDIAN;
	stLdImgInfo.usPixelFormat    = IT8951_8BPP; 
	stLdImgInfo.usRotate         = IT8951_ROTATE_0;
	stLdImgInfo.ulImgBufBaseAddr = imgBufAddr;
	//Set Load Area
	stAreaImgInfo.usX      = 0;
	stAreaImgInfo.usY      = 0;
	stAreaImgInfo.usWidth  = stI80DevInfo->usPanelW;
	stAreaImgInfo.usHeight = stI80DevInfo->usPanelH;

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t0 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif
	
	//Load Image from Host to IT8951 Image Buffer
	IT8951HostAreaPackedPixelWrite(&stLdImgInfo, &stAreaImgInfo);//Display function 2
	//Display Area ?V (x,y,w,h) with mode 2 for fast gray clear mode - depends on current waveform 
	IT8951DisplayArea(0,0, stI80DevInfo->usPanelW, stI80DevInfo->usPanelH, IT8951_MODE_GC);
	
#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t1 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	pr_err("EPD Display time: %d ms\n", t1-t0);
#endif

	mutex_unlock(&display_lock);
}

void IT8951DisplayUserData(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel, 
		u16 usX, u16 usY, u16 usW, u16 usH, u16 usDpyMode, char* path)
{
	IT8951LdImgInfo stLdImgInfo;
	IT8951AreaImgInfo stAreaImgInfo;

#ifdef DEBUG_FPS
	struct timespec ts;
	u32 t0 = 0;
	u32 t1 = 0;
#endif

	mutex_lock(&display_lock);

	Load_UserFile(path, pixel);

	IT8951WaitForDisplayReady();
	
	//Setting Load image information
	stLdImgInfo.ulStartFBAddr	 = (u16*)pixel;
	stLdImgInfo.usEndianType	 = IT8951_LDIMG_L_ENDIAN;
	stLdImgInfo.usPixelFormat	 = IT8951_8BPP; 
	stLdImgInfo.usRotate		 = IT8951_ROTATE_0;
	stLdImgInfo.ulImgBufBaseAddr = imgBufAddr;
	//Set Load Area
	stAreaImgInfo.usX	   = usX;
	stAreaImgInfo.usY	   = usY;
	stAreaImgInfo.usWidth  = usW;
	stAreaImgInfo.usHeight = usH;

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t0 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif

	//Load Image from Host to IT8951 Image Buffer
	IT8951HostAreaPackedPixelWrite(&stLdImgInfo, &stAreaImgInfo);//Display function 2
	//Display Area ?V (x,y,w,h) with mode 2 for fast gray clear mode - depends on current waveform 
	IT8951DisplayArea(usX, usY, usW, usH, usDpyMode);

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t1 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	pr_err("EPD Display time: %d ms\n", t1-t0);
#endif

	mutex_unlock(&display_lock);
}

void IT8951DisplayPartial(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel,
		u16 usX, u16 usY, u16 usW, u16 usH, u16 usDpyMode)
{
	IT8951LdImgInfo stLdImgInfo;
	IT8951AreaImgInfo stAreaImgInfo;

#ifdef DEBUG_FPS
	struct timespec ts;
	u32 t0 = 0;
	u32 t1 = 0;
#endif

	mutex_lock(&display_lock);

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t0 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif

	//Load_UserFile("/data/system/epd/wakeup_noti.data", pixel);

	IT8951WaitForDisplayReady();

	//Setting Load image information
	stLdImgInfo.ulStartFBAddr	 = (u16*)pixel;
	stLdImgInfo.usEndianType	 = IT8951_LDIMG_L_ENDIAN;
	stLdImgInfo.usPixelFormat	 = IT8951_8BPP;
	stLdImgInfo.usRotate		 = IT8951_ROTATE_0;
	stLdImgInfo.ulImgBufBaseAddr = imgBufAddr;
	//Set Load Area
	stAreaImgInfo.usX	   = usX;
	stAreaImgInfo.usY	   = usY;
	stAreaImgInfo.usWidth  = usW;
	stAreaImgInfo.usHeight = usH;

	//Load Image from Host to IT8951 Image Buffer
	IT8951HostAreaPackedPixelWrite(&stLdImgInfo, &stAreaImgInfo);//Display function 2
	//Display Area ?V (x,y,w,h) with mode 2 for fast gray clear mode - depends on current waveform
	IT8951DisplayArea(usX, usY, usW, usH, usDpyMode);

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t1 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	pr_err("EPD Display time: %d ms\n", t1-t0);
#endif

	mutex_unlock(&display_lock);
}

void IT8951DisplayInitClear(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel)
{
	IT8951LdImgInfo stLdImgInfo;
	IT8951AreaImgInfo stAreaImgInfo;

#ifdef DEBUG_FPS
	struct timespec ts;
	u32 t0 = 0;
	u32 t1 = 0;
#endif

	mutex_lock(&display_lock);
	if(display_skip) {
		mutex_unlock(&display_lock);
		return;
	}

	//Prepare image
    //Write pixel 0xF0(White) to Frame Buffer
    memset(pixel, 0xF0, stI80DevInfo->usPanelW * stI80DevInfo->usPanelH);
    
    //Check TCon is free ? Wait TCon Ready (optional)
    IT8951WaitForDisplayReady();
    
    //--------------------------------------------------------------------------------------------
    //      initial display - Display white only
    //--------------------------------------------------------------------------------------------
    //Load Image and Display
    //Setting Load image information
    stLdImgInfo.ulStartFBAddr    = (u16*)pixel;
    stLdImgInfo.usEndianType     = IT8951_LDIMG_L_ENDIAN;
    stLdImgInfo.usPixelFormat    = IT8951_8BPP;
    stLdImgInfo.usRotate         = IT8951_ROTATE_0;
    stLdImgInfo.ulImgBufBaseAddr = imgBufAddr;
    //Set Load Area
    stAreaImgInfo.usX      = 0;
    stAreaImgInfo.usY      = 0;
    stAreaImgInfo.usWidth  = stI80DevInfo->usPanelW;
    stAreaImgInfo.usHeight = stI80DevInfo->usPanelH;

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t0 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif    
    
    //Load Image from Host to IT8951 Image Buffer
    IT8951HostAreaPackedPixelWrite(&stLdImgInfo, &stAreaImgInfo);//Display function 2
    //Display Area ¡V (x,y,w,h) with mode 0 for initial White to clear Panel
    IT8951DisplayArea(0,0, stI80DevInfo->usPanelW, stI80DevInfo->usPanelH, IT8951_MODE_INIT);

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t1 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	pr_err("EPD Display time: %d ms\n", t1-t0);
#endif

	mutex_unlock(&display_lock);
}

void IT8951DisplayBuffer(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel, 
		u16 usX, u16 usY, u16 usW, u16 usH, u16 usDpyMode)
{
	IT8951LdImgInfo stLdImgInfo;
	IT8951AreaImgInfo stAreaImgInfo;

#ifdef DEBUG_FPS
	struct timespec ts;
	u32 t0 = 0;
	u32 t1 = 0;
#endif

	mutex_lock(&display_lock);
	if(display_skip) {
		mutex_unlock(&display_lock);
		return;
	}

	IT8951WaitForDisplayReady();
	
	//Setting Load image information
	stLdImgInfo.ulStartFBAddr	 = (u16*)pixel;
	stLdImgInfo.usEndianType	 = IT8951_LDIMG_L_ENDIAN;
	stLdImgInfo.usPixelFormat	 = IT8951_8BPP; 
	stLdImgInfo.usRotate		 = IT8951_ROTATE_0;
	stLdImgInfo.ulImgBufBaseAddr = imgBufAddr;
	//Set Load Area
	stAreaImgInfo.usX	   = usX;
	stAreaImgInfo.usY	   = usY;
	stAreaImgInfo.usWidth  = usW;
	stAreaImgInfo.usHeight = usH;

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t0 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif

	//Load Image from Host to IT8951 Image Buffer
	IT8951HostAreaPackedPixelWrite(&stLdImgInfo, &stAreaImgInfo);//Display function 2
	//Display Area ?V (x,y,w,h) with mode 2 for fast gray clear mode - depends on current waveform 
	IT8951DisplayArea(usX, usY, usW, usH, usDpyMode);

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t1 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	pr_err("EPD Display time: %d ms\n", t1-t0);
#endif

	mutex_unlock(&display_lock);
}

void IT8951DisplayClear(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel, u8 value)
{
	IT8951LdImgInfo stLdImgInfo;
	IT8951AreaImgInfo stAreaImgInfo;

#ifdef DEBUG_FPS
	struct timespec ts;
	u32 t0 = 0;
	u32 t1 = 0;
#endif

	mutex_lock(&display_lock);
	
	//Prepare image
    memset(pixel, value, stI80DevInfo->usPanelW * stI80DevInfo->usPanelH);
    
    //Check TCon is free ? Wait TCon Ready (optional)
    IT8951WaitForDisplayReady();
    
    //--------------------------------------------------------------------------------------------
    //      initial display - Display white only
    //--------------------------------------------------------------------------------------------
    //Load Image and Display
    //Setting Load image information
    stLdImgInfo.ulStartFBAddr    = (u16*)pixel;
    stLdImgInfo.usEndianType     = IT8951_LDIMG_L_ENDIAN;
    stLdImgInfo.usPixelFormat    = IT8951_8BPP;
    stLdImgInfo.usRotate         = IT8951_ROTATE_0;
    stLdImgInfo.ulImgBufBaseAddr = imgBufAddr;
    //Set Load Area
    stAreaImgInfo.usX      = 0;
    stAreaImgInfo.usY      = 0;
    stAreaImgInfo.usWidth  = stI80DevInfo->usPanelW;
    stAreaImgInfo.usHeight = stI80DevInfo->usPanelH;

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t0 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
#endif    
    
    //Load Image from Host to IT8951 Image Buffer
    IT8951HostAreaPackedPixelWrite(&stLdImgInfo, &stAreaImgInfo);//Display function 2
    //Display Area ¡V (x,y,w,h) with mode 0 for initial White to clear Panel
    IT8951DisplayArea(0,0, stI80DevInfo->usPanelW, stI80DevInfo->usPanelH, IT8951_MODE_GC);

#ifdef DEBUG_FPS
	getnstimeofday(&ts);
	t1 = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
	pr_err("EPD Display time: %d ms\n", t1-t0);
#endif

	mutex_unlock(&display_lock);
}

void IT8951DisplayFWflash(u8* buf, u32 imgBufAddr)
{
	u16 bufAddrL;
	u16 bufAddrH;
	u16 fwSizeL;
	u16 fwSizeH;
	u16 sfiAddrL;
	u16 sfiAddrH;

	u32 ulWrFWSize;
	u32 ulWrSFIAddr;
	u8* ulCurSrcBufAddr;
	u32 ulMaxTransferSize = 64*1024; //64KB * (ImageBufferSize /64KB)(64KB alignment? because the erased size of SPI Flash is 64KB)
	u32 ulRemainSize;
	u32 size;

	size = Load_UserFile("/vendor/etc/epd.bin", buf);
	printk("%s size: %d\n", __func__, size);

	ulCurSrcBufAddr = buf;//FW File Buffer Address
	ulRemainSize = size; //FW File Size 
		
	ulWrSFIAddr = 0x00000000; //Start Address of SPI Flash
	while (ulRemainSize > 0)
	{
		if (ulRemainSize > ulMaxTransferSize)
			ulWrFWSize = ulMaxTransferSize;
		else
			ulWrFWSize = ulRemainSize;

		ulRemainSize -= ulWrFWSize;

		bufAddrL = (imgBufAddr & 0x0000FFFF);
		bufAddrH = ((imgBufAddr >> 16) & 0x0000FFFF);

		fwSizeL = (ulWrFWSize & 0x0000FFFF);
		fwSizeH = ((ulWrFWSize >> 16) & 0x0000FFFF);

		sfiAddrL = (ulWrSFIAddr & 0x0000FFFF);
		sfiAddrH = ((ulWrSFIAddr >> 16) & 0x0000FFFF);

		//Send FW.bin to IT8951 Image buffer
		IT8951MemBurstWriteProc(imgBufAddr, ulWrFWSize / 2, (u16 *)ulCurSrcBufAddr);
		//Send command to inform IT8951 to FW Upgrade to indicated SFI Address and Size
		IT8951FWUpgrade(bufAddrL, bufAddrH, fwSizeL, fwSizeH, sfiAddrL, sfiAddrH);
		//Wait for Upgrade finished
		LCDWaitForReady();

		ulCurSrcBufAddr += ulWrFWSize;
		ulWrSFIAddr += ulWrFWSize;

		printk("%s write %d(%d)\n", __func__, ulWrFWSize, ulWrSFIAddr);
	}

	printk("%s done!\n", __func__);
}

void IT8951DisplayLock(int lock)
{
	display_skip = lock;
}

void IT8951DisplayWakeup(void)
{
	pr_err("++ %s\n", __func__);
	IT8951SystemRun();
	pr_err("-- %s\n", __func__);
}

void IT8951DisplaySleep(void)
{
	pr_err("++ %s\n", __func__);
	IT8951Sleep();
	pr_err("-- %s\n", __func__);
}

void IT8951DisplaySetVCOM(int vcom)
{
	u16 usArg[2];

	pr_err("++ %s\n", __func__);

	usArg[0] = 1;
	usArg[1] = (u16)vcom;
	LCDSendCmdArg(IT8951_TCON_SET_VCOM, usArg, 2);

	pr_err("-- %s\n", __func__);
}

/*-------------------------------------------------------------------------*/


