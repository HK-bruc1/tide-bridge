"""使用模拟引脚运行实际拨码控制代码，验证 app_core 停滞时的行为。"""
import re
from host_c_test_lib import ROOT, function, run_c_checks

source = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_dip_switch.c').read_text(encoding='utf-8')
source = re.sub(r'^#include.*\n', '', source, flags=re.M)
stubs = r"""
#define TCFG_DIP_SWITCH_POWER_ENABLE 1
#define TCFG_T2620_PC_STORAGE_ENABLE 0
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 0
#define TCFG_DIP_SWITCH_POWER_IO 1
#define IO_PORT_SPILT(x) x
#define PORT_INPUT_PULLUP_10K 0
#define REGISTER_LP_TARGET(x) static struct { const char *name; unsigned char (*is_idle)(void); } x
#define ASSERT(x) ((void)0)
#define RISING_EDGE 1
#define FALLING_EDGE 0
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
static int on=1, vbus, mode=APP_MODE_BT, started, reset_count, off_requests, service_calls, edge;
static struct { int goto_poweroff_flag, goto_poweroff_cnt; } app_var;
int get_power_on_status(void) { return on; }
int get_charge_online_flag(void) { return vbus; }
int rdx_app_business_started(void) { return started; }
u32 usb_otg_online(int n) { return 0; }
int app_in_mode(int m) { return m==mode; }
void cpu_reset(void) { ++reset_count; }
void p33_io_wakeup_edge(int io,int e) { edge=e; }
void app_send_message(int msg,int arg) { if(msg==APP_MSG_REQUEST_POWEROFF) ++off_requests; }
int rdx_storage_lifecycle_service(int a,int b) { ++service_calls; return 1; }
void gpio_set_mode(int io,int mode) {}
u16 sys_timer_add(void *p,void (*fn)(void*),int ms) { return 1; }
u16 usr_timer_add(void *p,void (*fn)(void*),int ms,int priority) { return 2; }
"""
tests = r"""
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
static void reset(void) {
    on=1; vbus=0; started=0; reset_count=off_requests=service_calls=0;
    s_init_done=s_business_mode_entered=0;
    s_shutdown_ticks=s_guard_off_samples=s_shutdown_pending=s_irq_pending=0;
    s_last_on=s_sample_on=-1; s_attempts=s_retry_ticks=0;
    app_var.goto_poweroff_flag=0;
}
int test_stalled_app_core(void) {
    reset(); rdx_dip_switch_note_business_mode();
    CHECK(rdx_dip_switch_idle());
    on=0; rdx_dip_switch_p33_irq(1); CHECK(!rdx_dip_switch_idle());
    rdx_dip_switch_guard(0); CHECK(!s_shutdown_pending);
    on=1; rdx_dip_switch_guard(0); CHECK(!s_shutdown_pending);
    on=0; rdx_dip_switch_guard(0); rdx_dip_switch_guard(0);
    CHECK(s_shutdown_pending && !reset_count);
    /* 完全不调度 app_core 回调；ON 不得取消已接受的退出流程。 */
    on=1;
    for(int i=0;i<28;++i) { rdx_dip_switch_shutdown_begin(); rdx_dip_switch_guard(0); }
    CHECK(!reset_count);
    rdx_dip_switch_guard(0); CHECK(reset_count==1);
    return 0;
}
int test_native_off_priority(void) {
    reset(); started=1; rdx_dip_switch_note_business_mode();
    on=0; vbus=1;
    rdx_dip_switch_deferred_handle(0); CHECK(!off_requests);
    rdx_dip_switch_deferred_handle(0);
    CHECK(off_requests==1 && s_shutdown_pending && !service_calls);
    /* USB 或 ON 状态变化不得重新进入已部分销毁的运行时。 */
    on=1; vbus=0; rdx_dip_switch_deferred_handle(0);
    rdx_dip_switch_deferred_handle(0); CHECK(off_requests==2 && !service_calls);
    app_var.goto_poweroff_flag=1;
    rdx_dip_switch_deferred_handle(0); CHECK(off_requests==2);
    return 0;
}
int test_cold_charge_and_battery(void) {
    reset(); on=0; vbus=1; mode=APP_MODE_IDLE;
    for(int i=0;i<100;++i) { rdx_dip_switch_guard(0); rdx_dip_switch_deferred_handle(0); }
    CHECK(!reset_count && !off_requests && !s_shutdown_pending);
    vbus=0; rdx_dip_switch_deferred_handle(0);
    CHECK(off_requests==1 && s_shutdown_pending);
    return 0;
}
"""
path = ROOT / 'cache/power-recovery-tests/dip.c'
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(stubs + source + tests, encoding='utf-8')
run_c_checks(path, ['test_stalled_app_core', 'test_native_off_priority', 'test_cold_charge_and_battery'])
print('DIP recovery: debounce, stuck app_core, fixed deadline, OFF/ON latch and cold USB/battery passed.')

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
    on=0; rdx_dip_switch_guard(0); rdx_dip_switch_guard(0);
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
