"""Run the production CDC recording worker with deterministic I/O completion."""
import re
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, run_c_checks


STUBS = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long size_t;
typedef int FILE;
typedef int OS_SEM;
#define NULL ((void *)0)
#define TCFG_T2620_CDC_FILE_ENABLE 1
#define MIN(first,second) ((first)<(second)?(first):(second))
void *memcpy(void *,const void *,size_t);
void *memset(void *,int,size_t);
char *strcpy(char *,const char *);
void rdx_cdc_source_service(void);
void rdx_cdc_source_cancel(void);
void rdx_cdc_source_release(void);
static u32 now, position;
static int lease, releases, handles, closes, reads, wakes, blocked, shutdown;
static int open_result, read_failure, close_failure, task_failure, acquire_failure;
static int cancel_read, detach_open, close_timeout;
static int worker_stalled;
static int list_opens, late_conflict;
static void source_work(void);
void local_irq_disable(void) {}
void local_irq_enable(void) {}
u32 sys_timer_get_ms(void) { return now; }
void rdx_factory_cdc_wake(void) { ++wakes; }
int rdx_dut_is_in_mode(void) { return 0; }
int rdx_factory_audio_owned(void) { return 0; }
int rdx_app_factory_file_is_busy(void) { return lease; }
int rdx_app_cdc_file_conflict(void) { return late_conflict; }
int rdx_storage_lifecycle_business_blocked(void) { return blocked; }
int rdx_dip_switch_shutdown_pending(void) { return shutdown; }
void rdx_app_emmc_poweroff_check_timer_stop(void) {}
int os_sem_create(OS_SEM *sem,int value) { return 0; }
int os_sem_del(OS_SEM *sem,int value) { return 0; }
int os_sem_post(OS_SEM *sem) { return 0; }
int os_sem_pend(OS_SEM *sem,int value) { return 0; }
int os_task_create(void (*worker)(void *),void *arg,int priority,int stack,int queue,const char *name) {
    return task_failure;
}
void os_time_dly(int ticks) { now+=10; if(!worker_stalled) source_work(); }
int rdx_record_format_download_acquire(void) {
    if(acquire_failure || lease) return -1;
    lease=1; return 0;
}
void rdx_record_format_download_release(void) { lease=0; ++releases; }
int rdx_record_format_download_open(u32 sn,const char *name,void **handle,u32 *size) {
    *handle=NULL; *size=0;
    if(detach_open) rdx_cdc_source_release();
    if(open_result) return open_result;
    *handle=(void *)1; *size=10000; ++handles; position=0; return 0;
}
int rdx_record_format_download_list_open(void **handle,u32 *size) {
    ++list_opens;
    int result=rdx_record_format_download_open(0,"",handle,size);
    if(!result) *size=2;
    return result;
}
int fread(void *data,int unit,u32 length,FILE *handle) {
    ++reads;
    if(cancel_read) rdx_cdc_source_cancel();
    if(read_failure) return -1;
    for(u32 index=0;index<length;++index) ((u8 *)data)[index]=(u8)((position+index)*19+7);
    position+=length; return length;
}
int fclose(FILE *handle) {
    ++closes;
    if(close_timeout) { now+=3000; rdx_cdc_source_service(); }
    if(close_failure) return -1;
    --handles; return 0;
}
'''

TESTS = r'''
#define CHECK(value) do { if(!(value)) return __LINE__; } while(0)
static u8 received[10000], block[4096];
static void reset(void) {
    memset(&source,0,sizeof(source));
    now=position=0;
    lease=releases=handles=closes=reads=wakes=blocked=shutdown=0;
    open_result=read_failure=close_failure=task_failure=acquire_failure=0;
    cancel_read=detach_open=close_timeout=0;
    worker_stalled=list_opens=late_conflict=0;
}
int test_source_stream(void) {
    reset(); CHECK(!rdx_cdc_source_begin(7,"abc123.raw") && lease);
    source_work();
    CHECK(handles==1 && reads==2 && source.ready[0] && source.ready[1] && !source.closed);
    u32 length=0, size=0, offset=0;
    for(int attempt=0;attempt<3;++attempt) {
        CHECK(rdx_cdc_source_take(block,&length,&size)==1 && size==10000);
        memcpy(received+offset,block,length); offset+=length;
        source_work();
    }
    CHECK(offset==10000 && !handles && closes==1 && source.closed==1 && lease);
    for(u32 index=0;index<offset;++index) CHECK(received[index]==(u8)(index*19+7));
    CHECK(rdx_cdc_source_take(block,&length,&size)==0);
    rdx_cdc_source_release(); rdx_cdc_source_service();
    CHECK(!source.busy && !lease && releases==1);
    rdx_cdc_source_release(); CHECK(releases==1);
    CHECK(!rdx_cdc_source_begin(7,"abc123.raw"));
    CHECK(!rdx_cdc_source_shutdown() && !lease && !handles);
    reset(); CHECK(!rdx_cdc_source_list_begin() && lease);
    source_work();
    CHECK(list_opens==1 && source.closed==1 && !handles && lease);
    CHECK(rdx_cdc_source_take(block,&length,&size)==1 && length==2 && size==2);
    rdx_cdc_source_release();
    CHECK(!lease && releases==1);
    return 0;
}
int test_source_late_completion(void) {
    reset(); CHECK(!rdx_cdc_source_begin(7,"abc.raw"));
    detach_open=1; source_work(); rdx_cdc_source_service();
    CHECK(!source.busy && !handles && !reads && closes==1 && releases==1);
    reset(); CHECK(!rdx_cdc_source_begin(7,"abc.raw"));
    cancel_read=1; source_work();
    CHECK(!source.ready[0] && !source.ready[1] && !handles && source.closed==1);
    rdx_cdc_source_release(); CHECK(!lease && releases==1);
    reset(); CHECK(!rdx_cdc_source_begin(7,"abc.raw")); source_work();
    rdx_cdc_source_release(); CHECK(source.busy && lease);
    CHECK(rdx_cdc_source_begin(8,"def.raw")==2);
    source_work(); rdx_cdc_source_service();
    CHECK(!source.busy && releases==1 && !handles);
    return 0;
}
int test_source_faults(void) {
    u32 length,size;
    reset(); open_result=-3; CHECK(!rdx_cdc_source_begin(7,"abc.raw")); source_work();
    CHECK(rdx_cdc_source_take(block,&length,&size)==-3 && source.closed==1);
    rdx_cdc_source_release(); CHECK(!lease && releases==1);
    reset(); read_failure=1; CHECK(!rdx_cdc_source_begin(7,"abc.raw")); source_work();
    CHECK(rdx_cdc_source_take(block,&length,&size)==-5 && !handles && closes==1);
    rdx_cdc_source_release(); CHECK(!lease);
    reset(); close_failure=1; CHECK(!rdx_cdc_source_begin(7,"abc.raw")); source_work();
    rdx_cdc_source_release(); source_work(); rdx_cdc_source_service();
    CHECK(source.closed==2 && source.busy && lease && releases==0 && closes==1);
    CHECK(rdx_cdc_source_shutdown()<0 && closes==1);
    CHECK(rdx_cdc_source_begin(7,"abc.raw")==8);
    reset(); open_result=-2; CHECK(!rdx_cdc_source_begin(7,"abc.raw")); source_work();
    rdx_cdc_source_release(); CHECK(source.closed==2 && source.busy && lease);
    reset(); close_timeout=1; CHECK(!rdx_cdc_source_begin(7,"abc.raw")); source_work();
    rdx_cdc_source_cancel(); source_work();
    CHECK(source.error==7 && source.closed==1 && !handles);
    rdx_cdc_source_release(); CHECK(!lease);
    reset(); CHECK(!rdx_cdc_source_begin(7,"abc.raw"));
    source_operation(1); now=7999; rdx_cdc_source_service(); CHECK(!source.error);
    now=8000; rdx_cdc_source_service(); CHECK(source.error==7 && source.abort && lease);
    rdx_cdc_source_release(); CHECK(source.busy);
    source_work(); rdx_cdc_source_service(); CHECK(!source.busy && !lease);
    reset(); CHECK(!rdx_cdc_source_begin(7,"abc.raw")); worker_stalled=1;
    CHECK(rdx_cdc_source_shutdown()<0 && now==500 && source.busy && lease);
    worker_stalled=0; source_work(); rdx_cdc_source_service(); CHECK(!source.busy && !lease);
    return 0;
}
int test_source_admission(void) {
    reset(); blocked=1; CHECK(rdx_cdc_source_begin(7,"abc.raw")==8 && !lease);
    reset(); shutdown=1; CHECK(rdx_cdc_source_begin(7,"abc.raw")==8 && !lease);
    reset(); task_failure=1; CHECK(rdx_cdc_source_begin(7,"abc.raw")==8 && !lease);
    reset(); acquire_failure=1; CHECK(rdx_cdc_source_begin(7,"abc.raw")==2 && !lease);
    reset(); late_conflict=1;
    CHECK(rdx_cdc_source_begin(7,"abc.raw")==2 && !lease && releases==1);
    CHECK(!source.busy && !handles && !reads);
    late_conflict=0; CHECK(!rdx_cdc_source_begin(7,"abc.raw"));
    CHECK(!rdx_cdc_source_shutdown());
    reset(); CHECK(!rdx_cdc_source_begin(7,"abc.raw"));
    CHECK(rdx_cdc_source_begin(8,"def.raw")==2);
    CHECK(!rdx_cdc_source_shutdown() && !lease && !handles);
    return 0;
}
'''


def run_source_checks():
    folder = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    source = re.sub(r'^#include.*$', '', (folder / 'rdx_cdc_source.c').read_text(encoding='utf-8'), flags=re.M)
    with tempfile.TemporaryDirectory(prefix='cdc_source_') as directory:
        path = Path(directory) / 'source.c'
        path.write_text(STUBS + source + TESTS, encoding='utf-8')
        run_c_checks(path, ('test_source_stream', 'test_source_late_completion',
                           'test_source_faults', 'test_source_admission'), native=True)
    print('PASS: actual recording worker: double buffer, late open/read, close quarantine, deadlines, shutdown')


if __name__ == '__main__':
    run_source_checks()
