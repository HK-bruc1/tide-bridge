"""执行固件下载参数策略，验证延迟省电与旧回调隔离。"""
import tempfile
from pathlib import Path
from host_c_test_lib import ROOT, run_c_checks


def main():
    server = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c').read_text(encoding='utf-8-sig')
    code = server[server.index('static const struct conn_update_param_t connection_param_table[]'):server.index('/**************************************************************************', server.index('void rdx_ble_server_post_transfer_lowpower(void)'))]
    code += server[server.index('static void rdx_ble_server_download_info_view('):server.index('rdx_ble_server_info_t * rdx_ble_server_get_info(void)')]
    stubs = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
typedef struct { u8 ble_conn; u16 ble_mtu_size; u32 owner; } rdx_ble_server_info_t;
typedef struct { u32 epoch; } rdx_ble_async_token_t;
typedef struct { u16 con_handle, conn_interval, conn_latency; } rdx_ble_link_state_t;
typedef struct { u8 file_send_busy; } ReqFileInfo;
typedef struct { u8 run, process_state; } RecordStatus;
#define RECORD_STATE_STOP 3
#define REC_PROCESS_STATE_BUSY 1
static RecordStatus record = {3,0};
RecordStatus *rdx_record_get_status(void) {return &record;}
struct conn_update_param_t { u16 interval_min, interval_max, latency, timeout; };
#define Q_CALLBACK 1
#define r_printf(...) ((void)0)
#define log_info(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static u32 now, epoch=1, queued, delay;
static int fail_queue, fail_timer, requests, last_index, busy;
static rdx_ble_link_state_t link = {80,72,19};
static ReqFileInfo file;
static void (*timer_cb)(void *);
static void *timer_arg;
void local_irq_disable(void) {}
void local_irq_enable(void) {}
u32 sys_timer_get_ms(void) { return now; }
int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t, int ready) {t->epoch=epoch; return epoch!=0;}
rdx_ble_link_state_t *rdx_ble_session_rdx_token_resolve(const rdx_ble_async_token_t *t,int ready) {return epoch && t->epoch==epoch ? &link : 0;}
void rdx_ble_session_link_set_conn_param_index(rdx_ble_link_state_t *l,u8 i) {last_index=i;}
int ble_op_conn_param_request(u16 c,struct conn_update_param_t *p) {requests++;return 0;}
ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) {return &file;}
u8 rdx_is_file_transfer_active(void) {return busy;}
u8 rdx_is_file_sync_busy(void) {return 0;}
u16 sys_timeout_add_2_task(void *arg,void (*cb)(void *),u32 d,const char *task) {if(fail_timer)return 0;timer_arg=arg;timer_cb=cb;delay=d;return 1;}
void sys_timeout_del(u16 id) {timer_cb=0;}
int os_taskq_post_type(const char *task,int type,int count,int *msg) {if(fail_queue)return -1;queued=(u32)msg[2];return 0;}
'''
    checks = r'''
static void drain(void) {u32 g=queued;queued=0;if(g)rdx_ble_server_transfer_param_run((void *)g);}
static void fire(void) {void (*cb)(void *)=timer_cb;void *arg=timer_arg;timer_cb=0;if(cb)cb(arg);}
static void reset(void) {
    rdx_ble_server_transfer_param_cancel();
    now=0;epoch=1;queued=0;record.run=3;record.process_state=0;requests=0;busy=0;file.file_send_busy=0;
    fail_queue=fail_timer=0;link.conn_interval=72;link.conn_latency=19;
}
int check_batch(void) {
    reset();rdx_ble_server_request_stream_fast_param();drain();CHECK(requests==1 && last_index==0);
    link.conn_interval=6;link.conn_latency=4;now=2000;fire();CHECK(!timer_cb);
    rdx_ble_server_post_transfer_lowpower();drain();CHECK(delay==5000 && requests==1);
    void *stale=timer_arg;
    now=4000;rdx_ble_server_request_stream_fast_param();drain();CHECK(!timer_cb && requests==1);
    rdx_ble_server_transfer_param_run(stale);CHECK(requests==1);
    rdx_ble_server_post_transfer_lowpower();drain();now=8999;fire();CHECK(requests==1 && delay==1);
    now=9000;fire();CHECK(requests==2 && last_index==8);
    return 0;
}
int check_previous_fast_interval(void) {
    reset();link.conn_interval=10;link.conn_latency=4;
    rdx_ble_server_request_stream_fast_param();drain();CHECK(requests==1);
    now=2000;fire();CHECK(requests==2);
    link.conn_interval=6;now=4000;fire();CHECK(requests==2 && !timer_cb);
    return 0;
}
int check_retry_and_stale(void) {
    reset();rdx_ble_server_request_stream_fast_param();drain();
    now=2000;fire();now=4000;fire();now=6000;fire();CHECK(requests==3 && !timer_cb);
    rdx_ble_server_transfer_param_run((void *)g_rdx_transfer_param.generation);CHECK(requests==3);
    rdx_ble_server_post_transfer_lowpower();drain();void *old=timer_arg;
    epoch=2;now+=5000;fire();CHECK(requests==3 && !g_rdx_transfer_param.enabled);
    rdx_ble_server_request_stream_fast_param();drain();int count=requests;
    rdx_ble_server_transfer_param_run(old);CHECK(requests==count);
    rdx_ble_server_transfer_param_cancel();fire();CHECK(requests==count);
    return 0;
}
int check_busy_failure_wrap(void) {
    reset();rdx_ble_server_post_transfer_lowpower();drain();file.file_send_busy=1;
    now=5000;fire();CHECK(!requests && delay==5000);
    file.file_send_busy=0;now=10000;fire();CHECK(requests==1 && last_index==8);
    reset();fail_queue=1;rdx_ble_server_request_stream_fast_param();drain();CHECK(!requests);fire();CHECK(requests==1);
    reset();rdx_ble_server_post_transfer_lowpower();drain();record.run=0;now=5000;fire();CHECK(!requests);
    reset();fail_timer=1;rdx_ble_server_post_transfer_lowpower();drain();CHECK(!requests);
    reset();now=0xfffffff0u;rdx_ble_server_post_transfer_lowpower();drain();now+=5000;fire();CHECK(requests==1);
    return 0;
}
int check_late_slow(void) {
    reset();link.conn_interval=6;link.conn_latency=4;
    rdx_ble_server_request_stream_fast_param();drain();CHECK(!requests);
    link.conn_interval=72;link.conn_latency=19;
    rdx_ble_server_transfer_param_run((void *)g_rdx_transfer_param.generation);
    CHECK(requests==1 && last_index==0 && timer_cb);
    return 0;
}
int check_duplicate_fast(void) {
    reset();rdx_ble_server_request_stream_fast_param();drain();
    void *stale=timer_arg;
    /* 重现连续 getlist，每次入口之间都让任务执行，不能只测队列合并。 */
    for (now=5;now<=20;now+=5) {
        rdx_ble_server_request_stream_fast_param();drain();CHECK(requests==1);
    }
    rdx_ble_server_transfer_param_run(stale);CHECK(requests==1);
    now=1999;fire();CHECK(requests==1 && delay==1);
    now=2000;fire();CHECK(requests==2);
    /* 文件结束后立即开始下个文件，也不能重置未完成协商的预算。 */
    now=2010;rdx_ble_server_post_transfer_lowpower();drain();
    now=2020;rdx_ble_server_request_stream_fast_param();drain();
    CHECK(requests==2 && delay==1980);
    now=4000;fire();CHECK(requests==3);
    now=6000;fire();CHECK(!timer_cb);
    now=8000;rdx_ble_server_request_stream_fast_param();drain();
    CHECK(requests==3 && !timer_cb);
    /* 实际进入省电请求后，下轮下载可以重新申请。 */
    rdx_ble_server_post_transfer_lowpower();drain();now=13000;fire();
    CHECK(requests==4 && last_index==8);
    now=13010;rdx_ble_server_request_stream_fast_param();drain();
    CHECK(requests==5 && last_index==0);
    /* 连接所有者变化后，不能继承旧连接预算。 */
    epoch=2;now=13020;rdx_ble_server_request_stream_fast_param();drain();
    CHECK(requests==6);
    return 0;
}
int check_duplicate_wrap(void) {
    reset();now=0xfffffff0u;rdx_ble_server_request_stream_fast_param();drain();
    now+=10;rdx_ble_server_request_stream_fast_param();drain();
    CHECK(requests==1 && delay==1990);
    now+=1989;fire();CHECK(requests==1 && delay==1);
    now++;fire();CHECK(requests==2);
    return 0;
}

int check_download_view(void) {
    rdx_ble_server_info_t actual = {1,514,123}, view;
    rdx_ble_server_download_info_view(&view,&actual,1);
    CHECK(view.ble_mtu_size-10==244 && actual.ble_mtu_size==514);
    CHECK(view.ble_conn==1 && view.owner==123);
    rdx_ble_server_download_info_view(&view,&actual,0);
    CHECK(view.ble_mtu_size==514);
    actual.ble_conn=0;rdx_ble_server_download_info_view(&view,&actual,1);
    CHECK(!view.ble_conn && view.ble_mtu_size==514);
    actual.ble_conn=1;
    for (u32 mtu=0;mtu<=254;++mtu) {
        actual.ble_mtu_size=mtu;
        rdx_ble_server_download_info_view(&view,&actual,1);
        CHECK(view.ble_mtu_size==mtu && actual.ble_mtu_size==mtu);
    }
    actual.ble_mtu_size=517;actual.owner=456;
    rdx_ble_server_download_info_view(&view,&actual,1);
    CHECK(view.ble_mtu_size==254 && view.owner==456 && actual.ble_mtu_size==517);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='ble-download-') as tmp:
        path = Path(tmp) / 'download.c'
        path.write_text(stubs + code + checks, encoding='utf-8')
        run_c_checks(path, ['check_batch', 'check_previous_fast_interval', 'check_retry_and_stale', 'check_busy_failure_wrap', 'check_late_slow', 'check_duplicate_fast', 'check_duplicate_wrap', 'check_download_view'])
    print('BLE download parameter policy: batch, deadline, stale token, bounded retry and faults passed.')

if __name__ == '__main__':
    main()
