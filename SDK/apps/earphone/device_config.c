#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".device_config.data.bss")
#pragma data_seg(".device_config.data")
#pragma const_seg(".device_config.text.const")
#pragma code_seg(".device_config.text")
#endif
#include "system/includes.h"
#include "app_main.h"
#include "app_config.h"
#include "asm/sdmmc.h"
// #include "asm/rtc.h"
#include "rtc/rtc_dev.h"

#include "linein_dev.h"
#include "usb/host/usb_storage.h"

#include "rtc_test.h"

//dons++
int sdmmc_0_io_detect_user(const struct sdmmc_platform_data *p)
{
    return 1;
}

//dons++
void sd_set_power_user(u8 en);

#if TCFG_SD0_ENABLE
SD0_PLATFORM_DATA_BEGIN(sd0_data) = {
    .port = {
        TCFG_SD0_PORT_CMD,
        TCFG_SD0_PORT_CLK,
        TCFG_SD0_PORT_DA0,
        TCFG_SD0_PORT_DA1,
        TCFG_SD0_PORT_DA2,
        TCFG_SD0_PORT_DA3,
    },
    .data_width             = TCFG_SD0_DAT_MODE,
    .speed                  = TCFG_SD0_CLK,
    .detect_mode            = TCFG_SD0_DET_MODE,
    .priority				= 3,

#if (TCFG_SD0_DET_MODE == SD_IO_DECT)
    .detect_io              = TCFG_SD0_DET_IO,
    .detect_io_level        = TCFG_SD0_DET_IO_LEVEL,
    .detect_func            = sdmmc_0_io_detect_user,
#elif (TCFG_SD0_DET_MODE == SD_CLK_DECT)
    .detect_io_level        = TCFG_SD0_DET_IO_LEVEL,
    .detect_func            = sdmmc_0_clk_detect,
#else
    .detect_func            = sdmmc_cmd_detect,
#endif

#if (TCFG_SD0_POWER_SEL == SD_PWR_SDPG)
    .power                  = sd_set_power,
#else
    .power                  = sd_set_power_user, //NULL
#endif

    SD0_PLATFORM_DATA_END()
};
#endif

/************************** linein KEY ****************************/
#if TCFG_APP_LINEIN_EN
struct linein_dev_data linein_data = {
    .enable = TCFG_APP_LINEIN_EN,
    .port   = NO_CONFIG_PORT,
    .up     = 1,
    .down   = 0,
    .ad_channel = NO_CONFIG_PORT,
    .ad_vol = 0,
};
#endif

/************************** rtc ****************************/
#if TCFG_APP_RTC_EN
//初始一下当前时间
const struct sys_time def_sys_time = {
    .year = 2000,
    .month = 1,
    .day = 1,
    .hour = 0,
    .min = 0,
    .sec = 0,
};

//初始一下目标时间，即闹钟时间
const struct sys_time def_alarm = {
    .year = 2050,
    .month = 1,
    .day = 1,
    .hour = 0,
    .min = 0,
    .sec = 0,
};

/* extern void alarm_isr_user_cbfun(u8 index); */
RTC_DEV_PLATFORM_DATA_BEGIN(rtc_data)
    .default_sys_time = &def_sys_time,
    .default_alarm = &def_alarm,
    .rtc_clk = RTC_CLK_RES_SEL,//引用板级配置中的宏定义，默认也是CLK_SEL_32K
    .rtc_sel = HW_RTC,
    //闹钟中断的回调函数,用户自行定义
#if RTC_TEST_ENABLE && (RTC_TEST_ITEM_MASK & RTC_TEST_ITEM_ALARM)
    .cbfun = rtc_test_alarm_callback,
#else
    .cbfun = NULL,
#endif
    /* .cbfun = alarm_isr_user_cbfun, */
    RTC_DEV_PLATFORM_DATA_END()
#endif

REGISTER_DEVICES(device_table) = {
#if TCFG_SD0_ENABLE
    { "sd0", 	&sd_dev_ops,	(void *) &sd0_data},
#endif
#if TCFG_APP_LINEIN_EN
    { "linein",  &linein_dev_ops, (void *) &linein_data},
#endif
#if TCFG_UDISK_ENABLE
    { "udisk0",   &mass_storage_ops, NULL},
#endif

#if TCFG_APP_RTC_EN
    { "rtc",   &rtc_dev_ops, (void *) &rtc_data},
#endif
};
