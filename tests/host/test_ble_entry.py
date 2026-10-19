"""执行实际第二入口策略及服务端广播、连接适配代码。"""
import re
import hashlib
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
#define TRUE 1
#define FALSE 0
#define Q_CALLBACK 1
#define ADV_IND 0
#define ADV_CHANNEL_ALL 7
#define ADV_RSP_PACKET_MAX 31
#define RDX_BLE_PHASE0A_WRAPPER_MAX 2
#define RDX_LED_SCENE_BLE_ADV_START 1
#define RDX_LE_CONNECTION_COMPLETE_MIN_SIZE 20
#define RDX_LE_ENHANCED_CONNECTION_COMPLETE_MIN_SIZE 32
#define RDX_BLE_LINK_INVALID_INDEX 255
#define r_printf(...) ((void)0)
#define y_printf(...) ((void)0)
#define log_info(...) ((void)0)
#define printf(...) ((void)0)
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
void *memset(void*,int,unsigned long long);
static u32 now;
static int queue_pending, queue_fail, timer_fail, in_core, context_error, irq;
static u32 queue_args[512];
static int queue_read, queue_write;
static int suppressed, busy, power_calls, led_calls, opens, sdk_stops, close_calls;
static int sdk_fail, submit_fail, auto_notification, open_race, setup_fail, close_race;
static int exit_ack, exit_delays, free_calls;
typedef struct { u16 con; u8 adv, handle, freed; } wrapper_t;
static wrapper_t wrappers[2];
typedef struct { u8 connected; u16 con_handle; void *ble_hdl; } rdx_ble_link_state_t;
static rdx_ble_link_state_t links[2];
static int accepted, rejected;
static struct { void (*cb)(void*); void *arg; u32 due; int active; } timers[256];
static u16 next_timer;
static struct { int opcode; u8 *data; u32 size; } commands[64];
static int command_count;
static struct { void *rdx_ble_server_hdl; u16 adv_interval_min, adv_interval_change_timer;
                u8 adv_refresh_pending; } g_rdx_ble_server_info;
static void *g_rdx_ble_advertising_hdl, *g_rdx_ble_phase0a_disconnect_pending_hdl;
static void *g_rdx_ble_secondary_hdl;
static u8 g_rdx_ble_exiting;
static struct { u8 slot_index; } g_rdx_ble_adv_token;
static u16 g_rdx_ble_phase0a_connect_adv_timer;
static volatile u8 g_rdx_ble_dual_acl_sleep_blocked;
void rdx_ble_entry_changed(void);
void rdx_ble_entry_start(void);
void rdx_ble_entry_stop(void);
void rdx_ble_entry_topology_changed(void);
void rdx_ble_entry_request(u8,u8);
int rdx_ble_server_adv_enable(u8);
u8 rdx_ble_entry_allowed(void);
int rdx_ble_entry_radio_apply(u8,u8);
u32 rdx_ble_entry_radio_deadline(void);
static void rdx_ble_server_adv_close_event(void*,const u8*,u16);
static void rdx_ble_server_phase0a_link_connected(void*,const u8*,u16,u8);
static void accept_during_getter(void *h) {
    u8 packet[34]={62,19,1,0,0x51,0};
    wrapper_t *w=h; w->con=0x51; w->adv=w->handle=0;
    rdx_ble_server_phase0a_link_connected(h,packet,20,0);
}
void local_irq_disable(void) { ++irq; }
void local_irq_enable(void) { --irq; }
u32 sys_timer_get_ms(void) { return now; }
u8 rdx_ble_entry_suppressed(void) { return suppressed; }
u8 rdx_ble_entry_business_busy(void) { return busy; }
u8 rdx_ble_server_phase0a_connected_count(void) { return !!wrappers[0].con + !!wrappers[1].con; }
u8 rdx_ble_server_get_connected_count(void) { return rdx_ble_server_phase0a_connected_count(); }
u16 app_ble_get_hdl_con_handle(void *h) {
    if(!h || ((wrapper_t*)h)->freed) { ++context_error; return 0; }
    return ((wrapper_t*)h)->con;
}
int app_ble_adv_handle_get(void *h) {
    if(close_race) { close_race=0; accept_during_getter(h); }
    u8 handle=((wrapper_t*)h)->handle;
    return handle ? handle-1 : 255;
}
int app_ble_adv_state_get(void *h) { return ((wrapper_t*)h)->adv; }
u8 rdx_ble_server_phase0a_wrapper_index(void *h) { return h ? (wrapper_t*)h-wrappers : 255; }
void *rdx_ble_server_phase0a_idle_wrapper_get(void) {
    for(int i=0;i<2;++i) if(!wrappers[i].con) return &wrappers[i];
    return NULL;
}
int app_ble_adv_enable(void *h,u8 on) {
    wrapper_t *w=h;
    if(w->freed) ++context_error;
    if(!in_core || irq) ++context_error;
    if(on && rdx_ble_server_get_connected_count() && !g_rdx_ble_dual_acl_sleep_blocked) ++context_error;
    w->adv=on;
    if(!on) { ++sdk_stops; w->handle=0; }
    else {
        ++opens;
        w->handle=(wrappers[0].handle==1 || wrappers[1].handle==1) ? 2 : 1;
    }
    return sdk_fail ? -1 : 0;
}
int fake_ll(int argc,int opcode,u8 *data,u32 size) {
    if(!in_core || irq || argc!=3 || command_count>=64) ++context_error;
    if(rdx_ble_server_get_connected_count() && !g_rdx_ble_dual_acl_sleep_blocked) ++context_error;
    ++close_calls;
    if(submit_fail==close_calls) return -1;
    commands[command_count].opcode=opcode; commands[command_count].data=data;
    commands[command_count++].size=size;
    return 0;
}
#define ll_hci_cmd_to_task fake_ll
int app_ble_set_adv_param(void *h,u16 interval,u8 type,u8 channel) {
    if(!in_core || irq) ++context_error;
    if(open_race==1) { open_race=0; suppressed=1; rdx_ble_entry_changed(); }
    return setup_fail==1 ? -1 : 0;
}
u8 rdx_ble_server_fill_adv_data(u8 *data) { return 3; }
u8 rdx_ble_server_fill_rsp_data(u8 *data) { return 3; }
int app_ble_adv_data_set(void *h,u8 *data,u8 len) {
    if(open_race==2) { open_race=0; suppressed=1; rdx_ble_entry_changed(); }
    return setup_fail==2 ? -1 : 0;
}
int app_ble_rsp_data_set(void *h,u8 *data,u8 len) {
    if(open_race==3) { open_race=0; suppressed=1; rdx_ble_entry_changed(); }
    return setup_fail==3 ? -1 : 0;
}
int rdx_peripheral_power_vdd_fast_adv_notify(void) { ++power_calls; return 0; }
void rdx_led_ctrl_set_scene(int scene) { ++led_calls; if(auto_notification) rdx_ble_entry_changed(); }
void rdx_adv_policy_enable(void) {}
void rdx_adv_policy_disable(void) {}
int os_taskq_post_type(const char *task,int type,int n,int *msg) {
    if(task[0]!='a' || n!=3 || msg[1]!=1 || type!=Q_CALLBACK || irq) ++context_error;
    if(queue_fail) return -1;
    queue_args[queue_write++ % 512]=msg[2];
    ++queue_pending; return 0;
}
u16 sys_timeout_add_2_task(void *arg,void (*cb)(void*),u32 delay,const char *task) {
    if(!in_core || irq || task[0]!='a') ++context_error;
    if(timer_fail || next_timer==255) return 0;
    u16 id=++next_timer; timers[id].cb=cb; timers[id].arg=arg;
    timers[id].due=now+delay; timers[id].active=1; return id;
}
void sys_timeout_del(u16 id) { if(!in_core || irq) ++context_error; timers[id].active=0; }
u16 little_endian_read_16(const u8 *p,int offset) { return p[offset] | p[offset+1]<<8; }
u8 hci_subevent_le_connection_complete_get_status(const u8 *p) { return p[3]; }
u16 hci_subevent_le_connection_complete_get_connection_handle(const u8 *p) { return little_endian_read_16(p,4); }
#define hci_subevent_le_enhanced_connection_complete_get_status hci_subevent_le_connection_complete_get_status
#define hci_subevent_le_enhanced_connection_complete_get_connection_handle hci_subevent_le_connection_complete_get_connection_handle
rdx_ble_link_state_t *rdx_ble_server_phase0b_link_find(void *h,u16 c) {
    for(int i=0;i<2;++i) if(links[i].connected && links[i].ble_hdl==h && links[i].con_handle==c) return &links[i];
    return NULL;
}
void rdx_ble_server_phase0a_wrapper_connection_clear(void *h,u16 c,const char *reason) {
    if(((wrapper_t*)h)->con==c) ((wrapper_t*)h)->con=0; ++rejected;
}
u8 rdx_ble_server_phase0a_event_matches(void *h,u16 c,const char *reason) { return ((wrapper_t*)h)->con==c; }
u8 rdx_hogp_keyboard_is_ready(void) { return 1; }
void rdx_hogp_input_invalidate(void) {}
rdx_ble_link_state_t *rdx_ble_session_link_accept(void *h,u16 c) {
    rdx_ble_link_state_t *l=&links[(wrapper_t*)h-wrappers];
    l->connected=1; l->con_handle=c; l->ble_hdl=h; ++accepted; return l;
}
void rdx_ble_server_phase0a_link_state_capture(rdx_ble_link_state_t *l,const u8 *p,u8 enhanced) {}
void multi_att_clear_ccc_config(u16 c) {}
void rdx_ble_server_disconnected_adv_restart_cancel(void) {}
void rdx_peripheral_power_vdd_ble_links_changed_notify(void) {}
u8 rdx_ble_session_active_count(void) { return links[0].connected+links[1].connected; }
void rdx_ble_server_conn_tone_post(void) {}
void rdx_ble_server_phase0a_connect_adv_restart_schedule(void) { g_rdx_ble_phase0a_connect_adv_timer=1; }
void os_time_dly(int ticks) {
    if(!in_core || irq || ++exit_delays>220) ++context_error;
    now+=ticks*10;
    if(exit_ack) {
        u8 packet[]={14,4,1,0x3c,0x20,0};
        rdx_ble_server_adv_close_event(&wrappers[0],packet,5);
        rdx_ble_server_adv_close_event(&wrappers[1],packet,5);
    }
}
void rdx_ble_server_transfer_param_cancel(void) {}
void rdx_ble_server_syn_data_timers_cancel(void) {}
void rdx_ble_server_phase0a_connect_adv_restart_cancel(void) { g_rdx_ble_phase0a_connect_adv_timer=0; }
void rdx_ble_server_rdx_send_pending_reset(void) {}
void rdx_session_control_reset(void) {}
void rdx_ble_session_transport_deinit(void) {}
int rdx_ble_server_rdx_runtime_try_rearm(void) { return 1; }
int rdx_app_rdx_rebind_is_idle(void) { return 1; }
void rdx_hogp_key_action_deinit(void) {}
void rdx_hogp_deinit(void) {}
void rdx_ble_session_reset(void) { memset(links,0,sizeof(links)); }
void app_ble_disconnect(void *h) { ((wrapper_t*)h)->con=0; }
void app_ble_hdl_free(void *h) {
    wrapper_t *w=h;
    if(w->freed || w->adv || w->con) ++context_error;
    w->freed=1; ++free_calls;
}
'''

CHECKS = r'''
static void reset(void) {
    memset(&entry,0,sizeof(entry)); memset(&g_rdx_adv_close,0,sizeof(g_rdx_adv_close));
    memset(wrappers,0,sizeof(wrappers)); memset(links,0,sizeof(links));
    memset(timers,0,sizeof(timers)); memset(commands,0,sizeof(commands));
    memset(&g_rdx_ble_server_info,0,sizeof(g_rdx_ble_server_info));
    g_rdx_ble_server_info.rdx_ble_server_hdl=&wrappers[0];
    g_rdx_ble_secondary_hdl=&wrappers[1];
    g_rdx_ble_server_info.adv_interval_min=160;
    g_rdx_ble_advertising_hdl=g_rdx_ble_phase0a_disconnect_pending_hdl=NULL;
    g_rdx_ble_phase0a_connect_adv_timer=g_rdx_ble_dual_acl_sleep_blocked=0;
    now=next_timer=queue_pending=queue_fail=timer_fail=in_core=context_error=irq=0;
    suppressed=busy=power_calls=led_calls=opens=sdk_stops=close_calls=0;
    sdk_fail=submit_fail=auto_notification=open_race=command_count=accepted=rejected=0;
    setup_fail=close_race=exit_ack=exit_delays=free_calls=queue_read=queue_write=0;
    g_rdx_ble_exiting=0;
    in_core=1; rdx_ble_entry_start(); in_core=0;
}
static int drain(void) {
    int budget=100; in_core=1;
    while(queue_pending && --budget) {
        --queue_pending; entry_run((void*)queue_args[queue_read++ % 512]);
    }
    in_core=0; return budget && !context_error && !irq;
}
static int advance(u32 dt) {
    u32 end=now+dt; int budget=100;
    while(--budget) {
        u16 id=0;
        for(u16 i=1;i<=next_timer;++i) if(timers[i].active && (s32)(timers[i].due-end)<=0 &&
            (!id || (s32)(timers[i].due-timers[id].due)<0)) id=i;
        if(!id) break;
        now=timers[id].due; timers[id].active=0;
        in_core=1; timers[id].cb(timers[id].arg); in_core=0;
        if(!drain()) return 0;
    }
    now=end; return budget && !context_error;
}
static int single(void) {
    wrappers[0].con=0x50;
    links[0].connected=1; links[0].con_handle=0x50; links[0].ble_hdl=&wrappers[0];
    rdx_ble_entry_topology_changed(); rdx_ble_entry_request(1,0); return drain();
}
static int reply(u8 status) {
    u8 packet[]={14,4,1,0x3c,0x20,status};
    rdx_ble_server_adv_close_event(&wrappers[0],packet,5);
    rdx_ble_server_adv_close_event(&wrappers[1],packet,5);
    return drain();
}
static int closed(void) { return reply(0) && reply(0x42); }
int check_window_and_business_cycle(void) {
    reset(); CHECK(single()); CHECK(opens==1 && g_rdx_ble_dual_acl_sleep_blocked);
    CHECK(!power_calls && !led_calls);
    u32 deadline=entry.deadline;
    CHECK(advance(110000)); busy=1; rdx_ble_entry_activity(); CHECK(drain());
    CHECK(entry.deadline==deadline);
    CHECK(advance(10000)); CHECK(g_rdx_adv_close.waiting && g_rdx_ble_dual_acl_sleep_blocked);
    CHECK(commands[0].opcode==0x2039 && commands[0].data[0]==0 && commands[0].data[1]==1);
    CHECK(commands[0].data[2]==0 && commands[1].opcode==0x203c && commands[1].data[0]==0);
    CHECK(wrappers[1].handle==1 && wrappers[1].adv); /* 不能提前释放 SDK 句柄。 */
    CHECK(closed()); CHECK(!g_rdx_ble_dual_acl_sleep_blocked && !g_rdx_ble_advertising_hdl);
    for(int i=0;i<6;++i) { rdx_ble_entry_activity(); CHECK(drain()); }
    CHECK(opens==1); /* 同一持续业务不能重开已到期的窗口。 */
    busy=0; rdx_ble_entry_changed(); CHECK(drain()); CHECK(!entry.active);
    rdx_ble_entry_activity(); CHECK(drain()); CHECK(opens==2 && entry.window);
    CHECK(entry.deadline==240000 && !power_calls);
    CHECK(advance(120000)); CHECK(closed()); CHECK(!entry.window);
    for(int i=0;i<20;++i) { rdx_ble_entry_changed(); rdx_ble_entry_request(1,0); CHECK(drain()); }
    CHECK(opens==2 && !g_rdx_ble_dual_acl_sleep_blocked && !entry.timer);
    return 0;
}
int check_hid_key_without_pa4_and_no_renewal(void) {
    reset(); CHECK(single()); CHECK(advance(120000)); CHECK(closed());
    rdx_ble_entry_key(1); CHECK(drain()); CHECK(opens==2 && !power_calls && entry.active);
    CHECK(advance(120000)); CHECK(closed());
    rdx_ble_entry_key(1); CHECK(drain()); CHECK(opens==2);
    rdx_ble_entry_key(0); CHECK(drain()); CHECK(!entry.active);
    rdx_ble_entry_key(1); CHECK(drain()); CHECK(opens==3 && !power_calls);
    return 0;
}
int check_topology_and_inflight_connection(void) {
    reset(); CHECK(single()); CHECK(advance(120000));
    u8 packet[34]={62,19,1,0,0x51,0};
    wrappers[1].con=0x51; wrappers[1].adv=0; wrappers[1].handle=0;
    rdx_ble_server_phase0a_link_connected(&wrappers[1],packet,20,0);
    CHECK(accepted==1 && !rejected && links[1].connected);
    CHECK(drain()); CHECK(g_rdx_ble_dual_acl_sleep_blocked && !entry.window);
    CHECK(closed()); CHECK(g_rdx_ble_dual_acl_sleep_blocked && !entry.window);
    rdx_ble_server_phase0a_link_connected(&wrappers[1],packet,20,0);
    CHECK(accepted==1 && !rejected); /* 迟到的重复事件不能清除有效 ACL。 */
    wrappers[0].con=0; links[0].connected=0;
    g_rdx_ble_phase0a_connect_adv_timer=0;
    rdx_ble_entry_topology_changed(); rdx_ble_entry_request(1,0); CHECK(drain());
    CHECK(entry.window && opens==2 && g_rdx_ble_advertising_hdl==&wrappers[0]);
    CHECK(advance(120000)); CHECK(closed()); CHECK(!g_rdx_ble_dual_acl_sleep_blocked);
    wrappers[1].con=0; links[1].connected=0;
    rdx_ble_entry_topology_changed(); rdx_ble_entry_request(1,0); CHECK(drain());
    CHECK(opens==3 && !entry.window && !g_rdx_ble_dual_acl_sleep_blocked);
    CHECK(advance(1200000)); CHECK(!entry.timer && opens==3);
    return 0;
}
int check_failure_is_not_cached_off_success(void) {
    reset(); CHECK(single()); CHECK(advance(120000)); sdk_fail=1;
    CHECK(reply(0)); CHECK(g_rdx_adv_close.fault && !wrappers[1].adv);
    sdk_fail=0;
    for(int i=0;i<6;++i) { rdx_ble_entry_changed(); CHECK(drain()); }
    CHECK(g_rdx_ble_dual_acl_sleep_blocked && g_rdx_adv_close.fault && sdk_stops==1);
    reset(); CHECK(single()); CHECK(advance(120000)); CHECK(reply(12));
    CHECK(g_rdx_ble_dual_acl_sleep_blocked);
    CHECK(g_rdx_adv_close.fault);
    CHECK(!sdk_stops);
    reset(); CHECK(single()); CHECK(advance(120000)); CHECK(advance(2000));
    CHECK(g_rdx_adv_close.fault && g_rdx_ble_dual_acl_sleep_blocked && !entry.timer);
    CHECK(closed()); rdx_ble_entry_activity(); CHECK(drain());
    CHECK(opens==1 && g_rdx_adv_close.fault); /* 迟到完成事件不能恢复已锁定的故障事务。 */
    reset(); CHECK(single()); submit_fail=1; CHECK(advance(120000)); CHECK(advance(2000));
    CHECK(g_rdx_adv_close.fault && g_rdx_ble_dual_acl_sleep_blocked && !entry.timer);
    reset(); CHECK(single()); submit_fail=2; CHECK(advance(120000));
    CHECK(g_rdx_adv_close.fault && g_rdx_ble_dual_acl_sleep_blocked);
    reset(); CHECK(single()); CHECK(advance(120000)); submit_fail=3; CHECK(reply(0));
    CHECK(g_rdx_adv_close.fault && g_rdx_ble_dual_acl_sleep_blocked && !wrappers[1].adv);
    reset(); sdk_fail=1; rdx_ble_entry_request(1,0); CHECK(drain());
    CHECK(g_rdx_adv_close.fault && g_rdx_ble_dual_acl_sleep_blocked); /* 零 ACL 时也必须保留故障保护。 */
    return 0;
}
int check_completion_filter_and_usb_quiesce(void) {
    reset(); CHECK(single()); CHECK(advance(120000));
    u8 packet[]={14,4,1,0x3c,0x20,0};
    rdx_ble_server_adv_close_event(&wrappers[1],packet,5);
    rdx_ble_server_adv_close_event(&wrappers[0],packet,4);
    packet[3]=0x39; rdx_ble_server_adv_close_event(&wrappers[0],packet,5);
    CHECK(!g_rdx_adv_close.complete && !queue_pending && !sdk_stops);
    CHECK(closed()); CHECK(!g_rdx_ble_dual_acl_sleep_blocked);
    reset(); rdx_ble_entry_request(1,0); CHECK(drain());
    CHECK(!rdx_ble_server_usb_quiesce()); CHECK(drain());
    CHECK(!rdx_ble_server_usb_quiesce()); CHECK(closed());
    CHECK(rdx_ble_server_usb_quiesce()); CHECK(drain());
    return 0;
}
int check_refresh_suppression_and_stale_callbacks(void) {
    reset(); CHECK(single()); CHECK(advance(10000));
    u32 deadline=entry.deadline;
    u16 old_timer=entry.timer; void *old_arg=timers[old_timer].arg;
    rdx_ble_entry_request(1,1); CHECK(drain()); CHECK(g_rdx_adv_close.waiting);
    CHECK(closed()); CHECK(opens==2 && entry.deadline==deadline);
    in_core=1; entry_timeout(old_arg); in_core=0; CHECK(!queue_pending);
    CHECK(advance(110000)); CHECK(closed());
    rdx_ble_entry_request(1,1); CHECK(drain()); CHECK(opens==2);
    suppressed=1; rdx_ble_entry_key(1); CHECK(drain()); CHECK(opens==2);
    rdx_ble_entry_key(0); CHECK(drain()); suppressed=0;
    rdx_ble_entry_changed(); CHECK(drain()); CHECK(opens==2);
    rdx_ble_entry_key(1); CHECK(drain()); CHECK(opens==3);
    suppressed=1; rdx_ble_entry_changed(); CHECK(drain()); CHECK(closed());
    CHECK(!g_rdx_ble_advertising_hdl);
    reset(); auto_notification=1; rdx_ble_entry_request(1,1); CHECK(drain());
    CHECK(opens==1 && !g_rdx_adv_close.waiting); /* 广播和灯效通知不算新的刷新请求。 */
    return 0;
}
int check_queue_timer_failure_and_rollover(void) {
    reset(); queue_fail=1; CHECK(single()); CHECK(!opens && rdx_ble_entry_needs_service());
    queue_fail=0; rdx_ble_entry_service(); CHECK(drain()); CHECK(opens==1);
    reset(); timer_fail=1; CHECK(single()); CHECK(g_rdx_adv_close.waiting);
    CHECK(g_rdx_ble_dual_acl_sleep_blocked && !entry.window);
    for(int i=0;i<8;++i) { rdx_ble_entry_service(); CHECK(drain()); }
    CHECK(!rdx_ble_entry_needs_service()); timer_fail=0; CHECK(closed());
    CHECK(!g_rdx_ble_dual_acl_sleep_blocked);
    reset(); now=0xffff0000u; CHECK(single());
    CHECK(advance(119999)); CHECK(!g_rdx_adv_close.waiting);
    CHECK(advance(1)); CHECK(closed()); CHECK(!entry.window);
    reset(); CHECK(single()); now=119999; rdx_ble_entry_activity();
    now=120001; CHECK(drain()); CHECK(g_rdx_adv_close.waiting && !entry.window);
    CHECK(closed()); CHECK(opens==1); /* 窗口内发生但延迟处理的活动不能续期。 */
    return 0;
}
int check_getter_connection_and_cancel_before_enable(void) {
    reset(); CHECK(single()); close_race=1; CHECK(advance(120000));
    CHECK(accepted==1 && !rejected && links[1].connected);
    CHECK(!g_rdx_adv_close.fault && !g_rdx_adv_close.waiting && !close_calls);
    CHECK(g_rdx_ble_dual_acl_sleep_blocked && !g_rdx_ble_advertising_hdl);
    for(int stage=1;stage<=3;++stage) {
        reset(); open_race=stage; CHECK(single());
        CHECK(!opens && !g_rdx_adv_close.fault && !g_rdx_ble_advertising_hdl);
        CHECK(!g_rdx_ble_dual_acl_sleep_blocked);
        suppressed=0; rdx_ble_entry_changed(); CHECK(drain());
        CHECK(opens==1 && g_rdx_ble_dual_acl_sleep_blocked);
        reset(); setup_fail=stage; CHECK(single()); CHECK(advance(1000));
        CHECK(!opens && !g_rdx_adv_close.fault && !g_rdx_ble_advertising_hdl);
        setup_fail=0; rdx_ble_entry_changed(); CHECK(drain()); CHECK(opens==1);
    }
    reset(); sdk_fail=1; CHECK(single()); CHECK(g_rdx_adv_close.fault);
    CHECK(g_rdx_ble_dual_acl_sleep_blocked); /* 已提交开启但结果不确定时仍须保留保护。 */
    return 0;
}
static void restart(void) {
    wrappers[0].freed=wrappers[1].freed=0;
    g_rdx_ble_server_info.rdx_ble_server_hdl=&wrappers[0];
    g_rdx_ble_secondary_hdl=&wrappers[1]; g_rdx_ble_exiting=0;
    in_core=1; rdx_ble_entry_start(); in_core=0;
}
int check_lifecycle_clean_exit_and_old_work(void) {
    reset(); CHECK(single());
    void *old_arg=timers[entry.timer].arg;
    rdx_ble_entry_changed(); u32 old_epoch=entry.epoch;
    exit_ack=1; in_core=1; rdx_ble_server_exit(); in_core=0;
    CHECK(!entry.running && !entry.timer && !entry.requested);
    CHECK(free_calls==2 && !g_rdx_adv_close.hdl && !g_rdx_adv_close.fault);
    CHECK(!g_rdx_ble_dual_acl_sleep_blocked && exit_delays==2);
    rdx_ble_entry_request(1,1); rdx_ble_entry_key(1); rdx_ble_entry_topology_changed();
    CHECK(!rdx_ble_entry_needs_service() && !rdx_ble_entry_allowed());
    CHECK(drain()); CHECK(opens==1 && !context_error);
    restart(); rdx_ble_entry_request(1,0); CHECK(entry.queued);
    in_core=1; entry_run((void*)old_epoch); entry_timeout(old_arg); in_core=0;
    CHECK(entry.queued); CHECK(drain()); CHECK(opens==2 && !g_rdx_adv_close.fault);
    CHECK(advance(120000)); CHECK(opens==2); /* 旧生命周期的期限不能关闭新生命周期的入口。 */
    in_core=1; rdx_ble_server_exit(); rdx_ble_server_exit(); in_core=0;
    CHECK(free_calls==4 && !context_error); CHECK(drain());
    reset(); rdx_ble_entry_request(1,0); /* 首次开启任务尚未执行就退出。 */
    in_core=1; rdx_ble_server_exit(); in_core=0;
    CHECK(drain()); CHECK(!opens && !context_error);
    reset(); rdx_ble_entry_request(1,0);
    in_core=1; rdx_ble_server_exit(); in_core=0;
    restart(); rdx_ble_entry_request(1,0); CHECK(queue_pending==2);
    CHECK(drain()); CHECK(opens==1 && !context_error);
    return 0;
}
int check_lifecycle_pending_and_faulted_exit(void) {
    for(int phase=0;phase<2;++phase) {
        reset(); CHECK(single()); CHECK(advance(120000));
        if(phase) CHECK(reply(0));
        exit_ack=1; in_core=1; rdx_ble_server_exit(); in_core=0;
        CHECK(!g_rdx_adv_close.fault && !g_rdx_adv_close.waiting && free_calls==2);
        CHECK(drain()); restart(); CHECK(single()); CHECK(opens==2);
    }
    reset(); CHECK(single()); CHECK(advance(120000));
    u8 *buffer=commands[0].data; u8 handle=commands[1].data[0];
    in_core=1; rdx_ble_server_exit(); in_core=0;
    CHECK(g_rdx_adv_close.fault && !g_rdx_adv_close.hdl && !g_rdx_adv_close.waiting);
    CHECK(free_calls==2 && exit_delays<=100 && g_rdx_ble_dual_acl_sleep_blocked);
    CHECK(drain()); restart(); CHECK(reply(0)); CHECK(single()); CHECK(advance(2000));
    CHECK(opens==1 && g_rdx_adv_close.fault && g_rdx_ble_dual_acl_sleep_blocked);
    CHECK(buffer==commands[0].data && buffer[2]==handle && !context_error);
    return 0;
}
'''


def main():
    # 已审阅的 SDK ABI：公共句柄从 0 开始，0xff 表示不存在；命令保留缓冲引用；
    # 0x203c 返回 Command Complete，其 size 不包含事件码字节。
    # SDK 升级必须重新审阅，不能直接沿用模拟结果。
    for name, expected in {
        'btstack.a': '03d1626fac16289ab5c3011fbd154748f562786cec6fc336fd367b30c1fbc5c5',
        'btctrler.a': '6ecbbd876ea08e6d260fbcd1c8c7b8cd214bfde70edf1145e3f7d02eccfabc50',
    }.items():
        actual = hashlib.sha256((ROOT / 'SDK/cpu/br28/liba' / name).read_bytes()).hexdigest()
        assert actual == expected, f'{name} changed: re-audit the BLE entry controller boundary'
    server = (FOLDER / 'rdx_ble_server.c').read_text(encoding='utf-8-sig')
    policy = (FOLDER / 'rdx_ble_entry.c').read_text(encoding='utf-8-sig')
    policy = re.sub(r'^#include[^\n]*\n', '', policy, flags=re.M)
    state = re.search(r'static struct \{\n    void \*hdl;.*?#define RDX_HCI_REMOVE_ADV_SET[^\n]*',
                      server, re.S)[0]
    names = ['rdx_ble_server_dual_acl_sleep_policy_update', 'rdx_ble_server_adv_fault',
             'rdx_ble_server_adv_interval_change_timer_stop',
             'rdx_ble_server_adv_interval_change_timer_start',
             'rdx_ble_server_adv_enable_on_hdl', 'rdx_ble_entry_radio_deadline',
             'rdx_ble_server_adv_close_begin', 'rdx_ble_server_adv_close_event',
             'rdx_ble_entry_radio_apply', 'rdx_ble_server_phase0a_link_connected',
             'rdx_ble_server_entry_stop', 'rdx_ble_server_exit',
             'rdx_ble_server_adv_enable', 'rdx_ble_server_usb_quiesce']
    source = STUBS + state + '\n' + policy + '\n'.join(function(server, n) for n in names) + CHECKS
    with tempfile.TemporaryDirectory(prefix='ble-entry-') as directory:
        path = Path(directory) / 'entry.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (check_\w+)\(void\)', CHECKS), native=True)
    # 实际调用方必须发布事件，不得绕过协调器独立操作广播。
    assert 'rdx_ble_entry_request(1, 1)' in function(server, 'rdx_ble_server_adv_data_changed')
    assert 'rdx_ble_entry_request(enable, 0)' in function(server, 'rdx_ble_server_adv_enable')
    assert 'app_ble_adv_enable' not in function(server, 'rdx_ble_server_adv_interval_change_timer_cb')
    init = function(server, 'rdx_ble_server_init')
    assert init.index('rdx_ble_session_transport_init') < init.index('rdx_ble_entry_start()')
    handler = function(server, 'rdx_ble_server_phase0a_packet_handler')
    assert 'g_rdx_ble_exiting' in handler and 'HCI_EVENT_COMMAND_COMPLETE' in handler
    print('BLE second-entry policy, controller ABI pins, completion, races and lifecycle checks passed.')


if __name__ == '__main__':
    main()
