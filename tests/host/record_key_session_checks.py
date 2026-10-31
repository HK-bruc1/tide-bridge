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
#define RDX_RECORD_USB_FENCE 12
#define RDX_RECORD_BIND_FENCE 13
#define RDX_RECORD_DUT_TOKEN_BIT 0x80000000UL
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
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1
#define TCFG_T2620_MEETING_MONO_DEBUG_MIC 1
typedef void (*RecUIFunction)(void);
typedef struct { u8 slot_index; u32 slot_generation, transport_epoch; } rdx_ble_async_token_t;
static int dut, factory, bound=1, storage_busy, cdc_busy, ux_busy, stream_ready=1, live=1;
static int ota, shutdown, factory_keys, playback_stops, memory_resets, metadata, writers, storage_ends, power_wakes;
static int writer_active, file_transfer, file_sync, file_loading, file_index, file_format;
static int pc_refresh, file_fault, rf_error, rf_erasing, rf_service, rf_io_users, rf_command_users;
static int retire_on_close;
static int queue_fail, timer_fail, posts, replies, triggers, processes, files, saves, audio;
static int app_queue_fail, worker_queue_fail, binding_app_posts, binding_resets, dut_resets;
static int release_at_worker_query, upload_timer_stops;
static u32 record_binding_generation=1;
static int record_binding_cleanup_error;
static u32 pending_binding_token=1;
static RecordStatus record_status;
static u32 record_session_generation=1, record_initialized_generation;
static u32 record_init_failed_generation, record_finish_generation;
static int record_finish_result;
static int store_error, abort_result, aborts, storage_resets, marks_cleared, close_exit, fault_on_close;
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
static bool record_session_tone_enabled=1;
static u32 record_start_tone_epoch;
static int tone_setting=1;
static u8 rdx_record_tone_enabled(void) { return tone_setting; }
static u32 record_start_tone_completed, record_stop_tone_pending_epoch;
static bool record_start_tone_ready;
static u32 tone_binding_token;
static struct { int goto_poweroff_flag; } app_var;
static int poweroff, tone_running, tone_fail, tone_plays, tone_callback_posts;
static int dut_record_stopping, record_binding_revoke_pending;
static u32 usb_record_done;
static int usb_record_result;
static void local_irq_disable(void) {}
static void local_irq_enable(void) {}
static int rdx_record_format_status(void) { return rf_error; }
static void rdx_record_format_storage_fault(void) { rf_error=-1; }
static int rdx_record_storage_error(void) { return store_error; }
static int rdx_uxfile_storage_status(void) { return file_fault; }
static int rdx_uxfile_abort_record(void) {
    ++aborts;
    if(writer_active) return -1;
    if(!abort_result) rf_error=0;
    return abort_result;
}
static void rdx_record_storage_reset(void) { ++storage_resets; store_error=0; }
enum stream_event { STREAM_EVENT_INIT=1, STREAM_EVENT_START, STREAM_EVENT_STOP };
static int rdx_app_get_poweroff_flag(void) { return poweroff; }
u8 rdx_record_binding_allowed(void);
u8 rdx_record_binding_token_is_current(u32 t);
u32 rdx_record_binding_token_capture(void);
static u32 rdx_record_session_token_capture(void) { return rdx_record_binding_token_capture(); }
static int rdx_record_session_token_is_current(u32 t) { return rdx_record_binding_token_is_current(t); }
static int closes, finishes;
static int stream_resume_timer, g_stream_resume_token_valid;
static rdx_ble_async_token_t owner={0,1,1};
static void noop(void) {}
static RecordStatus *rdx_record_get_status(void) { return &record_status; }
static int rdx_app_get_dut_status(void) { return dut; }
static int rdx_factory_audio_owned(void) { return factory; }
static int lose_ready_on_admission;
static int rdx_ble_server_is_stream_tx_ready(void) { return stream_ready; }
static int rdx_ble_session_rdx_token_resolve(const rdx_ble_async_token_t *t,int ready) {
    return live && t && t->slot_index==owner.slot_index &&
        t->slot_generation==owner.slot_generation && t->transport_epoch==owner.transport_epoch;
}
static int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t,int ready) {
    *t=owner; return live;
}
static u32 rdx_vm_get_bound_token(void) { return bound; }
static int rdx_cdc_source_stream_blocked(void);
u8 rdx_record_stream_only_release(void);
static int rdx_storage_lifecycle_business_blocked(void) {
    if(lose_ready_on_admission) stream_ready=0;
    return storage_busy || cdc_busy || pc_refresh || file_fault || rf_error || rf_service;
}
static int rdx_storage_lifecycle_stream_blocked(void) {
    if(release_at_worker_query) {
        /* app_core Up may preempt after the worker's earlier identity guard. */
        int fail=release_at_worker_query==2;
        release_at_worker_query=0;
        worker_queue_fail=fail;
        rdx_record_stream_only_release();
        worker_queue_fail=0;
    }
    return storage_busy || pc_refresh || rf_service ||
        rdx_cdc_source_stream_blocked();
}
static int rdx_dut_test_keys_active(void) { return factory_keys; }
static int get_ota_status(void) { return ota; }
static int rdx_dip_switch_shutdown_pending(void) { return shutdown; }
static void rdx_playback_stop(void) { ++playback_stops; }
static int rdx_record_format_download_conflict(const char *op) { return cdc_busy; }
static int rdx_record_format_service_status(void) { return cdc_busy || rf_service || rf_error; }
static int rdx_record_storage_active(void) { return writer_active; }
static int rdx_factory_audio_file_busy(void) { return factory; }
static int rdx_is_file_transfer_active(void) { return file_transfer; }
static int rdx_is_file_sync_busy(void) { return file_sync; }
static u8 rdx_uxfile_is_datFileInfo_loading(void) { return file_loading; }
static u8 rdx_uxfile_is_index_active(void) { return file_index; }
static u8 rdx_uxfile_is_formatting(void) { return file_format; }
static int rdx_uxfile_sd_format_status_check(void) { return file_format; }
static int rdx_app_init_flag=1;
typedef struct { int state; } pb_public_info_t;
enum { PB_STATE_UNREADY, PB_STATE_STOPPED, PB_STATE_PAUSED, PB_STATE_PLAYING };
static int playback_state;
static void rdx_playback_get_info(pb_public_info_t *p) { p->state=playback_state; }
static int rdx_record_state_is_publishable(void) { return 1; }
static void rdx_protocol_record_state_indicate(void) { ++replies; }
static void rdx_record_state_indicate_if_current(const rdx_ble_async_token_t *t) { ++replies; }
void rdx_record_set_process_state_ready(void);
static void rdx_peripheral_power_vdd_business_changed_notify(void) {}
static void rdx_record_clear_marks(void) { ++marks_cleared; }
static void (*process_hook)(void);
static void rdx_record_process(void) {
    ++processes;
    void (*hook)(void)=process_hook;
    process_hook=0;
    if(hook) hook();
}
static void rdx_app_emmc_poweron(int x) { ++power_wakes; }
static int rdx_record_uxfile_is_busy(void) { return ux_busy; }
static int pause_timeout_starts, pause_timeout_stops;
static void rdx_record_pause_timeout_stop(void) { ++pause_timeout_stops; }
static void rdx_record_pause_timeout_start(void) { ++pause_timeout_starts; }
static int max_timer_stops;
static void rdx_record_max_timer_stop(void) { ++max_timer_stops; }
static void rdx_record_max_timer_start(void) {}
static void rdx_record_set_filter_cnt(int n) {}
static int rdx_record_get_filter_cnt(void) { return 10; }
static int tone_player_runing(void) { return tone_running; }
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
static void rdx_record_binding_revoke_on_app_core(void);
static void rdx_record_binding_revoke_post(void *priv);
void rdx_record_binding_revoke(void);
static void rdx_record_dut_authorization_revoke(void) {}
static void rdx_app_record_binding_reset(void) { ++binding_resets; }
static void rdx_dut_record_reset(void) { ++dut_resets; }
static int os_taskq_post_msg(const char *name,int n,...) { if(queue_fail || worker_queue_fail) return -1; ++posts; return 0; }
static int os_taskq_post_type(const char *name,int q,int n,int *msg) {
    if(queue_fail || app_queue_fail) return -1;
    if(msg[0]==(int)rdx_record_binding_revoke_on_app_core) ++binding_app_posts;
    ++tone_callback_posts; return 0;
}
static void rdx_record_start_tone_cancel(void) { record_start_tone_pending=0; record_status.run=RECORD_STATE_STOP; }
static void rdx_app_record_state_upload_timer_stop(void);
static void rdx_led_ctrl_set_scene(int scene) {}
int rdx_record_run_exit(void);
static void translation_ear_recoder_close_all(void) {
    ++closes;
    if(fault_on_close) store_error=-25;
    if(close_exit) rdx_record_run_exit();
    if(retire_on_close) g_stream_only_session_active=0;
}
static int rdx_uxfile_finish_record(void) { ++finishes; return 0; }
static void rdx_ble_server_auto_shut_down_enable(int en) {}
static void rdx_protocol_record_trigger_indicate(RecordStatus *p,int factor) { ++triggers; }
static int rdx_record_session_allowed(void) { return rdx_record_binding_allowed(); }
static int worker_opens;
static void translation_ear_recoder_open_all_bound(int mode,u32 binding) { ++worker_opens; }
typedef struct { int file_send_busy; } ReqFileInfo;
static ReqFileInfo file_info;
static ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) { return &file_info; }
static void rdx_protocol_file_cmd_handle(int x) {}
static void rdx_uxfile_operate_file_init(void) { ++memory_resets; }
static void rdx_record_storage_end(void) { ++storage_ends; writer_active=0; }
static void rdx_factory_audio_saved(int result) { ++saves; }
static void rdx_record_notify_playback_content_changed(void) {}
static void rdx_app_emmc_poweroff_check(void) {}
static void rdx_app_emmc_poweroff_check_timer_stop(void) {}
/* 空间准入可控：admit 为 app_core 受理门禁，check_start 为 sink 初始化纵深防御。 */
static int admit_ret, admit_calls, space_ret;
static int rdx_record_storage_admit(void) { ++admit_calls; return admit_ret; }
struct tone_files { const char *no_storage, *ding; };
static const struct tone_files tones={"no_sd","ding"};
static const struct tone_files *get_tone_files(void) { return &tones; }
static int full_tones;
static int play_tone_file(const char *name) { full_tones+=!strcmp(name,"no_sd"); return 0; }
static int play_tone_file_with_completion(const char *name,void *priv,
        int (*cb)(void *,enum stream_event),void (*complete)(void *)) {
    ++tone_plays; return tone_fail ? -1 : cb(priv,STREAM_EVENT_INIT);
}
static int play_tone_file_callback(const char *name,void *priv,int (*cb)(void *,enum stream_event)) {
    return play_tone_file_with_completion(name,priv,cb,0);
}
static int rdx_record_storage_check_start(void) { return space_ret; }
static int rdx_uxfile_dat_1_gen(u8 scene) { ++files; return 0; }
static void rdx_record_init_raw_metadata(int format) { ++metadata; }
static int rdx_uxfile_get_operateFile_info(void) { return 0; }
static int rdx_record_format_begin(int file,int format) { ++metadata; return 0; }
static int rdx_record_storage_begin(u32 generation,int scene,int format) { ++writers; return 0; }
static int rdx_protocol_audio_data_indicate(u8 *d,u32 n) { ++audio; return 0; }
static void rdx_factory_audio_frame(u8 *d,u32 n) {}
static int rdx_record_storage_push(u8 *d,u32 n) { ++saves; return 0; }
'''

SOURCE_STUBS = r'''
#define TCFG_T2620_CDC_FILE_ENABLE 1
#define RDX_STORAGE_PLAYBACK_PREEMPT 2
#define MIN(a,b) ((a)<(b)?(a):(b))
#define rf_download cdc_busy
typedef int FILE;
typedef int OS_SEM;
static int source_handles, source_reads, source_releases, acquire_interleave;
static void rdx_cdc_source_service(void);
static void rdx_cdc_source_cancel(void);
static void rdx_cdc_source_release(void);
static int rdx_dut_is_in_mode(void) { return dut; }
static u32 sys_timer_get_ms(void) { return now_ms; }
static void rdx_factory_cdc_wake(void) {}
static int os_sem_create(OS_SEM *s,int n) { return 0; }
static int os_sem_del(OS_SEM *s,int n) { return 0; }
static int os_sem_pend(OS_SEM *s,int n) { return 0; }
static int os_sem_post(OS_SEM *s) { return 0; }
static int os_task_create(void (*f)(void *),void *arg,int p,int st,int q,const char *n) { return 0; }
static void os_time_dly(int n) {}
static void rdx_peripheral_power_vdd_connected_changed_notify(void) { ++source_releases; }
static int rdx_record_format_download_open(u32 sn,const char *name,void **handle,u32 *size) {
    *handle=(void *)1; *size=9000; ++source_handles; return 0;
}
static int rdx_record_format_download_list_open(void **handle,u32 *size) {
    return rdx_record_format_download_open(0,"",handle,size);
}
static int fread(void *data,int n,u32 length,FILE *handle) {
    memset(data,7,length); ++source_reads; return length;
}
static int fclose(FILE *handle) { --source_handles; return 0; }
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
    dut=factory=storage_busy=cdc_busy=ux_busy=lose_ready_on_admission=0; bound=live=stream_ready=save_enabled=1;
    ota=shutdown=factory_keys=playback_stops=memory_resets=metadata=writers=storage_ends=power_wakes=retire_on_close=worker_opens=0;
    tone_setting=1;
    record_session_tone_enabled=1;
    poweroff=tone_running=tone_fail=tone_plays=tone_callback_posts=0;
    record_start_tone_completed=record_stop_tone_pending_epoch=record_start_tone_ready=0;
    tone_binding_token=app_var.goto_poweroff_flag=dut_record_stopping=record_binding_revoke_pending=0;
    owner=(rdx_ble_async_token_t){0,1,1}; record_initialized_generation=0; now_ms=100;
    record_init_failed_generation=record_finish_generation=record_finish_result=0;
    store_error=abort_result=aborts=storage_resets=marks_cleared=close_exit=fault_on_close=0;
    writer_active=file_transfer=file_sync=file_loading=file_index=file_format=0;
    pc_refresh=file_fault=rf_error=rf_erasing=rf_service=rf_io_users=rf_command_users=0;
    playback_state=PB_STATE_STOPPED; file_info.file_send_busy=0;
    memset(&source,0,sizeof(source)); source.created=1;
    source_handles=source_reads=source_releases=acquire_interleave=0;
    app_queue_fail=worker_queue_fail=binding_app_posts=binding_resets=dut_resets=0;
    release_at_worker_query=upload_timer_stops=0;
    max_timer_stops=0;
    process_hook=0;
    record_binding_generation=pending_binding_token=1; record_binding_cleanup_error=0;
}
static void cmd(u8 state) {
    Record_info info={state+0x30,'1','0'};
    rdx_record_cmd_handle_from_rdx(&info,&owner);
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
        CHECK(closes==1 && !finishes && !files && !saves);
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
int test_online_session_storage_policy(void) {
    u8 data=0;
    /* Saved meeting, inspiration and no-save meeting share the lifecycle. */
    for(int policy=0;policy<3;++policy) {
        reset(); save_enabled=policy!=2;
        int kind=policy==1?RDX_RECORD_INSPIRATION:RDX_RECORD_MEETING;
        int stream_only=policy!=0;
        if(kind==RDX_RECORD_INSPIRATION) CHECK(rdx_record_key_start_prepare(kind,RECORD_MODE_ONLINE,&owner));
        if(stream_only) admit_ret=-2; /* Pure streaming never queries capacity. */
        cmd(RECORD_STATE_START);
        CHECK(rdx_record_stream_only_session_is_active()==stream_only);
        CHECK(rdx_record_nosave_session_open()==(policy==2));
        CHECK(rdx_record_kind_get()==kind && rdx_record_key_state_get()==RDX_RECORD_KEY_ACTIVE);
        u32 generation=record_session_generation;
        cmd(RECORD_STATE_START); CHECK(record_session_generation==generation);
        CHECK(!rdx_record_run_init() && files==!stream_only);
        CHECK(record_status.orig_mode==RECORD_MODE_ONLINE);
        CHECK(!rdx_record_run_data_handle(&data,1) && audio==1 && saves==!stream_only);
        cmd(RECORD_STATE_PAUSE); CHECK(record_status.run==RECORD_STATE_PAUSE);
        CHECK(!rdx_record_key_start_prepare(kind,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START); CHECK(record_session_generation==generation);
        cmd(RECORD_STATE_RESUME); CHECK(record_status.run==RECORD_STATE_RESUME);
        CHECK(rdx_record_kind_get()==kind && rdx_record_stream_only_session_is_active()==stream_only);
        CHECK(!rdx_record_run_init() && files==!stream_only);
        if(kind==RDX_RECORD_INSPIRATION) { CHECK(!rdx_record_stream_only_release()); drain_release(); }
        else { cmd(RECORD_STATE_STOP); drain_stop_reply(); }
        CHECK(files==!stream_only && saves==!stream_only && !rdx_record_nosave_session_open());
        if(stream_only) CHECK(!admit_calls && !metadata && !writers);
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
    reset(); CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
    stream_ready=0; cmd(RECORD_STATE_START);
    CHECK(g_record_cmd_delay_timer && g_pending_record_token_valid);
    u32 old=g_record_cmd_delay_generation;
    CHECK(!rdx_record_stream_only_release());
    CHECK(!g_record_cmd_delay_timer && !g_pending_record_token_valid);
    stream_ready=1; rdx_record_cmd_delay_cb((void *)old);
    CHECK(!processes && !files && record_status.run==RECORD_STATE_STOP);
    drain_release(); cmd(RECORD_STATE_STOP); drain_stop_reply();
    CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
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
        reset(); tone_setting=0; admit_ret=err?-1:-2;
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
        reset(); tone_setting=0; cmd(RECORD_STATE_START);
        CHECK(admit_calls==1 && record_status.run==RECORD_STATE_START);
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
int test_nosave_meeting_app_start_admission(void) {
    Record_info info={RECORD_STATE_START+0x30,'1','0'};
    reset(); save_enabled=0;
    CHECK(!rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_OFFLINE,NULL));
    CHECK(rdx_record_key_state_get()==RDX_RECORD_KEY_IDLE && !processes);
    /* 无法在线推流时，START 在任何状态、提示音、进程、存储动作之前被拒绝。 */
    for(int mode=0;mode<4;++mode) {
        reset(); save_enabled=0; tone_setting=1;
        if(mode==0) stream_ready=0;
        if(mode==1) live=0;
        if(mode==2) bound=0;
        if(mode==3) record_start_tone_pending=1;
        u32 generation=record_session_generation; key_snapshot_t key=key_snapshot();
        rdx_record_cmd_handle_from_rdx(&info,&owner);
        CHECK(record_status.run==RECORD_STATE_STOP && record_session_generation==generation);
        CHECK(key_snapshot_equal(key,key_snapshot()) && !g_stream_only_start_pending);
        CHECK(!processes && !files && !admit_calls && !tone_plays && !g_record_cmd_delay_timer);
        CHECK(!g_record_session_token_valid && !rdx_record_stream_only_session_is_active());
    }
    /* CCC drops between admission and the internal START check: no retry may
     * survive aborting the temporary no-save sidecar. */
    reset(); save_enabled=0; lose_ready_on_admission=1;
    rdx_record_cmd_handle_from_rdx(&info,&owner);
    CHECK(!stream_ready && record_status.run==RECORD_STATE_STOP);
    CHECK(!g_record_cmd_delay_timer && !g_stream_only_start_pending &&
          rdx_record_key_state_get()==RDX_RECORD_KEY_IDLE && !admit_calls && !processes);
    /* CALL remains file-backed with the switch OFF. */
    reset(); save_enabled=0; info.type='0'+RECORD_SCENE_CALL;
    rdx_record_cmd_handle_from_rdx(&info,&owner);
    CHECK(!rdx_record_stream_only_session_is_active() && admit_calls==1);
    return 0;
}
int test_nosave_meeting_loss_and_no_cdc_expansion(void) {
    Record_info info={RECORD_STATE_START+0x30,'1','0'};
    for(int stage=0;stage<4;++stage) {
        reset(); save_enabled=0; tone_setting=1;
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
        CHECK(rdx_record_nosave_session_open());
        if(stage) rdx_record_cmd_handle_from_rdx(&info,&owner);
        if(stage==1) CHECK(rdx_record_start_tone_wait());
        if(stage>=2) CHECK(!rdx_record_run_init());
        if(stage==3) { cmd(RECORD_STATE_PAUSE); CHECK(!rdx_record_run_exit()); }
        CHECK(rdx_record_cdc_read_is_busy() && rdx_cdc_source_list_begin()==2);
        /* Receiver loss while pending/tone/active/paused never retries START
         * or changes to file-backed recording. The worker still owns close. */
        stream_ready=0;
        rdx_record_on_ble_conn_changed(0);
        CHECK(!pause_timeout_starts);
        rdx_record_cmd_handle_from_rdx(&info,&owner);
        CHECK(!g_record_cmd_delay_timer && !admit_calls && !tone_plays);
        CHECK(!rdx_record_stream_only_release());
        u32 ticket=g_hold_release_ticket;
        worker_release(ticket);
        CHECK(rdx_record_stream_only_release());
        rdx_record_stream_only_release_complete();
        CHECK(record_status.run==RECORD_STATE_STOP && !rdx_record_nosave_session_open());
        CHECK(!files && !saves && !finishes && !metadata && !writers);
        if(stage==1) {
            rdx_record_start_tone_play((void *)record_start_tone_epoch);
            CHECK(!tone_plays);
        }
    }
    reset(); save_enabled=0; cdc_busy=1;
    rdx_record_cmd_handle_from_rdx(&info,&owner);
    CHECK(record_status.run==RECORD_STATE_STOP && !processes && !admit_calls);
    CHECK(!rdx_record_key_start_prepare(RDX_RECORD_MEETING,RECORD_MODE_ONLINE,&owner));
    /* F19 keeps its original online-only coexistence exception. */
    CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
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
static int check_tone_snapshot_and_stop(int enabled,int hold) {
    u32 epoch=record_start_tone_epoch;
    CHECK(rdx_record_start_tone_wait()==enabled);
    CHECK(record_start_tone_epoch!=epoch && record_start_tone_pending==enabled);
    CHECK(record_session_tone_enabled==enabled && tone_callback_posts==enabled && !tone_plays);
    tone_setting=!enabled; /* A session must not resample RAM during resume/STOP. */
    if(enabled) {
        epoch=record_start_tone_epoch;
        rdx_record_start_tone_play((void *)epoch); CHECK(tone_plays==1);
        int procs=processes;
        rdx_record_start_tone_complete((void *)epoch);
        rdx_record_start_tone_callback((void *)epoch,STREAM_EVENT_STOP);
        rdx_record_start_tone_finish((void *)epoch);
        CHECK(processes==procs+1 && !record_start_tone_pending && record_start_tone_ready);
        CHECK(!rdx_record_start_tone_wait() && !record_start_tone_ready);
    }
    /* Only an actually initialized recording may own an end prompt. */
    record_tone_session_active=1;
    record_status.run=RECORD_STATE_PAUSE;
    CHECK(!rdx_record_start_tone_wait());
    int before=tone_callback_posts;
    worker_stop(RECORD_STATE_PAUSE);
    CHECK(record_tone_session_active && tone_callback_posts==before);
    record_status.run=RECORD_STATE_RESUME;
    CHECK(!rdx_record_start_tone_wait() && tone_callback_posts==before);
    CHECK(record_session_tone_enabled==enabled);
    record_status.run=RECORD_STATE_STOP;
    if(hold) worker_release(7); else worker_stop(RECORD_STATE_STOP);
    CHECK(tone_callback_posts==before+enabled);
    CHECK(!!record_stop_tone_pending_epoch==enabled && !record_tone_session_active);
    CHECK(record_session_tone_enabled==enabled);
    if(enabled) {
        epoch=record_stop_tone_pending_epoch;
        rdx_record_stop_tone_play((void *)epoch);
        rdx_record_stop_tone_callback((void *)epoch,STREAM_EVENT_STOP);
        CHECK(!record_stop_tone_pending_epoch && tone_plays==2);
    } else CHECK(!tone_plays);
    return 0;
}
int test_record_tone_snapshot_and_scope(void) {
    /* Key businesses: F18/F19, including fixed offline and online routes. */
    for(int online=0;online<2;++online) for(int kind=0;kind<2;++kind) for(int setting=0;setting<2;++setting) {
        reset(); tone_setting=setting;
        CHECK(rdx_record_key_start_prepare(kind,online,&owner));
        if(online) cmd(RECORD_STATE_START);
        else { rdx_record_prepare_new_session(); record_status.run=RECORD_STATE_START; }
        CHECK(!check_tone_snapshot_and_stop(setting,1));
    }
    /* Ordinary APP/F18 online and local business, including normal CALL recording. */
    for(int online=0;online<2;++online) for(int call=0;call<2;++call) for(int setting=0;setting<2;++setting) {
        reset(); tone_setting=setting;
        Record_info info={RECORD_STATE_START+0x30,'1',(call?RECORD_SCENE_CALL:RECORD_SCENE_CHAT)+0x30};
        if(online) rdx_record_cmd_handle_internal(&info,&owner);
        else {
            record_status.scene=call?RECORD_SCENE_CALL:RECORD_SCENE_CHAT;
            rdx_record_prepare_new_session(); record_status.run=RECORD_STATE_START;
        }
        CHECK(rdx_record_kind_get()==(call?RDX_RECORD_CALL:RDX_RECORD_MEETING));
        CHECK(!check_tone_snapshot_and_stop(setting,0));
    }
    /* Factory/DUT audio is not a user recording setting. */
    for(int mode=0;mode<2;++mode) {
        reset(); tone_setting=0; dut=mode==0; factory=mode==1;
        record_status.run=RECORD_STATE_START;
        CHECK(!check_tone_snapshot_and_stop(1,0));
    }
    reset(); tone_setting=0; record_status.run=RECORD_STATE_START;
    CHECK(!rdx_record_start_tone_wait());
    record_status.run=RECORD_STATE_STOP;
    worker_stop(RECORD_STATE_STOP); CHECK(!tone_callback_posts); /* no phantom end tone */
    return 0;
}
int test_tone_start_guards_and_failures(void) {
    for(int setting=0;setting<2;++setting) for(int failure=0;failure<5;++failure) {
        reset(); tone_setting=setting;
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START); int procs=processes;
        if(failure==0) live=0;
        if(failure==1) bound=0;
        if(failure==2) storage_busy=1;
        if(failure==3) poweroff=1;
        if(failure==4) app_var.goto_poweroff_flag=1;
        CHECK(rdx_record_start_tone_wait());
        if(setting) rdx_record_start_tone_play((void *)record_start_tone_epoch);
        CHECK(!record_start_tone_pending && record_status.run==RECORD_STATE_STOP && !tone_plays && procs==processes);
    }
    for(int failure=0;failure<3;++failure) {
        reset(); CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START); CHECK(rdx_record_start_tone_wait());
        u32 epoch=record_start_tone_epoch; int procs=processes;
        if(failure==0) { /* STOP/preemption is not natural completion. */
            rdx_record_start_tone_play((void *)epoch);
            rdx_record_start_tone_callback((void *)epoch,STREAM_EVENT_STOP);
            rdx_record_start_tone_finish((void *)epoch);
        }
        if(failure==1) { tone_fail=1; rdx_record_start_tone_play((void *)epoch); }
        if(failure==2) { record_status.run=RECORD_STATE_STOP; rdx_record_start_tone_wait(); }
        CHECK(!record_start_tone_pending && record_status.run==RECORD_STATE_STOP && processes==procs);
        rdx_record_start_tone_complete((void *)epoch);
        rdx_record_start_tone_finish((void *)epoch);
        CHECK(processes==procs);
    }
    reset(); queue_fail=1; record_status.run=RECORD_STATE_START;
    CHECK(rdx_record_start_tone_wait() && !record_start_tone_pending && record_status.run==RECORD_STATE_STOP);
    return 0;
}
int test_tone_config_full_idle_and_old_end_callback(void) {
    reset(); CHECK(!rdx_record_tone_config_busy());
    record_status.run=RECORD_STATE_PAUSE; CHECK(rdx_record_tone_config_busy());
    record_status.run=RECORD_STATE_STOP;
    record_status.process_state=REC_PROCESS_STATE_BUSY; CHECK(rdx_record_tone_config_busy());
    record_status.process_state=REC_PROCESS_STATE_READY;
    g_record_cmd_delay_timer=1; CHECK(rdx_record_tone_config_busy()); g_record_cmd_delay_timer=0;
    CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
    CHECK(rdx_record_tone_config_busy()); /* waiting for APP with run==STOP */
    rdx_record_stream_only_release(); CHECK(rdx_record_tone_config_busy());
    drain_release(); CHECK(rdx_record_tone_config_busy()); /* late reply quarantine */
    now_ms+=RDX_RECORD_HOLD_LATE_REPLY_MS; CHECK(!rdx_record_tone_config_busy());
    int *gates[]={&record_start_tone_pending,&g_app_stop_pending,&dut_record_stopping,&record_binding_revoke_pending,&tone_running};
    for(unsigned i=0;i<sizeof(gates)/sizeof(gates[0]);++i) {
        *gates[i]=1; CHECK(rdx_record_tone_config_busy()); *gates[i]=0;
    }
    record_tone_session_active=1; CHECK(rdx_record_tone_config_busy()); record_tone_session_active=0;
    record_start_tone_epoch=8; rdx_record_stop_tone_post(8);
    CHECK(rdx_record_tone_config_busy());
    ++record_start_tone_epoch;
    rdx_record_stop_tone_play((void *)8); CHECK(!tone_plays && !record_stop_tone_pending_epoch);
    rdx_record_stop_tone_post(9);
    rdx_record_stop_tone_callback((void *)8,STREAM_EVENT_STOP); CHECK(record_stop_tone_pending_epoch==9);
    rdx_record_stop_tone_callback((void *)9,STREAM_EVENT_STOP); CHECK(!record_stop_tone_pending_epoch);
    queue_fail=1; rdx_record_stop_tone_post(9); CHECK(!record_stop_tone_pending_epoch);
    return 0;
}
int test_normal_stop_and_usb_retire_tone_lifecycle(void) {
    for(int muted=0;muted<2;++muted) {
        reset(); tone_setting=!muted;
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START); rdx_record_start_tone_wait(); record_start_tone_pending=0;
        record_tone_session_active=1;
        int before=tone_callback_posts;
        worker_stop(RECORD_STATE_PAUSE);
        CHECK(record_tone_session_active && before==tone_callback_posts);
        worker_stop(RECORD_STATE_STOP);
        CHECK(!record_tone_session_active && tone_callback_posts==before+!muted);
        CHECK(!!record_stop_tone_pending_epoch==!muted);
    }
    reset(); record_tone_session_active=1;
    worker_usb(5);
    CHECK(!record_tone_session_active && usb_record_done==5 && !usb_record_result);
    CHECK(!rdx_record_tone_config_busy());
    return 0;
}
int test_cdc_first_f19_no_storage_lifecycle(void) {
    for(int cleanup=0;cleanup<5;++cleanup) {
        reset(); cdc_busy=1; tone_setting=cleanup%2;
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START);
        CHECK(record_status.run==RECORD_STATE_START && g_stream_only_session_active);
        CHECK(!admit_calls && !power_wakes && playback_stops==1);
        if(tone_setting) {
            CHECK(rdx_record_start_tone_wait());
            u32 epoch=record_start_tone_epoch;
            rdx_record_start_tone_play((void *)epoch);
            rdx_record_start_tone_complete((void *)epoch);
            rdx_record_start_tone_callback((void *)epoch,STREAM_EVENT_STOP);
            rdx_record_start_tone_finish((void *)epoch);
        }
        CHECK(!rdx_record_start_tone_wait());
        process_gate_0(); process_gate_1(); worker_start(RECORD_STATE_START);
        CHECK(worker_opens==1 && !rdx_record_run_init());
        CHECK(memory_resets==1 && !files && !metadata && !writers);
        u8 frame=7; CHECK(!rdx_record_run_data_handle(&frame,1) && audio==1 && !saves);
        if(cleanup==0) {
            cmd(RECORD_STATE_PAUSE); CHECK(!rdx_record_run_exit());
            cdc_busy=0; cmd(RECORD_STATE_RESUME);
            CHECK(g_stream_only_session_active && !rdx_record_run_init());
            CHECK(!rdx_record_run_data_handle(&frame,1) && !saves);
            cdc_busy=1; record_status.run=RECORD_STATE_STOP;
            CHECK(!rdx_record_run_exit() && !g_stream_only_session_active);
        } else if(cleanup==1) {
            rdx_record_stream_only_release(); retire_on_close=1;
            worker_release(g_hold_release_ticket);
        } else if(cleanup==2) {
            cmd(RECORD_STATE_STOP); retire_on_close=1; worker_stop(RECORD_STATE_STOP);
        } else if(cleanup==3) {
            live=0; rdx_record_on_ble_conn_changed(0);
            CHECK(!rdx_record_stream_only_release());
            worker_release(g_hold_release_ticket);
        } else {
            retire_on_close=1; worker_usb(5);
        }
        CHECK(cdc_busy && !files && !saves && !finishes && !metadata && !writers);
    }
    /* Ordinary recordings still require file finalization. */
    reset(); cmd(RECORD_STATE_START); worker_stop(RECORD_STATE_STOP);
    CHECK(finishes==1);
    return 0;
}
int test_f19_first_cdc_list_get(void) {
    for(int list=0;list<2;++list) for(int stage=0;stage<5;++stage) {
        reset(); tone_setting=stage==1;
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        if(stage) cmd(RECORD_STATE_START);
        if(stage==1) CHECK(rdx_record_start_tone_wait());
        if(stage>=2) CHECK(!rdx_record_run_init());
        if(stage>=3) { cmd(RECORD_STATE_PAUSE); CHECK(!rdx_record_run_exit()); }
        if(stage==4) cmd(RECORD_STATE_RESUME);
        if(stage) {
            record_status.process_state=REC_PROCESS_STATE_BUSY;
            CHECK(rdx_app_factory_file_is_busy() && rdx_pc_storage_is_busy());
            CHECK(rdx_app_storage_activity_is_busy("FORMAT",0,0));
            CHECK(rdx_app_storage_activity_is_busy("REBIND",1,1));
        }
        CHECK(!rdx_record_cdc_read_is_busy() && !rdx_app_cdc_file_is_busy());
        CHECK(!(list?rdx_cdc_source_list_begin():rdx_cdc_source_begin(7,"abc.raw")));
        CHECK(cdc_busy && !rdx_app_cdc_file_conflict() && rdx_app_cdc_file_is_busy());
        source_work(); CHECK(source_handles==1 && source.offset==8192);
        u8 reader[sizeof(source)]; memcpy(reader,&source,sizeof(reader));
        rdx_record_stream_only_release(); CHECK(rdx_record_cdc_read_is_busy());
        drain_release(); cmd(RECORD_STATE_START);
        CHECK(record_status.run==RECORD_STATE_STOP);
        CHECK(!memcmp(reader,&source,sizeof(reader)) && cdc_busy);
        CHECK(!files && !writers && !metadata && !saves && !finishes);
        u8 block[4096]; u32 length,size;
        CHECK(rdx_cdc_source_take(block,&length,&size)==1 && size==9000);
        source_work(); rdx_cdc_source_release();
        CHECK(!cdc_busy && !source.busy && !source_handles && source_releases==1);
    }
    return 0;
}
int test_cdc_recording_exclusion_and_identity(void) {
    for(int first=0;first<2;++first) for(int kind=0;kind<2;++kind) for(int route=0;route<2;++route) {
        reset(); cdc_busy=first;
        int safe=kind==RDX_RECORD_INSPIRATION && route==RECORD_MODE_ONLINE;
        CHECK(!!rdx_record_key_start_prepare(kind,route,&owner)==(!first || safe));
        if(!first) CHECK(rdx_cdc_source_list_begin()==(safe?0:2));
    }
    for(int resume=0;resume<2;++resume) {
        reset(); cdc_busy=1;
        if(resume) record_status.run=RECORD_STATE_PAUSE;
        Record_info info={(resume?RECORD_STATE_RESUME:RECORD_STATE_START)+0x30,'1','0'};
        rdx_app_record_cmd_request_t request={info,1,owner};
        rdx_app_record_cmd_on_app_core(&request);
        CHECK(!playback_stops && !processes && !admit_calls && cdc_busy);
    }
    reset(); cmd(RECORD_STATE_START);
    CHECK(rdx_cdc_source_begin(7,"abc.raw")==2 && !cdc_busy);
    for(int active=0;active<2;++active) for(int bad=0;bad<8;++bad) {
        reset(); CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        if(active) cmd(RECORD_STATE_START);
        if(bad==0) writer_active=1;
        if(bad==1) live=0;
        if(bad==2) stream_ready=0;
        if(bad==3) ++owner.transport_epoch;
        if(bad==4) g_app_stop_pending=1;
        if(bad==5) record_binding_revoke_pending=1;
        if(bad==6) g_stream_only_release_started=1;
        if(bad==7) ++g_stream_only_start_token.transport_epoch;
        CHECK(rdx_record_cdc_read_is_busy());
        CHECK(rdx_cdc_source_begin(7,"abc.raw")==2 && !cdc_busy && !source.busy);
    }
    int *guards[]={&storage_busy,&pc_refresh,&file_fault,&rf_error,&rf_service,
        &file_transfer,&file_sync,&file_loading,&file_index,&file_format,
        &ota,&dut,&factory,&factory_keys,&poweroff,&shutdown,&app_var.goto_poweroff_flag,
        &rf_io_users,&rf_command_users};
    for(int i=0;i<sizeof(guards)/sizeof(guards[0]);++i) {
        reset(); CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START); *guards[i]=1;
        int rejected=rdx_cdc_source_begin(7,"abc.raw");
        CHECK(rejected==(guards[i]==&file_fault?8:2) && !cdc_busy && !source.busy);
    }
    return 0;
}
int test_cdc_post_acquire_recheck(void) {
    for(int interleave=0;interleave<7;++interleave) {
        reset(); acquire_interleave=interleave;
        int rejected=interleave>=2 && interleave<=5;
        if(rejected) CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        CHECK(rdx_cdc_source_begin(7,"abc.raw")== (rejected?2:0));
        CHECK(cdc_busy==!rejected && source.busy==!rejected && source_releases==rejected);
        CHECK(!source_handles && !source_reads && !source.offset && !files && !finishes);
        if(!rejected) {
            if(interleave!=1) CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
            cmd(RECORD_STATE_START); CHECK(g_stream_only_session_active && !admit_calls);
        }
    }
    return 0;
}
static int fault_kind, gate_path;
static unsigned char source_snapshot[sizeof(source)];
static void change_cdc_health(void) {
    if(fault_kind==0) ++now_ms;
    else if(fault_kind==1) source.error=5;
    else rdx_cdc_source_cancel();
    memcpy(source_snapshot,&source,sizeof(source));
}
static void process_health_change(void) {
    change_cdc_health();
    if(gate_path==0) process_gate_0(); else process_gate_1();
}
static int prepare_cdc_gate(int state) {
    reset(); tone_setting=0;
    CHECK(!rdx_cdc_source_begin(7,"abc.raw"));
    CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
    if(state!=RECORD_STATE_START) {
        cmd(RECORD_STATE_START); CHECK(!rdx_record_start_tone_wait());
        worker_start(RECORD_STATE_START); CHECK(!rdx_record_run_init());
        if(state==RECORD_STATE_RESUME) {
            cmd(RECORD_STATE_PAUSE); worker_stop(RECORD_STATE_PAUSE);
            CHECK(!rdx_record_run_exit());
        }
    }
    source_work(); CHECK(source_handles==1 && source.offset==8192);
    source.operation=2; source.since=now_ms-2999u;
    CHECK(!rdx_cdc_source_stream_blocked());
    return 0;
}
static void fail_accepted_operation(int state) {
    if(gate_path<2) {
        process_hook=process_health_change;
        cmd(state);
    } else {
        cmd(state); rdx_record_set_process_state_busy();
        change_cdc_health(); worker_start(state);
    }
}
int test_cdc_gate_fault_cleanup(void) {
    const int states[]={RECORD_STATE_START,RECORD_STATE_PAUSE,RECORD_STATE_RESUME};
    /* Two process implementations and the later worker START/RESUME guard. */
    for(int path=0;path<3;++path) for(int i=0;i<3;++i) for(int fault=0;fault<3;++fault) {
        if(path==2 && states[i]==RECORD_STATE_PAUSE) continue;
        CHECK(!prepare_cdc_gate(states[i])); gate_path=path; fault_kind=fault;
        u32 old=rdx_record_session_token_capture();
        int opened=worker_opens, closed=closes, queued=posts;
        fail_accepted_operation(states[i]);
        CHECK(record_binding_revoke_pending && binding_app_posts==1 && posts==queued);
        CHECK(!rdx_record_binding_allowed() && !rdx_record_session_token_is_current(old));
        int sent=audio; u8 frame=1;
        CHECK(!rdx_record_run_data_handle(&frame,1) && audio==sent);
        worker_start_token(RECORD_STATE_START,old);
        worker_start_token(RECORD_STATE_RESUME,old);
        CHECK(worker_opens==opened && binding_app_posts==1);
        rdx_record_binding_revoke_on_app_core(); retire_on_close=1; worker_binding();
        CHECK(!record_binding_revoke_pending && !record_binding_cleanup_error && bound);
        CHECK(closes==closed+1 && record_status.run==RECORD_STATE_STOP);
        CHECK(record_status.process_state==REC_PROCESS_STATE_READY);
        CHECK(!g_stream_only_session_active && !g_record_session_token_valid);
        CHECK(rdx_record_key_state_get()==RDX_RECORD_KEY_IDLE);
        CHECK(!files && !metadata && !writers && !saves && !finishes);
        CHECK(!memcmp(source_snapshot,&source,sizeof(source)) && source_handles==1 && cdc_busy);
        rdx_cdc_source_release(); source_work(); rdx_cdc_source_service();
        CHECK(!source.busy && !cdc_busy && !source_handles && !rdx_app_cdc_file_is_busy());
        CHECK(!rdx_cdc_source_begin(7,"abc.raw"));
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START); CHECK(!rdx_record_start_tone_wait());
        RecordStatus current=record_status; storage_busy=1;
        worker_start_token(RECORD_STATE_START,old);
        CHECK(!memcmp(&current,&record_status,sizeof(current)) && !record_binding_revoke_pending);
    }
    return 0;
}
int test_cdc_gate_cleanup_retry(void) {
    for(int path=0;path<3;++path) for(int worker_queue=0;worker_queue<2;++worker_queue)
    for(int allocation=0;allocation<2;++allocation) {
        CHECK(!prepare_cdc_gate(RECORD_STATE_START)); gate_path=path; fault_kind=0;
        if(worker_queue) worker_queue_fail=1; else app_queue_fail=1;
        timer_fail=allocation; int queued=posts;
        fail_accepted_operation(RECORD_STATE_START);
        if(worker_queue) rdx_record_binding_revoke_on_app_core();
        CHECK(record_binding_revoke_pending && posts==queued && !closes);
        CHECK(allocation ? record_binding_cleanup_error==-2 : timeout_live && timeout_delay==10);
        rdx_record_process_state_set_timer_cb(NULL);
        CHECK(!rdx_record_binding_allowed());
        CHECK(!rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        app_queue_fail=worker_queue_fail=timer_fail=0;
        if(!allocation) {
            CHECK(timeout_callback==rdx_record_binding_revoke_post);
            timeout_callback(timeout_priv); rdx_record_binding_revoke_on_app_core(); worker_binding();
            CHECK(!record_binding_revoke_pending && !record_binding_cleanup_error && closes==1);
            CHECK(record_status.process_state==REC_PROCESS_STATE_READY);
        } else CHECK(record_binding_cleanup_error==-2 && record_binding_revoke_pending);
        CHECK(!files && !writers && !finishes && source_handles==1 && cdc_busy);
        CHECK(!memcmp(source_snapshot,&source,sizeof(source)));
    }
    return 0;
}
int test_cdc_worker_release_race(void) {
    for(int resume=0;resume<2;++resume) for(int release=0;release<3;++release) {
        int state=resume?RECORD_STATE_RESUME:RECORD_STATE_START;
        CHECK(!prepare_cdc_gate(state)); cmd(state); rdx_record_set_process_state_busy();
        u32 binding=rdx_record_session_token_capture();
        u8 snapshot[sizeof(source)]; memcpy(snapshot,&source,sizeof(source));
        if(!release) { source.operation=0; rdx_record_stream_only_release(); }
        else { ++now_ms; release_at_worker_query=release; }
        worker_start_token(state,binding);
        CHECK(worker_opens==resume && g_stream_only_release_started && upload_timer_stops);
        if(!release) {
            CHECK(!record_binding_revoke_pending && !binding_app_posts);
            drain_release();
        } else {
            CHECK(record_binding_revoke_pending && binding_app_posts==1);
            rdx_record_binding_revoke_on_app_core();
            CHECK(!binding_resets && dut_resets==1);
            if(release==1) worker_release(g_hold_release_ticket);
            worker_binding();
            if(release==2) {
                CHECK(!rdx_record_stream_only_release());
                worker_release(g_hold_release_ticket);
            }
            rdx_record_stream_only_release(); rdx_record_stream_only_release_complete();
            CHECK(!memcmp(snapshot,&source,sizeof(source)));
        }
        CHECK(!record_binding_revoke_pending && !record_binding_cleanup_error);
        CHECK(!g_stream_only_release_started && rdx_record_key_state_get()==RDX_RECORD_KEY_IDLE);
        CHECK(record_status.run==RECORD_STATE_STOP && record_status.process_state==REC_PROCESS_STATE_READY);
        CHECK(!files && !writers && !metadata && !finishes && cdc_busy && source_handles==1);
    }
    return 0;
}
int test_failed_writer_cleanup(void) {
    /* Sink exit and the outer STOP both finalize: discard exactly once,
     * after the writer has drained, with no DAT save or idle recovery timer. */
    for(int fault=0;fault<4;++fault) {
        reset(); tone_setting=0;
        rdx_record_prepare_new_session();
        record_status.run=RECORD_STATE_STOP;
        record_status.begin_time=123;
        record_status.process_state=REC_PROCESS_STATE_BUSY;
        record_initialized_generation=record_session_generation;
        writer_active=close_exit=1;
        if(fault==0) store_error=-25;
        if(fault==1) { store_error=-24; file_fault=-1; }
        if(fault==2) store_error=-23;
        if(fault==3) record_init_failed_generation=record_session_generation;
        rf_error=-1;
        worker_stop(RECORD_STATE_STOP);
        CHECK(!writer_active && storage_ends==1 && aborts==1 && !finishes);
        CHECK(!record_status.begin_time && !record_initialized_generation);
        CHECK(record_status.process_state==REC_PROCESS_STATE_READY);
        CHECK(rdx_record_key_state_get()==RDX_RECORD_KEY_IDLE && !timeout_live);
        CHECK(record_finish_generation==record_session_generation && record_finish_result<0);
        CHECK(storage_resets==(fault!=1) && !record_binding_revoke_pending);
        if(fault==1) CHECK(rf_error<0 && rdx_storage_lifecycle_business_blocked());
        else CHECK(!rf_error && !store_error && !rdx_storage_lifecycle_business_blocked());
        /* A delayed/repeated STOP never retries the destructive operation. */
        worker_stop(RECORD_STATE_STOP); CHECK(aborts==1 && !finishes);
        /* Pure F19 streaming still works with a latched file fault. */
        CHECK(rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner));
        cmd(RECORD_STATE_START);
        CHECK(g_stream_only_session_active && !admit_calls && !timeout_live);
    }
    return 0;
}
int test_abort_failure_releases_controls(void) {
    for(int unbind=0;unbind<2;++unbind) {
        reset(); tone_setting=0;
        rdx_record_prepare_new_session();
        record_status.process_state=REC_PROCESS_STATE_BUSY;
        store_error=-25; rf_error=-1; abort_result=-1;
        if(unbind) {
            record_binding_revoke_pending=1;
            worker_binding();
            CHECK(!record_binding_revoke_pending && record_binding_cleanup_error==-1);
        } else {
            record_status.run=RECORD_STATE_STOP;
            worker_stop(RECORD_STATE_STOP);
        }
        CHECK(aborts==1 && !finishes && !storage_resets && rf_error<0);
        CHECK(record_status.process_state==REC_PROCESS_STATE_READY);
        CHECK(record_status.run==RECORD_STATE_STOP && rdx_record_binding_allowed());
        CHECK(rdx_record_key_state_get()==RDX_RECORD_KEY_IDLE && !timeout_live);
        CHECK(rdx_storage_lifecycle_business_blocked());
        CHECK(!rdx_storage_lifecycle_stream_blocked());
    }
    return 0;
}
int test_pause_drain_failure_stops_session(void) {
    for(int fault=0;fault<2;++fault) {
        reset(); tone_setting=0;
        rdx_record_prepare_new_session();
        record_status.run=RECORD_STATE_PAUSE;
        record_status.process_state=REC_PROCESS_STATE_BUSY;
        record_status.begin_time=100;
        close_exit=writer_active=1;
        if(!fault) fault_on_close=1;
        else record_init_failed_generation=record_session_generation;
        worker_stop(RECORD_STATE_PAUSE);
        CHECK(!writer_active && aborts==1 && !finishes && !store_error);
        CHECK(record_status.run==RECORD_STATE_STOP && !record_status.begin_time);
        CHECK(record_status.process_state==REC_PROCESS_STATE_READY);
        CHECK(!record_initialized_generation && !timeout_live && pause_timeout_stops==1);
        CHECK(max_timer_stops==1);
        CHECK(rdx_record_key_state_get()==RDX_RECORD_KEY_IDLE);
    }
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
    source = prefix + types + STUBS[STUBS.index('static int dut,'):] + SOURCE_STUBS
    source += re.search(r'#define RDX_RECORD_HOLD_LATE_REPLY_MS.*', record)[0] + '\n'
    source += re.search(r'static struct \{[^}]*\} record_key_context;', record, re.S)[0]
    source += '\nstatic u8 rdx_record_token_equal(const rdx_ble_async_token_t *,const rdx_ble_async_token_t *);\n'
    source += 'static u8 rdx_record_hold_fence_blocks(const rdx_ble_async_token_t *);\n'
    source += 'static u8 rdx_record_start_uses_local_storage(const rdx_ble_async_token_t *);\n'
    source += 'void rdx_record_stream_only_start_arm(const rdx_ble_async_token_t *);\n'
    source += 'static void rdx_record_app_stop_request(const rdx_ble_async_token_t *);\n'
    source += 'static void rdx_record_cmd_handle_internal(Record_info *,const rdx_ble_async_token_t *);\n'
    source += 'static int save_enabled=1;\nstatic int rdx_record_save_enabled(void) { return save_enabled; }\n'
    for name in (
        'rdx_record_binding_allowed', 'rdx_record_binding_token_capture',
        'rdx_record_binding_token_is_current',
        'rdx_record_process_state_timer_stop', 'rdx_record_process_state_set_timer_cb',
        'rdx_record_process_is_busy_check', 'rdx_record_set_process_state_busy',
        'rdx_record_set_process_state_ready',
        'rdx_record_kind_is_stream_only',
        'rdx_record_nosave_session_open',
        'rdx_record_local_route_is_fixed', 'rdx_record_kind_get', 'rdx_record_key_state_get',
        'rdx_record_key_request_generation', 'rdx_record_key_request_is_current',
        'rdx_record_stream_business_blocked',
        'rdx_record_key_start_prepare', 'rdx_record_key_start_abort',
        'rdx_record_stream_only_token_capture', 'rdx_record_rdx_token_is_current',
        'rdx_record_token_equal', 'rdx_record_hold_fence_arm',
        'rdx_record_hold_fence_blocks', 'rdx_record_stream_only_start_arm',
        'rdx_record_stream_only_start_cancel', 'rdx_record_stream_only_session_is_active',
        'rdx_record_stream_only_start_consume', 'rdx_record_online_session_clear',
        'rdx_record_online_session_bind', 'rdx_record_online_session_is_current',
        'rdx_record_online_session_accepts', 'rdx_record_online_session_bind_current',
        'rdx_record_transport_is_detached',
        'rdx_record_pending_cmd_clear', 'rdx_record_start_uses_local_storage',
        'rdx_record_stream_owner_is_current', 'rdx_record_cdc_read_is_busy',
        'rdx_record_session_storage_blocked',
        'rdx_record_command_storage_blocked',
        'rdx_record_local_storage_admit',
        'rdx_record_format_for_session', 'rdx_record_prepare_new_session',
        'rdx_record_app_stop_pump', 'rdx_record_app_stop_tick', 'rdx_record_app_stop_request',
        'rdx_record_audio_fault_stop', 'rdx_record_stream_only_release',
        'rdx_record_stream_only_release_complete', 'rdx_record_stream_only_is_releasing',
        'rdx_record_cmd_delay_cb', 'rdx_record_cmd_handle_internal',
        'rdx_record_cmd_handle_admit', 'rdx_record_cmd_handle_from_rdx',
        'rdx_record_update_connection_mode', 'rdx_record_on_ble_conn_changed',
        'rdx_record_run_init', 'rdx_record_run_data_handle',
        'rdx_record_stop_session_cleanup',
        'rdx_record_finish_file',
        'rdx_record_worker_close', 'rdx_record_run_exit',
        'rdx_record_start_tone_admitted', 'rdx_record_start_tone_is_current',
        'rdx_record_start_tone_finish',
        'rdx_record_start_tone_complete', 'rdx_record_start_tone_callback',
        'rdx_record_start_tone_play', 'rdx_record_start_tone_wait',
        'rdx_record_stop_tone_retire', 'rdx_record_stop_tone_callback',
        'rdx_record_stop_tone_play', 'rdx_record_stop_tone_post',
        'rdx_record_adv_busy', 'rdx_record_tone_config_busy',
        'rdx_record_binding_revoke_on_app_core', 'rdx_record_binding_revoke_post',
        'rdx_record_binding_revoke'):
        source += function(record, name)
    hold_worker = re.search(r'if \(msg\[1\] == RDX_RECORD_HOLD_FENCE\) \{(.*?)\n\s*continue;\n\s*\}',
                            function(record, 'rdx_record_task'), re.S)[1]
    source += '\nstatic void worker_release(u32 ticket) { int msg[4]={0,RDX_RECORD_HOLD_FENCE,(int)ticket};\n' + hold_worker + '\n}\n'
    task = function(record, 'rdx_record_task')
    binding_worker = re.search(r'if \(msg\[1\] == RDX_RECORD_BIND_FENCE\) \{(.*?)\n\s*continue;\n\s*\}', task, re.S)[1]
    source += '\nstatic void worker_binding(void) {\n' + binding_worker + '\n}\n'
    identity_guard = task[task.index('if ((msg[1] == RECORD_STATE_START ||'):]
    identity_guard = identity_guard[:identity_guard.index('if (msg[1] == RDX_RECORD_DUT_FENCE)')]
    guard = task[task.index('if (rdx_record_session_storage_blocked() &&'):]
    guard = guard[:guard.index('}else if(msg[1] == RECORD_STATE_STOP')]
    source += '\nstatic void worker_start_token(int state,u32 token) { int msg[4]={0,state,1,(int)token};\n' + (identity_guard + guard).replace('continue;', 'return;') + '\n}\n}\n'
    source += '\nstatic void worker_start(int state) { worker_start_token(state,rdx_record_session_token_capture()); }\n'
    implementations = [match.start() for match in re.finditer(r'static void rdx_record_process_impl\(void\)', record)]
    assert len(implementations) == 2
    for index, start in enumerate(implementations):
        prefix = function(record[start:], 'rdx_record_process_impl').split('/*----------------------------------------------------------------*/', 1)[0]
        source += prefix.replace('rdx_record_process_impl', f'process_gate_{index}') + '\n++processes;\n}\n'
    start = task.index('bool play_stop_tone = msg[1] == RECORD_STATE_STOP &&')
    end = task.index('if (app_stop_ticket) {', start)
    source += '\nstatic void worker_stop(int state) { int msg[4]={0,state};\n' + task[start:end] + '\n}\n'
    usb_worker = re.search(r'if \(msg\[1\] == RDX_RECORD_USB_FENCE\) \{(.*?)\n\s*continue;\n\s*\}', task, re.S)[1]
    source += '\nstatic void worker_usb(u32 ticket) { int msg[4]={0,RDX_RECORD_USB_FENCE,(int)ticket};\n' + usb_worker + '\n}\n'
    source += '\nstatic void rdx_app_record_state_upload_timer_stop(void) { ++upload_timer_stops; rdx_record_stream_only_start_cancel(); }\n'
    source += re.search(r'typedef struct \{[^}]*\} rdx_app_record_trigger_request_t;', app, re.S)[0]
    source += function(app, 'rdx_app_record_trigger_on_app_core')
    source += re.search(r'typedef struct \{[^}]*\} rdx_app_record_cmd_request_t;', app, re.S)[0]
    source += function(app, 'rdx_app_record_cmd_on_app_core')
    source += function(app, 'rdx_app_local_player_message_blocked')
    for name in ('rdx_app_storage_activity_check', 'rdx_app_storage_activity_is_busy',
                 'rdx_app_cdc_file_conflict', 'rdx_app_cdc_file_is_busy',
                 'rdx_app_factory_file_is_busy', 'rdx_pc_storage_is_busy'):
        source += function(app, name)
    record_format = (BASE / 'rdx_record_format.c').read_text(encoding='utf-8')
    source += function(record_format, 'rdx_record_format_download_acquire').replace(
        'rdx_record_format_download_acquire', 'actual_download_acquire')
    source += function(record_format, 'rdx_record_format_download_release')
    source += r'''
static int rdx_record_format_download_acquire(void) {
    int result=actual_download_acquire();
    if(result) return result;
    if(acquire_interleave==1)
        rdx_record_key_start_prepare(RDX_RECORD_INSPIRATION,RECORD_MODE_ONLINE,&owner);
    if(acquire_interleave==2) rdx_record_stream_only_release();
    if(acquire_interleave==3) writer_active=1;
    if(acquire_interleave==4) ++owner.transport_epoch;
    if(acquire_interleave==5) pc_refresh=1;
    if(acquire_interleave==6) {
        Record_info ordinary={RECORD_STATE_START+0x30,'1','0'};
        rdx_record_cmd_handle_internal(&ordinary,&owner);
    }
    return result;
}
'''
    source += re.sub(r'^#include.*$', '', (BASE / 'rdx_cdc_source.c').read_text(encoding='utf-8'), flags=re.M)
    source += TESTS
    with tempfile.TemporaryDirectory(prefix='record-key-session-') as temp:
        path = Path(temp) / 'session.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)
    print('Recorder sessions: pending cancellation, late-reply fence, frozen routes, pure streaming, worker failure cleanup and storage admission passed.')


if __name__ == '__main__':
    main()
