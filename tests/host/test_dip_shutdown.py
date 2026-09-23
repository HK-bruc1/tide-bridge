"""联合执行拨码采样与存储关机流程，模拟 SDK 接口及工作任务交互。"""
import re
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, function, run_c_checks


STUBS = r'''
#include <stdbool.h>
#include <stddef.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
typedef int P33_IO_WKUP_EDGE;
#define TCFG_DIP_SWITCH_POWER_ENABLE 1
#define TCFG_T2620_PC_STORAGE_ENABLE 1
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 0
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 0
#define RDX_WIFI_ENABLE 0
#define TCFG_DIP_SWITCH_POWER_IO 1
#define PORT_INPUT_PULLUP_10K 1
#define IO_PORT_SPILT(x) x
#define RISING_EDGE 1
#define FALLING_EDGE 0
#define SLAVE_MODE 1
#define APP_MODE_BT 1
#define APP_MODE_PC 2
#define APP_MODE_IDLE 3
#define IDLE_MODE_CHARGE 1
#define APP_MSG_REQUEST_POWEROFF 1
#define APP_MSG_GOTO_MODE 2
enum poweroff_reason { POWEROFF_NORMAL, POWEROFF_LOW_BAT };
#define APP_MSG_POWER_OFF 3
#define USER_CTRL_POWER_OFF 0
#define log_info(...) ((void)0)
#define DUT_LOG(...) ((void)0)
#define MEM_FORMAT_RESULT_OK 1
#define RDX_LED_SCENE_FINALPACK_DONE 3
#define RDX_LED_SCENE_USB_SWITCH_FAILED 2
#define RDX_LED_SCENE_USB_SWITCH_WAIT 1
#define TRANSFER_BY_WIFI_OFF 0
#define r_printf(...) ((void)0)
#define ASSERT(x) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static struct { int goto_poweroff_flag, goto_poweroff_cnt; } app_var;
static int on, vbus, usb, mode, started, ready, ota, formatting;
static struct { int dut_mode; } rdx_dut_info;
static bool g_finalpack_end_pending, g_finalpack_format_waiting, g_finalpack_committed;
static int commit_error, low_battery, native_poweroffs, g_bt_detach_timer;
int get_vbat_need_shutdown(void) { return low_battery; }
void bt_stop_a2dp_slience_detect(void *arg) {}
void bt_cmd_prepare(int command, int arg, void *data) { ++native_poweroffs; }
void wait_exit_btstack_flag(void *arg) {}
int rdx_vm_reset_defaults_no_poweroff(void) { return commit_error == 1; }
int rdx_vm_set_bound_status(int value, int reason) { return commit_error == 2; }
static int rdx_dut_key_dut_store(bool disabled) { return commit_error == 3; }
void rdx_ble_server_auto_shut_down_enable(u8 enable) {}
static int refresh, storage, format_status, record_result, file_result, ble_idle;
static int record_error, file_error, record_posts, file_posts, resets;
static int poweroff_posts, mode_posts, led;
static u32 now;
int rdx_dip_switch_cold_service(void);
int get_power_on_status(void) { return on; }
int get_charge_online_flag(void) { return vbus; }
u32 usb_otg_online(int port) { return usb; }
int app_in_mode(int value) { return mode == value; }
int rdx_app_business_started(void) { return started; }
int rdx_app_business_ready(void) { return ready; }
u8 get_ota_status(void) { return ota; }
int rdx_uxfile_is_formatting(void) { return formatting; }
int rdx_record_format_status(void) { return format_status; }
int rdx_uxfile_storage_status(void) { return storage; }
int rdx_uxfile_pc_refresh_status(void) { return refresh; }
void rdx_uxfile_pc_returned(void) {}
void rdx_hogp_input_invalidate(void) {}
void rdx_hogp_key_action_reset(void) {}
void rdx_led_ctrl_restore_system_state(void) {}
void rdx_led_ctrl_set_scene(int scene) { led = scene; }
void rdx_ble_server_adv_enable(int enable) {}
void rdx_app_emmc_poweroff_check_timer_stop(void) {}
void rdx_app_wifi_handle(u8 cmd) {}
void sys_auto_shut_down_disable(void) {}
u32 jiffies_msec(void) { return now; }
int rdx_record_usb_quiesce_request(u32 ticket) { ++record_posts; return record_error; }
int rdx_record_usb_quiesce_poll(u32 ticket) { return record_result; }
int rdx_ble_server_usb_quiesce(void) { return ble_idle; }
int rdx_uxfile_fence_request(u32 ticket) { ++file_posts; return file_error; }
int rdx_uxfile_fence_poll(u32 ticket) { return file_result; }
void rdx_cpu_reset(void) { ++resets; }
void app_send_message(int msg, int arg) {
    if (msg == APP_MSG_REQUEST_POWEROFF) ++poweroff_posts;
    if (msg == APP_MSG_GOTO_MODE) ++mode_posts;
}
void p33_io_wakeup_edge(int io, int edge) {}
void gpio_set_mode(int io, int value) {}
u16 sys_timer_add(void *arg, void (*cb)(void *), int ms) { return 1; }
'''

CHECKS = r'''
static void reset(void) {
    app_var.goto_poweroff_flag = 0;
    on = vbus = usb = ota = formatting = 0;
    rdx_dut_info.dut_mode = 0;
    g_finalpack_end_pending = g_finalpack_format_waiting = g_finalpack_committed = false;
    commit_error = low_battery = native_poweroffs = 0;
    refresh = storage = format_status = 0;
    record_result = file_result = -2;
    ble_idle = record_error = file_error = 0;
    record_posts = file_posts = resets = poweroff_posts = mode_posts = led = 0;
    now = 0;
    mode = APP_MODE_BT; started = ready = 1;
    s_init_done = false; s_business_mode_entered = true;
    s_last_on = s_sample_on = -1; s_last_usb = (u32)-1; s_last_vbus = -1;
    s_attempts = s_retry_ticks = 0;
    s_usb_switch = USB_SWITCH_IDLE; s_wait_busy = 0;
    s_switch_deadline = 0; s_refresh_previous = s_pc_refresh_pending = 0;
}
static void tick(int value) {
    on = value; now += 100;
    rdx_dip_switch_deferred_handle(NULL);
}
int check_battery_and_usb_shutdown(void) {
    for (int powered = 0; powered < 2; ++powered) {
        reset(); vbus = powered;
        tick(1); tick(1); tick(0);
        CHECK(!record_posts && !poweroff_posts);
        tick(1); tick(1); /* 忽略短暂的 OFF 脉冲。 */
        CHECK(!record_posts);
        tick(0); tick(0);
        CHECK(record_posts == 1 && rdx_storage_lifecycle_business_blocked());
        CHECK(!file_posts && !resets && !poweroff_posts);
        for (int i = 0; i < 20; ++i) tick(i & 1);
        CHECK(record_posts == 1 && !file_posts && !resets);
        record_result = 0; tick(0);
        CHECK(!file_posts); /* BLE 仍有未完成的工作。 */
        ble_idle = 1; tick(1);
        CHECK(file_posts == 1 && !resets);
        tick(0); CHECK(!resets);
        file_result = 0; tick(1);
        CHECK(resets == 1 && !poweroff_posts && !mode_posts);
    }
    return 0;
}
int check_init_and_cold_off(void) {
    reset(); started = ready = 0;
    for (int i = 0; i < 30; ++i) tick(0);
    CHECK(!record_posts && !poweroff_posts && !mode_posts);
    started = 1; tick(0);
    CHECK(!record_posts && !poweroff_posts);
    ready = 1; tick(0);
    CHECK(record_posts == 1 && !poweroff_posts);
    reset(); started = 0; s_business_mode_entered = false; mode = APP_MODE_IDLE;
    tick(0); tick(0);
    CHECK(poweroff_posts == 1 && !record_posts && !resets);
    return 0;
}
int check_busy_wait_and_cancel(void) {
    for (int busy = 0; busy < 3; ++busy) {
        reset(); ota = busy == 0; formatting = busy == 1; rdx_dut_info.dut_mode = busy == 2;
        tick(0); tick(0);
        CHECK(rdx_storage_lifecycle_shutdown_deferred() && !record_posts);
        tick(1); /* 单次 ON 采样不能取消忙等待。 */
        CHECK(rdx_storage_lifecycle_shutdown_deferred());
        tick(1);
        CHECK(!rdx_storage_lifecycle_shutdown_deferred() && !record_posts);
        tick(0); tick(0); ota = formatting = rdx_dut_info.dut_mode = 0; tick(0);
        CHECK(record_posts == 1 && !poweroff_posts);
    }
    return 0;
}
int check_failure_and_chatter_deadline(void) {
    for (int failure = 0; failure < 6; ++failure) {
        reset(); now = 0xfffff000u; /* 超时期限跨越时钟计数回绕点。 */
        if (failure == 0) record_error = -1;
        tick(0); tick(0);
        if (failure == 1) record_result = -1;
        if (failure == 2) storage = -1;
        if (failure == 3 || failure == 4) {
            record_result = 0; ble_idle = 1;
            if (failure == 3) file_error = -1;
            else file_result = -1;
        }
        /* 即使每次回调都发生电平变化，也必须推进状态切换或进入失败状态。 */
        for (int i = 0; i < 305; ++i) tick((i + 1) & 1);
        CHECK(led == RDX_LED_SCENE_USB_SWITCH_FAILED);
        CHECK(rdx_storage_lifecycle_shutdown_deferred());
        CHECK(!resets && !poweroff_posts && !mode_posts && record_posts == 1);
    }
    return 0;
}

int check_independent_poweroff_entry(void) {
    /* USB 拔出请求可先于拨码采样，也可发生在初始化等待或收尾中。 */
    reset(); started = ready = 0;
    sys_enter_soft_poweroff(POWEROFF_NORMAL);
    CHECK(!native_poweroffs && !app_var.goto_poweroff_flag);
    tick(0); tick(0);
    CHECK(rdx_storage_lifecycle_shutdown_deferred());
    sys_enter_soft_poweroff(POWEROFF_NORMAL);
    CHECK(!native_poweroffs);
    started = ready = 1; tick(0);
    sys_enter_soft_poweroff(POWEROFF_NORMAL);
    CHECK(record_posts == 1 && !native_poweroffs);
    /* 低电压紧急保护、无业务冷关机及 ON 下的普通关机不受误拦截。 */
    low_battery = 1; sys_enter_soft_poweroff(POWEROFF_NORMAL);
    CHECK(native_poweroffs == 1);
    reset(); started = ready = 0; s_business_mode_entered = false;
    sys_enter_soft_poweroff(POWEROFF_NORMAL); CHECK(native_poweroffs == 1);
    reset(); on = 1;
    sys_enter_soft_poweroff(POWEROFF_NORMAL); CHECK(native_poweroffs == 1);
    reset(); sys_enter_soft_poweroff(POWEROFF_LOW_BAT); CHECK(native_poweroffs == 1);
    /* 初始化等待可由稳定 ON 取消，单次抖动不能取消。 */
    reset(); ready = 0; tick(0); tick(0); tick(1);
    CHECK(rdx_storage_lifecycle_shutdown_deferred());
    tick(1); CHECK(!rdx_storage_lifecycle_shutdown_deferred());
    return 0;
}

int check_finalpack_to_shutdown(void) {
    for (int fault = 0; fault < 5; ++fault) {
        reset(); rdx_dut_info.dut_mode = 1; g_finalpack_end_pending = true;
        g_finalpack_format_waiting = true;
        tick(0); tick(0); CHECK(!record_posts);
        /* 真正执行封箱提交函数；格式化或任一持久化失败均不能放行。 */
        commit_error = fault;
        g_finalpack_format_waiting = false;
        rdx_dut_finalpack_commit(fault == 4 ? 0 : MEM_FORMAT_RESULT_OK);
        tick(0);
        if (fault) {
            CHECK(!record_posts && rdx_dut_shutdown_busy());
        } else {
            CHECK(rdx_dut_info.dut_mode && !rdx_dut_shutdown_busy());
            CHECK(record_posts == 1 && !resets);
            record_result = 0; ble_idle = 1; tick(0);
            CHECK(file_posts == 1 && !resets);
            file_result = 0; tick(0);
            CHECK(resets == 1 && !native_poweroffs);
        }
    }
    return 0;
}
'''


def main():
    directory = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    dut = (directory / 'rdx_dut.c').read_text(encoding='utf-8-sig')
    sources = [function(dut, name) for name in (
        'bool rdx_dut_shutdown_busy(', 'static void rdx_dut_finalpack_fail(',
        'static void rdx_dut_finalpack_commit(')]
    for name in ('rdx_storage_lifecycle.c', 'rdx_dip_switch.c'):
        source = (directory / name).read_text(encoding='utf-8-sig')
        sources.append(re.sub(r'^#include[^\n]*\n', '', source, flags=re.M))
    power = (ROOT / 'SDK/apps/earphone/mode/bt/poweroff.c').read_text(encoding='utf-8-sig')
    sources.append(function(power, 'void sys_enter_soft_poweroff('))
    with tempfile.TemporaryDirectory(prefix='dip-shutdown-') as tmp:
        path = Path(tmp) / 'checks.c'
        path.write_text(STUBS + '\n'.join(sources) + CHECKS, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (check_\w+)\(void\)', CHECKS))
    print('PASS: DIP shutdown fences, debounce, startup, chatter and failures')


if __name__ == '__main__':
    main()
