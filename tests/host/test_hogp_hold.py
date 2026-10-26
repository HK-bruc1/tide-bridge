"""Exercise production HOGP scan/gesture/FIFO/action/session code in one harness.

Only OS scheduling, business admission and ATT are mocked; PC receipt requires
on-device regression. Keep checks focused on externally meaningful behavior.
"""
import re
import tempfile
from pathlib import Path
from host_c_test_lib import ROOT, function, run_c_checks

BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'

def source(name):
    return (BASE / name).read_text(encoding='utf-8')

def no_includes(text):
    return re.sub(r'^#include[^\n]*', '', text, flags=re.M)

STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
void rdx_ble_entry_key(unsigned char down) {}
typedef int bool;
typedef unsigned long long size_t;
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
int memcmp(const void *,const void *,size_t);
int strcmp(const char *,const char *);
#define true 1
#define false 0
#define NULL ((void *)0)
#define NO_KEY 255
#define KEY_IO_NUM0 65
#define KEY_IO_NUM3 68
#define KEY_IO_NUM4 69
#define KEY_DRIVER_TYPE_IO 0
#define KEY_DRIVER_TYPE_CTMU_TOUCH 2
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define TCFG_RDX_HOGP_ENABLE 1
#define APP_BLE_NO_ERROR 0
#define MSG_FROM_KEY 1
#define TCFG_MAX_HOLD_SEC ((KEY_ACTION_HOLD_8SEC << 8) | 8)
static int irq_depth, in_irq, context_errors;
#define local_irq_disable() (++irq_depth)
#define local_irq_enable() (--irq_depth)
#define cpu_irq_disabled() (irq_depth != 0)
#define cpu_in_irq() in_irq
#define y_printf(...) ((void)0)
#define jiffies_to_msecs(x) (x)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
u32 jiffies;
u32 sys_timer_get_ms(void) { return jiffies; }
int jiffies_offset_to_msec(u32 a,u32 b) { return b-a; }
static int timer_live, timer_adds, timer_dels, timer_fail;
u16 sys_timer_add(void *p, void (*f)(void *), u32 ms) {
    ++timer_adds; if(timer_fail) return 0; timer_live=1; return 1;
}
void sys_timer_del(u16 id) { ++timer_dels; timer_live=0; }
int offline, key5, route, ready, fail_down, fail_up, reports, attempts;
u8 report_bytes[512][8];
int offline_event;
static int feedback_clicks;
static int business_mode=1, local_events, local_cancels, cancel_fail;
static int local_actions[512];
static u32 local_ids[512];
static u8 local_usages[512];
static u32 cancelled_ids[32];
static void (*local_callback_hook)(void);
static int dut_ready=1, dut_checks;
static void (*dut_callback_hook)(void);
void rdx_app_key_click(u8 value,u8 action) { if(action==0) ++feedback_clicks; }
int rdx_app_key_msg_handler(int *msg);
int key5_events[32];
int app_send_message_from(int a,int b,int *c);
u8 rdx_app_hogp_input_route(void) { return route; }
u8 rdx_app_local_key_mode(void) { return business_mode; }
'''

TRANSPORT = r'''
static u32 s_hid_epoch=1;
static void *s_hogp_app_ble_hdl;
#define HID_INPUT_REPORT_VALUE_HANDLE 0x1c
#define ATT_OP_NOTIFY 1
u8 rdx_hogp_keyboard_is_ready(void) { return ready; }
void rdx_hogp_current_report_set(const u8 *p,u8 n) {}
static void hogp_tx_execute(u32 ticket);
static const char *task_name="btstack";
static int queue_fail, queue_count, queued_ticket, invalidate_on_send;
static int app_queued, app_posts, app_post_fail;
static void input_event(void *priv);
#define Q_CALLBACK 1
const char *os_current_task(void) { return task_name; }
int os_taskq_post_type(const char *task,int type,int n,int *msg) {
    if (!strcmp(task,"app_core")) {
        if(type!=Q_CALLBACK || n!=3 || msg[1]!=1 || msg[0]!=(int)input_event) ++context_errors;
        ++app_posts;
        if(app_post_fail || app_queued) return -1;
        app_queued=1; return 0;
    }
    if (irq_depth || in_irq || strcmp(task,"btstack") || type!=Q_CALLBACK ||
        n!=3 || msg[1]!=1 || msg[0]!=(int)hogp_tx_execute) ++context_errors;
    if (queue_fail || queue_count) return -1;
    queued_ticket=msg[2]; queue_count=1; return 0;
}
static void run_app(void) {
    if (!app_queued) return;
    app_queued=0;
    const char *saved=task_name; task_name="app_core";
    input_event(NULL); task_name=saved;
}
static void run_btstack(void) {
    if (!queue_count) return;
    int ticket=queued_ticket; queue_count=0;
    const char *saved=task_name; task_name="btstack";
    hogp_tx_execute(ticket); task_name=saved;
}
int app_ble_att_send_data(void *hdl,u16 att,u8 *b,u16 n,int op) {
    if (irq_depth || in_irq || strcmp(task_name,"btstack")) {
        ++context_errors; return -99;
    }
    if (invalidate_on_send) { invalidate_on_send=0; rdx_hogp_input_invalidate(); }
    int down=0; ++attempts;
    for (int i=0;i<n;i++) down|=b[i];
    if (down?fail_down:fail_up) return -1;
    memcpy(report_bytes[reports++],b,8); return 0;
}
'''

SESSION_TESTS = r'''
static rdx_ble_link_state_t *rdx_link, *hid_link;
static void session_boot(int split) {
    input_initialized=input_pending=input_posted=input_servicing=0;
    input_timer=0; timer_live=timer_adds=timer_dels=timer_fail=0;
    app_queued=app_posts=app_post_fail=0;
    task_name="btstack"; irq_depth=in_irq=context_errors=0;
    queue_fail=queue_count=invalidate_on_send=0; hogp_tx.state=HOGP_TX_IDLE;
    s_rdx_runtime_consumed_this_boot=0;
    rdx_ble_session_transport_init((void *)1,(void *)2);
    rdx_link=rdx_ble_session_link_accept((void *)1,10);
    hid_link=split?rdx_ble_session_link_accept((void *)2,11):rdx_link;
    rdx_ble_session_claim_rdx(rdx_link,rdx_link->slot_generation);
    rdx_ble_session_claim_hid(hid_link,hid_link->slot_generation);
    s_hogp_app_ble_hdl=hid_link->ble_hdl;
    ++s_hid_epoch; ready=1; fail_up=fail_down=reports=attempts=0;
    rdx_hogp_key_action_init();
    rdx_hogp_key_action_keymap_t map={0}; map.version=1; map.key_count=5;
    map.keys[0].usages[0]=4;
    map.keys[4].usages[0]=0x6c; /* KEY5 对应 F17 */
    rdx_hogp_key_action_keymap_apply(&map);
    rdx_hogp_key_action_service(); reports=0; cleanup_pending=0;
}
static int detach_rdx(void) {
    if (!rdx_ble_session_rdx_runtime_begin_quiesce(rdx_link)) return 0;
    return rdx_ble_session_release_rdx(rdx_link,rdx_link->slot_generation);
}
int test_queue_failure_preserves_release(void) {
    session_boot(0); task_name="app_core"; queue_fail=1;
    CHECK(rdx_hogp_key_action_press(0,input_epoch)!=0 && !reports && !queue_count);
    CHECK(held_key==0xff && !down_pending);
    queue_fail=0; CHECK(!rdx_hogp_key_action_press(0,input_epoch)); run_btstack();
    rdx_hogp_key_action_service(); CHECK(reports==1);
    queue_fail=1; rdx_hogp_key_action_release(0);
    CHECK(release_pending && !send_pending);
    queue_fail=0; rdx_hogp_key_action_service(); run_btstack();
    rdx_hogp_key_action_service();
    CHECK(!release_pending && reports==2 && !context_errors);
    return 0;
}
int test_queued_owner_reuse_and_invalid_epoch(void) {
    session_boot(0); task_name="app_core";
    CHECK(!rdx_hogp_key_action_press(0,input_epoch));
    rdx_ble_session_link_release((void *)1,10);
    hid_link=rdx_ble_session_link_accept((void *)1,10);
    CHECK(rdx_ble_session_claim_hid(hid_link,hid_link->slot_generation)==RDX_BLE_CLAIM_OK);
    run_btstack(); CHECK(!reports); /* Same wrapper and ACL number, new owner. */
    rdx_hogp_key_action_service(); run_btstack(); rdx_hogp_key_action_service();
    CHECK(reports==1 && !report_bytes[0][2]);
    rdx_hogp_token_t token; CHECK(rdx_hogp_token_capture(&token));
    rdx_hogp_keyboard_report_t down={0}; down.usages[0]=4;
    CHECK(rdx_hogp_report_send_for_input(&down,&token,input_epoch+1)==RDX_HOGP_SEND_PENDING);
    run_btstack();
    CHECK(rdx_hogp_report_send_for_input(&down,&token,input_epoch+1)!=0 && reports==1);
    irq_depth=1;
    CHECK(rdx_hogp_report_send_for_token(&down,&token)!=0 && !queue_count);
    irq_depth=0; in_irq=1;
    CHECK(rdx_hogp_report_send_for_token(&down,&token)!=0 && !queue_count);
    in_irq=0; task_name="btstack"; irq_depth=1;
    CHECK(rdx_hogp_report_send_for_token(&down,&token)!=0);
    irq_depth=0; CHECK(!context_errors);
    return 0;
}
int test_pending_rdx_release_and_cancel(void) {
    for (int split=0;split<2;split++) {
        session_boot(split); task_name="app_core";
        CHECK(!rdx_hogp_key_action_press(0,input_epoch)); CHECK(detach_rdx());
        run_btstack(); rdx_hogp_key_action_service();
        CHECK(reports==1 && held_key==0);
        rdx_hogp_key_action_cancel(); CHECK(release_pending && queue_count);
        run_btstack(); rdx_hogp_key_action_service();
        CHECK(reports==2 && !release_pending && !context_errors);
    }
    return 0;
}
'''

SCAN_TESTS = r'''int rdx_app_key_msg_handler(int *msg) { ++offline; offline_event=((struct key_event *)msg)->event; return 1; }
void rdx_app_local_key_down(const rdx_hogp_input_cycle_t *cycle) {
    struct key_event key={.type=KEY_DRIVER_TYPE_IO,.value=cycle->physical_key,.event=KEY_ACTION_MAX};
    rdx_app_local_key_gesture(&key,cycle);
}
int rdx_app_local_key_dut_ready(const rdx_hogp_input_cycle_t *owner) {
    ++dut_checks;
    if(dut_callback_hook) dut_callback_hook();
    return dut_ready;
}
void rdx_app_local_key_gesture(const struct key_event *key, const rdx_hogp_input_cycle_t *cycle) {
    if (in_irq || irq_depth) ++context_errors;
    if (local_events < 512) {
        local_actions[local_events]=key->event;
        local_ids[local_events]=cycle->id;
        local_usages[local_events]=cycle->usage;
    }
    ++local_events;
    if(key->value==KEY_IO_NUM4) {
        if(key5<32) key5_events[key5]=key->event;
        ++key5;
    } else {
        ++offline; offline_event=key->event;
    }
    if (local_callback_hook) local_callback_hook();
}
u8 rdx_app_local_key_cancel(const rdx_hogp_input_cycle_t *cycle) {
    if (in_irq || irq_depth) ++context_errors;
    if (local_cancels<32) cancelled_ids[local_cancels]=cycle->id;
    ++local_cancels;
    if (cancel_fail) return 0;
    if(cycle->physical_key==KEY_IO_NUM4) {
        if(key5<32) key5_events[key5]=KEY_ACTION_UP;
        ++key5;
    }
    return 1;
}
int app_send_message_from(int a,int b,int *c) {
    if(key5<32) key5_events[key5]=((struct key_event *)c)->event;
    ++key5; return 0;
}

u8 raw_key, g_is_key_active;
int filtered;
u8 read_key(void) { return raw_key; }
bool key_test_scan_filter(u8 type,u8 value,u8 previous) {
    return filtered;
}
void key_stable_sample(u8 type,u8 previous,u8 current,u8 consumed) {
    rdx_hogp_input_scan(type,previous,current,consumed);
}
void key_down_event_handler(u8 value) { }
static void key_driver_scan(void *ops);
static struct key_driver_para scan;
static struct key_driver_ops ops = {
    .key_type=KEY_DRIVER_TYPE_IO, .param=&scan, .get_value=read_key,
    .filter_time=1, .long_time=4, .hold_time=7, .click_delay_time=3,
};
static void sample(u8 value) {
    raw_key=value;
    for (int i=0;i<3;i++) { jiffies+=10; key_driver_scan(&ops); }
}
static void tick(void) { input_service(NULL); }
static void settle(void) { sample(NO_KEY); tick(); sample(NO_KEY); tick(); }
static void boot(int online) {
    input_timer=0; input_head=input_count=0;
    input_epoch=1; scan_seq=scan_epoch=scan_baseline=0;
    overflow_count=reported_overflows=0; input_high_water=0;
    stable_key=NO_KEY; published_route=admission=0; cleanup_pending=1;
    local_head=local_count=local_mode=local_admission=local_down=local_owner_pending=0;
    local_cleanup_pending=1; local_epoch=1; cycle_sequence=local_baseline=local_overflows=0;
    memset(&scan_cycle,0,sizeof(scan_cycle)); memset(&local_owner,0,sizeof(local_owner));
    memset(&local_start,0,sizeof(local_start)); memset(&local_f19_owner,0,sizeof(local_f19_owner));
    memset(&local_dut,0,sizeof(local_dut)); memset(&local_dut_owner,0,sizeof(local_dut_owner));
    dut_ready=1; dut_checks=0; dut_callback_hook=NULL;
    local_events=local_cancels=cancel_fail=0; business_mode=RDX_LOCAL_KEY_NORMAL;
    local_callback_hook=NULL; memset(cancelled_ids,0,sizeof(cancelled_ids));
    memset(&published_token,0,sizeof(published_token));
    memset(&scan,0,sizeof(scan)); scan.last_key=scan.filter_value=NO_KEY;
    filtered=scan_filtered=0; key_event_reset();
    session_boot(0);
    route=online?2:1; ready=online;
    fail_up=fail_down=reports=attempts=offline=key5=0;
    rdx_hogp_input_init(); tick(); settle();
    reports=attempts=0;
}
static void reconnect(void) {
    rdx_hogp_input_invalidate(); ready=1; route=2; ++s_hid_epoch; tick();
}
int test_plain_key_query_tracks_active_map(void) {
    boot(1);
    CHECK(rdx_hogp_key_action_is_plain_key(4,0x6c));
    CHECK(!rdx_hogp_key_action_is_plain_key(5,0x6c));
    CHECK(!rdx_hogp_key_action_is_plain_key(4,0));
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    map.keys[4].usages[0]=0x6d;
    CHECK(!rdx_hogp_key_action_keymap_apply(&map));
    CHECK(!rdx_hogp_key_action_is_plain_key(4,0x6c));
    map.keys[4].usages[0]=0;
    map.keys[4].usages[5]=0x6c;
    CHECK(!rdx_hogp_key_action_keymap_apply(&map));
    CHECK(rdx_hogp_key_action_is_plain_key(4,0x6c));
    rdx_hogp_key_action_deinit();
    CHECK(!rdx_hogp_key_action_is_plain_key(4,0x6c));
    return 0;
}
int test_short_long_and_fast_cycles(void) {
    boot(1); CHECK(admission);
    sample(KEY_IO_NUM0); tick(); CHECK(reports==1 && report_bytes[0][2]==4);
    for(int i=0;i<150;i++) { sample(KEY_IO_NUM0); tick(); }
    CHECK(reports==1 && local_events>0 && local_actions[0]==KEY_ACTION_LONG);
    sample(NO_KEY); tick(); CHECK(reports==2 && report_bytes[1][2]==0 && local_cancels==1);
    reports=offline=local_events=0;
    for(int i=0;i<3;i++) { sample(KEY_IO_NUM0); sample(NO_KEY); }
    tick(); CHECK(reports==6 && offline==3);
    for(int i=0;i<6;i++) CHECK(report_bytes[i][2]==(i%2?0:4));
    settle(); CHECK(!key5 && offline==3 && reports==6);
    return 0;
}
int test_release_retry_and_map_rollback(void) {
    boot(1); sample(KEY_IO_NUM0); tick(); fail_up=1; sample(NO_KEY); tick();
    CHECK(release_pending && reports==1 && !admission);
    sample(KEY_IO_NUM0+1); fail_up=0; tick(); CHECK(reports==2 && !release_pending);
    CHECK(report_bytes[1][2]==0); sample(NO_KEY); settle();
    sample(KEY_IO_NUM0); tick(); CHECK(reports==3);
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    fail_up=1; map.keys[0].usages[0]=9;
    CHECK(!rdx_hogp_key_action_keymap_apply(&map));
    map.keys[0].usages[0]=4; CHECK(!rdx_hogp_key_action_keymap_apply(&map));
    CHECK(release_pending); fail_up=0; tick(); CHECK(reports==4);
    tick(); CHECK(reports==4); sample(NO_KEY); settle(); sample(KEY_IO_NUM0); tick();
    CHECK(reports==5 && report_bytes[4][2]==4);
    return 0;
}
int test_local_gestures_survive_hid_reconnect(void) {
    boot(0);
    sample(KEY_IO_NUM4); sample(NO_KEY); reconnect(); settle(); CHECK(key5==1);
    boot(0); sample(KEY_IO_NUM0); sample(NO_KEY);
    reconnect(); settle(); CHECK(offline==1); /* HID 状态变化不丢弃本地单击。 */
    boot(0); sample(KEY_IO_NUM0); sample(KEY_IO_NUM0); tick();
    CHECK(get_key_hold(KEY_IO_NUM0,0));
    reconnect(); sample(KEY_IO_NUM4); sample(KEY_IO_NUM4); tick();
    CHECK(local_cancels==1 && !local_owner.id && !local_admission);
    sample(NO_KEY); settle(); CHECK(local_admission);
    return 0;
}
int test_key5_gestures_do_not_overlap(void) {
    for(int clicks=1;clicks<=2;clicks++) {
        boot(0);
        for(int i=0;i<clicks;i++) { sample(KEY_IO_NUM4); sample(NO_KEY); }
        settle(); CHECK(key5==1);
        CHECK(key5_events[0]==(clicks==1?KEY_ACTION_CLICK:KEY_ACTION_DOUBLE_CLICK));
    }
    boot(0);
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); tick(); }
    sample(NO_KEY); settle();
    CHECK(key5>=2 && key5_events[0]==KEY_ACTION_LONG);
    CHECK(key5_events[key5-1]==KEY_ACTION_UP);
    for(int i=0;i<key5;i++) {
        CHECK(key5_events[i]!=KEY_ACTION_CLICK && key5_events[i]!=KEY_ACTION_DOUBLE_CLICK);
    }
    return 0;
}
int test_disabled_map_keeps_physical_gestures(void) {
    boot(1);
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    memset(&map.keys[4],0,sizeof(map.keys[4]));
    CHECK(!rdx_hogp_key_action_keymap_apply(&map));
    tick(); settle(); reports=key5=0;
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); tick(); }
    sample(NO_KEY); settle();
    CHECK(!reports && key5>=2 && key5_events[0]==KEY_ACTION_LONG);
    CHECK(key5_events[key5-1]==KEY_ACTION_UP);

    return 0;
}
'''

ASYNC_TESTS = r'''
static void edge(u8 value) {
    u8 previous=stable_key;
    in_irq=1; rdx_hogp_input_scan(KEY_DRIVER_TYPE_IO,previous,value,0); in_irq=0;
}
static void pump(int ticks) {
    while(ticks--) { input_service(NULL); run_btstack(); }
}
static void async_boot(void) {
    boot(1); task_name="app_core";
    rdx_hogp_input_invalidate(); pump(8); edge(NO_KEY); pump(8);
    reports=attempts=0;
}
int test_async_fifo_preserves_fast_edges(void) {
    async_boot(); CHECK(admission && !irq_depth);
    for (int i=0;i<3;i++) { edge(KEY_IO_NUM0); edge(NO_KEY); }
    CHECK(input_count==6);
    input_service(NULL); CHECK(queue_count && reports==0 && input_count==5);
    for(int i=0;i<10;i++) input_service(NULL);
    CHECK(input_count==5 && queue_count==1 && !reports);
    pump(50);
    CHECK(input_count==0 && reports==6 && !release_pending && !send_pending);
    for(int i=0;i<6;i++) CHECK(report_bytes[i][2]==(i%2?0:4));
    CHECK(!context_errors && !irq_depth);
    return 0;
}
int test_async_cancel_before_and_during_submit(void) {
    async_boot(); edge(KEY_IO_NUM0); input_service(NULL); CHECK(queue_count);
    rdx_hogp_input_invalidate(); run_btstack(); CHECK(!reports);
    pump(10); CHECK(reports==1 && !report_bytes[0][2] && !admission);
    edge(NO_KEY); pump(8); CHECK(admission);
    reports=0; edge(KEY_IO_NUM0); input_service(NULL);
    invalidate_on_send=1; run_btstack(); CHECK(reports==1 && report_bytes[0][2]==4);
    pump(10); CHECK(reports==2 && !report_bytes[1][2] && !admission);
    CHECK(!release_pending && !send_pending && !context_errors);
    return 0;
}
int test_async_overflow_and_release_retry(void) {
    async_boot(); edge(KEY_IO_NUM0); input_service(NULL); CHECK(queue_count);
    for(int i=0;i<30;i++) edge(i%2?KEY_IO_NUM0:NO_KEY);
    CHECK(overflow_count && !admission);
    run_btstack(); CHECK(!reports); pump(8);
    edge(NO_KEY); pump(8); CHECK(admission && !release_pending);
    reports=0; edge(KEY_IO_NUM0); pump(4); CHECK(reports==1);
    fail_up=1; edge(NO_KEY); pump(12);
    CHECK(release_pending && !admission && reports==1);
    fail_up=0; pump(12); edge(NO_KEY); pump(6);
    CHECK(reports==2 && !report_bytes[1][2] && !release_pending && admission);
    CHECK(!context_errors && !irq_depth);
    return 0;
}
int test_idle_worker_lifecycle(void) {
    boot(0); run_app(); rdx_hogp_input_poll(); run_app();
    CHECK(admission && !timer_live && !input_timer && !timer_adds);
    CHECK(!rdx_hogp_input_needs_poll());
    int posts=app_posts;
    for(int i=0;i<100;i++) { sample(NO_KEY); rdx_hogp_input_poll(); run_app(); }
    CHECK(app_posts==posts && !timer_adds && !input_count);
    /* 不手动触发定时器：首次离线单击由事件送达。 */
    sample(KEY_IO_NUM0); run_app(); sample(NO_KEY); run_app();
    sample(NO_KEY); run_app(); sample(NO_KEY); run_app();
    CHECK(offline==1 && offline_event==KEY_ACTION_CLICK && !timer_adds);
    route=RDX_HOGP_INPUT_DISCARD; rdx_hogp_input_invalidate(); run_app();
    CHECK(!input_timer); /* 仅 RDX 就绪不能禁止本地输入。 */
    sample(NO_KEY); run_app();
    int before=local_events;
    sample(KEY_IO_NUM0); run_app(); sample(NO_KEY); run_app();
    CHECK(local_events==before+1 && !input_timer);
    ready=1; route=RDX_HOGP_INPUT_HID; ++s_hid_epoch;
    rdx_hogp_input_wake(); run_app();
    CHECK(input_timer && timer_live);
    ready=0; route=RDX_HOGP_INPUT_OFFLINE; rdx_hogp_input_invalidate();
    rdx_hogp_input_poll(); run_app();
    CHECK(!input_timer && !timer_live && timer_dels==1 && !release_pending);
    sample(NO_KEY); run_app(); CHECK(admission);
    CHECK(!irq_depth && !context_errors);
    return 0;
}
int test_worker_notification_and_timer_failure_recovery(void) {
    boot(0); run_app(); rdx_hogp_input_poll(); run_app();
    app_post_fail=1; rdx_hogp_input_invalidate();
    CHECK(input_pending && !app_queued);
    CHECK(rdx_hogp_input_needs_poll());
    app_post_fail=0; rdx_hogp_input_poll(); sample(NO_KEY); run_app();
    CHECK(admission && !input_timer);
    app_post_fail=1; sample(KEY_IO_NUM0); sample(NO_KEY);
    CHECK(input_count && !app_queued);
    app_post_fail=0; rdx_hogp_input_poll(); sample(NO_KEY); run_app();
    sample(NO_KEY); run_app(); CHECK(offline==1);
    timer_fail=1; ready=1; route=RDX_HOGP_INPUT_HID; ++s_hid_epoch;
    rdx_hogp_input_wake(); run_app();
    CHECK(!input_timer && timer_adds && !app_queued);
    CHECK(rdx_hogp_input_needs_poll());
    timer_fail=0; rdx_hogp_input_poll(); CHECK(input_timer);
    ready=0; route=RDX_HOGP_INPUT_OFFLINE; rdx_hogp_input_poll(); run_app();
    CHECK(!input_timer && !release_pending && !irq_depth && !context_errors);
    return 0;
}
int test_down_snapshot_has_no_torn_map_and_classifies_plain_usage(void) {
    boot(1);
    rdx_hogp_key_action_keyboard_t action={0};
    action.usages[5]=0x6d; CHECK(rdx_hogp_key_action_plain_usage(&action)==0x6d);
    action.usages[0]=0x6d; CHECK(!rdx_hogp_key_action_plain_usage(&action));
    action.usages[0]=0; action.modifiers=1; CHECK(!rdx_hogp_key_action_plain_usage(&action));
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    map.keys[0].usages[0]=0x6e;
    rdx_hogp_key_action_keymap_apply(&map);
    /* 服务发布配键前发生的 Down 仍须锁定当前 RAM 中的实际配键。 */
    sample(KEY_IO_NUM0);
    rdx_hogp_input_cycle_t cycle;
    CHECK(rdx_hogp_input_cycle_get(KEY_IO_NUM0,&cycle));
    CHECK(cycle.usage==0x6e && !cycle.hid_admitted);
    for(int i=0;i<3;i++) { sample(KEY_IO_NUM0); tick(); }
    CHECK(local_events && local_usages[0]==0x6e && !local_cancels);
    sample(NO_KEY); settle(); CHECK(local_cancels==1);
    return 0;
}
static void map_f19(int key) {
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    memset(&map.keys[key],0,sizeof(map.keys[key])); map.keys[key].usages[0]=0x6e;
    rdx_hogp_key_action_keymap_apply(&map); tick(); settle();
}
int test_f19_down_before_long_all_keys_and_hid_backpressure(void) {
    for(int key=0;key<5;key++) for(int online=0;online<2;online++) {
        boot(online); map_f19(key); task_name="app_core";
        sample(KEY_IO_NUM0+key); tick();
        CHECK(local_events==1 && local_actions[0]==KEY_ACTION_MAX && local_owner.id);
        CHECK(local_owner.physical_key==KEY_IO_NUM0+key && local_owner.down_ms==jiffies);
        /* Deliberately do not drain btstack: local start is independent. */
        for(int i=0;i<10;i++) { sample(KEY_IO_NUM0+key); tick(); }
        CHECK(local_events==1);
        sample(NO_KEY); tick(); CHECK(local_cancels==1 && !local_owner.id);
        CHECK(!context_errors && !irq_depth);
    }
    return 0;
}
int test_f19_fast_up_queued_down_and_source_cancel_cannot_replay(void) {
    for(int cancel=0;cancel<2;cancel++) {
        boot(0); map_f19(0); app_post_fail=timer_fail=1;
        sample(KEY_IO_NUM0); CHECK(local_start.id && !local_events);
        if(cancel) rdx_hogp_input_cancel(); else sample(NO_KEY);
        rdx_hogp_input_poll(); CHECK(local_events==(cancel?0:1) && !local_owner.id);
        if(!cancel) CHECK(local_actions[0]==KEY_ACTION_CLICK);
        app_post_fail=timer_fail=0; settle(); CHECK(local_events==(cancel?0:1));
    }
    boot(0); map_f19(0); app_post_fail=1;
    sample(KEY_IO_NUM0); CHECK(!local_events && local_start.id);
    rdx_hogp_input_poll(); CHECK(local_events==1 && local_owner.id);
    for(int i=0;i<8;i++) { sample(KEY_IO_NUM0); rdx_hogp_input_poll(); }
    CHECK(local_events==1); sample(NO_KEY); rdx_hogp_input_poll(); CHECK(local_cancels==1);
    return 0;
}
int test_f19_modified_multiple_disabled_and_dut_never_fast_start(void) {
    for(int kind=0;kind<4;kind++) for(int key=0;key<5;key++) {
        boot(0);
        rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
        memset(&map.keys[key],0,sizeof(map.keys[key]));
        map.keys[key].usages[0]=0x6e;
        if(kind==0) map.keys[key].modifiers=1;
        if(kind==1) map.keys[key].usages[1]=4;
        if(kind==2) map.keys[key].usages[0]=0;
        if(kind==3) business_mode=RDX_LOCAL_KEY_DUT;
        rdx_hogp_key_action_keymap_apply(&map); tick(); settle();
        sample(KEY_IO_NUM0+key); tick(); CHECK(!local_events && !local_start.id);
    }
    return 0;
}
int test_f19_hot_map_keeps_down_snapshot_and_one_attempt(void) {
    boot(0); map_f19(2); sample(KEY_IO_NUM0+2); tick(); CHECK(local_events==1);
    u32 id=local_owner.id;
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    map.keys[2].usages[0]=0x68; rdx_hogp_key_action_keymap_apply(&map); tick();
    for(int i=0;i<8;i++) { sample(KEY_IO_NUM0+2); tick(); }
    CHECK(local_events==1 && local_owner.id==id);
    sample(NO_KEY); settle(); CHECK(local_cancels==1);
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM0+2); tick(); }
    CHECK(local_actions[1]==KEY_ACTION_LONG && local_usages[1]==0x68);
    return 0;
}
int test_key5_f19_eight_click_survives_failed_cleanup_and_wakes(void) {
    boot(0); map_f19(4); run_app(); sample(KEY_IO_NUM4); run_app(); CHECK(local_events==1);
    cancel_fail=1; app_post_fail=timer_fail=1;
    sample(NO_KEY); rdx_hogp_input_poll(); CHECK(local_owner_pending);
    u32 epoch=local_epoch;
    for(int i=1;i<8;i++) {
        sample(KEY_IO_NUM4); rdx_hogp_input_poll();
        sample(NO_KEY); rdx_hogp_input_poll();
    }
    sample(NO_KEY); sample(NO_KEY); rdx_hogp_input_poll();
    CHECK(local_dut.cycle.id && local_epoch==epoch && local_events==1);
    CHECK(local_owner_pending && rdx_hogp_input_needs_poll());
    dut_ready=0; cancel_fail=0; rdx_hogp_input_poll();
    CHECK(!local_owner.id && local_dut.cycle.id && local_events==1);
    for(int i=0;i<5;i++) rdx_hogp_input_poll(); CHECK(local_events==1);
    dut_ready=1; rdx_hogp_input_poll();
    CHECK(!local_dut.cycle.id && local_events==2 &&
        local_actions[1]==KEY_ACTION_EIGHTUPLE_CLICK);
    rdx_hogp_input_poll(); CHECK(local_events==2);
    return 0;
}
int test_key5_deferred_entry_cancelled_by_system_new_press_or_gate(void) {
    for(int reason=0;reason<4;reason++) {
        boot(0); map_f19(4); dut_ready=0;
        for(int i=0;i<8;i++) { sample(KEY_IO_NUM4); tick(); sample(NO_KEY); tick(); }
        settle(); CHECK(local_dut.cycle.id);
        int before=local_events;
        if(reason==0) rdx_hogp_input_cancel();
        if(reason==1) sample(KEY_IO_NUM0);
        if(reason==2) business_mode=RDX_LOCAL_KEY_BLOCKED;
        if(reason==3) dut_ready=-1;
        tick(); CHECK(!local_dut.cycle.id);
        dut_ready=1; tick(); CHECK(local_events==before);
    }
    return 0;
}
static void cancel_dut_during_admission(void) {
    dut_callback_hook=NULL;
    rdx_hogp_input_cancel();
}
static void new_down_during_dut_admission(void) {
    dut_callback_hook=NULL;
    sample(KEY_IO_NUM0);
}
int test_key5_ticket_revalidates_after_callback_race(void) {
    for(int cancel=0;cancel<2;cancel++) {
        boot(0); dut_ready=0;
        for(int i=0;i<8;i++) { sample(KEY_IO_NUM4); sample(NO_KEY); tick(); }
        settle(); CHECK(local_dut.cycle.id && !local_events);
        dut_callback_hook=cancel?cancel_dut_during_admission:new_down_during_dut_admission;
        dut_ready=1; tick();
        CHECK(!local_dut.cycle.id && !local_events);
        tick(); CHECK(!local_events && !context_errors && !irq_depth);
    }
    return 0;
}
int test_f19_local_overflow_cannot_retry_and_hid_overflow_cannot_stop(void) {
    for(int local=0;local<2;local++) {
        async_boot(); map_f19(4); sample(KEY_IO_NUM4); pump(4);
        CHECK(local_events==1 && local_owner.id);
        rdx_hogp_input_cycle_t c=local_owner;
        if(local) {
            struct key_event key={.type=KEY_DRIVER_TYPE_IO,.value=KEY_IO_NUM4,.event=KEY_ACTION_HOLD};
            for(int i=0;i<LOCAL_FIFO_SIZE+1;i++) rdx_hogp_input_gesture(&key,&c);
        } else {
            struct input_event e={0}; e.epoch=input_epoch;
            for(int i=0;i<INPUT_FIFO_SIZE+1;i++) enqueue_locked(&e);
        }
        pump(6);
        CHECK(local_events==1 && local_cancels==local);
        for(int i=0;i<6;i++) { sample(KEY_IO_NUM4); pump(2); }
        CHECK(local_events==1);
        sample(NO_KEY); pump(6); CHECK(local_cancels==1 && !local_owner.id);
    }
    return 0;
}
int test_snapshot_survives_map_change_and_hid_invalidation(void) {
    async_boot();
    sample(KEY_IO_NUM4); pump(6);
    rdx_hogp_input_cycle_t before;
    CHECK(rdx_hogp_input_cycle_get(KEY_IO_NUM4,&before) && before.usage==0x6c);
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    map.keys[4].usages[0]=0x6e;
    CHECK(!rdx_hogp_key_action_keymap_apply(&map));
    pump(8);
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); pump(2); }
    CHECK(local_events && local_usages[0]==0x6c && local_ids[0]==before.id);
    CHECK(!local_cancels && local_owner.id==before.id);
    /* HID 令牌变化同样不能取消本次本地长按。 */
    ready=0; route=RDX_HOGP_INPUT_DISCARD; rdx_hogp_input_invalidate(); pump(4);
    CHECK(!local_cancels && local_owner.id==before.id);
    sample(NO_KEY); pump(4); CHECK(local_cancels==1 && !local_owner.id);
    settle(); sample(KEY_IO_NUM4); pump(2);
    rdx_hogp_input_cycle_t after;
    CHECK(rdx_hogp_input_cycle_get(KEY_IO_NUM4,&after));
    CHECK(after.id!=before.id && after.usage==0x6e && after.map!=before.map);
    return 0;
}
int test_failed_hid_down_and_up_do_not_lose_local_cleanup(void) {
    for(int down_failure=0;down_failure<2;down_failure++) {
        async_boot(); fail_down=down_failure;
        for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); pump(2); }
        CHECK(local_owner.id && local_events);
        fail_up=1; sample(NO_KEY); pump(8);
        CHECK(local_cancels==1 && !local_owner.id);
        fail_up=fail_down=0; pump(8);
        sample(NO_KEY); pump(4);
        CHECK(!release_pending && !local_owner_pending && !context_errors);
    }
    return 0;
}
int test_hid_backlog_does_not_block_local_hold_or_release(void) {
    async_boot(); sample(KEY_IO_NUM4); input_service(NULL);
    CHECK(queue_count && !reports);
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); input_service(NULL); }
    CHECK(queue_count && local_owner.id && local_events && local_actions[0]==KEY_ACTION_LONG);
    sample(NO_KEY); input_service(NULL);
    CHECK(queue_count && local_cancels==1 && !local_owner.id && !reports);
    pump(12); CHECK(reports==2 && !report_bytes[1][2] && !context_errors);
    return 0;
}
int test_local_cancel_retries_outside_full_queues(void) {
    boot(0); run_app();
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); run_app(); }
    CHECK(local_owner.id);
    rdx_hogp_input_cycle_t old=local_owner;
    cancel_fail=1; app_post_fail=1; timer_fail=1;
    sample(NO_KEY);
    rdx_hogp_input_poll();
    CHECK(local_owner_pending && local_owner.id==old.id && rdx_hogp_input_needs_poll());
    int tries=local_cancels; rdx_hogp_input_poll(); CHECK(local_cancels>tries);
    cancel_fail=0; rdx_hogp_input_poll();
    CHECK(!local_owner_pending && !local_owner.id);
    app_post_fail=timer_fail=0; settle();
    rdx_hogp_input_gesture(&(struct key_event){.value=KEY_IO_NUM4,.event=KEY_ACTION_UP},&old);
    tick(); CHECK(!local_owner.id);
    return 0;
}
int test_local_overflow_cancels_owner_without_dropping_hid(void) {
    async_boot();
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); pump(2); }
    CHECK(local_owner.id && held_key==4);
    int old_reports=reports;
    rdx_hogp_input_cycle_t cycle=local_owner;
    struct key_event key={.value=KEY_IO_NUM4,.event=KEY_ACTION_HOLD};
    for(int i=0;i<LOCAL_FIFO_SIZE+1;i++) rdx_hogp_input_gesture(&key,&cycle);
    CHECK(local_overflows==1 && !local_admission);
    pump(6);
    CHECK(local_cancels==1 && !local_owner.id && reports==old_reports && held_key==4);
    sample(NO_KEY); pump(6); CHECK(held_key==0xff && reports==old_reports+1);
    return 0;
}
int test_source_cancel_and_stale_held_events(void) {
    boot(0);
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); tick(); }
    rdx_hogp_input_cycle_t old=local_owner;
    CHECK(old.id);
    int before=local_events;
    struct key_event duplicate={.value=KEY_IO_NUM4,.event=KEY_ACTION_LONG};
    rdx_hogp_input_gesture(&duplicate,&old); tick(); CHECK(local_events==before);
    rdx_hogp_input_cancel(); tick(); CHECK(local_cancels==1 && !local_owner.id);
    int events=local_events;
    struct key_event key={.value=KEY_IO_NUM4,.event=KEY_ACTION_LONG};
    rdx_hogp_input_gesture(&key,&old); tick(); CHECK(local_events==events);
    sample(NO_KEY); settle();
    /* LONG 排队到松开之后才处理时，也不能短暂启动录音。 */
    boot(0);
    for(int i=0;i<4;i++) sample(KEY_IO_NUM4);
    sample(NO_KEY); settle(); CHECK(!local_events && !local_cancels);
    return 0;
}
int test_f18_click_groups_keep_snapshots_and_key5_eight_clicks(void) {
    boot(0);
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    map.keys[0].usages[0]=0x6d; rdx_hogp_key_action_keymap_apply(&map); tick(); settle();
    for(int i=0;i<2;++i) { sample(KEY_IO_NUM0); sample(NO_KEY); tick(); }
    settle(); CHECK(local_events==1 && local_actions[0]==KEY_ACTION_DOUBLE_CLICK);
    local_events=0;
    sample(KEY_IO_NUM0); sample(NO_KEY); tick(); CHECK(!local_events);
    map.keys[0].usages[0]=0x68; rdx_hogp_key_action_keymap_apply(&map);
    tick(); sample(KEY_IO_NUM0); sample(NO_KEY); tick();
    CHECK(local_events==2 && local_actions[0]==KEY_ACTION_CLICK && local_actions[1]==KEY_ACTION_CLICK);
    CHECK(local_usages[0]==0x6d && local_usages[1]==0x68 && local_ids[0]!=local_ids[1]);
    settle(); CHECK(local_events==2);
    for(int dut=0;dut<2;dut++) {
        boot(0); business_mode=dut?RDX_LOCAL_KEY_DUT:RDX_LOCAL_KEY_NORMAL; tick(); settle();
        for(int i=0;i<8;i++) { sample(KEY_IO_NUM4); sample(NO_KEY); tick(); }
        CHECK(!local_events); settle();
        CHECK(local_events==1 && local_actions[0]==KEY_ACTION_EIGHTUPLE_CLICK);
        if(dut) {
            sample(KEY_IO_NUM0); sample(NO_KEY); settle(); CHECK(local_events==1);
        }
    }
    return 0;
}
int test_pending_click_survives_next_map_long_press(void) {
    boot(0);
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    map.keys[0].usages[0]=0x6d; rdx_hogp_key_action_keymap_apply(&map); tick(); settle();
    sample(KEY_IO_NUM0); sample(NO_KEY); tick(); CHECK(!local_events);
    map.keys[0].usages[0]=0x6e; rdx_hogp_key_action_keymap_apply(&map); tick();
    for(int i=0;i<3;i++) { sample(KEY_IO_NUM0); tick(); }
    CHECK(local_events==2 && local_actions[0]==KEY_ACTION_MAX && local_usages[0]==0x6e);
    CHECK(local_actions[1]==KEY_ACTION_CLICK && local_usages[1]==0x6d);
    sample(NO_KEY); settle(); CHECK(local_cancels==1);
    return 0;
}
int test_no_key_never_wraps_into_a_click(void) {
    boot(0);
    for(int i=0;i<270;i++) { sample(KEY_IO_NUM4); sample(NO_KEY); tick(); }
    settle(); CHECK(!local_events);
    sample(KEY_IO_NUM4); sample(NO_KEY); settle();
    CHECK(local_events==1 && local_actions[0]==KEY_ACTION_CLICK);
    return 0;
}
int test_cancel_pending_rejects_new_press_until_release(void) {
    boot(0);
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); tick(); }
    cancel_fail=1; sample(NO_KEY); tick();
    CHECK(local_owner_pending && !local_admission);
    sample(KEY_IO_NUM4); tick();
    cancel_fail=0; tick();
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); tick(); }
    CHECK(!local_owner.id && !local_admission);
    sample(NO_KEY); settle(); CHECK(local_admission);
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); tick(); }
    CHECK(local_owner.id);
    return 0;
}
int test_hid_fifo_overflow_keeps_local_owner_until_real_release(void) {
    async_boot();
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); pump(2); }
    CHECK(local_owner.id && held_key==4);
    struct input_event e={0}; e.epoch=input_epoch;
    local_irq_disable();
    for(int i=0;i<INPUT_FIFO_SIZE+1;i++) enqueue_locked(&e);
    local_irq_enable();
    CHECK(overflow_count && local_owner.id && local_admission);
    pump(6); CHECK(!local_cancels && local_owner.id);
    sample(NO_KEY); pump(6); CHECK(local_cancels==1 && !local_owner.id);
    return 0;
}
static void scan_new_hold_during_business(void) {
    local_callback_hook=NULL;
    in_irq=1;
    sample(NO_KEY);
    for(int i=0;i<4;i++) sample(KEY_IO_NUM4);
    in_irq=0;
}
int test_scan_irq_cannot_overwrite_unreleased_owner(void) {
    boot(0);
    local_callback_hook=scan_new_hold_during_business;
    sample(KEY_IO_NUM4); sample(KEY_IO_NUM4); tick();
    CHECK(local_events>=2 && local_actions[0]==KEY_ACTION_LONG && local_actions[1]==KEY_ACTION_LONG);
    CHECK(local_ids[0]!=local_ids[1] && local_cancels==1 && cancelled_ids[0]==local_ids[0]);
    CHECK(local_owner.id==local_ids[1]);
    sample(NO_KEY); tick(); CHECK(local_cancels==2 && cancelled_ids[1]==local_ids[1]);
    CHECK(!context_errors);
    return 0;
}
int test_filtered_source_cancels_hold_and_quarantines_release(void) {
    boot(0);
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); tick(); }
    CHECK(local_owner.id);
    filtered=1; sample(KEY_IO_NUM4); tick();
    CHECK(local_cancels==1 && !local_owner.id && !local_admission);
    filtered=0; sample(KEY_IO_NUM4); tick(); CHECK(!local_admission);
    sample(NO_KEY); settle(); CHECK(local_admission);
    int before=key5;
    sample(KEY_IO_NUM4); sample(NO_KEY); settle();
    CHECK(key5==before+1 && key5_events[key5-1]==KEY_ACTION_CLICK);
    return 0;
}
int test_cycle_sequence_wrap_never_uses_zero(void) {
    boot(0); cycle_sequence=0xffffffffu;
    sample(KEY_IO_NUM0);
    rdx_hogp_input_cycle_t cycle;
    CHECK(rdx_hogp_input_cycle_get(KEY_IO_NUM0,&cycle) && cycle.id==1);
    sample(NO_KEY); tick(); CHECK(local_events==1 && local_ids[0]==1);
    return 0;
}
int test_blocked_mode_cannot_replay_old_gestures(void) {
    boot(0); sample(KEY_IO_NUM0); sample(NO_KEY);
    business_mode=RDX_LOCAL_KEY_BLOCKED; tick(); CHECK(!local_events);
    business_mode=RDX_LOCAL_KEY_NORMAL; tick(); settle(); CHECK(!local_events);
    sample(KEY_IO_NUM0); sample(NO_KEY); tick(); CHECK(local_events==1);
    return 0;
}
int test_offline_gestures_without_periodic_ticks(void) {
    for(int clicks=1;clicks<=3;clicks++) {
        boot(0); run_app(); rdx_hogp_input_poll(); run_app();
        for(int i=0;i<clicks;i++) {
            sample(KEY_IO_NUM0); run_app(); sample(NO_KEY); run_app();
        }
        sample(NO_KEY); run_app(); sample(NO_KEY); run_app();
        CHECK(offline==clicks && offline_event==KEY_ACTION_CLICK);
        CHECK(!input_timer && !timer_adds);
    }
    boot(0); run_app(); rdx_hogp_input_poll(); run_app();
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); run_app(); }
    sample(NO_KEY); run_app(); sample(NO_KEY); run_app();
    CHECK(key5>=2 && key5_events[0]==KEY_ACTION_LONG &&
          key5_events[key5-1]==KEY_ACTION_UP && !timer_adds);
    filtered=1; sample(NO_KEY); run_app(); CHECK(!admission);
    filtered=0; sample(NO_KEY); run_app(); CHECK(admission && !input_timer);
    rdx_hogp_input_stats_t stats; rdx_hogp_input_stats_get(&stats);
    CHECK(stats.worker_available && !stats.periodic_running && !context_errors);
    return 0;
}
'''

def main():
    header = (ROOT / 'SDK/apps/common/device/key/key_driver.h').read_text(encoding='utf-8')
    types = re.search(r'enum key_action \{.*?\};', header, re.S)[0]
    for name in ('key_event', 'key_driver_para', 'key_driver_ops'):
        types += re.search(r'struct ' + name + r' \{.*?\};', header, re.S)[0]
    adapter = (ROOT / 'SDK/apps/earphone/message/adapter/key.c').read_text(encoding='utf-8')
    adapter_state = adapter[adapter.index('struct key_hold {'):adapter.index('#if TCFG_USER_TWS_ENABLE')]
    driver = (ROOT / 'SDK/apps/common/device/key/key_driver.c').read_text(encoding='utf-8')
    header = source('rdx_hogp_keyboard.h')
    keyboard = source('rdx_hogp_keyboard.c')
    program = (STUBS + types + '\n' +
               header[header.index('#define RDX_HOGP_KEYBOARD_REPORT_LEN'):header.index('int rdx_hogp_keyboard_report_send(')] +
               no_includes(source('rdx_hogp_key_action.h')) +
               no_includes(source('rdx_hogp_input.h')) +
               'void rdx_app_local_key_gesture(const struct key_event *, const rdx_hogp_input_cycle_t *);\n'
               'u8 rdx_app_local_key_cancel(const rdx_hogp_input_cycle_t *);\n' +
               'void rdx_app_local_key_down(const rdx_hogp_input_cycle_t *);\n'
               'int rdx_app_local_key_dut_ready(const rdx_hogp_input_cycle_t *);\n' +
               no_includes(source('rdx_ble_session.h')) + no_includes(source('rdx_ble_session.c')) +
               TRANSPORT + keyboard[keyboard.index('enum hogp_tx_state'):keyboard.index('int rdx_hogp_keyboard_report_send(')] +
               ''.join(function(keyboard, name) for name in (
                   'u8 rdx_hogp_token_capture(', 'static int hogp_report_submit(',
                   'static void hogp_tx_execute(', 'int rdx_hogp_report_send_for_input(',
                   'int rdx_hogp_report_send_for_token(')) +
               no_includes(source('rdx_hogp_key_action.c')) +
               no_includes(source('rdx_hogp_input.c')) + adapter_state +
               function(adapter, 'void key_event_handler(') + SESSION_TESTS + SCAN_TESTS +
               function(driver, 'static void key_driver_scan(') + ASYNC_TESTS)
    with tempfile.TemporaryDirectory(prefix='hogp-hold-') as temp:
        path = Path(temp) / 'hold.c'
        path.write_text(program, encoding='utf-8')
        tests = re.findall(r'int (test_\w+)\(void\)', SCAN_TESTS + SESSION_TESTS + ASYNC_TESTS)
        run_c_checks(path, tests, native=True)
    print(f'HOGP actual C scan/gesture/FIFO/session/async ATT: {len(tests)} scenarios passed.')
    from key_record_dispatch_checks import main as check_local_keys
    check_local_keys()
    from led_key_feedback_checks import main as check_key_led
    check_key_led()
    from connection_tone_checks import main as check_connection_tones
    check_connection_tones()

if __name__ == '__main__':
    main()
