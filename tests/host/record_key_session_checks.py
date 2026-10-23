"""执行录音旁路上下文、旧 RECORD 命令及排队触发请求的行为检查。"""
import re
import tempfile
from pathlib import Path
from host_c_test_lib import ROOT, function, run_c_checks

BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'

STUBS = r'''
typedef unsigned char u8;
typedef unsigned char uint8_t;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int bool;
#define true 1
#define false 0
#define TRUE true
#define FALSE false
#define NULL ((void *)0)
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
#define r_printf(...) ((void)0)
#define log_error(...) ((void)0)
#define g_printf(...) ((void)0)
#define y_printf(...) ((void)0)
#define log_info(...) ((void)0)
#define free(p) ((void)0)
#define RECORD_TASK_NAME "record"
#define RDX_RECORD_APP_STOP_FENCE 10
#define RDX_RECORD_HOLD_FENCE 11
#define RDX_LED_SCENE_RECORD_STOP 1
#define RECORD_CMD_DELAY_MS 2000
#define RECORD_CMD_MAX_RETRY 5
#define RDX_RECORD_STATE_BUSY_TIMEOUT 2000
#define RDX_BLE_LINK_INVALID_INDEX 255
#define RDX_APP_FILE_CMD_STOP 1
#define Q_CALLBACK 1
#define APP_MSG_REC_PLAY_TOGGLE 20
#define APP_MSG_REC_PREV 21
#define APP_MSG_REC_NEXT 22
#define APP_MSG_REC_FR 23
#define APP_MSG_REC_FF 24
#define APP_MSG_VOL_UP 25
#define APP_MSG_VOL_DOWN 26
#define APP_MSG_LOCAL_VOL_UP 27
#define APP_MSG_LOCAL_VOL_DOWN 28
#define TCFG_SD_ALWAY_ONLINE_ENABLE 1
#define TCFG_T2620_MEETING_MONO_DEBUG_MIC 1
typedef void (*RecUIFunction)(void);
typedef struct { u8 slot_index; u32 slot_generation, transport_epoch; } rdx_ble_async_token_t;
static int dut, factory, bound=1, storage_busy, ux_busy, stream_ready=1, live=1;
static int queue_fail, timer_fail, posts, replies, triggers, processes, files, saves, audio;
static u32 pending_binding_token=1;
static RecordStatus record_status;
static u32 record_session_generation=1, record_initialized_generation;
static u16 record_set_process_state_timer;
static int timeout_adds, timeout_deletes, timeout_live, timeout_delay;
static void (*timeout_callback)(void *);
static void *timeout_priv;
static int g_record_cmd_delay_timer, g_record_cmd_retry_cnt;
static u32 g_record_cmd_delay_generation;
static Record_info g_pending_record_info;
static rdx_ble_async_token_t g_pending_record_token, g_record_session_token;
static rdx_ble_async_token_t g_stream_only_start_token, g_app_stop_token;
static u8 g_pending_record_token_valid, g_record_session_token_valid;
static u8 g_stream_only_start_pending, g_stream_only_session_active;
static u8 g_stream_only_hold_released, g_stream_only_release_started, g_hold_release_posted;
static u32 g_hold_released_ms;
static u32 g_hold_release_ticket, g_hold_release_done, g_app_stop_ticket, g_app_stop_done;
static int g_app_stop_pending, g_app_stop_posted, g_app_stop_close, g_app_stop_timer;
static int record_start_tone_pending, is_record_task_created=1;
static bool record_tone_session_active;
static u32 record_start_tone_epoch;
static int closes, finishes;
static int stream_resume_timer, g_stream_resume_token_valid;
static rdx_ble_async_token_t owner={0,1,1};
static void noop(void) {}
static RecordStatus *rdx_record_get_status(void) { return &record_status; }
static int rdx_app_get_dut_status(void) { return dut; }
static int rdx_factory_audio_owned(void) { return factory; }
static int rdx_ble_server_is_stream_tx_ready(void) { return stream_ready; }
static int rdx_ble_session_rdx_token_resolve(const rdx_ble_async_token_t *t,int ready) {
    return live && t && t->slot_index==owner.slot_index &&
        t->slot_generation==owner.slot_generation && t->transport_epoch==owner.transport_epoch;
}
static int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t,int ready) {
    *t=owner; return live;
}
static int rdx_record_binding_allowed(void) { return bound; }
static int rdx_record_binding_token_is_current(u32 t) { return bound && t==1; }
static u32 rdx_record_binding_token_capture(void) { return bound; }
static int rdx_storage_lifecycle_business_blocked(void) { return storage_busy; }
static int rdx_record_format_download_conflict(const char *op) { return storage_busy; }
static int rdx_record_state_is_publishable(void) { return 1; }
static void rdx_protocol_record_state_indicate(void) { ++replies; }
static void rdx_record_state_indicate_if_current(const rdx_ble_async_token_t *t) { ++replies; }
void rdx_record_set_process_state_ready(void);
static void rdx_peripheral_power_vdd_business_changed_notify(void) {}
static void rdx_record_clear_marks(void) {}
static void rdx_record_process(void) { ++processes; }
static void rdx_app_emmc_poweron(int x) {}
static int rdx_record_uxfile_is_busy(void) { return ux_busy; }
static int pause_timeout_starts, pause_timeout_stops;
static void rdx_record_pause_timeout_stop(void) { ++pause_timeout_stops; }
static void rdx_record_pause_timeout_start(void) { ++pause_timeout_starts; }
static void rdx_record_max_timer_stop(void) {}
static void rdx_record_max_timer_start(void) {}
static void rdx_record_set_filter_cnt(int n) {}
static int rdx_record_get_filter_cnt(void) { return 10; }
static int tone_player_runing(void) { return 0; }
static u16 rdx_ble_server_get_conn_handle(void) { return live?1:0xffff; }
static u32 now_ms=100;
static u32 jiffies_msec(void) { return now_ms; }
static int sys_timeout_add(void *p,void (*cb)(void *),int ms) {
    if(timer_fail) return 0;
    ++timeout_adds; timeout_live=1; timeout_delay=ms;
    timeout_callback=cb; timeout_priv=p;
    return 1;
}
static int sys_timer_add(void *p,void (*cb)(void *),int ms) { return timer_fail?0:2; }
static void sys_timeout_del(int t) { ++timeout_deletes; timeout_live=0; }
static void sys_timer_del(int t) {}
static int os_taskq_post_msg(const char *name,int n,...) { if(queue_fail) return -1; ++posts; return 0; }
static int os_taskq_post_type(const char *name,int q,int n,int *msg) { return queue_fail?-1:0; }
static void rdx_record_start_tone_cancel(void) { record_start_tone_pending=0; record_status.run=RECORD_STATE_STOP; }
static void rdx_app_record_state_upload_timer_stop(void);
static void rdx_led_ctrl_set_scene(int scene) {}
static void translation_ear_recoder_close_all(void) { ++closes; }
static int rdx_uxfile_finish_record(void) { ++finishes; return 0; }
static void rdx_record_stop_tone_post(u32 epoch) {}
static void rdx_ble_server_auto_shut_down_enable(int en) {}
static void rdx_protocol_record_trigger_indicate(RecordStatus *p,int factor) { ++triggers; }
static int rdx_record_session_allowed(void) { return bound; }
typedef struct { int file_send_busy; } ReqFileInfo;
static ReqFileInfo file_info;
static ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) { return &file_info; }
static void rdx_protocol_file_cmd_handle(int x) {}
static void rdx_uxfile_operate_file_init(void) {}
static void rdx_app_emmc_poweroff_check_timer_stop(void) {}
/* 空间准入可控：admit 为 app_core 受理门禁，check_start 为 sink 初始化纵深防御。 */
static int admit_ret, admit_calls, space_ret;
static int rdx_record_storage_admit(void) { ++admit_calls; return admit_ret; }
struct tone_files { const char *no_storage; };
static const struct tone_files tones={"no_sd"};
static const struct tone_files *get_tone_files(void) { return &tones; }
static int full_tones;
static int play_tone_file(const char *name) { full_tones+=!strcmp(name,"no_sd"); return 0; }
static int rdx_record_storage_check_start(void) { return space_ret; }
static int rdx_uxfile_dat_1_gen(u8 scene) { ++files; return 0; }
static void rdx_record_init_raw_metadata(int format) {}
static int rdx_uxfile_get_operateFile_info(void) { return 0; }
static int rdx_record_format_begin(int file,int format) { return 0; }
static int rdx_record_storage_begin(u32 generation,int scene,int format) { return 0; }
static int rdx_protocol_audio_data_indicate(u8 *d,u32 n) { ++audio; return 0; }
static void rdx_factory_audio_frame(u8 *d,u32 n) {}
static int rdx_record_storage_push(u8 *d,u32 n) { ++saves; return 0; }
'''

TESTS = r'''
static void reset(void) {
    record_status=(RecordStatus){0}; record_status.run=RECORD_STATE_STOP;
    record_status.ui_notify=noop; record_status.process_state=REC_PROCESS_STATE_READY;
    record_key_context.state=RDX_RECORD_KEY_IDLE; record_key_context.fixed=0;
    g_stream_only_hold_released=g_stream_only_release_started=g_stream_only_start_pending=0;
    g_stream_only_session_active=g_record_session_token_valid=g_pending_record_token_valid=0;
    g_record_cmd_delay_timer=g_app_stop_pending=g_app_stop_posted=g_app_stop_timer=0;
    record_start_tone_pending=queue_fail=timer_fail=posts=replies=triggers=processes=files=saves=audio=0;
    record_set_process_state_timer=timeout_adds=timeout_deletes=timeout_live=timeout_delay=0;
    timeout_callback=0; timeout_priv=0;
    g_hold_release_posted=g_hold_release_ticket=g_hold_release_done=0;
    closes=finishes=record_tone_session_active=0; is_record_task_created=1;
    admit_ret=admit_calls=space_ret=pause_timeout_starts=pause_timeout_stops=full_tones=0;
    dut=factory=storage_busy=ux_busy=0; bound=live=stream_ready=1;
    owner=(rdx_ble_async_token_t){0,1,1}; record_initialized_generation=0; now_ms=100;
}
static void cmd(u8 state) {
    Record_info info={state+0x30,'1','0'};
    rdx_record_cmd_handle_internal(&info,&owner);
}
static void drain_release(void) {
    worker_release(g_hold_release_ticket);
    rdx_record_stream_only_release();
    rdx_record_stream_only_release_complete();
}
static void drain_stop_reply(void) {
    g_app_stop_done=g_app_stop_ticket;
    rdx_record_app_stop_pump((void *)g_app_stop_ticket);
}
int test_release_poll_keeps_worker_ready_and_retires_fence(void) {
    for(int pending=0;pending<2;++pending) for(int fail=0;fail<2;++fail) {
        reset(); CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        u32 old=rdx_record_key_request_generation();
        if(!pending) {
            cmd(RECORD_STATE_START);
            CHECK(!rdx_record_run_init() && !files && rdx_record_stream_only_session_is_active());
        }
        queue_fail=fail;
        CHECK(!rdx_record_stream_only_release());
        CHECK(!rdx_record_key_request_is_current(old) && !g_stream_only_start_pending);
        CHECK(record_status.run==RECORD_STATE_STOP && record_status.process_state==REC_PROCESS_STATE_BUSY);
        u32 ticket=g_hold_release_ticket;
        CHECK(g_hold_release_posted==!fail && rdx_record_stream_only_is_releasing());
        CHECK(rdx_record_process_is_busy_check() && record_set_process_state_timer);
        CHECK(timeout_delay==RDX_RECORD_STATE_BUSY_TIMEOUT && timeout_adds==1);
        queue_fail=0;
        CHECK(!rdx_record_stream_only_release());
        CHECK(g_hold_release_ticket==ticket && g_hold_release_posted && posts==1);
        CHECK(rdx_record_process_is_busy_check() && timeout_adds==1);
        worker_release(ticket);
        CHECK(closes==1 && finishes==1 && !files && !saves);
        CHECK(g_hold_release_done==ticket && !rdx_record_stream_only_session_is_active());
        CHECK(record_status.process_state==REC_PROCESS_STATE_READY);
        CHECK(!record_set_process_state_timer && !timeout_live && timeout_deletes==1);
        /* app_core 在录音工作任务发布 READY 状态与确认后才轮询。 */
        CHECK(rdx_record_stream_only_release());
        CHECK(record_status.process_state==REC_PROCESS_STATE_READY);
        CHECK(!rdx_record_process_is_busy_check() && !record_set_process_state_timer && timeout_adds==1);
        CHECK(rdx_record_stream_only_release() && posts==1);
        rdx_record_stream_only_release_complete();
        CHECK(!rdx_record_stream_only_is_releasing() && rdx_record_key_state_get()==RDX_RECORD_KEY_IDLE);
        /* 仅 START 尚未生效即被取消时才设迟到回复栅栏。 */
        CHECK(g_stream_only_hold_released==pending && !rdx_record_process_is_busy_check());
        const int messages[]={APP_MSG_REC_PLAY_TOGGLE,APP_MSG_REC_PREV,APP_MSG_REC_NEXT,
            APP_MSG_REC_FR,APP_MSG_REC_FF,APP_MSG_LOCAL_VOL_UP,APP_MSG_LOCAL_VOL_DOWN};
        for(int message=0;message<7;++message) {
            CHECK(!rdx_app_local_player_message_blocked(messages[message]));
        }
        if(!pending) {
            /* START 已生效：APP 对设备侧停止不回 STOP，在线会议、灵感与 APP 开录须立即可用。 */
            for(int kind=RDX_RECORD_MEETING;kind<=RDX_RECORD_INSPIRATION;++kind) {
                CHECK(rdx_record_key_start_prepare(kind,RECORD_MODE_ONLINE,&owner));
                rdx_record_key_start_abort();
            }
            cmd(RECORD_STATE_START);
            CHECK(record_status.run==RECORD_STATE_START && !rdx_record_stream_only_session_is_active());
            CHECK(rdx_record_kind_get()==RDX_RECORD_MEETING);
            continue;
        }
        /* 尚未收到 STOP 回复：本地业务与回放已可准入，仅对该协议会话保留栅栏。 */
        CHECK(!rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        CHECK(!rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_OFFLINE,NULL));
        rdx_record_key_start_abort();
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_OFFLINE,NULL));
        rdx_record_key_start_abort();
        rdx_record_stream_only_start_arm(&owner);
        CHECK(!g_stream_only_start_pending);
        cmd(RECORD_STATE_START);
        CHECK(!rdx_record_stream_only_session_is_active() && record_status.run==RECORD_STATE_STOP);
        cmd(RECORD_STATE_STOP);
        CHECK(g_app_stop_pending && record_status.process_state==REC_PROCESS_STATE_BUSY);
        /* 看门狗回调不能证明待完成的 APP STOP 已由录音工作任务处理完毕。 */
        rdx_record_process_state_set_timer_cb(NULL);
        CHECK(record_status.process_state==REC_PROCESS_STATE_BUSY && g_stream_only_hold_released);
        drain_stop_reply();
        CHECK(!g_stream_only_hold_released && !g_app_stop_pending);
        CHECK(!rdx_record_process_is_busy_check() && !record_set_process_state_timer);
        for(int kind=RDX_RECORD_MEETING;kind<=RDX_RECORD_INSPIRATION;++kind) {
            CHECK(rdx_record_key_start_prepare(kind,RECORD_MODE_ONLINE,&owner));
            rdx_record_key_start_abort();
        }
        /* 过期的 APP 收尾轮询不能清除新会话的 BUSY 状态。 */
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START); rdx_record_set_process_state_busy();
        rdx_record_app_stop_pump((void *)g_app_stop_ticket);
        CHECK(record_status.run==RECORD_STATE_START && record_status.process_state==REC_PROCESS_STATE_BUSY);
        CHECK(rdx_record_key_state_get()==RDX_RECORD_KEY_ACTIVE);
        rdx_record_set_process_state_ready();
    }
    return 0;
}
int test_released_hold_fence_expires(void) {
    reset(); CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
    CHECK(!rdx_record_stream_only_release()); drain_release();
    CHECK(g_stream_only_hold_released);
    now_ms+=RDX_RECORD_HOLD_LATE_REPLY_MS-1;
    cmd(RECORD_STATE_START);
    CHECK(record_status.run==RECORD_STATE_STOP && replies==1);
    CHECK(!rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
    /* 窗口到期后栅栏失效，不会让整个 RDX 会话的在线开录永久被拒。 */
    ++now_ms;
    CHECK(rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
    CHECK(!g_stream_only_hold_released);
    rdx_record_key_start_abort();
    cmd(RECORD_STATE_START);
    CHECK(record_status.run==RECORD_STATE_START && rdx_record_kind_get()==RDX_RECORD_MEETING);
    return 0;
}
int test_local_stop_fence_and_owner(void) {
    const int states[]={RECORD_STATE_START,RECORD_STATE_RESUME,RECORD_STATE_PAUSE};
    for(int state=0;state<3;++state) for(int peer=0;peer<3;++peer) {
        reset(); g_record_session_token=owner; g_record_session_token_valid=peer!=2;
        record_status.run=states[state]; timer_fail=1;
        CHECK(!rdx_record_audio_fault_stop());
        CHECK(record_status.run==states[state] && !g_app_stop_pending);
        timer_fail=0; queue_fail=1;
        CHECK(rdx_record_audio_fault_stop() && g_app_stop_pending && !posts && !replies);
        u32 ticket=g_app_stop_ticket;
        CHECK(rdx_record_audio_fault_stop() && g_app_stop_ticket==ticket);
        queue_fail=0; rdx_record_app_stop_pump((void *)ticket);
        CHECK(posts==1 && !replies && g_app_stop_pending);
        rdx_record_app_stop_pump((void *)ticket); CHECK(posts==1 && !replies);
        if(peer==1) ++owner.transport_epoch;
        if(peer==2) live=0;
        /* 看门狗超时不能替代录音工作任务对 STOP 的确认。 */
        rdx_record_process_state_set_timer_cb(NULL);
        CHECK(rdx_record_process_is_busy_check());
        drain_stop_reply();
        CHECK(!g_app_stop_pending && replies==(peer==0));
        CHECK(!rdx_record_process_is_busy_check() && !record_set_process_state_timer);
        rdx_record_app_stop_pump((void *)ticket);
        CHECK(posts==1 && replies==(peer==0));
    }
    return 0;
}
int test_online_meeting_saves_but_inspiration_only_streams(void) {
    u8 data=0;
    for(int kind=RDX_RECORD_MEETING;kind<=RDX_RECORD_INSPIRATION;++kind) {
        reset(); int stream_only=kind==RDX_RECORD_INSPIRATION;
        if(stream_only) CHECK(rdx_record_key_start_prepare(kind,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START);
        CHECK(rdx_record_stream_only_session_is_active()==stream_only);
        CHECK(rdx_record_kind_get()==kind && rdx_record_key_state_get()==RDX_RECORD_KEY_ACTIVE);
        CHECK(!rdx_record_run_init() && files==!stream_only);
        CHECK(record_status.orig_mode==RECORD_MODE_ONLINE);
        CHECK(!rdx_record_run_data_handle(&data,1) && audio==1 && saves==!stream_only);
        cmd(RECORD_STATE_PAUSE); CHECK(record_status.run==RECORD_STATE_PAUSE);
        CHECK(!rdx_record_key_start_prepare(kind,RECORD_MODE_ONLINE,&owner));
        u32 generation=record_session_generation;
        cmd(RECORD_STATE_START); CHECK(record_session_generation==generation);
        cmd(RECORD_STATE_RESUME); CHECK(record_status.run==RECORD_STATE_RESUME);
        CHECK(rdx_record_kind_get()==kind && rdx_record_stream_only_session_is_active()==stream_only);
        CHECK(!rdx_record_run_init() && files==!stream_only);
        if(stream_only) { CHECK(!rdx_record_stream_only_release()); drain_release(); }
        else { cmd(RECORD_STATE_STOP); drain_stop_reply(); }
        CHECK(files==!stream_only && saves==!stream_only);
    }
    return 0;
}
int test_local_route_frozen_through_init_and_commands(void) {
    u8 data=0;
    for(int kind=RDX_RECORD_MEETING;kind<=RDX_RECORD_INSPIRATION;++kind) {
        reset(); live=0;
        CHECK(rdx_record_key_start_prepare(kind,RECORD_MODE_OFFLINE,NULL));
        record_status.run=RECORD_STATE_START; record_status.scene=RECORD_SCENE_CHAT;
        rdx_record_prepare_new_session();
        live=1; CHECK(rdx_record_online_session_bind_current());
        CHECK(g_record_session_token_valid); /* 只绑定控制与状态，不改变路由。 */
        record_start_tone_pending=1;
        rdx_record_on_ble_conn_changed(0);
        CHECK(record_start_tone_pending && record_status.run==RECORD_STATE_START);
        record_start_tone_pending=0;
        CHECK(rdx_record_online_session_bind_current());
        CHECK(!rdx_record_run_init() && files==1 && record_status.orig_mode==RECORD_MODE_OFFLINE);
        CHECK(!rdx_record_run_data_handle(&data,1) && saves==1 && !audio);
        cmd(RECORD_STATE_PAUSE); cmd(RECORD_STATE_RESUME);
        CHECK(g_record_session_token_valid);
        CHECK(!rdx_record_run_init() && files==1 && record_status.orig_mode==RECORD_MODE_OFFLINE);
        CHECK(!rdx_record_run_data_handle(&data,1) && saves==2 && !audio);
    }
    return 0;
}
int test_guards_tokens_and_queued_start(void) {
    reset(); rdx_ble_async_token_t stale=owner; ++stale.transport_epoch;
    CHECK(!rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&stale));
    stream_ready=0; CHECK(!rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
    stream_ready=1; CHECK(rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
    rdx_app_record_trigger_request_t request={0};
    request.token=owner; request.binding_token=1; request.stream_only=1;
    request.status.run=RECORD_STATE_START;
    request.request_generation=rdx_record_key_request_generation();
    rdx_record_stream_only_release();
    rdx_app_record_trigger_on_app_core(&request); CHECK(!triggers);
    drain_release(); ++owner.transport_epoch;
    CHECK(rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
    rdx_app_record_trigger_on_app_core(&request); CHECK(!triggers);
    request.token=owner; request.request_generation=rdx_record_key_request_generation();
    rdx_app_record_trigger_on_app_core(&request); CHECK(triggers==1);
    rdx_record_key_start_abort();
    for(int guard=0;guard<8;++guard) {
        reset();
        if(guard==0) record_status.run=RECORD_STATE_PAUSE;
        if(guard==1) record_status.process_state=REC_PROCESS_STATE_BUSY;
        if(guard==2) g_app_stop_pending=1;
        if(guard==3) g_record_cmd_delay_timer=1;
        if(guard==4) bound=0;
        if(guard==5) storage_busy=1;
        if(guard==6) dut=1;
        if(guard==7) factory=1;
        CHECK(!rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_OFFLINE,NULL));
    }
    return 0;
}
int test_app_replacement_retires_old_key_request(void) {
    reset(); CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
    cmd(RECORD_STATE_START);
    u32 old=rdx_record_key_request_generation();
    cmd(RECORD_STATE_STOP); CHECK(!rdx_record_key_request_is_current(old));
    drain_stop_reply();
    cmd(RECORD_STATE_START);
    CHECK(record_status.run==RECORD_STATE_START && rdx_record_kind_get()==RDX_RECORD_MEETING);
    CHECK(!rdx_record_stream_only_session_is_active() && !rdx_record_key_request_is_current(old));
    return 0;
}
int test_queued_delay_cannot_resurrect_cancelled_start(void) {
    reset(); CHECK(rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
    stream_ready=0; cmd(RECORD_STATE_START);
    CHECK(g_record_cmd_delay_timer && g_pending_record_token_valid);
    u32 old=g_record_cmd_delay_generation;
    CHECK(!rdx_record_stream_only_release());
    CHECK(!g_record_cmd_delay_timer && !g_pending_record_token_valid);
    stream_ready=1; rdx_record_cmd_delay_cb((void *)old);
    CHECK(!processes && !files && record_status.run==RECORD_STATE_STOP);
    drain_release(); cmd(RECORD_STATE_STOP); drain_stop_reply();
    CHECK(rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
    stream_ready=0; cmd(RECORD_STATE_START);
    CHECK(g_record_cmd_delay_generation!=old);
    stream_ready=1; rdx_record_cmd_delay_cb((void *)old);
    CHECK(!processes && g_record_cmd_delay_timer && g_pending_record_token_valid);
    rdx_record_cmd_delay_cb((void *)g_record_cmd_delay_generation);
    CHECK(processes==1 && rdx_record_stream_only_session_is_active());
    CHECK(!rdx_record_run_init() && !files);
    return 0;
}
typedef struct { u32 request_generation, record_generation; u8 kind, route, state, fixed; } key_snapshot_t;
static key_snapshot_t key_snapshot(void) {
    key_snapshot_t k={record_key_context.request_generation,record_key_context.record_generation,
        record_key_context.kind,record_key_context.route,record_key_context.state,record_key_context.fixed};
    return k;
}
static int key_snapshot_equal(key_snapshot_t a,key_snapshot_t b) {
    return a.request_generation==b.request_generation && a.record_generation==b.record_generation &&
        a.kind==b.kind && a.route==b.route && a.state==b.state && a.fixed==b.fixed;
}
int test_full_storage_start_rejected_before_mutation(void) {
    for(int err=0;err<2;++err) for(int with_token=0;with_token<2;++with_token) {
        reset(); admit_ret=err?-1:-2;
        u32 generation=record_session_generation; key_snapshot_t key=key_snapshot();
        Record_info info={RECORD_STATE_START+0x30,'1','0'};
        rdx_record_cmd_handle_internal(&info,with_token?&owner:NULL);
        /* 拒绝发生在任何会话、按键上下文、提示音、录音进程变更之前。 */
        CHECK(admit_calls==1 && record_status.run==RECORD_STATE_STOP);
        CHECK(record_session_generation==generation && key_snapshot_equal(key,key_snapshot()));
        CHECK(!processes && !record_start_tone_pending && !files && !g_record_session_token_valid);
        CHECK(!g_record_cmd_delay_timer && !timeout_adds && !g_app_stop_pending);
        CHECK(replies==with_token); /* 仅向当前 RDX 归属回一次状态。 */
        CHECK(full_tones==!err); /* 仅触及预留时报存储不足提示音。 */
        /* 空间恢复后同一入口正常受理。 */
        admit_ret=0; rdx_record_cmd_handle_internal(&info,with_token?&owner:NULL);
        CHECK(admit_calls==2 && record_status.run==RECORD_STATE_START && processes==1);
        CHECK(record_session_generation==generation+1);
        /* 重复 START、PAUSE、STOP 均不再查询空间。 */
        admit_ret=-2; rdx_record_cmd_handle_internal(&info,with_token?&owner:NULL);
        CHECK(admit_calls==2 && record_status.run==RECORD_STATE_START && processes==1);
        info.cmd=RECORD_STATE_PAUSE+0x30; rdx_record_cmd_handle_internal(&info,with_token?&owner:NULL);
        CHECK(admit_calls==2 && record_status.run==RECORD_STATE_PAUSE);
        info.cmd=RECORD_STATE_STOP+0x30; rdx_record_cmd_handle_internal(&info,with_token?&owner:NULL);
        CHECK(admit_calls==2);
        if(with_token) drain_stop_reply();
    }
    return 0;
}
int test_busy_uxfile_defers_before_space_query(void) {
    reset(); ux_busy=1; admit_ret=-2;
    u32 generation=record_session_generation; key_snapshot_t key=key_snapshot();
    cmd(RECORD_STATE_START);
    CHECK(!admit_calls && g_record_cmd_delay_timer && g_pending_record_token_valid && !replies);
    rdx_record_cmd_delay_cb((void *)g_record_cmd_delay_generation);
    CHECK(!admit_calls && g_record_cmd_delay_timer); /* 仍忙：重试，不查询。 */
    ux_busy=0; rdx_record_cmd_delay_cb((void *)g_record_cmd_delay_generation);
    CHECK(admit_calls==1 && record_status.run==RECORD_STATE_STOP && replies==1);
    CHECK(!g_record_cmd_delay_timer && !g_pending_record_token_valid && !processes);
    CHECK(record_session_generation==generation && key_snapshot_equal(key,key_snapshot()));
    return 0;
}
int test_full_storage_resume_keeps_pause(void) {
    for(int err=0;err<2;++err) {
        reset(); cmd(RECORD_STATE_START); CHECK(admit_calls==1 && record_status.run==RECORD_STATE_START);
        now_ms=200; cmd(RECORD_STATE_PAUSE);
        CHECK(record_status.run==RECORD_STATE_PAUSE && record_status.pause_start_ms==200);
        int starts=pause_timeout_starts, stops=pause_timeout_stops, procs=processes;
        u32 generation=record_session_generation; key_snapshot_t key=key_snapshot();
        now_ms=500; admit_ret=err?-1:-2; cmd(RECORD_STATE_RESUME);
        CHECK(admit_calls==2 && record_status.run==RECORD_STATE_PAUSE && replies==1);
        CHECK(full_tones==!err && !record_start_tone_pending);
        CHECK(record_status.pause_start_ms==200 && !record_status.paused_accumulated_ms);
        CHECK(pause_timeout_starts==starts && pause_timeout_stops==stops && processes==procs);
        CHECK(record_session_generation==generation && key_snapshot_equal(key,key_snapshot()));
        admit_ret=0; cmd(RECORD_STATE_RESUME);
        CHECK(admit_calls==3 && record_status.run==RECORD_STATE_RESUME && processes==procs+1);
        CHECK(record_status.paused_accumulated_ms==300 && !record_status.pause_start_ms);
        CHECK(record_session_generation==generation);
        cmd(RECORD_STATE_STOP); drain_stop_reply(); CHECK(admit_calls==3);
    }
    return 0;
}
int test_stream_only_never_queries_space(void) {
    reset(); admit_ret=-2;
    CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
    cmd(RECORD_STATE_START);
    CHECK(rdx_record_stream_only_session_is_active() && record_status.run==RECORD_STATE_START);
    CHECK(!rdx_record_run_init() && !files);
    cmd(RECORD_STATE_PAUSE); cmd(RECORD_STATE_RESUME);
    CHECK(record_status.run==RECORD_STATE_RESUME && !admit_calls && !full_tones);
    CHECK(!rdx_record_stream_only_release()); drain_release(); CHECK(!admit_calls);
    return 0;
}
int test_run_init_propagates_reserve(void) {
    reset(); cmd(RECORD_STATE_START);
    space_ret=-2; CHECK(rdx_record_run_init()==-2 && !files);
    space_ret=-1; CHECK(rdx_record_run_init()==-1 && !files);
    space_ret=0; CHECK(!rdx_record_run_init() && files==1);
    return 0;
}
'''


def main():
    record = (BASE / 'rdx_record.c').read_text(encoding='utf-8')
    header = (BASE / 'rdx_record.h').read_text(encoding='utf-8')
    app = (BASE / 'rdx_app.c').read_text(encoding='utf-8')
    types = '\n'.join(line for line in header.splitlines()
                      if re.match(r'#define RECORD_(STATE|MODE|SCENE|FORMATE)_', line)) + '\n'
    for pattern in (r'enum rdx_record_kind \{.*?\};', r'enum rdx_record_key_state \{.*?\};',
                    r'typedef enum \{[^}]*\} RecProcessState;',
                    r'typedef struct \{[^}]*\} RecordStatus;', r'typedef struct \{[^}]*\} Record_info;'):
        types += re.search(pattern, header, re.S)[0] + '\n'
    prefix = STUBS[:STUBS.index('static int dut,')]
    source = prefix + types + STUBS[STUBS.index('static int dut,'):]
    source += re.search(r'#define RDX_RECORD_HOLD_LATE_REPLY_MS.*', record)[0] + '\n'
    source += re.search(r'static struct \{[^}]*\} record_key_context;', record, re.S)[0]
    source += '\nstatic u8 rdx_record_token_equal(const rdx_ble_async_token_t *,const rdx_ble_async_token_t *);\n'
    source += 'static u8 rdx_record_hold_fence_blocks(const rdx_ble_async_token_t *);\n'
    source += 'void rdx_record_stream_only_start_arm(const rdx_ble_async_token_t *);\n'
    source += 'static void rdx_record_app_stop_request(const rdx_ble_async_token_t *);\n'
    source += 'static void rdx_record_cmd_handle_internal(Record_info *,const rdx_ble_async_token_t *);\n'
    for name in (
        'rdx_record_process_state_timer_stop', 'rdx_record_process_state_set_timer_cb',
        'rdx_record_process_is_busy_check', 'rdx_record_set_process_state_busy',
        'rdx_record_set_process_state_ready',
        'rdx_record_local_route_is_fixed', 'rdx_record_kind_get', 'rdx_record_key_state_get',
        'rdx_record_key_request_generation', 'rdx_record_key_request_is_current',
        'rdx_record_key_start_prepare', 'rdx_record_key_start_abort',
        'rdx_record_stream_only_token_capture', 'rdx_record_rdx_token_is_current',
        'rdx_record_token_equal', 'rdx_record_hold_fence_arm',
        'rdx_record_hold_fence_blocks', 'rdx_record_stream_only_start_arm',
        'rdx_record_stream_only_start_cancel', 'rdx_record_stream_only_session_is_active',
        'rdx_record_stream_only_start_consume', 'rdx_record_online_session_clear',
        'rdx_record_online_session_bind', 'rdx_record_online_session_is_current',
        'rdx_record_online_session_accepts', 'rdx_record_online_session_bind_current',
        'rdx_record_pending_cmd_clear', 'rdx_record_start_uses_local_storage',
        'rdx_record_local_storage_admit',
        'rdx_record_format_for_session', 'rdx_record_prepare_new_session',
        'rdx_record_app_stop_pump', 'rdx_record_app_stop_tick', 'rdx_record_app_stop_request',
        'rdx_record_audio_fault_stop', 'rdx_record_stream_only_release',
        'rdx_record_stream_only_release_complete', 'rdx_record_stream_only_is_releasing',
        'rdx_record_cmd_delay_cb', 'rdx_record_cmd_handle_internal',
        'rdx_record_update_connection_mode', 'rdx_record_on_ble_conn_changed',
        'rdx_record_run_init', 'rdx_record_run_data_handle',
        'rdx_record_stop_session_cleanup'):
        source += function(record, name)
    hold_worker = re.search(r'if \(msg\[1\] == RDX_RECORD_HOLD_FENCE\) \{(.*?)\n\s*continue;\n\s*\}',
                            function(record, 'rdx_record_task'), re.S)[1]
    source += '\nstatic void worker_release(u32 ticket) { int msg[4]={0,RDX_RECORD_HOLD_FENCE,(int)ticket};\n' + hold_worker + '\n}\n'
    source += '\nstatic void rdx_app_record_state_upload_timer_stop(void) { rdx_record_stream_only_start_cancel(); }\n'
    source += re.search(r'typedef struct \{[^}]*\} rdx_app_record_trigger_request_t;', app, re.S)[0]
    source += function(app, 'rdx_app_record_trigger_on_app_core')
    source += function(app, 'rdx_app_local_player_message_blocked') + TESTS
    with tempfile.TemporaryDirectory(prefix='record-key-session-') as temp:
        path = Path(temp) / 'session.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)
    print('Recorder sessions: pending cancellation, late-reply fence, frozen routes, pure streaming, kinds and storage admission passed.')


if __name__ == '__main__':
    main()
