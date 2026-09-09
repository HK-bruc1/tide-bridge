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
#include "rdx_uxfile.h"
#include "rdx_record.h"
#include "rdx_ble_server.h"
#include "rdx_led_ctrl.h"
#include "rdx_rtc.h"
#include "rdx_playback.h"
#include "jiffies.h"

static bool s_init_done;
static bool s_business_mode_entered;

#if TCFG_T2620_PC_STORAGE_ENABLE
enum { USB_SWITCH_IDLE, USB_SWITCH_DRAIN, USB_SWITCH_FILE, USB_SWITCH_FAILED };
static volatile u8 s_usb_switch;
static u8 s_wait_busy;
static u32 s_switch_deadline;
static int s_refresh_previous;
static u8 s_pc_refresh_pending;
#define USB_SWITCH_TICKET 1u /* Once per boot; successful switch reboots. */
#define USB_SWITCH_TIMEOUT_MS 30000u
extern void rdx_app_emmc_poweroff_check_timer_stop(void);
extern void rdx_app_wifi_handle(u8 cmd);
extern u8 get_ota_status(void);

void rdx_dip_switch_pc_returned(void)
{
    s_pc_refresh_pending = 1;
    rdx_uxfile_pc_returned();
}

int rdx_dip_switch_business_blocked(void)
{
    return s_usb_switch != USB_SWITCH_IDLE ||
           rdx_uxfile_pc_refresh_status() != 0 ||
           rdx_uxfile_storage_status() < 0;
}

int rdx_dip_switch_shutdown_deferred(void)
{
    return s_usb_switch != USB_SWITCH_IDLE || s_wait_busy;
}

int rdx_dip_switch_transition_led(void)
{
    if (s_usb_switch == USB_SWITCH_FAILED ||
        (get_power_on_status() && rdx_uxfile_pc_refresh_status() < 0) ||
        rdx_uxfile_storage_status() < 0) {
        return 2;
    }
    return s_usb_switch != USB_SWITCH_IDLE ||
           (get_power_on_status() && rdx_uxfile_pc_refresh_status() > 0);
}

static void rdx_usb_switch_fail(const char *reason)
{
    s_usb_switch = USB_SWITCH_FAILED;
    r_printf("[USB-SWITCH] FAILED: %s; no export/reset, restart required\n", reason);
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_USB_SWITCH_FAILED);
}

static int rdx_usb_switch_service(int on, int vbus)
{
    int refresh = rdx_uxfile_pc_refresh_status();
    if (refresh != s_refresh_previous) {
        r_printf("[USB-SWITCH] PC index refresh=%d\n", refresh);
        s_refresh_previous = refresh;
    }
    if (s_pc_refresh_pending && !refresh && on &&
        s_usb_switch == USB_SWITCH_IDLE && rdx_app_business_started()) {
        s_pc_refresh_pending = 0;
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
        rdx_playback_refresh_playlist();
#endif
        rdx_ble_server_adv_enable(1);
        rdx_led_ctrl_restore_system_state();
    }
    if (s_usb_switch == USB_SWITCH_FAILED) {
        return 1;
    }
    if (s_usb_switch == USB_SWITCH_IDLE) {
        if (app_var.goto_poweroff_flag) {
            return 0;
        }
        if (on) {
            s_wait_busy = 0;
            return 0;
        }
        if (!s_business_mode_entered || !rdx_app_business_started() ||
            (!vbus && !s_wait_busy)) {
            return 0;
        }
        /* Let an existing OTA/format finish; do not disconnect its transport
         * halfway. Returning ON before draining starts cancels this wait. */
        if (get_ota_status() || rdx_uxfile_is_formatting() ||
            rdx_app_get_dut_status()) {
            if (!s_wait_busy) {
                r_printf("[USB-SWITCH] waiting for OTA/format/DUT; ON cancels wait\n");
            }
            s_wait_busy = 1;
            return 1;
        }
        s_usb_switch = USB_SWITCH_DRAIN;
        s_switch_deadline = jiffies_msec() + USB_SWITCH_TIMEOUT_MS;
        sys_auto_shut_down_disable();
        rdx_app_emmc_poweroff_check_timer_stop();
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
        rdx_playback_stop();
#endif
#if RDX_WIFI_ENABLE
        rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
#endif
        r_printf("[USB-SWITCH] admission closed; drain recording and real BLE links\n");
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_USB_SWITCH_WAIT);
        if (rdx_record_usb_quiesce_request(USB_SWITCH_TICKET)) {
            rdx_usb_switch_fail("record fence enqueue");
            return 1;
        }
    }
    /* Keep the storage rail available throughout the actual worker drain. */
    rdx_app_emmc_poweroff_check_timer_stop();
    if ((s32)(jiffies_msec() - s_switch_deadline) >= 0) {
        rdx_usb_switch_fail("drain timeout");
        return 1;
    }
    if (s_usb_switch == USB_SWITCH_DRAIN) {
        int saved = rdx_record_usb_quiesce_poll(USB_SWITCH_TICKET);
        int ble_idle = rdx_ble_server_usb_quiesce();
        if (saved == -1 || rdx_uxfile_storage_status() < 0) {
            rdx_usb_switch_fail("record/file save");
        } else if (saved == 0 && ble_idle) {
            if (rdx_uxfile_fence_request(USB_SWITCH_TICKET)) {
                rdx_usb_switch_fail("file fence enqueue");
            } else {
                s_usb_switch = USB_SWITCH_FILE;
                r_printf("[USB-SWITCH] recording closed, BLE FIFO drained; file fence queued\n");
            }
        }
    } else if (s_usb_switch == USB_SWITCH_FILE) {
        int result = rdx_uxfile_fence_poll(USB_SWITCH_TICKET);
        if (result == -1) {
            rdx_usb_switch_fail("file flush/fence");
        } else if (!result) {
            /* The library worker/timers are quiet and recording is closed.
             * Reboot reconstructs the immutable RDX runtime and samples the
             * latest DIP/USB input. The cold export protection stays intact. */
            r_printf("[USB-SWITCH] saved and quiet; controlled reset, latest ON=%d VBUS=%d\n", on, vbus);
            rdx_cpu_reset();
        }
    }
    return 1;
}
#endif

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
#if TCFG_T2620_PC_STORAGE_ENABLE
    if (rdx_usb_switch_service(on, vbus)) {
        return;
    }
#endif
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
