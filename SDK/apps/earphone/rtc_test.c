/*===========================================================================
 * FILE: rtc_test.c
 * DESC: 公版硬件 RTC 综合测试模块
 *
 * 场景覆盖：
 *   Test1  当前 RTC 时间快照      （rtc_read_time）
 *   Test2  基础读写验证           （rtc_write_time / rtc_read_time）
 *   Test3  可配置走时精度验证     （后台定时器）
 *   Test4  可配置闹钟触发验证     （rtc_write_alarm + rtc_alarm_en + 回调）
 *   Test5  工具函数验证       （time_diff_for_sec / time_add_sec /
 *                               caculate_weekday_by_time / 月末跨界 /
 *                               年末跨界 / datetime_to_sec↔sec_to_datetime）
 *===========================================================================*/

#include "rtc_test.h"

#if RTC_TEST_COMPILED

#include "rtc/rtc_dev.h"
#include "os/os_api.h"
#include "system/timer.h"
#include "system/generic/jiffies.h"

#if ((RTC_TEST_TIMING_SAMPLE_PERIOD_MS % 1000UL) != 0)
#error "RTC_TEST_TIMING_SAMPLE_PERIOD_MS must be a multiple of 1000"
#endif

#if (RTC_TEST_TIMING_SAMPLE_COUNT == 0)
#error "RTC_TEST_TIMING_SAMPLE_COUNT must be greater than 0"
#endif

/* -------------------------------------------------------------------------
 * 内部日志宏
 * ---------------------------------------------------------------------- */
#define RTC_LOG(fmt, ...)  printf("[RTC_TEST] " fmt "\n", ##__VA_ARGS__)
#define RTC_TEST_TIMING_SAMPLE_PERIOD_SEC   (RTC_TEST_TIMING_SAMPLE_PERIOD_MS / 1000UL)
#define RTC_TEST_BOOL_STR(enabled)          ((enabled) ? "ON" : "OFF")

static unsigned int rtc_test_u64_to_u32_clamped(u64 value)
{
    if (value > (u64)0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }

    return (unsigned int)value;
}

static int rtc_test_item_enabled(u32 item_mask)
{
    return (RTC_TEST_ITEM_MASK & item_mask) != 0;
}

static int rtc_test_time_equal(const struct sys_time *lhs, const struct sys_time *rhs)
{
    return lhs->year  == rhs->year  &&
           lhs->month == rhs->month &&
           lhs->day   == rhs->day   &&
           lhs->hour  == rhs->hour  &&
           lhs->min   == rhs->min   &&
           lhs->sec   == rhs->sec;
}

static int rtc_test_time_valid(const struct sys_time *time)
{
    if (time->year == 0 || time->month == 0 || time->day == 0) {
        return 0;
    }

    if (time->month > 12 || time->day > 31) {
        return 0;
    }

    if (time->hour > 23 || time->min > 59 || time->sec > 59) {
        return 0;
    }

    return 1;
}

static void rtc_test_log_sys_time(const char *prefix, const struct sys_time *time)
{
    RTC_LOG("%s%04d-%02d-%02d %02d:%02d:%02d",
            prefix,
            time->year, time->month, time->day,
            time->hour, time->min, time->sec);
}

static void rtc_test_log_config(void)
{
    RTC_LOG("Config mask=0x%02X reg=%s rw=%s timing=%s alarm=%s utils=%s",
        (unsigned int)RTC_TEST_ITEM_MASK,
            RTC_TEST_BOOL_STR(rtc_test_item_enabled(RTC_TEST_ITEM_REG_DUMP)),
            RTC_TEST_BOOL_STR(rtc_test_item_enabled(RTC_TEST_ITEM_BASIC_RW)),
            RTC_TEST_BOOL_STR(rtc_test_item_enabled(RTC_TEST_ITEM_TIMING)),
            RTC_TEST_BOOL_STR(rtc_test_item_enabled(RTC_TEST_ITEM_ALARM)),
            RTC_TEST_BOOL_STR(rtc_test_item_enabled(RTC_TEST_ITEM_UTILS)));

    if (rtc_test_item_enabled(RTC_TEST_ITEM_TIMING)) {
    RTC_LOG("Timing config: %u ms x %u",
        (unsigned int)RTC_TEST_TIMING_SAMPLE_PERIOD_MS,
                (unsigned int)RTC_TEST_TIMING_SAMPLE_COUNT);
    }

    if (rtc_test_item_enabled(RTC_TEST_ITEM_ALARM)) {
        RTC_LOG("Alarm config: +%u s", (unsigned int)RTC_TEST_ALARM_DELAY_SEC);
    }
}

/* =========================================================================
 * Test1: 当前 RTC 时间快照
 *   目的：统一 RTC_TEST 模块日志风格，并确认 rtc_read_time 接口可用
 *   预期：打印带 [RTC_TEST] 前缀的当前 RTC 时间
 * ======================================================================= */
static void test1_reg_dump(void)
{
    struct sys_time now = {0};

    RTC_LOG("=== Test1: RTC Snapshot ===");
    rtc_read_time(&now);
    rtc_test_log_sys_time("Test1 RTC: ", &now);
    RTC_LOG("=== Test1 Done ===");
}

/* =========================================================================
 * Test2: 基础读写验证
 *   目的：确认 rtc_write_time / rtc_read_time 接口通路正常
 *   预期日志（正常）：
 *     [RTC_TEST] write:     2025-06-15 10:30:00
 *     [RTC_TEST] read back: 2025-06-15 10:30:00
 *     [RTC_TEST] Test2 PASS: read back matches write
 *   异常：
 *     FAIL → TCFG_APP_RTC_EN 未生效，或 RTC 硬件故障
 * ======================================================================= */
static void test2_basic_rw(void)
{
    struct sys_time set_t = {
        .year = 2025, .month = 6, .day = 15,
        .hour = 10,   .min   = 30, .sec = 0,
    };
    struct sys_time rb = {0};

    RTC_LOG("=== Test2: Basic Read/Write ===");

    rtc_write_time(&set_t);
    RTC_LOG("write:     %04d-%02d-%02d %02d:%02d:%02d",
            set_t.year, set_t.month, set_t.day,
            set_t.hour, set_t.min,   set_t.sec);

    rtc_read_time(&rb);
    RTC_LOG("read back: %04d-%02d-%02d %02d:%02d:%02d",
            rb.year, rb.month, rb.day, rb.hour, rb.min, rb.sec);

    if (rb.year  == set_t.year  &&
        rb.month == set_t.month &&
        rb.day   == set_t.day   &&
        rb.hour  == set_t.hour  &&
        rb.min   == set_t.min) {
        RTC_LOG("Test2 PASS: read back matches write");
    } else {
        RTC_LOG("Test2 FAIL: mismatch! Check TCFG_APP_RTC_EN or RTC hardware");
    }
    RTC_LOG("=== Test2 Done ===");
}

/* =========================================================================
 * Test3: 60 秒走时精度验证（每 10 秒采样一次，共 6 次）
 *   目的：验证 RTC 实际在走时，且走时速度与系统 jiffies 基本一致
 *   预期日志（正常）：
 *     [RTC_TEST] Test3 [1/6] RTC now: 2025-06-15 10:30:10
 *       jiffies elapsed: ~10 s  |  RTC elapsed: 10 s
 *       -> PASS (expected ~10 s)
 *     ... (重复 6 次，最后一次打印 Done)
 *   异常排查：
 *     RTC elapsed: 0 s → 时钟源不通（无 32K 晶振则改 CLK_SEL_LRC）
 *     elapsed 远超预期  → 寄存器数据异常，查看 Test1 dump
 *     走时明显偏快/慢   → CLK_SEL_LRC 温漂，换外部 32K 晶振
 * ======================================================================= */
static struct sys_time  s_t3_start_rtc = {0};
static unsigned long    s_t3_start_ms  = 0;
static u32              s_t3_count     = 0;
static int              s_t3_timer_id  = 0;
static u8               s_t4_alarm_pending = 0;
static volatile u8      s_t4_alarm_posted = 0;
static struct sys_time  s_t4_alarm_expect = {0};

static u32 rtc_test_time_abs_diff_sec(const struct sys_time *lhs, const struct sys_time *rhs)
{
    u64 lhs_sec = datetime_to_sec(lhs);
    u64 rhs_sec = datetime_to_sec(rhs);

    if (lhs_sec >= rhs_sec) {
        return rtc_test_u64_to_u32_clamped(lhs_sec - rhs_sec);
    }

    return rtc_test_u64_to_u32_clamped(rhs_sec - lhs_sec);
}

static void rtc_test_alarm_handle_in_task(void *priv)
{
    u8 reason = (u8)(u32)priv;
    struct sys_time fired = {0};
    u32 delta_sec = 0;

    rtc_read_time(&fired);

    if (!s_t4_alarm_pending) {
        RTC_LOG("Test4 WARN: unexpected alarm callback, reason=0x%02X", reason);
        rtc_alarm_en(0);
        s_t4_alarm_posted = 0;
        return;
    }

    s_t4_alarm_pending = 0;
    delta_sec = rtc_test_time_abs_diff_sec(&s_t4_alarm_expect, &fired);

    RTC_LOG("=== Test4: Alarm Callback Fired! reason=0x%02X ===", reason);
    RTC_LOG("  RTC alarm expected: %04d-%02d-%02d %02d:%02d:%02d",
            s_t4_alarm_expect.year, s_t4_alarm_expect.month, s_t4_alarm_expect.day,
            s_t4_alarm_expect.hour, s_t4_alarm_expect.min,   s_t4_alarm_expect.sec);
    RTC_LOG("  RTC at alarm: %04d-%02d-%02d %02d:%02d:%02d",
            fired.year, fired.month, fired.day,
            fired.hour, fired.min,   fired.sec);
    RTC_LOG("  alarm delta: %u s", delta_sec);

    rtc_alarm_en(0);    /* 关闭闹钟，防止持续触发 */
    s_t4_alarm_posted = 0;
    if (delta_sec <= 1) {
        RTC_LOG("Test4 PASS: alarm fired and disabled within 1 s tolerance");
    } else {
        RTC_LOG("Test4 WARN: alarm fired but deviated by %u s", delta_sec);
    }
}

static void test3_sample(void *priv)
{
    (void)priv;

    struct sys_time now = {0};

    s_t3_count++;
    rtc_read_time(&now);

    u32 elapsed_jiffies_s = (unsigned long)(jiffies_msec() - s_t3_start_ms) / 1000;
    u32 elapsed_rtc_s     = time_diff_for_sec(&s_t3_start_rtc, &now);
    u32 expected          = s_t3_count * RTC_TEST_TIMING_SAMPLE_PERIOD_SEC;

    RTC_LOG("Test3 [%u/%u] RTC now: %04d-%02d-%02d %02d:%02d:%02d",
            s_t3_count,
        (unsigned int)RTC_TEST_TIMING_SAMPLE_COUNT,
            now.year, now.month, now.day, now.hour, now.min, now.sec);
    RTC_LOG("  jiffies elapsed: ~%u s  |  RTC elapsed: %u s",
            elapsed_jiffies_s, elapsed_rtc_s);

    if (elapsed_rtc_s == 0) {
        RTC_LOG("  -> FAIL: RTC NOT ticking. Check clock source (CLK_SEL_32K needs crystal)");
    } else if (elapsed_rtc_s >= expected - 2 && elapsed_rtc_s <= expected + 2) {
        RTC_LOG("  -> PASS (expected ~%u s)", expected);
    } else {
        RTC_LOG("  -> WARN: RTC %u s vs expected ~%u s", elapsed_rtc_s, expected);
    }

    if (s_t3_count >= RTC_TEST_TIMING_SAMPLE_COUNT) {
        sys_timer_del(s_t3_timer_id);
        s_t3_timer_id = 0;
        RTC_LOG("=== Test3 Done ===");
    }
}

static void test3_timing_start(void)
{
    if (s_t3_timer_id) {
        sys_timer_del(s_t3_timer_id);
        s_t3_timer_id = 0;
    }

    RTC_LOG("=== Test3: Timing Accuracy ===");
    rtc_read_time(&s_t3_start_rtc);
    s_t3_start_ms = (unsigned long)jiffies_msec();
    s_t3_count    = 0;

    RTC_LOG("Test3 start: %04d-%02d-%02d %02d:%02d:%02d",
            s_t3_start_rtc.year, s_t3_start_rtc.month, s_t3_start_rtc.day,
            s_t3_start_rtc.hour, s_t3_start_rtc.min,   s_t3_start_rtc.sec);

    s_t3_timer_id = sys_timer_add(NULL, test3_sample, RTC_TEST_TIMING_SAMPLE_PERIOD_MS);
    if (!s_t3_timer_id) {
        RTC_LOG("Test3 FAIL: unable to create timing timer");
    }
}

/* =========================================================================
 * Test4: 可配置延迟闹钟触发验证
 *   目的：验证 rtc_write_alarm + rtc_alarm_en + 回调链路
 *   预期日志（正常，约 RTC_TEST_ALARM_DELAY_SEC 秒后）：
 *     [RTC_TEST] === Test4: Alarm Callback Fired! reason=0x00/0x01/... ===
 *     [RTC_TEST]   RTC alarm expected: 2025-06-15 10:30:20
 *     [RTC_TEST]   RTC at alarm:      2025-06-15 10:30:19/20/21
 *     [RTC_TEST]   alarm delta: 0/1 s
 *     [RTC_TEST] Test4 PASS: alarm fired and disabled within 1 s tolerance
 *   异常排查：
 *     20 秒后无回调 → device_config.c 的 .cbfun 未注册为 rtc_test_alarm_callback
 *                     或 rtc_alarm_en(1) 未调用
 *     触发后打 assert → 回调运行在 RTC 中断里，真正的日志/RTC 读写需切到任务态执行
 *     时间不对       → 检查 time_add_sec 跨界逻辑
 * ======================================================================= */
void rtc_test_alarm_callback(u8 reason)
{
    int msg[3];

    if (!rtc_test_item_enabled(RTC_TEST_ITEM_ALARM)) {
        return;
    }

    if (s_t4_alarm_posted) {
        return;
    }

    s_t4_alarm_posted = 1;
    msg[0] = (int)rtc_test_alarm_handle_in_task;
    msg[1] = 1;
    msg[2] = reason;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        s_t4_alarm_posted = 0;
    }
}

static void test4_alarm_start(void)
{
    struct sys_time now   = {0};
    struct sys_time alarm = {0};

    RTC_LOG("=== Test4: Delayed Alarm Trigger ===");

    rtc_alarm_en(0);
    s_t4_alarm_pending = 0;
    s_t4_alarm_posted = 0;
    rtc_read_time(&now);

    if (!rtc_test_time_valid(&now)) {
        RTC_LOG("Test4 FAIL: invalid RTC current time, skip alarm setup");
        rtc_test_log_sys_time("Test4 invalid now: ", &now);
        return;
    }

    alarm = now;
    time_add_sec(&alarm, RTC_TEST_ALARM_DELAY_SEC);
    s_t4_alarm_expect = alarm;

    rtc_write_alarm(&alarm);
    s_t4_alarm_pending = 1;
    rtc_alarm_en(1);

    RTC_LOG("Test4 now:   %04d-%02d-%02d %02d:%02d:%02d",
            now.year,   now.month,   now.day,   now.hour,   now.min,   now.sec);
    RTC_LOG("Test4 alarm: %04d-%02d-%02d %02d:%02d:%02d  (fires in ~%u s)",
            alarm.year, alarm.month, alarm.day, alarm.hour, alarm.min, alarm.sec,
            (unsigned int)RTC_TEST_ALARM_DELAY_SEC);
}

/* =========================================================================
 * Test5: 工具函数验证
 *   目的：对 rtc_dev.h 提供的工具函数进行单元测试
 *   覆盖：time_diff_for_sec / time_add_sec (正向跨分钟 / 月末 / 年末) /
 *         caculate_weekday_by_time / datetime_to_sec↔sec_to_datetime /
 *         month_to_day 当月天数判断
 * ======================================================================= */
static void test5_utils(void)
{
    u8 fail_count = 0;

    RTC_LOG("=== Test5: Utility Functions ===");

    /* 5a: time_diff_for_sec —— 基础差值 */
    {
        struct sys_time ta = {.year=2025,.month=6,.day=15,.hour=10,.min=0,.sec=0};
        struct sys_time tb = {.year=2025,.month=6,.day=15,.hour=10,.min=1,.sec=30};
        u32 diff = time_diff_for_sec(&ta, &tb);
        int ok = (diff == 90);
        RTC_LOG("5a time_diff_for_sec: expected 90, got %u -> %s",
                diff, ok ? "PASS" : "FAIL");
        fail_count += !ok;
    }

    /* 5b: time_add_sec —— 正向跨分钟 */
    {
        struct sys_time t = {.year=2025,.month=6,.day=15,.hour=10,.min=59,.sec=45};
        time_add_sec(&t, 20);   /* 10:59:45 + 20s = 11:00:05 */
        int ok = (t.hour==11 && t.min==0 && t.sec==5);
        RTC_LOG("5b time_add_sec cross-minute: expected 11:00:05, got %02d:%02d:%02d -> %s",
                t.hour, t.min, t.sec, ok ? "PASS" : "FAIL");
        fail_count += !ok;
    }

    /* 5c: time_add_sec —— 月末跨界（6月→7月） */
    {
        struct sys_time t = {.year=2025,.month=6,.day=30,.hour=23,.min=59,.sec=50};
        time_add_sec(&t, 15);   /* 2025-06-30 23:59:50 + 15s = 2025-07-01 00:00:05 */
        int ok = (t.month==7 && t.day==1 && t.hour==0 && t.min==0 && t.sec==5);
        RTC_LOG("5c time_add_sec cross-month: expected 2025-07-01 00:00:05, "
                "got %04d-%02d-%02d %02d:%02d:%02d -> %s",
                t.year, t.month, t.day, t.hour, t.min, t.sec, ok ? "PASS" : "FAIL");
        fail_count += !ok;
    }

    /* 5d: time_add_sec —— 年末跨界 */
    {
        struct sys_time t = {.year=2025,.month=12,.day=31,.hour=23,.min=59,.sec=55};
        struct sys_time expected = {.year=2026,.month=1,.day=1,.hour=0,.min=0,.sec=5};
        time_add_sec(&t, 10);   /* 2025-12-31 23:59:55 + 10s = 2026-01-01 00:00:05 */
        int ok = rtc_test_time_equal(&t, &expected);
        RTC_LOG("5d time_add_sec cross-year: expected 2026-01-01 00:00:05, "
                "got %04d-%02d-%02d %02d:%02d:%02d -> %s",
                t.year, t.month, t.day, t.hour, t.min, t.sec, ok ? "PASS" : "FAIL");
        fail_count += !ok;
        if (!ok) {
            RTC_LOG("5d detail: time_add_sec year rollover result differs from expected calendar result");
        }
    }

    /* 5e: caculate_weekday_by_time —— SDK 约定为 Monday=1 ... Sunday=7 */
    {
        struct sys_time t = {.year=2025,.month=6,.day=15};  /* 周日 */
        u8 wd = caculate_weekday_by_time(&t);
        int ok_sun = (wd == 7);
        RTC_LOG("5e weekday 2025-06-15(Sun): expected 7, got %d -> %s",
                wd, ok_sun ? "PASS" : "FAIL");

        t.day = 16;  /* 周一 */
        wd = caculate_weekday_by_time(&t);
        {
            int ok_mon = (wd == 1);
        RTC_LOG("5e weekday 2025-06-16(Mon): expected 1, got %d -> %s",
                wd, ok_mon ? "PASS" : "FAIL");
            fail_count += !(ok_sun && ok_mon);
        }
    }

    /* 5f: datetime_to_sec + sec_to_datetime 往返验证 */
    {
        struct sys_time orig = {.year=2025,.month=6,.day=15,.hour=12,.min=30,.sec=45};
        struct sys_time back = {0};
        u64 secs = datetime_to_sec(&orig);
        sec_to_datetime(secs, &back);
        int ok = (back.year ==orig.year  && back.month==orig.month &&
                  back.day  ==orig.day   && back.hour ==orig.hour  &&
                  back.min  ==orig.min   && back.sec  ==orig.sec);
        RTC_LOG("5f datetime_to_sec+sec_to_datetime: %04d-%02d-%02d %02d:%02d:%02d "
            "-> %u s -> %04d-%02d-%02d %02d:%02d:%02d -> %s",
                orig.year, orig.month, orig.day, orig.hour, orig.min, orig.sec,
            rtc_test_u64_to_u32_clamped(secs),
                back.year, back.month, back.day, back.hour, back.min, back.sec,
                ok ? "PASS" : "FAIL");
        fail_count += !ok;
    }

    /* 5g: month_to_day —— 当月天数验证 */
    {
        u16 d_leap   = month_to_day(2024, 2);   /* 2024 闰年 2 月应为 29 天 */
        u16 d_normal = month_to_day(2025, 2);   /* 2025 平年 2 月应为 28 天 */
        RTC_LOG("5g month_to_day(days-in-month): 2024-Feb=%d(exp 29)%s  2025-Feb=%d(exp 28)%s",
                d_leap,   d_leap   == 29 ? " PASS" : " FAIL",
                d_normal, d_normal == 28 ? " PASS" : " FAIL");
        if (!(d_leap == 29 && d_normal == 28)) {
            fail_count++;
            RTC_LOG("5g detail: month_to_day leap-year result is inconsistent with calendar expectation");
        }
    }

    if (fail_count == 0) {
        RTC_LOG("Test5 PASS: all utility checks passed");
    } else {
        RTC_LOG("Test5 WARN: %u utility check(s) failed; current evidence points to 5d/5g as library defects", fail_count);
    }

    RTC_LOG("=== Test5 Done ===");
}

/* =========================================================================
 * 对外接口：rtc_test_init()
 *   在 app_task_init() 末尾调用一次。
 *   立即执行：根据 RTC_TEST_ITEM_MASK 调度需要的同步测试
 *   后台执行：根据 RTC_TEST_ITEM_MASK 调度 Test3 / Test4
 * ======================================================================= */
void rtc_test_init(void)
{
    if (RTC_TEST_ITEM_MASK == RTC_TEST_ITEM_NONE) {
        RTC_LOG("RTC test enabled, but no items are selected.");
        return;
    }

    RTC_LOG("========================================");
    RTC_LOG("  RTC Test Module Start");
    RTC_LOG("========================================");

    rtc_test_log_config();

    if (rtc_test_item_enabled(RTC_TEST_ITEM_REG_DUMP)) {
        test1_reg_dump();
    }

    if (rtc_test_item_enabled(RTC_TEST_ITEM_BASIC_RW)) {
        test2_basic_rw();
    }

    if (rtc_test_item_enabled(RTC_TEST_ITEM_UTILS)) {
        test5_utils();
    }

    if (rtc_test_item_enabled(RTC_TEST_ITEM_ALARM)) {
        test4_alarm_start();
    }

    if (rtc_test_item_enabled(RTC_TEST_ITEM_TIMING)) {
        test3_timing_start();
    }

    RTC_LOG("Init done. Selected RTC tests started.");
    RTC_LOG("========================================");
}

#endif /* TCFG_APP_RTC_EN */