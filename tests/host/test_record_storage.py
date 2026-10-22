"""Run production async recorder with deterministic worker/I/O scheduling.
Hardware latency and library behavior still require on-device acceptance.
"""
import re
import tempfile
from pathlib import Path
from host_c_test_lib import ROOT, function, run_c_checks


def main():
    check_record_limit()
    check_format_reply()
    check_reconnect()
    check_stream()
    check_snapshot()
    base = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    source = (base / 'rdx_record_storage.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include .*\n', '', source, flags=re.M)
    record = (base / 'rdx_record.c').read_text(encoding='utf-8')
    assert record.count('rdx_record_storage_push(d, len)') == 2
    assert record.count('rdx_record_storage_end();') == 2
    assert 'rdx_uxfile_raw_write(' not in record
    assert 'rdx_record_format_frame(' not in record
    starts = re.findall(r'int rdx_record_run_init\(void\)\s*\{.*?\n\}', record, re.S)
    assert len(starts) == 2
    for start in starts:
        assert start.index('rdx_record_storage_check_start()') < start.index('rdx_uxfile_dat_1_gen(')
    stubs = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef int OS_SEM;
#define NULL ((void*)0)
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define RECORD_FORMATE_OPUS_16K_MONO 1
#define RECORD_FORMATE_OPUS_16K_STERO 2
#define printf(...) ((void)0)
#define OS_ENTER_CRITICAL() (++locked)
#define OS_EXIT_CRITICAL() (--locked)
static int locked, bad, faults, status, meta_fail, fail_write, writes, full_stops, space_error;
static u32 space_bytes;
static int io_users, service_busy;
static u32 meta_calls, saved_len, ticks, stall_frames, seq, expected, frame_len;
static int inside_push, inject, meta_inject;
static int push(void);
static u8 input[80];
void *memcpy(void*, const void*, unsigned int);
void *memset(void*, int, unsigned int);
int os_sem_create(OS_SEM *s, int v) {*s=v; return 0;}
int os_sem_del(OS_SEM *s, int v) {return 0;}
int os_sem_set(OS_SEM *s, unsigned short v) {*s=v; return 0;}
int os_sem_post(OS_SEM *s) {++*s; return 0;}
int os_sem_pend(OS_SEM *, int);
int os_task_create(void (*f)(void*), void *p, int a, int b, int c, const char *n) {return 0;}
void os_task_del(const char *n) {}
u32 sys_timer_get_ms(void) {return ticks;}
void translation_ear_recoder_storage_fault(void) {++faults;}
void translation_ear_recoder_storage_full(void) {++full_stops;}
int rdx_record_format_space(u32 *budget) {
    if (locked || inside_push) bad=1;
    *budget=space_bytes;
    return space_error;
}
int rdx_record_format_status(void) {return status;}
int rdx_record_format_io_enter(void) { if(service_busy) return -1; ++io_users; return 0; }
void rdx_record_format_io_leave(void) { --io_users; }
void rdx_record_format_storage_fault(void) {status=-1;}
void *rdx_uxfile_get_operateFile_info(void) {return (void*)1;}
int rdx_record_format_frame(void *file, u8 format, const u8 *data, u32 len) {
    if (locked || inside_push || len != frame_len) bad=1;
    ++meta_calls;
    if (meta_inject) {
        meta_inject=0;
        for (u32 i=0;i<stall_frames;++i) {ticks+=20; push();}
        /* The first frame must remain owned by the worker during MTA I/O. */
        for (u32 i=0;i<len;++i) if (data[i]!=(i&255)) bad=1;
    }
    if (meta_fail) return -1;
    return 0;
}
int rdx_uxfile_raw_write(u8*, u32, u8);
'''
    tests = r'''
#define CHECK(x) do {if (!(x)) return __LINE__;} while(0)
static void step(void) {
    if (rs.wake) {--rs.wake; rs_service();}
}
int os_sem_pend(OS_SEM *s, int timeout) {
    if (locked || inside_push) bad=1;
    for (int i=0; !*s && i<1000; ++i) step();
    if (!*s) {bad=1; return -1;}
    --*s; return 0;
}
static int push(void) {
    for (u32 i=0;i<frame_len;++i) input[i]=(seq*37+i)&255;
    inside_push=1;
    int ret=rdx_record_storage_push(input,frame_len);
    inside_push=0;
    if (!ret) ++seq;
    /* Producer owns the original frame immediately after return. */
    memset(input,0xff,sizeof(input));
    return ret;
}
int rdx_uxfile_raw_write(u8 *data,u32 len,u8 scene) {
    if (locked || inside_push || !meta_calls || scene!=7 || !len || len>8000) bad=1;
    ++writes;
    if (inject) {
        inject=0;
        for (u32 i=0;i<stall_frames;++i) {ticks+=20; push();}
    }
    if (fail_write==writes) return -1;
    for (u32 i=0;i<len;++i) {
        u32 frame=expected/frame_len, pos=expected%frame_len;
        if (data[i]!=((frame*37+pos)&255)) bad=1;
        ++expected;
    }
    saved_len+=len;
    space_bytes = space_bytes > len ? space_bytes-len : 0;
    return writes%2 ? 0 : 7; /* nonnegative library status, not byte count */
}
static int reset(u8 format) {
    memset(&rs,0,sizeof(rs));
    locked=bad=faults=status=meta_fail=fail_write=writes=0;
    full_stops=space_error=0; space_bytes=100000000;
    io_users=service_busy=0;
    meta_calls=saved_len=ticks=stall_frames=seq=expected=inside_push=inject=meta_inject=0;
    frame_len=format==1 ? 40 : 80;
    if (rdx_record_storage_create()) return -1;
    return rdx_record_storage_begin(42,7,format);
}
int test_storage(void) {
    for (u8 format=1;format<=2;++format) {
        CHECK(!reset(format));
        CHECK(rdx_record_storage_begin(43,7,format)<0);
        /* Two hours plus a non-block-aligned tail, and many ring wraps. */
        for (u32 i=0;i<360013;++i) {CHECK(!push()); step();}
        CHECK(!rdx_record_storage_end());
        CHECK(saved_len==seq*frame_len && rs.accepted==rs.persisted);
        CHECK(!bad && !faults && !rs.active && !rs.count);
        u32 before=saved_len;
        CHECK(!rdx_record_storage_end() && saved_len==before);
        /* Resume same archive with a fresh sink generation. */
        CHECK(!rdx_record_storage_begin(43,7,format));
        CHECK(!push()); CHECK(!rdx_record_storage_end());
        CHECK(saved_len==before+frame_len && !bad);
        rdx_record_storage_destroy(); CHECK(!rs.created);
    }
    /* Block the writer for 220ms and 2s while audio keeps enqueueing. */
    const u32 stalls[]={11,100};
    for (u32 t=0;t<2;++t) {
        CHECK(!reset(1));
        stall_frames=stalls[t]; inject=1;
        for (u32 i=0;i<200;++i) {CHECK(!push()); step();}
        CHECK(!rdx_record_storage_end());
        CHECK(saved_len==seq*frame_len && !bad && !faults);
        CHECK(rs.max_write_ms==stall_frames*20);
    }
    CHECK(!reset(1));
    stall_frames=100; meta_inject=1;
    CHECK(!push()); step();
    CHECK(!rdx_record_storage_end() && !bad && !faults);
    CHECK(saved_len==seq*frame_len && rs.max_meta_ms==2000);
    /* Saturation cannot overwrite the in-flight slot; drain accepted prefix. */
    CHECK(!reset(2));
    for (u32 i=0;i<RS_FRAMES;++i) CHECK(!push());
    CHECK(push()<0 && faults && !writes);
    CHECK(rdx_record_storage_end()<0 && status<0);
    CHECK(saved_len==RS_FRAMES*80 && !bad);
    CHECK(rdx_record_storage_begin(43,7,2)<0);
    /* Overflow while raw_write owns the aggregation block. */
    CHECK(!reset(1)); stall_frames=RS_FRAMES+4; inject=1;
    for (u32 i=0;i<200;++i) {CHECK(!push()); step();}
    CHECK(rdx_record_storage_end()<0 && status<0);
    CHECK(saved_len==seq*frame_len && !bad);
    /* RAW failure must never retry or append later bytes after the gap. */
    CHECK(!reset(1)); fail_write=1;
    for (u32 i=0;i<200;++i) {CHECK(!push()); step();}
    CHECK(rdx_record_storage_end()<0 && status<0 && faults);
    CHECK(writes==1 && !saved_len && !bad);
    CHECK(push()<0 && rdx_record_storage_begin(44,7,1)<0);
    CHECK(!reset(2)); fail_write=1;
    CHECK(!push()); CHECK(rdx_record_storage_end()<0);
    CHECK(writes==1 && !saved_len && status<0 && !bad);
    /* Metadata failure prevents any RAW output, including tail. */
    CHECK(!reset(1)); meta_fail=1;
    CHECK(!push()); CHECK(rdx_record_storage_end()<0);
    CHECK(!writes && !saved_len && status<0 && !bad);
    CHECK(!reset(1));
    CHECK(rdx_record_storage_push(input,79)<0);
    CHECK(rdx_record_storage_end()<0 && !writes && status<0);
    CHECK(!reset(1));
    CHECK(!rdx_record_storage_end() && !writes && !status && !bad);
    /* Low space is a normal stop, not a sticky storage fault. Both formats
     * drain every accepted byte, including the non-block-aligned tail. */
    for (u8 format=1;format<=2;++format) {
        CHECK(!reset(format)); CHECK(!rdx_record_storage_end());
        space_bytes=8000+frame_len*3;
        CHECK(!rdx_record_storage_check_start());
        CHECK(!rdx_record_storage_begin(50,7,format));
        while (!push()) step();
        CHECK(full_stops==1 && !faults && !status);
        CHECK(push()>0 && full_stops==1 && !faults);
        CHECK(!rdx_record_storage_end());
        CHECK(saved_len==seq*frame_len && saved_len==8000+frame_len*3);
        CHECK(!rs.error && !rs.io_failed && !bad);
        CHECK(rdx_record_storage_check_start()==-2);
        CHECK(rdx_record_storage_begin(51,7,format)<0 && !rs.active);
        space_bytes=100000; /* User deleted a file: no reboot/format needed. */
        CHECK(!rdx_record_storage_begin(52,7,format));
        CHECK(!push() && !rdx_record_storage_end() && !bad);
    }
    CHECK(!reset(1)); CHECK(!rdx_record_storage_end());
    space_error=1; CHECK(rdx_record_storage_check_start()<0);
    CHECK(rdx_record_storage_begin(51,7,1)<0);
    CHECK(!io_users); space_error=0; service_busy=1;
    CHECK(rdx_record_storage_check_start()<0);
    CHECK(rdx_record_storage_begin(51,7,1)<0 && !io_users && !rs.active);
    /* Capacity resampling includes queued/in-flight frames, even when a write
     * stalled while the producer filled the FIFO. No I/O occurs on producer. */
    CHECK(!reset(1)); rs.budget=space_bytes=12000; stall_frames=100; inject=1;
    for (u32 i=0;i<200;++i) {CHECK(!push()); step();}
    CHECK(rs.full && full_stops==1 && !faults);
    CHECK(!rdx_record_storage_end() && saved_len==12000 && !bad && !status);
    /* Reaching the space limit on the final FIFO slot is FULL, not overflow. */
    CHECK(!reset(2)); rs.budget=space_bytes=RS_FRAMES*80;
    for(u32 i=0;i<RS_FRAMES;++i) CHECK(!push());
    CHECK(push()>0 && full_stops==1 && !faults);
    CHECK(!rdx_record_storage_end() && saved_len==RS_FRAMES*80 && !status && !bad);
    /* An explicit format can retire I/O latches only after the worker exits. */
    CHECK(!reset(1)); rs.error=-24; rs.io_failed=1;
    rdx_record_storage_reset(); CHECK(rs.error && rs.io_failed);
    CHECK(rdx_record_storage_end()<0);
    rdx_record_storage_reset(); CHECK(!rs.error && !rs.io_failed && !rs.full);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-storage-') as work:
        path = Path(work) / 'record_storage.c'
        path.write_text(stubs + source + tests, encoding='utf-8')
        run_c_checks(path, ['test_storage'])
    print('Recording async storage/lifecycle behavioral checks passed.')


def check_format_reply():
    app = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c').read_text(encoding='utf-8')
    source = r'''
typedef unsigned char u8;
#define NULL ((void*)0)
#define RDX_STORAGE_PLAYBACK_PREEMPT 2
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1
#define PB_PLAYLIST_FORMATTING 4
#define APP_MODE_PC 6
#define r_printf(...) ((void)0)
typedef struct { int epoch; } rdx_ble_async_token_t;
typedef struct { rdx_ble_async_token_t token; } rdx_app_sd_format_request_t;
static int owner=1,busy,format_result,formats,acks,ack,last_preempt,freed,poweroff,pc;
static struct { int goto_poweroff_flag; } app_var;
static void respond(u8 value) { ++acks; ack=value; }
static struct { void (*sd_format_ack_indicate)(u8); } ops={respond}, *g_protocol_ops=&ops;
int rdx_ble_session_rdx_token_resolve(const rdx_ble_async_token_t *t,int ready) { return owner==t->epoch; }
int rdx_app_storage_activity_is_busy(const char *s,int preempt,int detached) { last_preempt=preempt; return busy; }
void rdx_hogp_key_action_reset(void) {}
void rdx_playback_invalidate_playlist(int reason) {}
int rdx_uxfile_sd_format(void *callback) { ++formats; return format_result; }
int rdx_app_get_poweroff_flag(void) { return poweroff; }
int app_in_mode(int mode) { return pc; }
void release_request(void *p) { ++freed; }
#define free release_request
'''
    for name in ('rdx_app_format_handle', 'rdx_app_sd_format_on_app_core', 'rdx_app_format_is_busy'):
        source += function(app, name)
    source += r'''
#define CHECK(x) do {if(!(x)) return __LINE__;} while(0)
int test_format_reply(void) {
    rdx_app_sd_format_request_t request={{1}};
    for(int result=0;result>=-1;--result) {
        format_result=result; formats=acks=freed=0;
        rdx_app_sd_format_on_app_core(&request);
        CHECK(formats==1 && acks==1 && ack==(result!=0) && freed==1);
    }
    busy=1; formats=acks=0; rdx_app_sd_format_on_app_core(&request);
    CHECK(!formats && acks==1 && ack==1);
    busy=0; owner=2; formats=acks=0; rdx_app_sd_format_on_app_core(&request);
    CHECK(!formats && !acks);
    CHECK(!rdx_app_format_is_busy() && last_preempt==0);
    poweroff=1; CHECK(rdx_app_format_is_busy()); poweroff=0;
    app_var.goto_poweroff_flag=1; CHECK(rdx_app_format_is_busy()); app_var.goto_poweroff_flag=0;
    pc=1; CHECK(rdx_app_format_is_busy()); pc=0;
    busy=1; CHECK(rdx_app_format_is_busy());
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-format-reply-') as work:
        path = Path(work) / 'format_reply.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, ['test_format_reply'], native=True)


def check_record_limit():
    base = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    record = (base / 'rdx_record.c').read_text(encoding='utf-8')
    definition = re.search(r'^#define RDX_RECORD_LIMIT_TIME[ \t]+(.+)$', record, re.M)
    assert definition and definition[1].strip() == '0'
    production = ''.join(function(record, name) for name in (
        'rdx_record_max_time_deal', 'rdx_record_max_timer_stop',
        'rdx_record_max_timer_start'))
    stubs = r'''
typedef unsigned short u16;
#define NULL ((void *)0)
static u16 rdx_record_max_timer;
static int adds, deletes, stops, delay;
static void (*callback)(void *);
int sys_timeout_add(void *p, void (*cb)(void *), int ms) {
    ++adds; callback=cb; delay=ms; return 7;
}
void sys_timeout_del(int id) { ++deletes; }
void rdx_record_stop(void) { ++stops; }
'''
    tests = r'''
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
int test_limit(void) {
    rdx_record_max_timer_start();
    rdx_record_max_timer_start();
#if RDX_RECORD_LIMIT_TIME > 0
    CHECK(adds==1 && delay==RDX_RECORD_LIMIT_TIME && rdx_record_max_timer==7);
    callback(NULL);
    CHECK(stops==1 && rdx_record_max_timer==0);
    rdx_record_max_timer_start();
    CHECK(adds==2 && rdx_record_max_timer==7);
#else
    CHECK(adds==0 && stops==0 && rdx_record_max_timer==0);
#endif
    rdx_record_max_timer_stop();
    CHECK(rdx_record_max_timer==0);
    int before=deletes;
    rdx_record_max_timer_stop();
    CHECK(deletes==before);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-record-limit-') as work:
        for limit in (definition[1], '((5*60*60 - 2)*1000)'):
            path = Path(work) / 'record_limit.c'
            path.write_text('#define RDX_RECORD_LIMIT_TIME ' + limit + '\n' +
                            stubs + production + tests, encoding='utf-8')
            run_c_checks(path, ['test_limit'])


# 模式切换的关键回归：重连准入、音频去向与状态报文。
def check_reconnect():
    base = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    record = (base / 'rdx_record.c').read_text(encoding='utf-8')
    app = (base / 'rdx_app.c').read_text(encoding='utf-8')
    source = r'''
static int cdc_file_busy;
static int cdc_download_busy;
int rdx_record_format_download_busy(void) { return cdc_download_busy; }
int rdx_record_format_download_conflict(const char *operation) { return cdc_download_busy; }
int rdx_record_format_service_status(void) { return cdc_download_busy; }
int rdx_factory_audio_file_busy(void) { return cdc_file_busy; }
void rdx_factory_audio_frame(const void *d, unsigned n) {}

typedef unsigned char u8;
#define true 1
#define false 0
#define NULL ((void*)0)
#define r_printf(...) ((void)0)
#define RECORD_STATE_START 0
#define RECORD_STATE_PAUSE 1
#define RECORD_STATE_RESUME 2
#define RECORD_STATE_STOP 3
#define RDX_STORAGE_PLAYBACK_PREEMPT 2
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1
enum { PB_STATE_UNREADY, PB_STATE_STOPPED, PB_STATE_STARTING, PB_STATE_PLAYING,
       PB_STATE_PAUSED, PB_STATE_SWITCHING, PB_STATE_DRAINING };
typedef struct { int state; } pb_public_info_t;
static int playback_state;
void rdx_playback_get_info(pb_public_info_t *p) { p->state=playback_state; }
typedef struct { int run, stream_discont; } RecordStatus;
static RecordStatus record_status;
static int g_record_session_token_valid, g_stream_only_session_active;
static int stream_resume_timer, g_stream_resume_token_valid;
static int record_start_tone_pending, cancelled_timer, pause_timer;
static int rdx_record_local_route_is_fixed(void) { return 0; }
void rdx_record_stream_only_start_cancel(void) {}
void rdx_record_start_tone_cancel(void) { record_start_tone_pending=0; }
void sys_timeout_del(int timer) { cancelled_timer=timer; }
void rdx_record_online_session_clear(void) { g_record_session_token_valid=0; }
void rdx_record_pause_timeout_stop(void) { pause_timer=0; }
void rdx_record_pause_timeout_start(void) { pause_timer=1; }
static int key_record_disconnect_pending;
static int rdx_app_init_flag=1, worker, transfer, sync_busy, loading, scan, format, ota;
typedef struct { int file_send_busy; } ReqFileInfo;
typedef struct { int send_pending, bulk_sending; } BLE_SendData;
typedef struct { int busy, bulk_flag; } BleBulkSendData;
static ReqFileInfo info;
static BLE_SendData send;
static BleBulkSendData bulk;
RecordStatus *rdx_record_get_status(void) { return &record_status; }
int rdx_record_process_is_busy_check(void) { return worker; }
ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) { return &info; }
BLE_SendData *rdx_protocol_get_ble_send_data(void) { return &send; }
BleBulkSendData *rdx_protocol_get_bulk_send_data(void) { return &bulk; }
int rdx_is_file_transfer_active(void) { return transfer; }
int rdx_is_file_sync_busy(void) { return sync_busy; }
u8 rdx_uxfile_is_datFileInfo_loading(void) { return loading; }
u8 rdx_uxfile_is_index_active(void) { return scan; }
u8 rdx_uxfile_is_formatting(void) { return format; }
int rdx_uxfile_sd_format_status_check(void) { return format; }
int get_ota_status(void) { return ota; }
'''
    source += function(record, 'rdx_record_transport_is_detached')
    source += function(record, 'rdx_record_on_ble_conn_changed')
    for name in ['rdx_app_storage_activity_check', 'rdx_app_storage_activity_is_busy', 'rdx_pc_storage_is_busy',
                 'rdx_app_cdc_file_conflict', 'rdx_app_factory_file_is_busy', 'rdx_app_rdx_rebind_is_idle']:
        source += function(app, name)
    source += r'''
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
int test_reconnect(void) {
    for (int state=0; state<3; ++state) {
        record_status.run=state;
        record_status.stream_discont=0;
        g_record_session_token_valid=1;
        stream_resume_timer=42;
        g_stream_resume_token_valid=1;
        record_start_tone_pending=1;
        rdx_record_on_ble_conn_changed(0);
        CHECK(record_status.run==state && record_status.stream_discont);
        CHECK(!g_record_session_token_valid && !g_stream_resume_token_valid);
        CHECK(!stream_resume_timer && cancelled_timer==42);
        CHECK(!record_start_tone_pending);
        CHECK(pause_timer==(state==RECORD_STATE_PAUSE));
        rdx_record_on_ble_conn_changed(1);
        CHECK(!pause_timer);
        /* 已脱离传输的开始、暂停和恢复状态仅放行传输重置，不放行存储接管。 */
        CHECK(rdx_app_rdx_rebind_is_idle());
        CHECK(rdx_pc_storage_is_busy());
        CHECK(rdx_app_storage_activity_is_busy("FORMAT", 2, 0));
        /* 仍绑定会话、处于纯推流模式或传输未中断时，拒绝重新绑定。 */
        g_record_session_token_valid=1;
        CHECK(!rdx_app_rdx_rebind_is_idle());
        g_record_session_token_valid=0;
        g_stream_only_session_active=1;
        CHECK(!rdx_app_rdx_rebind_is_idle());
        g_stream_only_session_active=0;
        record_status.stream_discont=0;
        CHECK(!rdx_app_rdx_rebind_is_idle());
        record_status.stream_discont=1;
        int *busy_flags[]={&cdc_download_busy,&worker,&transfer,&sync_busy,&loading,&scan,&format,&ota,
            &info.file_send_busy,&send.send_pending,&send.bulk_sending,
            &bulk.busy,&bulk.bulk_flag};
        for (int i=0; i<13; ++i) {
            *busy_flags[i]=1;
            CHECK(!rdx_app_rdx_rebind_is_idle());
            *busy_flags[i]=0;
            CHECK(rdx_app_rdx_rebind_is_idle());
        }
    }
    record_status.run=RECORD_STATE_STOP;
    CHECK(rdx_app_rdx_rebind_is_idle());
    CHECK(!rdx_pc_storage_is_busy());
    playback_state=PB_STATE_PAUSED;
    CHECK(rdx_pc_storage_is_busy()); /* MSC admission remains strict. */
    CHECK(!rdx_app_factory_file_is_busy());
    cdc_download_busy=1;
    CHECK(rdx_app_factory_file_is_busy() && !rdx_app_cdc_file_conflict());
    scan=1; CHECK(rdx_app_cdc_file_conflict()); scan=0;
    transfer=1; CHECK(rdx_app_cdc_file_conflict()); transfer=0;
    cdc_download_busy=0;
    int *file_flags[]={&worker,&transfer,&sync_busy,&loading,&scan,&format,&ota,
        &info.file_send_busy,&cdc_file_busy};
    for (int i=0;i<9;++i) {
        *file_flags[i]=1;
        CHECK(rdx_app_factory_file_is_busy());
        *file_flags[i]=0;
    }
    record_status.run=RECORD_STATE_START;
    CHECK(rdx_app_factory_file_is_busy());
    record_status.run=RECORD_STATE_STOP;
    for (int state=0;state<=PB_STATE_DRAINING;++state) {
        playback_state=state;
        CHECK(!!rdx_app_factory_file_is_busy()==
              (state!=PB_STATE_UNREADY && state!=PB_STATE_STOPPED && state!=PB_STATE_PAUSED));
    }
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-reconnect-') as work:
        path = Path(work) / 'reconnect.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, ['test_reconnect'], native=True)


def check_stream():
    record = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c').read_text(encoding='utf-8')
    source = r'''
int rdx_factory_audio_file_busy(void) { return 0; }
void rdx_factory_audio_frame(const void *d, unsigned n) {}

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define false 0
#define RECORD_MODE_OFFLINE 0
#define RECORD_MODE_ONLINE 1
typedef struct { int mode, orig_mode, stream_discont; } RecordStatus;
static RecordStatus status;
static struct { int fixed, route; } record_key_context;
static int record_initialized_generation, record_session_generation=1;
static int current, ready, only, sent, saved, allowed=1;
static u16 conn=0xffff;
RecordStatus *rdx_record_get_status(void) { return &status; }
int rdx_record_online_session_is_current(void) { return current; }
int rdx_record_session_allowed(void) { return allowed; }
u16 rdx_ble_server_get_conn_handle(void) { return conn; }
u8 rdx_record_get_filter_cnt(void) { return 10; }
void rdx_record_set_filter_cnt(u8 c) {}
void rdx_record_set_process_state_ready(void) {}
int rdx_ble_server_is_stream_tx_ready(void) { return ready; }
int rdx_record_stream_only_session_is_active(void) { return only; }
int rdx_protocol_audio_data_indicate(u8 *d, u32 len) { ++sent; return 0; }
int rdx_record_storage_push(u8 *d, u32 len) { ++saved; return 0; }
'''
    source += function(record, 'rdx_record_update_connection_mode')
    source += function(record, 'rdx_record_run_data_handle')
    source += r'''
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
int test_stream(void) {
    u8 data=0;
    rdx_record_update_connection_mode(conn);
    CHECK(status.orig_mode==RECORD_MODE_OFFLINE);
    record_initialized_generation=record_session_generation;
    for (int i=0; i<3; ++i) {
        conn=1; current=ready=1;
        /* 恢复录音的初始化必须保留原始离线模式。 */
        rdx_record_update_connection_mode(conn);
        CHECK(status.mode==RECORD_MODE_ONLINE);
        CHECK(status.orig_mode==RECORD_MODE_OFFLINE);
        status.stream_discont=0;
        CHECK(!rdx_record_run_data_handle(&data,1));
        CHECK(!sent && saved==i*2+1);
        conn=0xffff; current=ready=0;
        rdx_record_update_connection_mode(conn);
        CHECK(!rdx_record_run_data_handle(&data,1));
        CHECK(!sent && saved==i*2+2);
    }
    /* 新开始的在线录音仍支持本地保存和实时推流。 */
    ++record_session_generation;
    conn=1; current=ready=1;
    rdx_record_update_connection_mode(conn);
    CHECK(status.orig_mode==RECORD_MODE_ONLINE);
    CHECK(!rdx_record_run_data_handle(&data,1));
    CHECK(sent==1 && saved==7);
    current=0;
    rdx_record_run_data_handle(&data,1);
    current=1; ready=0;
    rdx_record_run_data_handle(&data,1);
    ready=1; status.stream_discont=1;
    rdx_record_run_data_handle(&data,1);
    CHECK(sent==1 && saved==10);
    status.stream_discont=0; only=1;
    rdx_record_run_data_handle(&data,1);
    CHECK(sent==2 && saved==10);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-record-stream-') as work:
        path = Path(work) / 'stream.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, ['test_stream'], native=True)


def check_snapshot():
    import ctypes
    from llvmlite import binding as llvm

    # Windows 下 MCJIT 不会自动解析 UCRT 的 snprintf 符号，需显式注册。
    crt = ctypes.CDLL('msvcrt')
    for name in ('snprintf', 'memcpy', 'memcmp', 'strcmp'):
        llvm.add_symbol(name, ctypes.cast(getattr(crt, '_snprintf' if name == 'snprintf' else name), ctypes.c_void_p).value)
    base = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    record = (base / 'rdx_record.c').read_text(encoding='utf-8')
    source = r'''
int rdx_factory_audio_file_busy(void) { return 0; }
void rdx_factory_audio_frame(const void *d, unsigned n) {}

typedef unsigned long long size_t;
extern int snprintf(char *, size_t, const char *, ...);
extern void *memcpy(void *, const void *, size_t);
extern int memcmp(const void *, const void *, size_t);
extern int strcmp(const char *, const char *);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef struct { int epoch; } rdx_ble_async_token_t;
typedef struct {
    u8 run, formate, scene, orig_mode, mode;
    u32 begin_time, paused_accumulated_ms, pause_start_ms;
    int stream_discont;
} RecordStatus;
typedef struct { u32 sn; char filename[32]; } uxfile_data_t;
#define RECORD_STATE_START 0
#define RECORD_STATE_PAUSE 1
#define RECORD_STATE_RESUME 2
#define RECORD_STATE_STOP 3
#define RECORD_MODE_OFFLINE 0
#define RECORD_MODE_ONLINE 1
#define RECORD_SCENE_CHAT 0
#define __UUX_FILE__ 1
#define false 0
#define y_printf(...) ((void)0)
#define r_printf(...) ((void)0)
static RecordStatus record_status;
#define status record_status
static uxfile_data_t file={6,"d9e348.raw"};
static int g_app_stop_pending, current=1, sends;
static int g_stream_only_session_active, g_record_session_token_valid;
static u32 record_initialized_generation=1, record_session_generation=1;
static char sent[128];
RecordStatus *rdx_record_get_status(void) { return &status; }
uxfile_data_t *rdx_uxfile_get_operateFile_info(void) { return &file; }
int rdx_record_online_session_token_is_current(const rdx_ble_async_token_t *t) {
    return t && t->epoch==current;
}
u32 rdx_record_get_active_offset_ms(void) { return 102705; }
u8 rdx_app_get_record_mode(void) { return 0; }
int rdx_protocol_packet_send_priority(void *p, u16 n) {
    memcpy(sent,p,n); sent[n]=0; ++sends; return 0;
}
void rdx_protocol_record_state_indicate(void) { ++sends; }
void rdx_uxfile_operate_file_init(void) { file=(uxfile_data_t){0}; }
void rdx_record_online_session_clear(void) { g_record_session_token_valid=0; }
'''
    source += function(record, 'rdx_record_state_is_publishable')
    source += function(record, 'rdx_record_state_indicate_if_current')
    source += function(record, 'rdx_record_state_snapshot_indicate')
    source += function(record, 'rdx_record_stop_session_cleanup')
    source += r'''
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
int test_snapshot(void) {
    status.formate=1;
    for (int cycle=1; cycle<=3; ++cycle) {
        current=cycle;
        rdx_ble_async_token_t token={cycle};
        for (int state=0; state<3; ++state) {
            status.run=state;
            RecordStatus before=status;
            rdx_record_state_snapshot_indicate(&token);
            CHECK(!memcmp(&before,&status,sizeof(status)));
            CHECK(!strcmp(sent,state==1 ?
                "*DEV#record#2#0#1#0#0#102705#6#d9e348.raw#" :
                "*DEV#record#1#0#1#0#0#102705#6#d9e348.raw#"));
        }
        int count=sends;
        token.epoch=cycle-1;
        rdx_record_state_snapshot_indicate(&token);
        CHECK(sends==count);
    }
    rdx_ble_async_token_t token={current};
    status.run=3;
    int count=sends;
    rdx_record_state_snapshot_indicate(&token);
    CHECK(sends==count);
    status.run=2; g_app_stop_pending=1;
    rdx_record_state_snapshot_indicate(&token);
    CHECK(sends==count);
    g_app_stop_pending=0; status.orig_mode=1;
    rdx_record_state_snapshot_indicate(&token);
    CHECK(!strcmp(sent,"*DEV#record#1#0#1#0#1#0#6#d9e348.raw#"));
    count=sends;
    ++record_session_generation;
    for (int state=0;state<3;++state) {
        status.run=state;
        rdx_record_state_snapshot_indicate(&token);
        rdx_record_state_indicate_if_current(&token);
        CHECK(sends==count);
    }
    record_initialized_generation=record_session_generation;
    rdx_record_state_snapshot_indicate(&token);
    CHECK(sends==count+1);
    rdx_record_state_indicate_if_current(&token);
    CHECK(sends==count+2);
    token.epoch=current-1;
    rdx_record_state_indicate_if_current(&token);
    CHECK(sends==count+2);
    return 0;
}
int test_stop_cleanup(void) {
    status.run=RECORD_STATE_STOP;
    status.mode=RECORD_MODE_ONLINE; status.orig_mode=RECORD_MODE_OFFLINE;
    status.begin_time=1000; status.paused_accumulated_ms=200; status.pause_start_ms=2500;
    status.stream_discont=1;
    file=(uxfile_data_t){6,"d9e348.raw"};
    record_initialized_generation=record_session_generation=5;
    g_stream_only_session_active=g_record_session_token_valid=1;
    g_app_stop_pending=0;
    int count=sends;
    for(int repeat=0;repeat<2;++repeat) {
        rdx_record_stop_session_cleanup();
        CHECK(!status.begin_time && !status.paused_accumulated_ms && !status.pause_start_ms);
        CHECK(status.orig_mode==RECORD_MODE_ONLINE && !status.stream_discont);
        CHECK(!file.sn && !file.filename[0] && !record_initialized_generation);
        CHECK(!g_stream_only_session_active && !g_record_session_token_valid);
        CHECK(status.run==RECORD_STATE_STOP && record_session_generation==5 && sends==count);
        CHECK(rdx_record_state_is_publishable());
    }
    status.run=RECORD_STATE_START; ++record_session_generation;
    CHECK(!rdx_record_state_is_publishable());
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='t2620-snapshot-') as work:
        path = Path(work) / 'snapshot.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, ['test_snapshot', 'test_stop_cleanup'], native=True)


if __name__ == '__main__':
    main()
