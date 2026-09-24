"""Execute packaging commit/failure paths with mocked flash, format and timers."""
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, function, run_c_checks

STUBS = r'''
void rdx_hogp_input_invalidate(void) {}
typedef unsigned char u8;
typedef unsigned int u32;
typedef int bool;
#define true 1
#define false 0
#define NULL ((void *)0)
#define DUT_LOG(...) ((void)0)
#define g_printf(...) ((void)0)
#define r_printf(...) ((void)0)
#define KEY_DUT_DISABLED_FLAG 0xaa
#define VM_RDX_KEY_DUT_DISABLED 0
#define VM_RDX_NOTTA_BOUND_STATUS 1
#define MEM_FORMAT_RESULT_OK 1
#define RDX_BOUND_STATE_UNBOUND 0
#define RDX_BOUND_STATE_BOUND 1
#define RECORD_STATE_START 1
#define RECORD_STATE_RESUME 2
#define DUT_FUNC_NONE 0
#define Q_CALLBACK 0
#define RDX_LED_SCENE_FINALPACK_DONE 42
#define RDX_LED_SCENE_DUT_ENTER 41
int done_led, auto_off;
void rdx_led_ctrl_set_scene(int scene) { done_led = scene; }
#define TCFG_AUTO_SHUT_DOWN_TIME 1
#define TCFG_DIP_SWITCH_POWER_ENABLE 1
#define RECORD_STATE_STOP 0
#define y_printf(...) ((void)0)
#define log_info(...) ((void)0)
#define VM_RDX_AUTO_OFF_TIME 2
#define RDX_DEFAULT_SHUT_DOWN_TIME 5
typedef struct { u32 auto_off_time; } AUTO_OFF_TIME_CONFIG;
u32 stored_auto_off;
int rdx_app_get_dut_status(void);
void sys_auto_shut_down_enable(void) { auto_off=!rdx_app_get_dut_status(); }
void sys_auto_shut_down_disable(void) { auto_off=0; }
int bt_get_total_connect_dev(void) { return 0; }
struct { int ble_conn; } g_rdx_ble_server_info;
void rdx_ble_server_auto_shut_down_enable(u8 enable);
typedef struct { int run; } RecordStatus;
typedef struct { int file_send_busy; } ReqFileInfo;
RecordStatus record;
ReqFileInfo files;
struct { int dut_mode, current_func, key_dut_disabled; } rdx_dut_info;
struct { int goto_poweroff_flag; int auto_off_time; } app_var;
struct { u8 bound_state; } rdx_bound_info;
u32 bound_epoch, bound_token;
bool g_finalpack_end_pending, g_finalpack_format_waiting;
#define FT_DUT "ft_dut"
int replies, reset_requests;
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
void rdx_protocol_custom_msg_indicate(const char *cmd, char *value) { ++replies; }
void rdx_app_time_to_reset(void) { ++reset_requests; }
int write_fail, read_fail, mismatch, fail_id, resets_fail, busy, posts_fail, timer_fail;
int formats, resets, revokes, timers, disconnects, poweroffs, posts;
u8 flash[2];
void (*timer_cb)(void *);
int syscfg_write(int id, const void *p, int n) {
    if (id == fail_id && write_fail) return -1;
    if (id == VM_RDX_AUTO_OFF_TIME) { stored_auto_off=*(const u32 *)p; return n; }
    flash[id] = *(const u8 *)p; return n;
}
int syscfg_read(int id, void *p, int n) {
    if (id == fail_id && read_fail) return -1;
    if (id == VM_RDX_AUTO_OFF_TIME) { *(u32 *)p=stored_auto_off; return n; }
    *(u8 *)p = flash[id] ^ (id == fail_id && mismatch); return n;
}
int rdx_vm_bound_transition_lock(void) { return 0; }
void rdx_vm_bound_transition_unlock(void) {}
void rdx_record_binding_revoke(void) { ++revokes; }
int rdx_vm_reset_defaults_no_poweroff(void) { ++resets; return resets_fail; }
void rdx_dut_poweroff(void) { ++poweroffs; }
void rdx_ble_server_app_disconnect(void) { ++disconnects; }
int sys_timeout_add(void *p, void (*cb)(void *), int ms) {
    if (timer_fail) return 0;
    ++timers; timer_cb=cb; return 1;
}
int os_taskq_post_type(const char *task, int type, int n, int *msg) {
    ++posts; return posts_fail;
}
RecordStatus *rdx_record_get_status(void) { return &record; }
bool rdx_app_get_dut_status(void) { return rdx_dut_info.dut_mode; }
ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) { return &files; }
int get_ota_status(void) { return busy; }
int client_file_is_transfer_in_progress(void) { return busy; }
int rdx_uxfile_sd_format_status_check(void) { return busy; }
int rdx_storage_lifecycle_business_blocked(void) { return busy; }
int rdx_storage_lifecycle_shutdown_deferred(void) { return busy; }
void rdx_uxfile_device_sd_format(void (*cb)(u8)) { ++formats; }
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
'''

TESTS = r'''
static void boot(void) {
    write_fail=read_fail=mismatch=resets_fail=busy=posts_fail=timer_fail=0;
    formats=resets=revokes=timers=disconnects=poweroffs=posts=0;
    done_led=0; auto_off=1; replies=reset_requests=0;
    fail_id=0; flash[0]=0xff; flash[1]=1;
    rdx_bound_info.bound_state=1; bound_epoch=bound_token=1;
    rdx_dut_info.dut_mode=1; rdx_dut_info.current_func=0;
    rdx_dut_info.key_dut_disabled=0; record.run=0; files.file_send_busy=0;
    app_var.goto_poweroff_flag=0;
    g_finalpack_end_pending=g_finalpack_format_waiting=0; timer_cb=NULL;
}
static void finish(int result) {
    rdx_dut_finalpack_end_format_cb(result);
    if (!posts_fail) rdx_dut_finalpack_commit(result);
}
int test_finalpack_success(void) {
    boot(); rdx_dut_finalpack_end();
    CHECK(formats==1 && timers==0 && resets==0 && flash[1]==1 && flash[0]==0xff);
    CHECK(done_led==RDX_LED_SCENE_DUT_ENTER);
    CHECK(rdx_dut_finalpack_mode_ack(FT_DUT, "0") && replies==1);
    CHECK(g_finalpack_end_pending && done_led==RDX_LED_SCENE_DUT_ENTER);
    rdx_dut_finalpack_end(); CHECK(formats==1);
    finish(MEM_FORMAT_RESULT_OK);
    CHECK(flash[1]==0 && rdx_bound_info.bound_state==0 && bound_token==0 && revokes==1);
    CHECK(flash[0]==0xaa && rdx_dut_info.key_dut_disabled && timers==0 && g_finalpack_end_pending);
    rdx_dut_finalpack_end_format_cb(1); CHECK(posts==1);
    CHECK(done_led==RDX_LED_SCENE_FINALPACK_DONE && !auto_off);
    CHECK(!disconnects && !poweroffs && !timers && !reset_requests);
    CHECK(rdx_dut_finalpack_mode_ack(FT_DUT, "0"));
    CHECK(rdx_dut_finalpack_mode_ack(FT_DUT, "1") && replies==3);
    CHECK(!rdx_dut_finalpack_mode_ack("ft_oled", "1"));
    CHECK(!rdx_dut_finalpack_mode_ack(FT_DUT, "2"));
    CHECK(done_led==RDX_LED_SCENE_FINALPACK_DONE && g_finalpack_end_pending);
    rdx_ble_server_auto_shut_down_enable(1); CHECK(!auto_off);
    rdx_dut_info.dut_mode=0;
    rdx_ble_server_auto_shut_down_enable(1); CHECK(auto_off);
    return 0;
}
int test_finalpack_failures(void) {
    boot(); busy=1; rdx_dut_finalpack_end(); CHECK(!formats && !g_finalpack_end_pending);
    boot(); record.run=1; rdx_dut_finalpack_end(); CHECK(!formats);
    boot(); files.file_send_busy=1; rdx_dut_finalpack_end(); CHECK(!formats);
    boot(); rdx_dut_info.current_func=1; rdx_dut_finalpack_end(); CHECK(!formats);
    boot(); rdx_dut_finalpack_end(); finish(0);
    CHECK(!timers && !resets && flash[1]==1 && flash[0]==0xff && !g_finalpack_end_pending);
    CHECK(done_led==RDX_LED_SCENE_DUT_ENTER);
    boot(); rdx_dut_finalpack_end(); posts_fail=1; finish(1);
    CHECK(!timers && !resets && !g_finalpack_end_pending);
    CHECK(done_led==RDX_LED_SCENE_DUT_ENTER);
    boot(); rdx_dut_finalpack_end(); resets_fail=1; finish(1);
    CHECK(!timers && flash[1]==1 && flash[0]==0xff);
    CHECK(done_led==RDX_LED_SCENE_DUT_ENTER);
    for (int id=0; id<2; ++id) {
        for (int fault=0; fault<3; ++fault) {
            boot(); rdx_dut_finalpack_end(); fail_id=id;
            write_fail=fault==0; read_fail=fault==1; mismatch=fault==2;
            finish(1);
            CHECK(!timers && !poweroffs && done_led==RDX_LED_SCENE_DUT_ENTER && !g_finalpack_end_pending);
            CHECK(!rdx_dut_info.key_dut_disabled);
            if(id==1) CHECK(rdx_bound_info.bound_state==1 && !revokes && flash[0]==0xff);
        }
    }
    return 0;
}
int test_normal_defaults_reset(void) {
    boot(); rdx_vm_sys_reset_to_defaults();
    CHECK(resets==1 && reset_requests==1);
    boot(); resets_fail=1; rdx_vm_sys_reset_to_defaults();
    CHECK(resets==1 && !reset_requests);
    return 0;
}
int test_auto_off_scope(void) {
    boot(); rdx_dut_info.dut_mode=0;
    sys_set_auto_off_time(10);
    CHECK(stored_auto_off==10 && sys_get_auto_off_time()==10);
    CHECK(app_var.auto_off_time==600 && auto_off);
    rdx_dut_info.dut_mode=1;
    sys_set_auto_off_time(3);
    CHECK(stored_auto_off==3 && sys_get_auto_off_time()==3);
    CHECK(app_var.auto_off_time==180 && !auto_off);
    rdx_dut_info.dut_mode=0;
    rdx_ble_server_auto_shut_down_enable(1); CHECK(auto_off);
    sys_set_auto_off_time(0);
    CHECK(sys_get_auto_off_time()==0 && app_var.auto_off_time==0);
    return 0;
}
int test_key_entry_recovery(void) {
    boot(); flash[0]=0xaa; rdx_dut_info.key_dut_disabled=1;
    write_fail=1; rdx_dut_key_dut_enable(); CHECK(rdx_dut_info.key_dut_disabled);
    write_fail=0; rdx_dut_key_dut_enable(); CHECK(!rdx_dut_info.key_dut_disabled && flash[0]==0xff);
    rdx_dut_key_dut_disable(); CHECK(rdx_dut_info.key_dut_disabled && flash[0]==0xaa);
    return 0;
}
'''


def main():
    base = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    dut = (base / 'rdx_dut.c').read_text(encoding='utf-8')
    vm = (base / 'rdx_vm.c').read_text(encoding='utf-8')
    names = [
        'static bool rdx_dut_finalpack_mode_ack(',
        'static void rdx_dut_finalpack_fail(', 'static int rdx_dut_key_dut_store(',
        'static void rdx_dut_finalpack_commit(',
        'void rdx_dut_finalpack_end_format_cb(', 'void rdx_dut_finalpack_end(',
        'void rdx_dut_key_dut_enable(', 'void rdx_dut_key_dut_disable(',
    ]
    no_off = function(vm, 'int rdx_vm_reset_defaults_no_poweroff(')
    assert 'rdx_app_time_to_reset(' not in no_off
    assert 'rdx_vm_reset_defaults_no_poweroff()' in function(vm, 'void rdx_vm_sys_reset_to_defaults(')
    for name in ['static void rdx_dut_cmd_async_handle(', 'void rdx_dut_ble_cmd_handle(',
                 'void rdx_dut_key_handle(']:
        assert 'if (g_finalpack_end_pending)' in function(dut, name)
    ble = (base / 'rdx_ble_server.c').read_text(encoding='utf-8')
    source = STUBS + function(ble[ble.rindex('void rdx_ble_server_auto_shut_down_enable('):],
                              'void rdx_ble_server_auto_shut_down_enable(')
    power = (ROOT / 'SDK/apps/earphone/mode/bt/poweroff.c').read_text(encoding='utf-8')
    enable = function(power, 'void sys_auto_shut_down_enable(')
    assert 'if (rdx_app_get_dut_status()) {\n        sys_auto_shut_down_disable();\n        return;' in enable
    overlay = (ROOT / 'SDK/apps/earphone/include/t2620_project_config.h').read_text(encoding='utf-8')
    assert '#undef TCFG_AUTO_SHUT_DOWN_TIME' not in overlay
    cfg = (ROOT / 'SDK/apps/earphone/user_cfg.c').read_text(encoding='utf-8')
    source += function(cfg, 'u32 sys_get_auto_off_time(')
    source += function(cfg, 'void sys_set_auto_off_time(')
    source += function(vm, 'int rdx_vm_set_bound_status(')
    source += function(vm, 'void rdx_vm_sys_reset_to_defaults(')
    source += '\n'.join(function(dut, name) for name in names) + TESTS
    with tempfile.TemporaryDirectory(prefix='finalpack_') as tmp:
        path = Path(tmp) / 'finalpack.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, ['test_finalpack_success', 'test_finalpack_failures',
                            'test_key_entry_recovery', 'test_auto_off_scope', 'test_normal_defaults_reset'])
    print('Finalpack commit/failure/recovery behavior passed (mock flash/format/timers).')


if __name__ == '__main__':
    main()
