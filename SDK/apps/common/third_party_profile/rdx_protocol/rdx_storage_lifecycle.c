#include "app_config.h"
#include "rdx_dip_switch.h"

#include "system/includes.h"
#include "poweroff.h"
#include "app_main.h"
#include "rdx_app.h"
#include "rdx_uxfile.h"
#include "rdx_record.h"
#include "rdx_record_format.h"
#include "rdx_ble_server.h"
#include "rdx_led_ctrl.h"
#include "rdx_rtc.h"
#include "rdx_playback.h"
#include "jiffies.h"

#include "rdx_storage_lifecycle.h"

#if TCFG_DIP_SWITCH_POWER_ENABLE
enum { USB_SWITCH_IDLE, USB_SWITCH_DRAIN, USB_SWITCH_FILE, USB_SWITCH_FAILED };
static volatile u8 s_usb_switch;
static u8 s_wait_busy;
static u32 s_switch_deadline;
static int s_refresh_previous;
static u8 s_pc_refresh_pending;
#define USB_SWITCH_TICKET 1u /* 每次启动只使用一次；切换成功后重启。 */
#define USB_SWITCH_TIMEOUT_MS 30000u
extern void rdx_app_emmc_poweroff_check_timer_stop(void);
extern void rdx_app_wifi_handle(u8 cmd);
extern u8 get_ota_status(void);
extern bool rdx_app_get_dut_status(void);

void rdx_storage_lifecycle_pc_returned(void)
{
    s_pc_refresh_pending = 1;
    rdx_uxfile_pc_returned();
}

int rdx_storage_lifecycle_business_blocked(void)
{
    return rdx_record_format_status() < 0 || s_usb_switch != USB_SWITCH_IDLE ||
           rdx_uxfile_pc_refresh_status() != 0 ||
           rdx_uxfile_storage_status() < 0;
}

int rdx_storage_lifecycle_shutdown_deferred(void)
{
    return s_usb_switch != USB_SWITCH_IDLE || s_wait_busy;
}

int rdx_storage_lifecycle_transition_led(void)
{
    if (rdx_record_format_status() < 0 || s_usb_switch == USB_SWITCH_FAILED ||
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

int rdx_storage_lifecycle_service(int on, int vbus)
{
    int refresh = rdx_uxfile_pc_refresh_status();
    if (refresh != s_refresh_previous) {
        r_printf("[USB-SWITCH] PC index refresh=%d\n", refresh);
        s_refresh_previous = refresh;
    }
    if (s_pc_refresh_pending && !refresh && on &&
        s_usb_switch == USB_SWITCH_IDLE && rdx_app_business_started()) {
        s_pc_refresh_pending = 0;
        r_printf("[USB-SWITCH] storage reconciliation complete; enabling business, supply stability unverified\n");
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
        if (rdx_dip_switch_cold_service() || !rdx_app_business_started() ||
            (!vbus && !s_wait_busy)) {
            return 0;
        }
        /* 等待正在进行的 OTA、格式化或 DUT 操作结束，避免中途断开传输。
         * 开始收尾前拨回 ON，可取消本次等待。 */
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
    /* 工作任务排空期间保持存储供电，禁止提前断电。 */
    rdx_app_emmc_poweroff_check_timer_stop();
    if ((s32)(jiffies_msec() - s_switch_deadline) >= 0) {
        rdx_usb_switch_fail("drain timeout");
        return 1;
    }
    if (s_usb_switch == USB_SWITCH_DRAIN) {
        int saved = rdx_record_usb_quiesce_poll(USB_SWITCH_TICKET);
        int ble_idle = rdx_ble_server_usb_quiesce();
        if (saved == -1 || rdx_record_format_status() < 0 || rdx_uxfile_storage_status() < 0) {
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
            /* 库内工作任务和定时器已静默，录音已关闭。
             * 通过重启重建不可原地重建的 RDX 运行时，并重新采样拨码和 USB 状态。
             * 文件工作任务在完成收尾屏障后被冻结，因此关闭 U 盘导出时也需要重启；
             * 此时拨码为 OFF 则进入纯充电待机，仍保留仅允许冷启动导出的保护。 */
            r_printf("[USB-SWITCH] saved and quiet; controlled reset, latest ON=%d VBUS=%d\n", on, vbus);
            rdx_cpu_reset();
        }
    }
    return 1;
}
#endif
