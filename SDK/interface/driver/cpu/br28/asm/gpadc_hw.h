#ifndef  __GPADC_HW_H__
#define  __GPADC_HW_H__
//br28
#include "gpadc_hw_v10.h"
#include "generic/typedef.h"
#include "gpio.h"
#include "clock.h"
#include "asm/power_interface.h"

#define ADC_CH_MASK_TYPE_SEL	0xffff0000
#define ADC_CH_MASK_CH_SEL	    0x000000ff
#define ADC_CH_MASK_PMU_VBG_CH_SEL   0x0000ff00

#define ADC_CH_TYPE_BT     	(0x0<<16)
#define ADC_CH_TYPE_PMU    	(0x1<<16)
#define ADC_CH_TYPE_AUDIO  	(0x2<<16)
#define ADC_CH_TYPE_LPCTM  	(0x3<<16)
#define ADC_CH_TYPE_X32K    (0x4<<16)
#define ADC_CH_TYPE_PLL1   	(0x5<<16)
#define ADC_CH_TYPE_IO		(0x10<<16)

#define ADC_CH_BT_			(ADC_CH_TYPE_BT | 0x0)
#define ADC_CH_PMU_WBG04  	(ADC_CH_TYPE_PMU | (0x0<<8) | 0x0)//WBG04
#define ADC_CH_PMU_MBG08  	(ADC_CH_TYPE_PMU | (0x1<<8) | 0x0)//MBG08
#define ADC_CH_PMU_VSW  	(ADC_CH_TYPE_PMU | 0x1)
#define ADC_CH_PMU_PROGI	(ADC_CH_TYPE_PMU | 0x2)
#define ADC_CH_PMU_PROGF	(ADC_CH_TYPE_PMU | 0x3)
#define ADC_CH_PMU_VTEMP	(ADC_CH_TYPE_PMU | 0x4)
#define ADC_CH_PMU_VPWR_4 	(ADC_CH_TYPE_PMU | 0x5) //1/4vpwr
#define ADC_CH_PMU_VBAT_4 	(ADC_CH_TYPE_PMU | 0x6)  //1/4vbat
#define ADC_CH_PMU_VBAT_2	(ADC_CH_TYPE_PMU | 0x7)
#define ADC_CH_PMU_VB17  	(ADC_CH_TYPE_PMU | 0x8)
#define ADC_CH_PMU_IOVDD2  	(ADC_CH_TYPE_PMU | 0x9)
#define ADC_CH_PMU_VDC15	(ADC_CH_TYPE_PMU | 0xa)
#define ADC_CH_PMU_DVDD		(ADC_CH_TYPE_PMU | 0xb)
#define ADC_CH_PMU_RVDD  	(ADC_CH_TYPE_PMU | 0xc)
#define ADC_CH_PMU_TWVD  	(ADC_CH_TYPE_PMU | 0xd)
#define ADC_CH_PMU_PVDD  	(ADC_CH_TYPE_PMU | 0xe)
#define ADC_CH_PMU_EVDD  	(ADC_CH_TYPE_PMU | 0xf)
#define ADC_CH_AUDIO_VADADC	(ADC_CH_TYPE_AUDIO | BIT(0))
#define ADC_CH_AUDIO_VCM   	(ADC_CH_TYPE_AUDIO | BIT(1))
#define ADC_CH_AUDIO_VBGLDO	(ADC_CH_TYPE_AUDIO | BIT(2))
#define ADC_CH_AUDIO_HPVDD 	(ADC_CH_TYPE_AUDIO | BIT(3))
#define ADC_CH_AUDIO_RN    	(ADC_CH_TYPE_AUDIO | BIT(4))
#define ADC_CH_AUDIO_RP    	(ADC_CH_TYPE_AUDIO | BIT(5))
#define ADC_CH_AUDIO_LN    	(ADC_CH_TYPE_AUDIO | BIT(6))
#define ADC_CH_AUDIO_LP    	(ADC_CH_TYPE_AUDIO | BIT(7))
#define ADC_CH_AUDIO_DACVDD	(ADC_CH_TYPE_AUDIO | BIT(8))
#define ADC_CH_LPCTM_		(ADC_CH_TYPE_LPCTM | 0x0)
#define ADC_CH_X32K_		(ADC_CH_TYPE_X32K | 0x0)
#define ADC_CH_PLL1_	    (ADC_CH_TYPE_PLL1 | 0x0)
#define ADC_CH_IO_PA0  	(ADC_CH_TYPE_IO | 0x0)
#define ADC_CH_IO_PA5   (ADC_CH_TYPE_IO | 0x1)
#define ADC_CH_IO_PA6   (ADC_CH_TYPE_IO | 0x2)
#define ADC_CH_IO_PA8   (ADC_CH_TYPE_IO | 0x3)
#define ADC_CH_IO_PC4   (ADC_CH_TYPE_IO | 0x4)
#define ADC_CH_IO_PC5   (ADC_CH_TYPE_IO | 0x5)
#define ADC_CH_IO_PB1   (ADC_CH_TYPE_IO | 0x6)
#define ADC_CH_IO_PB2   (ADC_CH_TYPE_IO | 0x7)
#define ADC_CH_IO_PB5   (ADC_CH_TYPE_IO | 0x8)
#define ADC_CH_IO_PB9   (ADC_CH_TYPE_IO | 0x9)
#define ADC_CH_IO_DP   	(ADC_CH_TYPE_IO | 0xA)
#define ADC_CH_IO_DM   	(ADC_CH_TYPE_IO | 0xB)
#define ADC_CH_IO_PG0   (ADC_CH_TYPE_IO | 0xC)
#define ADC_CH_IO_PG1   (ADC_CH_TYPE_IO | 0xD)
#define ADC_CH_IO_PG5   (ADC_CH_TYPE_IO | 0xE)
#define ADC_CH_IO_PG7   (ADC_CH_TYPE_IO | 0xF)

enum AD_CH {
    AD_CH_BT = ADC_CH_BT_,

    AD_CH_PMU_WBG04 = ADC_CH_PMU_WBG04,
    AD_CH_PMU_MBG08 = ADC_CH_PMU_MBG08,
    AD_CH_PMU_VSW = ADC_CH_PMU_VSW,
    AD_CH_PMU_PROGI,
    AD_CH_PMU_PROGF,
    AD_CH_PMU_VTEMP,
    AD_CH_PMU_VPWR_4,	//1/4 VPWR
    AD_CH_PMU_VBAT_4, // 1/4 VBAT
    AD_CH_PMU_VBAT_2, // 1/2 VBAT  SDK中禁止使用
    AD_CH_PMU_VB17,
    AD_CH_PMU_IOVDD2,
    AD_CH_PMU_VDC15,
    AD_CH_PMU_DVDD,
    AD_CH_PMU_RVDD,
    AD_CH_PMU_TWVD,
    AD_CH_PMU_PVDD,
    AD_CH_PMU_EVDD,

    AD_CH_AUDIO = ADC_CH_TYPE_AUDIO, //防编译报错，该宏非法
    AD_CH_AUDIO_VADADC = ADC_CH_AUDIO_VADADC,
    AD_CH_AUDIO_VCM = ADC_CH_AUDIO_VCM,
    AD_CH_AUDIO_VBGLDO = ADC_CH_AUDIO_VBGLDO,
    AD_CH_AUDIO_HPVDD = ADC_CH_AUDIO_HPVDD,
    AD_CH_AUDIO_RN = ADC_CH_AUDIO_RN,
    AD_CH_AUDIO_RP = ADC_CH_AUDIO_RP,
    AD_CH_AUDIO_LN = ADC_CH_AUDIO_LN,
    AD_CH_AUDIO_LP = ADC_CH_AUDIO_LP,
    AD_CH_AUDIO_DACVDD = ADC_CH_AUDIO_DACVDD,

    AD_CH_LPCTM = ADC_CH_LPCTM_,

    AD_CH_X32K = ADC_CH_X32K_,

    AD_CH_PLL1 = ADC_CH_PLL1_,

    AD_CH_IO_PA0 = ADC_CH_IO_PA0,
    AD_CH_IO_PA5,
    AD_CH_IO_PA6,
    AD_CH_IO_PA8,
    AD_CH_IO_PC4,
    AD_CH_IO_PC5,
    AD_CH_IO_PB1,
    AD_CH_IO_PB2,
    AD_CH_IO_PB5,
    AD_CH_IO_PB9,
    AD_CH_IO_DP,
    AD_CH_IO_DM,
    AD_CH_IO_PG0,
    AD_CH_IO_PG1,
    AD_CH_IO_PG5,
    AD_CH_IO_PG7,

    AD_CH_IOVDD = 0xffffffff,
};

#define     ADC_VBG_CENTER        800
#define     ADC_VBG_TRIM_STEP     3
#define     ADC_VBG_DATA_WIDTH    4

//防编译报错
#define AD_CH_PMU_VBG   AD_CH_PMU_MBG08
#define AD_CH_LDOREF    AD_CH_PMU_VBG
#define AD_CH_PMU_VPWR   AD_CH_PMU_VPWR_4
#define AD_CH_PMU_VBAT  AD_CH_PMU_VBAT_4
#define AD_CH_PMU_VBAT_DIV  4


#define ADC_PMU_VBG_TEST_SEL(x)     SFR(P3_ANA_CON4, 7, 1, x)
#define ADC_PMU_VBG_TEST_EN(x)      SFR(P3_ANA_CON4, 6, 1, x)
#define ADC_PMU_VBG_BUFFER_EN(x)    SFR(P3_ANA_CON4, 5, 1, x)
#define ADC_PMU_CHANNEL_ADC(x)      SFR(P3_ANA_CON4, 1, 4, x)
#define ADC_PMU_DET_OE(x)           SFR(P3_ANA_CON4, 0, 1, x)
#define ADC_PMU_CH_CLOSE()  {   ADC_PMU_DET_OE(0);\
                                ADC_PMU_VBG_TEST_EN(0);\
                            }

//ADC_CON寄存器差异部分
// #define GPADC_CON_RESERVED  24 //bit24~bit31
#define GPADC_CON_ADC_MUX_SEL   21
#define GPADC_CON_ADC_MUX_SEL_  3
#define GPADC_CON_ADC_ASEL      18
#define GPADC_CON_ADC_ASEL_     3
#define GPADC_CON_ADC_ISEL      16

#endif  /*GPADC_HW_H*/
