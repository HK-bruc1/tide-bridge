#include "app_config.h"
#include "rdx_dip_switch.h"
#include "usb/device/usb_factory.h"

#if TCFG_DIP_SWITCH_POWER_ENABLE
#include "system/includes.h"
#include "poweroff.h"
#include "app_main.h"
#include "app_msg.h"
#include "idle.h"
#include "asm/charge.h"
#include "usb/otg.h"
#include "rdx_app.h"
#include "rdx_storage_lifecycle.h"
#include "rdx_factory_cdc.h"
#include "rdx_hogp_input.h"
#include "rdx_dut.h"
#include "rdx_adv_policy.h"
#include "rdx_led_ctrl.h"
#include "rdx_ble_server.h"

static volatile bool s_init_done;
static volatile bool s_business_mode_entered;
static volatile u8 s_shutdown_pending;
static volatile u8 s_irq_pending;
static volatile u8 s_debounce_restart;
static volatile int s_stable_on = -1;
static int s_sample_on = -1;
static u32 s_sample_since;
static volatile u32 s_shutdown_since;
static volatile u8 s_work_pending, s_work_posted, s_work_failed;
static u16 s_work_timer;
static u32 s_work_serial;
static volatile u32 s_work_due;
static int s_last_on = -1;
static volatile u32 s_last_usb = (u32)-1;
static volatile int s_last_vbus = -1;
static u8 s_attempts, s_retry_wait;
static u32 s_retry_due;
#define DIP_SERVICE_MAX_ATTEMPTS 3
#define DIP_SERVICE_RETRY_MS 1000

static void rdx_dip_switch_deferred_handle(void *priv);

/* 只查询已发布的轻量状态；中断入口不执行协议、文件或模式切换。 */
static int rdx_dip_switch_recovery_needed(void)
{
    return rdx_dut_service_needs_poll() || rdx_factory_cdc_needs_poll() ||
           rdx_hogp_input_needs_poll() || rdx_storage_lifecycle_changed() ||
           rdx_led_ctrl_needs_service() || rdx_ble_server_conn_param_needs_service();
}

static u8 rdx_dip_switch_idle(void)
{
    /* 保留正常 ON 状态的休眠，但接受 OFF 后或处于中断消抖窗口时
     * 禁止休眠，以保证优先级为 0 的 usr_timer 按周期运行。 */
    return !s_shutdown_pending && !s_irq_pending && !s_work_pending &&
           !s_work_failed && !rdx_dip_switch_recovery_needed() &&
           s_stable_on == get_power_on_status() &&
           s_last_usb == usb_otg_online(0) && s_last_vbus == get_charge_online_flag() &&
           (get_power_on_status() || (!s_business_mode_entered && get_charge_online_flag()));
}

REGISTER_LP_TARGET(rdx_dip_switch_lp_target) = {
    .name = "rdx_dip",
    .is_idle = rdx_dip_switch_idle,
};

/* 硬件定时器上下文：仅采样 GPIO 和调用 SDK 的 P33 复位接口。
 * 此回调不操作 FAT、输出日志、投递任务队列或销毁蓝牙栈。
 * 拨回 ON、重复请求或 USB 状态变化均不得延长截止期限。
 * 长时间屏蔽中断或硬件故障仍需平台看门狗兜底。 */
static void rdx_dip_switch_guard(void *priv)
{
    (void)priv;
    local_irq_disable();
    u32 now = sys_timer_get_ms();
    if (!s_shutdown_pending) {
        int on = get_power_on_status();
        if (s_debounce_restart || on != s_sample_on) {
            s_debounce_restart = 0;
            s_sample_on = on;
            s_sample_since = now;
            s_irq_pending = 1;
        } else if (s_irq_pending && (u32)(now - s_sample_since) >= 100) {
            s_stable_on = on;
            s_irq_pending = 0;
            p33_io_wakeup_edge(TCFG_DIP_SWITCH_POWER_IO,
                               on ? RISING_EDGE : FALLING_EDGE);
            s_work_pending = 1;
        }
        if (!s_irq_pending && s_stable_on == 0 &&
            (s_business_mode_entered || !get_charge_online_flag())) {
            rdx_dip_switch_shutdown_begin();
        }
    }
    int expired = s_shutdown_pending && (u32)(now - s_shutdown_since) >= 3000;
    local_irq_enable();
    if (expired) {
        cpu_reset();
    }
}

void rdx_dip_switch_shutdown_begin(void)
{
    local_irq_disable();
    if (!s_shutdown_pending) {
        s_shutdown_since = sys_timer_get_ms();
        s_shutdown_pending = 1;
        s_work_pending = 1;
    }
    local_irq_enable();
}

int rdx_dip_switch_shutdown_pending(void)
{
    return s_shutdown_pending;
}


void rdx_dip_switch_note_business_mode(void)
{
    s_business_mode_entered = true;
    rdx_dip_switch_wake();
}

int rdx_dip_switch_cold_service(void)
{
    return !s_business_mode_entered && !rdx_app_business_started();
}
/* OFF 冷 USB 服务不依赖蓝牙初始化。 */
int rdx_dip_switch_pc_allowed(void)
{
#if TCFG_T2620_PC_STORAGE_ENABLE
    return !get_power_on_status() && get_charge_online_flag() &&
           usb_otg_online(0) == SLAVE_MODE &&
           rdx_dip_switch_cold_service() && !app_var.goto_poweroff_flag;
#else
    return false;
#endif
}

void rdx_dip_switch_wake(void)
{
    s_work_pending = 1;
}

static void rdx_dip_switch_timeout(void *priv)
{
    /* 撤销超时无法撤回已入队的旧回调，必须核对代次。 */
    if ((u32)priv != s_work_serial) return;
    s_work_timer = 0;
    s_work_failed = 0;
    rdx_dip_switch_wake();
    rdx_dip_switch_deferred_handle(NULL);
}

static void rdx_dip_switch_schedule(u32 delay)
{
    ++s_work_serial;
    if (s_work_timer) sys_timeout_del(s_work_timer);
    s_work_timer = 0;
    s_work_failed = 0;
    if (!delay) return;
    s_work_due = sys_timer_get_ms() + delay;
    s_work_timer = sys_timeout_add((void *)s_work_serial, rdx_dip_switch_timeout, delay);
    s_work_failed = !s_work_timer;
}

static u32 rdx_dip_switch_mode_delay(int on, int vbus, u32 usb)
{
    u32 now = sys_timer_get_ms();
    if (on != s_last_on || usb != s_last_usb || vbus != s_last_vbus) {
        /* 复用现有电源边沿观察，USB 插拔使连接空闲窗口作废，不增加轮询。 */
        rdx_adv_policy_activity();
        if (rdx_dut_test_keys_active()) rdx_dut_service_wake();
        s_attempts = 0;
        s_retry_wait = 0;
    }
    s_last_on = on;
    s_last_usb = usb;
    s_last_vbus = vbus;
    int target = -1;
    if (on) {
        /* PC 退出必须先停止 USB 并归还 SD，再允许启动蓝牙。 */
        if (app_in_mode(APP_MODE_PC) || app_in_mode(APP_MODE_IDLE)) target = APP_MODE_BT;
    } else if (rdx_dip_switch_pc_allowed()) {
        if (!app_in_mode(APP_MODE_PC)) target = APP_MODE_PC;
    } else if (app_in_mode(APP_MODE_PC)) {
        target = APP_MODE_IDLE | (IDLE_MODE_CHARGE << 8);
    }
    if (target < 0) {
        s_retry_wait = 0;
        return 0;
    }
    if (s_attempts >= DIP_SERVICE_MAX_ATTEMPTS) return 0;
    if (s_retry_wait && (int)(now - s_retry_due) < 0) return s_retry_due - now;
    s_retry_wait = 0;
    if (target >= 0) {
        ++s_attempts;
        s_retry_due = now + DIP_SERVICE_RETRY_MS;
        s_retry_wait = s_attempts < DIP_SERVICE_MAX_ATTEMPTS;
        r_printf("[DIP] request mode=%d attempt=%d/%d\n", target,
                 s_attempts, DIP_SERVICE_MAX_ATTEMPTS);
        app_send_message(APP_MSG_GOTO_MODE, target);
        return s_retry_wait ? DIP_SERVICE_RETRY_MS : 0;
    }
    return 0;
}

static void rdx_dip_switch_deferred_handle(void *priv)
{
    (void)priv;
    local_irq_disable();
    s_work_pending = 0;
    local_irq_enable();
    rdx_dip_switch_schedule(0);
    if (app_var.goto_poweroff_flag) return;
    int on = s_stable_on;
    int vbus = get_charge_online_flag();
    if (s_shutdown_pending) {
        app_send_message(APP_MSG_REQUEST_POWEROFF, POWEROFF_NORMAL);
        /* 原生消息接口不返回投递结果；有界保护期间重试，不等待存储许可。 */
        rdx_dip_switch_schedule(100);
        return;
    }
    if (s_irq_pending || on < 0 || on != get_power_on_status()) return;
    if (!on && (!rdx_dip_switch_cold_service() || !vbus)) {
        rdx_dip_switch_shutdown_begin();
        app_send_message(APP_MSG_REQUEST_POWEROFF, POWEROFF_NORMAL);
        rdx_dip_switch_schedule(100);
        return;
    }
    u32 delay = rdx_dip_switch_mode_delay(on, vbus, usb_otg_online(0));
    u32 due = s_retry_due;
    u8 scheduled = delay != 0;
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
    usb_factory_service();
    u32 usb_delay = usb_factory_service_delay();
    u32 usb_due = sys_timer_get_ms() + usb_delay;
    if (usb_delay && (!scheduled || (int)(usb_due - due) < 0)) {
        due = usb_due;
        scheduled = 1;
    }
#endif
    rdx_dut_service_poll();
    rdx_factory_cdc_poll();
    rdx_hogp_input_poll();
    rdx_led_ctrl_service();
    rdx_ble_server_conn_param_service();
    if (rdx_storage_lifecycle_service(on, vbus)) {
        u32 storage_due = sys_timer_get_ms() + 100;
        if (!scheduled || (int)(storage_due - due) < 0) due = storage_due;
        scheduled = 1;
    }
    /* 其他收尾耗时不能把已经选定的模式/USB 重试期限向后平移。 */
    u32 now = sys_timer_get_ms();
    rdx_dip_switch_schedule(scheduled ? ((int)(due - now) > 0 ? due - now : 1) : 0);
}

static void rdx_dip_switch_event(void *priv)
{
    local_irq_disable();
    s_work_posted = 0;
    local_irq_enable();
    rdx_dip_switch_deferred_handle(NULL);
}

/* 与独立保护分开：普通硬件定时器上下文只合并通知，不执行任何业务。
 * 优先级 0 不主动唤醒；稳定态不投递空任务，失败待办由低功耗锁保证恢复。 */
static void rdx_dip_switch_recovery_tick(void *priv)
{
    if (!s_init_done || app_var.goto_poweroff_flag || s_work_posted) return;
    if (!s_shutdown_pending && s_irq_pending) return;
    if (!s_work_pending &&
        !(s_work_failed && (int)(sys_timer_get_ms() - s_work_due) >= 0) &&
        s_last_usb == usb_otg_online(0) && s_last_vbus == get_charge_online_flag() &&
        !rdx_dip_switch_recovery_needed()) return;
    int message[] = {(int)rdx_dip_switch_event, 1, 0};
    local_irq_disable();
    s_work_pending = 1;
    s_work_posted = 1;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, message)) {
        s_work_posted = 0;
    }
    local_irq_enable();
}

void rdx_dip_switch_p33_irq(P33_IO_WKUP_EDGE edge)
{
    /* P33 上下文中不操作任务队列、文件系统或销毁协议栈。 */
    (void)edge;
    s_irq_pending = 1;
    s_debounce_restart = 1;
}

void rdx_dip_switch_init(void)
{
    if (s_init_done) {
        rdx_dip_switch_wake();
        return;
    }
    gpio_set_mode(IO_PORT_SPILT(TCFG_DIP_SWITCH_POWER_IO), PORT_INPUT_PULLUP_10K);
    p33_io_wakeup_edge(TCFG_DIP_SWITCH_POWER_IO,
                      get_power_on_status() ? RISING_EDGE : FALLING_EDGE);
    /* 独立于 app_core 运行，覆盖启动或存储回调阻塞的情况。 */
    u16 guard = usr_timer_add(NULL, rdx_dip_switch_guard, 100, 0);
    ASSERT(guard != 0);
    u16 recovery = usr_timer_add(NULL, rdx_dip_switch_recovery_tick, 100, 0);
    ASSERT(recovery != 0);
    s_irq_pending = s_debounce_restart = 1;
    s_init_done = true;
}
#endif
