"""运行实际离线广播策略代码，注入队列、定时器和广播故障。

本测试验证软件执行顺序，不验证实际射频间隔、SDK 操作完成情况或 PA4 电压。
"""
import re
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, function, run_c_checks


def main():
    source = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_adv_policy.c').read_text(encoding='utf-8-sig')
    source = re.sub(r'^#include "[^\n]+\n', '', source, flags=re.M)
    stubs = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
#define Q_CALLBACK 1
#define RDX_SHARED_VDD_WAKE_BUSINESS 1
#define printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static u32 now_ms, hold_mask, timer_delay;
static int allowed, queue_fail, timer_fail, radio_fail, restore_fail;
static int queue_pending, radio_calls, radio_slow, slow_commits, file_queue_fail;
static void (*timer_cb)(void *);
static void *timer_arg;
void local_irq_disable(void) {}
void local_irq_enable(void) {}
u32 sys_timer_get_ms(void) { return now_ms; }
int os_taskq_post_type(const char *t, int type, int n, int *msg) {
    if (queue_fail) return -1;
    queue_pending = 1;
    return 0;
}
int os_taskq_post_msg(const char *t, int argc, ...) { return file_queue_fail; }
u16 sys_timeout_add(void *arg, void (*cb)(void *), u32 delay) {
    if (timer_fail) return 0;
    timer_arg = arg; timer_cb = cb; timer_delay = delay;
    return 1;
}
void sys_timeout_del(u16 id) { timer_cb = 0; }
int rdx_peripheral_power_vdd_ensure_on(int reason) { return restore_fail; }
u8 rdx_adv_policy_allowed(void) { return allowed; }
u32 rdx_adv_policy_hold_snapshot(void) { return hold_mask; }
int rdx_adv_policy_radio_set(u8 slow) {
    ++radio_calls;
    if (radio_fail) return -1;
    radio_slow = slow;
    return 0;
}
void rdx_adv_policy_slow_committed(void) { ++slow_commits; }
void rdx_adv_policy_business_exit(void);
'''
    checks = r'''
static void reset(void) {
    for (unsigned int i=0; i<sizeof(adv); ++i) ((unsigned char *)&adv)[i] = 0;
    now_ms = hold_mask = timer_delay = 0;
    allowed = 1;
    queue_fail = timer_fail = radio_fail = restore_fail = 0;
    queue_pending = radio_calls = radio_slow = slow_commits = file_queue_fail = 0;
    timer_cb = 0;
}
static int drain(void) {
    int budget = 32;
    while (queue_pending && budget--) { queue_pending = 0; adv_run(0); }
    return budget > 0;
}
static void fire(void) {
    void (*cb)(void *) = timer_cb;
    void *arg = timer_arg;
    timer_cb = 0;
    if (cb) cb(arg);
}
int check_idle_activity_and_stale(void) {
    reset(); rdx_adv_policy_enable(); CHECK(drain());
    CHECK(!radio_slow && timer_delay == 120000 && !rdx_adv_policy_idle_valid());
    void *stale = timer_arg;
    now_ms = 119000; rdx_adv_policy_activity(); CHECK(drain());
    adv_timer(stale); CHECK(!queue_pending);
    now_ms = 120000; CHECK(!rdx_adv_policy_idle_valid());
    now_ms = 239000; fire(); CHECK(drain());
    CHECK(radio_slow && slow_commits == 1 && rdx_adv_policy_idle_valid() && !timer_cb);
    rdx_adv_policy_changed(); CHECK(drain());
    CHECK(radio_slow && slow_commits == 1 && !timer_cb);
    now_ms++; rdx_adv_policy_activity(); CHECK(!rdx_adv_policy_idle_valid()); CHECK(drain());
    CHECK(!radio_slow && timer_delay == 120000);
    return 0;
}
int check_business_overlap_and_short(void) {
    reset(); rdx_adv_policy_enable(); CHECK(drain());
    now_ms = 119999;
    CHECK(!rdx_adv_policy_business_enter());
    CHECK(!rdx_adv_policy_business_enter()); CHECK(drain());
    CHECK(!rdx_adv_policy_idle_valid() && !radio_slow && timer_delay == 1000);
    now_ms = 700000; fire(); CHECK(drain()); CHECK(!radio_slow);
    rdx_adv_policy_business_exit(); CHECK(drain()); CHECK(timer_delay == 1000);
    rdx_adv_policy_business_exit(); CHECK(drain()); CHECK(timer_delay == 120000);
    now_ms += 50000; rdx_adv_policy_business_exit(); CHECK(drain());
    CHECK(timer_delay == 70000); /* 重复完成通知不得延长空闲窗口。 */
    now_ms += 70000; fire(); CHECK(drain()); CHECK(radio_slow);
    CHECK(!rdx_adv_policy_business_enter()); rdx_adv_policy_business_exit();
    CHECK(!rdx_adv_policy_idle_valid()); CHECK(drain());
    CHECK(!radio_slow && timer_delay == 120000);
    return 0;
}
int check_snapshot_pause_and_timeout(void) {
    reset(); rdx_adv_policy_enable(); CHECK(drain());
    hold_mask = 2; now_ms = 120000; fire(); CHECK(drain());
    CHECK(!radio_slow && !slow_commits && timer_delay == 1000);
    now_ms += 600000; fire(); CHECK(drain()); CHECK(!radio_slow);
    hold_mask = 0; now_ms++; fire(); CHECK(drain());
    CHECK(timer_delay == 120000);
    return 0;
}
int check_failures_and_recovery(void) {
    reset(); timer_fail = 1; rdx_adv_policy_enable(); CHECK(drain());
    CHECK(!timer_cb && !rdx_adv_policy_idle_valid());
    now_ms = 500000; timer_fail = 0; rdx_adv_policy_changed(); CHECK(drain());
    CHECK(!radio_slow && timer_delay == 120000); /* 此前未成功启动定时器，需重新开始空闲窗口。 */
    now_ms += 120000; radio_fail = 1; fire(); CHECK(drain());
    CHECK(!rdx_adv_policy_idle_valid() && !slow_commits);
    for (int i=0; i<5; ++i) { now_ms += 100; fire(); CHECK(drain()); }
    CHECK(!timer_cb && !rdx_adv_policy_idle_valid());
    radio_fail = 0; rdx_adv_policy_activity(); CHECK(drain());
    CHECK(timer_delay == 120000 && !radio_slow);
    queue_fail = 1; rdx_adv_policy_activity();
    for (int i=0; i<5; ++i) { now_ms += 100; fire(); }
    CHECK(!rdx_adv_policy_idle_valid() && !timer_cb);
    queue_fail = 0; rdx_adv_policy_changed(); CHECK(drain());
    CHECK(timer_delay == 120000);
    restore_fail = 1; CHECK(rdx_adv_policy_business_enter() != 0); CHECK(drain());
    CHECK(!adv.pending && !rdx_adv_policy_idle_valid());
    file_queue_fail = -1; restore_fail = 0;
    CHECK(rdx_adv_uxfile_post("UXFILE", 1, 3) != 0); CHECK(!adv.pending);
    file_queue_fail = 0;
    CHECK(!rdx_adv_uxfile_post("UXFILE", 1, 3)); CHECK(adv.pending == 1);
    rdx_adv_uxfile_done(0, 0x40000003); CHECK(adv.pending == 1);
    rdx_adv_uxfile_done(1, 0x40000003); CHECK(!adv.pending);
    return 0;
}
int check_deferred_and_wrap(void) {
    reset(); now_ms = 0xffff0000u; rdx_adv_policy_enable(); CHECK(drain());
    now_ms += 119999; rdx_adv_policy_changed(); CHECK(drain()); CHECK(!radio_slow);
    now_ms++; fire(); CHECK(drain()); CHECK(radio_slow);
    rdx_adv_policy_disable(); CHECK(!rdx_adv_policy_idle_valid()); CHECK(drain());
    int calls = radio_calls;
    rdx_adv_policy_activity(); CHECK(drain()); CHECK(radio_calls == calls);
    allowed = 0; rdx_adv_policy_enable(); CHECK(drain()); CHECK(radio_calls == calls);
    hold_mask = 1; allowed = 1; rdx_adv_policy_changed(); CHECK(drain());
    CHECK(!radio_slow && timer_delay == 1000);
    hold_mask = 0; rdx_adv_policy_changed(); CHECK(drain()); CHECK(timer_delay == 120000);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='rdx-adv-policy-') as tmp:
        path = Path(tmp) / 'adv_policy.c'
        path.write_text(stubs + source + checks, encoding='utf-8')
        run_c_checks(path, [
            'check_idle_activity_and_stale', 'check_business_overlap_and_short',
            'check_snapshot_pause_and_timeout', 'check_failures_and_recovery',
            'check_deferred_and_wrap',
        ], native=True)
    print('Advertising policy: idle/hold/short operations, stale events, wrap, deferred modes and failures passed.')
    check_radio_adapter()


def check_radio_adapter():
    server = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c').read_text(encoding='utf-8-sig')
    stubs = r'''
typedef unsigned char u8;
#define ADV_IND 0
#define ADV_CHANNEL_ALL 7
#define RDX_BLE_ADV_INTERVAL_LOW 1600
#define r_printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static void *g_rdx_ble_advertising_hdl;
static struct { int adv_interval_min; } g_rdx_ble_server_info;
static int calls, fail_at, active, interval, wake_fail, wake_calls, allowed;
u8 rdx_adv_policy_allowed(void) { return allowed; }
int rdx_peripheral_power_vdd_fast_adv_notify(void) { ++wake_calls; return wake_fail; }
int app_ble_adv_enable(void *h, int on) {
    if (++calls == fail_at) return -7;
    active = on; return 0;
}
int app_ble_set_adv_param(void *h, int value, int type, int channels) {
    if (++calls == fail_at) return -7;
    interval = value; return 0;
}
int app_ble_adv_state_get(void *h) { return active; }
'''
    checks = r'''
int check_radio(void) {
    g_rdx_ble_advertising_hdl = (void *)1;
    g_rdx_ble_server_info.adv_interval_min = 48;
    allowed = 1;
    for (int i=1; i<=3; ++i) {
        calls = 0; fail_at = i; active = 1; interval = 48;
        CHECK(rdx_adv_policy_radio_set(1) != 0);
        CHECK(active && interval == 48); /* 任一步骤失败后均不得误判慢广播切换成功。 */
    }
    calls = 0; fail_at = 0;
    CHECK(!rdx_adv_policy_radio_set(1)); CHECK(active && interval == 1600);
    wake_fail = -1; CHECK(!rdx_adv_policy_radio_set(0));
    CHECK(wake_calls == 1 && active && interval == 48);
    allowed = 0; calls = 0; CHECK(rdx_adv_policy_radio_set(1) != 0); CHECK(!calls);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='rdx-adv-radio-') as tmp:
        path = Path(tmp) / 'radio.c'
        path.write_text(stubs + function(server, 'rdx_adv_policy_radio_set') + checks, encoding='utf-8')
        run_c_checks(path, ['check_radio'], native=True)
    print('Advertising radio adapter: disable/parameter/enable failures, fast fallback and independent VDD failure passed.')


if __name__ == '__main__':
    main()
