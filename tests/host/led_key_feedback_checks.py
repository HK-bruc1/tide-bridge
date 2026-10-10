"""执行真实灯效引擎，验证按键提示、优先级及绑定后的静默广播。"""
import re
import tempfile
from pathlib import Path
from host_c_test_lib import ROOT, run_c_checks

BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'

STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int bool;
#define true 1
#define false 0
#define NULL ((void *)0)
#define TCFG_T2620_CHARGE_COEXIST_ENABLE 1
#define TCFG_DIP_SWITCH_POWER_ENABLE 1
#define RDX_SHARED_VDD_WAKE_BUSINESS 1
#define g_printf(...) ((void)0)
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
typedef struct { int initialized, run_en; } LedPt0807Config_t;
typedef struct { u8 g,r,b; } LedPt0807Rgb_t;
typedef struct { int run; } RecordStatus;
typedef struct { int onoff; } RdxWifiInfo;
enum { RECORD_STATE_STOP, RECORD_STATE_START, RECORD_STATE_RESUME };
enum { RDX_CHARGE_OUT, RDX_CHARGE_IN, RDX_CHARGE_FULL };
#define TRANSFER_BY_WIFI_ON 1
static struct { int goto_poweroff_flag; } app_var;
static int low_shutdown, usb, charge, ota, dut, transition, slow, hold, wakes;
static int link=1, battery=80, bound;
static u8 rdx_vm_get_bound_status(void) { return bound; }
static RecordStatus record;
static RdxWifiInfo wifi;
static LedPt0807Rgb_t rgb;
static RecordStatus *rdx_record_get_status(void) { return &record; }
static RdxWifiInfo *rdx_app_get_wifi_info(void) { return &wifi; }
static bool rdx_app_get_dut_status(void) { return dut; }
static u8 get_ota_status(void) { return ota; }
static int get_vbat_need_shutdown(void) { return low_shutdown; }
static int get_charge_online_flag(void) { return usb; }
static int get_vbat_percent(void) { return battery; }
static int rdx_app_get_charge_state(void) { return charge; }
static int rdx_app_business_started(void) { return 1; }
static int get_power_on_status(void) { return 1; }
static int rdx_storage_lifecycle_transition_led(void) { return transition; }
static int rdx_ble_server_has_active_link(void) { return link; }
static int rdx_peripheral_power_vdd_is_slow_adv(void) { return slow; }
static int rdx_peripheral_power_vdd_ensure_on(int reason) { ++wakes; return -1; }
static void rdx_peripheral_power_vdd_business_changed_notify(void) {}
static int rdx_app_record_is_hold_active(void) { return hold; }
static int sys_timer_add(void *p,void (*cb)(void *),int ms) { return 1; }
static void sys_timer_del(int id) {}
static void led_pt0807_set_all_rgb(LedPt0807Config_t *p,LedPt0807Rgb_t *v) { rgb=*v; }
static void led_pt0807_update(LedPt0807Config_t *p) {}
static void led_pt0807_run_enable(LedPt0807Config_t *p,int en) { p->run_en=en; }
'''

TESTS = r'''
static LedPt0807Config_t config={1};
static void reset(void) {
    low_shutdown=usb=charge=ota=dut=transition=slow=hold=wakes=0;
    app_var.goto_poweroff_flag=0; link=1; battery=80;
    bound=g_led_bound_status=0;
    record.run=RECORD_STATE_STOP; wifi.onoff=0;
    g_current_scene=RDX_LED_SCENE_OFF;
    g_led_config=&config; g_led_initialized=1;
    _rdx_led_apply_effect(RDX_LED_EFFECT_OFF);
}
int test_key_feedback_color_timeout(void) {
    for(int key=0;key<2;++key) {
        reset();
        rdx_led_ctrl_set_scene(key ? RDX_LED_SCENE_KEY2_FEEDBACK : RDX_LED_SCENE_KEY1_FEEDBACK);
        CHECK(g_active_effect->timeout_ms==2000);
        CHECK(rgb.r==(key?181:0) && rgb.g==(key?82:0) && rgb.b==(key?255:200));
        for(int i=0;i<99;++i) rdx_led_ctrl_update();
        CHECK(_rdx_led_is_key_feedback_scene(g_current_scene));
        rdx_led_ctrl_show_battery();
        CHECK(_rdx_led_is_key_feedback_scene(g_current_scene) && g_effect_elapsed_ms==1980);
        rdx_led_ctrl_update();
        CHECK(g_current_scene==RDX_LED_SCENE_OFF && !rgb.r && !rgb.g && !rgb.b);
    }
    reset(); rdx_led_ctrl_show_battery();
    CHECK(g_current_scene==RDX_LED_SCENE_BATTERY_QUERY);
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY1_FEEDBACK);
    CHECK(g_current_scene==RDX_LED_SCENE_KEY1_FEEDBACK && !rgb.g && rgb.b);
    return 0;
}
int test_key_feedback_protected_scenes(void) {
    for(int key=0;key<2;++key) for(int guard=0;guard<12;++guard) {
        reset();
        if(guard==0) record.run=RECORD_STATE_START;
        if(guard==1) record.run=RECORD_STATE_RESUME;
        if(guard==2) wifi.onoff=TRANSFER_BY_WIFI_ON;
        if(guard==3) ota=1;
        if(guard==4) dut=1;
        if(guard==5) charge=RDX_CHARGE_IN;
        if(guard==6) { charge=RDX_CHARGE_FULL; usb=1; }
        if(guard==7) app_var.goto_poweroff_flag=1;
        if(guard==8) low_shutdown=1;
        if(guard==9) transition=1;
        if(guard==10) g_current_scene=RDX_LED_SCENE_FINALPACK_DONE;
        if(guard==11) g_current_scene=RDX_LED_SCENE_LOW_BATTERY;
        rdx_led_scene_e before=g_current_scene;
        g_effect_elapsed_ms=100;
        rdx_led_ctrl_set_scene(key ? RDX_LED_SCENE_KEY2_FEEDBACK : RDX_LED_SCENE_KEY1_FEEDBACK);
        CHECK(g_current_scene==before && g_effect_elapsed_ms==100 && !wakes);
    }
    return 0;
}
int test_key_feedback_live_restore(void) {
    for(int next=0;next<6;++next) {
        reset(); rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY2_FEEDBACK);
        if(next==0) { record.run=RECORD_STATE_START; hold=1; }
        if(next==1) wifi.onoff=TRANSFER_BY_WIFI_ON;
        if(next==2) ota=1;
        if(next==3) dut=1;
        if(next==4) { charge=RDX_CHARGE_IN; usb=1; }
        if(next==5) transition=1;
        rdx_led_ctrl_update();
        int expected[]={RDX_LED_SCENE_RECORD_START,RDX_LED_SCENE_WIFI_START,
            RDX_LED_SCENE_OTA_START,RDX_LED_SCENE_DUT_ENTER,
            RDX_LED_SCENE_CHARGE_PLUG_IN,RDX_LED_SCENE_USB_SWITCH_WAIT};
        CHECK(g_current_scene==expected[next]);
    }
    reset(); rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY1_FEEDBACK);
    link=0;
    for(int i=0;i<100;++i) rdx_led_ctrl_update();
    CHECK(g_current_scene==RDX_LED_SCENE_BLE_ADV_START);
    return 0;
}
int test_single_indication_duration(void) {
    for(int scene=0;scene<3;++scene) {
        reset();
        if(scene==0) {
            record.run=RECORD_STATE_START;
            rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_MARK);
        } else {
            battery=scene==1?80:5;
            rdx_led_ctrl_show_battery();
        }
        rdx_led_scene_e before=g_current_scene;
        CHECK(g_active_effect->timeout_ms==2000);
        for(int i=0;i<99;++i) rdx_led_ctrl_update();
        CHECK(g_current_scene==before && g_effect_elapsed_ms==1980);
        rdx_led_ctrl_update();
        CHECK(g_current_scene==(scene==0 ? RDX_LED_SCENE_RECORD_START : RDX_LED_SCENE_OFF));
    }
    return 0;
}
int test_bound_advertising_gate(void) {
    int scenes[]={RDX_LED_SCENE_BLE_ADV_START, RDX_LED_SCENE_BLE_FAST_ADV,
                  RDX_LED_SCENE_BLE_DISCONNECTED, RDX_LED_SCENE_OTA_STOP};
    for(int b=0;b<2;++b) for(int s=0;s<4;++s) {
        reset(); link=0; bound=b;
        rdx_led_ctrl_set_scene(scenes[s]);
        CHECK(g_current_scene==(b ? RDX_LED_SCENE_OFF : scenes[s]));
        CHECK(b ? !rgb.b : rgb.b>0);
    }
    reset(); link=0; bound=1;
    g_led_config=NULL; g_led_initialized=0;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_FAST_ADV);
    CHECK(!wakes);
    reset(); link=0; bound=1;
    CHECK(rdx_led_ctrl_init(&config)==0);
    rdx_led_ctrl_restore_system_state();
    CHECK(g_current_scene==RDX_LED_SCENE_OFF);
    return 0;
}
int test_binding_change_and_restore(void) {
    reset(); link=0;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
    bound=1; rdx_led_ctrl_update();
    CHECK(g_current_scene==RDX_LED_SCENE_OFF && !rgb.b);
    bound=0; rdx_led_ctrl_update();
    CHECK(g_current_scene==RDX_LED_SCENE_BLE_ADV_START);
    for(int guard=0;guard<2;++guard) {
        reset(); bound=g_led_bound_status=1;
        link=guard==0; slow=guard==1;
        bound=0; rdx_led_ctrl_update();
        CHECK(g_current_scene==RDX_LED_SCENE_OFF);
    }
    int prompts[]={RDX_LED_SCENE_KEY1_FEEDBACK,RDX_LED_SCENE_KEY2_FEEDBACK,
                   RDX_LED_SCENE_BATTERY_QUERY,RDX_LED_SCENE_BLE_CONNECTED,
                   RDX_LED_SCENE_BIND_SUCCESS};
    for(int p=0;p<5;++p) {
        reset(); link=0;
        rdx_led_ctrl_set_scene(prompts[p]);
        bound=1;
        rdx_led_ctrl_update();
        CHECK(g_current_scene==prompts[p] && g_effect_elapsed_ms==20);
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_DISCONNECTED);
        CHECK(g_current_scene==prompts[p] && g_effect_elapsed_ms==20);
        for(int i=0;i<250;++i) rdx_led_ctrl_update();
        CHECK(g_current_scene==RDX_LED_SCENE_OFF && !rgb.b);
    }
    reset(); bound=1; link=0; record.run=RECORD_STATE_START;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_MARK);
    for(int i=0;i<100;++i) rdx_led_ctrl_update();
    CHECK(g_current_scene==RDX_LED_SCENE_RECORD_START);
    record.run=RECORD_STATE_STOP;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_STOP);
    CHECK(g_current_scene==RDX_LED_SCENE_OFF);
    return 0;
}
int test_bound_advertising_preserves_business(void) {
    int scenes[]={RDX_LED_SCENE_RECORD_START,RDX_LED_SCENE_RECORD_MARK,
        RDX_LED_SCENE_WIFI_START,RDX_LED_SCENE_OTA_START,
        RDX_LED_SCENE_DUT_ENTER,RDX_LED_SCENE_CHARGE_PLUG_IN,
        RDX_LED_SCENE_USB_SWITCH_WAIT,RDX_LED_SCENE_FINALPACK_DONE};
    for(int s=0;s<8;++s) {
        reset(); bound=1;
        rdx_led_ctrl_set_scene(scenes[s]);
        g_effect_elapsed_ms=100;
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_FAST_ADV);
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_DISCONNECTED);
        CHECK(g_current_scene==scenes[s] && g_effect_elapsed_ms==100);
    }
    reset(); bound=1; record.run=RECORD_STATE_START;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
    CHECK(g_current_scene==RDX_LED_SCENE_RECORD_START);
    reset(); bound=1; charge=RDX_CHARGE_IN;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_DISCONNECTED);
    CHECK(g_current_scene==RDX_LED_SCENE_CHARGE_PLUG_IN);
    reset(); bound=1; ota=1;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_OTA_START);
    ota=0;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_OTA_STOP);
    CHECK(g_current_scene==RDX_LED_SCENE_OFF);
    return 0;
}
'''


def main():
    def source(name):
        return re.sub(r'^#include[^\n]*', '',
                      (BASE / name).read_text(encoding='utf-8'), flags=re.M)
    program = (STUBS + source('rdx_led_ctrl.h') + source('rdx_led_cfg.h') +
               source('rdx_led_ctrl.c') + TESTS)
    with tempfile.TemporaryDirectory(prefix='key-led-') as temp:
        path = Path(temp) / 'key_led.c'
        path.write_text(program, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)
    print('LED engine: key feedback, timeout, arbitration, binding-gated advertising and live restore passed.')


if __name__ == '__main__':
    main()
