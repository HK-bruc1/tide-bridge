/*********************************************************************************************
    *   Filename        : lib_driver_config.c

    *   Description     : Optimized Code & RAM (编译优化配置)

    *   Author          : Bingquan

    *   Email           : caibingquan@zh-jieli.com

    *   Last modifiled  : 2019-03-18 14:58

    *   Copyright:(c)JIELI  2011-2019  @ , All Rights Reserved.
*********************************************************************************************/
#include "app_config.h"
#include "system/includes.h"
#include "gpadc.h"

#if TCFG_SD0_SD1_USE_THE_SAME_HW
const int sd0_sd1_use_the_same_hw = 1;
#else
const int sd0_sd1_use_the_same_hw = 0;
#endif

#if TCFG_KEEP_CARD_AT_ACTIVE_STATUS
const int keep_card_at_active_status = 1;
#else
const int keep_card_at_active_status = 0;
#endif

#if TCFG_SDX_CAN_OPERATE_MMC_CARD
const int sdx_can_operate_mmc_card = TCFG_SDX_CAN_OPERATE_MMC_CARD;
#else
const int sdx_can_operate_mmc_card = 0;
#endif

#if TCFG_POWER_MODE_QUIET_ENABLE
const int config_dcdc_mode = 1;
#else
const int config_dcdc_mode = 0;
#endif

#if(TCFG_CLOCK_SYS_SRC == SYS_CLOCK_INPUT_PLL_RCL) //系统时钟源选择
const int  clock_sys_src_use_lrc_hw = 1; //当使用lrc时timer.c需要特殊设置
#else
const int  clock_sys_src_use_lrc_hw = 0;
#endif

#ifdef TCFG_VAD_LOWPOWER_CLOCK
const char config_vad_lowpower_clock = TCFG_VAD_LOWPOWER_CLOCK;
#else
const char config_vad_lowpower_clock = 0;
#endif

#if defined(CONFIG_CPU_BR28) || (TCFG_CHARGESTORE_PORT == IO_PORT_DP)
const int config_otg_slave_detect_method_2 = 1;
#else
const int config_otg_slave_detect_method_2 = 0;
#endif

//gpadc驱动开启 “电池电压”和“温度采集功能”
const u8 adc_vbat_ch_en = 1;
const u8 adc_vtemp_ch_en = 1;
const u32 lib_adc_clk_max = 500 * 1000;
const u8 gpadc_battery_mode = WEIGHTING_MODE; //使用IOVDD供电时,禁止使用 MEAN_FILTERING_MODE 模式
const u32 gpadc_ch_power = AD_CH_PMU_VBAT_4; //根据供电方式选择通道
const u8 gpadc_ch_power_div = 4; //分压系数,需和gpadc_ch_power匹配
const u8 gpadc_power_supply_mode = 2; //映射供电方式
const u16 gpadc_battery_trim_vddiom_voltage = 2800; //电池trim 使用的vddio电压
const u16 gpadc_battery_trim_voltage = 3700; //电池trim 使用的vbat电压
const u16 gpadc_extern_voltage_trim = 0; //使用外部输入方式校准的电压

/* 是否开启把vm配置项暂存到ram的功能 */
/* 具体使用方法和功能特性参考《项目帮助文档》的“11.4. 配置项管理 -VM配置项暂存RAM功能描述” */
const char vm_ram_storage_enable = FALSE;

const char vm_ram_storage_in_irq_enable = TRUE;

/* 设置vm的ram内存缓存区总大小限制，0为不限制，非0为限制大小（单位/byte）。 */
const int  vm_ram_storage_limit_data_total_size  = 16 * 1024;

// 设置vm写是否进行组包:
// 0 :不组包
// -1:使用malloc的buf进行组包(vm_write时分配)
// 具体数值:使用固定的buf进行组包(vm_init时分配)
const int vm_packet_size = 256;

const int config_rtc_enable = 0;

//gptimer
const u8 lib_gptimer_src_lsb_clk = 0; //时钟源选择lsb_clk, 单位:MHz
const u8 lib_gptimer_src_std_clk = 12; //时钟源选择std_x_clk, 单位:MHz
const u8 lib_gptimer_timer_mode_en = 1; //gptimer timer功能使能
const u8 lib_gptimer_pwm_mode_en = 1; //gptimer pwm功能使能
const u8 lib_gptimer_capture_mode_en = 1; //gptimer capture功能使能
const u8 lib_gptimer_auto_tid_en = 1; //gptimer_tid 内部自动分配使能
const u8 lib_gptimer_extern_use = GPTIMER_EXTERN_USE; //gptimer 模块外部已经占用, bit0=1表示timer0 外部占用，以此类推

const u32 lib_config_uart_flow_enable = 1;

//秘钥鉴权相关
#define     AUTH_multi_algorithm (1<<0)   //多算法授权
#define     AUTH_mkey_check      (1<<1)   //二级秘钥
#define     AUTH_sdk_chip_key    (1<<2)   //SDK秘钥校验

//需要对应的功能，就或上对应的宏定义，支持多种鉴权同时打开
#if (defined(TCFG_BURNER_CURRENT_CALIBRATION) && TCFG_BURNER_CURRENT_CALIBRATION)
const u32 lib_config_enable_auth_check = 0b0000 | AUTH_multi_algorithm;
#else
const u32 lib_config_enable_auth_check = 0b0000;
#endif
/**
 * @brief Log (Verbose/Info/Debug/Warn/Error)
 */
/*-----------------------------------------------------------*/
const char log_tag_const_v_CLOCK  = CONFIG_DEBUG_LIB(FALSE) ;
const char log_tag_const_i_CLOCK  = CONFIG_DEBUG_LIB(FALSE) ;
const char log_tag_const_d_CLOCK  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_w_CLOCK  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_CLOCK  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_LP_TIMER  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_LP_TIMER  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_LP_TIMER  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_LP_TIMER  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_LP_TIMER  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_LRC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_LRC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_LRC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_LRC  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_LRC  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_RCH AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_RCH AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_RCH AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_RCH AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_RCH AT(.LOG_TAG_CONST) = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_P33_MISC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_P33_MISC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_P33_MISC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_P33_MISC  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_P33_MISC  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_P33  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_P33  = CONFIG_DEBUG_LIB(0);   //dons++
const char log_tag_const_d_P33  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_P33  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_P33  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_PMU  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_PMU  = CONFIG_DEBUG_LIB(FALSE); //dons++
const char log_tag_const_d_PMU  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_PMU  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_PMU  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_RTC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_RTC  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_d_RTC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_RTC  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_RTC  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_WKUP  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_WKUP  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_d_WKUP  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_WKUP  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_WKUP  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_SDFILE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_SDFILE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_SDFILE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_SDFILE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_SDFILE  = CONFIG_DEBUG_LIB(FALSE);

const char log_tag_const_v_CHARGE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_CHARGE  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_d_CHARGE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_CHARGE  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_CHARGE  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_c_CHARGE  = CONFIG_DEBUG_LIB(FALSE);

const char log_tag_const_v_DEBUG  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_DEBUG  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_DEBUG  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_DEBUG  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_DEBUG  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_PWM_LED  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_PWM_LED  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_PWM_LED  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_PWM_LED  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_PWM_LED  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_KEY  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_KEY  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_KEY  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_KEY  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_KEY  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_TMR  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_TMR  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_TMR  = CONFIG_DEBUG_LIB(0);
const char log_tag_const_w_TMR  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_TMR  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_VM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_VM  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_d_VM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_VM  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_VM  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_TRIM_VDD  = CONFIG_DEBUG_LIB(FALSE) ;
const char log_tag_const_i_TRIM_VDD  = CONFIG_DEBUG_LIB(1);
const char log_tag_const_d_TRIM_VDD  = CONFIG_DEBUG_LIB(1);
const char log_tag_const_w_TRIM_VDD  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_TRIM_VDD  = CONFIG_DEBUG_LIB(TRUE);

//audio dac
const char log_tag_const_v_SYS_DAC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_SYS_DAC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_SYS_DAC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_SYS_DAC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_SYS_DAC  = CONFIG_DEBUG_LIB(FALSE);


const char log_tag_const_v_APP_EDET  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_APP_EDET  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_APP_EDET  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_APP_EDET  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_APP_EDET  = CONFIG_DEBUG_LIB(FALSE);


const char log_tag_const_v_FM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_FM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_FM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_FM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_FM  = CONFIG_DEBUG_LIB(FALSE);

const char log_tag_const_v_CORE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_CORE  = CONFIG_DEBUG_LIB(1);
const char log_tag_const_d_CORE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_CORE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_CORE  = CONFIG_DEBUG_LIB(FALSE);

const char log_tag_const_v_CACHE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_CACHE  = CONFIG_DEBUG_LIB(1);
const char log_tag_const_d_CACHE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_CACHE  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_CACHE  = CONFIG_DEBUG_LIB(FALSE);

const char log_tag_const_v_LP_KEY  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_LP_KEY  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_d_LP_KEY  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_w_LP_KEY  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_LP_KEY  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_SD  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_SD  = CONFIG_DEBUG_LIB(FALSE);    //dons++
const char log_tag_const_d_SD  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_SD  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_SD  = CONFIG_DEBUG_LIB(FALSE);    //dons++

const char log_tag_const_v_EAR_DETECT  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_EAR_DETECT  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_d_EAR_DETECT  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_EAR_DETECT  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_EAR_DETECT  = CONFIG_DEBUG_LIB(FALSE);

const char log_tag_const_v_TDM  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_TDM  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_d_TDM  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_w_TDM  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_TDM  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_USB  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_USB  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_USB  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_w_USB  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_USB  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_UART  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_UART  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_UART  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_UART  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_UART  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_IIC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_IIC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_IIC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_IIC  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_IIC  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_SPI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_SPI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_SPI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_SPI  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_SPI  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_EXTI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_EXTI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_EXTI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_EXTI  = CONFIG_DEBUG_LIB(TRUE);
const char log_tag_const_e_EXTI  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_GPIO  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_GPIO  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_GPIO  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_GPIO  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_GPIO  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_GPTIMER  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_GPTIMER  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_GPTIMER  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_GPTIMER  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_GPTIMER  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_PERI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_PERI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_PERI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_PERI  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_PERI  = CONFIG_DEBUG_LIB(TRUE);

const char log_tag_const_v_GPADC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_i_GPADC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_d_GPADC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_w_GPADC  = CONFIG_DEBUG_LIB(FALSE);
const char log_tag_const_e_GPADC  = CONFIG_DEBUG_LIB(TRUE);

