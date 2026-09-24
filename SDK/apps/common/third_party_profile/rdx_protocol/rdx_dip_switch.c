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

static bool s_init_done;
static volatile bool s_business_mode_entered;
static volatile u8 s_shutdown_ticks;
static volatile u8 s_guard_off_samples;
static volatile u8 s_shutdown_pending;
static volatile u8 s_irq_pending;

static u8 rdx_dip_switch_idle(void)
{
    /* 保留正常 ON 状态的休眠，但接受 OFF 后或处于中断消抖窗口时
     * 禁止休眠，以保证优先级为 0 的 usr_timer 按周期运行。 */
    return !s_shutdown_pending && !s_irq_pending &&
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
    if (!s_shutdown_pending) {
        if (get_power_on_status()) {
            s_guard_off_samples = 0;
            s_irq_pending = 0;
        } else if (++s_guard_off_samples >= 2) {
            s_guard_off_samples = 2;
            s_irq_pending = 0;
            if (s_business_mode_entered || !get_charge_online_flag()) {
                s_shutdown_pending = 1;
            }
        }
    }
    if (s_shutdown_pending && ++s_shutdown_ticks >= 30) {
        cpu_reset();
    }
}

void rdx_dip_switch_shutdown_begin(void)
{
    s_shutdown_pending = 1;
}

int rdx_dip_switch_shutdown_pending(void)
{
    return s_shutdown_pending;
}


void rdx_dip_switch_note_business_mode(void)
{
    s_business_mode_entered = true;
}

int rdx_dip_switch_cold_service(void)
{
    return !s_business_mode_entered && !rdx_app_business_started();
}
static int s_last_on = -1;
static int s_sample_on = -1;
static u32 s_last_usb = (u32)-1;
static int s_last_vbus = -1;
/* Bounded reconciliation also covers rejected entry and asynchronous MSC
 * startup failure. Input changes open a new attempt window. */
static u8 s_attempts;
static u8 s_retry_ticks;
#define DIP_SERVICE_MAX_ATTEMPTS 3
#define DIP_SERVICE_RETRY_TICKS 10

/* OFF USB service never requires BLE initialization. */
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

static void rdx_dip_switch_deferred_handle(void *priv)
{
    int on = get_power_on_status();
    u32 usb = usb_otg_online(0);
    int vbus = get_charge_online_flag();
    (void)priv;
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
    /* Runs even when the DIP/PC service is waiting, retrying or failing. */
    usb_factory_service();
#endif
    /* Require two matching samples before reacting to a mechanical edge. */
    if (on != s_sample_on) {
        s_sample_on = on;
        return;
    }
    p33_io_wakeup_edge(TCFG_DIP_SWITCH_POWER_IO,
                       on ? RISING_EDGE : FALLING_EDGE);
    if (on != s_last_on || usb != s_last_usb || vbus != s_last_vbus) {
        s_attempts = 0;
        s_retry_ticks = 0;
    }
    s_last_on = on;
    s_last_usb = usb;
    s_last_vbus = vbus;
    if (app_var.goto_poweroff_flag) {
        return;
    }
    if (s_shutdown_pending || (!on && (!rdx_dip_switch_cold_service() || !vbus))) {
        rdx_dip_switch_shutdown_begin();
        /* 关机一旦接受，即使用户拨回 ON 也须完成退出流程。 */
        app_send_message(APP_MSG_REQUEST_POWEROFF, POWEROFF_NORMAL);
        return;
    }
    rdx_storage_lifecycle_service(on, vbus);
    if (s_retry_ticks) {
        --s_retry_ticks;
        return;
    }
    if (s_attempts >= DIP_SERVICE_MAX_ATTEMPTS) {
        return;
    }
    /* Count requests, never pretend an enqueued transition has succeeded. */
    int target = -1;
    if (on) {
        /* PC try_exit must stop USB and restore SD before BT starts. */
        if (app_in_mode(APP_MODE_PC) || app_in_mode(APP_MODE_IDLE)) {
            target = APP_MODE_BT;
        }
    } else if (!rdx_dip_switch_cold_service() || !vbus) {
        /* BLE exit alone cannot drain library workers. Only a fresh boot may
         * export; retain the normal shutdown path for an existing business. */
        app_send_message(APP_MSG_REQUEST_POWEROFF, POWEROFF_NORMAL);
        ++s_attempts;
        s_retry_ticks = DIP_SERVICE_RETRY_TICKS;
    } else if (rdx_dip_switch_pc_allowed()) {
        if (!app_in_mode(APP_MODE_PC)) {
            r_printf("[DIP] OFF + confirmed host -> PC, BLE disabled\n");
            target = APP_MODE_PC;
        }
    } else if (app_in_mode(APP_MODE_PC)) {
        target = APP_MODE_IDLE | (IDLE_MODE_CHARGE << 8);
    }
    if (target >= 0) {
        ++s_attempts;
        s_retry_ticks = DIP_SERVICE_RETRY_TICKS;
        r_printf("[DIP] request mode=%d attempt=%d/%d\n", target,
                 s_attempts, DIP_SERVICE_MAX_ATTEMPTS);
        app_send_message(APP_MSG_GOTO_MODE, target);
        if (s_attempts == DIP_SERVICE_MAX_ATTEMPTS) {
            r_printf("[DIP] final automatic attempt; input change permits retry\n");
        }
    }
}

void rdx_dip_switch_p33_irq(P33_IO_WKUP_EDGE edge)
{
    /* P33 上下文中不操作任务队列、文件系统或销毁协议栈。 */
    (void)edge;
    s_irq_pending = 1;
}

void rdx_dip_switch_init(void)
{
    if (s_init_done) {
        return;
    }
    gpio_set_mode(IO_PORT_SPILT(TCFG_DIP_SWITCH_POWER_IO), PORT_INPUT_PULLUP_10K);
    p33_io_wakeup_edge(TCFG_DIP_SWITCH_POWER_IO,
                      get_power_on_status() ? RISING_EDGE : FALLING_EDGE);
    /* Also observes OTG identification after cold boot. Each callback reads
     * current inputs; queued USB events cannot select an obsolete mode. */
    u16 timer = sys_timer_add(NULL, rdx_dip_switch_deferred_handle, 100);
    ASSERT(timer != 0);
    /* 独立于 app_core 运行，覆盖启动或存储回调阻塞的情况。 */
    u16 guard = usr_timer_add(NULL, rdx_dip_switch_guard, 100, 0);
    ASSERT(guard != 0);
    s_init_done = true;
}
#endif
