"""Execute the patched download parser/sender/cleanup plus production C policy.

Only OS, BLE and filesystem boundaries are mocked. Durable filesystem replay is
covered separately by test_record_format; this exercises the actual auto path.
"""
import ctypes
import os
import re
import subprocess

from llvmlite import binding as llvm
from host_c_test_lib import ROOT, function, _reject_unresolved_calls
from test_protocol_recv import extract

DIRECTORY = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'

STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long size_t;
typedef _Bool bool;
#define NULL ((void*)0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define Q_CALLBACK 1
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1
#define RDX_RECORD_DELETE_REJECTED (-2)
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
void *malloc(size_t); void free(void *);
void *memset(void *,int,size_t); void *memcpy(void *,const void *,size_t);
void *memchr(const void *,int,size_t);
int memcmp(const void *,const void *,size_t);
int strcmp(const char *,const char *); char *strcpy(char *,const char *);
size_t strlen(const char *); int sprintf(char *,const char *,...);
typedef struct { u8 slot_index; u32 slot_generation, transport_epoch; } rdx_ble_async_token_t;
static int invalid, allocations, alloc_size_fail, alloc_fail_once, owner=1;
static void *allocated[128];
void *test_malloc(u32 n) {
    if (alloc_fail_once || (alloc_size_fail && n==(u32)alloc_size_fail)) {
        alloc_fail_once=0; return NULL;
    }
    void *p=malloc(n?n:1);
    for(int i=0;i<128;++i) if(!allocated[i]) { allocated[i]=p; ++allocations; return p; }
    ++invalid; return NULL;
}
void test_free(void *p) {
    if(!p) return;
    for(int i=0;i<128;++i) if(allocated[i]==p) {
        allocated[i]=NULL; --allocations; free(p); return;
    }
    ++invalid;
}
#define malloc test_malloc
#define free test_free
#define printf(...) ((void)0)
static int irq, rf_service, rf_error, rf_io_users, power, power_fail;
static int timer, clock_hold, post_fail, complete_post_fail=1, delete_calls;
static int short_read, read_fail, enqueue_fail, ota, record_run=1;
static int flush_result, delete_result, reload_result, drop_during_scan;
static int committed, drop_after_commit, raw_exists=1, mta_exists=1, dat_count=1;
static int cache_invalidations, list_invalidations, read_open, sends;
static int playback_notifications, stop_indications;
static int preempt_posts, preempt_post_fail;
static int sleep_refresh;
static u32 preempt_generation;
static u32 loaded_sn=13, loaded_size=4000, last_offset, last_length;
static char loaded_name[64]="f5062.raw";
static void *pending, *dispatched, *completion;
void rdx_playback_on_file_deleted(u32 sn) {
    if(sn!=loaded_sn) ++invalid;
    ++playback_notifications;
}
void local_irq_disable(void) { ++irq; }
void local_irq_enable(void) { if(--irq<0) ++invalid; }
int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t,u8 active) {
    memset(t,0,sizeof(*t)); t->transport_epoch=owner; return owner!=0;
}
void *rdx_ble_session_rdx_token_resolve(const rdx_ble_async_token_t *t,u8 active) {
    return owner && t->transport_epoch==(u32)owner ? (void*)1:NULL;
}
int rdx_record_format_service_status(void) { return rf_service; }
void rdx_record_format_service_ready(int result) { rf_service=result<0?-1:result>0?1:0; }
int rdx_adv_policy_business_enter(void) { if(power_fail) return -1; ++power; return 0; }
void rdx_adv_policy_business_exit(void) { --power; }
void rdx_peripheral_power_vdd_idle_hint(void) {}
void rdx_record_format_io_leave(void);
int rdx_uxfile_auto_delete_submit(void *p) {
    if(!power || !rf_io_users || rf_service!=1 || timer || clock_hold || read_open) ++invalid;
    if(post_fail) return -1;
    if(pending) ++invalid;
    pending=p; return 0;
}
int os_taskq_post_type(const char *task,int type,int n,int *args) {
    /* Recording admission hands the stop to the busy timer's task. The 32-bit
     * callback pointer is not host-sized; tests run the captured generation. */
    if(!strcmp(task,"protocol_send_task")) {
        if(type!=Q_CALLBACK || n!=3 || !args[0] || args[1]!=1 || !args[2]) ++invalid;
        ++preempt_posts; preempt_generation=(u32)args[2];
        return preempt_post_fail;
    }
    /* The patched duplicate branch must not queue a borrowed ReqFileInfo. */
    if(!dispatched || strcmp(task,"app_core") || type!=Q_CALLBACK ||
       n!=3 || !args[0] || args[1]!=1 || args[2]!=(int)dispatched) ++invalid;
    if(complete_post_fail) return 1;
    if(completion) ++invalid;
    completion=dispatched;
    return 0;
}
int os_mutex_pend(void *p,int timeout) { return 0; }
int os_mutex_post(void *p) { return 0; }
int os_sem_post(void *p) { return 0; }
int clock_alloc(char *name,int hz) { clock_hold=1; return 0; }
int clock_free(char *name) { clock_hold=0; return 0; }
void clock_refurbish(void) {}
int jiffies_msec(void) { return 0; }
void log_print(int level,void *a,void *b,...) {}
u16 sys_timeout_add_2_task(void *context,void (*callback)(void*),u32 delay,const char *task) {
    timer=1; return 1;
}
void sys_timeout_del(u16 id) { timer=0; }
void sys_timer_re_run(u16 id) { timer=1; }
void rdx_protocol_fileTransfer_stop_indicate(void) { ++stop_indications; }
/* Preemption stop and deferred direct sends go straight to ATT through the
 * production gate; tx_log records their order ('S' = stop). */
static int stop_sends, direct_sends, att_fail, kicks;
static char tx_log[64];
int rdx_file_download_tx_gate(const u8 *data,u32 len,const rdx_ble_async_token_t *token);
int rdx_ble_server_send_for_token(u8 *data,u32 len,const rdx_ble_async_token_t *t) {
    if(!t || irq || len<2 || strlen(tx_log)>=sizeof(tx_log)-1) ++invalid;
    if(!owner || t->transport_epoch!=(u32)owner) return -1;
    if(rdx_file_download_tx_gate(data,len,t)) ++invalid;
    if(att_fail) return -1;
    u32 n=strlen(tx_log);
    if(len==15 && !memcmp(data,"*DEV#filecmd#0#",15)) { ++stop_sends; tx_log[n]='S'; }
    else { ++direct_sends; tx_log[n]=data[len-2]; }
    tx_log[n+1]=0;
    return 0;
}
/* The frame in flight (archive g_send_ctx) and the cleanup's tick wait. */
void test_frame_set(void *buffer,u32 sent,u32 total);
void test_frame_finish(void);
static int ticks, frame_end_tick;
void os_time_dly(int n) {
    if(n!=1 || irq) ++invalid;
    if(++ticks==frame_end_tick) test_frame_finish();
}
/* Library send queue behind the archive's rdx_queue_* calls. */
static void *queue_data[16];
static u32 queue_len[16], queue_count;
u32 test_queue_size(void) { return queue_count; }
int test_queue_pop(void *packet) {
    if(!queue_count) return 0;
    memcpy(packet,&queue_data[0],sizeof(void*));
    memcpy((u8*)packet+sizeof(void*),&queue_len[0],4);
    --queue_count;
    memmove(queue_data,queue_data+1,queue_count*sizeof(void*));
    memmove(queue_len,queue_len+1,queue_count*4);
    return 1;
}
int test_queue_push(void *packet) {
    void *data; u32 len;
    memcpy(&data,packet,sizeof(void*));
    memcpy(&len,(u8*)packet+sizeof(void*),4);
    if(queue_count>=16) return 0;
    void *copy=test_malloc(len);
    if(!copy) return 0;
    memcpy(copy,data,len);
    queue_data[queue_count]=copy; queue_len[queue_count++]=len;
    return 1;
}
void rdx_ble_server_rdx_send_kick(void) { if(irq) ++invalid; ++kicks; }
void rdx_record_mode_active_check(bool active) {}
void rdx_app_wifi_handle(u8 active) {}
void rdx_app_emmc_poweroff_check(void) {}
int xxp_wifi_tcp_file_stop_indicate(void) { return 0; }
void rdx_ble_server_request_stream_fast_param(void) {}
void rdx_ble_server_post_transfer_lowpower(void) {}
void rdx_ble_server_file_transfer_changed(void) { ++sleep_refresh; }
int get_ota_status(void) { return ota; }
void *rdx_record_get_status(void) { static u8 s[128]; s[0]=record_run; return s; }
void *rdx_app_get_wifi_info(void) { static u8 s[2]; return s; }
void *rdx_ble_server_get_info(void) { static u8 s[128]; s[6]=1; return s; }
'''

BOUNDARIES = r'''
static uxfile_data_t file;
uxfile_data_t *rdx_uxfile_get_file_data_by_sn(u32 sn,u8 type,int offset) {
    if(sn!=loaded_sn || !raw_exists) { memset(&file,0,sizeof(file)); return &file; }
    file.sn=sn; strcpy(file.filename,loaded_name); file.data_len=loaded_size;
    file.divpack=(loaded_size-offset+24479)/24480;
    file.remain_pack_len=(loaded_size-offset)%24480;
    return &file;
}
int rdx_uxfile_raw_read(u32 n,u32 offset,u8 *p) {
    last_offset=offset; last_length=n; read_open=1;
    if(read_fail) return -1;
    memset(p,0,n);
    return short_read ? n-1 : n;
}
void rdx_uxfile_close_read_file_handle(void) { read_open=0; }
void rdx_uxfile_recordFileData_sendBuf_free(void) {
    test_free(file.data); file.data=NULL; file.left_len=0;
    rdx_uxfile_close_read_file_handle();
}
int rdx_protocol_send_bulk_data(void *p,u32 len,bool owned) {
    if(enqueue_fail) return 0;
    ++sends; test_free(p); return 1;
}
int rdx_util_crc32(void *p,u32 n,u32 *crc) { return 0; }
void rdx_queue_clearQueue(void *queue) {}
int rdx_uxfile_flush_cache(void) { return flush_result; }
int rdx_record_format_delete_guarded(u32 sn,const char *name,
                                    int (*authorize)(void*),void *context) {
    ++delete_calls;
    if(sn!=loaded_sn || strcmp(name,loaded_name)) { ++invalid; return -2; }
    if(!authorize(context)) return -2;
    if(drop_during_scan) owner=0;
    if(!authorize(context)) return -2;
    committed=1;
    if(drop_after_commit) owner=0;
    if(delete_result) return delete_result;
    raw_exists=mta_exists=dat_count=0; return 0;
}
void rdx_uxfile_invalidate_dat_cache(void) { ++cache_invalidations; }
void rdx_uxfile_datFileInfo_sendBuf_free(void) { ++list_invalidations; }
void test_cache_loaded(void);
int rdx_uxfile_dat_init_impl(void) { test_cache_loaded(); return reload_result; }
int rdx_protocol_handle_recordFileData_req(u8 *,u16);
void rdx_protocol_download_cleanup(void);
void rdx_file_download_abort(void);
void rdx_file_download_delete_worker(void *);
void rdx_uxfile_recordFileData_send_finish(ReqFileInfo *);
void rdx_protocol_file_sync_busy_timer_cb(void *);
void rdx_protocol_file_cmd_handle(u8);
void rdx_protocol_download_preempt(u32);
'''

TESTS = r'''
static int cmd(u32 sn,int pack,int autodel,u32 offset,int ack) {
    char text[128]; sprintf(text,"*APP#opusfile#%d#%u#%d#%d#%u#",ack,sn,pack,autodel,offset);
    return rdx_protocol_handle_recordFileData_req((u8*)text,strlen(text));
}
static void worker(void) {
    dispatched=pending; pending=NULL;
    if(dispatched) rdx_file_download_delete_worker(dispatched);
    dispatched=NULL;
}
static void complete(void) {
    /* The target callback queue has a 32-bit pointer ABI. Keep the original
     * pointer for host delivery after checking its encoded queue argument. */
    void *p=completion; completion=NULL;
    if(p) download_delete_complete(p);
}
static void reset(void) {
    rdx_protocol_download_cleanup(); worker(); complete();
    if(allocations || irq || power || rf_io_users) ++invalid;
    memset(&file,0,sizeof(file));
    owner=1; loaded_sn=13; loaded_size=4000; strcpy(loaded_name,"f5062.raw");
    rf_service=rf_error=rf_io_users=power=power_fail=timer=clock_hold=post_fail=0;
    short_read=read_fail=enqueue_fail=ota=0; record_run=1;
    flush_result=delete_result=reload_result=drop_during_scan=drop_after_commit=0;
    committed=delete_calls=cache_invalidations=list_invalidations=sends=0;
    complete_post_fail=1; playback_notifications=stop_indications=0;
    preempt_posts=preempt_post_fail=0; preempt_generation=0;
    stop_sends=direct_sends=att_fail=kicks=0; memset(&fence,0,sizeof(fence));
    tx_log[0]=0; ticks=frame_end_tick=0;
    while(queue_count) test_free(queue_data[--queue_count]);
    sleep_refresh=0;
    raw_exists=mta_exists=dat_count=1;
    alloc_size_fail=alloc_fail_once=0;
}
int test_auto_success_and_duplicates(void) {
    reset();
    CHECK(!cmd(13,1,1,0,0) && sends==1);
    CHECK(!cmd(13,1,1,0,2) && sends==2 && last_length==4000);
    CHECK(!cmd(13,0,1,0,0) && pending && power==1 && rf_service==1);
    CHECK(!timer && !clock_hold && !read_open && !download.active);
    CHECK(sleep_refresh>=3);
    CHECK(!cmd(13,0,1,0,2) && !delete_calls);
    CHECK(cmd(14,1,1,0,0)==3);
    worker();
    CHECK(delete_calls==1 && committed && !raw_exists && !mta_exists && !dat_count);
    CHECK(cache_invalidations==1 && list_invalidations==1);
    CHECK(!rf_service && !power && !rf_io_users && !allocations && !invalid);
    CHECK(!cmd(13,0,1,0,2) && delete_calls==1);
    return 0;
}
int test_auto_full_sn_and_stale_finish(void) {
    reset(); loaded_sn=302993506;
    CHECK(!cmd(loaded_sn,1,1,0,0));
    CHECK(cmd(13,0,1,0,0)==3 && download.active && timer && clock_hold);
    CHECK(!cmd(loaded_sn,0,1,0,0)); worker();
    CHECK(delete_calls==1 && !raw_exists && !invalid);
    reset(); CHECK(!cmd(13,1,1,0,0)); rdx_protocol_download_cleanup();
    loaded_sn=269; strcpy(loaded_name,"abc123.raw");
    CHECK(!cmd(269,1,1,0,0));
    CHECK(cmd(13,0,1,0,2)==3 && download.active);
    CHECK(!cmd(269,0,1,0,0)); worker();
    CHECK(delete_calls==1 && !raw_exists && !invalid);
    return 0;
}
int test_auto_completion_callback_and_optional_offset(void) {
    for(int stale=0;stale<2;++stale) {
        reset(); complete_post_fail=0;
        char start[]="*APP#opusfile#0#13#1#1#";
        char finish[]="*APP#opusfile#0#13#0#1#";
        CHECK(!rdx_protocol_handle_recordFileData_req((u8*)start,strlen(start)));
        CHECK(!rdx_protocol_handle_recordFileData_req((u8*)finish,sizeof(finish)));
        worker();
        CHECK(completion && allocations==1 && !rf_service && !power && !rf_io_users);
        CHECK(!raw_exists && !mta_exists && !playback_notifications);
        if(stale) owner=2;
        complete();
        CHECK(playback_notifications==!stale && !allocations && !invalid);
    }
    return 0;
}
int test_auto_multiblock_resume_and_gaps(void) {
    reset(); loaded_size=60000;
    CHECK(!cmd(13,1,1,0,0) && last_offset==0 && last_length==24480);
    CHECK(!cmd(13,2,1,0,0) && last_offset==24480 && last_length==24480);
    CHECK(!cmd(13,3,1,0,0) && last_offset==48960 && last_length==11040);
    CHECK(!cmd(13,0,1,0,0)); worker(); CHECK(!raw_exists);
    reset(); loaded_size=60000;
    CHECK(!cmd(13,1,1,123,0) && last_offset==123 && last_length==24480);
    CHECK(!cmd(13,2,1,123,0) && last_offset==24603);
    CHECK(!cmd(13,3,1,123,0) && last_offset==49083 && last_length==10917);
    CHECK(!cmd(13,0,1,0,0)); worker(); CHECK(!raw_exists);
    reset(); loaded_size=60000;
    CHECK(!cmd(13,1,1,0,0) && !cmd(13,3,1,0,0));
    CHECK(!cmd(13,0,1,0,0) && !pending && raw_exists);
    CHECK(!invalid && !allocations && !timer && !clock_hold);
    return 0;
}
int test_auto_failures_never_delete(void) {
    for(int failure=0;failure<8;++failure) {
        reset();
        if(failure==0) short_read=1;
        if(failure==1) read_fail=1;
        if(failure==2) enqueue_fail=1;
        if(failure==3) alloc_size_fail=4000;
        if(failure==4) alloc_size_fail=4064;
        if(failure==5) ota=1;
        if(failure==6) record_run=0;
        if(failure==7) raw_exists=0;
        cmd(13,1,1,0,0);
        CHECK(!timer && !clock_hold && !read_open && !download.active);
        /* Only a device-side conflict is an interrupt the APP must be told
         * about; transport/file failures stay silent and are retried. */
        CHECK(stop_indications==(failure==5 || failure==6));
        CHECK(!cmd(13,0,1,0,0) && !pending && !delete_calls);
        CHECK(!invalid && !allocations);
    }
    reset(); loaded_size=60000;
    CHECK(!cmd(13,1,1,0,0) && !cmd(13,0,1,0,0) && raw_exists && !pending);
    reset(); CHECK(!cmd(13,1,0,0,0) && !cmd(13,0,1,0,0) && !pending);
    reset(); CHECK(!cmd(13,1,1,0,0) && !cmd(13,0,0,0,0) && !pending);
    return 0;
}
int test_auto_handoff_and_owner_loss(void) {
    for(int failure=0;failure<6;++failure) {
        reset(); CHECK(!cmd(13,1,1,0,0));
        if(failure==0) post_fail=1;
        if(failure==1) power_fail=1;
        if(failure==2) alloc_fail_once=1;
        CHECK(!cmd(13,0,1,0,0));
        if(failure==3) owner=0;
        if(failure==4) drop_during_scan=1;
        if(failure==5) drop_after_commit=1;
        worker();
        CHECK(raw_exists==(failure!=5) && !power && !rf_io_users && !rf_service);
        CHECK(!timer && !clock_hold && !allocations && !invalid);
    }
    reset(); CHECK(!cmd(13,1,1,0,0)); owner=2;
    CHECK(cmd(13,0,1,0,0)==3 && !pending && raw_exists);
    rdx_protocol_download_cleanup();
    reset(); CHECK(!cmd(13,1,1,0,0) && !cmd(13,0,1,0,0));
    delete_result=-1; worker();
    CHECK(rf_service==-1 && !power && !rf_io_users && !allocations && raw_exists);
    return 0;
}
int test_auto_generic_finish_and_old_generation(void) {
    reset(); CHECK(!cmd(13,1,1,0,0));
    u32 old=rdx_file_download_generation();
    ReqFileInfo request={0}; request.file_num=13; request.auto_del=1;
    rdx_uxfile_recordFileData_send_finish(&request);
    CHECK(!download.active && !pending && raw_exists && !delete_calls);
    rdx_protocol_download_cleanup();
    CHECK(!cmd(13,1,1,0,0));
    u32 current=download.generation; download.next=0; download.identified=0;
    rdx_file_download_sent(old,&request,&file,0,4000,4000,1);
    CHECK(download.generation==current && !download.next && !download.identified);
    CHECK(!cmd(13,0,1,0,0) && !pending && raw_exists && !invalid);
    return 0;
}
int test_auto_timeout_and_stop(void) {
    for(int cancel=0;cancel<2;++cancel) {
        reset(); CHECK(!cmd(13,1,1,0,0) && timer && clock_hold && read_open);
        if(cancel) rdx_protocol_file_cmd_handle(0);
        else rdx_protocol_file_sync_busy_timer_cb(NULL);
        CHECK(stop_indications==1);
        CHECK(!download.active && !timer && !clock_hold && !read_open);
        CHECK(!cmd(13,0,1,0,0) && !pending && raw_exists && !delete_calls);
        CHECK(!invalid && !allocations);
        CHECK(!cmd(13,1,1,0,0) && !cmd(13,0,1,0,0));
        worker(); CHECK(delete_calls==1 && !raw_exists);
    }
    return 0;
}
int test_auto_record_interrupt_notifies_stop(void) {
    reset(); loaded_size=60000;
    CHECK(!cmd(13,1,1,0,0) && sends==1 && last_offset==0 && !stop_indications);
    /* A recording run rejects the next chunk. The APP must learn that the
     * transfer stopped instead of retrying until it reports a failure. */
    record_run=0;
    CHECK(cmd(13,2,1,0,0)==3 && stop_indications==1);
    CHECK(!download.active && !timer && !clock_hold && !read_open && !allocations);
    /* Later chunks are refused before the parser: no duplicate indication and
     * no borrowed state may be resurrected. */
    CHECK(cmd(13,3,1,0,0)==3 && stop_indications==1 && !download.active);
    CHECK(rdx_protocol_handle_recordFileData_req((u8*)"*APP#opusfile#0#13#2#1#junk#",
                                                 strlen("*APP#opusfile#0#13#2#1#junk#"))==3);
    CHECK(stop_indications==1);
    /* APP resumes from the last complete block after the recording ends. */
    record_run=1;
    CHECK(!cmd(13,1,1,24480,0) && sends==2 && last_offset==24480 && last_length==24480);
    CHECK(download.offset==24480);
    CHECK(download.failed==0);
    CHECK(download.identified==1);
    CHECK(download.next==48960);
    CHECK(download.size==60000);
    CHECK(cmd(13,0,1,0,0)==0);
    CHECK(!pending);
    CHECK(raw_exists);
    CHECK(!delete_calls);
    CHECK(!invalid && !allocations && stop_indications==1);
    /* OTA uses the same contract and must not be mistaken for a file failure. */
    ota=1;
    CHECK(cmd(13,1,1,0,0)==3 && stop_indications==2);
    CHECK(!download.active && !timer && !clock_hold && !read_open && !allocations);
    ota=0;
    CHECK(!invalid && !timer && !clock_hold);
    return 0;
}
static int gate(const char *text,u32 len) {
    return rdx_file_download_tx_gate((const u8*)text,len,NULL);
}
/* A direct (token-bound) send such as the F19 flashnote. */
static int direct(const char *text) {
    rdx_ble_async_token_t token=download.token;
    return rdx_file_download_tx_gate((const u8*)text,strlen(text),&token);
}
#define FRAME "*DEV#filect#13#1#0#24480#"
#define BINARY "\x01\x02\x03\x04"
#define STATUS "*DEV#flashnote#1#"
int test_auto_record_admission_preempts_download(void) {
    /* Idle admission queues nothing and says nothing. */
    reset(); CHECK(!rdx_file_download_preempt("F19") && !preempt_posts && !stop_sends);
    CHECK(!gate(FRAME,25) && !gate(STATUS,17));
    loaded_size=60000;
    CHECK(!cmd(13,1,1,0,0) && sends==1 && timer && clock_hold && read_open);
    u32 generation=download.generation;
    /* F18/F19 admission answers the APP at once, straight to ATT, before the
     * recording starts; the cleanup itself is queued. */
    CHECK(!rdx_file_download_preempt("F19") && preempt_posts==1 &&
          preempt_generation==generation && stop_sends==1 && !stop_indications &&
          download.active && fence.state==FENCE_SENT);
    /* Until the cleanup ran, frames already queued never follow the stop;
     * control packets (recording status) pass. */
    CHECK(gate(FRAME,25) && gate(BINARY,4) && !gate(STATUS,17));
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(stop_sends==1 && !stop_indications && !download.active && fence.state==FENCE_IDLE);
    CHECK(!timer && !clock_hold && !read_open && !allocations && kicks);
    CHECK(!gate(FRAME,25) && !gate(STATUS,17));
    /* The recording now owns the device: the APP's in-flight chunk request is
     * refused silently (already notified), a fresh request is told again. */
    record_run=0;
    CHECK(cmd(13,2,1,0,0)==3 && !stop_indications && sends==1);
    CHECK(cmd(13,1,1,24480,0)==3 && stop_indications==1 && !download.active);
    /* A duplicate or stale preemption never stops a newer transfer. */
    record_run=1;
    rdx_protocol_download_preempt(generation);
    CHECK(stop_indications==1 && stop_sends==1 && fence.state==FENCE_IDLE);
    CHECK(!cmd(13,1,1,24480,0) && sends==2 && last_offset==24480);
    rdx_protocol_download_preempt(generation);
    CHECK(stop_indications==1 && stop_sends==1 && download.active && timer && clock_hold);
    CHECK(!gate(FRAME,25));
    CHECK(cmd(13,0,1,0,0)==0 && !pending && raw_exists && !delete_calls);
    /* A transfer that finished before the queued cleanup ran is left alone
     * and its fence opens again. */
    CHECK(!cmd(13,1,0,0,0) && download.active);
    CHECK(!rdx_file_download_preempt("F18") && preempt_posts==2 && stop_sends==2);
    CHECK(cmd(13,0,0,0,0)==0 && !download.active);
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(stop_indications==1 && fence.state==FENCE_IDLE && !gate(FRAME,25));
    /* If the cleanup cannot be queued, nothing is fenced; the parser conflict
     * branch still stops and notifies on the APP's next chunk request. */
    CHECK(!cmd(13,1,0,0,0) && download.active);
    preempt_post_fail=1;
    CHECK(rdx_file_download_preempt("F19") && download.active &&
          stop_sends==2 && fence.state==FENCE_IDLE && !gate(FRAME,25));
    record_run=0;
    CHECK(cmd(13,2,0,0,0)==3 && stop_indications==2 && !download.active);
    CHECK(!timer && !clock_hold && !read_open && !allocations && !invalid);
    return 0;
}
int test_auto_record_preempt_stop_ordering(void) {
    /* ATT full at admission: the stop is owed. Nothing, not even the
     * recording status, overtakes it; CAN_SEND_NOW flushes it first. */
    reset(); loaded_size=60000;
    CHECK(!cmd(13,1,1,0,0));
    att_fail=1;
    CHECK(!rdx_file_download_preempt("F19") && !stop_sends && fence.state==FENCE_ARMED);
    CHECK(gate(STATUS,17) && gate(FRAME,25) && !stop_sends);
    att_fail=0; kicks=0;
    rdx_file_download_tx_ready();
    CHECK(stop_sends==1 && fence.state==FENCE_SENT && kicks==1);
    CHECK(!gate(STATUS,17) && gate(BINARY,4));
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(fence.state==FENCE_IDLE && !download.active && stop_sends==1);
    /* Still full after the cleanup: owed until the next packet or event,
     * which sends the stop and only then itself. */
    CHECK(!cmd(13,1,1,0,0));
    att_fail=1;
    CHECK(!rdx_file_download_preempt("F18") && stop_sends==1);
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(fence.state==FENCE_PENDING && !download.active && gate(STATUS,17));
    att_fail=0;
    CHECK(!gate(STATUS,17) && stop_sends==2 && fence.state==FENCE_IDLE);
    CHECK(!gate(FRAME,25));
    /* A disconnected or replaced owner gets no stop and blocks nothing. */
    CHECK(!cmd(13,1,1,0,0));
    att_fail=1;
    CHECK(!rdx_file_download_preempt("F19") && fence.state==FENCE_ARMED);
    owner=2;
    CHECK(!gate(STATUS,17) && fence.state==FENCE_IDLE && stop_sends==2);
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(fence.state==FENCE_IDLE && !download.active);
    /* A new transfer supersedes a stop still owed for the old one. */
    owner=1; CHECK(!cmd(13,1,1,0,0));
    CHECK(!rdx_file_download_preempt("F19") && fence.state==FENCE_ARMED);
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(fence.state==FENCE_PENDING);
    CHECK(!cmd(13,1,1,0,0) && fence.state==FENCE_IDLE && !gate(FRAME,25));
    att_fail=0;
    CHECK(stop_sends==2 && !stop_indications && !invalid && !irq);
    return 0;
}
int test_auto_record_preempt_frame_boundary(void) {
    /* A frame is one length-prefixed APP message: the one in flight ends
     * first, then the stop, then the deferred flashnote and statuses. */
    reset(); loaded_size=60000;
    CHECK(!cmd(13,1,1,0,0));
    test_frame_set(test_malloc(64),10,64);
    CHECK(rdx_protocol_bulk_frame_busy());
    CHECK(!rdx_file_download_preempt("F19") && !stop_sends && fence.state==FENCE_ARMED);
    CHECK(!gate(BINARY,4) && gate(STATUS,17) && gate(FRAME,25) && !stop_sends);
    CHECK(direct("*DEV#flashnote#1#")==1 && direct("*DEV#record#2#")==1 &&
          fence.count==2 && rdx_file_download_tx_held() && !direct_sends);
    test_frame_finish();
    kicks=0;
    rdx_file_download_tx_ready();
    CHECK(!strcmp(tx_log,"S12") && fence.state==FENCE_SENT && !fence.count && kicks==1);
    CHECK(gate(BINARY,4) && gate(FRAME,25) && !gate(STATUS,17) && !rdx_file_download_tx_held());
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(fence.state==FENCE_IDLE && !download.active && !allocations);
    /* ATT full while draining: the rest keeps its order behind the head. */
    CHECK(!cmd(13,1,1,0,0));
    att_fail=1;
    CHECK(!rdx_file_download_preempt("F18") && fence.state==FENCE_ARMED);
    CHECK(direct("*DEV#flashnote#3#")==1 && direct("*DEV#record#4#")==1);
    att_fail=0;
    CHECK(!gate(STATUS,17) && !strcmp(tx_log,"S12S34") && fence.state==FENCE_SENT);
    /* A not yet started frame never holds the stop back. */
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(!cmd(13,1,1,0,0));
    test_frame_set(test_malloc(64),0,64);
    CHECK(!rdx_protocol_bulk_frame_busy() && !rdx_file_download_preempt("F19") &&
          fence.state==FENCE_SENT && stop_sends==3);
    rdx_protocol_download_preempt(preempt_generation);
    /* The owner left: deferred sends are dropped, nothing stays held. */
    CHECK(!cmd(13,1,1,0,0));
    att_fail=1;
    CHECK(!rdx_file_download_preempt("F19") && direct("*DEV#flashnote#5#")==1);
    owner=2; att_fail=0;
    rdx_file_download_tx_ready();
    CHECK(!fence.count && fence.state==FENCE_IDLE && !rdx_file_download_tx_held());
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(!strcmp(tx_log,"S12S34S") && !download.active && !allocations && !invalid && !irq);
    return 0;
}
int test_auto_record_preempt_waits_frame(void) {
    /* The cleanup waits for the frame in flight, then stops at its end. */
    reset(); loaded_size=60000;
    CHECK(!cmd(13,1,1,0,0));
    test_frame_set(test_malloc(64),10,64);
    CHECK(!rdx_file_download_preempt("F18") && !stop_sends);
    frame_end_tick=3;
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(ticks==3 && stop_sends==1 && fence.state==FENCE_IDLE);
    CHECK(!download.active && !rdx_protocol_bulk_frame_busy() && !allocations);
    /* A stalled link does not hold the recording's stop forever. */
    CHECK(!cmd(13,1,1,0,0));
    test_frame_set(test_malloc(64),10,64);
    CHECK(!rdx_file_download_preempt("F19") && stop_sends==1);
    ticks=0; frame_end_tick=0;
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(ticks==200 && stop_sends==2 && fence.state==FENCE_IDLE && !allocations);
    /* Without a frame in flight nothing waits. */
    CHECK(!cmd(13,1,1,0,0));
    CHECK(!rdx_file_download_preempt("F19") && stop_sends==3);
    ticks=0;
    rdx_protocol_download_preempt(preempt_generation);
    CHECK(!ticks && !invalid);
    return 0;
}
int test_auto_cleanup_keeps_queued_packets(void) {
    /* The cleanup drops queued download frames only; a status queued just
     * before it still goes out, in order. */
    reset(); CHECK(!cmd(13,1,1,0,0));
    int base=allocations;
    struct { void *data; u32 len; } packet;
    u8 descriptor[16]={1}; void *frame=test_malloc(32);
    memcpy(descriptor+4,&frame,sizeof(frame));
    packet.data="*DEV#record#1#"; packet.len=14; CHECK(test_queue_push(&packet));
    packet.data=descriptor; packet.len=16; CHECK(test_queue_push(&packet));
    packet.data="*DEV#stream#2#"; packet.len=14; CHECK(test_queue_push(&packet));
    CHECK(allocations==base+4);
    rdx_protocol_download_cleanup();
    CHECK(queue_count==2 && allocations==2);
    CHECK(queue_len[0]==14 && !memcmp(queue_data[0],"*DEV#record#1#",14));
    CHECK(queue_len[1]==14 && !memcmp(queue_data[1],"*DEV#stream#2#",14));
    while(queue_count) test_free(queue_data[--queue_count]);
    CHECK(!allocations && !invalid);
    return 0;
}
int test_auto_malformed_finish(void) {
    reset(); CHECK(!cmd(13,1,1,0,0));
    const char *bad[]={"*APP#opusfile#0#13#0#", "*APP#opusfile#0#13#0#1#junk#",
        "*APP#opusfile#0#4294967309#0#1#0#", "*APP#opusfile#0#13#-1#1#0#",
        "*APP#opusfile#0#13#0#1#0#extra", "*APP#opusfile#0#13#0#1"};
    for(int i=0;i<6;++i) CHECK(rdx_protocol_handle_recordFileData_req((u8*)bad[i],strlen(bad[i]))==3);
    CHECK(download.active && timer && clock_hold && !pending);
    CHECK(!cmd(13,0,1,0,0)); worker();
    CHECK(!raw_exists && !invalid && !allocations && !irq);
    return 0;
}
'''


def run_auto_delete_checks():
    audit = ROOT / 'cache/auto-delete-tests'
    audit.mkdir(parents=True, exist_ok=True)
    archive = os.environ.get('RDX_TEST_ARCHIVE', str(DIRECTORY / 'librdxApp_patched.a'))
    actual = {}
    for name in ('protocol', 'uxfile'):
        member = f'rdx_{name}.c.o'
        subprocess.run(['C:/JL/pi32/bin/llvm-ar.exe', 'x', archive, member],
                       cwd=audit, check=True, capture_output=True)
        path = audit / f'{name}.ll'
        subprocess.run(['C:/JL/pi32/bin/clang.exe', '-S', '-emit-llvm', '-target',
                        'pi32v2', '-mcpu=r3', '-x', 'ir', str(audit / member),
                        '-o', str(path)], check=True, capture_output=True)
        actual[name] = path.read_text(encoding='utf-8')

    # Protect the queue route and the cleanup/authorization boundary as well as
    # executing both sides. The target's ptrtoint queue ABI is not host-sized.
    submit = extract(actual['uxfile'], 'rdx_uxfile_auto_delete_submit')
    assert '@rdx_adv_uxfile_post' in submit and '306463821' in submit
    task = extract(actual['uxfile'], 'rdx_uxfile_task')
    assert '@rdx_file_download_delete_worker' in task
    # Standalone bitcode verification misses the LTO debug-location rule.
    assert re.search(r'call void @rdx_file_download_delete_worker\([^\n]+, !dbg !\d+',
                     actual['uxfile'])
    finish = extract(actual['uxfile'], 'rdx_uxfile_recordFileData_send_finish')
    assert '@rdx_uxfile_del_read_file_dat' not in finish

    selected = {
        'protocol': ['rdx_protocol_handle_recordFileData_req',
                     'rdx_protocol_handle_recordFileData_req_legacy',
                     'rdx_protocol_recordFileData_indicateByRequestNum_ble',
                     '_rdx_protocol_command_split_params',
                     'rdx_protocol_download_cleanup', '_bulk_data_copy_free',
                     'rdx_protocol_clear_send_queue', 'rdx_protocol_prepared_data_clean',
                     'rdx_protocol_file_transfer_track_reset',
                     'rdx_protocol_file_sync_busy_timer_start', 'rdx_protocol_file_sync_busy_timer_stop',
                     'rdx_protocol_file_sync_busy_timer_rerun', 'rdx_protocol_file_sync_busy_timer_cb',
                     'rdx_protocol_file_cmd_handle', 'rdx_protocol_uploadFileInfo_clean',
                     'rdx_protocol_download_preempt', 'rdx_protocol_bulk_frame_busy',
                     'rdx_protocol_download_purge_frames',
                     'rdx_protocol_free_send_queue_packet'],
        'uxfile': ['rdx_uxfile_recordFileData_send_finish', 'rdx_uxfile_delete_checked_guarded'],
    }
    bodies = '\n'.join(extract(actual[k], n) for k, names in selected.items() for n in names)
    all_ir = actual['protocol'] + '\n' + actual['uxfile']
    types = {}
    for line in re.findall(r'^%[\w.]+ = type .+$', all_ir, re.M):
        types.setdefault(line.split(' =')[0], line)
    # The library queue and the frame context are driven by the tests below.
    queue = ('rdx_queue_getQueueSize', 'rdx_queue_dequeue', 'rdx_queue_enqueue')
    defined = set(sum(selected.values(), [])) | set(queue)
    globals_, declarations = [], []
    for name in sorted(set(re.findall(r'@([\w.]+)', bodies)) - defined):
        global_ = re.search(r'^@' + re.escape(name) + r' = .+$', all_ir, re.M)
        if global_:
            globals_.append(re.sub(r', !dbg !\d+', '', global_[0]).replace('internal ', ''))
            continue
        decl = re.search(r'^declare [^\n]*@' + re.escape(name) + r'\([^\n]*', all_ir, re.M)
        if decl:
            declarations.append(re.sub(r' #\d+| !dbg !\d+', '', decl[0]))
        else:
            body = extract(all_ir, name)
            declarations.append(body.split('\n')[0].replace('define ', 'declare ').removesuffix(' {'))
    ir = '\n'.join([*types.values(), *globals_, *declarations, bodies])
    ir += '''
define void @test_cache_loaded() {
  store i1 true, i1* @g_dat_cache.3
  ret void
}
define void @test_frame_set(i8* %buffer, i32 %sent, i32 %total) {
  store i1 true, i1* @g_send_ctx.0
  store i8* %buffer, i8** @g_send_ctx.1
  store i32 %total, i32* @g_send_ctx.2
  store i32 %sent, i32* @g_send_ctx.3
  ret void
}
define void @test_frame_finish() {
  %total = load i32, i32* @g_send_ctx.2
  store i32 %total, i32* @g_send_ctx.3
  ret void
}
declare i32 @test_queue_size()
declare i32 @test_queue_pop(i8*)
declare i32 @test_queue_push(i8*)
define i32 @rdx_queue_getQueueSize(%struct.Queue* %queue) {
  %size = call i32 @test_queue_size()
  ret i32 %size
}
define zeroext i1 @rdx_queue_dequeue(%struct.Queue* %queue, %struct.DataPacket* %packet) {
  %raw = bitcast %struct.DataPacket* %packet to i8*
  %ok = call i32 @test_queue_pop(i8* %raw)
  %taken = icmp ne i32 %ok, 0
  ret i1 %taken
}
define zeroext i1 @rdx_queue_enqueue(%struct.Queue* %queue, %struct.DataPacket* byval align 4 %packet) {
  %raw = bitcast %struct.DataPacket* %packet to i8*
  %ok = call i32 @test_queue_push(i8* %raw)
  %kept = icmp ne i32 %ok, 0
  ret i1 %kept
}
'''
    ir = ir.replace('fastcc ', '').replace('@malloc(', '@test_malloc(').replace('@free(', '@test_free(')
    # Host LLVM spells the byval type out (target bitcode predates that).
    ir = re.sub(r'(%[\w.]+)\* byval(?!\()', lambda m: f'{m[1]}* byval({m[1]})', ir)

    header = (DIRECTORY / 'rdx_uxfile.h').read_text(encoding='utf-8')
    # Use the production layouts; omit the SDK-only function declarations.
    header = header[header.index('#define RECORD_FILE_NAME_SIZE'):header.index('void rdx_uxfile_init')]
    source = re.sub(r'^#include.*\n', '', (DIRECTORY / 'rdx_file_download.c').read_text(), flags=re.M)
    assert 'rdx_ble_server_file_transfer_changed();' in function(source, 'rdx_file_download_abort')
    assert 'rdx_ble_server_file_transfer_changed();' in function(source, 'rdx_file_download_request')
    # Every RDX characteristic send passes the preemption fence before ATT.
    server = (DIRECTORY / 'rdx_ble_server.c').read_text(encoding='utf-8')
    send = function(server, 'rdx_ble_server_send_internal')
    assert (send.index('rdx_file_download_tx_gate(data, len, expected_token)') <
            send.index('app_ble_att_send_data('))
    # Stream audio leaves queue room to the statuses held behind the stop.
    assert 'rdx_file_download_tx_held()' in function(server, 'rdx_ble_server_is_stream_tx_ready')
    assert 'rdx_file_download_tx_ready();' in server
    preempt = extract(actual['protocol'], 'rdx_protocol_download_preempt')
    assert '@rdx_file_download_preempt_done' in preempt
    assert '@rdx_protocol_fileTransfer_stop_indicate' not in preempt
    assert preempt.index('@rdx_file_download_preempt_wait') < preempt.index('@os_mutex_pend')
    cleanup = extract(actual['protocol'], 'rdx_protocol_download_cleanup')
    assert '@rdx_protocol_download_purge_frames' in cleanup
    assert '@rdx_protocol_clear_send_queue' not in cleanup
    # Once a frame starts, queued packets wait for its end.
    sender = extract(actual['protocol'], '_rdx_protocol_ble_send')
    assert not re.search(r'br i1 %23, label %26, label %24', sender)
    storage = (DIRECTORY / 'rdx_record_format.c').read_text(encoding='utf-8')
    storage_functions = ''.join(function(storage, n) for n in
                                ('rdx_record_format_io_leave', 'rdx_record_format_delete_acquire',
                                 'rdx_record_format_delete_release'))
    path = audit / 'auto.c'
    path.write_text(STUBS + header + BOUNDARIES + storage_functions + source + TESTS, encoding='utf-8')
    result = subprocess.run(['C:/JL/pi32/bin/clang.exe', '-target', llvm.get_default_triple(),
                             '-O0', '-S', '-emit-llvm', str(path), '-o', str(path.with_suffix('.ll'))],
                            capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    native = re.sub(r' "target-(?:cpu|features)"="[^"]*"', '', path.with_suffix('.ll').read_text())
    native = re.sub(r'(%[\w.]+)\* (noalias )?(sret|byval)(?!\()',
                    lambda m: f'{m[1]}* {m[2] or ""}{m[3]}({m[1]})', native)
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
    target = llvm.Target.from_default_triple().create_target_machine()
    module = llvm.parse_assembly(native)
    library = llvm.parse_assembly(ir)
    for m in (module, library):
        m.triple = llvm.get_default_triple()
        m.data_layout = str(target.target_data)
        m.verify()
    module.link_in(library)
    module.verify()
    _reject_unresolved_calls(module)
    (audit / 'linked.ll').write_text(str(module), encoding='utf-8')
    engine = llvm.create_mcjit_compiler(module, target)
    engine.finalize_object()
    for test in re.findall(r'^int (test_auto_\w+)\(void\)', TESTS, re.M):
        result = ctypes.CFUNCTYPE(ctypes.c_int)(engine.get_function_address(test))()
        if result:
            state = {n: ctypes.c_int.from_address(engine.get_global_value_address(n)).value
                     for n in ('rf_service', 'power', 'rf_io_users', 'allocations', 'invalid',
                               'delete_calls', 'sends', 'timer', 'clock_hold')}
            raise AssertionError(f'{test} failed at {path}:{result}: {state}')
        print(f'PASS: actual archive {test}')
