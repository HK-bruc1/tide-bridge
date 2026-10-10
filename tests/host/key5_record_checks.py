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
#define TCFG_RDX_HOGP_ENABLE 1
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
static u8 hold_record_disconnect_pending;
static int stream_active, release_calls, release_done=1;
static u8 hold_record_stop_queued, hold_record_pressed, hold_record_session_active;
static u8 hold_record_busy_wait_armed, hold_record_scene, key5_online_hold_routed;
static u8 key5_local_hold_routed, hold_record_local, key_press_record_ready_flag;
static u32 hold_record_local_generation, generation;
static u16 hold_record_retry_timer;
static u16 key5_offline_stop_timer;
static u32 key5_offline_stop_generation;
static int timer_fail, timer_deletes, pc_mode, factory_keys;
static void (*queued_callback)(void *);
static void *queued_priv;
static int rdx_app_init_flag=1, poweroff_ready_flag, rdx_dut_mode, mode_switch_keep_timer;
static int connected, ready, bound=1, blocked, queue_fail, stop_fail, stops, playback, playback_stops, starts;
static int online_starts, online_stops, ota, formatting;
static int marks;
#define KEY_IO_NUM0 65
#define KEY_IO_NUM1 66
#define KEY_IO_NUM4 69
static int battery_queries, battery_posts;
static int hid_ready, feedback_scene, feedback_calls;
enum { RDX_LED_SCENE_KEY1_FEEDBACK=101, RDX_LED_SCENE_KEY2_FEEDBACK };
static int rdx_hogp_keyboard_is_ready(void) { return hid_ready; }
static void rdx_led_ctrl_set_scene(int scene) { feedback_scene=scene; ++feedback_calls; }
#define y_printf(...) ((void)0)
static struct { int goto_poweroff_flag; } app_var;
static int rdx_app_business_started(void) { return rdx_app_init_flag; }
static int rdx_dut_test_keys_active(void) { return rdx_dut_mode || factory_keys; }
static void rdx_led_ctrl_show_battery(void) { ++battery_queries; }
static RecordStatus *rdx_record_get_status(void) { return &status; }
static int rdx_record_binding_allowed(void) { return bound; }
static int rdx_storage_lifecycle_business_blocked(void) { return blocked; }
static int rdx_record_format_download_conflict(const char *operation) { return blocked; }
static int rdx_record_process_is_busy_check(void) { return status.process_state==REC_PROCESS_STATE_BUSY; }
static int rdx_ble_server_has_active_link(void) { return connected; }
static u16 rdx_ble_server_get_conn_handle(void) { return connected?1:0xffff; }
static int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t,int r) { return ready; }
static int get_ota_status(void) { return ota; }
static int app_in_mode(int m) { return pc_mode; }
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
    if(n==3) { queued_callback=(void (*)(void *))m[0]; queued_priv=(void *)m[2]; }
    ++battery_posts;
    if(status.run==RECORD_STATE_START) ++starts;
    return 0;
}
static int sys_timeout_add(void *p,void (*cb)(void *),int t) { return 1; }
static void sys_timeout_del(int t) {}
static int sys_timer_add(void *p,void (*cb)(void *),int t) { return timer_fail?0:2; }
static void sys_timer_del(int t) { ++timer_deletes; }
static int rdx_record_audio_fault_stop(void) {
    if(stop_fail) return 0;
    ++stops; status.run=RECORD_STATE_STOP; return 1;
}
static int rdx_record_stream_only_release(void) { ++release_calls; return release_done; }
static int rdx_record_stream_only_session_is_active(void) { return stream_active; }
static void rdx_record_stream_only_release_complete(void) {}
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
int test_dut_key5_routing(void) {
    for(int link=0;link<3;++link) {
        connected=link!=0; ready=link==2;
        key5_local_hold_routed=key5_online_hold_routed=0;
        int msg=-1;
        rdx_app_key5_remap(&msg,KEY_ACTION_DOUBLE_CLICK,4);
        CHECK(msg==APP_MSG_DUT_BURNIN_TOGGLE && msg<=255);
        rdx_app_key5_remap(&msg,10,4);
        CHECK(msg==APP_MSG_DUT);
        rdx_app_key5_remap(&msg,KEY_ACTION_LONG,4);
        CHECK(msg==APP_MSG_NULL && !key5_online_hold_routed);
        rdx_app_key5_remap(&msg,KEY_ACTION_CLICK,4);
        CHECK(msg==APP_MSG_NULL);
    }
    return 0;
}
static void reset(void) {
    rdx_app_hold_record_reset(); status=(RecordStatus){0};
    status.scene=RECORD_SCENE_CHAT; status.orig_mode=RECORD_MODE_OFFLINE;
    connected=ready=blocked=queue_fail=stop_fail=stops=playback=playback_stops=starts=0;
    online_starts=online_stops=ota=formatting=poweroff_ready_flag=rdx_dut_mode=0;
    mode_switch_keep_timer=0; rdx_app_init_flag=bound=1;
    marks=0;
    hid_ready=feedback_scene=feedback_calls=battery_queries=0;
    key5_offline_stop_timer=0; key5_offline_stop_generation=0;
    timer_fail=timer_deletes=pc_mode=factory_keys=0;
    app_var.goto_poweroff_flag=0; queued_callback=0; queued_priv=0;
    s_rdx_hogp_key_action_active=1;
    s_rdx_hogp_key_action_active_keymap=(rdx_hogp_key_action_keymap_t){0};
    s_rdx_hogp_key_action_active_keymap.key_count=5;
    s_rdx_hogp_key_action_active_keymap.keys[4].usages[0]=0x6c;
}
static void key(int event) {
    int message=APP_MSG_NULL;
    rdx_app_key5_remap(&message,event,1);
    handle(message);
}
int test_connected_offline_double_stop(void) {
    const int states[]={RECORD_STATE_START,RECORD_STATE_RESUME,RECORD_STATE_PAUSE};
    for(int state=0;state<3;++state) {
        for(int link=0;link<2;++link) {
            for(int mapping=0;mapping<4;++mapping) {
                reset(); connected=1; ready=link; status.run=states[state];
                if(mapping==1) s_rdx_hogp_key_action_active_keymap.keys[4].usages[0]=4;
                if(mapping==2) s_rdx_hogp_key_action_active_keymap.keys[4].modifiers=1;
                if(mapping==3) s_rdx_hogp_key_action_active=0;
                key(KEY_ACTION_DOUBLE_CLICK);
                CHECK(stops==1 && status.run==RECORD_STATE_STOP);
                CHECK(!starts && !online_starts && !online_stops && !key5_offline_stop_timer);
                key(KEY_ACTION_DOUBLE_CLICK); CHECK(stops==1 && !starts);
            }
        }
    }
    reset(); connected=ready=1; status.run=RECORD_STATE_START;
    status.orig_mode=RECORD_MODE_OFFLINE+1;
    key(KEY_ACTION_DOUBLE_CLICK); CHECK(!stops);
    reset(); connected=ready=1; key(KEY_ACTION_DOUBLE_CLICK);
    CHECK(!stops && !starts && !online_starts);
    return 0;
}
int test_offline_stop_retry_and_generation(void) {
    reset(); connected=ready=1; status.run=RECORD_STATE_START; stop_fail=1;
    key(KEY_ACTION_DOUBLE_CLICK); CHECK(key5_offline_stop_timer && !stops);
    u32 old=generation;
    queue_fail=1; rdx_app_key5_offline_stop_tick((void *)old);
    CHECK(!queued_callback && key5_offline_stop_timer);
    queue_fail=0; rdx_app_key5_offline_stop_tick((void *)old);
    CHECK(queued_callback);
    connected=ready=0; stop_fail=0;
    queued_callback(queued_priv);
    CHECK(stops==1 && !key5_offline_stop_timer);
    queued_callback(queued_priv); CHECK(stops==1);

    reset(); connected=ready=1; status.run=RECORD_STATE_PAUSE; stop_fail=1;
    key(KEY_ACTION_DOUBLE_CLICK); old=generation;
    ++generation; stop_fail=0;
    rdx_app_key5_offline_stop_pump((void *)old);
    CHECK(!stops && !key5_offline_stop_timer && status.run==RECORD_STATE_PAUSE);
    stop_fail=1; key(KEY_ACTION_DOUBLE_CLICK);
    CHECK(key5_offline_stop_timer);
    rdx_app_key5_offline_stop_pump((void *)old);
    CHECK(key5_offline_stop_timer && !stops);
    stop_fail=0; rdx_app_key5_offline_stop_pump((void *)generation);
    CHECK(stops==1 && !key5_offline_stop_timer);

    reset(); connected=ready=1; status.run=RECORD_STATE_START; stop_fail=1;
    key(KEY_ACTION_DOUBLE_CLICK);
    status.run=RECORD_STATE_STOP; stop_fail=0;
    rdx_app_key5_offline_stop_pump((void *)generation);
    CHECK(!stops && !starts && !key5_offline_stop_timer);
    reset(); connected=ready=1; status.run=RECORD_STATE_START; timer_fail=1;
    key(KEY_ACTION_DOUBLE_CLICK);
    CHECK(!stops && !key5_offline_stop_timer && status.run==RECORD_STATE_START);
    timer_fail=0; key(KEY_ACTION_DOUBLE_CLICK); CHECK(stops==1);
    return 0;
}
int test_offline_stop_protection_and_hold_pairing(void) {
    for(int variant=0;variant<10;++variant) {
        reset(); connected=ready=1; status.run=RECORD_STATE_START;
        int *guards[]={&poweroff_ready_flag,&rdx_dut_mode,&ota,&mode_switch_keep_timer,
                       &formatting,&blocked,&app_var.goto_poweroff_flag,&pc_mode,&factory_keys};
        if(variant<9) *guards[variant]=1; else rdx_app_init_flag=0;
        key(KEY_ACTION_DOUBLE_CLICK);
        CHECK(!stops && !key5_offline_stop_timer);
    }
    reset(); key(KEY_ACTION_LONG); connected=ready=1;
    key(KEY_ACTION_DOUBLE_CLICK); CHECK(stops==1);
    key(KEY_ACTION_UP); CHECK(stops==1 && !hold_record_session_active && !key5_local_hold_routed);
    return 0;
}
int test_online_record_requires_plain_f17(void) {
    for(int variant=0;variant<7;++variant) {
        reset(); connected=ready=1;
        rdx_hogp_key_action_keyboard_t *k=&s_rdx_hogp_key_action_active_keymap.keys[4];
        if(variant==0) k->usages[0]=0x6d;
        if(variant==1) k->modifiers=1;
        if(variant==2) k->usages[1]=4;
        if(variant==3) k->usages[0]=0;
        if(variant==4) s_rdx_hogp_key_action_active=0;
        if(variant==5) s_rdx_hogp_key_action_active_keymap.key_count=4;
        if(variant==6) k->usages[1]=0x6c;
        s_rdx_hogp_key_action_active_keymap.keys[0].usages[0]=0x6c;
        key(KEY_ACTION_LONG); key(KEY_ACTION_UP);
        CHECK(!online_starts && !online_stops && !key5_online_hold_routed);
    }
    reset(); connected=ready=1;
    key(KEY_ACTION_LONG); CHECK(online_starts==1 && key5_online_hold_routed);
    s_rdx_hogp_key_action_active_keymap.keys[4].usages[0]=0x6d;
    key(KEY_ACTION_UP); CHECK(online_stops==1 && !key5_online_hold_routed);
    reset(); connected=ready=1;
    s_rdx_hogp_key_action_active_keymap.keys[4].usages[0]=0x6d;
    key(KEY_ACTION_LONG);
    s_rdx_hogp_key_action_active_keymap.keys[4].usages[0]=0x6c;
    key(KEY_ACTION_HOLD); CHECK(!online_starts);
    key(KEY_ACTION_UP); CHECK(!online_starts);
    key(KEY_ACTION_LONG); CHECK(online_starts==1);
    key(KEY_ACTION_UP); CHECK(online_stops==1);
    reset();
    s_rdx_hogp_key_action_active_keymap.keys[4].usages[0]=0x6d;
    key(KEY_ACTION_LONG); key(KEY_ACTION_UP); CHECK(starts==1 && stops==1);
    return 0;
}
int test_battery_single_click_only(void) {
    reset(); battery_queries=0; app_var.goto_poweroff_flag=0;
    rdx_app_key_click_cb(NULL); CHECK(battery_queries==1);
    connected=1; ready=0;
    rdx_app_key_click_cb(NULL); CHECK(battery_queries==2);
    blocked=1; rdx_app_key_click_cb(NULL);
    CHECK(battery_queries==3); blocked=0;
    app_var.goto_poweroff_flag=1; rdx_app_key_click_cb(NULL);
    CHECK(battery_queries==3); app_var.goto_poweroff_flag=0;
    battery_posts=0;
    rdx_app_key_click(KEY_IO_NUM4,KEY_ACTION_DOUBLE_CLICK);
    rdx_app_key_click(KEY_IO_NUM0,KEY_ACTION_LONG);
    rdx_app_key_click(KEY_IO_NUM0,KEY_ACTION_UP);
    CHECK(battery_posts==0);
    rdx_app_key_click(KEY_IO_NUM4,KEY_ACTION_CLICK);
    rdx_app_key_click(KEY_IO_NUM0,KEY_ACTION_CLICK);
    CHECK(battery_posts==2);
    return 0;
}
int test_business_key_feedback(void) {
    for(int id=0; id<5; ++id) {
        for(int mapping=0; mapping<5; ++mapping) {
            for(int hid=0; hid<2; ++hid) {
                reset(); hid_ready=hid;
                rdx_hogp_key_action_keyboard_t *map =
                    &s_rdx_hogp_key_action_active_keymap.keys[id];
                map->usages[0]=0x68+id;
                if(mapping==1) map->usages[0]=0x04;
                if(mapping==2) map->modifiers=1;
                if(mapping==3) map->usages[1]=0x04;
                if(mapping==4) map->usages[0]=0;
                rdx_app_key_click(KEY_IO_NUM0+id, KEY_ACTION_CLICK);
                CHECK(queued_priv==(void *)(KEY_IO_NUM0+id));
                CHECK(!feedback_calls && !battery_queries);
                /* 宿主指针可能宽于固件队列字段，直接执行真实回调。 */
                rdx_app_key_click_cb(queued_priv);
                int feedback=hid && id<2 && mapping==0;
                CHECK(feedback_calls==feedback && battery_queries==!feedback);
                if(feedback) CHECK(feedback_scene==
                    (id==0 ? RDX_LED_SCENE_KEY1_FEEDBACK : RDX_LED_SCENE_KEY2_FEEDBACK));
            }
        }
    }
    for(int guard=0; guard<5; ++guard) {
        reset(); hid_ready=1;
        s_rdx_hogp_key_action_active_keymap.keys[0].usages[0]=0x68;
        if(guard==0) rdx_app_init_flag=0;
        if(guard==1) factory_keys=1;
        if(guard==2) app_var.goto_poweroff_flag=1;
        if(guard==3) pc_mode=1;
        if(guard==4) formatting=1;
        rdx_app_key_click_cb((void *)KEY_IO_NUM0);
        CHECK(!feedback_calls && !battery_queries);
    }
    reset(); hid_ready=1;
    s_rdx_hogp_key_action_active_keymap.keys[0].usages[0]=0x68;
    rdx_app_key_click(KEY_IO_NUM0,KEY_ACTION_CLICK);
    hid_ready=0;
    rdx_app_key_click_cb(queued_priv);
    CHECK(battery_queries==1 && !feedback_calls);
    reset(); queue_fail=1;
    rdx_app_key_click(KEY_IO_NUM0,KEY_ACTION_CLICK);
    CHECK(!queued_callback && !feedback_calls && !battery_queries);
    return 0;
}
int test_disconnect(void) {
    hold_record_local=0; hold_record_pressed=1; hold_record_session_active=1;
    key5_online_hold_routed=1; stream_active=1; release_done=0; release_calls=0;
    rdx_app_record_transport_lost();
    CHECK(hold_record_disconnect_pending && !hold_record_pressed);
    rdx_app_hold_record_pump();
    CHECK(release_calls==1 && hold_record_disconnect_pending && hold_record_retry_timer);
    release_done=1; rdx_app_hold_record_pump();
    CHECK(!hold_record_disconnect_pending && !hold_record_session_active);
    CHECK(key5_online_hold_routed && !hold_record_retry_timer);
    int calls=release_calls; rdx_app_hold_record_pump(); CHECK(release_calls==calls);
    stream_active=0; hold_record_local=1; hold_record_pressed=1;
    rdx_app_record_transport_lost(); CHECK(!hold_record_disconnect_pending && hold_record_pressed);
    hold_record_local=0; hold_record_pressed=0;
    rdx_app_record_transport_lost(); CHECK(!hold_record_disconnect_pending);
    return 0;
}
int test_local_hold_and_double(void) {
    reset(); playback=1;
    key(KEY_ACTION_LONG);
    CHECK(starts==1 && !playback && playback_stops==1 && hold_record_session_active);
    CHECK(rdx_app_record_is_hold_active());
    key(KEY_ACTION_CLICK); CHECK(!playback && marks==1 && !rdx_playback_can_start());
    key(KEY_ACTION_HOLD); CHECK(starts==1 && !stops);
    key(KEY_ACTION_UP); CHECK(stops==1 && status.run==RECORD_STATE_STOP);
    CHECK(!rdx_app_record_is_hold_active());
    key(KEY_ACTION_UP); CHECK(stops==1);
    key(KEY_ACTION_CLICK); CHECK(playback);
    reset(); key(KEY_ACTION_DOUBLE_CLICK); CHECK(starts==1);
    CHECK(!rdx_app_record_is_hold_active());
    key(KEY_ACTION_LONG); key(KEY_ACTION_HOLD); key(KEY_ACTION_UP);
    CHECK(status.run==RECORD_STATE_START && !stops && starts==1);
    key(KEY_ACTION_DOUBLE_CLICK); CHECK(status.run==RECORD_STATE_STOP);
    CHECK(key_table_io_num4_normal[KEY_ACTION_CLICK]==APP_MSG_REC_PLAY_TOGGLE);
    CHECK(APP_MSG_RECORD_LOCAL_HOLD_STOP <= 255);
    return 0;
}
int test_hold_led_session_identity(void) {
    reset(); key(KEY_ACTION_LONG);
    connected=1; ready=1;
    CHECK(rdx_app_record_is_hold_active());
    status.run=RECORD_STATE_PAUSE; CHECK(!rdx_app_record_is_hold_active());
    status.run=RECORD_STATE_RESUME; CHECK(rdx_app_record_is_hold_active());
    ++generation; CHECK(!rdx_app_record_is_hold_active());
    reset(); status.run=RECORD_STATE_START; stream_active=1;
    CHECK(rdx_app_record_is_hold_active());
    status.run=RECORD_STATE_STOP; CHECK(!rdx_app_record_is_hold_active());
    status.run=RECORD_STATE_START; stream_active=0;
    CHECK(!rdx_app_record_is_hold_active());
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
    CHECK(rdx_app_record_is_hold_active());
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

STOP_STUBS = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef int bool;
#define true 1
#define false 0
#define NULL ((void *)0)
#define Q_CALLBACK 1
#define RECORD_TASK_NAME "record"
#define RDX_RECORD_APP_STOP_FENCE 1
#define RDX_LED_SCENE_RECORD_STOP 1
#define r_printf(...) ((void)0)
#define g_printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
enum {RECORD_STATE_STOP, RECORD_STATE_START, RECORD_STATE_RESUME,
      RECORD_STATE_PAUSE, REC_PROCESS_STATE_BUSY, REC_PROCESS_STATE_READY};
typedef struct {int epoch;} rdx_ble_async_token_t;
static struct {int run, process_state;} record_status;
static int g_app_stop_pending, g_app_stop_posted, g_app_stop_close, g_app_stop_timer;
static u32 g_app_stop_ticket, g_app_stop_done;
static rdx_ble_async_token_t g_app_stop_token, g_record_session_token;
static int g_record_session_token_valid=1, is_record_task_created=1;
static int g_record_cmd_delay_timer, g_record_cmd_retry_cnt, record_start_tone_pending;
static int queue_fail, timer_fail, posts, sends, current_epoch=1;
static int sys_timer_add(void *p,void (*cb)(void *),int ms) { return timer_fail?0:1; }
static void sys_timer_del(int t) {}
static void sys_timeout_del(int t) {}
static int os_taskq_post_msg(const char *name,int n,int msg,u32 ticket,int close) {
    if(queue_fail) return -1;
    ++posts; return 0;
}
static int os_taskq_post_type(const char *name,int type,int n,int *msg) { return 0; }
static void rdx_record_max_timer_stop(void) {}
static void rdx_record_set_process_state_ready(void) {record_status.process_state=REC_PROCESS_STATE_READY;}
static void rdx_record_set_process_state_busy(void) {record_status.process_state=REC_PROCESS_STATE_BUSY;}
static int rdx_record_rdx_token_is_current(const rdx_ble_async_token_t *t) {
    return t->epoch==current_epoch && current_epoch!=0;
}
static void rdx_protocol_record_state_indicate(void) {++sends;}
static void rdx_record_pending_cmd_clear(void) {}
static void rdx_app_record_state_upload_timer_stop(void) {}
static void rdx_record_stream_only_start_cancel(void) {}
static void rdx_record_start_tone_cancel(void) {record_start_tone_pending=0;}
static void rdx_record_pause_timeout_stop(void) {}
static void rdx_record_set_filter_cnt(int n) {}
static void rdx_led_ctrl_set_scene(int s) {}
static void rdx_ble_server_auto_shut_down_enable(int n) {}
'''

STOP_TESTS = r'''
int test_local_stop_fence_and_owner(void) {
    const int states[]={RECORD_STATE_START,RECORD_STATE_RESUME,RECORD_STATE_PAUSE};
    for(int state=0;state<3;++state) {
        for(int owner=0;owner<3;++owner) {
            posts=sends=0; queue_fail=0; current_epoch=1;
            g_record_session_token.epoch=1;
            g_record_session_token_valid=owner!=2;
            record_status.run=states[state];
            timer_fail=1;
            CHECK(!rdx_record_audio_fault_stop());
            CHECK(record_status.run==states[state] && !g_app_stop_pending);
            timer_fail=0; queue_fail=1;
            CHECK(rdx_record_audio_fault_stop());
            CHECK(g_app_stop_pending && !posts && !sends);
            u32 ticket=g_app_stop_ticket;
            CHECK(rdx_record_audio_fault_stop() && g_app_stop_ticket==ticket);
            queue_fail=0;
            rdx_record_app_stop_pump((void *)ticket);
            CHECK(posts==1 && !sends && g_app_stop_pending);
            rdx_record_app_stop_pump((void *)ticket);
            CHECK(posts==1 && !sends);
            if(owner==1) current_epoch=2;
            if(owner==2) current_epoch=0;
            g_app_stop_done=ticket;
            rdx_record_app_stop_pump((void *)ticket);
            CHECK(!g_app_stop_pending && sends==(owner==0));
            CHECK(record_status.process_state==REC_PROCESS_STATE_READY);
            rdx_record_app_stop_pump((void *)ticket);
            CHECK(posts==1 && sends==(owner==0));
        }
    }
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
                     for n in ('key_table_record_hold', 'key_table_io_num4_dut',
                               *(f'key_table_io_num{i}_normal' for i in range(5))))
    helpers = ''.join(function(app, n) for n in (
        'rdx_app_hold_record_retry_cb', 'rdx_app_hold_record_retry_schedule',
        'rdx_app_hold_record_retry_cancel', 'rdx_app_hold_record_reset',
        'rdx_app_record_is_hold_active', 'rdx_app_key_click_cb', 'rdx_app_key_click',
        'rdx_app_record_transport_lost', 'rdx_app_hold_record_wait_until_ready', 'rdx_app_hold_record_pump',
        'rdx_app_rdx_key_route_ready',
        'rdx_app_key5_offline_stop_allowed', 'rdx_app_key5_offline_stop_pump',
        'rdx_app_key5_offline_stop_tick', 'rdx_app_key5_offline_stop_request',
        'rdx_app_key5_remap', 'rdx_app_local_player_key_remap',
        'rdx_app_local_player_message_blocked',
        'rdx_app_device_record_set', 'rdx_app_device_record_handle'))
    handler = function(app, 'rdx_app_msg_handler')
    assert re.search(r'case APP_MSG_DUT_BURNIN_TOGGLE:\s*rdx_dut_burnin_toggle\(\);', handler)
    assert handler.index('if (rdx_app_local_player_message_blocked(msg[0]))') < handler.index('switch (msg[0])')
    begin = handler.index('        case APP_MSG_RECORD_LOCAL_HOLD_START:')
    end = handler.index('        case APP_MSG_REC_PREV:', begin)
    cases = handler[begin:end]
    begin = handler.index('        case APP_MSG_REC_PLAY_TOGGLE:')
    cases += handler[begin:handler.index('        case APP_MSG_TWS_START_PAIR:', begin)]
    playback = (BASE / 'rdx_playback.c').read_text(encoding='utf-8')
    action = (BASE / 'rdx_hogp_key_action.c').read_text(encoding='utf-8')
    query = (re.sub(r'^#include[^\n]*', '',
                    (BASE / 'rdx_hogp_key_action.h').read_text(encoding='utf-8'), flags=re.M) +
             'static rdx_hogp_key_action_keymap_t s_rdx_hogp_key_action_active_keymap;\n'
             'static u8 s_rdx_hogp_key_action_active;\n' +
             function(action, 'rdx_hogp_key_action_is_plain_key'))
    program = (STUBS + enums + '\n' + tables + '\n' + query +
               'static u8 *rdx_key_get_io_num_table(int n,int s) { u8 *t[]={key_table_io_num0_normal,key_table_io_num1_normal,key_table_io_num2_normal,key_table_io_num3_normal,key_table_io_num4_normal}; return s==4 ? key_table_io_num4_dut : t[n]; }\n'
               'static void rdx_app_hold_record_pump(void);\n'
               'static int rdx_app_device_record_set(u8,u8,u8);\n' + helpers +
               function(playback, 'rdx_playback_can_start') +
               'static void handle(int m) { int msg[1]={m}; int ret=0; switch(m) {\n' + cases +
               'default: break; }}\n' + TESTS)
    with tempfile.TemporaryDirectory(prefix='key5-record-') as temp:
        path = Path(temp) / 'key5.c'
        path.write_text(program, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)
        record = (BASE / 'rdx_record.c').read_text(encoding='utf-8')
        stop_program = STOP_STUBS + ''.join(function(record, name) for name in (
            'rdx_record_app_stop_pump', 'rdx_record_app_stop_tick',
            'rdx_record_app_stop_request', 'rdx_record_audio_fault_stop')) + STOP_TESTS
        path = Path(temp) / 'key5_stop.c'
        path.write_text(stop_program, encoding='utf-8')
        run_c_checks(path, ['test_local_stop_fence_and_owner'], native=True)
    print('KEY5 recording: routing, generation-safe offline stop, retry fence and owner-scoped reply passed.')


if __name__ == '__main__':
    main()
