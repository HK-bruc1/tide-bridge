"""运行实际 PA4 管理器，验证连接空闲调度、停用复检与失败恢复；不模拟 SD 硬件。"""
import re
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, run_c_checks

FOLDER = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
STUBS = r'''
#define NULL ((void*)0)
void *memset(void *,int,unsigned long long);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
typedef int bool;
typedef int OS_MUTEX;
#define TCFG_T2620_SHARED_VDD_ENABLE 1
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define TCFG_SD0_ENABLE 1
#define TCFG_DEV_MANAGER_ENABLE 1
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1
#define TCFG_T2620_CONNECTED_VDD_IDLE_MS 120000u
#define T2620_SHARED_VDD_MODE_DRY_RUN 0
#define T2620_SHARED_VDD_MODE_UNMOUNT_ONLY 1
#define T2620_SHARED_VDD_MODE_POWER_CUT 2
#define TCFG_T2620_CONNECTED_VDD_MODE test_mode
#define TCFG_T2620_SHARED_VDD_MODE offline_mode
#define TCFG_T2620_SHARED_VDD_POWER_STABLE_TICKS 1
#define TCFG_T2620_SHARED_VDD_IO 4
#define TCFG_SD0_PORT_CLK 10
#define TCFG_SD0_PORT_CMD 11
#define TCFG_SD0_PORT_DA0 12
#define IO_PORT_SPILT(x) x
#define PORT_HIGHZ 2
#define PORT_OUTPUT_HIGH 1
#define PORT_OUTPUT_LOW 0
#define RECORD_STATE_STOP 0
#define REC_PROCESS_STATE_BUSY 1
#define PB_STATE_UNREADY 0
#define PB_STATE_STOPPED 1
#define APP_MODE_PC 1
#define RDX_LED_SCENE_OFF 0
#define RDX_CHARGE_OUT 0
#define IDLE_MODE 0
#define DISCONN_MODE 1
#define Q_CALLBACK 1
#define r_printf(...) ((void)0)
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
typedef struct { int run, process_state; } RecordStatus;
typedef struct { int state; } pb_public_info_t;
typedef struct { int file_send_busy; } ReqFileInfo;
static RecordStatus record;
static ReqFileInfo file;
static int links, charge, vbus, usb, pc, led, dut, ota, lowbat, poweroff;
static int transfer, sync_busy, uxsync, loading, scanning, formatting, format_check;
static int record_busy, download, factory_busy, persistent_busy, service, storage_error;
static int irq, irq_bad, locks, lock_bad, takeover_calls, restore_calls, cuts, highz;
static int takeover_error, restore_error, led_error, led_restore_error, queue_fail, timer_fail;
static int race_stage, race_kind, race_irq, test_mode=2, offline_mode=2;
static struct { int goto_poweroff_flag; } app_var;
static pb_public_info_t playback;
static u32 version, pending, users, now, last_delay;
static int offline_valid;
static int events[128], epochs[128], qhead, qtail;
static struct { void (*cb)(void*); void *arg; u32 due; int active; } timers[64];
static u16 next_timer;
static void race(void);
static void inject_irq(void);
void local_irq_disable(void) { if(race_irq) inject_irq(); ++irq; }
void local_irq_enable(void) { --irq; }
int os_mutex_create(OS_MUTEX *m) { return 0; }
int os_mutex_pend(OS_MUTEX *m,int n) { if(irq || locks) ++lock_bad; ++locks; return 0; }
int os_mutex_post(OS_MUTEX *m) { --locks; return 0; }
void os_time_dly(int n) {}
int os_taskq_post_type(const char *t,int k,int n,int *args) {
    if(queue_fail || qtail-qhead>=128) return -1;
    events[qtail%128]=args[2]; epochs[qtail%128]=args[3]; ++qtail; return 0;
}
u32 sys_timer_get_ms(void) { return now; }
u16 sys_timeout_add(void *arg,void (*cb)(void*),u32 delay) {
    last_delay=delay;
    if(timer_fail) return 0;
    u16 id=++next_timer;
    if(id>=64) return 0;
    timers[id].cb=cb; timers[id].arg=arg; timers[id].due=now+delay; timers[id].active=1;
    return id;
}
void sys_timeout_del(u16 id) { timers[id].active=0; }
void gpio_set_mode(int io,int mode) {
    if(io==4 && mode==0) ++cuts;
    if(mode==PORT_HIGHZ) ++highz;
}
int dev_manager_takeover(const char *s) {
    ++takeover_calls; if(race_stage==1) race(); return takeover_error;
}
int dev_manager_restore(const char *s) { ++restore_calls; return restore_error; }
int rdx_led_hardware_deinit(void) {
    if(race_stage==2) race(); if(race_stage==3) race_irq=1; return led_error;
}
int rdx_led_hardware_resume(void) { return led_restore_error; }
u8 rdx_ble_server_get_connected_count(void) { if(irq) ++irq_bad; return links; }
RecordStatus *rdx_record_get_status(void) { return &record; }
void rdx_playback_get_info(pb_public_info_t *p) { *p=playback; }
ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) { return &file; }
int rdx_is_file_transfer_active(void) { return transfer; }
int rdx_is_file_sync_busy(void) { return sync_busy; }
int rdx_uxfile_sync_is_in_progress(void) { return uxsync; }
int rdx_uxfile_is_datFileInfo_loading(void) { return loading; }
int rdx_uxfile_is_scan_active(void) { return scanning; }
int app_in_mode(int m) { return pc; }
int rdx_uxfile_is_formatting(void) { return formatting; }
bool rdx_uxfile_sd_format_status_check(void) { return format_check; }
int rdx_led_ctrl_get_scene(void) { return led; }
int rdx_app_get_charge_state(void) { return charge; }
bool rdx_app_get_dut_status(void) { return dut; }
u8 get_ota_status(void) { return ota; }
int rdx_record_adv_busy(void) { return record_busy; }
int get_charge_online_flag(void) { return vbus; }
u32 usb_otg_online(int n) { return usb; }
u32 rdx_adv_policy_business_pending(void) { return pending; }
u32 rdx_adv_policy_business_version(void) { return version; }
int rdx_record_format_download_busy(void) { return download; }
int rdx_record_format_service_status(void) { return service; }
int rdx_record_format_status(void) { return storage_error; }
int rdx_factory_audio_file_busy(void) { return factory_busy; }
u32 rdx_record_format_io_users(void) { return users; }
u8 rdx_uxfile_power_busy(void) { return persistent_busy; }
int rdx_app_get_poweroff_flag(void) { return poweroff; }
int get_vbat_need_shutdown(void) { return lowbat; }
u8 rdx_adv_policy_idle_valid(void) { return offline_valid; }
void rdx_adv_policy_changed(void) {}
void rdx_adv_policy_activity(void) { ++version; }
void rdx_ble_server_conn_param_changed(void) {}
void rdx_ble_entry_changed(void) {}
'''

TESTS = r'''
static void race(void) {
    race_stage=0;
    if(race_kind==1) { ++version; ++pending; }
    if(race_kind==2) { links=0; rdx_peripheral_power_vdd_wake_request_begin(); }
    if(race_kind==3) { users=1; }
    if(race_kind==4) { vbus=1; ++version; }
}
static void inject_irq(void) { race_irq=0; race(); }
static int drain(void) {
    int budget=80;
    while(qhead<qtail && budget--) {
        int e=events[qhead%128], p=epochs[qhead%128]; ++qhead;
        rdx_peripheral_power_vdd_event_on_app_core(e,p);
    }
    return budget>0 && !irq && !irq_bad && !lock_bad && !locks;
}
static int changed(void) { rdx_peripheral_power_vdd_connected_changed_notify(); return drain(); }
static int fire(void) {
    u16 id=g_rdx_shared_vdd.conn_timer;
    if(id && timers[id].active) {
        timers[id].active=0; timers[id].cb(timers[id].arg);
    }
    return drain();
}
static void reset(void) {
    memset(&g_rdx_shared_vdd,0,sizeof(g_rdx_shared_vdd));
    g_rdx_shared_vdd.state=RDX_SHARED_VDD_STATE_ON_READY;
    g_rdx_shared_vdd.last_would_off_generation=(u32)-1;
    links=1; charge=vbus=usb=pc=led=dut=ota=lowbat=poweroff=0;
    transfer=sync_busy=uxsync=loading=scanning=formatting=format_check=0;
    record_busy=download=factory_busy=persistent_busy=service=storage_error=0;
    record.run=record.process_state=file.file_send_busy=0; playback.state=PB_STATE_STOPPED;
    app_var.goto_poweroff_flag=0;
    irq=irq_bad=locks=lock_bad=takeover_calls=restore_calls=cuts=highz=0;
    takeover_error=restore_error=led_error=led_restore_error=queue_fail=timer_fail=0;
    race_stage=race_kind=race_irq=0; test_mode=offline_mode=2;
    version=pending=users=now=last_delay=0; offline_valid=0;
    qhead=qtail=0; next_timer=0; memset(timers,0,sizeof(timers));
    rdx_peripheral_power_vdd_early_init();
}
int check_idle_modes_and_restore(void) {
    for(int mode=0; mode<=2; ++mode) for(int n=1; n<=2; ++n) {
        reset(); test_mode=mode; links=n; CHECK(changed());
        CHECK(g_rdx_shared_vdd.conn_timer && last_delay==120000 && !cuts);
        now=119999; CHECK(changed()); CHECK(!takeover_calls);
        now=120000; CHECK(fire()); CHECK(!g_rdx_shared_vdd.conn_timer);
        CHECK(takeover_calls==(mode!=0) && cuts==(mode==2) && links==n);
        if(mode==2) CHECK(g_rdx_shared_vdd.state==RDX_SHARED_VDD_STATE_OFF && highz==3);
        if(mode==1) CHECK(g_rdx_shared_vdd.state==RDX_SHARED_VDD_STATE_QUIESCED_ON);
        CHECK(changed()); CHECK(takeover_calls==(mode!=0));
        CHECK(!rdx_peripheral_power_vdd_ensure_on(RDX_SHARED_VDD_WAKE_BUSINESS));
        CHECK(drain()); CHECK(rdx_peripheral_power_vdd_is_ready());
        CHECK(restore_calls==(mode!=0));
        if(mode) CHECK(g_rdx_shared_vdd.conn_timer && last_delay==120000);
    }
    return 0;
}
int check_busy_gates_and_events(void) {
    int *gates[]={&charge,&vbus,&pc,&led,&dut,&ota,&lowbat,&poweroff,
        &transfer,&sync_busy,&uxsync,&loading,&scanning,&formatting,&format_check,
        &record_busy,&download,&factory_busy,&persistent_busy,&service,
        &record.run,&record.process_state,&file.file_send_busy,&app_var.goto_poweroff_flag};
    for(u32 i=0;i<sizeof(gates)/sizeof(gates[0]);++i) {
        reset(); CHECK(changed()); now=119000; *gates[i]=1; CHECK(changed());
        CHECK(!g_rdx_shared_vdd.conn_timer && !takeover_calls);
        now=900000; CHECK(changed()); CHECK(!g_rdx_shared_vdd.conn_timer);
        *gates[i]=0; CHECK(changed()); CHECK(last_delay==120000 && !cuts);
        now+=120000; CHECK(fire()); CHECK(cuts==1);
    }
    reset(); usb=2; CHECK(changed()); CHECK(!g_rdx_shared_vdd.conn_timer);
    usb=DISCONN_MODE; CHECK(changed()); CHECK(g_rdx_shared_vdd.conn_timer);
    reset(); playback.state=2; CHECK(changed()); CHECK(!g_rdx_shared_vdd.conn_timer);
    reset(); pending=1; CHECK(changed()); CHECK(!g_rdx_shared_vdd.conn_timer);
    /* A completed storage failure must neither prevent idle nor re-arm a
     * recurring retry. Actual dirty cache and live I/O still fence power. */
    reset(); service=storage_error=-1; CHECK(changed());
    CHECK(g_rdx_shared_vdd.conn_timer && last_delay==120000);
    now=120000; CHECK(fire()); CHECK(cuts==1 && !g_rdx_shared_vdd.conn_timer);
    for(int i=0;i<20;++i) { now+=5000; CHECK(changed()); }
    CHECK(cuts==1 && !restore_calls && !g_rdx_shared_vdd.conn_timer);
    return 0;
}
int check_activity_and_passive(void) {
    reset(); CHECK(changed()); void *old=(void*)g_rdx_shared_vdd.conn_timer_serial;
    now=119000; version+=2; CHECK(changed()); CHECK(last_delay==120000);
    rdx_vdd_conn_timeout(old); CHECK(!cuts && g_rdx_shared_vdd.conn_timer);
    now=238999; CHECK(changed()); CHECK(!cuts);
    now=239000; users=1; CHECK(fire());
    CHECK(!g_rdx_shared_vdd.conn_timer && g_rdx_shared_vdd.conn_armed && !cuts);
    users=0; rdx_peripheral_power_vdd_idle_hint(); CHECK(drain()); CHECK(cuts==1);
    for(int i=0;i<20;++i) { now+=5000; rdx_peripheral_power_vdd_idle_hint(); CHECK(drain()); }
    CHECK(cuts==1 && !restore_calls && !g_rdx_shared_vdd.conn_timer);
    reset(); now=0xffff0000u; CHECK(changed()); now+=120000; CHECK(fire()); CHECK(cuts==1);
    return 0;
}
int check_transition_races(void) {
    for(int stage=1;stage<=3;++stage) for(int kind=1;kind<=4;++kind) {
        reset(); CHECK(changed()); race_stage=stage; race_kind=kind;
        now=120000; CHECK(fire()); CHECK(!cuts && takeover_calls==1 && restore_calls==1);
        CHECK(rdx_peripheral_power_vdd_is_ready());
    }
    reset(); CHECK(changed()); now=119000; links=2;
    rdx_peripheral_power_vdd_ble_links_changed_notify(); CHECK(drain());
    CHECK(last_delay==120000); now=120000; CHECK(changed()); CHECK(!cuts);
    reset(); CHECK(changed()); now=120000; ++version; pending=1;
    rdx_peripheral_power_vdd_idle_stop(0,RDX_VDD_SRC_CONNECTED);
    CHECK(drain()); CHECK(!takeover_calls && !cuts);
    return 0;
}
int check_failures(void) {
    reset(); timer_fail=1; CHECK(changed()); CHECK(!g_rdx_shared_vdd.conn_armed);
    timer_fail=0; now=500000; CHECK(changed()); CHECK(last_delay==120000 && !cuts);
    reset(); CHECK(changed()); queue_fail=1; ++version;
    rdx_peripheral_power_vdd_connected_changed_notify(); queue_fail=0;
    now=120000; CHECK(fire()); CHECK(!cuts && last_delay==120000);
    for(int kind=0;kind<2;++kind) {
        reset(); CHECK(changed()); if(kind) led_error=1; else takeover_error=1;
        now=120000; CHECK(fire()); CHECK(!cuts && !g_rdx_shared_vdd.conn_timer);
        now+=120000; CHECK(changed()); CHECK(takeover_calls==1 && !g_rdx_shared_vdd.conn_timer);
        takeover_error=led_error=0; ++version; CHECK(changed()); CHECK(last_delay==120000);
    }
    reset(); CHECK(changed()); now=120000; CHECK(fire()); restore_error=1;
    CHECK(rdx_peripheral_power_vdd_ensure_on(RDX_SHARED_VDD_WAKE_BUSINESS)!=0);
    CHECK(changed()); CHECK(g_rdx_shared_vdd.state==RDX_SHARED_VDD_STATE_FAULT_ON);
    CHECK(g_rdx_shared_vdd.pa4_enabled && !g_rdx_shared_vdd.conn_timer);
    return 0;
}
int check_offline_boundary(void) {
    reset(); links=0; CHECK(changed()); CHECK(!g_rdx_shared_vdd.conn_timer);
    g_rdx_shared_vdd.slow_adv=1; offline_valid=1;
    rdx_peripheral_power_vdd_idle_evaluate(0); CHECK(cuts==1);
    reset(); links=0; g_rdx_shared_vdd.slow_adv=1; offline_valid=1; led=1;
    rdx_peripheral_power_vdd_idle_evaluate(0); CHECK(!cuts && last_delay==1000);
    CHECK(!g_rdx_shared_vdd.conn_timer);
    reset(); g_rdx_shared_vdd.slow_adv=1; offline_valid=1;
    rdx_peripheral_power_vdd_idle_evaluate(0); CHECK(!cuts);
    reset(); links=0; g_rdx_shared_vdd.slow_adv=1; offline_valid=1; users=1;
    rdx_peripheral_power_vdd_idle_evaluate(0);
    CHECK(!takeover_calls && last_delay==1000 && g_rdx_shared_vdd.idle_recheck_timer);
    users=0; now=1000;
    rdx_peripheral_power_vdd_idle_recheck_cb(NULL); CHECK(drain()); CHECK(cuts==1);
    return 0;
}
'''


def main():
    header = (FOLDER / 'rdx_peripheral_power.h').read_text(encoding='utf-8-sig')
    source = (FOLDER / 'rdx_peripheral_power.c').read_text(encoding='utf-8-sig')
    code = STUBS + re.sub(r'^#include[^\n]*\n', '', header + '\n' + source, flags=re.M) + TESTS
    checks = re.findall(r'int (check_\w+)\(void\)', TESTS)
    with tempfile.TemporaryDirectory(prefix='connected_power_') as work:
        path = Path(work) / 'check.c'
        path.write_text(code, encoding='utf-8')
        run_c_checks(path, checks, native=True)
    print('Connected PA4 power checks passed (hardware mocked).')


if __name__ == '__main__':
    main()
