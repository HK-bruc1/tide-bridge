/*===========================================================================
 * FILE: rtc_test.h
 * DESC: 公版硬件 RTC 测试模块头文件
 *
 * 使用前提：
 *   app_config.h 中 TCFG_APP_RTC_EN = 1
 *   app_config.h 中 RTC_TEST_ENABLE = 1
 *
 * 配置入口：
 *   RTC_TEST_ENABLE               总开关
 *   RTC_TEST_ITEM_MASK            测试项 bitmask
 *   RTC_TEST_TIMING_SAMPLE_*      走时测试参数
 *   RTC_TEST_ALARM_DELAY_SEC      闹钟测试参数
 *
 * 调用说明（两处）：
 *
 *   ① apps/earphone/app_main.c  →  app_task_init() 末尾
 *        rtc_test_init();
 *
 *   ② apps/earphone/device_config.c  →  RTC 平台数据的 cbfun 字段
 *        .cbfun = rtc_test_alarm_callback,
 *===========================================================================*/

#ifndef __RTC_TEST_H__
#define __RTC_TEST_H__

#include "app_config.h"

#include "typedef.h"

#define RTC_TEST_COMPILED   (TCFG_APP_RTC_EN && RTC_TEST_ENABLE)

#if RTC_TEST_COMPILED

/*
 * rtc_test_init()
 *   每次上电后调用一次。
 *   - 根据 RTC_TEST_ITEM_MASK 选择执行哪些测试。
 *   - 测试项和时序参数均由 app_config.h 中的宏控制。
 */
void rtc_test_init(void);

/*
 * rtc_test_alarm_callback(u8 reason)
 *   闹钟中断回调，注册到 device_config.c 的 .cbfun 字段。
 *   闹钟触发时由 RTC 驱动自动调用，按 ISR 上下文处理。
 *   该回调自身只负责把后续处理投递到 app_core；
 *   不要在这里直接做 RTC_LOG、rtc_read_time、rtc_alarm_en(0) 等任务态操作。
 *   reason 值由底层驱动定义，测试日志按实测打印，不应写死为固定常量。
 */
void rtc_test_alarm_callback(u8 reason);

#else

static inline void rtc_test_init(void)
{
}

static inline void rtc_test_alarm_callback(u8 reason)
{
	(void)reason;
}

#endif /* TCFG_APP_RTC_EN */

#endif /* __RTC_TEST_H__ */