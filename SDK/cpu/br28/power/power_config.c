#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".power_config.data.bss")
#pragma data_seg(".power_config.data")
#pragma const_seg(".power_config.text.const")
#pragma code_seg(".power_config.text")
#endif
#include "app_config.h"
#include "cpu/includes.h"
#include "asm/charge.h"
#include "gpio_config.h"
#include "power/low_power.h"

#include "rtc/rtc_dev.h"

const struct low_power_param power_param = {
    //sniff时芯片是否进入低功耗
    .config         = TCFG_LOWPOWER_LOWPOWER_SEL,

    //外接晶振频率
    .btosc_hz       = TCFG_CLOCK_OSC_HZ,

    .vddiom_lev     = TCFG_LOWPOWER_VDDIOM_LEVEL,         //vddiom等级
    .osc_type       = TCFG_LOWPOWER_OSC_TYPE,			  //低功耗晶振类型，btosc/lrc
#if TCFG_APP_RTC_EN
    .rtc_clk        = RTC_CLK_RES_SEL,
#endif
    .lpctmu_en 		= TCFG_LP_TOUCH_KEY_ENABLE,
#if TCFG_LOWPOWER_RAM_SIZE
    .mem_init_con   = MEM_PWR_RAM_SET(TCFG_LOWPOWER_RAM_SIZE),
#else
    .mem_init_con = 0,
#endif

};

void key_wakeup_init();
void board_power_init(void)
{
    puts("power config\n");

    gpio_config_init();

    power_init(&power_param);

    /* 仅在启动时观察一次原生选择，不强制切换睡眠时钟。 */
    printf("[T2620_LP] sleep_clock product_lrc=%u cfg=%u default=%u selected=%u (0=LRC 1=BT_OSC)\n",
           TCFG_T2620_LOWPOWER_LRC_ENABLE, TCFG_LOWPOWER_OSC_TYPE,
           low_power_get_default_osc_type(), low_power_get_osc_type());

    key_wakeup_init();

#if (!TCFG_CHARGE_ENABLE)
    power_set_mode(TCFG_LOWPOWER_POWER_SEL);
#endif

}

