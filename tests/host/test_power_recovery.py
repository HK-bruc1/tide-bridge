"""使用模拟引脚运行实际拨码控制代码，验证 app_core 停滞时的行为。"""
import re
from host_c_test_lib import ROOT, function, run_c_checks

source = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_dip_switch.c').read_text(encoding='utf-8')
source = re.sub(r'^#include.*\n', '', source, flags=re.M)
stubs = r"""
#define TCFG_DIP_SWITCH_POWER_ENABLE 1
#ifndef TCFG_T2620_PC_STORAGE_ENABLE
#define TCFG_T2620_PC_STORAGE_ENABLE 0
#endif
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 1
#define TCFG_DIP_SWITCH_POWER_IO 1
#define IO_PORT_SPILT(x) x
#define PORT_INPUT_PULLUP_10K 0
#define REGISTER_LP_TARGET(x) static struct { const char *name; unsigned char (*is_idle)(void); } x
#define ASSERT(x) ((void)0)
#define RISING_EDGE 1
#define FALLING_EDGE 0
#define SLAVE_MODE 1
#define Q_CALLBACK 2
#define APP_MODE_PC 1
#define APP_MODE_IDLE 2
#define APP_MODE_BT 3
#define IDLE_MODE_CHARGE 4
#define APP_MSG_REQUEST_POWEROFF 5
#define APP_MSG_GOTO_MODE 6
#define POWEROFF_NORMAL 0
#define r_printf(...) ((void)0)
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef int bool;
typedef int P33_IO_WKUP_EDGE;
#define true 1
#define false 0
#define NULL ((void*)0)
static int on=1, vbus, role, mode=APP_MODE_BT, started, reset_count, off_requests, service_calls, edge;
static int worker_polls, dut_need, cdc_need, hid_need, led_need, storage_changed, storage_wait;
static int posts, queued, post_fail, timer_fail, timeout_adds, periodic_adds, user_adds, bad_priority;
static int mode_requests, target_mode, wake_inside_service, usb_calls;
static u32 now, timer_due, timer_ms, usb_delay;
static u16 timer_id, next_id;
static void *timer_priv;
static void (*timer_cb)(void *);
void rdx_dip_switch_wake(void);
void rdx_adv_policy_activity(void) {}
void rdx_dip_switch_shutdown_begin(void);
void rdx_factory_cdc_poll(void) { ++worker_polls; cdc_need=0; }
void rdx_hogp_input_poll(void) { ++worker_polls; hid_need=0; }
void rdx_dut_service_poll(void) { ++worker_polls; dut_need=0; }
int rdx_factory_cdc_needs_poll(void) { return cdc_need; }
int rdx_hogp_input_needs_poll(void) { return hid_need; }
int rdx_dut_service_needs_poll(void) { return dut_need; }
int rdx_led_ctrl_needs_service(void) { return led_need; }
void rdx_led_ctrl_service(void) { ++worker_polls; led_need=0; }
int rdx_storage_lifecycle_changed(void) { return storage_changed; }
void rdx_dut_service_wake(void) {}
int rdx_dut_test_keys_active(void) { return 0; }
void usb_factory_service(void) { ++usb_calls; }
u32 usb_factory_service_delay(void) { return usb_delay; }
static struct { int goto_poweroff_flag, goto_poweroff_cnt; } app_var;
int get_power_on_status(void) { return on; }
int get_charge_online_flag(void) { return vbus; }
int rdx_app_business_started(void) { return started; }
u32 usb_otg_online(int n) { return role; }
int app_in_mode(int m) { return m==mode; }
void cpu_reset(void) { ++reset_count; }
void p33_io_wakeup_edge(int io,int e) { edge=e; }
void app_send_message(int msg,int arg) {
    if(msg==APP_MSG_REQUEST_POWEROFF) ++off_requests;
    if(msg==APP_MSG_GOTO_MODE) { ++mode_requests; target_mode=arg; }
}
int rdx_storage_lifecycle_service(int a,int b) {
    ++service_calls; storage_changed=0;
    if(wake_inside_service) { wake_inside_service=0; rdx_dip_switch_wake(); }
    return storage_wait;
}
void gpio_set_mode(int io,int mode) {}
void local_irq_disable(void) {}
void local_irq_enable(void) {}
u32 sys_timer_get_ms(void) { return now; }
u16 sys_timer_add(void *p,void (*fn)(void*),int ms) { ++periodic_adds; return 1; }
u16 sys_timeout_add(void *p,void (*fn)(void*),u32 ms) {
    ++timeout_adds; timer_ms=ms;
    if(timer_fail) return 0;
    timer_cb=fn; timer_priv=p; timer_due=now+ms; timer_id=++next_id; return timer_id;
}
void sys_timeout_del(u16 id) { if(id==timer_id) timer_id=0; }
u16 usr_timer_add(void *p,void (*fn)(void*),int ms,int priority) {
    ++user_adds; if(priority) bad_priority=1; return user_adds;
}
int os_taskq_post_type(const char *task,int type,int n,int *args) {
    ++posts; if(post_fail || queued) return -1; queued=1; return 0;
}
"""
tests = r"""
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
static void reset(void) {
    on=1; vbus=role=started=0; mode=APP_MODE_BT;
    reset_count=off_requests=service_calls=worker_polls=usb_calls=0;
    dut_need=cdc_need=hid_need=led_need=storage_changed=storage_wait=0;
    posts=queued=post_fail=timer_fail=timeout_adds=periodic_adds=user_adds=bad_priority=0;
    mode_requests=target_mode=wake_inside_service=0;
    now=timer_due=timer_ms=usb_delay=0; timer_id=next_id=0;
    s_init_done=s_business_mode_entered=0;
    s_shutdown_pending=s_irq_pending=s_debounce_restart=0;
    s_stable_on=s_last_on=s_sample_on=-1; s_last_usb=(u32)-1; s_last_vbus=-1;
    s_sample_since=s_shutdown_since=s_retry_due=0; s_attempts=s_retry_wait=0;
    s_work_pending=s_work_posted=s_work_failed=0;
    s_work_timer=0; s_work_serial=s_work_due=0;
    app_var.goto_poweroff_flag=0;
}
static void dispatch(void) {
    if(queued) { queued=0; rdx_dip_switch_event(0); }
}
static void timeout(void) {
    if(timer_id && (int)(now-timer_due)>=0) {
        void *p=timer_priv; timer_id=0; timer_cb(p);
    }
}
static void tick(u32 ms) {
    now+=ms; rdx_dip_switch_guard(0); rdx_dip_switch_recovery_tick(0);
    dispatch(); timeout();
}
static void boot(void) { rdx_dip_switch_init(); tick(100); tick(100); }
int test_stable_no_system_poll(void) {
    reset(); boot(); CHECK(!periodic_adds && user_adds==2 && !bad_priority);
    CHECK(rdx_dip_switch_idle() && !timer_id && !s_work_failed && edge==RISING_EDGE);
    int old_posts=posts, old_services=service_calls;
    for(int i=0;i<1000;++i) tick(100);
    CHECK(posts==old_posts && service_calls==old_services && !timeout_adds);
    vbus=role=1; CHECK(!rdx_dip_switch_idle()); tick(100);
    old_posts=posts; old_services=service_calls;
    for(int i=0;i<100;++i) tick(100);
    CHECK(posts==old_posts && service_calls==old_services && !timer_id);
    return 0;
}
int test_debounce_and_edges(void) {
    reset(); boot(); on=0; rdx_dip_switch_p33_irq(RISING_EDGE);
    CHECK(!rdx_dip_switch_idle()); tick(20); CHECK(!off_requests);
    on=1; rdx_dip_switch_p33_irq(FALLING_EDGE); tick(20); tick(99);
    CHECK(!off_requests); tick(1); CHECK(rdx_dip_switch_idle());
    on=0; rdx_dip_switch_p33_irq(RISING_EDGE); tick(100);
    for(int i=0;i<20;++i) rdx_dip_switch_guard(0);
    CHECK(!s_shutdown_pending); tick(99); CHECK(!s_shutdown_pending);
    tick(1); CHECK(s_shutdown_pending && off_requests==1);
    return 0;
}
int test_stalled_app_core(void) {
    reset(); boot(); rdx_dip_switch_note_business_mode();
    on=0; rdx_dip_switch_p33_irq(1);
    now+=100; rdx_dip_switch_guard(0); CHECK(!s_shutdown_pending);
    now+=100; rdx_dip_switch_guard(0); CHECK(s_shutdown_pending);
    u32 accepted=now; on=1; post_fail=1;
    for(int i=0;i<29;++i) {
        now+=100; rdx_dip_switch_shutdown_begin();
        rdx_dip_switch_guard(0); rdx_dip_switch_recovery_tick(0);
    }
    CHECK(!reset_count && s_shutdown_since==accepted && !off_requests);
    now+=100; rdx_dip_switch_guard(0); CHECK(reset_count==1);
    return 0;
}
int test_native_off_priority(void) {
    reset(); boot(); started=1; rdx_dip_switch_note_business_mode();
    on=0; vbus=1; rdx_dip_switch_p33_irq(1); tick(100);
    worker_polls=service_calls=usb_calls=0;
    tick(100); CHECK(off_requests==1 && s_shutdown_pending);
    CHECK(!service_calls && !worker_polls && !usb_calls);
    on=1; vbus=0; tick(100); CHECK(off_requests>=2 && !worker_polls);
    int old=off_requests; app_var.goto_poweroff_flag=1; tick(100);
    CHECK(off_requests==old && !timer_id && !worker_polls);
    return 0;
}
int test_cold_charge_and_battery(void) {
    reset(); on=0; vbus=1; mode=APP_MODE_IDLE; boot();
    CHECK(edge==FALLING_EDGE && rdx_dip_switch_idle());
    int old=posts;
    for(int i=0;i<100;++i) tick(100);
    CHECK(!reset_count && !off_requests && !s_shutdown_pending && posts==old && !timer_id);
    vbus=0; tick(100); CHECK(off_requests==1 && s_shutdown_pending);
    return 0;
}
int test_cold_on_transition(void) {
    reset(); on=0; vbus=1; mode=APP_MODE_IDLE; boot();
    on=1; rdx_dip_switch_p33_irq(FALLING_EDGE); tick(100); CHECK(!mode_requests);
    tick(100); CHECK(mode_requests==1 && target_mode==APP_MODE_BT && edge==RISING_EDGE);
    mode=APP_MODE_BT; started=1; rdx_dip_switch_note_business_mode(); tick(100);
    CHECK(!timer_id);
    tick(1000); CHECK(!timer_id && !off_requests && !reset_count);
    return 0;
}
int test_bounded_mode_retry(void) {
    reset(); mode=APP_MODE_IDLE; boot(); CHECK(mode_requests==1 && timer_ms==1000);
    u32 due=s_retry_due;
    for(int i=0;i<9;++i) { rdx_dip_switch_wake(); tick(100); }
    CHECK(mode_requests==1 && s_retry_due==due);
    tick(100); CHECK(mode_requests==2);
    tick(1000); CHECK(mode_requests==3 && !timer_id);
    for(int i=0;i<10;++i) { rdx_dip_switch_wake(); tick(1000); }
    CHECK(mode_requests==3 && !timer_id);
    vbus=1; tick(100); CHECK(mode_requests==4);
    return 0;
}
int test_queue_failure_and_coalescing(void) {
    reset(); boot(); int old=posts;
    post_fail=1; rdx_dip_switch_wake(); rdx_dip_switch_wake(); tick(100);
    CHECK(posts==old+1 && s_work_pending && !s_work_posted && !rdx_dip_switch_idle());
    post_fail=0; rdx_dip_switch_recovery_tick(0); rdx_dip_switch_recovery_tick(0);
    CHECK(posts==old+2 && queued); dispatch(); CHECK(rdx_dip_switch_idle());
    wake_inside_service=1; rdx_dip_switch_wake(); tick(100);
    CHECK(s_work_pending); tick(100); CHECK(!s_work_pending);
    return 0;
}
int test_timeout_failure_and_stale(void) {
    reset(); mode=APP_MODE_IDLE; timer_fail=1; boot();
    CHECK(mode_requests==1 && s_work_failed && !rdx_dip_switch_idle());
    tick(999); CHECK(mode_requests==1);
    timer_fail=0; tick(1); CHECK(mode_requests==2 && timer_id);
    void *stale=timer_priv;
    mode=APP_MODE_BT; rdx_dip_switch_wake(); tick(100);
    int calls=service_calls; rdx_dip_switch_timeout(stale);
    CHECK(service_calls==calls);
    tick(1000); CHECK(!timer_id && rdx_dip_switch_idle());
    return 0;
}
int test_wraparound(void) {
    reset(); now=0xffffff00u; boot(); on=0; rdx_dip_switch_p33_irq(1);
    tick(100); tick(100); CHECK(s_shutdown_pending);
    now+=2999; rdx_dip_switch_guard(0); CHECK(!reset_count);
    ++now; rdx_dip_switch_guard(0); CHECK(reset_count==1);
    reset(); now=0xfffffff0u; mode=APP_MODE_IDLE; boot();
    tick(999); CHECK(mode_requests==1); tick(1); CHECK(mode_requests==2);
    return 0;
}
int test_worker_recovery(void) {
    reset(); boot(); int old=posts;
    dut_need=cdc_need=hid_need=led_need=storage_changed=1;
    CHECK(!rdx_dip_switch_idle()); tick(100);
    CHECK(posts==old+1 && !dut_need && !cdc_need && !hid_need && !led_need && !storage_changed);
    CHECK(rdx_dip_switch_idle());
    storage_wait=1; rdx_dip_switch_wake(); tick(100); CHECK(timer_ms==100);
    storage_wait=0; tick(100); CHECK(!timer_id);
    usb_delay=300; rdx_dip_switch_wake(); tick(100); CHECK(timer_ms==300);
    usb_delay=0; tick(300); CHECK(!timer_id);
    return 0;
}
int test_pc_config(void) {
    reset(); on=0; vbus=role=1; mode=APP_MODE_IDLE; boot();
#if TCFG_T2620_PC_STORAGE_ENABLE
    CHECK(mode_requests==1 && target_mode==APP_MODE_PC && rdx_dip_switch_pc_allowed());
    tick(1000); tick(1000); CHECK(mode_requests==3 && !timer_id);
    role=0; tick(100); CHECK(!rdx_dip_switch_pc_allowed() && !timer_id);
    role=1; tick(100); CHECK(mode_requests==4);
#else
    CHECK(!mode_requests && !timer_id && !rdx_dip_switch_pc_allowed());
#endif
    return 0;
}
"""
path = ROOT / 'cache/power-recovery-tests/dip.c'
path.parent.mkdir(parents=True, exist_ok=True)
checks = ['test_stable_no_system_poll', 'test_debounce_and_edges', 'test_stalled_app_core',
          'test_native_off_priority', 'test_cold_charge_and_battery', 'test_cold_on_transition',
          'test_bounded_mode_retry', 'test_queue_failure_and_coalescing',
          'test_timeout_failure_and_stale', 'test_wraparound', 'test_worker_recovery', 'test_pc_config']
for pc in (0, 1):
    path.write_text(f'#define TCFG_T2620_PC_STORAGE_ENABLE {pc}\n' + stubs + source + tests, encoding='utf-8')
    run_c_checks(path, checks)
print('DIP event scheduling: 12 scenarios x MSC off/on passed; no idle system timer or empty app_core dispatch.')

# 同时执行实际 JL 电源入口与 idle 收尾，避免仅测拨码模块漏掉普通关机回归。
power = (ROOT / 'SDK/apps/earphone/mode/bt/poweroff.c').read_text(encoding='utf-8')
idle = (ROOT / 'SDK/apps/earphone/mode/idle/idle.c').read_text(encoding='utf-8')
native_stubs = r'''
#define log_info(...) ((void)0)
#define APP_MSG_POWER_OFF 7
#define USER_CTRL_POWER_OFF 8
enum poweroff_reason { TEST_NORMAL = 0 };
static int g_bt_detach_timer, softoff_count;
void wait_exit_btstack_flag(void *p) {}
void sys_auto_shut_down_disable(void) {}
void bt_stop_a2dp_slience_detect(void *p) {}
void bt_cmd_prepare(int cmd,int n,void *p) {}
void dac_power_off(void) {}
void dlog_flush2flash(int ms) {}
void usb_factory_shutdown(void) {}
void power_set_soft_poweroff(void) { ++softoff_count; }
'''
native_tests = r'''
int test_native_normal_off(void) {
    reset(); softoff_count=0; started=1; rdx_dip_switch_note_business_mode();
    sys_enter_soft_poweroff(POWEROFF_NORMAL);
    CHECK(app_var.goto_poweroff_flag && !rdx_dip_switch_shutdown_pending());
    app_idle_enter_softoff();
    CHECK(softoff_count==1 && !reset_count);
    return 0;
}
int test_native_dip_restart(void) {
    reset(); softoff_count=0; started=1; rdx_dip_switch_note_business_mode();
    on=0; rdx_dip_switch_guard(0); now+=100; rdx_dip_switch_guard(0);
    sys_enter_soft_poweroff(POWEROFF_NORMAL);
    CHECK(rdx_dip_switch_shutdown_pending());
    app_idle_enter_softoff(); CHECK(softoff_count==1 && !reset_count);
    on=1; app_idle_enter_softoff(); CHECK(reset_count>0);
    return 0;
}
'''
path = path.with_name('native_power.c')
path.write_text(stubs + source + tests + native_stubs +
                function(power, 'sys_enter_soft_poweroff') +
                function(idle, 'app_idle_enter_softoff') + native_tests, encoding='utf-8')
run_c_checks(path, ['test_native_normal_off', 'test_native_dip_restart'])
print('Native power: normal ON shutdown and accepted DIP OFF/ON remain distinct.')

storage = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_storage_lifecycle.c').read_text(encoding='utf-8')
storage = re.sub(r'^#include.*\n', '', storage, flags=re.M)
storage_stubs = r'''
typedef unsigned char u8;
#define TCFG_DIP_SWITCH_POWER_ENABLE 1
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1
#define r_printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int format, refresh, error, business, active, wakes, invalidates, dut_wakes, restores;
int rdx_record_format_service_status(void) { return format; }
int rdx_uxfile_pc_refresh_status(void) { return refresh; }
int rdx_uxfile_storage_status(void) { return error; }
int rdx_app_business_started(void) { return business; }
int rdx_dut_test_keys_active(void) { return active; }
void rdx_dut_service_wake(void) { ++dut_wakes; }
void rdx_hogp_input_invalidate(void) { ++invalidates; }
void rdx_uxfile_pc_returned(void) { refresh=1; }
void rdx_dip_switch_wake(void) { ++wakes; }
void rdx_playback_refresh_playlist(void) { ++restores; }
void rdx_ble_server_adv_enable(int en) { ++restores; }
void rdx_led_ctrl_restore_system_state(void) { ++restores; }
'''
storage_tests = r'''
int test_storage_event_reconciliation(void) {
    format=refresh=error=business=active=wakes=invalidates=dut_wakes=restores=0;
    s_refresh_previous=s_pc_refresh_pending=s_business_blocked_previous=0;
    CHECK(!rdx_storage_lifecycle_changed() && !rdx_storage_lifecycle_service(1,0));
    active=1; format=1; CHECK(rdx_storage_lifecycle_changed());
    CHECK(!rdx_storage_lifecycle_service(1,0) && dut_wakes==1);
    CHECK(!rdx_storage_lifecycle_changed());
    format=0; CHECK(rdx_storage_lifecycle_changed());
    rdx_storage_lifecycle_service(1,0); CHECK(dut_wakes==2);
    rdx_storage_lifecycle_pc_returned(); CHECK(wakes==1 && invalidates==1);
    CHECK(rdx_storage_lifecycle_service(1,0)==1 && !restores);
    refresh=-1; CHECK(rdx_storage_lifecycle_changed());
    CHECK(!rdx_storage_lifecycle_service(1,0) && !s_pc_refresh_pending);
    CHECK(rdx_storage_lifecycle_business_blocked() && !restores);
    CHECK(!rdx_storage_lifecycle_changed());
    rdx_storage_lifecycle_pc_returned(); refresh=0;
    CHECK(rdx_storage_lifecycle_service(1,0)==1);
    business=1; CHECK(!rdx_storage_lifecycle_service(1,0));
    CHECK(restores==3 && !s_pc_refresh_pending);
    CHECK(!rdx_storage_lifecycle_service(1,0) && restores==3);
    return 0;
}
'''
path = path.with_name('storage_lifecycle.c')
path.write_text(storage_stubs + storage + storage_tests, encoding='utf-8')
run_c_checks(path, ['test_storage_event_reconciliation'])
print('Storage lifecycle: state-change wake, pending reconciliation and failed-refresh stop with isolation passed.')

# 验证旧库 ABI 兼容入口，测试环境不提供 GPIO、SD 或定时器接口。
app = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c').read_text(encoding='utf-8')
storage_stubs = r'''
typedef unsigned char u8;
#define RDX_SHARED_VDD_WAKE_BUSINESS 3
#define r_printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int wake_calls, idle_notifications, wake_error, wake_reason;
int rdx_peripheral_power_vdd_ensure_on(int reason) {
    ++wake_calls; wake_reason = reason; return wake_error;
}
void rdx_peripheral_power_vdd_business_changed_notify(void) {
    ++idle_notifications;
}
'''
storage_tests = r'''
int test_legacy_storage_delegation(void) {
    wake_calls=idle_notifications=wake_error=0;
    rdx_app_emmc_poweron(0);
    CHECK(wake_calls==1 && wake_reason==RDX_SHARED_VDD_WAKE_BUSINESS);
    rdx_app_emmc_poweron(1);
    CHECK(wake_calls==2 && !idle_notifications);
    rdx_app_emmc_poweroff();
    rdx_app_emmc_poweroff_check();
    CHECK(wake_calls==2 && idle_notifications==2);
    rdx_app_emmc_poweroff_check_timer_stop();
    CHECK(wake_calls==2 && idle_notifications==2);
    /* 恢复失败不能产生额外的“已上电”影子状态。 */
    wake_error=-1; rdx_app_emmc_poweron(1);
    CHECK(wake_calls==3 && idle_notifications==2);
    wake_error=0; rdx_app_emmc_poweron(0);
    CHECK(wake_calls==4 && idle_notifications==2);
    return 0;
}
'''
path = path.with_name('storage_power.c')
path.write_text(storage_stubs + ''.join(function(app, name) for name in (
    'rdx_app_emmc_poweron', 'rdx_app_emmc_poweroff',
    'rdx_app_emmc_poweroff_check_timer_stop', 'rdx_app_emmc_poweroff_check',
)) + storage_tests, encoding='utf-8')
run_c_checks(path, ['test_legacy_storage_delegation'])
print('Storage power: legacy ABI delegates wake/idle to PA4 manager, without MIC GPIO or a second timer/state.')
