"""验证按键值绑定的本地分发、固定录音路由及按压周期归属收尾。"""
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
enum { RDX_RECORD_MEETING, RDX_RECORD_INSPIRATION, RDX_RECORD_CALL, RDX_RECORD_FACTORY };
enum { RDX_RECORD_KEY_IDLE, RDX_RECORD_KEY_START_PENDING, RDX_RECORD_KEY_ACTIVE, RDX_RECORD_KEY_STOPPING };
#define RECORD_MODE_ONLINE 1
static int key_state, record_kind, stream_ready=1, mark_fail, releasing;
static u32 request_generation, key_record_request_generation;
static int record_state_upload_timer;
static RecordStatus status, set_rp;
static u8 key_record_disconnect_pending;
static int stream_active, release_calls, release_done=1;
static int trigger_deferred, trigger_stop_pending, retirement_pending;
static u8 key_record_stop_queued, key_record_pressed, key_record_session_active;
static u8 key_record_scene, key_record_kind;
static u8 key_record_local, key_press_record_ready_flag;
static u32 key_record_local_generation, generation;
static u16 key_record_retry_timer;
static int timer_fail, pc_mode, factory_keys;
static int local_burnin, consume_key, dut_burnins;
static int player_messages, last_player_message;
static u32 local_epoch=1;
static int rdx_app_init_flag=1, poweroff_ready_flag, rdx_dut_mode, mode_switch_keep_timer;
static int ready, bound=1, blocked, cdc_busy, queue_fail, stop_fail, stops, playback, playback_stops, starts;
static int online_starts, online_stops, ota, formatting;
static int marks;
static int dut_disabled;
static int dut_entries;
static int dut_ticket_pending=1, dut_admission_race;
typedef struct { int file_send_busy; } ReqFileInfo;
static ReqFileInfo upload_info;
#define RDX_APP_FILE_CMD_STOP 1
static int rdx_dut_is_in_mode(void) { return rdx_dut_mode; }
static ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) {
    if(dut_admission_race==1) dut_ticket_pending=0;
    return &upload_info;
}
static void rdx_protocol_file_cmd_handle(int command) {
    upload_info.file_send_busy=0;
    if(dut_admission_race==2) dut_ticket_pending=0;
}
static void rdx_dut_key_mode_handle(void) { ++dut_entries; rdx_dut_mode=!rdx_dut_mode; }
static int rdx_dut_is_key_dut_disabled(void) { return dut_disabled; }
#define KEY_IO_NUM0 65
#define KEY_IO_NUM1 66
#define KEY_IO_NUM4 69
static int battery_queries;
static int hid_ready, feedback_calls;
enum { RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK=101 };
static int rdx_hogp_keyboard_is_ready(void) { return hid_ready; }
static void rdx_led_ctrl_set_scene(int scene) { ++feedback_calls; }
#define y_printf(...) ((void)0)
static struct { int goto_poweroff_flag; } app_var;
static int rdx_app_business_started(void) { return rdx_app_init_flag; }
static int rdx_dut_test_keys_active(void) { return factory_keys; }
static int rdx_dut_key_consume(u8 value,u32 tmr) {
    return consume_key || (factory_keys && !(local_burnin && value==KEY_IO_NUM4));
}
static int rdx_app_get_scene(void) { return rdx_dut_mode?4:1; }
static void rdx_led_ctrl_show_battery(void) { ++battery_queries; }
static RecordStatus *rdx_record_get_status(void) { return &status; }
static int rdx_record_binding_allowed(void) { return bound; }
static int rdx_storage_lifecycle_business_blocked(void) { return blocked || cdc_busy; }
static int rdx_storage_lifecycle_stream_blocked(void) { return blocked; }
static int rdx_record_format_download_conflict(const char *operation) { return blocked; }
static int rdx_record_process_is_busy_check(void) { return status.process_state==REC_PROCESS_STATE_BUSY; }
static int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t,int r) { return ready; }
static int get_ota_status(void) { return ota; }
static int app_in_mode(int m) { return pc_mode; }
static int rdx_uxfile_sd_format_status_check(void) { return formatting; }
static int rdx_uxfile_sync_is_in_progress(void) { return 0; }
static int rdx_uxfile_is_scan_active(void) { return 0; }
static int rdx_uxfile_is_formatting(void) { return formatting; }
static int rdx_record_format_for_session(int s,int stream) { return 1; }
static void rdx_record_prepare_new_session(void) { ++generation; key_state=RDX_RECORD_KEY_ACTIVE; }
static u32 rdx_record_generation_get(void) { return generation; }
static void rdx_record_process(void) {}
static void rdx_playback_stop(void) { playback=0; ++playback_stops; }
static int os_taskq_post_type(const char *t,int q,int n,int *m) {
    if(queue_fail) return -1;
    if(status.run==RECORD_STATE_START) ++starts;
    return 0;
}
static int sys_timeout_add(void *p,void (*cb)(void *),int t) { return timer_fail?0:1; }
static void sys_timeout_del(int t) {}
static int rdx_record_audio_fault_stop(void) {
    if(stop_fail) return 0;
    ++stops; status.run=RECORD_STATE_STOP; key_state=RDX_RECORD_KEY_STOPPING; return 1;
}
static int rdx_record_stream_only_release(void) {
    ++release_calls;
    if(trigger_deferred) retirement_pending=1;
    if(!releasing) ++request_generation;
    releasing=1; key_state=RDX_RECORD_KEY_STOPPING; status.run=RECORD_STATE_STOP;
    return release_done;
}
static int rdx_record_stream_only_session_is_active(void) { return stream_active; }
static void rdx_record_stream_only_release_complete(void) { releasing=0; key_state=RDX_RECORD_KEY_IDLE; }
static int rdx_record_stream_only_is_releasing(void) { return releasing; }
static void rdx_record_stream_only_start_arm(rdx_ble_async_token_t *t) {}
static void rdx_app_record_state_upload_timer_start(void) {}
static int rdx_app_record_trigger_post(RecordStatus *s,int f,rdx_ble_async_token_t *t,int stream) {
    if(queue_fail) return -1;
    if(s->run==RECORD_STATE_START) ++online_starts;
    else {
        ++online_stops;
        if(trigger_deferred) trigger_stop_pending=1;
        else rdx_record_stream_only_release_complete();
    }
    return 0;
}
static void app_send_message(int m,int a) {}
static int local_volume, volume_step, tone_running;
#define APP_AUDIO_CURRENT_STATE 0
#define APP_AUDIO_STATE_MUSIC 1
static int tone_player_runing(void) { return tone_running; }
static int rdx_record_tone_config_busy(void) {
    return status.run!=RECORD_STATE_STOP || status.process_state==REC_PROCESS_STATE_BUSY ||
        tone_running || releasing || key_state!=RDX_RECORD_KEY_IDLE || retirement_pending;
}
static int app_audio_get_volume(int state) { return local_volume; }
static void app_audio_volume_up(u8 step) {
    volume_step=step; local_volume+=step;
    if(local_volume>16) local_volume=16;
}
static void app_audio_volume_down(u8 step) {
    volume_step=step; local_volume-=step;
    if(local_volume<0) local_volume=0;
}
static int rdx_uxfile_is_datFileInfo_loading(void) { return 0; }
static int rdx_is_file_transfer_active(void) { return 0; }
static int rdx_is_file_sync_busy(void) { return 0; }
static int rdx_record_add_mark(int source) { if(mark_fail) return 1; ++marks; return 0; }
static bool rdx_playback_can_start(void);
static void rdx_playback_toggle(void) { if(rdx_playback_can_start()) playback=!playback; }
static void rdx_playback_prev(void) { ++player_messages; last_player_message=1; }
static void rdx_playback_next(void) { ++player_messages; last_player_message=2; }
static void rdx_playback_fr(void) { ++player_messages; last_player_message=3; }
static void rdx_playback_ff(void) { ++player_messages; last_player_message=4; }
static int app_default_msg_handler(int *msg) { return 0; }
static void rdx_dut_burnin_toggle(void) { ++dut_burnins; }
#define MSG_FROM_APP 99

static int rdx_ble_server_is_stream_tx_ready(void) { return stream_ready; }
static int rdx_record_kind_get(void) { return record_kind; }
static int rdx_record_key_state_get(void) { return key_state; }
static int rdx_record_key_start_prepare(int kind,int route,rdx_ble_async_token_t *t) {
    if(key_state!=RDX_RECORD_KEY_IDLE || status.run!=RECORD_STATE_STOP ||
       (route==RECORD_MODE_ONLINE && retirement_pending)) return 0;
    ++request_generation; record_kind=kind; key_state=RDX_RECORD_KEY_START_PENDING; return 1;
}
static u32 rdx_record_key_request_generation(void) { return request_generation; }
static int rdx_record_key_request_is_current(u32 gen) {
    return gen==request_generation && (key_state==RDX_RECORD_KEY_START_PENDING || key_state==RDX_RECORD_KEY_ACTIVE);
}
static void rdx_record_key_start_abort(void) { key_state=RDX_RECORD_KEY_IDLE; }
static int rdx_record_stream_only_token_capture(rdx_ble_async_token_t *t) { return ready; }
static void rdx_app_record_state_upload_timer_stop(void) {}
static int storage_full, storage_admits;
static int rdx_record_local_storage_admit(const char *who) { ++storage_admits; return storage_full?-2:0; }
static int save_enabled=1;
static int rdx_record_save_enabled(void) { return save_enabled; }
static int rdx_record_nosave_session_open(void) {
    return !save_enabled && record_kind==RDX_RECORD_MEETING &&
        (key_state==RDX_RECORD_KEY_START_PENDING || key_state==RDX_RECORD_KEY_ACTIVE);
}
'''

def check_seek_step():
    playback = (BASE / 'rdx_playback.c').read_text(encoding='utf-8')
    definitions = '\n'.join(re.search(r'^#define ' + name + r'[ \t]+.+$',
                                     playback, re.M)[0] for name in (
        'PB_OPUS_FRAME_MS', 'PB_SEEK_STEP_MS', 'PB_SEEK_STEP_FRAMES'))
    source = definitions + r'''
typedef int s32;
#define PB_RESULT_OK 0
#define PB_LOG(...) ((void)0)
static int delta, calls;
static int pb_seek_relative(s32 frames) { delta=frames; ++calls; return 0; }
'''
    source += function(playback, 'rdx_playback_ff')
    source += function(playback, 'rdx_playback_fr')
    source += r'''
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
int test_seek_step(void) {
    CHECK(PB_SEEK_STEP_MS==1000u);
    for (int i=0; i<20; ++i) {
        rdx_playback_ff();
        CHECK(delta>0 && delta*PB_OPUS_FRAME_MS==1000u);
        rdx_playback_fr();
        CHECK(delta<0 && (-delta)*PB_OPUS_FRAME_MS==1000u);
    }
    CHECK(calls==40);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-seek-step-') as work:
        path = Path(work) / 'seek_step.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, ['test_seek_step'])


TESTS = r'''
static void reset(void) {
    rdx_app_key_record_reset();
    status=(RecordStatus){0}; status.run=RECORD_STATE_STOP; status.scene=RECORD_SCENE_CHAT;
    generation=1; key_state=RDX_RECORD_KEY_IDLE; record_kind=RDX_RECORD_MEETING;
    rdx_app_init_flag=bound=stream_ready=release_done=1;
    poweroff_ready_flag=rdx_dut_mode=mode_switch_keep_timer=ready=blocked=cdc_busy=0;
    queue_fail=stop_fail=stops=starts=online_starts=online_stops=ota=formatting=0;
    timer_fail=pc_mode=factory_keys=local_burnin=consume_key=0;
    player_messages=last_player_message=0;
    playback=playback_stops=marks=mark_fail=tone_running=0;
    battery_queries=hid_ready=feedback_calls=release_calls=stream_active=releasing=0;
    trigger_deferred=trigger_stop_pending=retirement_pending=0;
    storage_full=storage_admits=0;
    save_enabled=1;
    app_var.goto_poweroff_flag=0; local_epoch=1;
    dut_disabled=dut_entries=0;
    dut_ticket_pending=1; dut_admission_race=0; upload_info.file_send_busy=0;
}
static rdx_hogp_input_cycle_t cycle(u8 key,u8 usage,u32 id) {
    rdx_hogp_input_cycle_t c={0};
    c.id=id; c.epoch=local_epoch; c.map=1; c.physical_key=KEY_IO_NUM0+key;
    c.mode=rdx_dut_mode?RDX_LOCAL_KEY_DUT:RDX_LOCAL_KEY_NORMAL;
    c.keymap.usages[0]=usage; c.usage=usage; c.hid_admitted=hid_ready;
    return c;
}
static void gesture(rdx_hogp_input_cycle_t *c,u8 action) {
    struct key_event k={0}; k.type=KEY_DRIVER_TYPE_IO; k.value=c->physical_key; k.event=action;
    rdx_app_local_key_gesture(&k,c);
}
static void down(rdx_hogp_input_cycle_t *c) { rdx_app_local_key_down(c); }
static void finish_stop(void) {
    status.run=RECORD_STATE_STOP; status.process_state=0; key_state=RDX_RECORD_KEY_IDLE;
    rdx_app_key_record_pump();
}
int test_all_usages_on_all_keys(void) {
    const int messages[]={APP_MSG_REC_PREV,APP_MSG_REC_NEXT,APP_MSG_LOCAL_VOL_DOWN,
        APP_MSG_LOCAL_VOL_UP,APP_MSG_REC_PLAY_TOGGLE};
    for(int key=0;key<5;++key) for(int usage=0x68;usage<=0x73;++usage) {
        reset(); ready=hid_ready=1;
        rdx_hogp_input_cycle_t c=cycle(key,usage,1);
        int msg=-1; rdx_app_usage_key_remap(&msg,c.physical_key,usage,KEY_ACTION_CLICK,1);
        CHECK(msg==(usage<=0x6c?messages[usage-0x68]:APP_MSG_NULL));
        gesture(&c,KEY_ACTION_CLICK);
        CHECK(feedback_calls==(usage>=0x6f));
        CHECK(battery_queries==(usage<0x6f));
        CHECK(!online_starts && !starts && !marks);
        if(usage==0x68 || usage==0x69) CHECK(player_messages==1);
        if(usage==0x6a) CHECK(volume_step==2);
        if(usage==0x6b) CHECK(volume_step==2);
        if(usage==0x6c) CHECK(playback);
        reset(); c=cycle(key,usage,2); hid_ready=0;
        gesture(&c,KEY_ACTION_CLICK); CHECK(battery_queries==1 && !feedback_calls);
    }
    return 0;
}
int test_inspiration_routes_and_owner(void) {
    for(int key=0;key<5;++key) for(int links=0;links<5;++links) {
        reset(); ready=links>=2; stream_ready=links>=3;
        hid_ready=links==1 || links==4;
        int online=ready && stream_ready;
        rdx_hogp_input_cycle_t c=cycle(key,0x6e,10);
        down(&c);
        CHECK(key_record_session_active && key_record_owner_valid);
        CHECK(key_record_local==!online && record_kind==RDX_RECORD_INSPIRATION);
        CHECK(online_starts==online && starts==!online && playback_stops==1);
        gesture(&c,KEY_ACTION_HOLD); CHECK(online_starts==online && starts==!online);
        rdx_hogp_input_cycle_t stale=c; ++stale.id;
        CHECK(rdx_app_local_key_cancel(&stale) && !stops && !release_calls);
        /* 热配键和 HID 就绪状态变化不能抢走实体松开事件的归属。 */
        c.keymap.usages[0]=0x68; hid_ready=!hid_ready;
        gesture(&c,KEY_ACTION_UP);
        CHECK(online_stops==online && stops==!online);
        finish_stop(); CHECK(!key_record_session_active);
    }
    return 0;
}
int test_meeting_pending_pause_and_mutual_exclusion(void) {
    for(int key=0;key<5;++key) for(int online=0;online<2;++online) {
        reset(); ready=online;
        rdx_hogp_input_cycle_t meeting=cycle(key,0x6d,20);
        rdx_hogp_input_cycle_t inspiration=cycle(key,0x6e,21);
        gesture(&meeting,KEY_ACTION_DOUBLE_CLICK);
        /* 在线会议与 APP 开启一致（推流并落盘），不建立按住式会话；本地会议固定离线路由。 */
        CHECK(key_record_session_active==!online && record_kind==RDX_RECORD_MEETING);
        CHECK(online_starts==online && starts==!online);
        CHECK(!(online && key_record_local));
        gesture(&meeting,KEY_ACTION_UP); CHECK(!stops && !online_stops);
        /* 在线会议由 APP 回复后引擎受理；桩函数不模拟该回复，这里显式推进状态。 */
        if(online) { status.run=RECORD_STATE_START; key_state=RDX_RECORD_KEY_ACTIVE; }
        down(&inspiration); CHECK(!key_record_owner_valid);
        if(online) {
            /* 引擎已受理的在线会议，再双击只做本地权威停止。 */
            gesture(&meeting,KEY_ACTION_DOUBLE_CLICK);
            CHECK(stops==1 && !online_stops && !starts);
            finish_stop();
        } else {
            /* 本地待开始时再次双击取消。 */
            gesture(&meeting,KEY_ACTION_DOUBLE_CLICK);
            CHECK(stops==1);
            gesture(&meeting,KEY_ACTION_DOUBLE_CLICK);
            CHECK(starts==1);
            finish_stop();
        }
        /* APP 发起的 CHAT 录音即使处于 PAUSE 仍是会议，F19 不能替换它。 */
        reset(); ready=1; status.run=RECORD_STATE_PAUSE;
        record_kind=RDX_RECORD_MEETING; key_state=RDX_RECORD_KEY_ACTIVE;
        down(&inspiration); CHECK(!starts && !online_starts);
        gesture(&meeting,KEY_ACTION_DOUBLE_CLICK); CHECK(stops==1 && !starts && !online_starts);
        finish_stop();
        for(int kind=RDX_RECORD_CALL;kind<=RDX_RECORD_FACTORY;++kind) {
            reset(); record_kind=kind; key_state=RDX_RECORD_KEY_ACTIVE; status.run=RECORD_STATE_START;
            gesture(&meeting,KEY_ACTION_DOUBLE_CLICK); down(&inspiration);
            CHECK(!stops && !starts && !online_starts && !online_stops);
        }
    }
    return 0;
}
int test_stop_retry_generation_and_transport_loss(void) {
    reset(); rdx_hogp_input_cycle_t c=cycle(1,0x6e,30);
    down(&c); stop_fail=1;
    gesture(&c,KEY_ACTION_UP); CHECK(key_record_session_active && key_record_retry_timer);
    down(&c); CHECK(starts==1);
    stop_fail=0; CHECK(rdx_app_local_key_cancel(&c) && stops==1);
    reset(); c=cycle(2,0x6e,31); down(&c);
    ++generation; status.run=RECORD_STATE_START;
    gesture(&c,KEY_ACTION_UP); CHECK(!stops); /* 旧 Up 不能停止后续 APP 或 DUT 发起的录音。 */
    reset(); ready=1; c=cycle(2,0x6e,33); down(&c);
    ++request_generation; key_state=RDX_RECORD_KEY_ACTIVE; status.run=RECORD_STATE_START;
    gesture(&c,KEY_ACTION_UP);
    CHECK(!release_calls && !online_stops && status.run==RECORD_STATE_START);
    /* 灵感录音是按住式纯推流，断连立即收尾；在线会议沿用 APP 路径，断连转离线继续落盘。 */
    {
        reset(); ready=1; c=cycle(0,0x6e,32);
        down(&c);
        ready=0; release_done=0; rdx_app_record_transport_lost();
        CHECK(key_record_disconnect_pending && key_record_retry_timer==0);
        rdx_app_key_record_pump(); CHECK(key_record_retry_timer && !starts);
        release_done=1; rdx_app_key_record_pump(); CHECK(!key_record_session_active);
        gesture(&c,KEY_ACTION_UP); CHECK(!stops && !starts);
        reset(); ready=1; c=cycle(0,0x6d,34);
        gesture(&c,KEY_ACTION_DOUBLE_CLICK); CHECK(online_starts==1);
        ready=0; rdx_app_record_transport_lost();
        CHECK(!key_record_disconnect_pending && !online_stops && !stops);
    }
    return 0;
}
int test_online_release_restores_business_without_reboot(void) {
    for(int next=0;next<4;++next) {
        reset(); ready=1; release_done=0; trigger_deferred=1;
        rdx_hogp_input_cycle_t c=cycle(4,0x6e,60);
        down(&c);
        CHECK(online_starts==1 && key_record_session_active && !key_record_local);
        status.run=RECORD_STATE_START; key_state=RDX_RECORD_KEY_ACTIVE; stream_active=1;
        gesture(&c,KEY_ACTION_UP);
        CHECK(release_calls==1 && !online_stops && key_record_retry_timer);
        CHECK(releasing && key_state==RDX_RECORD_KEY_STOPPING && key_record_owner_valid);
        rdx_app_key_record_pump();
        CHECK(release_calls==2 && !online_stops && key_record_session_active);
        release_done=1; rdx_app_key_record_pump();
        CHECK(release_calls==3 && online_stops==1 && key_record_stop_queued);
        CHECK(trigger_stop_pending && releasing && key_record_session_active);
        rdx_app_key_record_pump();
        CHECK(release_calls==3 && online_stops==1 && key_record_session_active);
        /* 模拟已排队 STOP 通知在 app_core 执行，而不是测试中重启或重置按键状态。 */
        rdx_record_stream_only_release_complete(); trigger_stop_pending=0;
        rdx_app_key_record_pump();
        CHECK(!key_record_session_active && !key_record_stop_queued && !key_record_retry_timer);
        CHECK(rdx_app_local_key_cancel(&c) && !key_record_owner_valid);
        rdx_hogp_input_cycle_t playback_key=cycle(2,0x6c,61);
        gesture(&playback_key,KEY_ACTION_CLICK); CHECK(playback==1);
        local_volume=8;
        rdx_hogp_input_cycle_t volume=cycle(2,0x6b,64);
        gesture(&volume,KEY_ACTION_CLICK); CHECK(local_volume==10);
        /* 同一归属在线重开还须等匹配 APP STOP 的独立退役确认；本地回放无需等待。 */
        rdx_hogp_input_cycle_t pending=cycle(4,0x6e,67);
        down(&pending);
        CHECK(online_starts==1 && !key_record_session_active && !key_record_pressed);
        retirement_pending=0; /* 模拟 APP STOP 经录音工作任务确认，真实栅栏由会话测试桩覆盖。 */
        if(next<2) {
            ready=next==0;
            rdx_hogp_input_cycle_t inspiration=cycle(4,0x6e,65);
            down(&inspiration);
            CHECK(key_record_session_active && key_record_local==!ready);
            CHECK(ready ? online_starts==2 : starts==1);
        } else {
            ready=next==2;
            rdx_hogp_input_cycle_t meeting=cycle(3,0x6d,66);
            gesture(&meeting,KEY_ACTION_DOUBLE_CLICK);
            CHECK(ready ? online_starts==2 : starts==1);
        }
    }
    return 0;
}
int test_mark_feedback_and_player_admission(void) {
    reset(); rdx_hogp_input_cycle_t f18=cycle(0,0x6d,40), f17=cycle(1,0x6c,41);
    status.run=RECORD_STATE_START; key_state=RDX_RECORD_KEY_ACTIVE;
    gesture(&f17,KEY_ACTION_CLICK); CHECK(!marks && !playback && battery_queries==1);
    gesture(&f18,KEY_ACTION_CLICK); CHECK(marks==1 && battery_queries==1);
    mark_fail=1; gesture(&f18,KEY_ACTION_CLICK); CHECK(marks==1 && battery_queries==2);
    mark_fail=0; status.run=RECORD_STATE_PAUSE;
    gesture(&f18,KEY_ACTION_CLICK); CHECK(marks==1 && battery_queries==3);
    reset(); f17=cycle(0,0x6c,42); ready=1;
    gesture(&f17,KEY_ACTION_CLICK); CHECK(playback && battery_queries==1);
    const int messages[]={APP_MSG_REC_PREV,APP_MSG_REC_NEXT,APP_MSG_REC_FR,APP_MSG_REC_FF,
        APP_MSG_REC_PLAY_TOGGLE,APP_MSG_LOCAL_VOL_UP,APP_MSG_LOCAL_VOL_DOWN};
    for(int i=0;i<7;++i) {
        for(int state=RDX_RECORD_KEY_START_PENDING;state<=RDX_RECORD_KEY_STOPPING;++state) {
            key_state=state; CHECK(rdx_app_local_player_message_blocked(messages[i]));
        }
        key_state=RDX_RECORD_KEY_IDLE; blocked=1; CHECK(rdx_app_local_player_message_blocked(messages[i]));
        blocked=0; tone_running=1; CHECK(rdx_app_local_player_message_blocked(messages[i])); tone_running=0;
    }
    return 0;
}
int test_nosave_meeting_f18_online_only(void) {
    for(int link=0;link<3;++link) {
        int online=link==2; /* No RDX, RDX not ready, ready receiver. */
        reset(); save_enabled=0; ready=link!=0; stream_ready=online; playback=1;
        rdx_hogp_input_cycle_t f18=cycle(0,0x6d,90);
        gesture(&f18,KEY_ACTION_DOUBLE_CLICK);
        if(!online) {
            /* RDX 未就绪：不开始、不落盘、不打断播放、无任何会话状态。 */
            CHECK(!starts && !online_starts && !storage_admits && playback && !playback_stops);
            CHECK(key_state==RDX_RECORD_KEY_IDLE && !key_record_session_active && !key_record_pressed);
            continue;
        }
        CHECK(online_starts==1 && !starts && !storage_admits && !key_record_local);
        CHECK(key_record_session_active && key_state==RDX_RECORD_KEY_START_PENDING);
        /* APP 受理后再次触发：按纯推流停止，不走本地落盘停止。 */
        status.run=RECORD_STATE_START; key_state=RDX_RECORD_KEY_ACTIVE; stream_active=1;
        gesture(&f18,KEY_ACTION_DOUBLE_CLICK);
        CHECK(release_calls>=1 && online_stops==1 && !stops);
    }
    /* APP 发起的不落盘会议：F18 同样必须能停止，不能被请求代次不符而静默吞掉。 */
    reset(); save_enabled=0; ready=1; status.run=RECORD_STATE_START; stream_active=1;
    key_state=RDX_RECORD_KEY_ACTIVE; record_kind=RDX_RECORD_MEETING; request_generation+=7;
    rdx_hogp_input_cycle_t f18=cycle(1,0x6d,91);
    gesture(&f18,KEY_ACTION_DOUBLE_CLICK);
    CHECK(release_calls>=1 && online_stops==1 && !stops);
    return 0;
}
int test_nosave_meeting_receiver_loss(void) {
    for(int app=0;app<2;++app) {
        reset(); save_enabled=0; ready=1;
        rdx_hogp_input_cycle_t c=cycle(0,0x6d,92);
        if(!app) gesture(&c,KEY_ACTION_DOUBLE_CLICK);
        else {
            stream_active=1; status.run=RECORD_STATE_PAUSE;
            key_state=RDX_RECORD_KEY_ACTIVE; record_kind=RDX_RECORD_MEETING;
            key_record_local=1; /* retired local key must not mask this APP session */
        }
        ready=stream_ready=0; release_done=0;
        rdx_app_record_receiver_lost();
        CHECK(key_record_disconnect_pending);
        rdx_app_key_record_pump(); CHECK(release_calls && key_record_retry_timer);
        release_done=1; rdx_app_key_record_pump();
        CHECK(!key_record_disconnect_pending && !key_record_session_active && !key_record_pressed);
        CHECK(!starts && !stops && !online_stops);
    }
    reset(); ready=1; stream_active=1; status.run=RECORD_STATE_START;
    key_state=RDX_RECORD_KEY_ACTIVE;
    rdx_app_record_receiver_lost(); CHECK(!key_record_disconnect_pending);
    return 0;
}
int test_full_storage_rejects_local_writes_only(void) {
    /* 预留区内：离线 F19/F18 与在线 F18（推流并落盘）在改动任何状态前拒绝。 */
    for(int case_id=0;case_id<3;++case_id) {
        reset(); storage_full=1; playback=1; ready=case_id==2;
        u8 usage=case_id==0?0x6e:0x6d;
        rdx_hogp_input_cycle_t c=cycle(case_id,usage,70+case_id);
        if(usage==0x6e) down(&c); else gesture(&c,KEY_ACTION_DOUBLE_CLICK);
        CHECK(storage_admits==1 && !starts && !online_starts && !playback_stops && playback);
        CHECK(key_state==RDX_RECORD_KEY_IDLE && status.run==RECORD_STATE_STOP);
        CHECK(!key_record_session_active && !key_record_pressed && !status.key_trigger);
        gesture(&c,KEY_ACTION_UP); CHECK(!stops && !online_stops && !release_calls);
    }
    /* 在线 F19 纯推流不落盘，不受存储预留门禁约束。 */
    reset(); storage_full=1; ready=1; rdx_hogp_input_cycle_t c=cycle(4,0x6e,80);
    down(&c);
    CHECK(!storage_admits && online_starts==1 && key_record_session_active && !key_record_local);
    return 0;
}
int test_device_record_set_admits_before_key_context(void) {
    const int kinds[]={RDX_RECORD_MEETING,RDX_RECORD_INSPIRATION,RDX_RECORD_MEETING,RDX_RECORD_INSPIRATION};
    const int routes[]={RECORD_MODE_OFFLINE,RECORD_MODE_OFFLINE,RECORD_MODE_ONLINE,RECORD_MODE_ONLINE};
    for(int i=0;i<4;++i) {
        reset(); storage_full=1; ready=1; playback=1;
        u32 req=request_generation, gen=generation;
        int online_f19=kinds[i]==RDX_RECORD_INSPIRATION && routes[i]==RECORD_MODE_ONLINE;
        int ret=rdx_app_device_record_set(RECORD_SCENE_CHAT,RECORD_STATE_START,routes[i],kinds[i]);
        CHECK(storage_admits==!online_f19);
        if(online_f19) {
            /* 纯推流 F19 不查询空间，照常预置并投递 APP 触发。 */
            CHECK(!ret && online_starts==1 && playback_stops==1 && request_generation==req+1);
            CHECK(key_state==RDX_RECORD_KEY_START_PENDING && record_kind==RDX_RECORD_INSPIRATION);
            continue;
        }
        /* 拒绝发生在 key_start_prepare、回放停止与触发投递之前。 */
        CHECK(ret==-1 && request_generation==req && generation==gen && key_state==RDX_RECORD_KEY_IDLE);
        CHECK(!playback_stops && playback && !online_starts && !starts && !status.key_trigger);
        CHECK(status.run==RECORD_STATE_STOP);
        storage_full=0;
        CHECK(!rdx_app_device_record_set(RECORD_SCENE_CHAT,RECORD_STATE_START,routes[i],kinds[i]));
        CHECK(storage_admits==2 && playback_stops==1 && (online_starts+starts)==1);
        /* STOP 不经过空间准入。 */
        storage_full=1;
        rdx_app_device_record_set(RECORD_SCENE_CHAT,RECORD_STATE_STOP,routes[i],kinds[i]);
        CHECK(storage_admits==2);
    }
    return 0;
}
int test_cdc_first_only_online_plain_f19(void) {
    for(int link=0;link<4;++link) {
        reset(); cdc_busy=1; ready=link>=1; stream_ready=link>=2;
        hid_ready=link==3; playback=1;
        rdx_hogp_input_cycle_t c=cycle(4,0x6e,200+link);
        down(&c);
        int admitted=ready && stream_ready;
        CHECK(online_starts==admitted && !starts && !storage_admits);
        CHECK(!!key_record_session_active==admitted && cdc_busy);
        CHECK(playback_stops==admitted && playback==!admitted);
        gesture(&c,KEY_ACTION_LONG); gesture(&c,KEY_ACTION_HOLD);
        CHECK(online_starts==admitted && !starts);
        gesture(&c,KEY_ACTION_UP);
        CHECK(cdc_busy && (!admitted || release_calls));
    }
    return 0;
}
int test_protection_and_physical_dut(void) {
    reset(); int *flags[]={&pc_mode,&formatting,&ota,&poweroff_ready_flag,
        &app_var.goto_poweroff_flag,&mode_switch_keep_timer,&factory_keys};
    for(int i=0;i<7;++i) {
        *flags[i]=1; rdx_hogp_input_cycle_t c=cycle(4,0x6e,50);
        down(&c); CHECK(!starts && !online_starts);
        *flags[i]=0;
    }
    for(int usage=0;usage<=0x73;++usage) {
        int msg=0; rdx_app_usage_key_remap(&msg,KEY_IO_NUM4,usage,KEY_ACTION_EIGHTUPLE_CLICK,1);
        CHECK(msg==APP_MSG_DUT);
        rdx_app_usage_key_remap(&msg,KEY_IO_NUM0,usage,KEY_ACTION_EIGHTUPLE_CLICK,1);
        CHECK(msg==APP_MSG_NULL);
    }
    rdx_dut_mode=1; rdx_hogp_input_cycle_t c=cycle(4,0x68,51);
    gesture(&c,KEY_ACTION_DOUBLE_CLICK); CHECK(dut_burnins);
    reset(); c=cycle(0,0x6e,52); status.process_state=REC_PROCESS_STATE_BUSY;
    down(&c); CHECK(!starts && !key_record_pressed);
    status.process_state=0; rdx_app_key_record_pump(); CHECK(!starts);
    return 0;
}
int test_rejected_f19_down_never_retries_on_long_or_hold(void) {
    for(int key=0;key<5;key++) {
        reset(); status.process_state=REC_PROCESS_STATE_BUSY;
        rdx_hogp_input_cycle_t c=cycle(key,0x6e,100+key);
        down(&c); CHECK(!starts && !online_starts && !key_record_owner_valid);
        status.process_state=0;
        gesture(&c,KEY_ACTION_LONG); gesture(&c,KEY_ACTION_HOLD);
        CHECK(!starts && !online_starts && !key_record_pressed);
    }
    return 0;
}
int test_dut_waits_for_owned_worker_tone_and_retirement(void) {
    for(int online=0;online<2;online++) {
        reset(); ready=online;
        trigger_deferred=online;
        rdx_hogp_input_cycle_t c=cycle(4,0x6e,110);
        down(&c); CHECK(key_record_owner_valid);
        u32 admitted_request=request_generation;
        stop_fail=!online; release_done=!online;
        CHECK(rdx_app_local_key_dut_ready(&c)==0 && !dut_entries);
        if(online) CHECK(request_generation==admitted_request+1 &&
            key_record_request_generation==request_generation);
        stop_fail=0; release_done=1;
        CHECK(rdx_app_local_key_dut_ready(&c)==0 && !dut_entries);
        if(online) {
            CHECK(trigger_stop_pending && retirement_pending);
            rdx_record_stream_only_release_complete();
            rdx_app_key_record_pump();
        }
        key_state=RDX_RECORD_KEY_IDLE; tone_running=1;
        CHECK(rdx_app_local_key_dut_ready(&c)==0 && !dut_entries);
        gesture(&c,KEY_ACTION_EIGHTUPLE_CLICK); CHECK(!dut_entries);
        tone_running=0;
        if(online) {
            CHECK(rdx_app_local_key_dut_ready(&c)==0 && !dut_entries);
            retirement_pending=0;
        }
        CHECK(rdx_app_local_key_dut_ready(&c)==1);
        gesture(&c,KEY_ACTION_EIGHTUPLE_CLICK); CHECK(dut_entries==1 && rdx_dut_mode);
        c.mode=RDX_LOCAL_KEY_DUT; down(&c); CHECK(online?online_starts==1:starts==1);
    }
    return 0;
}
int test_dut_never_stops_other_owners_or_bypasses_gates(void) {
    for(int kind=RDX_RECORD_MEETING;kind<=RDX_RECORD_FACTORY;kind++) {
        reset(); record_kind=kind; status.run=RECORD_STATE_START;
        key_state=RDX_RECORD_KEY_ACTIVE;
        rdx_hogp_input_cycle_t c=cycle(4,0x6e,120);
        CHECK(rdx_app_local_key_dut_ready(&c)<0 && !stops && !release_calls);
        gesture(&c,KEY_ACTION_EIGHTUPLE_CLICK); CHECK(!dut_entries);
    }
    reset(); rdx_hogp_input_cycle_t other=cycle(0,0x6e,121), c=cycle(4,0x6e,122);
    down(&other); CHECK(rdx_app_local_key_dut_ready(&c)<0 && !stops);
    reset(); down(&c); ++generation; status.run=RECORD_STATE_START;
    CHECK(rdx_app_local_key_dut_ready(&c)<0 && !stops);
    for(int online=0;online<2;online++) {
        reset(); ready=online; down(&c);
        gesture(&c,KEY_ACTION_UP);
        if(online) rdx_app_key_record_pump();
        if(online) ++request_generation; else ++generation;
        tone_running=1;
        CHECK(rdx_app_local_key_dut_ready(&c)<0);
        CHECK(online?release_calls==1:stops==1);
    }
    for(int gate=0;gate<5;gate++) {
        reset(); down(&c);
        if(gate==0) dut_disabled=1;
        if(gate==1) ota=1;
        if(gate==2) poweroff_ready_flag=1;
        if(gate==3) factory_keys=1;
        if(gate==4) blocked=1;
        CHECK(rdx_app_local_key_dut_ready(&c)<0 && !stops && !dut_entries);
    }
    reset(); dut_disabled=1; gesture(&c,KEY_ACTION_EIGHTUPLE_CLICK); CHECK(!dut_entries);
    return 0;
}
int test_dut_final_ticket_guard_and_existing_direct_exit_paths(void) {
    for(int race=0;race<3;race++) {
        reset();
        rdx_hogp_input_cycle_t c=cycle(4,0x6e,130+race);
        dut_ticket_pending=race!=0; dut_admission_race=race;
        upload_info.file_send_busy=race==2;
        gesture(&c,KEY_ACTION_EIGHTUPLE_CLICK);
        CHECK(!dut_entries && !rdx_dut_mode && !dut_ticket_pending);
    }
    reset(); dut_ticket_pending=0;
    int message=APP_MSG_DUT;
    rdx_app_msg_handler(&message); CHECK(dut_entries==1 && rdx_dut_mode);
    rdx_hogp_input_cycle_t c=cycle(4,0x6e,134);
    gesture(&c,KEY_ACTION_EIGHTUPLE_CLICK); CHECK(dut_entries==2 && !rdx_dut_mode);
    return 0;
}
'''


def main():
    check_seek_step()
    app = (BASE / 'rdx_app.c').read_text(encoding='utf-8')
    ble = (BASE / 'rdx_ble_server.c').read_text(encoding='utf-8')
    assert 'rdx_app_record_receiver_lost();' in function(ble, 'rdx_ble_server_phase2_rdx_write')
    keys = (BASE / 'rdx_key.c').read_text(encoding='utf-8')
    header = (ROOT / 'SDK/apps/earphone/include/app_msg.h').read_text(encoding='utf-8')
    begin = header.rfind('enum', 0, header.index('APP_MSG_NULL'))
    enums = header[begin:header.index('};', begin) + 2]
    kh = (ROOT / 'SDK/apps/common/device/key/key_driver.h').read_text(encoding='utf-8')
    enums += re.search(r'enum key_action \{.*?\};', kh, re.S)[0]
    enums += re.search(r'struct key_event \{.*?\};', kh, re.S)[0]
    headers = ''.join(re.sub(r'^#include[^\n]*', '', (BASE / name).read_text(encoding='utf-8'), flags=re.M)
                      for name in ('rdx_hogp_key_action.h', 'rdx_hogp_input.h'))
    tables = re.search(r'static const u8 key_table_usage.*?\n\};', keys, re.S)[0]
    tables += re.search(r'static u8 key_table_io_num4_dut.*?\n\};', keys, re.S)[0]
    helpers = ''.join(function(app, name) for name in (
        'rdx_app_key_record_retry_cb', 'rdx_app_key_record_retry_schedule',
        'rdx_app_key_record_retry_cancel', 'rdx_app_key_record_reset',
        'rdx_app_key_record_product_blocked',
        'rdx_app_local_key_cancel', 'rdx_app_local_key_mode', 'rdx_app_record_is_hold_active',
        'rdx_app_local_key_dut_ready',
        'rdx_app_key_click_cb', 'rdx_app_record_transport_lost',
        'rdx_app_record_receiver_lost', 'rdx_app_key_record_pump',
        'rdx_app_rdx_key_route_ready', 'rdx_app_meeting_record_toggle', 'rdx_app_usage_key_remap',
        'rdx_app_local_player_message_blocked', 'rdx_app_local_volume_change',
        'rdx_app_device_record_set'))
    handler = function(app, 'rdx_app_msg_handler')
    assert handler.index('rdx_app_local_player_message_blocked(msg[0])') < handler.index('switch (msg[0])')
    begin = handler.index('        case APP_MSG_RECORD_LOCAL_HOLD_START:')
    end = handler.index('        case APP_MSG_RECORD_SWITCH:', begin)
    cases = handler[begin:end]
    # 旧 KEY_POWER 控制不属于按键值绑定的测试范围。
    cases = cases[:cases.index('        case APP_MSG_RECORD_CHAT_MODE:')] + cases[cases.index('        case APP_MSG_RECORD_LOCAL_TOGGLE:'):]
    begin = handler.index('        case APP_MSG_LOCAL_VOL_UP:')
    cases += handler[begin:handler.index('        case APP_MSG_BT_CLOSE_PAGE_SCAN:', begin)]
    begin = handler.index('        case APP_MSG_REC_PREV:')
    cases += handler[begin:handler.index('        // OLED', begin)]
    cases += 'case APP_MSG_DUT_BURNIN_TOGGLE: rdx_dut_burnin_toggle(); return 1;\n'
    begin = handler.index('        case APP_MSG_DUT:')
    cases += handler[begin:handler.index('    #endif', begin)]
    playback = (BASE / 'rdx_playback.c').read_text(encoding='utf-8')
    program = (STUBS + enums + headers + '\n' + tables +
               function(keys, 'rdx_key_usage_message') + function(keys, 'rdx_key_get_io_num_table') +
               function((BASE / 'rdx_hogp_key_action.c').read_text(encoding='utf-8'),
                        'rdx_hogp_key_action_plain_usage') +
               'static rdx_hogp_input_cycle_t key_record_owner;\nstatic u8 key_record_owner_valid;\n'
               'static const rdx_hogp_input_cycle_t *local_key_dispatch_cycle;\n'
               '#define KEY_DRIVER_TYPE_IO 0\n'
               'u8 rdx_hogp_input_cycle_valid(const rdx_hogp_input_cycle_t *c) { return c->epoch==local_epoch; }\n'
               'u8 rdx_hogp_input_dut_pending(const rdx_hogp_input_cycle_t *c) { return dut_ticket_pending; }\n'
               'static void rdx_app_key_record_pump(void);\n'
               'static int rdx_app_device_record_set(u8,u8,u8,u8);\n' + helpers +
               function(playback, 'rdx_playback_can_start') +
               'static int rdx_app_msg_handler(int *msg) { int ret=0; '
               'if(rdx_app_local_player_message_blocked(msg[0])) return 1; switch(msg[0]) {\n' +
               cases + 'default: break; } return ret; }\n' +
               function(app, 'rdx_app_local_key_down') + function(app, 'rdx_app_local_key_gesture') + TESTS)
    with tempfile.TemporaryDirectory(prefix='usage-key-record-') as temp:
        path = Path(temp) / 'keys.c'
        path.write_text(program, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)
    from record_key_session_checks import main as check_sessions
    check_sessions()
    print('Usage dispatch: all keys, routes, F18 toggle/mark, F19 ownership, DUT and STOP retries passed.')


if __name__ == '__main__':
    main()
