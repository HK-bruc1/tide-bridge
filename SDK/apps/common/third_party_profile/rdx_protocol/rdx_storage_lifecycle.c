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
#include "rdx_hogp_input.h"
#include "rdx_hogp_key_action.h"
#include "rdx_dut.h"
#include "rdx_cdc_source.h"

#if TCFG_DIP_SWITCH_POWER_ENABLE
static volatile int s_refresh_previous;
static u8 s_pc_refresh_pending;
static volatile int s_business_blocked_previous;

void rdx_storage_lifecycle_pc_returned(void)
{
    rdx_hogp_input_cancel();
    s_pc_refresh_pending = 1;
    rdx_uxfile_pc_returned();
    rdx_dip_switch_wake();
}

int rdx_storage_lifecycle_business_blocked(void)
{
    /* 此准入条件仅用于文件操作，不限制 BLE、HID 和电源流程。 */
    return rdx_record_format_service_status() != 0 ||
           rdx_uxfile_pc_refresh_status() != 0 ||
           rdx_uxfile_storage_status() < 0;
}

int rdx_storage_lifecycle_stream_blocked(void)
{
    return rdx_record_format_stream_status() != 0 ||
           rdx_cdc_source_stream_blocked() ||
           rdx_uxfile_pc_refresh_status() != 0 ||
           rdx_uxfile_storage_status() < 0;
}

int rdx_storage_lifecycle_shutdown_deferred(void)
{
    return 0;
}

int rdx_storage_lifecycle_transition_led(void)
{
    /* 历史录音异常由对应条目呈现，不触发全局故障灯效。 */
    return 0;
}

int rdx_storage_lifecycle_changed(void)
{
    /* 仅比较只读状态；文件服务和灯效只能由 app_core 处理。 */
    return rdx_storage_lifecycle_business_blocked() != s_business_blocked_previous ||
           rdx_uxfile_pc_refresh_status() != s_refresh_previous;
}

int rdx_storage_lifecycle_service(int on, int vbus)
{
    int blocked = rdx_storage_lifecycle_business_blocked();
    if (blocked != s_business_blocked_previous) {
        s_business_blocked_previous = blocked;
        if (rdx_dut_test_keys_active()) rdx_dut_service_wake();
    }
    int refresh = rdx_uxfile_pc_refresh_status();
    if (refresh != s_refresh_previous) {
        r_printf("[USB-SWITCH] PC index refresh=%d\n", refresh);
        s_refresh_previous = refresh;
    }
    /* 失败保留文件准入隔离，但不再为已结束的恢复任务续约超时。 */
    if (refresh < 0) s_pc_refresh_pending = 0;
    if (s_pc_refresh_pending && !refresh && on &&
        rdx_app_business_started()) {
        s_pc_refresh_pending = 0;
        r_printf("[USB-SWITCH] storage reconciliation complete; enabling business, supply stability unverified\n");
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
        rdx_playback_refresh_playlist();
#endif
        rdx_ble_server_adv_enable(1);
        rdx_led_ctrl_restore_system_state();
    }
    /* OFF 由原生电源状态机处理，不受存储屏障限制。 */
    (void)vbus;
    return s_pc_refresh_pending;
}
#endif
