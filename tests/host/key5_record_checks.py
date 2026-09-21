"""Production KEY5 routing and recording pump, with OS/audio/transport mocked."""
import re
import tempfile
from pathlib import Path
from host_c_test_lib import ROOT, function, run_c_checks

BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'

STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int bool;
#define NULL ((void *)0)
#define true 1
#define false 0
#define TRUE 1
#define FALSE 0
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1
#define TDX_HAS_RECMARK_ABILITY 1
#define RDX_MARK_SOURCE_KEY 1
#define PB_LOG(...) ((void)0)
#define APP_MODE_PC 1
#define Q_CALLBACK 1
#define RDX_HOLD_RECORD_RETRY_MS 20
#define r_printf(...) ((void)0)
#define g_printf(...) ((void)0)
#define log_info(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
enum { RECORD_STATE_STOP, RECORD_STATE_START, RECORD_STATE_RESUME, RECORD_STATE_PAUSE,
       RECORD_SCENE_CHAT, RECORD_SCENE_CALL, RECORD_FORMATE_OPUS_16K_STERO,
       REC_PROCESS_STATE_BUSY, RECORD_MODE_OFFLINE };
typedef struct { int run, scene, formate, mode, orig_mode, process_state, key_trigger; } RecordStatus;
typedef struct { int dummy; } rdx_ble_async_token_t;
static RecordStatus status, set_rp;
static u8 hold_record_stop_queued, hold_record_pressed, hold_record_session_active;
static u8 hold_record_busy_wait_armed, hold_record_scene, key5_online_hold_routed;
static u8 key5_local_hold_routed, hold_record_local, key_press_record_ready_flag;
static u32 hold_record_local_generation, generation;
static u16 hold_record_retry_timer;
static int rdx_app_init_flag=1, poweroff_ready_flag, rdx_dut_mode, mode_switch_keep_timer;
static int connected, ready, bound=1, blocked, queue_fail, stop_fail, stops, playback, playback_stops, starts;
static int online_starts, online_stops, ota, formatting;
static int marks;
static RecordStatus *rdx_record_get_status(void) { return &status; }
static int rdx_record_binding_allowed(void) { return bound; }
static int rdx_storage_lifecycle_business_blocked(void) { return blocked; }
static int rdx_record_process_is_busy_check(void) { return status.process_state==REC_PROCESS_STATE_BUSY; }
static int rdx_ble_server_has_active_link(void) { return connected; }
static u16 rdx_ble_server_get_conn_handle(void) { return connected?1:0xffff; }
static int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t,int r) { return ready; }
static int get_ota_status(void) { return ota; }
static int app_in_mode(int m) { return 0; }
static int rdx_uxfile_sd_format_status_check(void) { return formatting; }
static int rdx_uxfile_sync_is_in_progress(void) { return 0; }
static int rdx_uxfile_is_scan_active(void) { return 0; }
static int rdx_uxfile_is_formatting(void) { return formatting; }
static int rdx_record_format_for_session(int s,int stream) { return 1; }
static void rdx_record_prepare_new_session(void) { ++generation; }
static u32 rdx_record_generation_get(void) { return generation; }
static void rdx_record_process(void) {}
static void rdx_playback_stop(void) { playback=0; ++playback_stops; }
static int os_taskq_post_type(const char *t,int q,int n,int *m) {
    if(queue_fail) return -1;
    if(status.run==RECORD_STATE_START) ++starts;
    return 0;
}
static int sys_timeout_add(void *p,void (*cb)(void *),int t) { return 1; }
static void sys_timeout_del(int t) {}
static int rdx_record_audio_fault_stop(void) {
    if(stop_fail) return 0;
    ++stops; status.run=RECORD_STATE_STOP; return 1;
}
static int rdx_record_stream_only_release(void) { return 1; }
static int rdx_record_stream_only_is_releasing(void) { return 0; }
static void rdx_record_stream_only_start_arm(rdx_ble_async_token_t *t) {}
static void rdx_app_record_state_upload_timer_start(void) {}
static int rdx_app_record_trigger_post(RecordStatus *s,int f,rdx_ble_async_token_t *t,int stream) {
    if(s->run==RECORD_STATE_START) ++online_starts; else ++online_stops;
    return 0;
}
static void rdx_app_emmc_poweron(int x) {}
static void app_send_message(int m,int a) {}
static int rdx_uxfile_is_datFileInfo_loading(void) { return 0; }
static int rdx_is_file_transfer_active(void) { return 0; }
static int rdx_is_file_sync_busy(void) { return 0; }
static void rdx_record_add_mark(int source) { ++marks; }
static bool rdx_playback_can_start(void);
static void rdx_playback_toggle(void) { if(rdx_playback_can_start()) playback=!playback; }
'''

TESTS = r'''
static void reset(void) {
    rdx_app_hold_record_reset(); status=(RecordStatus){0};
    status.scene=RECORD_SCENE_CHAT; status.orig_mode=RECORD_MODE_OFFLINE;
    connected=ready=blocked=queue_fail=stop_fail=stops=playback=playback_stops=starts=0;
    online_starts=online_stops=ota=formatting=poweroff_ready_flag=rdx_dut_mode=0;
    mode_switch_keep_timer=0; rdx_app_init_flag=bound=1;
    marks=0;
}
static void key(int event) {
    int message=APP_MSG_NULL;
    rdx_app_key5_remap(&message,event,1);
    handle(message);
}
int test_local_hold_and_double(void) {
    reset(); playback=1;
    key(KEY_ACTION_LONG);
    CHECK(starts==1 && !playback && playback_stops==1 && hold_record_session_active);
    key(KEY_ACTION_CLICK); CHECK(!playback && marks==1 && !rdx_playback_can_start());
    key(KEY_ACTION_HOLD); CHECK(starts==1 && !stops);
    key(KEY_ACTION_UP); CHECK(stops==1 && status.run==RECORD_STATE_STOP);
    key(KEY_ACTION_UP); CHECK(stops==1);
    key(KEY_ACTION_CLICK); CHECK(playback);
    reset(); key(KEY_ACTION_DOUBLE_CLICK); CHECK(starts==1);
    key(KEY_ACTION_LONG); key(KEY_ACTION_HOLD); key(KEY_ACTION_UP);
    CHECK(status.run==RECORD_STATE_START && !stops && starts==1);
    key(KEY_ACTION_DOUBLE_CLICK); CHECK(status.run==RECORD_STATE_STOP);
    CHECK(key_table_io_num4_normal[KEY_ACTION_CLICK]==APP_MSG_REC_PLAY_TOGGLE);
    CHECK(APP_MSG_RECORD_LOCAL_HOLD_STOP <= 255);
    return 0;
}
int test_local_release_ownership(void) {
    reset(); key(KEY_ACTION_LONG); connected=1; ready=0; key(KEY_ACTION_UP);
    CHECK(stops==1 && !online_stops);
    reset(); key(KEY_ACTION_LONG); ++generation; key(KEY_ACTION_UP);
    CHECK(!stops && status.run==RECORD_STATE_START);
    reset(); queue_fail=1; key(KEY_ACTION_LONG); queue_fail=0; key(KEY_ACTION_UP);
    CHECK(!starts && !stops && status.run==RECORD_STATE_STOP);
    reset(); status.process_state=REC_PROCESS_STATE_BUSY; key(KEY_ACTION_LONG);
    CHECK(!starts && hold_record_pressed); key(KEY_ACTION_UP);
    status.process_state=0; rdx_app_hold_record_pump(); CHECK(!starts && !stops);
    reset(); key(KEY_ACTION_LONG); stop_fail=1; key(KEY_ACTION_UP);
    CHECK(hold_record_session_active && hold_record_retry_timer && !stops);
    stop_fail=0; rdx_app_hold_record_pump(); CHECK(stops==1 && !hold_record_session_active);
    return 0;
}
int test_recording_blocks_player_keys(void) {
    int value;
    const int states[] = {RECORD_STATE_START, RECORD_STATE_RESUME, RECORD_STATE_PAUSE};
    reset();
    for (int n=0; n<4; ++n) {
        for (int event=0; event<KEY_ACTION_MAX; ++event) {
            for (int s=0; s<3; ++s) {
                status.run=states[s]; value=-1;
                rdx_app_local_player_key_remap(&value,n,event,2);
                CHECK(value==APP_MSG_NULL);
            }
            status.run=RECORD_STATE_STOP; status.process_state=REC_PROCESS_STATE_BUSY;
            rdx_app_local_player_key_remap(&value,n,event,1);
            CHECK(value==APP_MSG_NULL);
            status.process_state=0;
            rdx_app_local_player_key_remap(&value,n,event,1);
            CHECK(value==rdx_key_get_io_num_table(n,1)[event]);
        }
    }
    CHECK(key_table_io_num0_normal[KEY_ACTION_CLICK]==APP_MSG_REC_PREV);
    CHECK(key_table_io_num1_normal[KEY_ACTION_LONG]==APP_MSG_REC_FF);
    for (int n=2; n<4; ++n) {
        int volume=rdx_key_get_io_num_table(n,1)[KEY_ACTION_CLICK];
        CHECK(volume==APP_MSG_VOL_UP || volume==APP_MSG_VOL_DOWN);
    }
    CHECK(key_table_io_num2_normal[KEY_ACTION_CLICK]!=key_table_io_num3_normal[KEY_ACTION_CLICK]);
    return 0;
}
int test_queued_player_message_during_recording(void) {
    const int messages[] = {APP_MSG_REC_PREV, APP_MSG_REC_NEXT,
        APP_MSG_REC_FR, APP_MSG_REC_FF, APP_MSG_VOL_UP, APP_MSG_VOL_DOWN};
    reset();
    for (int i=0; i<6; ++i) {
        status.run=RECORD_STATE_STOP; status.process_state=0;
        CHECK(!rdx_app_local_player_message_blocked(messages[i]));
        /* A message admitted before START must be rejected at dispatch. */
        status.run=RECORD_STATE_START;
        CHECK(rdx_app_local_player_message_blocked(messages[i]));
        status.run=RECORD_STATE_STOP; status.process_state=REC_PROCESS_STATE_BUSY;
        CHECK(rdx_app_local_player_message_blocked(messages[i]));
        status.process_state=0;
        CHECK(!rdx_app_local_player_message_blocked(messages[i]));
    }
    status.run=RECORD_STATE_START;
    CHECK(!rdx_app_local_player_message_blocked(APP_MSG_RECORD_LOCAL_TOGGLE));
    CHECK(!rdx_app_local_player_message_blocked(APP_MSG_RECORD_LOCAL_HOLD_STOP));
    CHECK(!rdx_app_local_player_message_blocked(APP_MSG_REC_PLAY_TOGGLE));
    connected=1;
    CHECK(!rdx_app_local_player_message_blocked(APP_MSG_VOL_UP));
    return 0;
}
int test_hold_admission_and_online(void) {
    reset(); bound=0; key(KEY_ACTION_LONG); key(KEY_ACTION_UP); CHECK(!starts && !stops);
    reset(); blocked=1; key(KEY_ACTION_LONG); key(KEY_ACTION_UP); CHECK(!starts && !stops);
    reset(); formatting=1; key(KEY_ACTION_LONG); key(KEY_ACTION_UP); CHECK(!starts && !stops);
    reset(); connected=ready=1; key(KEY_ACTION_LONG); key(KEY_ACTION_UP);
    CHECK(online_starts==1 && online_stops==1 && !starts && !stops);
    reset(); connected=1; key(KEY_ACTION_LONG); key(KEY_ACTION_UP); CHECK(!starts && !online_starts);
    return 0;
}
'''


def main():
    app = (BASE / 'rdx_app.c').read_text(encoding='utf-8')
    keys = (BASE / 'rdx_key.c').read_text(encoding='utf-8')
    header = (ROOT / 'SDK/apps/earphone/include/app_msg.h').read_text(encoding='utf-8')
    # Use the actual message enum so byte-sized key tables are validated too.
    enum_start = header.rfind('enum', 0, header.index('APP_MSG_NULL'))
    enums = header[enum_start:header.index('};', enum_start)+2]
    kh = (ROOT / 'SDK/apps/common/device/key/key_driver.h').read_text(encoding='utf-8')
    enums += re.search(r'enum key_action \{.*?\};', kh, re.S)[0]
    tables = ''.join(re.search(r'u8 '+n+r'\[KEY_ACTION_MAX\]\s*=\s*\{.*?\};', keys, re.S)[0]
                     for n in ('key_table_record_hold', *(f'key_table_io_num{i}_normal' for i in range(5))))
    helpers = ''.join(function(app, n) for n in (
        'rdx_app_hold_record_retry_cb', 'rdx_app_hold_record_retry_schedule',
        'rdx_app_hold_record_retry_cancel', 'rdx_app_hold_record_reset',
        'rdx_app_hold_record_wait_until_ready', 'rdx_app_hold_record_pump',
        'rdx_app_rdx_key_route_ready', 'rdx_app_key5_remap', 'rdx_app_local_player_key_remap',
        'rdx_app_local_player_message_blocked',
        'rdx_app_device_record_set', 'rdx_app_device_record_handle'))
    handler = function(app, 'rdx_app_msg_handler')
    assert handler.index('if (rdx_app_local_player_message_blocked(msg[0]))') < handler.index('switch (msg[0])')
    begin = handler.index('        case APP_MSG_RECORD_LOCAL_HOLD_START:')
    end = handler.index('        case APP_MSG_REC_PREV:', begin)
    cases = handler[begin:end]
    begin = handler.index('        case APP_MSG_REC_PLAY_TOGGLE:')
    cases += handler[begin:handler.index('        case APP_MSG_TWS_START_PAIR:', begin)]
    playback = (BASE / 'rdx_playback.c').read_text(encoding='utf-8')
    program = (STUBS + enums + '\n' + tables + '\n' +
               'static u8 *rdx_key_get_io_num_table(int n,int s) { u8 *t[]={key_table_io_num0_normal,key_table_io_num1_normal,key_table_io_num2_normal,key_table_io_num3_normal,key_table_io_num4_normal}; return t[n]; }\n'
               'static void rdx_app_hold_record_pump(void);\n'
               'static int rdx_app_device_record_set(u8,u8,u8);\n' + helpers +
               function(playback, 'rdx_playback_can_start') +
               'static void handle(int m) { int msg[1]={m}; int ret=0; switch(m) {\n' + cases +
               'default: break; }}\n' + TESTS)
    with tempfile.TemporaryDirectory(prefix='key5-record-') as temp:
        path = Path(temp) / 'key5.c'
        path.write_text(program, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)
    print('KEY5 recording: local hold/double ownership, playback stop, admission and release retries passed.')


if __name__ == '__main__':
    main()
