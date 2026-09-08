#include "app_config.h"
#include "rdx_dip_switch.h"

#if TCFG_DIP_SWITCH_POWER_ENABLE
#include "system/includes.h"
#include "poweroff.h"
#include "app_main.h"
#include "app_msg.h"
#include "idle.h"
#include "asm/charge.h"
#include "usb/otg.h"
#include "rdx_app.h"

static bool s_init_done;
static bool s_business_mode_entered;

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
    /* Sampling runs on app_core, never block in the ISR. */
    (void)edge;
}

void rdx_dip_switch_init(void)
{
    if (s_init_done) {
        return;
    }
    gpio_set_mode(IO_PORT_SPILT(TCFG_DIP_SWITCH_POWER_IO), PORT_INPUT_PULLUP_10K);
    /* Also observes OTG identification after cold boot. Each callback reads
     * current inputs; queued USB events cannot select an obsolete mode. */
    u16 timer = sys_timer_add(NULL, rdx_dip_switch_deferred_handle, 100);
    ASSERT(timer != 0);
    s_init_done = true;
}
#endif
