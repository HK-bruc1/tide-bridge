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
int jiffies_offset_to_msec(u32 a,u32 b) { return b-a; }
u16 sys_timer_add(void *p, void (*f)(void *), u32 ms) { return 1; }
int offline, key5, route, ready, fail_down, fail_up, reports, attempts;
u8 report_bytes[512][8];
int offline_event;
int rdx_app_key_msg_handler(int *msg);
int key5_events[32];
int app_send_message_from(int a,int b,int *c);
u8 rdx_app_hogp_input_route(void) { return route; }
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
#define Q_CALLBACK 1
const char *os_current_task(void) { return task_name; }
int os_taskq_post_type(const char *task,int type,int n,int *msg) {
    if (irq_depth || in_irq || strcmp(task,"btstack") || type!=Q_CALLBACK ||
        n!=3 || msg[1]!=1 || msg[0]!=(int)hogp_tx_execute) ++context_errors;
    if (queue_fail || queue_count) return -1;
    queued_ticket=msg[2]; queue_count=1; return 0;
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
    memset(cycle_epoch,0,sizeof(cycle_epoch)); memset(cycle_route,0,sizeof(cycle_route));
    memset(&published_token,0,sizeof(published_token));
    memset(&scan,0,sizeof(scan)); scan.last_key=scan.filter_value=NO_KEY;
    filtered=scan_filtered=0; key_event_reset(); product_click_epoch=product_hold_epoch=0;
    session_boot(0);
    route=online?2:1; ready=online;
    fail_up=fail_down=reports=attempts=offline=key5=0;
    rdx_hogp_input_init(); tick(); settle();
    reports=attempts=0;
}
static void reconnect(void) {
    rdx_hogp_input_invalidate(); ready=1; route=2; ++s_hid_epoch; tick();
}
int test_short_long_and_fast_cycles(void) {
    boot(1); CHECK(admission);
    sample(KEY_IO_NUM0); tick(); CHECK(reports==1 && report_bytes[0][2]==4);
    for(int i=0;i<150;i++) { sample(KEY_IO_NUM0); tick(); }
    CHECK(reports==1 && !offline);
    sample(NO_KEY); tick(); CHECK(reports==2 && report_bytes[1][2]==0);
    reports=0;
    for(int i=0;i<3;i++) { sample(KEY_IO_NUM0); sample(NO_KEY); }
    tick(); CHECK(reports==6 && !offline);
    for(int i=0;i<6;i++) CHECK(report_bytes[i][2]==(i%2?0:4));
    settle(); CHECK(!key5 && !offline && reports==6);
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
int test_offline_and_key5_are_preserved(void) {
    for (int clicks=1;clicks<=3;clicks++) {
        boot(0);
        for (int i=0;i<clicks;i++) { sample(KEY_IO_NUM0); sample(NO_KEY); }
        settle();
        CHECK(offline==1 && offline_event==(clicks==1?KEY_ACTION_CLICK:
              clicks==2?KEY_ACTION_DOUBLE_CLICK:KEY_ACTION_TRIPLE_CLICK));
    }
    sample(KEY_IO_NUM4); sample(NO_KEY); reconnect(); settle(); CHECK(key5==1);
    boot(0); sample(KEY_IO_NUM0); sample(NO_KEY);
    reconnect(); settle(); CHECK(!offline); /* Discard an old offline click. */
    boot(0); sample(KEY_IO_NUM0); sample(KEY_IO_NUM0); tick();
    CHECK(get_key_hold(KEY_IO_NUM0,0));
    reconnect(); sample(KEY_IO_NUM4); sample(KEY_IO_NUM4); tick();
    CHECK(get_key_hold(KEY_IO_NUM4,0) && !get_key_hold(KEY_IO_NUM0,0));
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
    for(int i=0;i<4;i++) sample(KEY_IO_NUM4);
    sample(NO_KEY); settle();
    CHECK(key5>=2 && key5_events[0]==KEY_ACTION_LONG);
    CHECK(key5_events[key5-1]==KEY_ACTION_UP);
    for(int i=0;i<key5;i++) {
        CHECK(key5_events[i]!=KEY_ACTION_CLICK && key5_events[i]!=KEY_ACTION_DOUBLE_CLICK);
    }
    return 0;
}
int test_key5_hid_and_recording_gestures_coexist(void) {
    boot(1);
    sample(KEY_IO_NUM4); tick();
    CHECK(reports==1 && report_bytes[0][2]==0x6c);
    for(int i=0;i<10;i++) { sample(KEY_IO_NUM4); tick(); }
    CHECK(reports==1 && key5>=1 && key5_events[0]==KEY_ACTION_LONG);
    sample(NO_KEY); settle();
    CHECK(reports==2 && report_bytes[1][2]==0);
    CHECK(key5_events[key5-1]==KEY_ACTION_UP && !offline);

    /* 禁用 HID 映射不能禁用录音手势处理路径。 */
    boot(1);
    rdx_hogp_key_action_keymap_t map=s_rdx_hogp_key_action_active_keymap;
    memset(&map.keys[4],0,sizeof(map.keys[4]));
    CHECK(!rdx_hogp_key_action_keymap_apply(&map));
    tick(); settle(); reports=key5=0;
    for(int i=0;i<4;i++) { sample(KEY_IO_NUM4); tick(); }
    sample(NO_KEY); settle();
    CHECK(!reports && key5>=2 && key5_events[0]==KEY_ACTION_LONG);
    CHECK(key5_events[key5-1]==KEY_ACTION_UP);

    /* KEY5 松开上报失败时，使用与其他按键相同的释放恢复机制。 */
    boot(1); sample(KEY_IO_NUM4); tick();
    fail_up=1; sample(NO_KEY); tick();
    CHECK(release_pending && reports==1);
    fail_up=0; tick();
    CHECK(!release_pending && reports==2 && !report_bytes[1][2]);
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
               no_includes(source('rdx_hogp_input.h')) +
               no_includes(source('rdx_ble_session.h')) + no_includes(source('rdx_ble_session.c')) +
               TRANSPORT + keyboard[keyboard.index('enum hogp_tx_state'):keyboard.index('int rdx_hogp_keyboard_report_send(')] +
               ''.join(function(keyboard, name) for name in (
                   'u8 rdx_hogp_token_capture(', 'static int hogp_report_submit(',
                   'static void hogp_tx_execute(', 'int rdx_hogp_report_send_for_input(',
                   'int rdx_hogp_report_send_for_token(')) +
               no_includes(source('rdx_hogp_key_action.h')) + no_includes(source('rdx_hogp_key_action.c')) +
               no_includes(source('rdx_hogp_input.c')) +
               'static u32 product_click_epoch, product_hold_epoch;\n' + adapter_state +
               function(adapter, 'void key_event_handler(') + SESSION_TESTS + SCAN_TESTS +
               function(driver, 'static void key_driver_scan(') + ASYNC_TESTS)
    with tempfile.TemporaryDirectory(prefix='hogp-hold-') as temp:
        path = Path(temp) / 'hold.c'
        path.write_text(program, encoding='utf-8')
        tests = re.findall(r'int (test_\w+)\(void\)', SCAN_TESTS + SESSION_TESTS + ASYNC_TESTS)
        run_c_checks(path, tests, native=True)
    print('HOGP actual C scan/gesture/FIFO/session/async ATT behavior passed.')
    from key5_record_checks import main as check_key5_record
    check_key5_record()

if __name__ == '__main__':
    main()
