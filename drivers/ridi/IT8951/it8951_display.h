#ifndef __IT8951_I80_DISPLAY_H__ //{__IT8951_I80_DISPLAY_H__
#define __IT8951_I80_DISPLAY_H__

//prototype of structure
//structure prototype 1
typedef struct IT8951LdImgInfo
{
    u16 usEndianType; //little or Big Endian
    u16 usPixelFormat; //bpp
    u16 usRotate; //Rotate mode
    u16* ulStartFBAddr; //Start address of source Frame buffer
    u32 ulImgBufBaseAddr;//Base address of target image buffer
}IT8951LdImgInfo;
//structure prototype 2
typedef struct IT8951AreaImgInfo
{
    u16 usX;
    u16 usY;
    u16 usWidth;
    u16 usHeight;
    
}IT8951AreaImgInfo;

//structure prototype 3
//See user defined command ¡V Get Device information (0x0302)
typedef struct
{
    u16 usPanelW;
    u16 usPanelH;
    u16 usImgBufAddrL;
    u16 usImgBufAddrH;
    u16 usFWVersion[8]; //16 Bytes String
    u16 usLUTVersion[8]; //16 Bytes String
    
}I80IT8951DevInfo;


//Built in I80 Command Code
#define IT8951_TCON_SYS_RUN      			0x0001
#define IT8951_TCON_SLEEP        			0x0003

#define IT8951_TCON_REG_RD       			0x0010
#define IT8951_TCON_REG_WR       			0x0011

#define IT8951_TCON_MEM_BST_RD_T 	0x0012
#define IT8951_TCON_MEM_BST_RD_S 	0x0013
#define IT8951_TCON_MEM_BST_WR   		0x0014
#define IT8951_TCON_MEM_BST_END  	0x0015

#define IT8951_TCON_LD_IMG       			0x0020
#define IT8951_TCON_LD_IMG_AREA  		0x0021
#define IT8951_TCON_LD_IMG_END   		0x0022

#define IT8951_TCON_FW_UPGRADE 0x009C

#define IT8951_TCON_SET_VCOM   		0x0039

//I80 User defined command code
#define USDEF_I80_CMD_DPY_AREA     		0x0034
#define USDEF_I80_CMD_GET_DEV_INFO 	0x0302

//Rotate mode
#define IT8951_ROTATE_0     	0
#define IT8951_ROTATE_90    	1
#define IT8951_ROTATE_180  	2
#define IT8951_ROTATE_270   	3

//Pixel mode , BPP - Bit per Pixel
#define IT8951_2BPP   0
#define IT8951_3BPP   1
#define IT8951_4BPP   2
#define IT8951_8BPP   3

//Waveform Mode
#define IT8951_MODE_INIT   0
#define IT8951_MODE_DU     1
#define IT8951_MODE_GC     2
#define IT8951_MODE_GL     3
#define IT8951_MODE_A2     4
#define IT8951_MODE_MAX    5


//Endian Type
#define IT8951_LDIMG_L_ENDIAN   0
#define IT8951_LDIMG_B_ENDIAN   1
//Auto LUT
#define IT8951_DIS_AUTO_LUT   0
#define IT8951_EN_AUTO_LUT    1
//LUT Engine Status
#define IT8951_ALL_LUTE_BUSY 0xFFFF

//-----------------------------------------------------------------------
// IT8951 TCon Registers defines
//-----------------------------------------------------------------------
//Register Base Address
#define DISPLAY_REG_BASE 0x1000               //Register RW access for I80 only
//Base Address of Basic LUT Registers
#define LUT0EWHR  (DISPLAY_REG_BASE + 0x00)   //LUT0 Engine Width Height Reg
#define LUT0XYR   (DISPLAY_REG_BASE + 0x40)   //LUT0 XY Reg
#define LUT0BADDR (DISPLAY_REG_BASE + 0x80)   //LUT0 Base Address Reg
#define LUT0MFN   (DISPLAY_REG_BASE + 0xC0)   //LUT0 Mode and Frame number Reg
#define LUT01AF   (DISPLAY_REG_BASE + 0x114)  //LUT0 and LUT1 Active Flag Reg
//Update Parameter Setting Register
#define UP0SR (DISPLAY_REG_BASE + 0x134)      //Update Parameter0 Setting Reg

#define UP1SR     		(DISPLAY_REG_BASE + 0x138)  //Update Parameter1 Setting Reg
#define LUT0ABFRV 	(DISPLAY_REG_BASE + 0x13C)  //LUT0 Alpha blend and Fill rectangle Value
#define UPBBADDR  	(DISPLAY_REG_BASE + 0x17C)  //Update Buffer Base Address
#define LUT0IMXY  		(DISPLAY_REG_BASE + 0x180)  //LUT0 Image buffer X/Y offset Reg
#define LUTAFSR   		(DISPLAY_REG_BASE + 0x224)  //LUT Status Reg (status of All LUT Engines)
#define IBPR 				(DISPLAY_REG_BASE + 0x24C)  //Image buffer Pitch Reg
#define BGVR      		(DISPLAY_REG_BASE + 0x250)  //Bitmap (1bpp) image color table
//-------System Registers----------------
#define SYS_REG_BASE 0x0000

//Address of System Registers
#define I80CPCR (SYS_REG_BASE + 0x04)
//-------Memory Converter Registers----------------
#define MCSR_BASE_ADDR 0x0200
#define MCSR (MCSR_BASE_ADDR  + 0x0000)
#define LISAR (MCSR_BASE_ADDR + 0x0008)

void IT8951_Drv_Init(I80IT8951DevInfo* stI80DevInfo, u32* imgBufAddr);
void IT8951DisplayTest0(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel);
void IT8951DisplayTest1(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel);
void IT8951DisplayUserData(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel,
		u16 usX, u16 usY, u16 usW, u16 usH, u16 usDpyMode, char* path);
void IT8951DisplayBuffer(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel, 
		u16 usX, u16 usY, u16 usW, u16 usH, u16 usDpyMode);
void IT8951DisplayInitClear(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel);
void IT8951DisplayClear(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel, u8 value);
void IT8951DisplayFWflash(u8* buf, u32 imgBufAddr);
void IT8951DisplayPartial(I80IT8951DevInfo* stI80DevInfo, u32 imgBufAddr, u8* pixel,
		u16 usX, u16 usY, u16 usW, u16 usH, u16 usDpyMode);
void IT8951DisplaySetVCOM(int vcom);

#endif //}__IT8951_I80_DISPLAY_H__
