"""执行真实灯效引擎，验证按键提示、优先级及绑定后的静默广播。"""
import re
import tempfile
from pathlib import Path
from host_c_test_lib import ROOT, function, run_c_checks

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
#define r_printf(...) (++errors)
#define DUT_LOG(...) ((void)0)
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
#define local_irq_disable() ((void)0)
#define local_irq_enable() ((void)0)
int strcmp(const char *, const char *);
typedef struct { int initialized, run_en; } LedPt0807Config_t;
typedef struct { u8 g,r,b; } LedPt0807Rgb_t;
typedef struct { int run; } RecordStatus;
typedef struct { int onoff; } RdxWifiInfo;
enum { RECORD_STATE_STOP, RECORD_STATE_START, RECORD_STATE_RESUME };
enum { RDX_CHARGE_OUT, RDX_CHARGE_IN, RDX_CHARGE_FULL };
#define TRANSFER_BY_WIFI_ON 1
static struct { int goto_poweroff_flag; } app_var;
static int low_shutdown, usb, charge, ota, dut, transition, slow, hold, wakes, errors;
enum { DUT_FUNC_NONE, DUT_FUNC_OLED, DUT_FUNC_KEY };
static struct { int current_func; } rdx_dut_info;
static int link=1, battery=80, bound;
static u8 rdx_vm_get_bound_status(void) { return bound; }
static RecordStatus record;
static RdxWifiInfo wifi;
static LedPt0807Rgb_t rgb;
static RecordStatus *rdx_record_get_status(void) { return &record; }
static RdxWifiInfo *rdx_app_get_wifi_info(void) { return &wifi; }
static bool rdx_app_get_dut_status(void) { return dut; }
static bool rdx_app_get_dut_oled_flag(void) { return rdx_dut_info.current_func==DUT_FUNC_OLED; }
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
static int restore_allowed;
int rdx_led_hardware_resume(void);
static int rdx_peripheral_power_vdd_ensure_on(int reason) {
    ++wakes;
    return restore_allowed ? rdx_led_hardware_resume() : -1;
}
static void rdx_peripheral_power_vdd_business_changed_notify(void) {}
static int rdx_app_record_is_hold_active(void) { return hold; }
static u32 now_ms;
static int timer_fail, post_fail, callbacks, wrong_owner, spi_wrong_owner;
static const char *task="app_core";
static const char *os_current_task(void) { return task; }
static u32 sys_timer_get_ms(void) { return now_ms; }
typedef struct { int active; u32 due; void (*cb)(void *); void *priv; const char *owner; } Timer;
static Timer timers[32];
static u16 sys_timeout_add_2_task(void *p,void (*cb)(void *),u32 ms,const char *owner) {
    if(timer_fail) return 0;
    for(int i=1;i<32;++i) if(!timers[i].active) {
        timers[i]=(Timer){1,now_ms+ms,cb,p,owner}; return i;
    }
    return 0;
}
static void sys_timeout_del(u16 id) { if(id<32) timers[id].active=0; }
#define Q_CALLBACK 1
static u32 requests[32];
static int request_count;
static int os_taskq_post_type(const char *owner,int kind,int count,int *msg) {
    if(post_fail || request_count==32) return -1;
    if(strcmp(owner,"app_core") || kind!=Q_CALLBACK || count!=3) ++wrong_owner;
    requests[request_count++]=(u32)msg[2]; return 0;
}
static void led_pt0807_set_all_rgb(LedPt0807Config_t *p,LedPt0807Rgb_t *v) { rgb=*v; }
static void led_pt0807_update(LedPt0807Config_t *p) {
    /* 同步硬件初始化允许在文件任务发送黑帧，灯效帧必须在主任务。 */
    if(strcmp(task,"app_core") && (rgb.r || rgb.g || rgb.b)) ++spi_wrong_owner;
}
static void led_pt0807_run_enable(LedPt0807Config_t *p,int en) { p->run_en=en; }
static LedPt0807Config_t led_pt0807_config;
#define LED_PT0807_SPI1 1
#define LED_PT0807_DATA_PORT_IO 1
#define IO_PORT_SPILT(x) (x)
#define PORT_OUTPUT_LOW 0
static void gpio_set_mode(int io,int mode) {}
static int led_pt0807_init(LedPt0807Config_t *p,int spi,int io,int count) { p->initialized=1; return 0; }
static int led_pt0807_deinit(LedPt0807Config_t *p) { p->initialized=0; return 0; }
'''

TESTS = r'''
static LedPt0807Config_t config={1};
static void tick(void) { now_ms+=20; rdx_led_ctrl_update(); }
static int timer_count(void) {
    int count=0; for(int i=1;i<32;++i) count+=timers[i].active; return count;
}
static void drain_requests(void) {
    task="app_core";
    int count=request_count; request_count=0;
    for(int i=0;i<count;++i) _rdx_led_request_cb((void *)requests[i]);
}
static void advance(u32 ms) {
    u32 target=now_ms+ms;
    for(int guard=0;guard<100000;++guard) {
        int selected=0;
        for(int i=1;i<32;++i) if(timers[i].active && (int)(target-timers[i].due)>=0 &&
                (!selected || (int)(timers[selected].due-timers[i].due)>0)) selected=i;
        if(!selected) break;
        Timer event=timers[selected]; timers[selected].active=0;
        now_ms=event.due; task=event.owner;
        if(strcmp(task,"app_core")) { ++wrong_owner; continue; }
        ++callbacks; event.cb(event.priv);
    }
    now_ms=target; task="app_core";
}
static void reset(void) {
    low_shutdown=usb=charge=ota=dut=transition=slow=hold=wakes=errors=0;
    timer_fail=post_fail=callbacks=wrong_owner=spi_wrong_owner=request_count=0;
    restore_allowed=0; task="app_core"; now_ms=0;
    for(int i=0;i<32;++i) timers[i].active=0;
    g_led_update_timer=g_low_battery_timer=0;
    g_led_current_request=g_led_applied_request=g_led_request_serial=g_led_dispatching=0;
    g_led_retry_flags=g_led_retry_request=0;
    led_pt0807_config.initialized=0;
    app_var.goto_poweroff_flag=0; link=1; battery=80;
    bound=g_led_bound_status=0;
    record.run=RECORD_STATE_STOP; wifi.onoff=0;
    g_current_scene=RDX_LED_SCENE_OFF;
    rdx_dut_info.current_func=DUT_FUNC_NONE;
    g_led_config=&config; g_led_initialized=1;
    _rdx_led_apply_effect(RDX_LED_EFFECT_OFF);
}
int test_protocol_restore_has_no_idle_timer(void) {
    reset();
    for(int cycle=0;cycle<12;++cycle) {
        task="app_core"; rdx_led_ctrl_deinit();
        led_pt0807_config.initialized=0;
        task="protocol_recv_task";
        CHECK(rdx_led_hardware_resume()==0);
        CHECK(g_current_scene==RDX_LED_SCENE_OFF && !timer_count());
        CHECK(!g_led_update_timer && !rgb.r && !rgb.g && !rgb.b);
        advance(120000);
        CHECK(!callbacks && !wrong_owner && !spi_wrong_owner);
    }
    /* 硬件恢复与真正的灯效请求分开，协议任务不执行动态 SPI 灯效。 */
    task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    CHECK(request_count==1 && !g_led_update_timer && !rgb.b);
    drain_requests();
    CHECK(rgb.b && timer_count()==1);
    CHECK(!strcmp(timers[g_led_update_timer].owner,"app_core"));
    CHECK(timers[g_led_update_timer].due-now_ms==2000);
    advance(1999); CHECK(!callbacks && rgb.b);
    advance(1);
    CHECK(callbacks==1 && !timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    return 0;
}
int test_dynamic_and_solid_scheduling(void) {
    reset(); record.run=RECORD_STATE_START;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
    CHECK(timer_count()==1 && timers[g_led_update_timer].due==20);
    advance(200); CHECK(callbacks==10 && timer_count()==1 && !wrong_owner);
    record.run=RECORD_STATE_STOP;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_STOP);
    CHECK(!timer_count());
    int scenes[]={RDX_LED_SCENE_OTA_START,RDX_LED_SCENE_CHARGE_FULL,
                  RDX_LED_SCENE_FINALPACK_DONE};
    for(int i=0;i<3;++i) {
        reset(); rdx_led_ctrl_set_scene(scenes[i]);
        CHECK(!timer_count() && (rgb.r || rgb.g || rgb.b));
        advance(120000); CHECK(!callbacks && g_current_scene==scenes[i]);
    }
    reset(); record.run=RECORD_STATE_START; hold=1;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
    CHECK(!timer_count());
    record.run=RECORD_STATE_STOP;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_STOP);
    CHECK(!timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    reset(); rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_CONNECTED);
    CHECK(timer_count()==1 && timers[g_led_update_timer].due==5000);
    advance(5000); CHECK(callbacks==1 && !timer_count());
    return 0;
}
static int rgb_primary(int index) {
    return rgb.r==(index==0?200:0) && rgb.g==(index==1?200:0) &&
           rgb.b==(index==2?200:0);
}
int test_dut_rgb_cycle_and_stop(void) {
    reset(); dut=1;
    rdx_dut_oled_start();
    CHECK(rdx_dut_info.current_func==DUT_FUNC_OLED);
    CHECK(g_active_effect->mode==RDX_LED_MODE_RGB_CYCLE);
    for(int step=0;step<9;++step) {
        CHECK(rgb_primary(step%3) && timer_count()==1);
        CHECK(timers[g_led_update_timer].due-now_ms==1000);
        CHECK(!strcmp(timers[g_led_update_timer].owner,"app_core"));
        advance(999);
        CHECK(rgb_primary(step%3) && callbacks==step);
        advance(1);
        CHECK(rgb_primary((step+1)%3) && callbacks==step+1);
    }
    advance(250);
    u32 start=g_effect_start_ms, due=timers[g_led_update_timer].due;
    rdx_dut_oled_start();
    CHECK(g_effect_start_ms==start && timers[g_led_update_timer].due==due);
    Timer old=timers[g_led_update_timer];
    rdx_dut_oled_stop();
    CHECK(rdx_dut_info.current_func==DUT_FUNC_NONE);
    CHECK(g_current_scene==RDX_LED_SCENE_DUT_ENTER);
    CHECK(g_active_effect->mode==RDX_LED_MODE_BREATH);
    u32 id=g_led_update_timer, generation=g_led_timer_generation;
    old.cb(old.priv);
    CHECK(g_led_update_timer==id && g_led_timer_generation==generation);
    advance(1000);
    CHECK(g_current_scene==RDX_LED_SCENE_DUT_ENTER);
    rdx_dut_oled_start();
    CHECK(rgb_primary(0) && timers[g_led_update_timer].due-now_ms==1000);
    CHECK(!wrong_owner && !spi_wrong_owner);
    reset(); dut=1; rdx_dut_info.current_func=DUT_FUNC_KEY;
    rdx_dut_oled_start();
    CHECK(rdx_dut_info.current_func==DUT_FUNC_KEY && !timer_count());
    CHECK(g_current_scene==RDX_LED_SCENE_OFF);
    return 0;
}
int test_dut_rgb_cycle_delayed_callback_and_wrap(void) {
    reset(); dut=1; now_ms=0xffffff00;
    rdx_dut_oled_start();
    advance(999); CHECK(rgb_primary(0) && !callbacks);
    advance(1); CHECK(rgb_primary(1) && callbacks==1);
    advance(1000); CHECK(rgb_primary(2) && callbacks==2);
    advance(1000); CHECK(rgb_primary(0) && callbacks==3);
    reset(); dut=1; rdx_dut_oled_start();
    Timer old=timers[g_led_update_timer];
    timers[g_led_update_timer].active=0;
    now_ms=2500; old.cb(old.priv);
    CHECK(rgb_primary(2) && timers[g_led_update_timer].due==3000);
    advance(499); CHECK(rgb_primary(2) && !callbacks);
    advance(1); CHECK(rgb_primary(0) && callbacks==1);
    timer_fail=1; advance(1000);
    CHECK(errors==1 && !timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    CHECK(!rgb.r && !rgb.g && !rgb.b);
    return 0;
}
int test_dut_rgb_cycle_charge_and_cleanup(void) {
    for(int b=0;b<2;++b) for(int c=0;c<2;++c) {
        reset(); dut=1; bound=g_led_bound_status=b;
        usb=c; charge=c?RDX_CHARGE_IN:RDX_CHARGE_OUT;
        rdx_dut_oled_start(); advance(1500);
        CHECK(rgb_primary(1));
        u32 start=g_effect_start_ms, due=timers[g_led_update_timer].due;
        usb=1; charge=RDX_CHARGE_IN;
        rdx_led_ctrl_set_charge_state_by_battery(80);
        charge=RDX_CHARGE_FULL;
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_CHARGE_FULL);
        usb=0; charge=RDX_CHARGE_OUT;
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_CHARGE_PLUG_OUT);
        rdx_led_ctrl_restore_system_state();
        CHECK(g_current_scene==RDX_LED_SCENE_DUT_LED_TEST && rgb_primary(1));
        CHECK(g_effect_start_ms==start && timers[g_led_update_timer].due==due);
        advance(500); CHECK(rgb_primary(2));
    }
    for(int exit=0;exit<4;++exit) {
        reset(); dut=1; rdx_dut_oled_start(); advance(1500);
        Timer old=timers[g_led_update_timer];
        if(exit==0) {
            dut=0; rdx_led_ctrl_restore_system_state();
        } else if(exit==1) {
            app_var.goto_poweroff_flag=1;
            rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
        } else if(exit==2) {
            transition=1; rdx_led_ctrl_restore_system_state();
        } else {
            rdx_led_ctrl_deinit();
        }
        CHECK(g_current_scene==(exit==2 ? RDX_LED_SCENE_USB_SWITCH_WAIT : RDX_LED_SCENE_OFF));
        u32 id=g_led_update_timer, generation=g_led_timer_generation;
        old.cb(old.priv);
        CHECK(g_led_update_timer==id && g_led_timer_generation==generation);
        if(exit<2) {
            advance(3000);
            CHECK(!timer_count() && !rgb.r && !rgb.g && !rgb.b);
        }
    }
    return 0;
}
int test_dut_rgb_cycle_preserves_owner_on_ordinary_events(void) {
    int scenes[]={RDX_LED_SCENE_BLE_ADV_START,RDX_LED_SCENE_BLE_FAST_ADV,
        RDX_LED_SCENE_BLE_CONNECTED,RDX_LED_SCENE_BLE_DISCONNECTED,
        RDX_LED_SCENE_LOW_BATTERY,RDX_LED_SCENE_BATTERY_QUERY,
        RDX_LED_SCENE_BIND_SUCCESS};
    for(int b=0;b<2;++b) for(int c=0;c<2;++c) {
        reset(); dut=1; bound=g_led_bound_status=b;
        usb=c; charge=c?RDX_CHARGE_IN:RDX_CHARGE_OUT;
        rdx_dut_oled_start(); advance(1500);
        u32 start=g_effect_start_ms, due=timers[g_led_update_timer].due;
        for(int i=0;i<7;++i) {
            rdx_led_ctrl_set_scene(scenes[i]);
            CHECK(g_current_scene==RDX_LED_SCENE_DUT_LED_TEST && rgb_primary(1));
            CHECK(g_effect_start_ms==start && timers[g_led_update_timer].due==due);
        }
        advance(500); CHECK(rgb_primary(2));
        transition=1; rdx_led_ctrl_restore_system_state();
        CHECK(g_current_scene==RDX_LED_SCENE_USB_SWITCH_WAIT);
        CHECK(rdx_dut_info.current_func==DUT_FUNC_OLED);
        transition=0; rdx_led_ctrl_restore_system_state();
        CHECK(g_current_scene==RDX_LED_SCENE_DUT_LED_TEST && rgb_primary(0));
        CHECK(timers[g_led_update_timer].due-now_ms==1000);
        rdx_dut_oled_stop();
        CHECK(g_current_scene==RDX_LED_SCENE_DUT_ENTER);
    }
    return 0;
}
int test_dut_rgb_cycle_real_exit_order(void) {
    for(int b=0;b<2;++b) for(int c=0;c<3;++c) {
        reset(); dut=1; bound=g_led_bound_status=b;
        usb=c!=RDX_CHARGE_OUT; charge=c;
        rdx_dut_oled_start(); advance(1500);
        Timer old=timers[g_led_update_timer];
        /* Match dut_mode_apply: clear DUT, close LED, then refresh BLE. */
        dut=0;
        rdx_dut_oled_stop();
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
        CHECK(rdx_dut_info.current_func==DUT_FUNC_NONE);
        CHECK(g_current_scene==(c==RDX_CHARGE_IN ? RDX_LED_SCENE_CHARGE_PLUG_IN :
            c==RDX_CHARGE_FULL ? RDX_LED_SCENE_CHARGE_FULL :
            b ? RDX_LED_SCENE_OFF : RDX_LED_SCENE_BLE_ADV_START));
        u32 id=g_led_update_timer, generation=g_led_timer_generation;
        old.cb(old.priv);
        CHECK(g_led_update_timer==id && g_led_timer_generation==generation);
        advance(3000);
        CHECK(g_active_effect->mode!=RDX_LED_MODE_RGB_CYCLE);
    }
    return 0;
}
int test_stale_callbacks_and_reinit(void) {
    reset(); rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    Timer old=timers[g_led_update_timer];
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_MARK);
    u32 generation=g_led_timer_generation, id=g_led_update_timer;
    old.cb(old.priv);
    CHECK(g_led_update_timer==id && g_led_timer_generation==generation && timer_count()==1);
    CHECK(g_current_scene==RDX_LED_SCENE_RECORD_MARK);
    old=timers[id];
    rdx_led_ctrl_deinit();
    task="protocol_recv_task"; CHECK(!rdx_led_ctrl_init(&config));
    task="app_core"; old.cb(old.priv);
    CHECK(!timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    id=g_led_update_timer;
    CHECK(!rdx_led_ctrl_init(&config) && g_led_update_timer==id);
    task="protocol_recv_task"; rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_MARK);
    task="app_core"; rdx_led_ctrl_deinit(); CHECK(!rdx_led_ctrl_init(&config));
    drain_requests(); CHECK(!timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    return 0;
}
int test_deferred_scene_order_and_shutdown(void) {
    reset(); dut=1; task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_LED_TEST);
    task="app_core"; rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_ENTER);
    drain_requests();
    CHECK(g_current_scene==RDX_LED_SCENE_DUT_ENTER && timer_count()==1);
    reset(); dut=1; task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_LED_TEST);
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_ENTER);
    drain_requests();
    CHECK(g_current_scene==RDX_LED_SCENE_DUT_ENTER && timer_count()==1);
    /* 低优先级请求被拒绝时，不吞掉已经排队的真实业务灯效。 */
    reset(); record.run=RECORD_STATE_START; task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
    task="app_core"; rdx_led_ctrl_show_battery();
    drain_requests(); CHECK(g_current_scene==RDX_LED_SCENE_BATTERY_QUERY);
    /* 队列失败后的单次超时可能比后发消息晚到，不能反向覆盖结束状态。 */
    reset(); ota=1; post_fail=1; task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_OTA_START);
    post_fail=0; ota=0; rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
    drain_requests(); advance(1);
    CHECK(g_current_scene==RDX_LED_SCENE_OFF && !timer_count());
    reset(); link=0; task="protocol_recv_task"; rdx_led_ctrl_restore_system_state();
    app_var.goto_poweroff_flag=1; drain_requests();
    CHECK(g_current_scene==RDX_LED_SCENE_OFF && !timer_count());
    reset(); g_led_request_serial=g_led_applied_request=0xfffffe;
    task="protocol_recv_task"; rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    task="app_core"; rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_MARK);
    drain_requests(); CHECK(g_current_scene==RDX_LED_SCENE_RECORD_MARK);
    task="protocol_recv_task"; rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
    drain_requests(); CHECK(g_current_scene==RDX_LED_SCENE_OFF && !timer_count());
    return 0;
}
int test_real_deadlines_and_wrap(void) {
    reset(); now_ms=0xffffff00;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    advance(1999); CHECK(rgb.b && !callbacks);
    advance(1); CHECK(!rgb.b && callbacks==1 && !timer_count());
    reset(); rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    Timer old=timers[g_led_update_timer]; timers[g_led_update_timer].active=0;
    now_ms=3500; old.cb(old.priv);
    CHECK(g_current_scene==RDX_LED_SCENE_OFF && !timer_count());
    return 0;
}
int test_low_battery_owner_and_lifecycle(void) {
    reset(); battery=5; task="protocol_recv_task";
    rdx_led_ctrl_update_low_battery(); CHECK(request_count==1 && !timer_count());
    drain_requests();
    CHECK(g_low_battery_timer && g_led_update_timer && timer_count()==2);
    CHECK(!strcmp(timers[g_low_battery_timer].owner,"app_core"));
    advance(2000); CHECK(timer_count()==1 && g_current_scene==RDX_LED_SCENE_OFF);
    rdx_led_ctrl_deinit(); restore_allowed=1;
    advance(598000);
    CHECK(wakes==1 && timer_count()==2 && g_current_scene==RDX_LED_SCENE_LOW_BATTERY);
    Timer old=timers[g_low_battery_timer];
    battery=80; rdx_led_ctrl_update_low_battery();
    CHECK(!timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    battery=5; rdx_led_ctrl_update_low_battery();
    u32 id=g_low_battery_timer, generation=g_low_battery_generation;
    old.cb(old.priv);
    CHECK(g_low_battery_timer==id && g_low_battery_generation==generation);
    usb=1; charge=RDX_CHARGE_IN; rdx_led_ctrl_update_low_battery();
    CHECK(!g_low_battery_timer && g_current_scene==RDX_LED_SCENE_CHARGE_PLUG_IN);
    reset(); battery=5; ota=1;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_OTA_START);
    rdx_led_ctrl_update_low_battery(); advance(600000);
    CHECK(g_current_scene==RDX_LED_SCENE_OTA_START && timer_count()==1);
    CHECK(!wrong_owner && !spi_wrong_owner);
    return 0;
}
int test_binding_notification_without_tick(void) {
    reset(); link=0; bound=g_led_bound_status=1;
    CHECK(!timer_count());
    bound=0; task="protocol_recv_task"; rdx_led_ctrl_bound_changed();
    CHECK(request_count==1 && !timer_count());
    drain_requests();
    CHECK(g_current_scene==RDX_LED_SCENE_BLE_ADV_START && timer_count()==1);
    bound=1; task="protocol_recv_task"; rdx_led_ctrl_bound_changed(); drain_requests();
    CHECK(g_current_scene==RDX_LED_SCENE_OFF && !timer_count());
    reset(); bound=1; g_led_bound_status=1; slow=1; link=0;
    bound=0; rdx_led_ctrl_bound_changed(); CHECK(!timer_count() && !wakes);
    return 0;
}
int test_timer_and_dispatch_failure(void) {
    reset(); timer_fail=1;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    CHECK(errors==1 && !timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    CHECK(!rgb.r && !rgb.g && !rgb.b);
    reset(); record.run=RECORD_STATE_START;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
    timer_fail=1; advance(20);
    CHECK(!timer_count() && errors==1 && g_current_scene==RDX_LED_SCENE_OFF);
    reset(); timer_fail=1; usb=1; charge=RDX_CHARGE_IN;
    rdx_led_ctrl_set_charge_state_by_battery(80);
    CHECK(!timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    reset(); post_fail=1; task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    CHECK(timer_count()==1 && !request_count && !rgb.b);
    advance(1); CHECK(rgb.b && timer_count()==1 && !wrong_owner);
    advance(2000); CHECK(!timer_count());
    reset(); post_fail=timer_fail=1; task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    CHECK(errors==1 && !timer_count() && !request_count && !rgb.b && !spi_wrong_owner);
    CHECK(rdx_led_ctrl_needs_service());
    task="app_core"; post_fail=timer_fail=0; rdx_led_ctrl_service();
    CHECK(!rdx_led_ctrl_needs_service() && timer_count()==1 && rgb.b);
    /* 双重投递失败时，结束事件仍能停止旧动态灯效。 */
    reset(); record.run=RECORD_STATE_START;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
    record.run=RECORD_STATE_STOP; post_fail=timer_fail=1; task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_STOP);
    CHECK(rdx_led_ctrl_needs_service());
    task="app_core"; rdx_led_ctrl_service();
    CHECK(!rdx_led_ctrl_needs_service() && !timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    reset(); post_fail=timer_fail=1; task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
    task="app_core"; timer_fail=0; rdx_led_ctrl_service();
    CHECK(!rdx_led_ctrl_needs_service() && !timer_count() && g_current_scene==RDX_LED_SCENE_OFF);
    /* 已接受的新场景使恢复槽中的旧请求失效。 */
    reset(); post_fail=timer_fail=1; task="protocol_recv_task";
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    task="app_core"; timer_fail=0; rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_MARK);
    rdx_led_ctrl_service();
    CHECK(!rdx_led_ctrl_needs_service() && g_current_scene==RDX_LED_SCENE_RECORD_MARK);
    reset(); bound=g_led_bound_status=1; link=0; battery=5;
    post_fail=timer_fail=1; task="protocol_recv_task";
    bound=0; rdx_led_ctrl_bound_changed(); rdx_led_ctrl_update_low_battery();
    CHECK((g_led_retry_flags & 6)==6);
    task="app_core"; timer_fail=0; rdx_led_ctrl_service();
    CHECK(!rdx_led_ctrl_needs_service() && g_low_battery_timer &&
          g_current_scene==RDX_LED_SCENE_LOW_BATTERY);
    reset(); battery=5; timer_fail=1; rdx_led_ctrl_update_low_battery();
    CHECK(!g_low_battery_timer && errors==1);
    timer_fail=0; rdx_led_ctrl_update_low_battery(); CHECK(g_low_battery_timer);
    return 0;
}
int test_feedback_color_and_deadline(void) {
    const int scenes[]={RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK,RDX_LED_SCENE_RECORD_MARK,
        RDX_LED_SCENE_PLAYBACK_PLAY,RDX_LED_SCENE_PLAYBACK_PAUSE};
    for(int i=0;i<4;++i) {
        reset(); rdx_led_ctrl_set_scene(scenes[i]);
        CHECK(rgb.r==(i?255:181) && rgb.g==(i?160:82) && rgb.b==(i?0:255));
        advance(1999); rdx_led_ctrl_show_battery();
        CHECK(g_current_scene==scenes[i]);
        advance(1);
        CHECK(g_current_scene==RDX_LED_SCENE_OFF && !rgb.r && !rgb.g && !rgb.b && !timer_count());
    }
    return 0;
}
int test_key_feedback_protected_scenes(void) {
    for(int guard=2;guard<12;++guard) {
        reset();
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
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
        CHECK(g_current_scene==before && g_effect_elapsed_ms==100 && !wakes);
    }
    return 0;
}
int test_key_feedback_live_restore(void) {
    for(int next=0;next<6;++next) {
        reset(); rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
        if(next==0) { record.run=RECORD_STATE_START; hold=1; }
        if(next==1) wifi.onoff=TRANSFER_BY_WIFI_ON;
        if(next==2) ota=1;
        if(next==3) dut=1;
        if(next==4) { charge=RDX_CHARGE_IN; usb=1; }
        if(next==5) transition=1;
        if(next==0) { advance(2000); } else { tick(); }
        int expected[]={RDX_LED_SCENE_RECORD_START,RDX_LED_SCENE_WIFI_START,
            RDX_LED_SCENE_OTA_START,RDX_LED_SCENE_DUT_ENTER,
            RDX_LED_SCENE_CHARGE_PLUG_IN,RDX_LED_SCENE_USB_SWITCH_WAIT};
        CHECK(g_current_scene==expected[next]);
    }
    reset(); rdx_led_ctrl_set_scene(RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
    link=0;
    for(int i=0;i<100;++i) tick();
    CHECK(g_current_scene==RDX_LED_SCENE_BLE_ADV_START);
    return 0;
}
int test_battery_and_recording_kind_restore(void) {
    for(int low=0;low<2;++low) {
        reset(); battery=low?5:80; rdx_led_ctrl_show_battery();
        advance(1999); CHECK(g_current_scene==RDX_LED_SCENE_BATTERY_QUERY);
        advance(1); CHECK(g_current_scene==RDX_LED_SCENE_OFF);
    }
    for(int inspiration=0;inspiration<2;++inspiration) {
        reset(); record.run=RECORD_STATE_START; hold=inspiration;
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
        for(int color=0;color<2;++color) {
            rdx_led_ctrl_set_scene(color?RDX_LED_SCENE_RECORD_MARK:RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK);
            CHECK(_rdx_led_is_key_overlay_scene(g_current_scene));
            advance(2000);
            CHECK(g_current_scene==RDX_LED_SCENE_RECORD_START);
            CHECK(g_active_effect->mode==(inspiration?RDX_LED_MODE_SOLID:RDX_LED_MODE_BREATH));
        }
        rdx_led_ctrl_show_battery(); CHECK(g_current_scene==RDX_LED_SCENE_BATTERY_QUERY);
        advance(2000); CHECK(g_current_scene==RDX_LED_SCENE_RECORD_START);
    }
    return 0;
}
int test_record_mark_deadline_and_restoration(void) {
    for(int deferred=0;deferred<2;++deferred) for(int charging=0;charging<2;++charging) {
        reset(); record.run=RECORD_STATE_START;
        if(charging) { usb=1; charge=RDX_CHARGE_IN; }
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
        advance(100);
        if(deferred) task="protocol_recv_task";
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_MARK);
        if(deferred) drain_requests();
        CHECK(g_current_scene==RDX_LED_SCENE_RECORD_MARK);
        advance(200);
        CHECK(g_current_scene==RDX_LED_SCENE_RECORD_MARK && rgb.r==255 && rgb.g==160 && !rgb.b);
        rdx_led_ctrl_show_battery();
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_START);
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_DISCONNECTED);
        rdx_led_ctrl_restore_system_state();
        advance(1799);
        CHECK(g_current_scene==RDX_LED_SCENE_RECORD_MARK);
        advance(1);
        CHECK(g_current_scene==RDX_LED_SCENE_RECORD_START);
        CHECK(g_active_effect->mode==RDX_LED_MODE_BREATH && timer_count()==1);
    }
    /* 录音已停止时不锁住旧标记；关机及存储切换也必须能够抢占。 */
    for(int next=0;next<3;++next) {
        reset(); record.run=RECORD_STATE_START;
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_MARK);
        advance(200);
        if(next==0) {
            record.run=RECORD_STATE_STOP;
            rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_STOP);
        } else if(next==1) {
            app_var.goto_poweroff_flag=1;
            rdx_led_ctrl_set_scene(RDX_LED_SCENE_OFF);
        } else {
            transition=1;
            rdx_led_ctrl_restore_system_state();
        }
        CHECK(g_current_scene==(next==2 ? RDX_LED_SCENE_USB_SWITCH_WAIT : RDX_LED_SCENE_OFF));
        if(next<2) CHECK(!timer_count() && !rgb.r && !rgb.g && !rgb.b);
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
    bound=1; tick();
    CHECK(g_current_scene==RDX_LED_SCENE_OFF && !rgb.b);
    bound=0; tick();
    CHECK(g_current_scene==RDX_LED_SCENE_BLE_ADV_START);
    for(int guard=0;guard<2;++guard) {
        reset(); bound=g_led_bound_status=1;
        link=guard==0; slow=guard==1;
        bound=0; tick();
        CHECK(g_current_scene==RDX_LED_SCENE_OFF);
    }
    int prompts[]={RDX_LED_SCENE_KEY_CUSTOM_FEEDBACK,RDX_LED_SCENE_RECORD_MARK,
                   RDX_LED_SCENE_BATTERY_QUERY,RDX_LED_SCENE_BLE_CONNECTED,
                   RDX_LED_SCENE_BIND_SUCCESS};
    for(int p=0;p<5;++p) {
        reset(); link=0;
        rdx_led_ctrl_set_scene(prompts[p]);
        bound=1;
        tick();
        CHECK(g_current_scene==prompts[p] && g_effect_elapsed_ms==20);
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_DISCONNECTED);
        CHECK(g_current_scene==prompts[p] && g_effect_elapsed_ms==20);
        for(int i=0;i<250;++i) tick();
        CHECK(g_current_scene==RDX_LED_SCENE_OFF && !rgb.b);
    }
    reset(); bound=1; link=0; record.run=RECORD_STATE_START;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_RECORD_MARK);
    for(int i=0;i<100;++i) tick();
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
        if(s==0 || s==1) record.run=RECORD_STATE_START;
        if(s==2) wifi.onoff=TRANSFER_BY_WIFI_ON;
        if(s==3) ota=1;
        if(s==4) dut=1;
        if(s==5) charge=RDX_CHARGE_IN;
        if(s==6) transition=1;
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
int test_bound_advertising_clears_finished_business(void) {
    int scenes[]={RDX_LED_SCENE_DUT_ENTER,RDX_LED_SCENE_DUT_LED_TEST,
        RDX_LED_SCENE_OTA_START,RDX_LED_SCENE_WIFI_START,
        RDX_LED_SCENE_RECORD_START,RDX_LED_SCENE_CHARGE_PLUG_IN,
        RDX_LED_SCENE_CHARGE_FULL,RDX_LED_SCENE_USB_SWITCH_WAIT};
    int events[]={RDX_LED_SCENE_BLE_ADV_START,RDX_LED_SCENE_BLE_FAST_ADV,
        RDX_LED_SCENE_BLE_DISCONNECTED,RDX_LED_SCENE_OTA_STOP};
    for(int s=0;s<8;++s) for(int e=0;e<4;++e) {
        reset(); link=0; bound=1;
        rdx_led_ctrl_set_scene(scenes[s]);
        rdx_led_ctrl_set_scene(events[e]);
        CHECK(g_current_scene==RDX_LED_SCENE_OFF);
        CHECK(g_active_effect->mode==RDX_LED_MODE_OFF);
        CHECK(!rgb.r && !rgb.g && !rgb.b);
        for(int i=0;i<100;++i) tick();
        CHECK(g_current_scene==RDX_LED_SCENE_OFF && !rgb.r && !rgb.g && !rgb.b);
    }
    /* 模拟 DUT 真实退出顺序：先清除业务状态，再请求广播场景。 */
    for(int b=0;b<2;++b) for(int c=0;c<3;++c) {
        reset(); link=0; bound=b; dut=1;
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_ENTER);
        dut=0; charge=c; usb=c!=RDX_CHARGE_OUT;
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
        CHECK(g_current_scene==(c==RDX_CHARGE_IN ? RDX_LED_SCENE_CHARGE_PLUG_IN :
            c==RDX_CHARGE_FULL ? RDX_LED_SCENE_CHARGE_FULL :
            b ? RDX_LED_SCENE_OFF : RDX_LED_SCENE_BLE_ADV_START));
    }
    reset(); bound=1; dut=1;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_LED_TEST);
    g_effect_elapsed_ms=100;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
    CHECK(g_current_scene==RDX_LED_SCENE_DUT_LED_TEST && g_effect_elapsed_ms==100);
    return 0;
}
'''


def main():
    def source(name):
        return re.sub(r'^#include[^\n]*', '',
                      (BASE / name).read_text(encoding='utf-8'), flags=re.M)
    led_source = source('rdx_led_ctrl.c')
    assert not re.search(r'\bsys_timer_add\s*\(|\bsys_timeout_add\s*\(', led_source)
    vm_source = (BASE / 'rdx_vm.c').read_text(encoding='utf-8')
    bound_write = function(vm_source, 'rdx_vm_set_bound_status')
    assert bound_write.index('rdx_led_ctrl_bound_changed();') > bound_write.rindex('rdx_vm_bound_transition_unlock();')
    dip_source = (BASE / 'rdx_dip_switch.c').read_text(encoding='utf-8')
    assert 'rdx_led_ctrl_needs_service()' in function(dip_source, 'rdx_dip_switch_recovery_needed')
    assert 'rdx_led_ctrl_service();' in function(dip_source, 'rdx_dip_switch_deferred_handle')
    dut_source = source('rdx_dut.c')
    program = (STUBS + source('rdx_led_ctrl.h') + source('rdx_led_cfg.h') +
               led_source + function(source('rdx_app.c'), 'rdx_led_hardware_resume') +
               ''.join(function(dut_source, name) for name in (
                   'rdx_dut_show', 'rdx_dut_oled_start', 'rdx_dut_oled_stop')) + TESTS)
    with tempfile.TemporaryDirectory(prefix='key-led-') as temp:
        path = Path(temp) / 'key_led.c'
        path.write_text(program, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)
    print('LED engine: DUT single-channel RGB cycle/cleanup, task-owned demand scheduling, real deadlines, stale callbacks, restore, failure paths, key feedback and arbitration passed.')


if __name__ == '__main__':
    main()
