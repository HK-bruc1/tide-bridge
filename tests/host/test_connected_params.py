"""执行真实 RDX 快档协调器；覆盖停用慢档、旧库入口与链路生命周期。"""
import re
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, function, run_c_checks

FOLDER = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
#define NULL ((void*)0)
#define Q_CALLBACK 1
#define RDX_BLE_LINK_INVALID_INDEX 255
#define RDX_BLE_LINK_MAX 2
#define TCFG_T2620_CONN_SLAVE_LATENCY 49
#define r_printf(...) ((void)0)
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
void *memset(void*,int,unsigned long long);
typedef struct { u8 slot_index; u32 slot_generation, transport_epoch; } rdx_ble_async_token_t;
typedef struct { u16 con_handle, conn_interval, conn_latency; u8 hid, pairing, connected; u32 generation; } rdx_ble_link_state_t;
struct conn_update_param_t { u16 interval_min, interval_max, latency, timeout; };
typedef struct { int file_send_busy; } ReqFileInfo;
static rdx_ble_link_state_t links[2];
static int owner, active, queue_pending, queue_fail, timer_fail, in_core, irq, context_error;
static int requests, accept_requests, request_result, race;
static u32 now, epoch, slot_generation, transport_epoch;
static u16 request_handles[256], request_modes[256];
static u32 request_times[256];
static struct { int goto_poweroff_flag; } app_var;
static struct { void (*cb)(void*); void *arg; u32 due; int active; } timers[512];
static u16 next_timer;
void rdx_ble_server_conn_param_changed(void);
void rdx_ble_server_request_stream_fast_param(void);
void rdx_ble_server_post_transfer_lowpower(void);
void local_irq_disable(void) { ++irq; }
void local_irq_enable(void) { --irq; }
u32 sys_timer_get_ms(void) { return now; }
u32 rdx_ble_session_rdx_runtime_epoch_get(void) {
    if(race) { race=0; active=0; ++epoch; rdx_ble_server_conn_param_changed(); }
    return epoch;
}
u8 rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t,u8 require) {
    if(!active || owner<0 || !links[owner].connected) return 0;
    t->slot_index=owner; t->slot_generation=slot_generation; t->transport_epoch=transport_epoch;
    return 1;
}
rdx_ble_link_state_t *rdx_ble_session_rdx_token_resolve(const rdx_ble_async_token_t *t,u8 require) {
    if(!active || owner<0 || !links[owner].connected || t->slot_index!=owner || t->slot_generation!=slot_generation ||
       t->transport_epoch!=transport_epoch) return NULL;
    return &links[owner];
}
rdx_ble_async_token_t rdx_ble_session_token_capture(const rdx_ble_link_state_t *l) {
    rdx_ble_async_token_t t = {l-links, l->generation, transport_epoch}; return t;
}
rdx_ble_link_state_t *rdx_ble_session_link_token_resolve(const rdx_ble_async_token_t *t) {
    if(t->slot_index>=2 || !links[t->slot_index].connected ||
       t->slot_generation!=links[t->slot_index].generation || t->transport_epoch!=transport_epoch) return NULL;
    return &links[t->slot_index];
}
u8 rdx_ble_session_link_index(rdx_ble_link_state_t *l) { return l-links; }
u8 rdx_ble_session_link_is_hid(rdx_ble_link_state_t *l) { return l && l->hid; }
void rdx_ble_session_link_set_conn_param_index(rdx_ble_link_state_t *l,u8 i) { if(i) ++context_error; }
int ble_op_conn_param_request(u16 handle,struct conn_update_param_t *p) {
    if(!in_core || irq || (handle!=links[0].con_handle && handle!=links[1].con_handle)) ++context_error;
    if(p->interval_min!=6 || p->interval_max!=12 || p->latency!=49 || p->timeout!=600) ++context_error;
    request_handles[requests]=handle; request_modes[requests]=p->interval_min;
    request_times[requests++]=now;
    if(accept_requests && !request_result) {
        int index=handle==0x50 ? 0 : 1;
        links[index].conn_interval=p->interval_min;
        links[index].conn_latency=p->latency;
        rdx_ble_server_conn_param_changed();
    }
    return request_result;
}
int os_taskq_post_type(const char *task,int type,int n,int *msg) {
    if(task[0]!='a' || type!=Q_CALLBACK || n!=3 || msg[1]!=1 || msg[2]!=0 || irq) ++context_error;
    if(queue_fail) return -1;
    ++queue_pending; return 0;
}
u16 sys_timeout_add_2_task(void *arg,void (*cb)(void*),u32 delay,const char *task) {
    if(!in_core || irq || task[0]!='a') ++context_error;
    if(timer_fail) return 0;
    u16 id=++next_timer; if(id>=512) return 0;
    timers[id].cb=cb; timers[id].arg=arg; timers[id].due=now+delay; timers[id].active=1;
    return id;
}
void sys_timeout_del(u16 id) {
    if(!in_core || irq) ++context_error;
    timers[id].active=0;
}
'''

TESTS = r'''
static void reset(void) {
    memset(g_rdx_transfer_params,0,sizeof(g_rdx_transfer_params));
    memset(&g_rdx_param_events,0,sizeof(g_rdx_param_events));
    memset(timers,0,sizeof(timers)); memset(links,0,sizeof(links));
    links[0].con_handle=0x50; links[1].con_handle=0x51;
    links[0].connected=links[1].connected=1; links[0].generation=links[1].generation=1;
    links[0].conn_interval=links[1].conn_interval=24;
    now=next_timer=queue_pending=queue_fail=timer_fail=0;
    requests=context_error=irq=in_core=request_result=race=0;
    epoch=slot_generation=transport_epoch=1; owner=0; active=accept_requests=1;
    app_var.goto_poweroff_flag=0;
}
static int drain(void) {
    int count=0; in_core=1;
    while(queue_pending && ++count<30) { --queue_pending; rdx_ble_server_transfer_param_run(NULL); }
    in_core=0; return !queue_pending && !context_error && !irq;
}
static int changed(void) { rdx_ble_server_conn_param_changed(); return drain(); }
static int advance(u32 dt) {
    u32 end=now+dt; int count=0;
    while(1) {
        u16 id=0;
        for(u16 i=1;i<=next_timer;++i) if(timers[i].active &&
            (s32)(timers[i].due-end)<=0 && (!id || (s32)(timers[i].due-timers[id].due)<0)) id=i;
        if(!id) break;
        if(++count>100) return 0;
        now=timers[id].due; timers[id].active=0;
        in_core=1; timers[id].cb(timers[id].arg); in_core=0;
        if(!drain()) return 0;
    }
    now=end; return !context_error;
}
static int service(void) {
    in_core=1; rdx_ble_server_conn_param_service(); in_core=0; return drain();
}
int check_idle_and_legacy_paths_stay_fast(void) {
    reset(); CHECK(changed()); CHECK(requests==1 && request_modes[0]==6);
    CHECK(advance(1200000)); CHECK(requests==1 && !g_rdx_transfer_params[0].timer);
    for(int i=0;i<9;++i) {
        rdx_ble_server_send_request_connect_parameter(i); CHECK(drain());
        rdx_ble_server_post_transfer_lowpower(); CHECK(drain());
        CHECK(advance(10000)); CHECK(requests==1 && !g_rdx_transfer_params[0].timer);
    }
    rdx_ble_server_send_request_connect_parameter(9);
    rdx_ble_server_send_request_connect_parameter(255); CHECK(!queue_pending);
    for(int i=0;i<9;++i) {
        reset(); rdx_ble_server_send_request_connect_parameter(i); CHECK(drain());
        CHECK(requests==1 && request_modes[0]==6);
    }
    reset(); rdx_ble_server_post_transfer_lowpower(); CHECK(drain()); CHECK(requests==1);
    reset(); active=0; CHECK(changed());
    rdx_ble_server_request_stream_fast_param(); rdx_ble_server_post_transfer_lowpower();
    CHECK(!requests && !queue_pending && !g_rdx_transfer_params[0].timer);
    return 0;
}
int check_actual_fast_range(void) {
    CHECK(rdx_fast_conn_param.interval_min==6 && rdx_fast_conn_param.interval_max==12);
    CHECK(rdx_fast_conn_param.latency==49 && rdx_fast_conn_param.timeout==600);
    for(u16 i=6;i<=12;++i) {
        reset(); links[0].conn_interval=i; links[0].conn_latency=49;
        CHECK(changed()); CHECK(advance(1200000));
        CHECK(!requests && !g_rdx_transfer_params[0].timer);
        /* 手机自行降回 latency 0/4（APP 切换连接优先级）仍须重新请求。 */
        for(u16 latency=0;latency<=4;latency+=4) {
            reset(); links[0].conn_interval=i; links[0].conn_latency=latency;
            CHECK(changed()); CHECK(requests==1);
        }
    }
    reset(); links[0].conn_interval=5; CHECK(changed()); CHECK(requests==1);
    reset(); links[0].conn_interval=13; CHECK(changed()); CHECK(requests==1);
    reset(); links[0].conn_interval=9; links[0].conn_latency=5;
    CHECK(changed()); CHECK(requests==1);
    return 0;
}
int check_rejection_and_delayed_confirmation(void) {
    for(int result=-1;result<=0;++result) {
        reset(); accept_requests=0; request_result=result; CHECK(changed());
        for(int i=0;i<80;++i) {
            rdx_ble_server_request_stream_fast_param();
            rdx_ble_server_post_transfer_lowpower();
            rdx_ble_server_send_request_connect_parameter(8);
            CHECK(changed()); CHECK(advance(100));
        }
        CHECK(requests==3 && !g_rdx_transfer_params[0].timer);
        CHECK(advance(19000)); CHECK(requests==3);
        active=0; ++epoch; rdx_ble_server_transfer_param_cancel(); CHECK(drain());
        active=1; ++epoch; CHECK(changed()); CHECK(advance(20000));
        CHECK(requests==3 && !g_rdx_transfer_params[0].timer);
        links[0].conn_interval=9; links[0].conn_latency=49; CHECK(changed());
        CHECK(advance(1200000)); CHECK(requests==3 && !g_rdx_transfer_params[0].timer);
        links[0].conn_interval=72; links[0].conn_latency=19; CHECK(changed());
        CHECK(advance(6000)); CHECK(requests==6 && !g_rdx_transfer_params[0].timer);
        for(int i=0;i<requests;++i) CHECK(request_modes[i]==6);
        for(int i=1;i<requests;++i) CHECK((u32)(request_times[i]-request_times[i-1])>=2000);
    }
    return 0;
}
int check_confirmed_then_late_slow_is_throttled(void) {
    reset(); CHECK(changed()); CHECK(advance(100));
    links[0].conn_interval=72; links[0].conn_latency=0;
    CHECK(changed()); CHECK(requests==1); CHECK(advance(1899)); CHECK(requests==1);
    CHECK(advance(1)); CHECK(requests==2 && links[0].conn_interval==6);
    CHECK(advance(1200000)); CHECK(requests==2 && !g_rdx_transfer_params[0].timer);
    return 0;
}
int check_hid_and_owner_isolation(void) {
    reset(); links[0].hid=1; CHECK(changed()); CHECK(advance(120000));
    CHECK(requests==1 && !g_rdx_transfer_params[0].timer);
    reset(); links[0].pairing=1; CHECK(changed()); CHECK(advance(120000)); CHECK(requests==1);
    reset(); links[1].hid=1; CHECK(changed()); CHECK(advance(120000));
    CHECK(requests==1 && request_handles[0]==0x50 && links[1].conn_interval==24);
    reset(); active=0; links[1].hid=1; CHECK(changed()); CHECK(!requests);
    return 0;
}
int check_hid_survives_rdx_release_and_handoff(void) {
    reset(); CHECK(changed()); CHECK(advance(100));
    links[0].hid=1; links[0].conn_interval=72; links[0].conn_latency=19;
    active=0; ++epoch; rdx_ble_server_transfer_param_cancel(); CHECK(drain());
    CHECK(requests==1); CHECK(advance(1900));
    CHECK(requests==2 && request_handles[1]==0x50 && links[0].conn_interval==6);
    owner=1; active=1; ++epoch; CHECK(changed());
    CHECK(requests==3 && request_handles[2]==0x51);
    CHECK(advance(2000)); links[0].conn_interval=72; CHECK(changed());
    CHECK(requests==4 && request_handles[3]==0x50);
    CHECK(advance(1200000)); CHECK(requests==4);
    CHECK(!g_rdx_transfer_params[0].timer && !g_rdx_transfer_params[1].timer);
    reset(); CHECK(changed()); active=0; ++epoch;
    rdx_ble_server_transfer_param_cancel(); CHECK(drain());
    CHECK(advance(2000)); links[0].conn_interval=72; CHECK(changed()); CHECK(requests==1);
    links[0].hid=1; CHECK(changed()); CHECK(requests==2 && links[0].conn_interval==6);
    return 0;
}
int check_stale_callbacks_and_reconnect(void) {
    reset(); accept_requests=0; CHECK(changed()); CHECK(advance(100));
    u16 old=g_rdx_transfer_params[0].timer; void (*cb)(void*)=timers[old].cb; void *arg=timers[old].arg;
    active=0; rdx_ble_server_transfer_param_cancel(); CHECK(drain());
    int n=requests; in_core=1; cb(arg); in_core=0; CHECK(requests==n);
    active=1; ++epoch; ++slot_generation; links[1].generation=slot_generation; owner=1; CHECK(changed());
    u16 fresh=g_rdx_transfer_params[1].timer;
    in_core=1; cb(arg); in_core=0; CHECK(g_rdx_transfer_params[1].timer==fresh);
    CHECK(advance(6000)); CHECK(request_handles[requests-1]==0x51 && requests==4);
    reset(); accept_requests=0; CHECK(changed()); CHECK(advance(6000)); CHECK(requests==3);
    active=0; links[0].connected=0; rdx_ble_server_transfer_param_cancel(); CHECK(drain());
    active=1; links[0].connected=1; ++epoch; ++slot_generation; links[0].generation=slot_generation;
    CHECK(changed()); CHECK(advance(6000)); CHECK(requests==6);
    reset(); rdx_ble_server_request_stream_fast_param(); active=0; ++epoch;
    CHECK(drain()); CHECK(!requests);
    return 0;
}
int check_races_and_dispatch_failure(void) {
    reset(); race=1; CHECK(changed()); CHECK(!requests && !g_rdx_transfer_params[0].timer);
    reset(); queue_fail=1; rdx_ble_server_conn_param_changed();
    CHECK(rdx_ble_server_conn_param_needs_service() && !requests);
    CHECK(service()); CHECK(requests==1); queue_fail=0; CHECK(service());
    CHECK(!rdx_ble_server_conn_param_needs_service() && !g_rdx_transfer_params[0].timer);
    reset(); accept_requests=0; timer_fail=1; CHECK(changed());
    CHECK(rdx_ble_server_conn_param_needs_service());
    for(int i=0;i<4;++i) { now+=100; CHECK(service()); }
    CHECK(!rdx_ble_server_conn_param_needs_service() && !g_rdx_transfer_params[0].timer);
    timer_fail=0; CHECK(changed()); CHECK(advance(6000));
    CHECK(requests==3 && !g_rdx_transfer_params[0].timer);
    reset(); accept_requests=0; CHECK(changed()); CHECK(advance(100));
    app_var.goto_poweroff_flag=1; CHECK(changed());
    CHECK(advance(10000)); CHECK(requests==1 && !g_rdx_transfer_params[0].timer);
    return 0;
}
int check_same_acl_runtime_throttle(void) {
    reset(); accept_requests=0; CHECK(changed()); CHECK(requests==1);
    CHECK(advance(100)); active=0; ++epoch; rdx_ble_server_transfer_param_cancel(); CHECK(drain());
    active=1; CHECK(changed()); CHECK(requests==1);
    CHECK(advance(1900)); CHECK(requests==2 && request_times[1]==2000);
    CHECK(advance(4000)); CHECK(requests==3 && !g_rdx_transfer_params[0].timer);
    return 0;
}
int check_wraparound(void) {
    reset(); now=0xfffffff0u; accept_requests=0; CHECK(changed());
    CHECK(advance(1999)); CHECK(requests==1); CHECK(advance(1)); CHECK(requests==2);
    CHECK(advance(4000)); CHECK(requests==3 && !g_rdx_transfer_params[0].timer);
    for(int i=1;i<requests;++i) CHECK((u32)(request_times[i]-request_times[i-1])==2000);
    return 0;
}
'''


def main():
    source = (FOLDER / 'rdx_ble_server.c').read_text(encoding='utf-8-sig')
    start = source.index('static const struct conn_update_param_t rdx_fast_conn_param')
    end = source.index('static u8 rdx_ble_server_local_name_copy(', start)
    code = STUBS + source[start:end] + TESTS
    assert 'rdx_ble_server_conn_param_changed();' in function(source, 'rdx_ble_server_phase2_rdx_attach')
    update = function(source, 'rdx_ble_server_phase0a_link_conn_params_update')
    assert 'rdx_ble_server_conn_param_changed();' in update and 'transfer_param_run(' not in update
    assert 'rdx_ble_server_transfer_param_cancel();' in function(source, 'rdx_ble_server_rdx_detach_common')
    assert 'rdx_ble_server_conn_param_changed();' in function(source, 'rdx_ble_server_phase2_hid_attach')
    assert len(re.findall(r'\bble_op_conn_param_request\s*\(', source)) == 1
    request = function(source, 'rdx_ble_server_request_connect_parameter_for_token')
    assert '&rdx_fast_conn_param' in request
    for obsolete in ('RDX_TRANSFER_IDLE_MS', 'RDX_PARAM_IDLE', 'connection_param_table',
                     'rdx_ble_server_conn_param_idle_hint', 'rdx_peripheral_power_connected_busy_mask'):
        assert obsolete not in source
    power = (FOLDER / 'rdx_peripheral_power.c').read_text(encoding='utf-8-sig')
    assert 'rdx_ble_server_conn_param_changed();' in function(power, 'rdx_peripheral_power_vdd_ble_links_changed_notify')
    assert 'rdx_ble_server_conn_param' not in function(power, 'rdx_peripheral_power_vdd_connected_changed_notify')
    assert 'rdx_ble_server_conn_param' not in function(power, 'rdx_peripheral_power_vdd_idle_hint')
    dip = (FOLDER / 'rdx_dip_switch.c').read_text(encoding='utf-8-sig')
    assert 'rdx_ble_server_conn_param_needs_service()' in function(dip, 'rdx_dip_switch_recovery_needed')
    assert 'rdx_ble_server_conn_param_service();' in function(dip, 'rdx_dip_switch_deferred_handle')
    checks = re.findall(r'int (check_\w+)\(void\)', TESTS)
    with tempfile.TemporaryDirectory(prefix='connected_params_') as work:
        path = Path(work) / 'check.c'
        path.write_text(code, encoding='utf-8')
        run_c_checks(path, checks, native=True)
    print('Connected BLE parameter policy checks passed (BLE/OS mocked).')


if __name__ == '__main__':
    main()
