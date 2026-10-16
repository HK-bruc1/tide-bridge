"""Validate factory USB admission, shutdown, CDC transport and driver boundaries.

Requires the repository's JL clang and Python llvmlite.
This checks policy/resource ordering, not physical USB enumeration or NAK timing.
"""
import re

from host_c_test_lib import ROOT, run_c_checks

STUBS = r'''
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned char usb_dev;
#define NULL ((void *)0)
#define FALSE 0
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 1
#define TCFG_USB_SLAVE_MSD_ENABLE 1
#define TCFG_USB_SLAVE_CDC_ENABLE 1
#define TCFG_USB_CDC_BACKGROUND_RUN 1
#define TCFG_SD0_ENABLE 1
#define APP_MODE_PC 1
#define SLAVE_MODE 1
#define USBSTACK_CDC_BACKGROUND 1
#define USBSTACK_STOP 2
#define USBSTACK_FACTORY_SHUTDOWN 3
#define MASSSTORAGE_CLASS 1
#define CDC_CLASS 2
#define log_info(...) ((void)0)
#define printf(...) ((void)0)
#define g_printf(...) ((void)0)
int on, vbus, role, pc, business, blocked, deferred, host_owned, dut_ready;
int rdx_dut_factory_usb_ready(void);
int post_fail, init_fail, posts, starts, stops, hw_class, violation, masked;
struct { int goto_poweroff_flag; } app_var;
int get_power_on_status(void) { return on; }
int get_charge_online_flag(void) { return vbus; }
int usb_otg_online(int id) { return role; }
int app_in_mode(int mode) { return pc; }
int rdx_app_business_started(void) { return business; }
int rdx_storage_lifecycle_business_blocked(void) { return blocked; }
int rdx_storage_lifecycle_shutdown_deferred(void) { return deferred; }
int rdx_dut_factory_usb_ready(void) {
    return on && business && !app_var.goto_poweroff_flag;
}
int rdx_dip_switch_pc_allowed(void) { return !on && vbus && role == SLAVE_MODE && !business && !app_var.goto_poweroff_flag; }
int pc_storage_usb_ready(void) { return host_owned && pc; }
void usb_stop(usb_dev id);
u32 test_now;
u32 sys_timer_get_ms(void) { return test_now; }
int factory_cdc_start(void) { return 0; }
int cdc_stops;
void factory_cdc_stop(void) { ++cdc_stops; }
void factory_cdc_quiesce(void) {}
void factory_cdc_process(void) {}
int factory_cdc_needs_restart(void) { return 0; }
int usb_message_to_stack(int msg, void *arg, int sync) {
    ++posts;
    if (sync) violation=10;
    if (post_fail) return -1;
    if (sync && msg == USBSTACK_STOP) usb_stop(0);
    return 0;
}
void local_irq_disable(void) {}
void local_irq_enable(void) {}
void usb_write_intr_usbe(usb_dev id, int value) { masked=1; }
void usb_clr_intr_txe(usb_dev id, int value) {}
void usb_clr_intr_rxe(usb_dev id, int value) {}
void usb_sie_disable(usb_dev id) {}
void usb_sie_close(usb_dev id) {}
int usb_device_mode(usb_dev id, u32 cls) {
    if (cls) {
        if (hw_class) violation=1;
        if (init_fail) return -1;
        hw_class=cls; ++starts; masked=0;
    } else {
        if (hw_class && !masked) violation=2;
        hw_class=0; ++stops;
    }
    return 0;
}
void msd_register_disk(const char *name, void *p) { if (hw_class != MASSSTORAGE_CLASS) violation=3; }
void usb_msd_wakeup(void) {}
void usb_msd_reset_wakeup(void) {}
int msd_set_wakeup_handle(void (*p)(void)) { return 1; }
void msd_set_reset_wakeup_handle(void (*p)(void)) {}
void msd_unregister_all(void) {}
'''

TESTS = r'''
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
void boot(void) {
    cdc_stops=0;
    dut_ready=1;
    on=vbus=role=business=1; pc=blocked=deferred=host_owned=0;
    post_fail=init_fail=posts=starts=stops=hw_class=violation=masked=0;
    app_var.goto_poweroff_flag=0;
    factory_cdc_requested=factory_usb_state=factory_usb_shutdown=0;
    factory_cdc_retry_at=factory_cdc_retry_wait=0;
    test_now=0;
}
int test_all(void) {
    boot(); usb_stop(0); CHECK(cdc_stops==0);
    boot(); on=business=0; pc=host_owned=1;
    usb_start(0); usb_stop(0); CHECK(cdc_stops==0);
    boot(); usb_factory_service(); usb_cdc_background_run(0);
    CHECK(cdc_stops==0); usb_stop(0); CHECK(cdc_stops==1);
    usb_stop(0); CHECK(cdc_stops==1);
    boot(); dut_ready=0;
    usb_factory_service(); usb_cdc_background_run(0);
    CHECK(hw_class==CDC_CLASS); /* normal ON needs no DUT */
    dut_ready=1; usb_factory_service(); usb_cdc_background_run(0);
    CHECK(hw_class==CDC_CLASS && starts==1); /* entry keeps the port */
    dut_ready=0; usb_cdc_background_run(0); CHECK(hw_class==CDC_CLASS && starts==1); /* exit keeps the port */
    boot(); usb_factory_service(); dut_ready=0;
    usb_cdc_background_run(0); CHECK(hw_class==CDC_CLASS); /* queued start remains valid after exit */
    boot(); vbus=0; usb_factory_service(); usb_cdc_background_run(0); CHECK(!hw_class);
    vbus=1; usb_factory_service(); usb_cdc_background_run(0); CHECK(hw_class==CDC_CLASS);
    boot(); business=0;
    CHECK(usb_cdc_background_standby(0)==0 && !hw_class);
    usb_factory_service(); CHECK(posts==0);
    business=1; usb_factory_service(); CHECK(posts==1 && !hw_class);
    usb_cdc_background_run(0); CHECK(hw_class==CDC_CLASS && starts==1);
    usb_factory_service(); usb_cdc_background_run(0);
    CHECK(starts==1 && !violation);
    on=0; usb_cdc_background_run(0); CHECK(!hw_class);
    usb_start(0); CHECK(!hw_class); /* warm runtime must not export */
    boot(); on=0; business=0; pc=1;
    usb_start(0); CHECK(!hw_class); /* no SD takeover acknowledgment */
    host_owned=1; usb_start(0); CHECK(hw_class==MASSSTORAGE_CLASS);
    usb_start(0); CHECK(starts==1);
    CHECK(usb_cdc_background_standby(0)==0 && starts==1 && hw_class==MASSSTORAGE_CLASS);
    on=1; usb_factory_service(); usb_cdc_background_run(0);
    CHECK(hw_class==MASSSTORAGE_CLASS); /* cannot replace live MSC */
    usb_stop(0); pc=0; host_owned=0; blocked=1; business=1;
    usb_factory_service(); usb_cdc_background_run(0); CHECK(hw_class==CDC_CLASS);
    blocked=0; usb_factory_service(); usb_cdc_background_run(0);
    CHECK(hw_class==CDC_CLASS && !violation);
    boot(); usb_factory_service(); on=0;
    usb_cdc_background_run(0); CHECK(!hw_class); /* stale request */
    boot(); usb_factory_service(); role=0;
    usb_cdc_background_run(0); CHECK(!hw_class);
    boot(); usb_factory_service(); vbus=0;
    usb_cdc_background_run(0); CHECK(!hw_class);
    boot(); post_fail=1; usb_factory_service(); CHECK(posts==1 && !hw_class);
    post_fail=0; usb_factory_service(); CHECK(posts==2);
    init_fail=1; usb_cdc_background_run(0); CHECK(!usb_factory_cdc_started());
    init_fail=0; usb_factory_service(); usb_cdc_background_run(0);
    CHECK(usb_factory_cdc_started());
    blocked=1; usb_factory_service(); usb_cdc_background_run(0); CHECK(hw_class==CDC_CLASS);
    boot(); usb_factory_service(); usb_cdc_background_run(0);
    deferred=1; usb_cdc_background_run(0); CHECK(hw_class==CDC_CLASS);
    boot(); usb_factory_service(); usb_cdc_background_run(0);
    post_fail=1; usb_factory_shutdown(); CHECK(hw_class==CDC_CLASS);
    post_fail=0; usb_factory_shutdown();
    usb_factory_shutdown_process(); CHECK(!hw_class);
    usb_factory_service(); usb_cdc_background_run(0);
    CHECK(!hw_class && !violation); /* shutdown is latched */
    boot(); vbus=role=0;
    usb_factory_shutdown(); CHECK(!posts && !stops);
    usb_factory_shutdown_process(); CHECK(!posts && !stops);
    usb_cdc_background_run(0); CHECK(!hw_class);
    boot(); usb_factory_service(); /* start queued before shutdown */
    usb_factory_shutdown(); usb_cdc_background_run(0);
    CHECK(!hw_class && !stops);
    boot(); on=business=0; pc=host_owned=1;
    usb_start(0); CHECK(hw_class==MASSSTORAGE_CLASS);
    int old_stops=stops;
    usb_factory_shutdown(); usb_factory_shutdown_process();
    CHECK(hw_class==MASSSTORAGE_CLASS && stops==old_stops);
    boot(); usb_factory_service(); usb_cdc_background_run(1); CHECK(!hw_class);
    return 0;
}
'''


def main():
    source = (ROOT / 'SDK/apps/common/device/usb/device/task_pc.c').read_text(encoding='utf-8')
    policy = source[source.index('enum { FACTORY_USB_NONE'):source.index('\n\n\n#if TCFG_USB_SLAVE_MSD_ENABLE')]
    # Remove the outer factory-config closing directive; keep its real body.
    policy = policy[:policy.rindex('#endif')]
    functions = source[source.index('void usb_start('):source.index('int pc_device_event_handler(')]
    audit = ROOT / 'cache/factory-usb-test'
    audit.mkdir(parents=True, exist_ok=True)
    c_path = audit / 'test.c'
    dut = (ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_dut.c').read_text(encoding='utf-8')
    key_entry = dut[dut.index('static volatile u8 factory_usb_revoked;'):dut.index('static void rdx_dut_motor_timer_cb(void *priv);')]
    lifecycle_stubs = r'''
#define DUT_LOG(...) ((void)0)
int dut_mode, key_disabled, entry_fail;
int rdx_dut_is_in_mode(void) { return dut_mode; }
int rdx_dut_is_key_dut_disabled(void) { return key_disabled; }
void rdx_dut_msg_handle(void) { if (!entry_fail) dut_mode = !dut_mode; }
'''
    lifecycle_tests = r'''
int cleanup_saw_ready, delayed_poweroff;
void rdx_dut_close_current_func(void) { cleanup_saw_ready = real_factory_usb_ready(); }
#define RDX_LED_SCENE_DUT_EXIT 0
void rdx_led_ctrl_set_scene(int scene) {}
void rdx_app_normal_poweroff(void) {}
int sys_timeout_add(void *p, void (*cb)(void *), int delay) { delayed_poweroff=1; return 0; }
POWER_OFF_BODY
int test_key_entry(void) {
    boot(); factory_usb_revoked=0; dut_mode=key_disabled=entry_fail=0;
    entry_fail=1; rdx_dut_key_mode_handle(); CHECK(rdx_dut_factory_usb_ready());
    entry_fail=0; key_disabled=1; rdx_dut_key_mode_handle(); CHECK(!dut_mode);
    key_disabled=0; on=0; rdx_dut_key_mode_handle(); CHECK(!dut_mode);
    on=1; blocked=1; rdx_dut_key_mode_handle(); CHECK(!dut_mode);
    blocked=0; rdx_dut_msg_handle(); CHECK(dut_mode && rdx_dut_factory_usb_ready());
    rdx_dut_msg_handle(); rdx_dut_key_mode_handle();
    CHECK(dut_mode && rdx_dut_factory_usb_ready());
    business=0; CHECK(!rdx_dut_factory_usb_ready()); business=1;
    dut_mode=0; CHECK(rdx_dut_factory_usb_ready()); dut_mode=1;
    blocked=1; CHECK(rdx_dut_factory_usb_ready()); blocked=0;
    rdx_dut_poweroff();
    CHECK(factory_usb_revoked && !dut_mode && !cleanup_saw_ready && delayed_poweroff);
    dut_mode=1; CHECK(!rdx_dut_factory_usb_ready()); /* no revival if shutdown never completes */
    return 0;
}
'''
    poweroff = dut[dut.index('void rdx_dut_poweroff(void)'):]
    poweroff = poweroff[:poweroff.index('\n}') + 2]
    lifecycle_tests = lifecycle_tests.replace('POWER_OFF_BODY', poweroff.replace('rdx_dut_info.dut_mode', 'dut_mode'))
    # Execute the real poweroff gate with a delayed USB worker, not a synchronous stub.
    gates = []
    for filename in ('poweroff.c', 'tws_poweroff.c'):
        text = (ROOT / 'SDK/apps/earphone/mode/bt' / filename).read_text(encoding='utf-8')
        start = text.index('    extern void usb_factory_shutdown(void);')
        gates.append(text[start:text.index('#endif', start)])
    assert gates[0] == gates[1], 'TWS and non-TWS shutdown gates diverged'
    gate_tests = r'''
static int gate_continued;
static void real_poweroff_gate(int reason) {
GATE_BODY
    ++gate_continued;
}
int test_poweroff_gate(void) {
    boot(); vbus=role=0; gate_continued=0;
    real_poweroff_gate(1);
    CHECK(gate_continued==1 && !posts && !stops && !violation);
    boot(); usb_factory_service(); usb_cdc_background_run(0);
    gate_continued=0;
    real_poweroff_gate(0); /* worker never acknowledges */
    CHECK(gate_continued==1 && hw_class==CDC_CLASS);
    boot(); usb_factory_service(); usb_cdc_background_run(0);
    gate_continued=0; post_fail=1;
    real_poweroff_gate(1); /* queue full */
    CHECK(gate_continued==1 && factory_usb_shutdown && !violation);
    return 0;
}
'''.replace('GATE_BODY', gates[0])
    c_path.write_text(STUBS + policy + functions + TESTS + lifecycle_stubs +
                     key_entry.replace('rdx_dut_factory_usb_ready', 'real_factory_usb_ready') +
                     lifecycle_tests.replace('rdx_dut_factory_usb_ready', 'real_factory_usb_ready') + gate_tests, encoding='utf-8')
    run_c_checks(c_path, ('test_all', 'test_key_entry', 'test_poweroff_gate'))
    print('PASS: shutdown continues without USB, worker ACK or queue capacity; MSC preserved; TWS matches')
    print('PASS: CDC independent of DUT/key/SD; key entry still guarded; OFF and shutdown revoke CDC')
    print('PASS: actual factory USB C: SD ownership, stale requests, retry, idempotence, IRQ-before-release and shutdown')
    check_cdc_transport()
    from test_factory_station import check_station
    check_station()
    from test_factory_protocol import check_protocol
    check_protocol()
    from test_factory_audio import check_audio
    check_audio()


CDC_STUBS = r'''
static int poll_ms;
void usr_timer_modify(int timer, int ms) { poll_ms=ms; }

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 1
#define NULL ((void *)0)
#define TCFG_T2620_FACTORY_USB_CDC_LOG_LEVEL 1
#define USB_TASK_NAME "usb_stack"
#define USBSTACK_FACTORY_CDC_IO 100
void *memcpy(void *, const void *, unsigned int);
void *memset(void *, int, unsigned int);
int memcmp(const void *, const void *, unsigned int);
static u32 now;
static int irq_depth, violation, post_fail, queued, timer_fail, busy, zero_write, short_write;
static int wire_len, reads, in_len, callback_reject, reset_in_callback, zlp_count;
int printf(const char *fmt, ...) { if (irq_depth) violation=9; return 0; }
static u8 wire[20000], incoming[64];
void local_irq_disable(void) { ++irq_depth; }
void local_irq_enable(void) { if (--irq_depth < 0) violation=1; }
u32 sys_timer_get_ms(void) { return now; }
int os_taskq_post_msg(const char *task, int count, int message) {
    if (post_fail) return -1;
    ++queued; return 0;
}
u16 usr_timer_add(void *p, void (*f)(void *), u32 ms, u8 prio) { poll_ms=ms; return !timer_fail; }
void usr_timer_del(u16 timer) {}
int cdc_factory_configured(void) { return 1; }
int cdc_factory_tx_busy(void) { return busy; }
int cdc_factory_write_packet(const u8 *data, u32 len) {
    if (!irq_depth) violation=2;
    if (busy) return -1;
    if (zero_write && len) return 0;
    if (short_write && len > 3) len=3;
    if (len) { memcpy(wire+wire_len, data, len); wire_len+=len; }
    else ++zlp_count;
    busy=1; return len;
}
int cdc_factory_read_packet(u8 *data) {
    if (!irq_depth) violation=3;
    if (!in_len) return 0;
    ++reads; int len=in_len; memcpy(data,incoming,len); in_len=0; return len;
}
'''
CDC_TESTS = r'''
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int consumer(u32 gen, const u8 *data, u32 len) {
    if (irq_depth) violation=4;
    if (callback_reject) return -1;
    if (reset_in_callback) {
        local_irq_disable(); factory_cdc_invalidate(FACTORY_CDC_RESET); local_irq_enable();
        return factory_cdc_send(gen,data,len);
    }
    return factory_cdc_send(gen,data,len);
}
static int state_notifications;
static u32 notified_generation;
static void state_notify(void) {
    ++state_notifications;
    notified_generation=factory_cdc_generation();
}
static void tick(void) {
    now+=10;
    factory_cdc_kick(NULL);
    if (queued) { --queued; factory_cdc_process(); }
}
static void pump(int count) {
    for (int i=0;i<count;++i) { busy=0; tick(); }
}
static void boot(void) {
    memset(&transport,0,sizeof(transport)); now=0;
    irq_depth=violation=post_fail=queued=timer_fail=busy=zero_write=short_write=0;
    wire_len=reads=in_len=callback_reject=reset_in_callback=zlp_count=0;
    factory_cdc_start();
    local_irq_disable(); factory_cdc_open(); local_irq_enable();
    factory_cdc_set_rx_handler(consumer);
}
int test_transport(void) {
    boot();
    state_notifications=0; factory_cdc_set_state_handler(state_notify);
    factory_cdc_open(); CHECK(!state_notifications); /* 重复 DTR 打开操作必须幂等。 */
    local_irq_disable(); factory_cdc_invalidate(FACTORY_CDC_RESET); local_irq_enable();
    CHECK(state_notifications==1 && !notified_generation && !irq_depth);
    local_irq_disable(); factory_cdc_open(); local_irq_enable();
    CHECK(state_notifications==2 && notified_generation && !irq_depth);
    factory_cdc_quiesce(); CHECK(state_notifications==3 && !notified_generation);
    factory_cdc_stop(); CHECK(state_notifications==4 && !notified_generation);
    factory_cdc_start(); factory_cdc_open();
    CHECK(state_notifications==6 && notified_generation); /* 重启后仍保留状态通知回调。 */
    boot();
    u32 abort_generation = factory_cdc_generation();
    factory_cdc_abort(abort_generation + 1);
    CHECK(factory_cdc_generation()==abort_generation && !factory_cdc_needs_restart());
    factory_cdc_abort(abort_generation);
    CHECK(!factory_cdc_generation() && factory_cdc_needs_restart() && !irq_depth);
    boot();

    u8 data[258]; for(int i=0;i<258;++i) data[i]=(u8)i;
    boot(); u32 gen=factory_cdc_generation(); CHECK(gen);
    /* Submitted DMA is not completed traffic; only hardware consumption counts. */
    CHECK(factory_cdc_send(gen,data,65)==0);
    tick(); CHECK(transport.tx_pending_length==64);
    tick(); CHECK(transport.tx_pending_length==64); /* still busy */
    busy=0; tick(); CHECK(transport.tx_pending_length==1);
    busy=0; tick(); CHECK(!transport.tx_pending_length);
    CHECK(wire_len==65);
    boot(); gen=factory_cdc_generation();
    /* Host-sized byte sequences and short writes preserve FIFO exactly. */
    int sizes[]={1,63,64,65,128,256,258};
    for(int k=0;k<7;++k) {
        wire_len=0; short_write=k&1;
        CHECK(factory_cdc_send(gen,data,sizes[k])==0);
        pump(180); CHECK(wire_len==sizes[k] && !memcmp(data,wire,wire_len));
    }
    CHECK(zlp_count>0 && !violation && !irq_depth);
    boot(); gen=factory_cdc_generation();
    CHECK(factory_cdc_send(gen,data,0)==FACTORY_CDC_INVALID);
    CHECK(factory_cdc_send(gen,data,259)==FACTORY_CDC_INVALID);
    for(int i=0;i<4;++i) CHECK(factory_cdc_send(gen,data,65)==0);
    CHECK(factory_cdc_send(gen,data,1)==FACTORY_CDC_FULL);
    /* Queue full applies RX backpressure and never consumes a new packet. */
    busy=1; in_len=64; tick(); CHECK(reads==0 && in_len==64);
    pump(20); CHECK(reads==1);
    boot(); gen=factory_cdc_generation();
    zero_write=1; CHECK(factory_cdc_send(gen,data,65)==0); pump(5);
    CHECK(wire_len==0 && transport.count==1);
    zero_write=0; pump(10); CHECK(wire_len==65 && !memcmp(data,wire,65));
    /* A lost IRQ post is recovered by the periodic tick. */
    boot(); post_fail=1; in_len=64; memcpy(incoming,data,64);
    factory_cdc_rx_irq(); CHECK(!queued && !transport.posted);
    post_fail=0; pump(5); CHECK(reads==1 && wire_len==64 && !memcmp(data,wire,64));
    /* Consumer rejection retains exactly one packet without duplicates. */
    boot(); in_len=63; memcpy(incoming,data,63); callback_reject=1;
    pump(5); CHECK(reads==1 && transport.rx_length==63 && !wire_len);
    callback_reject=0; pump(5); CHECK(reads==1 && wire_len==63 && !memcmp(data,wire,63));
    /* Reset racing the unlocked callback rejects its old reply. */
    boot(); reset_in_callback=1; in_len=1; pump(2);
    CHECK(!wire_len && !transport.count && !factory_cdc_generation());
    /* Invalidate half-sent TX on every session-ending event. */
    for(int reason=FACTORY_CDC_RESET;reason<=FACTORY_CDC_TIMEOUT;++reason) {
        boot(); gen=factory_cdc_generation();
        CHECK(factory_cdc_send(gen,data,258)==0); tick(); CHECK(wire_len==64);
        local_irq_disable(); factory_cdc_invalidate(reason); local_irq_enable();
        CHECK(factory_cdc_send(gen,data,1)==FACTORY_CDC_STALE);
        CHECK(!transport.tx_pending_length);
        pump(10); CHECK(wire_len==64 && !transport.count && !transport.rx_length);
    }
    /* Shutdown revokes software I/O even if cleanup never executes. */
    boot(); gen=factory_cdc_generation();
    CHECK(factory_cdc_send(gen,data,258)==0); tick(); CHECK(wire_len==64);
    factory_cdc_quiesce(); factory_cdc_open();
    CHECK(!factory_cdc_generation());
    CHECK(factory_cdc_send(gen,data,1)==FACTORY_CDC_STALE);
    factory_cdc_process(); pump(10); CHECK(wire_len==64);
    CHECK(!irq_depth && !violation);
    /* A pre-stop work message cannot perform I/O in the next session. */
    boot(); gen=factory_cdc_generation(); factory_cdc_kick(NULL);
    factory_cdc_stop(); factory_cdc_start();
    local_irq_disable(); factory_cdc_open(); local_irq_enable();
    CHECK(gen!=factory_cdc_generation());
    CHECK(factory_cdc_send(factory_cdc_generation(),data,1)==0);
    --queued; factory_cdc_process(); CHECK(!wire_len);
    pump(3); CHECK(wire_len==1);
    /* Last DMA packet still needs a timeout even after FIFO removal. */
    boot(); CHECK(factory_cdc_send(factory_cdc_generation(),data,1)==0);
    tick(); CHECK(!transport.count && transport.tx_wait);
    for(int i=0;i<205;++i) tick();
    CHECK(factory_cdc_needs_restart() && !factory_cdc_generation());
    /* Timeout wrap arithmetic, timer allocation failure, no retained payload. */
    boot(); now=0xfffffff0u;
    CHECK(factory_cdc_send(factory_cdc_generation(),data,1)==0);
    busy=1; for(int i=0;i<205;++i) tick(); CHECK(factory_cdc_needs_restart());
    boot(); factory_cdc_stop(); timer_fail=1; factory_cdc_start();
    CHECK(factory_cdc_needs_restart() && !factory_cdc_generation());
    CHECK(!irq_depth && !violation);
    /* 50 audio lines/sec plus control traffic must drain without FIFO growth.
     * Emulate immediate host consumption, but honor actual requested cadence. */
    boot(); unsigned next_tick=10, expected=0;
    for(now=0;now<2200;++now) {
        busy=0;
        if(now<2000 && now%20==0) {
            CHECK(factory_cdc_send(factory_cdc_generation(),data,126)==0); expected+=126;
        }
        if(now<2000 && now%200==0) {
            CHECK(factory_cdc_send(factory_cdc_generation(),data,48)==0); expected+=48;
        }
        if(now>=next_tick) {
            factory_cdc_kick(NULL);
            if(queued) { --queued; factory_cdc_process(); }
            next_tick=now+poll_ms;
        }
    }
    CHECK(wire_len==expected && !transport.count && !transport.tx_wait && poll_ms==10);
    return 0;
}
'''

CDC_DRIVER_STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define NULL ((void *)0)
#define BIT(n) (1u<<(n))
#define TCFG_T2620_FACTORY_USB_CDC_ENABLE 1
#define MAXP_SIZE_CDC_BULKIN 64
#define MAXP_SIZE_CDC_BULKOUT 64
#define CDC_DATA_EP_IN 1
#define CDC_DATA_EP_OUT 1
#define CDC_CLASS 4
#define USB_CONFIGURED 4
#define TXCSRP_TxPktRdy 1
#define TXCSRP_FIFONotEmpty 2
#define RXCSRP_RxPktRdy 1
#define RXCSRP_IncompRx 256
#define RXCSRP_SentStall 64
#define RXCSRP_SendStall 32
#define RXCSRP_FlushFIFO 16
#define RXCSRP_OverRun 4
void *memcpy(void *, const void *, unsigned int);
int memcmp(const void *, const void *, unsigned int);
struct usb_device_t { int bDeviceStates, wDeviceClass; } device;
struct { u8 *bulk_ep_in_buffer, *bulk_ep_out_buffer, *factory_rx_buffer; u8 bmTransceiver; } handle;
static __typeof__(handle) *cdc_hdl=&handle;
static u8 tx_dma[64], rx_dma[136];
static int txcsr, rxcsr, count, committed, tx_count, rx_ack, rx_enable, invalidated, configs;
static void *next_rx;
struct usb_device_t *usb_id2device(int id) { return &device; }
int usb_device2id(struct usb_device_t *p) { return 0; }
u32 usb_read_txcsr(int id,int ep) { return txcsr; }
u32 usb_read_rxcsr(int id,int ep) { return rxcsr; }
int usb_read_rxcount(int id,int ep) { return count; }
void usb_set_dma_taddr(int id,int ep,void *ptr) {}
void usb_set_dma_raddr(int id,int ep,void *ptr) { next_rx=ptr; }
void usb_write_ep_cnt(int id,int ep,int n) { tx_count=n; ++committed; }
void usb_write_txcsr(int id,int ep,int value) { txcsr=value; }
void usb_write_rxcsr(int id,int ep,int value) { ++rx_ack; rxcsr=0; }
void usb_set_intr_rxe(int id,int ep) { rx_enable=1; }
void usb_clr_intr_rxe(int id,int ep) { rx_enable=0; }
static void cdc_endpoint_init(struct usb_device_t *p,u32 itf) {
    ++configs; handle.factory_rx_buffer=rx_dma; rxcsr=txcsr=0;
}
'''
CDC_DRIVER_TESTS = r'''
void factory_cdc_invalidate(enum factory_cdc_reason reason) { ++invalidated; }
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int test_driver(void) {
    u8 data[64], received[64];
    for(int i=0;i<64;++i) { data[i]=i; rx_dma[i]=i; rx_dma[68+i]=63-i; }
    device.bDeviceStates=USB_CONFIGURED; device.wDeviceClass=CDC_CLASS;
    handle.bulk_ep_in_buffer=tx_dma; handle.bulk_ep_out_buffer=handle.factory_rx_buffer=rx_dma;
    handle.bmTransceiver=BIT(0)|BIT(4);
    CHECK(cdc_factory_write_packet(data,64)==64 && committed==1 && tx_count==64);
    CHECK(!memcmp(data,tx_dma,64));
    CHECK(cdc_factory_write_packet(data,1)==-1 && committed==1);
    txcsr=TXCSRP_FIFONotEmpty;
    CHECK(cdc_factory_write_packet(data,1)==-1 && committed==1);
    txcsr=0; CHECK(cdc_factory_write_packet(NULL,0)==0 && tx_count==0 && committed==2);
    txcsr=0; CHECK(cdc_factory_write_packet(data,65)==-1 && committed==2);
    handle.bmTransceiver=0; CHECK(cdc_factory_write_packet(data,1)==-1);
    handle.bmTransceiver=BIT(0)|BIT(4);
    CHECK(cdc_factory_read_packet(received)==0 && !rx_ack && rx_enable);
    rxcsr=1; count=1; CHECK(cdc_factory_read_packet(received)==1 && received[0]==0);
    CHECK(next_rx==rx_dma+68 && rx_ack==1);
    rxcsr=1; count=63; CHECK(cdc_factory_read_packet(received)==63);
    for(int i=0;i<63;++i) CHECK(received[i]==63-i);
    CHECK(next_rx==rx_dma && rx_ack==2);
    rxcsr=1; count=0; CHECK(cdc_factory_read_packet(received)==0);
    CHECK(next_rx==rx_dma+68 && rx_ack==3); /* ZLP consumed exactly once */
    rxcsr=1; count=65; CHECK(cdc_factory_read_packet(received)==0 && invalidated==1 && rx_ack==3);
    cdc_factory_configuration(&device,0);
    CHECK(invalidated==2 && !handle.bmTransceiver && configs==1 && !rx_enable);
    device.wDeviceClass=0; cdc_factory_configuration(&device,1); CHECK(configs==1);
    device.wDeviceClass=CDC_CLASS; cdc_factory_configuration(&device,1); CHECK(configs==2);
    device.bDeviceStates=0; CHECK(!cdc_factory_configured());
    return 0;
}
'''


def check_cdc_transport():
    folder = ROOT / 'SDK/apps/common/device/usb/device'
    header = (folder / 'usb_factory_cdc.h').read_text(encoding='utf-8') + (folder / 'usb_factory_cdc_internal.h').read_text(encoding='utf-8')
    source = (folder / 'usb_factory_cdc.c').read_text(encoding='utf-8')
    strip = lambda text: re.sub(r'^#include.*$', '', text, flags=re.M)
    audit = ROOT / 'cache/factory-cdc-transport-test'
    audit.mkdir(parents=True, exist_ok=True)
    driver = (folder / 'cdc.c').read_text(encoding='utf-8')
    driver = driver[driver.index('void cdc_factory_configuration('):driver.index('\nu32 cdc_read_data(')]
    driver = driver[:driver.rindex('#endif')]
    suites = [
        ('transport-0', 'test_transport', CDC_STUBS + '\n#define TCFG_T2620_FACTORY_USB_CDC_TEST_ENABLE 0\n' +
         strip(header) + strip(source) + CDC_TESTS),
        ('driver', 'test_driver', CDC_DRIVER_STUBS + strip(header) + driver + CDC_DRIVER_TESTS),
    ]
    for name, function, text in suites:
        path = audit / f'{name}.c'
        path.write_text(text, encoding='utf-8')
        run_c_checks(path, (function,))
    print('PASS: actual CDC transport: FIFO/short writes/zero writes/backpressure/IRQ retry/')
    print('      callback reset race/stale generation/final DMA timeout')
    print('PASS: actual CDC driver: bounded DMA commit, busy/ZLP, alternating short RX, deconfiguration')


if __name__ == '__main__':
    main()
