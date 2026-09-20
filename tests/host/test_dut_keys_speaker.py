"""Execute the production DUT queue/state machine with BLE/audio/scan boundaries mocked."""
import re
import ctypes
import tempfile
from pathlib import Path
from llvmlite import binding as llvm

from host_c_test_lib import ROOT, run_c_checks

BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
STUBS = r'''
typedef int bool;
#define true 1
#define false 0
#define NULL ((void *)0)
typedef unsigned long long size_t;
int snprintf(char *, size_t, const char *, ...);
int strcmp(const char *, const char *);
char *strstr(const char *, const char *);
char *strcpy(char *, const char *);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define KEY_IO_NUM0 10
#define KEY_IO_NUM1 11
#define KEY_IO_NUM2 12
#define KEY_IO_NUM3 13
#define KEY_IO_NUM4 14
#define NO_KEY 255
#define Q_CALLBACK 0
#define DUT_LOG(...) ((void)0)
#define local_irq_disable() ((void)0)
#define local_irq_enable() ((void)0)
#define jiffies_to_msecs(x) (x)
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
u32 jiffies;
struct { int dut_mode, current_func, motor_timer, motor_run; } rdx_dut_info;
struct { int goto_poweroff_flag; } app_var;
typedef struct { int run, process_state, scene, formate; } RecordStatus;
RecordStatus record;
#define RECORD_STATE_STOP 0
#define RECORD_STATE_START 1
#define RECORD_SCENE_CHAT 0
#define RECORD_SCENE_CALL 1
#define RECORD_FORMATE_OPUS_16K_MONO 2
#define RECORD_FORMATE_OPUS_16K_STERO 1
int mono_config, marks, record_starts;
#define TCFG_T2620_MEETING_MONO_DEBUG_MIC mono_config
u32 record_session_generation;
u32 product_binding, record_binding_generation=1;
int record_binding_revoke_pending, dut_record_stopping;
#define record_status record
u32 rdx_vm_get_bound_token(void) { return product_binding; }
u32 rdx_record_binding_token_capture(void);
u8 rdx_record_binding_allowed(void);
u8 rdx_record_dut_authorize(void);
void rdx_record_dut_authorization_revoke(void);
void rdx_record_binding_revoke(void);
bool rdx_dut_test_keys_active(void);
bool rdx_dut_rec_is_running(void);
bool rdx_dut_rec_call_is_running(void);
bool rdx_app_get_dut_status(void) { return rdx_dut_info.dut_mode; }
int rdx_record_online_session_bind_current(void) { return 1; }
const char *rdx_dut_get_current_func_name(void) { return "test"; }
void rdx_record_clear_marks(void) { marks=0; }
void rdx_record_process(void) { ++record_starts; }
#define REC_PROCESS_STATE_BUSY 1
RecordStatus *rdx_record_get_status(void) { return &record; }
int rdx_uxfile_sd_format_status_check(void) { return 0; }
int get_ota_status(void) { return 0; }
bool g_finalpack_end_pending;
typedef struct { u8 slot_index; u32 slot_generation, transport_epoch; } rdx_ble_async_token_t;
typedef struct { int rdx_ccc_configured, rdx_stream_tx_ready, mtu_size; } rdx_ble_link_state_t;
rdx_ble_link_state_t link;
u32 epoch;
int connected, powered, blocked, deferred, audio_state, open_fail, opens, cancels;
bool rdx_app_get_poweroff_flag(void) { return !powered; }
int sends, send_fail_at, post_fail, events, capture_during_send;
u32 event_gen[32], event_key[32];
char wire[8192];
int wire_size;
int record_post_fail, record_result, record_requests;
int transition_busy, transition_locks, cleanup_posts, record_binding_cleanup_error;
int rdx_vm_bound_transition_lock(void) {
    if (transition_busy) return -1;
    ++transition_locks; return 0;
}
void rdx_vm_bound_transition_unlock(void) { --transition_locks; }
static void rdx_record_binding_revoke_post(void *priv) { ++cleanup_posts; }
bool rdx_dut_key_scan(u8 value, u8 previous);
void rdx_dut_test_cancel(void);
void rdx_dut_close_current_func(void);
void rdx_dut_oled_stop(void);
void rdx_dut_motor_stop(void);
void rdx_dut_rec_stop(void);
void rdx_dut_rec_call_stop(void);
void rdx_dut_wifi_stop(void);
#define RDX_SUPPORT_MOTOR 1
#define FALSE 0
#define TRANSFER_BY_WIFI_OFF 0
int motor_power, timer_deleted, wifi_power, show_count;
void motor_off(void) { motor_power=0; }
void sys_timer_del(int id) { timer_deleted=id; }
void rdx_app_wifi_handle(int value) { wifi_power=value; }
static void rdx_dut_show(void) { ++show_count; }
int get_power_on_status(void) { return powered; }
int rdx_storage_lifecycle_business_blocked(void) { return blocked; }
int rdx_storage_lifecycle_shutdown_deferred(void) { return deferred; }
rdx_ble_link_state_t *rdx_ble_session_rdx_token_resolve(const rdx_ble_async_token_t *t, int active) {
    return connected && t->transport_epoch == epoch ? &link : NULL;
}
int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t, int active) {
    if (!connected) return 0;
    t->slot_index=0; t->slot_generation=1; t->transport_epoch=epoch; return 1;
}
int rdx_ble_server_send_for_token(u8 *p, u32 n, const rdx_ble_async_token_t *t) {
    ++sends;
    if (!rdx_ble_session_rdx_token_resolve(t,1) || sends==send_fail_at) return -1;
    if (n > link.mtu_size) return -1;
    memcpy(wire+wire_size,p,n); wire_size+=n; wire[wire_size]=0;
    if (capture_during_send) {
        capture_during_send=0; rdx_dut_key_scan(KEY_IO_NUM4,NO_KEY);
    }
    return 0;
}
int os_taskq_post_type(const char *task, int type, int count, int *msg) {
    if (post_fail) return -1;
    if (count==3) return 0; /* The test explicitly steps app_core service. */
    event_gen[events]=msg[2]; event_key[events]=msg[3]; ++events; return 0;
}
int sys_timer_modify(int id, int ms) { return 0; }
int rdx_dut_speaker_open(void) {
    if (open_fail) return -1;
    ++opens; audio_state=DUT_AUDIO_STARTING; return 0;
}
void rdx_dut_speaker_cancel(void) { ++cancels; }
int rdx_dut_speaker_state(void) { return audio_state; }
void rdx_dut_speaker_reap(void) { audio_state=DUT_AUDIO_IDLE; }
int rdx_record_dut_stop_request(u32 ticket) {
    rdx_record_dut_authorization_revoke(); ++record_requests; return record_post_fail;
}
int rdx_record_dut_stop_poll(u32 ticket) { return record_result; }
void rdx_dut_msg_handle(void) { rdx_dut_info.dut_mode=0; rdx_dut_test_cancel(); }
'''

TESTS = r'''
static void rdx_dut_cmd_async_handle(u8 type, u8 value) {
    if (type==DUT_CMD_DUT_MODE) rdx_dut_info.dut_mode=value;
    if (type==DUT_CMD_REC && !value) dut_record_stop();
    if (value && rdx_dut_info.current_func==DUT_FUNC_NONE) {
        if (type==DUT_CMD_REC) rdx_dut_rec_start();
        if (type==DUT_CMD_REC_CALL) rdx_dut_rec_call_start();
    }
    if (type==DUT_CMD_OLED && value && rdx_dut_info.current_func==DUT_FUNC_NONE)
        rdx_dut_info.current_func=DUT_FUNC_OLED;
}
static void boot(void) {
    rdx_dut_info.dut_mode=1; rdx_dut_info.current_func=DUT_FUNC_NONE;
    app_var.goto_poweroff_flag=0; g_finalpack_end_pending=0;
    connected=powered=1; blocked=deferred=0; epoch=1; jiffies=100;
    link.rdx_ccc_configured=link.rdx_stream_tx_ready=1; link.mtu_size=20;
    dut_head=dut_count=dut_emergency_cancel=dut_queue_abort=dut_key_gate=0;
    rdx_dut_info.motor_timer=rdx_dut_info.motor_run=0;
    motor_power=timer_deleted=wifi_power=show_count=0;
    dut_waiting=dut_stopping=dut_record_wait=dut_record_posted=0;
    dut_service_timer=1; dut_key_generation=1;
    memset(dut_key_cutoff,0,sizeof(dut_key_cutoff));
    memset(dut_key_quarantined,0,sizeof(dut_key_quarantined));
    audio_state=DUT_AUDIO_IDLE; open_fail=opens=cancels=0;
    sends=send_fail_at=post_fail=events=capture_during_send=0;
    wire_size=0; wire[0]=0; record_requests=record_post_fail=0; record_result=-2;
    memset(&record,0,sizeof(record));
    record_session_generation=1; mono_config=3; marks=record_starts=0;
    product_binding=1; record_binding_generation=1;
    record_binding_revoke_pending=dut_record_stopping=0;
    transition_busy=transition_locks=cleanup_posts=record_binding_cleanup_error=0;
    rdx_record_dut_authorization_revoke();
}
static void key_start(void) { dut_parse_test(FT_KEY_LAYOUT,"1"); dut_service(NULL); }
static void speaker_start(void) { dut_parse_test(FT_SPEAKER,"1"); dut_service(NULL); }
static void playing(void) { audio_state=DUT_AUDIO_PLAYING; dut_service(NULL); }
static void finished(void) { audio_state=DUT_AUDIO_DONE; dut_service(NULL); }
int test_layout_and_keys(void) {
    boot(); capture_during_send=1; key_start();
    CHECK(!strcmp(wire,"*DEV#custom#ft_key_layout#A,B&C,D&E#"));
    CHECK(events==1 && dut_key_gate && rdx_dut_info.current_func==DUT_FUNC_KEY);
    dut_key_event(event_gen[0],event_key[0]);
    CHECK(strstr(wire,"A,B&C,D&E#*DEV#custom#ft_key_event#E#"));
    for (int i=0;i<5;++i) {
        int n=events;
        CHECK(rdx_dut_key_scan(KEY_IO_NUM0+i,NO_KEY));
        CHECK(events==n+1);
        CHECK(rdx_dut_key_scan(KEY_IO_NUM0+i,KEY_IO_NUM0+i));
        CHECK(events==n+1); /* HOLD does not generate another press. */
        dut_key_event(event_gen[n],event_key[n]);
        char expected[32]; snprintf(expected,sizeof(expected),"ft_key_event#%c#",'A'+i);
        CHECK(strstr(wire,expected));
        CHECK(rdx_dut_key_scan(NO_KEY,KEY_IO_NUM0+i));
    }
    u32 old=dut_key_generation;
    key_start(); CHECK(dut_key_generation!=old);
    int n=sends; dut_key_event(old,KEY_IO_NUM0); CHECK(sends==n);
    dut_parse_test(FT_KEY,"0"); dut_service(NULL);
    CHECK(!dut_key_gate && rdx_dut_info.current_func==DUT_FUNC_NONE && sends==n);
    dut_key_event(dut_key_generation-1,KEY_IO_NUM0); CHECK(sends==n);
    CHECK(rdx_dut_key_consume(KEY_IO_NUM0,jiffies));
    CHECK(rdx_dut_key_scan(KEY_IO_NUM0,KEY_IO_NUM0)); /* held across stop */
    rdx_dut_key_scan(NO_KEY,KEY_IO_NUM0); ++jiffies;
    CHECK(!rdx_dut_key_scan(KEY_IO_NUM0,NO_KEY));
    CHECK(!rdx_dut_key_consume(KEY_IO_NUM0,jiffies));
    CHECK(rdx_dut_key_consume(KEY_IO_NUM0,jiffies-1));
    jiffies+=0x80000000u;
    CHECK(!rdx_dut_key_consume(KEY_IO_NUM0,jiffies)); /* old factory cutoff expires */
    return 0;
}
int test_strict_parser_and_gates(void) {
    boot();
    const char *invalid[]={"2", "01", "1x", " 1", "-1"};
    for(int i=0;i<5;++i) {
        CHECK(dut_parse_test(FT_KEY_LAYOUT,invalid[i]));
        CHECK(dut_parse_test(FT_SPEAKER,invalid[i]));
    }
    CHECK(dut_parse_test(FT_KEY,"1")); CHECK(!dut_count);
    CHECK(dut_parse_test(FT_SPEAKER,"")); CHECK(!dut_count);
    CHECK(!dut_parse_test("ft_key_layout_suffix","1"));
    CHECK(!dut_parse_test("ft_key_dut_enable","1"));
    for(int i=0;i<7;++i) {
        boot();
        if(i==0) rdx_dut_info.dut_mode=0;
        if(i==1) blocked=1;
        if(i==2) link.rdx_ccc_configured=0;
        if(i==3) link.rdx_stream_tx_ready=0;
        if(i==4) g_finalpack_end_pending=1;
        if(i==5) app_var.goto_poweroff_flag=1;
        if(i==6) rdx_dut_info.current_func=DUT_FUNC_OLED;
        key_start(); speaker_start(); CHECK(!opens && !sends && !dut_key_gate);
    }
    return 0;
}
int test_factory_entry_before_write_ready(void) {
    boot(); rdx_dut_info.dut_mode=0;
    link.rdx_stream_tx_ready=0;
    CHECK(!dut_enqueue(DUT_CMD_DUT_MODE,1)); dut_service(NULL);
    CHECK(rdx_dut_info.dut_mode==1 && !dut_count);
    CHECK(dut_parse_test(FT_KEY_LAYOUT,"")); dut_service(NULL);
    CHECK(!sends && !dut_key_gate);
    link.rdx_stream_tx_ready=1;
    CHECK(dut_parse_test(FT_KEY_LAYOUT,"")); dut_service(NULL);
    CHECK(!strcmp(wire,"*DEV#custom#ft_key_layout#A,B&C,D&E#"));
    CHECK(dut_key_gate);
    boot(); rdx_dut_info.dut_mode=0;
    CHECK(!dut_enqueue(DUT_CMD_DUT_MODE,1)); ++epoch; dut_service(NULL);
    CHECK(!rdx_dut_info.dut_mode);
    return 0;
}
int test_key_to_legacy_record(void) {
    for (int call=0; call<2; ++call) {
        boot(); key_start();
        u32 old_generation=dut_key_generation;
        CHECK(rdx_dut_key_scan(KEY_IO_NUM0,NO_KEY));
        CHECK(!dut_enqueue(call ? DUT_CMD_REC_CALL : DUT_CMD_REC,1));
        dut_service(NULL);
        CHECK(rdx_dut_info.current_func==(call ? DUT_FUNC_REC_CALL : DUT_FUNC_REC));
        CHECK(!dut_key_gate && !rdx_dut_test_keys_active());
        int previous_sends=sends;
        dut_key_event(old_generation,KEY_IO_NUM0);
        CHECK(sends==previous_sends);
        CHECK(rdx_dut_key_scan(KEY_IO_NUM0,KEY_IO_NUM0));
    }
    boot(); key_start();
    CHECK(!dut_enqueue(DUT_CMD_REC,1)); ++epoch; dut_service(NULL);
    CHECK(rdx_dut_info.current_func!=DUT_FUNC_REC);
    return 0;
}
int test_factory_record_profiles_and_new_sessions(void) {
    for (int mic=0; mic<=3; ++mic) {
        boot(); mono_config=mic;
        for (int attempt=0; attempt<4; ++attempt) {
            int call=attempt%2;
            record.run=RECORD_STATE_STOP;
            rdx_dut_info.current_func=DUT_FUNC_NONE;
            marks=7;
            u32 previous=record_session_generation;
            CHECK(!dut_enqueue(call ? DUT_CMD_REC_CALL : DUT_CMD_REC,1));
            dut_service(NULL);
            CHECK(record.run==RECORD_STATE_START);
            CHECK(record.scene==(call ? RECORD_SCENE_CALL : RECORD_SCENE_CHAT));
            CHECK(record.formate==(!call && mic ? RECORD_FORMATE_OPUS_16K_MONO : RECORD_FORMATE_OPUS_16K_STERO));
            CHECK(record_session_generation==previous+1 && !marks);
            CHECK(record_starts==attempt+1);
            CHECK(!dut_enqueue(call ? DUT_CMD_REC_CALL : DUT_CMD_REC,1));
            dut_service(NULL);
            CHECK(record_session_generation==previous+1 && record_starts==attempt+1);
        }
    }
    return 0;
}
int test_unbound_factory_authorization(void) {
    for (int call=0; call<2; ++call) {
        boot(); product_binding=0;
        CHECK(!rdx_record_binding_allowed() && !rdx_record_session_allowed());
        CHECK(!dut_enqueue(call ? DUT_CMD_REC_CALL : DUT_CMD_REC,1));
        dut_service(NULL);
        CHECK(record_starts==1 && record.run==RECORD_STATE_START);
        u32 token=rdx_record_session_token_capture();
        CHECK(token && rdx_record_session_token_is_current(token));
        CHECK(!rdx_record_binding_allowed() && !rdx_record_binding_token_is_current(token));
        CHECK(!product_binding); /* Never publish a fake persisted binding. */
        rdx_dut_test_session_revoke();
        CHECK(!rdx_record_session_token_is_current(token));
        dut_service(NULL);
        CHECK(dut_record_wait && record_requests==1);
        record_result=0; dut_service(NULL);
        record.run=RECORD_STATE_STOP;
        CHECK(!dut_enqueue(call ? DUT_CMD_REC_CALL : DUT_CMD_REC,1));
        dut_service(NULL);
        CHECK(record_starts==2 && rdx_record_session_allowed());
        CHECK(!rdx_record_session_token_is_current(token));
    }
    return 0;
}
int test_factory_authorization_revocation(void) {
    for (int reason=0; reason<8; ++reason) {
        boot(); product_binding=0;
        rdx_dut_rec_start();
        u32 token=rdx_record_session_token_capture(); CHECK(token);
        if (reason==0) ++epoch;
        if (reason==1) connected=0;
        if (reason==2) rdx_dut_info.dut_mode=0;
        if (reason==3) app_var.goto_poweroff_flag=1;
        if (reason==4) blocked=1;
        if (reason==5) record_binding_revoke_pending=1;
        if (reason==6) dut_record_stopping=1;
        if (reason==7) rdx_dut_info.current_func=DUT_FUNC_NONE;
        CHECK(!rdx_record_session_token_is_current(token));
        CHECK(!rdx_record_session_allowed());
        /* Binding the product afterwards must not revive an old DUT request. */
        product_binding=1;
        CHECK(!rdx_record_session_token_is_current(token));
    }
    boot(); product_binding=0; connected=0;
    rdx_dut_rec_start(); CHECK(!record_starts);
    boot(); product_binding=0; rdx_dut_info.dut_mode=0;
    rdx_dut_rec_start(); CHECK(!record_starts);
    boot(); product_binding=0; record.process_state=REC_PROCESS_STATE_BUSY;
    rdx_dut_rec_start(); CHECK(!record_starts);
    return 0;
}
int test_unbound_speaker_stop_then_record(void) {
    boot(); product_binding=0;
    speaker_start(); playing();
    dut_parse_test(FT_SPEAKER,"0");
    dut_enqueue(DUT_CMD_REC,1); dut_service(NULL);
    CHECK(dut_waiting==2 && !record_starts && dut_count==1);
    finished();
    CHECK(record_starts==1 && rdx_record_session_allowed());
    CHECK(!rdx_record_binding_allowed());
    return 0;
}
int test_factory_audio_guard_and_failure(void) {
    boot(); product_binding=0;
    CHECK(rdx_record_audio_start_enter(0)!=0 && !transition_locks);
    rdx_dut_rec_start();
    u32 token=rdx_record_session_token_capture(); CHECK(token);
    CHECK(rdx_record_audio_start_enter(token)==0 && transition_locks==1);
    rdx_record_audio_start_exit(0); CHECK(!transition_locks && !cleanup_posts);
    transition_busy=1;
    CHECK(rdx_record_audio_start_enter(token)!=0 && !transition_locks);
    transition_busy=0;
    ++epoch;
    CHECK(rdx_record_audio_start_enter(token)!=0 && !transition_locks);
    --epoch;
    CHECK(rdx_record_audio_start_enter(token)==0);
    rdx_record_audio_start_exit(-1);
    CHECK(!transition_locks && cleanup_posts==1 && record_binding_revoke_pending);
    CHECK(!rdx_record_session_token_is_current(token) && !rdx_record_binding_allowed());
    CHECK(!rdx_record_dut_authorize());
    /* The token namespaces remain disjoint at generation wrap. */
    record_binding_revoke_pending=0; record_binding_generation=0x7fffffff;
    rdx_record_binding_revoke();
    CHECK(record_binding_generation==1);
    return 0;
}
int test_speaker_order_and_idempotence(void) {
    boot(); speaker_start(); CHECK(opens==1 && !sends && dut_waiting==1);
    dut_parse_test(FT_SPEAKER,"1"); dut_service(NULL); CHECK(opens==1 && !sends);
    playing(); CHECK(opens==1 && strstr(wire,"ft_speaker#1#*DEV#custom#ft_speaker#1#"));
    CHECK(rdx_dut_key_scan(KEY_IO_NUM4,NO_KEY) && !events);
    dut_parse_test(FT_SPEAKER,"0"); dut_parse_test(FT_KEY_LAYOUT,"1");
    dut_service(NULL); CHECK(dut_waiting==2 && !strstr(wire,"ft_key_layout"));
    CHECK(rdx_dut_info.current_func==DUT_FUNC_SPEAKER);
    finished(); CHECK(rdx_dut_info.current_func==DUT_FUNC_KEY);
    CHECK(strstr(wire,"ft_speaker#0#*DEV#custom#ft_key_layout"));
    dut_parse_test(FT_SPEAKER,"0"); dut_service(NULL);
    CHECK(rdx_dut_info.current_func==DUT_FUNC_KEY); /* wrong-item stop is harmless */
    dut_parse_test(FT_KEY,"0"); dut_parse_test(FT_SPEAKER,"1"); dut_service(NULL);
    CHECK(opens==2 && rdx_dut_info.current_func==DUT_FUNC_SPEAKER);
    return 0;
}
int test_failure_and_session_cleanup(void) {
    boot(); send_fail_at=2; key_start(); CHECK(!dut_key_gate && !rdx_dut_info.current_func);
    boot(); key_start(); send_fail_at=sends+1;
    dut_key_event(dut_key_generation,KEY_IO_NUM0); CHECK(!dut_key_gate);
    boot(); open_fail=1; speaker_start(); CHECK(!sends && !rdx_dut_info.current_func);
    boot(); speaker_start(); finished(); CHECK(!sends && !rdx_dut_info.current_func);
    boot(); speaker_start(); send_fail_at=1; playing();
    CHECK(dut_stopping && cancels); finished(); CHECK(!rdx_dut_info.current_func);
    boot(); speaker_start(); playing(); ++epoch; dut_service(NULL);
    CHECK(cancels && dut_stopping); int n=sends; finished(); CHECK(sends==n);
    boot(); dut_parse_test(FT_SPEAKER,"1"); ++epoch; dut_service(NULL); CHECK(!opens);
    boot(); speaker_start(); jiffies+=2001; dut_service(NULL); CHECK(cancels && !sends);
    boot(); key_start(); rdx_dut_test_session_revoke(); dut_service(NULL);
    CHECK(!dut_key_gate && !rdx_dut_info.current_func);
    boot(); speaker_start(); playing(); link.rdx_stream_tx_ready=0; dut_service(NULL);
    CHECK(cancels && dut_stopping);
    boot(); speaker_start(); playing(); powered=0; dut_service(NULL);
    CHECK(!rdx_dut_info.dut_mode && cancels);
    return 0;
}
int test_queue_pressure_and_record_barrier(void) {
    boot(); speaker_start(); playing();
    for(int i=0;i<DUT_QUEUE_CAPACITY;++i) CHECK(!dut_enqueue(DUT_CMD_SPEAKER,1));
    CHECK(dut_enqueue(DUT_CMD_SPEAKER,0)!=0 && dut_emergency_cancel);
    dut_service(NULL); CHECK(!dut_count && cancels && dut_stopping);
    finished(); CHECK(!rdx_dut_info.current_func);
    boot(); key_start(); post_fail=1; rdx_dut_key_scan(KEY_IO_NUM0,NO_KEY);
    dut_service(NULL); CHECK(!dut_key_gate);
    boot(); rdx_dut_info.current_func=DUT_FUNC_REC;
    record_post_fail=1; dut_enqueue(DUT_CMD_REC,0); dut_enqueue(DUT_CMD_SPEAKER,1);
    dut_service(NULL); CHECK(dut_record_wait && dut_count==1 && !opens);
    dut_service(NULL); CHECK(record_requests==2 && !opens);
    record_post_fail=0; dut_service(NULL); CHECK(dut_record_posted && !opens);
    record_result=0; dut_service(NULL); CHECK(opens==1 && !dut_record_wait);
    return 0;
}
static void overflow_legacy_stop(int command) {
    for(int i=0;i<DUT_QUEUE_CAPACITY;++i) dut_enqueue(command,1);
    dut_enqueue(command,0);
    dut_service(NULL);
}
int test_legacy_overflow_cleanup(void) {
    boot(); rdx_dut_info.current_func=DUT_FUNC_MOTOR;
    rdx_dut_info.motor_timer=42; rdx_dut_info.motor_run=motor_power=1;
    overflow_legacy_stop(DUT_CMD_MOTOR);
    CHECK(!dut_count && !motor_power && timer_deleted==42);
    CHECK(!rdx_dut_info.motor_timer && !rdx_dut_info.motor_run);
    CHECK(rdx_dut_info.current_func==DUT_FUNC_NONE && show_count==1);
    boot(); rdx_dut_info.current_func=DUT_FUNC_OLED;
    overflow_legacy_stop(DUT_CMD_OLED);
    CHECK(rdx_dut_info.current_func==DUT_FUNC_NONE && show_count==1);
    boot(); rdx_dut_info.current_func=DUT_FUNC_WIFI; wifi_power=1;
    overflow_legacy_stop(DUT_CMD_WIFI);
    CHECK(!wifi_power && rdx_dut_info.current_func==DUT_FUNC_NONE);
    for(int i=0;i<2;++i) {
        boot(); rdx_dut_info.current_func=i ? DUT_FUNC_REC_CALL : DUT_FUNC_REC;
        record_post_fail=1;
        overflow_legacy_stop(i ? DUT_CMD_REC_CALL : DUT_CMD_REC);
        CHECK(!dut_count && dut_record_wait && !dut_record_posted);
        CHECK(rdx_dut_info.current_func==(i ? DUT_FUNC_REC_CALL : DUT_FUNC_REC));
        record_post_fail=0; dut_service(NULL);
        CHECK(dut_record_posted && dut_record_wait);
        record_result=0; dut_service(NULL);
        CHECK(!dut_record_wait && rdx_dut_info.current_func==DUT_FUNC_NONE);
    }
    boot(); rdx_dut_info.current_func=DUT_FUNC_FORMAT;
    overflow_legacy_stop(DUT_CMD_FORMAT);
    CHECK(!dut_count && rdx_dut_info.current_func==DUT_FUNC_FORMAT);
    /* A session revoke without overflow must not acquire legacy ownership. */
    boot(); rdx_dut_info.current_func=DUT_FUNC_MOTOR; motor_power=1;
    rdx_dut_test_session_revoke(); dut_service(NULL);
    CHECK(motor_power && rdx_dut_info.current_func==DUT_FUNC_MOTOR);
    return 0;
}
'''

AUDIO_STUBS = r'''
typedef unsigned char u8;
typedef short s16;
typedef int s32;
typedef unsigned long long size_t;
void *memset(void *,int,size_t);
#define NULL ((void *)0)
#define APP_AUDIO_STATE_IDLE 0
#define APP_AUDIO_STATE_MUSIC 1
#define AppVol_BT_MUSIC 0
#define AUDIO_MUTE_DEFAULT 0
#define AUDIO_UNMUTE_DEFAULT 1
#define DATA_BIT_WIDE_24BIT 1
#define WRITE_MODE_BLOCK 0
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
struct audio_dac_channel { int unused; };
struct audio_dac_channel_attr { int write_mode,delay_time,protect_time; };
struct { int channel; } dac_hdl;
int volume, maximum, scene, rate, working, width;
int task_fail, channel_fail, attr_fail, start_fail, rate_fail, write_fail;
int close_count, state_exits, deleted, calls, bad_pcm, peak, first_playing;
int maximum_seen, cancel_after;
int muted;
int in_worker, wrong_control_task, delayed_close_pending, delayed_close_owner;
void control_task_check(void) { if (in_worker) ++wrong_control_task; }
void (*worker)(void *);
void rdx_dut_speaker_cancel(void);
int audio_general_out_dev_bit_width(void) { return width; }
int audio_dac_is_working(void *d) { return working; }
int audio_dac_get_sample_rate(void *d) { return rate; }
int audio_dac_set_sample_rate(void *d,int r) { control_task_check(); if(rate_fail && r==48000) return -1; rate=r; return 0; }
int app_audio_get_state(void) { return scene; }
int app_audio_get_dac_digital_mute(void) { return muted; }
void app_audio_mute(int value) { muted=value==AUDIO_MUTE_DEFAULT; }
int app_audio_get_volume(int s) { return volume; }
int app_audio_volume_max_query(int s) { return maximum; }
void app_audio_state_switch(int s,int max,void *d) { scene=s; }
void app_audio_set_volume(int s,int v,int fade) { volume=v; if(v==maximum) maximum_seen++; }
void app_audio_state_exit(int s) { scene=0; ++state_exits; }
void audio_dac_set_volume(void *d,int v) { }
int audio_dac_new_channel(void *d,void *c) { return channel_fail; }
int audio_dac_channel_set_attr(void *c,void *a) { return attr_fail; }
int audio_dac_start(void *d) { control_task_check(); return start_fail; }
void audio_dac_channel_start(void *c) { control_task_check(); working=1; }
void audio_dac_channel_close(void *c) {
    control_task_check(); ++close_count; working=0;
    /* SDK close schedules audio_dac_delay_off_timeout on the caller. */
    delayed_close_pending=1; delayed_close_owner=in_worker;
}
int os_task_create(void (*fn)(void *),void *p,int a,int b,int c,const char *n) { worker=fn; return task_fail; }
void os_task_del(const char *n) { ++deleted; }
void os_time_dly(int n) { }
int audio_dac_channel_write(void *c,void *p,int n);
struct lp_target { const char *name; u8 (*is_idle)(void); };
#define REGISTER_LP_TARGET(n) struct lp_target n
'''

AUDIO_TESTS = r'''
int audio_dac_channel_write(void *c,void *p,int n) {
    ++calls;
    if (calls>1 && speaker_state==DUT_AUDIO_PLAYING) first_playing=1;
    int bytes=width ? 4 : 2;
    int take=n < 192 ? n : 192; /* Force partial writes across cycles. */
    if (write_fail) return -1;
    for(int i=0;i<take/bytes;++i) {
        int expected=sin1k_48k_16bit[(peak/dac_hdl.channel)%48];
        int actual=width ? ((s32 *)p)[i] : ((s16 *)p)[i];
        if (width) expected*=256;
        if (actual!=expected) bad_pcm=1;
        ++peak;
    }
    if (calls==cancel_after) rdx_dut_speaker_cancel();
    return take;
}
static void audio_boot(void) {
    speaker_state=DUT_AUDIO_IDLE; speaker_cancel=0; dac_hdl.channel=1;
    volume=3; maximum=15; scene=0; rate=44100; working=width=0;
    muted=1;
    in_worker=wrong_control_task=delayed_close_pending=delayed_close_owner=0;
    task_fail=channel_fail=attr_fail=start_fail=rate_fail=write_fail=0;
    close_count=state_exits=deleted=calls=bad_pcm=peak=first_playing=maximum_seen=0;
    cancel_after=23;
}
int test_sine_volume_and_cleanup(void) {
    int volumes[]={0,3,15};
    for(int v=0;v<3;++v) for(int w=0;w<2;++w) for(int ch=1;ch<=2;++ch) {
        audio_boot(); width=w; dac_hdl.channel=ch;
        volume=volumes[v]; muted=v!=1;
        CHECK(!rdx_dut_speaker_open()); CHECK(speaker_state==DUT_AUDIO_STARTING);
        CHECK(!muted && volume==maximum);
        CHECK(rdx_dut_speaker_open()!=0); /* No duplicate worker/save. */
        in_worker=1; worker(NULL); in_worker=0;
        CHECK(speaker_state==DUT_AUDIO_DONE && first_playing && !bad_pcm && peak);
        CHECK(maximum_seen==1 && rate==48000);
        CHECK(close_count==0 && working); /* Retain until app_core reaps. */
        rdx_dut_speaker_reap(); rdx_dut_speaker_reap();
        CHECK(close_count==1 && !working && rate==44100 && !wrong_control_task);
        CHECK(delayed_close_pending && !delayed_close_owner);
        CHECK(volume==volumes[v] && scene==0 && muted==(v!=1) && state_exits==1);
        CHECK(deleted==1 && speaker_state==DUT_AUDIO_IDLE);
    }
    return 0;
}
int test_sine_failure_rollback(void) {
    for(int fault=0;fault<7;++fault) {
        audio_boot();
        if(fault==0) task_fail=1;
        if(fault==1) channel_fail=1;
        if(fault==2) attr_fail=1;
        if(fault==3) start_fail=1;
        if(fault==4) rate_fail=1;
        if(fault==5) write_fail=1;
        int ret=rdx_dut_speaker_open();
        if(fault<5) {
            CHECK(ret && speaker_state==DUT_AUDIO_IDLE && volume==3 && muted);
            CHECK(close_count==(fault==0 || fault==2 || fault==3));
            CHECK(!working && rate==44100 && !wrong_control_task && !deleted);
            continue;
        }
        CHECK(!ret);
        if(fault==6) rdx_dut_speaker_cancel();
        in_worker=1; worker(NULL); in_worker=0;
        CHECK(speaker_state==DUT_AUDIO_DONE && !first_playing);
        CHECK(!close_count);
        rdx_dut_speaker_reap(); CHECK(deleted==1 && volume==3 && scene==0 && muted);
        CHECK(close_count==1 && !working && !wrong_control_task);
        CHECK(delayed_close_pending && !delayed_close_owner);
    }
    audio_boot(); working=1; CHECK(rdx_dut_speaker_open()!=0 && !maximum_seen);
    audio_boot(); scene=2; CHECK(rdx_dut_speaker_open()!=0 && !maximum_seen);
    return 0;
}
'''


KEY_STUBS = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef int bool;
#define NULL ((void *)0)
#define NO_KEY 255
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define KEY_DRIVER_TYPE_CTMU_TOUCH 2
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define TCFG_MAX_HOLD_SEC ((KEY_ACTION_HOLD_8SEC << 8) | 8)
#define jiffies_to_msecs(x) (x)
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
u32 jiffies;
int jiffies_offset_to_msec(u32 a,u32 b) { return b-a; }
u8 g_is_key_active;
int filtered, emitted, last_event, last_value;
u8 input;
u8 read_key(void) { return input; }
bool key_test_scan_filter(u8 type,u8 value,u8 previous) { return filtered; }
void key_down_event_handler(u8 value) { }
'''

KEY_TESTS = r'''
static struct key_driver_para scan;
static struct key_driver_ops ops = {
    .param=&scan, .get_value=read_key, .long_time=3,
    .hold_time=5, .click_delay_time=2,
};
static void step(u8 value) { input=value; jiffies+=10; key_driver_scan(&ops); }
static void scan_boot(void) {
    key_event_reset(); filtered=emitted=0; jiffies=0;
    scan.last_key=NO_KEY; scan.press_cnt=scan.click_delay_cnt=0;
}
static void click(void) { step(10); step(NO_KEY); }
static void settle(void) { step(NO_KEY); step(NO_KEY); step(NO_KEY); }
int test_scan_clears_pending_multiclick(void) {
    scan_boot(); click(); click(); settle();
    CHECK(emitted==1 && last_event==KEY_ACTION_DOUBLE_CLICK);
    scan_boot(); click(); CHECK(!emitted);
    filtered=1; step(NO_KEY); /* DUT begins before NO_KEY translation. */
    step(10); step(NO_KEY); CHECK(!emitted);
    filtered=0; click(); settle();
    CHECK(emitted==1 && last_event==KEY_ACTION_CLICK && last_value==10);
    return 0;
}
int test_scan_clears_pretest_hold(void) {
    scan_boot(); step(10); step(10); step(10);
    CHECK(emitted==1 && last_event==KEY_ACTION_LONG && get_key_hold(10,0));
    filtered=1; step(10);
    CHECK(!get_key_hold(10,0));
    jiffies+=9000; step(NO_KEY); /* Release remains consumed. */
    CHECK(emitted==1);
    filtered=0; click(); settle();
    CHECK(emitted==2 && last_event==KEY_ACTION_CLICK);
    return 0;
}
'''


def c_function(source, name):
    start = re.search(r'^(?:static )?(?:void|bool|int|u8) ' + name + r'\(', source, re.M).start()
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


def check_key_scan(temp):
    driver_dir = ROOT / 'SDK/apps/common/device/key'
    header = (driver_dir / 'key_driver.h').read_text(encoding='utf-8')
    types = re.search(r'enum key_action \{.*?\};', header, re.S)[0]
    for name in ('key_event', 'key_driver_para', 'key_driver_ops'):
        types += re.search(r'struct ' + name + r' \{.*?\};', header, re.S)[0]
    adapter = (ROOT / 'SDK/apps/earphone/message/adapter/key.c').read_text(encoding='utf-8')
    # Keep the real shared click/hold state, reset and translation together.
    adapter = adapter[adapter.index('struct key_hold {'):adapter.index('static int combination_key_translate')]
    dispatch = '''void key_event_handler(struct key_event *key) {
        if (!multi_clicks_translate(key) && key->event!=KEY_ACTION_NO_KEY) {
            ++emitted; last_event=key->event; last_value=key->value;
        }
    }\n'''
    driver = (driver_dir / 'key_driver.c').read_text(encoding='utf-8')
    path = Path(temp) / 'key_scan.c'
    path.write_text(KEY_STUBS + types + adapter + dispatch +
                    c_function(driver, 'key_driver_scan') + KEY_TESTS, encoding='utf-8')
    run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', KEY_TESTS), native=True)


def main():
    crt = ctypes.CDLL('msvcrt.dll')
    for name in ('snprintf', 'strcmp', 'strstr', 'strcpy', 'memcpy', 'memset'):
        export = '_snprintf' if name == 'snprintf' else name
        llvm.add_symbol(name, ctypes.cast(getattr(crt, export), ctypes.c_void_p).value)
    source = (BASE / 'rdx_dut.c').read_text(encoding='utf-8')
    header = (BASE / 'rdx_dut.h').read_text(encoding='utf-8')
    enums = re.search(r'typedef enum \{.*?\} rdx_dut_func_e;', header, re.S)[0]
    enums += re.search(r'typedef enum \{.*?\} DUT_CMD_TYPE;', source, re.S)[0]
    defines = '\n'.join(line for line in header.splitlines() if line.startswith('#define FT_'))
    audio = (BASE / 'rdx_dut_speaker.h').read_text(encoding='utf-8')
    core = source.split('/* DUT_TEST_CORE_BEGIN:', 1)[1]
    core = core.split('*/', 1)[1].split('/* DUT_TEST_CORE_END */', 1)[0]
    # Exercise the real legacy resource cleanup, not a state-only substitute.
    legacy = ''.join(c_function(source, name) for name in (
        'rdx_dut_close_current_func', 'rdx_dut_oled_stop', 'rdx_dut_motor_stop',
        'rdx_dut_rec_stop', 'rdx_dut_rec_call_stop', 'rdx_dut_wifi_stop'))
    record_source = (BASE / 'rdx_record.c').read_text(encoding='utf-8')
    legacy += ''.join(c_function(record_source, name) for name in (
        'rdx_record_format_for_session', 'rdx_record_prepare_new_session'))
    legacy += ''.join(c_function(source, name) for name in (
        'rdx_dut_rec_start', 'rdx_dut_rec_call_start',
        'rdx_dut_rec_is_running', 'rdx_dut_rec_call_is_running'))
    authorization = record_source.split('u8 rdx_record_binding_allowed(void)', 1)[1]
    authorization = 'u8 rdx_record_binding_allowed(void)' + authorization.split(
        '/* A successful enter must be paired', 1)[0]
    authorization += ''.join(c_function(record_source, name) for name in (
        'rdx_record_audio_start_enter', 'rdx_record_audio_start_exit',
        'rdx_record_binding_revoke'))
    tests = re.findall(r'int (test_\w+)\(void\)', TESTS)
    with tempfile.TemporaryDirectory(prefix='t2620_dut_') as temp:
        path = Path(temp) / 'dut.c'
        path.write_text(audio + enums + '\n' + defines + STUBS + authorization + core + legacy + TESTS, encoding='utf-8')
        run_c_checks(path, tests, native=True)
        check_key_scan(temp)
        speaker = (BASE / 'rdx_dut_speaker.c').read_text(encoding='utf-8')
        speaker = re.sub(r'^#include.*$', '', speaker, flags=re.M)
        # The worker's final park is an OS lifetime detail, excluded from this
        # synchronous harness; all open/write/close/restore code is unchanged.
        speaker = speaker.replace('while (1) {\n        os_time_dly(100);\n    }', 'return;')
        sine = (ROOT / 'SDK/audio/common/pcm_data/sine_pcm.c').read_text(encoding='utf-8')
        sine = re.search(r'const s16 sin1k_48k_16bit\[48\] = \{.*?\};', sine, re.S)[0]
        path = Path(temp) / 'speaker.c'
        path.write_text(audio + AUDIO_STUBS + sine + speaker + AUDIO_TESTS, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', AUDIO_TESTS), native=True)
    print(f'DUT keys/speaker: {len(tests)} production state-machine scenarios passed.')
    print('DUT sine worker: maximum-volume rollback, 16/24-bit mono/stereo and partial-write phase passed.')
    print('DUT key scan: real scan/adapter clears stale multi-click and long-hold state.')


if __name__ == '__main__':
    main()
