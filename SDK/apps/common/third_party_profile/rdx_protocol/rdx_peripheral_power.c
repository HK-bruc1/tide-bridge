#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_peripheral_power.data.bss")
#pragma data_seg(".rdx_peripheral_power.data")
#pragma const_seg(".rdx_peripheral_power.text.const")
#pragma code_seg(".rdx_peripheral_power.text")
#endif

#include "app_config.h"
#include "app_main.h"
#include "asm/dac.h"
#include "dev_manager.h"
#include "gpio_config.h"
#include "system/includes.h"

#include "rdx_app_config.h"
#include "rdx_app.h"
#include "rdx_ble_server.h"
#include "rdx_charge.h"
#include "rdx_led_ctrl.h"
#include "rdx_peripheral_power.h"
#include "rdx_adv_policy.h"
#include "rdx_playback.h"
#include "rdx_protocol.h"
#include "rdx_record.h"
#include "rdx_uxfile.h"
#include "rdx_record_format.h"
#include "rdx_factory_audio.h"
#include "app_power_manage.h"
#include "asm/charge.h"
#include "usb/otg.h"

#if TCFG_T2620_SHARED_VDD_ENABLE && RDX_WIFI_ENABLE
#error "T2620 PA4 shared VDD conflicts with the legacy RDX WiFi power assignment"
#endif

#if TCFG_T2620_SHARED_VDD_ENABLE && \
    defined(TCFG_DEBUG_DLOG_ENABLE) && TCFG_DEBUG_DLOG_ENABLE
#error "T2620 PA4 shared VDD conflicts with the DLOG SPI CS assignment"
#endif

#if TCFG_T2620_SHARED_VDD_ENABLE && TCFG_RDEC_KEY_ENABLE
#error "T2620 PA4 shared VDD conflicts with the current RDEC0 assignment"
#endif

#if TCFG_T2620_SHARED_VDD_ENABLE && !TCFG_SD0_ENABLE
#error "T2620 shared VDD manager requires SD0"
#endif

#if TCFG_T2620_SHARED_VDD_ENABLE && !TCFG_DEV_MANAGER_ENABLE
#error "T2620 shared VDD manager requires dev_manager takeover/restore"
#endif

#if TCFG_T2620_SHARED_VDD_ENABLE && (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN)

#define RDX_SHARED_VDD_IDLE_RECHECK_MS    (1000u)

typedef enum {
    RDX_SHARED_VDD_EVENT_FAST_ADV = 0,
    RDX_SHARED_VDD_EVENT_SLOW_ADV,
    RDX_SHARED_VDD_EVENT_BLE_LINKS_CHANGED,
    RDX_SHARED_VDD_EVENT_BUSINESS_CHANGED,
    RDX_SHARED_VDD_EVENT_CONNECTED_EVAL,
} rdx_shared_vdd_event_t;

/* 停用来源：决定执行器每一级复检使用哪套资格 */
enum {
    RDX_VDD_SRC_OFFLINE = 0,
    RDX_VDD_SRC_CONNECTED = 1,
};

/* 瞬时占用只等待归零事件，不作废连接态空闲窗口 */
#define RDX_VDD_CONN_TRANSIENT_BUSY    (RDX_SHARED_VDD_BUSY_STORAGE_IO)

typedef struct {
    u8 initialized;
    u8 transition_mutex_initialized;
    u8 pa4_enabled;
    u8 slow_adv;
    volatile u8 wake_requested;
    u8 usb_reserved;
    u8 last_reject_reason;
    u16 idle_recheck_timer;
    rdx_shared_vdd_state_t state;
    rdx_shared_vdd_owner_t owner;
    u32 busy_mask;
    u32 generation;
    volatile u32 wake_epoch;
    u32 slow_adv_epoch;
    u32 idle_recheck_epoch;
    u32 last_would_off_generation;
    u32 last_reject_generation;
    /* 连接态空闲窗口：只在 app_core 修改 */
    u8 conn_armed;
    volatile u8 conn_hint_wanted;
    volatile u8 conn_queued;
    volatile u8 conn_delivery_failed;
    u16 conn_timer;
    u32 conn_timer_serial;
    u32 conn_version;
    u32 conn_epoch;
    u32 conn_deadline;
    u32 conn_logged_busy;
    u8 conn_done;
    u8 conn_failed;
    u8 stop_source;
    OS_MUTEX transition_mutex;
} rdx_shared_vdd_context_t;

enum {
    RDX_SHARED_VDD_REJECT_NONE = 0,
    RDX_SHARED_VDD_REJECT_NOT_SLOW,
    RDX_SHARED_VDD_REJECT_BLE_LINK,
    RDX_SHARED_VDD_REJECT_BUSINESS,
};

static rdx_shared_vdd_context_t g_rdx_shared_vdd = {
    .state = RDX_SHARED_VDD_STATE_STARTING,
    .owner = RDX_SHARED_VDD_OWNER_DEVICE,
    .last_would_off_generation = (u32)-1,
    .last_reject_generation = (u32)-1,
};

static void rdx_peripheral_power_vdd_event_post(
    rdx_shared_vdd_event_t event, u32 epoch);
static void rdx_peripheral_power_vdd_idle_recheck_cancel(void);
static void rdx_peripheral_power_vdd_idle_recheck_schedule(u32 epoch);
static void rdx_vdd_conn_post(void);

static const char *rdx_peripheral_power_vdd_wake_name(
    rdx_shared_vdd_wake_reason_t reason)
{
    switch (reason) {
    case RDX_SHARED_VDD_WAKE_FAST_ADV:
        return "FAST_ADV";
    case RDX_SHARED_VDD_WAKE_BLE_LINK:
        return "BLE_LINK";
    case RDX_SHARED_VDD_WAKE_SD_DRIVER:
        return "SD_DRIVER";
    case RDX_SHARED_VDD_WAKE_USB_MSC:
        return "USB_MSC";
    case RDX_SHARED_VDD_WAKE_BUSINESS:
    default:
        return "BUSINESS";
    }
}

extern u8 get_ota_status(void);
extern bool rdx_app_get_dut_status(void);
extern bool rdx_uxfile_sd_format_status_check(void);
extern ReqFileInfo *rdx_protocol_get_uploadfileInfo(void);
/* 补丁库提供：脏缓存、打开的读句柄、IO 错误或删除排队时禁止卸载 */
extern u8 rdx_uxfile_power_busy(void);

/* This is the only PA4 hardware write entry in the product. */
static void rdx_peripheral_power_vdd_hw_set(u8 enable)
{
    u8 next = !!enable;

    if (g_rdx_shared_vdd.pa4_enabled == next &&
        g_rdx_shared_vdd.initialized) {
        return;
    }
    gpio_set_mode(IO_PORT_SPILT(TCFG_T2620_SHARED_VDD_IO),
                  next ? PORT_OUTPUT_HIGH : PORT_OUTPUT_LOW);
    g_rdx_shared_vdd.pa4_enabled = next;
}

void rdx_peripheral_power_vdd_early_init(void)
{
    if (g_rdx_shared_vdd.initialized) {
        return;
    }

    /* Runtime restore requests can originate from btstack, app_core or USB.
     * Create the transition lock before publishing initialized. */
    if (os_mutex_create(&g_rdx_shared_vdd.transition_mutex) == 0) {
        g_rdx_shared_vdd.transition_mutex_initialized = 1;
    }

    /* The soldered SD NAND may request power before BLE/RDX initialization. */
    rdx_peripheral_power_vdd_hw_set(1);
    g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_DEVICE;
    g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_ON_READY;
    g_rdx_shared_vdd.initialized = 1;
}

static u32 rdx_peripheral_power_vdd_wake_request_begin(void)
{
    u32 epoch;

    local_irq_disable();
    g_rdx_shared_vdd.wake_requested = 1;
    epoch = ++g_rdx_shared_vdd.wake_epoch;
    local_irq_enable();
    return epoch;
}

static void rdx_peripheral_power_vdd_storage_io_safe(void)
{
    gpio_set_mode(IO_PORT_SPILT(TCFG_SD0_PORT_CLK), PORT_HIGHZ);
    gpio_set_mode(IO_PORT_SPILT(TCFG_SD0_PORT_CMD), PORT_HIGHZ);
    gpio_set_mode(IO_PORT_SPILT(TCFG_SD0_PORT_DA0), PORT_HIGHZ);
#if TCFG_SD0_DAT_MODE == 4
    gpio_set_mode(IO_PORT_SPILT(TCFG_SD0_PORT_DA1), PORT_HIGHZ);
    gpio_set_mode(IO_PORT_SPILT(TCFG_SD0_PORT_DA2), PORT_HIGHZ);
    gpio_set_mode(IO_PORT_SPILT(TCFG_SD0_PORT_DA3), PORT_HIGHZ);
#endif
}

static int rdx_peripheral_power_vdd_restore_idle_domain(
    rdx_shared_vdd_wake_reason_t reason)
{
    int err;

    if (g_rdx_shared_vdd.owner != RDX_SHARED_VDD_OWNER_IDLE_BLOCKED) {
        return (g_rdx_shared_vdd.state == RDX_SHARED_VDD_STATE_ON_READY) ?
               0 : -1;
    }

    g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_STARTING;
    if (!g_rdx_shared_vdd.pa4_enabled) {
        rdx_peripheral_power_vdd_hw_set(1);
        os_time_dly(TCFG_T2620_SHARED_VDD_POWER_STABLE_TICKS);
    }

    err = dev_manager_restore("sd0");
    if (err) {
        g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_RECOVERY;
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_FAULT_ON;
        r_printf("[PWR] restore_failed reason=%s step=SD0 err=%d pa4=%u\n",
                 rdx_peripheral_power_vdd_wake_name(reason), err,
                 g_rdx_shared_vdd.pa4_enabled);
        return err;
    }

    g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_DEVICE;
    if (rdx_led_hardware_resume()) {
        g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_RECOVERY;
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_FAULT_ON;
        r_printf("[PWR] restore_failed reason=%s step=RGB pa4=%u\n",
                 rdx_peripheral_power_vdd_wake_name(reason),
                 g_rdx_shared_vdd.pa4_enabled);
        return -1;
    }

    g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_ON_READY;
    g_rdx_shared_vdd.wake_requested = 0;
    /* A successful business wake starts a new active interval even when BLE
     * remains in the same slow-advertising generation. Re-arm the one-shot
     * idle decision so business completion can quiesce the domain again. */
    g_rdx_shared_vdd.last_would_off_generation = (u32)-1;
    g_rdx_shared_vdd.last_reject_generation = (u32)-1;
    r_printf("[PWR] restored reason=%s owner=DEVICE pa4=%u state=ON_READY\n",
             rdx_peripheral_power_vdd_wake_name(reason),
             g_rdx_shared_vdd.pa4_enabled);
    /* 恢复期间的业务事件可能已被消费，恢复成功后补发一次连接态评估 */
    rdx_vdd_conn_post();
    return 0;
}

int rdx_peripheral_power_vdd_ensure_on(rdx_shared_vdd_wake_reason_t reason)
{
    u8 needs_restore;
    int err = 0;

    if (!g_rdx_shared_vdd.initialized) {
        rdx_peripheral_power_vdd_early_init();
    }

    /* The SD driver's callback only guarantees electrical power. During
     * dev_manager_restore(), it must never recursively enter the owner flow. */
    if (reason == RDX_SHARED_VDD_WAKE_SD_DRIVER) {
        if (!g_rdx_shared_vdd.pa4_enabled) {
            rdx_peripheral_power_vdd_hw_set(1);
            os_time_dly(TCFG_T2620_SHARED_VDD_POWER_STABLE_TICKS);
        }
        return 0;
    }

    rdx_peripheral_power_vdd_wake_request_begin();

    if (g_rdx_shared_vdd.transition_mutex_initialized) {
        os_mutex_pend(&g_rdx_shared_vdd.transition_mutex, 0);
    }

    local_irq_disable();
    needs_restore = (g_rdx_shared_vdd.owner ==
                     RDX_SHARED_VDD_OWNER_IDLE_BLOCKED);
    if (g_rdx_shared_vdd.state == RDX_SHARED_VDD_STATE_STOPPING) {
        local_irq_enable();
        r_printf("[PWR] wake_deferred reason=%s state=STOPPING pa4=%u\n",
                 rdx_peripheral_power_vdd_wake_name(reason),
                 g_rdx_shared_vdd.pa4_enabled);
        err = -1;
        goto __exit;
    }
    if (!g_rdx_shared_vdd.pa4_enabled && !needs_restore) {
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_STARTING;
        rdx_peripheral_power_vdd_hw_set(1);
    }
    local_irq_enable();

    if (needs_restore) {
        err = rdx_peripheral_power_vdd_restore_idle_domain(reason);
        goto __exit;
    }
    if (g_rdx_shared_vdd.owner != RDX_SHARED_VDD_OWNER_DEVICE ||
        g_rdx_shared_vdd.state == RDX_SHARED_VDD_STATE_FAULT_ON) {
        err = -1;
        goto __exit;
    }
    g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_ON_READY;
    g_rdx_shared_vdd.wake_requested = 0;

__exit:
    if (g_rdx_shared_vdd.transition_mutex_initialized) {
        os_mutex_post(&g_rdx_shared_vdd.transition_mutex);
    }
    return err;
}

static u32 rdx_peripheral_power_vdd_busy_snapshot(void)
{
    u32 busy = 0;
    RecordStatus *record = rdx_record_get_status();

    if (rdx_ble_server_get_connected_count()) {
        busy |= RDX_SHARED_VDD_BUSY_BLE_LINK;
    }

    if (!record || record->run != RECORD_STATE_STOP ||
        record->process_state == REC_PROCESS_STATE_BUSY) {
        busy |= RDX_SHARED_VDD_BUSY_RECORD;
    }

#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    {
        pb_public_info_t playback = {0};
        rdx_playback_get_info(&playback);
        if (playback.state != PB_STATE_UNREADY &&
            playback.state != PB_STATE_STOPPED) {
            busy |= RDX_SHARED_VDD_BUSY_PLAYBACK;
        }
    }
#endif

    {
        ReqFileInfo *file_info = rdx_protocol_get_uploadfileInfo();
        if ((file_info && file_info->file_send_busy) ||
            rdx_is_file_transfer_active() ||
            rdx_is_file_sync_busy() ||
            rdx_uxfile_sync_is_in_progress() ||
            rdx_uxfile_is_datFileInfo_loading() ||
            rdx_uxfile_is_scan_active()) {
            busy |= RDX_SHARED_VDD_BUSY_FILE_OP;
        }
    }

    if (app_in_mode(APP_MODE_PC) || g_rdx_shared_vdd.usb_reserved ||
        g_rdx_shared_vdd.owner == RDX_SHARED_VDD_OWNER_USB_HOST ||
        g_rdx_shared_vdd.owner == RDX_SHARED_VDD_OWNER_RECOVERY) {
        busy |= RDX_SHARED_VDD_BUSY_USB_MSC;
    }

    if (rdx_uxfile_is_formatting() ||
        rdx_uxfile_sd_format_status_check()) {
        busy |= RDX_SHARED_VDD_BUSY_FORMAT_RECOVERY;
    }

    if (rdx_led_ctrl_get_scene() != RDX_LED_SCENE_OFF ||
        rdx_app_get_charge_state() != RDX_CHARGE_OUT ||
        rdx_app_get_dut_status() || get_ota_status()) {
        busy |= RDX_SHARED_VDD_BUSY_RGB_REQUIRED;
    }

    return busy;
}

u32 rdx_adv_policy_hold_snapshot(void)
{
    u32 mask = 0;
    ReqFileInfo *file = rdx_protocol_get_uploadfileInfo();
    if (rdx_record_adv_busy()) mask |= RDX_SHARED_VDD_BUSY_RECORD;
#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE
    pb_public_info_t pb = {0};
    rdx_playback_get_info(&pb);
    if (pb.state != PB_STATE_UNREADY && pb.state != PB_STATE_STOPPED)
        mask |= RDX_SHARED_VDD_BUSY_PLAYBACK;
#endif
    if ((file && file->file_send_busy) || rdx_is_file_transfer_active() ||
        rdx_is_file_sync_busy() || rdx_uxfile_sync_is_in_progress() ||
        rdx_uxfile_is_datFileInfo_loading() || rdx_uxfile_is_scan_active())
        mask |= RDX_SHARED_VDD_BUSY_FILE_OP;
    if (app_in_mode(APP_MODE_PC) || g_rdx_shared_vdd.usb_reserved ||
        g_rdx_shared_vdd.owner == RDX_SHARED_VDD_OWNER_USB_HOST)
        mask |= RDX_SHARED_VDD_BUSY_USB_MSC;
    if (rdx_uxfile_is_formatting() || rdx_uxfile_sd_format_status_check())
        mask |= RDX_SHARED_VDD_BUSY_FORMAT_RECOVERY;
    if (rdx_app_get_dut_status() || get_ota_status()) mask |= 0x100;
    return mask;
}

/* 连接态资格：基础忙位去掉 BLE_LINK，再叠加外部电源、业务占用与库内存储事实。
 * 只在 app_core 调用（内部会查询 BLE 封装接口）。 */
static u32 rdx_vdd_conn_busy_snapshot(void)
{
    u32 busy = rdx_peripheral_power_vdd_busy_snapshot() &
               ~RDX_SHARED_VDD_BUSY_BLE_LINK;
    u32 usb = usb_otg_online(0);

    if (get_charge_online_flag() ||
        rdx_app_get_charge_state() != RDX_CHARGE_OUT ||
        (usb != IDLE_MODE && usb != DISCONN_MODE)) {
        busy |= RDX_SHARED_VDD_BUSY_EXTERNAL_POWER;
    }
    if (rdx_adv_policy_business_pending() || rdx_record_adv_busy() ||
        rdx_record_format_download_busy() || rdx_factory_audio_file_busy()) {
        busy |= RDX_SHARED_VDD_BUSY_BUSINESS;
    }
    if (rdx_record_format_io_users()) {
        busy |= RDX_SHARED_VDD_BUSY_STORAGE_IO;
    }
    if (rdx_uxfile_power_busy()) {
        busy |= RDX_SHARED_VDD_BUSY_STORAGE_STATE;
    }
    if (rdx_record_format_service_status() || rdx_record_format_status()) {
        busy |= RDX_SHARED_VDD_BUSY_FORMAT_RECOVERY;
    }
    if (app_var.goto_poweroff_flag || rdx_app_get_poweroff_flag() ||
        get_vbat_need_shutdown()) {
        busy |= RDX_SHARED_VDD_BUSY_SHUTDOWN;
    }
    return busy;
}

/* 关中断期间可调用：只读原子计数，不调用 SDK 接口 */
static u8 rdx_vdd_conn_atomic_valid(void)
{
    return g_rdx_shared_vdd.conn_armed &&
           !g_rdx_shared_vdd.conn_delivery_failed &&
           rdx_adv_policy_business_version() == g_rdx_shared_vdd.conn_version &&
           !rdx_adv_policy_business_pending() &&
           !rdx_record_format_io_users();
}

static u8 rdx_vdd_source_valid(u8 source)
{
    if (source == RDX_VDD_SRC_CONNECTED) {
        return rdx_ble_server_get_connected_count() &&
               rdx_vdd_conn_atomic_valid();
    }
    return !rdx_ble_server_get_connected_count() &&
           g_rdx_shared_vdd.slow_adv && rdx_adv_policy_idle_valid();
}

static u32 rdx_vdd_source_busy(u8 source)
{
    return source == RDX_VDD_SRC_CONNECTED ?
           rdx_vdd_conn_busy_snapshot() :
           rdx_peripheral_power_vdd_busy_snapshot();
}

static const char *rdx_peripheral_power_vdd_first_busy_name(u32 busy)
{
    if (busy & RDX_SHARED_VDD_BUSY_BLE_LINK) {
        return "BLE_LINK";
    }
    if (busy & RDX_SHARED_VDD_BUSY_RECORD) {
        return "RECORD";
    }
    if (busy & RDX_SHARED_VDD_BUSY_PLAYBACK) {
        return "PLAYBACK";
    }
    if (busy & RDX_SHARED_VDD_BUSY_FILE_OP) {
        return "FILE_OP";
    }
    if (busy & RDX_SHARED_VDD_BUSY_USB_MSC) {
        return "USB_MSC";
    }
    if (busy & RDX_SHARED_VDD_BUSY_FORMAT_RECOVERY) {
        return "FORMAT_RECOVERY";
    }
    if (busy & RDX_SHARED_VDD_BUSY_RGB_REQUIRED) {
        return "RGB_REQUIRED";
    }
    if (busy & RDX_SHARED_VDD_BUSY_EXTERNAL_POWER) {
        return "EXTERNAL_POWER";
    }
    if (busy & RDX_SHARED_VDD_BUSY_BUSINESS) {
        return "BUSINESS";
    }
    if (busy & RDX_SHARED_VDD_BUSY_STORAGE_IO) {
        return "STORAGE_IO";
    }
    if (busy & RDX_SHARED_VDD_BUSY_STORAGE_STATE) {
        return "STORAGE_STATE";
    }
    if (busy & RDX_SHARED_VDD_BUSY_SHUTDOWN) {
        return "SHUTDOWN";
    }
    return "NONE";
}

static void rdx_peripheral_power_vdd_reject_log(u8 reason)
{
    if (g_rdx_shared_vdd.last_reject_generation ==
            g_rdx_shared_vdd.generation &&
        g_rdx_shared_vdd.last_reject_reason == reason) {
        return;
    }
    g_rdx_shared_vdd.last_reject_generation = g_rdx_shared_vdd.generation;
    g_rdx_shared_vdd.last_reject_reason = reason;
    r_printf("[PWR] reject_off gen=%u reason=%s links=%u busy=0x%02x slow=%u\n",
             g_rdx_shared_vdd.generation,
             reason == RDX_SHARED_VDD_REJECT_NOT_SLOW ? "NOT_SLOW_ADV" :
             reason == RDX_SHARED_VDD_REJECT_BLE_LINK ? "BLE_LINK" :
             rdx_peripheral_power_vdd_first_busy_name(
                 g_rdx_shared_vdd.busy_mask),
             rdx_ble_server_get_connected_count(),
             g_rdx_shared_vdd.busy_mask,
             g_rdx_shared_vdd.slow_adv);
}

static void rdx_peripheral_power_vdd_idle_recheck_cancel(void)
{
    if (g_rdx_shared_vdd.idle_recheck_timer) {
        sys_timeout_del(g_rdx_shared_vdd.idle_recheck_timer);
        g_rdx_shared_vdd.idle_recheck_timer = 0;
    }
}

static void rdx_peripheral_power_vdd_idle_recheck_cb(void *priv)
{
    u32 epoch = g_rdx_shared_vdd.idle_recheck_epoch;

    (void)priv;
    g_rdx_shared_vdd.idle_recheck_timer = 0;
    if ((!g_rdx_shared_vdd.slow_adv || !rdx_adv_policy_idle_valid()) ||
        epoch != g_rdx_shared_vdd.slow_adv_epoch) {
        return;
    }

    /* Timer callbacks do not own the transition state machine. Re-enter it
     * through app_core and carry the exact slow-advertising epoch so an old
     * retry can never act on a newer wake cycle. */
    rdx_peripheral_power_vdd_event_post(
        RDX_SHARED_VDD_EVENT_BUSINESS_CHANGED, epoch);
}

static void rdx_peripheral_power_vdd_idle_recheck_schedule(u32 epoch)
{
    if ((!g_rdx_shared_vdd.slow_adv || !rdx_adv_policy_idle_valid()) ||
        g_rdx_shared_vdd.state != RDX_SHARED_VDD_STATE_ON_READY ||
        g_rdx_shared_vdd.idle_recheck_timer) {
        return;
    }

    g_rdx_shared_vdd.idle_recheck_epoch = epoch;
    g_rdx_shared_vdd.idle_recheck_timer = sys_timeout_add(
        NULL, rdx_peripheral_power_vdd_idle_recheck_cb,
        RDX_SHARED_VDD_IDLE_RECHECK_MS);
    if (!g_rdx_shared_vdd.idle_recheck_timer) {
        r_printf("[PWR] idle_recheck_schedule_failed epoch=%u pa4=%u\n",
                 epoch, g_rdx_shared_vdd.pa4_enabled);
    }
}

static u8 rdx_vdd_mode(u8 source)
{
    return source == RDX_VDD_SRC_CONNECTED ?
           TCFG_T2620_CONNECTED_VDD_MODE : TCFG_T2620_SHARED_VDD_MODE;
}

static const char *rdx_vdd_mode_name_of(u8 mode)
{
    return mode == T2620_SHARED_VDD_MODE_DRY_RUN ? "DRY_RUN" :
           mode == T2620_SHARED_VDD_MODE_UNMOUNT_ONLY ? "UNMOUNT_ONLY" :
           "POWER_CUT";
}

static const char *rdx_vdd_tag(u8 source)
{
    return source == RDX_VDD_SRC_CONNECTED ? "[PWR_CONN]" : "[PWR]";
}

/* 竞争导致停用取消时重新评估；硬件失败另外锁存，不自动周期重试。 */
static void rdx_vdd_conn_restart(void)
{
    g_rdx_shared_vdd.conn_armed = 0;
    rdx_vdd_conn_post();
}

static void rdx_vdd_stop_defer(u8 source)
{
    if (source == RDX_VDD_SRC_CONNECTED) {
        rdx_vdd_conn_restart();
    } else {
        /* 新增的短暂 IO 复检不能让原慢广播代次永久失去重试机会。 */
        g_rdx_shared_vdd.last_would_off_generation = (u32)-1;
        rdx_peripheral_power_vdd_idle_recheck_schedule(g_rdx_shared_vdd.wake_epoch);
    }
}

static void rdx_peripheral_power_vdd_stop_abort(const char *step)
{
    int err = 0;
    u8 source = g_rdx_shared_vdd.stop_source;

    /* A canceled transition must be eligible for a later business-complete
     * retry within the same slow-advertising generation. */
    g_rdx_shared_vdd.last_would_off_generation = (u32)-1;

    if (g_rdx_shared_vdd.owner == RDX_SHARED_VDD_OWNER_IDLE_BLOCKED) {
        err = rdx_peripheral_power_vdd_restore_idle_domain(
            RDX_SHARED_VDD_WAKE_BUSINESS);
    } else {
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_ON_READY;
    }
    r_printf("%s stop_aborted step=%s err=%d links=%u busy=0x%03x pa4=%u\n",
             rdx_vdd_tag(source), step, err,
             rdx_ble_server_get_connected_count(),
             g_rdx_shared_vdd.busy_mask,
             g_rdx_shared_vdd.pa4_enabled);
    if (source == RDX_VDD_SRC_CONNECTED) {
        if (!err) {
            rdx_vdd_conn_restart();
        }
        return;
    }
    if (!err && g_rdx_shared_vdd.slow_adv) {
        rdx_peripheral_power_vdd_idle_recheck_schedule(
            g_rdx_shared_vdd.wake_epoch);
    }
}

/* 共用停用执行器。source 决定每一级复检的资格：未连接要求零 ACL + 慢广播，
 * 连接态要求窗口仍有效；两者都以 wake_epoch 同步取消。 */
static void rdx_peripheral_power_vdd_idle_stop(u32 idle_epoch, u8 source)
{
    u32 stop_epoch;
    u8 canceled;
    u8 mode = rdx_vdd_mode(source);
    const char *tag = rdx_vdd_tag(source);
    int err;

    if (g_rdx_shared_vdd.transition_mutex_initialized) {
        os_mutex_pend(&g_rdx_shared_vdd.transition_mutex, 0);
    }
    g_rdx_shared_vdd.stop_source = source;

    /* 卸载前再取一次完整快照：脏缓存、打开的句柄等事实必须在 takeover 前拦截 */
    if (source == RDX_VDD_SRC_CONNECTED) g_rdx_shared_vdd.conn_hint_wanted = 1;
    g_rdx_shared_vdd.busy_mask = rdx_vdd_source_busy(source);
    if (g_rdx_shared_vdd.busy_mask || !rdx_vdd_source_valid(source)) {
        r_printf("%s stop_aborted step=PRE_TAKEOVER busy=0x%03x\n",
                 tag, g_rdx_shared_vdd.busy_mask);
        rdx_vdd_stop_defer(source);
        goto __exit;
    }

    local_irq_disable();
    stop_epoch = idle_epoch;
    canceled = g_rdx_shared_vdd.wake_requested ||
               g_rdx_shared_vdd.wake_epoch != stop_epoch ||
               rdx_record_format_io_users() ||
               (source == RDX_VDD_SRC_CONNECTED ?
                !rdx_vdd_conn_atomic_valid() : !rdx_adv_policy_idle_valid()) ||
               g_rdx_shared_vdd.state != RDX_SHARED_VDD_STATE_ON_READY;
    if (canceled) {
        local_irq_enable();
        r_printf("%s stop_aborted step=PRECHECK idle_epoch=%u wake_epoch=%u state=%u\n",
                 tag, stop_epoch, g_rdx_shared_vdd.wake_epoch,
                 g_rdx_shared_vdd.state);
        rdx_vdd_stop_defer(source);
        goto __exit;
    }
    if (mode == T2620_SHARED_VDD_MODE_DRY_RUN) {
        local_irq_enable();
        r_printf("%s would_off gen=%u mode=%s pa4=%u state=ON_READY\n",
                 tag, g_rdx_shared_vdd.generation,
                 rdx_vdd_mode_name_of(mode), g_rdx_shared_vdd.pa4_enabled);
        goto __exit;
    }
    g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_STOPPING;
    local_irq_enable();
    r_printf("%s stopping gen=%u mode=%s epoch=%u links=%u\n",
             tag, g_rdx_shared_vdd.generation,
             rdx_vdd_mode_name_of(mode), stop_epoch,
             rdx_ble_server_get_connected_count());

    err = dev_manager_takeover("sd0");
    if (err) {
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_ON_READY;
        r_printf("%s stop_failed step=SD0_TAKEOVER err=%d pa4=%u\n",
                 tag, err, g_rdx_shared_vdd.pa4_enabled);
        if (source == RDX_VDD_SRC_CONNECTED) {
            /* 硬件停用失败不自动续调，等新的活动或拓扑代次再尝试。 */
            g_rdx_shared_vdd.conn_failed = 1;
        }
        goto __exit;
    }
    g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_IDLE_BLOCKED;
    r_printf("%s sd0_owner=IDLE_BLOCKED\n", tag);

    g_rdx_shared_vdd.busy_mask = rdx_vdd_source_busy(source);
    if (g_rdx_shared_vdd.wake_requested ||
        g_rdx_shared_vdd.wake_epoch != stop_epoch ||
        g_rdx_shared_vdd.busy_mask || !rdx_vdd_source_valid(source)) {
        rdx_peripheral_power_vdd_stop_abort("POST_TAKEOVER_RECHECK");
        goto __exit;
    }

    err = rdx_led_hardware_deinit();
    if (err) {
        if (source == RDX_VDD_SRC_CONNECTED) g_rdx_shared_vdd.conn_failed = 1;
        rdx_peripheral_power_vdd_stop_abort("RGB_QUIESCE");
        goto __exit;
    }
    r_printf("%s rgb_quiesced data=LOW spi=OFF\n", tag);

    g_rdx_shared_vdd.busy_mask = rdx_vdd_source_busy(source);

    /* Close the final race with BLE/task wake sources. A BLE topology change
     * raises wake_requested/wake_epoch synchronously before its app_core event
     * is posted, while the busy snapshot above covers links that already
     * existed. Do not call app_ble_get_hdl_con_handle() through
     * rdx_ble_server_get_connected_count() here: the JL BLE wrapper takes its
     * own mutex and asserts when called with interrupts disabled. */
    local_irq_disable();
    canceled = g_rdx_shared_vdd.wake_requested ||
               g_rdx_shared_vdd.wake_epoch != stop_epoch ||
               g_rdx_shared_vdd.busy_mask ||
               rdx_record_format_io_users() ||
               (source == RDX_VDD_SRC_CONNECTED ?
                !rdx_vdd_conn_atomic_valid() :
                (!g_rdx_shared_vdd.slow_adv || !rdx_adv_policy_idle_valid()));
    if (!canceled) {
        if (mode == T2620_SHARED_VDD_MODE_POWER_CUT) {
            rdx_peripheral_power_vdd_storage_io_safe();
            rdx_peripheral_power_vdd_hw_set(0);
            g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_OFF;
        } else {
            g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_QUIESCED_ON;
        }
    }
    local_irq_enable();

    if (canceled) {
        rdx_peripheral_power_vdd_stop_abort("FINAL_RECHECK");
        goto __exit;
    }

    if (mode == T2620_SHARED_VDD_MODE_POWER_CUT) {
        r_printf("%s pa4=0 state=OFF owner=IDLE_BLOCKED\n", tag);
    } else {
        r_printf("%s quiesced mode=UNMOUNT_ONLY pa4=1 state=QUIESCED_ON owner=IDLE_BLOCKED\n", tag);
    }
    if (source == RDX_VDD_SRC_CONNECTED) g_rdx_shared_vdd.conn_hint_wanted = 0;

__exit:
    if (g_rdx_shared_vdd.transition_mutex_initialized) {
        os_mutex_post(&g_rdx_shared_vdd.transition_mutex);
    }
}

static void rdx_peripheral_power_vdd_idle_evaluate(u32 idle_epoch)
{
    u8 links = rdx_ble_server_get_connected_count();

    g_rdx_shared_vdd.busy_mask =
        rdx_peripheral_power_vdd_busy_snapshot();
    if ((!g_rdx_shared_vdd.slow_adv || !rdx_adv_policy_idle_valid())) {
        rdx_peripheral_power_vdd_reject_log(
            RDX_SHARED_VDD_REJECT_NOT_SLOW);
        return;
    }
    if (idle_epoch != g_rdx_shared_vdd.wake_epoch) {
        r_printf("[PWR] stale_idle_ignored event_epoch=%u wake_epoch=%u\n",
                 idle_epoch, g_rdx_shared_vdd.wake_epoch);
        return;
    }
    if (links ||
        (g_rdx_shared_vdd.busy_mask & RDX_SHARED_VDD_BUSY_BLE_LINK)) {
        rdx_peripheral_power_vdd_reject_log(
            RDX_SHARED_VDD_REJECT_BLE_LINK);
        return;
    }
    if (g_rdx_shared_vdd.busy_mask) {
        rdx_peripheral_power_vdd_reject_log(
            RDX_SHARED_VDD_REJECT_BUSINESS);
        rdx_peripheral_power_vdd_idle_recheck_schedule(idle_epoch);
        return;
    }

    g_rdx_shared_vdd.last_reject_reason = RDX_SHARED_VDD_REJECT_NONE;
    if (g_rdx_shared_vdd.last_would_off_generation ==
            g_rdx_shared_vdd.generation ||
        g_rdx_shared_vdd.state != RDX_SHARED_VDD_STATE_ON_READY) {
        return;
    }
    g_rdx_shared_vdd.last_would_off_generation =
        g_rdx_shared_vdd.generation;
    rdx_peripheral_power_vdd_idle_recheck_cancel();
    rdx_peripheral_power_vdd_idle_stop(idle_epoch, RDX_VDD_SRC_OFFLINE);
}

static void rdx_vdd_conn_evaluate(void);

static void rdx_vdd_conn_cancel(void)
{
    ++g_rdx_shared_vdd.conn_timer_serial;
    if (g_rdx_shared_vdd.conn_timer) {
        sys_timeout_del(g_rdx_shared_vdd.conn_timer);
        g_rdx_shared_vdd.conn_timer = 0;
    }
}

/* 定时器只在 app_core 创建，SDK 保证回调回到创建任务；不跨任务改窗口。 */
static void rdx_vdd_conn_timeout(void *serial)
{
    if ((u32)serial != g_rdx_shared_vdd.conn_timer_serial) return;
    g_rdx_shared_vdd.conn_timer = 0;
    rdx_vdd_conn_evaluate();
}

static void rdx_vdd_conn_evaluate(void)
{
    u32 busy, version, epoch, now;

    /* 先允许归零通知，再读占用，防止在快照与等待之间丢失完成事件。 */
    g_rdx_shared_vdd.conn_hint_wanted = 1;
    if (g_rdx_shared_vdd.conn_delivery_failed) {
        g_rdx_shared_vdd.conn_delivery_failed = 0;
        g_rdx_shared_vdd.conn_armed = 0;
    }
    if (!rdx_ble_server_get_connected_count() ||
        !rdx_peripheral_power_vdd_is_ready()) {
        rdx_vdd_conn_cancel();
        g_rdx_shared_vdd.conn_armed = 0;
        g_rdx_shared_vdd.conn_done = 0;
        g_rdx_shared_vdd.conn_hint_wanted = 0;
        return;
    }
    epoch = g_rdx_shared_vdd.wake_epoch;
    version = rdx_adv_policy_business_version();
    busy = rdx_vdd_conn_busy_snapshot();
    now = sys_timer_get_ms();
    if (version != g_rdx_shared_vdd.conn_version ||
        epoch != g_rdx_shared_vdd.conn_epoch) {
        g_rdx_shared_vdd.conn_armed = 0;
        g_rdx_shared_vdd.conn_done = 0;
        g_rdx_shared_vdd.conn_failed = 0;
    }
    if (busy) {
        rdx_vdd_conn_cancel();
        if (busy & ~RDX_VDD_CONN_TRANSIENT_BUSY) {
            g_rdx_shared_vdd.conn_armed = 0;
            g_rdx_shared_vdd.conn_done = 0;
        }
        if (busy != g_rdx_shared_vdd.conn_logged_busy) {
            r_printf("[PWR_CONN] wait busy=0x%03x reason=%s timer=OFF\n",
                     busy, rdx_peripheral_power_vdd_first_busy_name(busy));
        }
        g_rdx_shared_vdd.conn_logged_busy = busy;
        return;
    }
    g_rdx_shared_vdd.conn_logged_busy = 0;
    g_rdx_shared_vdd.conn_hint_wanted = 0;
    if (g_rdx_shared_vdd.conn_failed) {
        rdx_vdd_conn_cancel();
        return;
    }
    if (!g_rdx_shared_vdd.conn_armed) {
        rdx_vdd_conn_cancel();
        g_rdx_shared_vdd.conn_version = version;
        g_rdx_shared_vdd.conn_epoch = epoch;
        g_rdx_shared_vdd.conn_deadline = now + TCFG_T2620_CONNECTED_VDD_IDLE_MS;
        g_rdx_shared_vdd.conn_armed = 1;
        g_rdx_shared_vdd.conn_done = 0;
        r_printf("[PWR_CONN] armed idle_ms=%u version=%u epoch=%u links=%u\n",
                 TCFG_T2620_CONNECTED_VDD_IDLE_MS, version, epoch,
                 rdx_ble_server_get_connected_count());
    }
    if (g_rdx_shared_vdd.conn_done) return;
    if ((s32)(now - g_rdx_shared_vdd.conn_deadline) >= 0) {
        rdx_vdd_conn_cancel();
        g_rdx_shared_vdd.conn_done = 1;
        rdx_peripheral_power_vdd_idle_stop(epoch, RDX_VDD_SRC_CONNECTED);
        return;
    }
    if (!g_rdx_shared_vdd.conn_timer) {
        g_rdx_shared_vdd.conn_timer = sys_timeout_add(
            (void *)g_rdx_shared_vdd.conn_timer_serial, rdx_vdd_conn_timeout,
            g_rdx_shared_vdd.conn_deadline - now);
        if (!g_rdx_shared_vdd.conn_timer) {
            /* 分配失败只保留供电，下一次真实事件重新开始完整窗口。 */
            g_rdx_shared_vdd.conn_armed = 0;
            r_printf("[PWR_CONN] timer_failed pa4=1\n");
        }
    }
}

static void rdx_peripheral_power_vdd_event_on_app_core(int event, int epoch)
{
    u8 links;

    if (!g_rdx_shared_vdd.initialized) {
        rdx_peripheral_power_vdd_early_init();
    }

    switch ((rdx_shared_vdd_event_t)event) {
    case RDX_SHARED_VDD_EVENT_CONNECTED_EVAL:
        g_rdx_shared_vdd.conn_queued = 0;
        break;
    case RDX_SHARED_VDD_EVENT_FAST_ADV:
        rdx_peripheral_power_vdd_idle_recheck_cancel();
        if (!rdx_peripheral_power_vdd_is_ready()) {
            rdx_peripheral_power_vdd_ensure_on(
                RDX_SHARED_VDD_WAKE_FAST_ADV);
        }
        g_rdx_shared_vdd.slow_adv = 0;
        g_rdx_shared_vdd.generation++;
        g_rdx_shared_vdd.busy_mask =
            rdx_peripheral_power_vdd_busy_snapshot();
        r_printf("[PWR] wake gen=%u reason=FAST_ADV pa4=%u busy=0x%02x\n",
                 g_rdx_shared_vdd.generation,
                 g_rdx_shared_vdd.pa4_enabled,
                 g_rdx_shared_vdd.busy_mask);
        break;

    case RDX_SHARED_VDD_EVENT_SLOW_ADV:
        if (!rdx_adv_policy_idle_valid()) break;
        if ((u32)epoch != g_rdx_shared_vdd.wake_epoch) {
            r_printf("[PWR] stale_idle_ignored event_epoch=%u wake_epoch=%u\n",
                     (u32)epoch, g_rdx_shared_vdd.wake_epoch);
            break;
        }
        rdx_peripheral_power_vdd_idle_recheck_cancel();
        g_rdx_shared_vdd.slow_adv = 1;
        g_rdx_shared_vdd.slow_adv_epoch = (u32)epoch;
        g_rdx_shared_vdd.generation++;
        g_rdx_shared_vdd.busy_mask =
            rdx_peripheral_power_vdd_busy_snapshot();
        r_printf("[PWR] idle_request gen=%u links=%u busy=0x%02x\n",
                 g_rdx_shared_vdd.generation,
                 rdx_ble_server_get_connected_count(),
                 g_rdx_shared_vdd.busy_mask);
        rdx_peripheral_power_vdd_idle_evaluate(
            g_rdx_shared_vdd.slow_adv_epoch);
        break;

    case RDX_SHARED_VDD_EVENT_BLE_LINKS_CHANGED:
        rdx_peripheral_power_vdd_idle_recheck_cancel();
        links = rdx_ble_server_get_connected_count();
        if (links && !rdx_peripheral_power_vdd_is_ready()) {
            rdx_peripheral_power_vdd_ensure_on(
                RDX_SHARED_VDD_WAKE_BLE_LINK);
        }
        g_rdx_shared_vdd.generation++;
        /* Every topology change invalidates an older slow-advertising epoch. */
        g_rdx_shared_vdd.slow_adv = 0;
        g_rdx_shared_vdd.busy_mask =
            rdx_peripheral_power_vdd_busy_snapshot();
        r_printf("[PWR] links_changed gen=%u links=%u busy=0x%02x\n",
                 g_rdx_shared_vdd.generation, links,
                 g_rdx_shared_vdd.busy_mask);
        if (g_rdx_shared_vdd.state == RDX_SHARED_VDD_STATE_ON_READY) {
            g_rdx_shared_vdd.wake_requested = 0;
        }
        break;

    case RDX_SHARED_VDD_EVENT_BUSINESS_CHANGED:
        if ((u32)epoch != g_rdx_shared_vdd.wake_epoch) {
            r_printf("[PWR] stale_business_ignored event_epoch=%u wake_epoch=%u\n",
                     (u32)epoch, g_rdx_shared_vdd.wake_epoch);
            break;
        }
        g_rdx_shared_vdd.busy_mask =
            rdx_peripheral_power_vdd_busy_snapshot();
        if (g_rdx_shared_vdd.slow_adv && rdx_adv_policy_idle_valid()) {
            /* A business completion event is a fresh idle observation while
             * advertising is still slow. */
            g_rdx_shared_vdd.slow_adv_epoch =
                g_rdx_shared_vdd.wake_epoch;
            rdx_peripheral_power_vdd_idle_evaluate(
                g_rdx_shared_vdd.slow_adv_epoch);
        }
        break;

    default:
        break;
    }
    rdx_vdd_conn_evaluate();
}

/* 跨任务只合并通知；队列失败不在调用线程修改定时器，也不周期重试。 */
static void rdx_vdd_conn_post(void)
{
    int msg[] = { (int)rdx_peripheral_power_vdd_event_on_app_core, 2,
                  RDX_SHARED_VDD_EVENT_CONNECTED_EVAL, 0 };
    local_irq_disable();
    if (g_rdx_shared_vdd.conn_queued) {
        local_irq_enable();
        return;
    }
    g_rdx_shared_vdd.conn_queued = 1;
    local_irq_enable();
    if (os_taskq_post_type("app_core", Q_CALLBACK, 4, msg)) {
        local_irq_disable();
        g_rdx_shared_vdd.conn_queued = 0;
        g_rdx_shared_vdd.conn_delivery_failed = 1;
        local_irq_enable();
        r_printf("[PWR_CONN] event_failed cutoff_deferred\n");
    }
}

void rdx_peripheral_power_vdd_connected_changed_notify(void)
{
    rdx_vdd_conn_post();
}

void rdx_peripheral_power_vdd_idle_hint(void)
{
    if (g_rdx_shared_vdd.conn_hint_wanted) rdx_vdd_conn_post();
}

static void rdx_peripheral_power_vdd_event_post(rdx_shared_vdd_event_t event,
                                                 u32 epoch)
{
    int msg[4];

    msg[0] = (int)rdx_peripheral_power_vdd_event_on_app_core;
    msg[1] = 2;
    msg[2] = (int)event;
    msg[3] = (int)epoch;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 4, msg)) {
        /* A dropped idle event is fail-safe because PA4 remains high. */
        r_printf("[PWR] event_post_failed event=%u pa4=%u\n",
                 event, g_rdx_shared_vdd.pa4_enabled);
    }
}

int rdx_peripheral_power_vdd_fast_adv_notify(void)
{
    int err = rdx_peripheral_power_vdd_ensure_on(
        RDX_SHARED_VDD_WAKE_FAST_ADV);
    rdx_peripheral_power_vdd_event_post(RDX_SHARED_VDD_EVENT_FAST_ADV,
                                         g_rdx_shared_vdd.wake_epoch);
    return err;
}

void rdx_peripheral_power_vdd_slow_adv_notify(void)
{
    rdx_peripheral_power_vdd_event_post(RDX_SHARED_VDD_EVENT_SLOW_ADV,
                                         g_rdx_shared_vdd.wake_epoch);
}

void rdx_peripheral_power_vdd_ble_links_changed_notify(void)
{
    rdx_ble_server_conn_param_changed();
    if (rdx_ble_server_get_connected_count()) {
        rdx_peripheral_power_vdd_ensure_on(RDX_SHARED_VDD_WAKE_BLE_LINK);
    } else {
        rdx_peripheral_power_vdd_wake_request_begin();
    }
    rdx_peripheral_power_vdd_event_post(
        RDX_SHARED_VDD_EVENT_BLE_LINKS_CHANGED,
        g_rdx_shared_vdd.wake_epoch);
}

void rdx_peripheral_power_vdd_business_changed_notify(void)
{
    rdx_adv_policy_changed();
    rdx_peripheral_power_vdd_event_post(
        RDX_SHARED_VDD_EVENT_BUSINESS_CHANGED,
        g_rdx_shared_vdd.wake_epoch);
}

int rdx_peripheral_power_vdd_usb_prepare(void)
{
    int err;

    g_rdx_shared_vdd.usb_reserved = 1;
    rdx_adv_policy_activity();
    err = rdx_peripheral_power_vdd_ensure_on(
        RDX_SHARED_VDD_WAKE_USB_MSC);
    rdx_peripheral_power_vdd_idle_recheck_cancel();
    if (err) {
        g_rdx_shared_vdd.usb_reserved = 0;
        r_printf("[PWR] usb_prepare_failed err=%d pa4=%u state=%u\n",
                 err, g_rdx_shared_vdd.pa4_enabled,
                 g_rdx_shared_vdd.state);
    }
    return err;
}

void rdx_peripheral_power_vdd_usb_takeover_complete(u8 success)
{
    rdx_peripheral_power_vdd_idle_recheck_cancel();
    if (success) {
        g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_USB_HOST;
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_ON_READY;
        r_printf("[PWR] sd0_owner=USB_HOST pa4=%u\n",
                 g_rdx_shared_vdd.pa4_enabled);
        return;
    }
    g_rdx_shared_vdd.usb_reserved = 0;
    g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_DEVICE;
}

void rdx_peripheral_power_vdd_usb_restore_complete(u8 success)
{
    g_rdx_shared_vdd.usb_reserved = 0;
    if (success) {
        g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_DEVICE;
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_ON_READY;
        r_printf("[PWR] sd0_owner=DEVICE source=USB_RESTORE pa4=%u\n",
                 g_rdx_shared_vdd.pa4_enabled);
        rdx_peripheral_power_vdd_business_changed_notify();
    } else {
        g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_RECOVERY;
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_FAULT_ON;
        r_printf("[PWR] usb_restore_failed owner=RECOVERY pa4=%u\n",
                 g_rdx_shared_vdd.pa4_enabled);
    }
}

rdx_shared_vdd_state_t rdx_peripheral_power_vdd_state_get(void)
{
    return g_rdx_shared_vdd.state;
}

rdx_shared_vdd_owner_t rdx_peripheral_power_vdd_owner_get(void)
{
    return g_rdx_shared_vdd.owner;
}

u8 rdx_peripheral_power_vdd_is_enabled(void)
{
    return g_rdx_shared_vdd.pa4_enabled;
}

u8 rdx_peripheral_power_vdd_is_ready(void)
{
    return g_rdx_shared_vdd.state == RDX_SHARED_VDD_STATE_ON_READY &&
           g_rdx_shared_vdd.owner == RDX_SHARED_VDD_OWNER_DEVICE;
}

u8 rdx_peripheral_power_vdd_is_slow_adv(void)
{
    return g_rdx_shared_vdd.slow_adv;
}

u32 rdx_peripheral_power_vdd_busy_mask_get(void)
{
    return g_rdx_shared_vdd.busy_mask;
}

u32 rdx_peripheral_power_vdd_generation_get(void)
{
    return g_rdx_shared_vdd.generation;
}

#else

void rdx_peripheral_power_vdd_connected_changed_notify(void)
{
}

void rdx_peripheral_power_vdd_idle_hint(void)
{
}

void rdx_peripheral_power_vdd_early_init(void)
{
}

int rdx_peripheral_power_vdd_ensure_on(rdx_shared_vdd_wake_reason_t reason)
{
    (void)reason;
    return 0;
}

int rdx_peripheral_power_vdd_fast_adv_notify(void)
{
    return 0;
}

void rdx_peripheral_power_vdd_slow_adv_notify(void)
{
}

void rdx_peripheral_power_vdd_ble_links_changed_notify(void)
{
}

void rdx_peripheral_power_vdd_business_changed_notify(void)
{
    rdx_adv_policy_changed();
}

int rdx_peripheral_power_vdd_usb_prepare(void)
{
    return 0;
}

void rdx_peripheral_power_vdd_usb_takeover_complete(u8 success)
{
    (void)success;
}

void rdx_peripheral_power_vdd_usb_restore_complete(u8 success)
{
    (void)success;
}

rdx_shared_vdd_state_t rdx_peripheral_power_vdd_state_get(void)
{
    return RDX_SHARED_VDD_STATE_ON_READY;
}

rdx_shared_vdd_owner_t rdx_peripheral_power_vdd_owner_get(void)
{
    return RDX_SHARED_VDD_OWNER_DEVICE;
}

u8 rdx_peripheral_power_vdd_is_enabled(void)
{
    return 0;
}

u8 rdx_peripheral_power_vdd_is_ready(void)
{
    return 1;
}

u8 rdx_peripheral_power_vdd_is_slow_adv(void)
{
    return 0;
}

u32 rdx_peripheral_power_vdd_busy_mask_get(void)
{
    return 0;
}

u32 rdx_peripheral_power_vdd_generation_get(void)
{
    return 0;
}

#endif /* TCFG_T2620_SHARED_VDD_ENABLE && RDX_EN */

#if TCFG_T2620_AMP_POWER_ENABLE && RDX_WIFI_ENABLE
#error "T2620 PE5 amplifier enable conflicts with the legacy RDX WiFi SPI CS assignment"
#endif

#if TCFG_T2620_AMP_POWER_ENABLE

static u8 g_rdx_amp_enabled;
static u8 g_rdx_amp_analog_ready;
static u8 g_rdx_amp_output_running;

enum {
    RDX_AMP_SRC_ANALOG = 0,
    RDX_AMP_SRC_DIGITAL = 1,
};

/* BR28 media.a 中 _audio_dac_status_hook 的数字输出状态值；
 * 由 tests/host/amp_output_checks.py 对照 SDK 库校验，库升级时需复核。 */
enum {
    RDX_DAC_STATUS_INIT = 0,
    RDX_DAC_STATUS_START = 2,
    RDX_DAC_STATUS_STOP = 4,
};

#if TCFG_T2620_AMP_TRACE_ENABLE
/* PE5 边沿诊断：回调上下文只记录，app_core 统一打印 [AMP]。 */
#define RDX_AMP_TRACE_SIZE 8
struct rdx_amp_trace {
    u32 seq;
    u32 ms;
    u8 src;
    u8 state;
    u8 pe5;
    u8 analog_ready;
    u8 output_running;
};
static struct rdx_amp_trace g_rdx_amp_trace[RDX_AMP_TRACE_SIZE];
static u32 g_rdx_amp_trace_seq;
static u8 g_rdx_amp_trace_head;
static u8 g_rdx_amp_trace_count;
static u8 g_rdx_amp_trace_queued;
static u32 g_rdx_amp_trace_dropped;
#endif

void rdx_peripheral_power_amp_set(u8 enable)
{
    u8 next = !!enable;

    if (g_rdx_amp_enabled == next) {
        return;
    }

    gpio_set_mode(IO_PORT_SPILT(TCFG_T2620_AMP_ENABLE_IO),
                  next ? PORT_OUTPUT_HIGH : PORT_OUTPUT_LOW);
    g_rdx_amp_enabled = next;
}

#if TCFG_T2620_AMP_TRACE_ENABLE
static const char *rdx_amp_trace_event_name(u8 src, u8 state)
{
    if (src == RDX_AMP_SRC_DIGITAL) {
        return state == RDX_DAC_STATUS_INIT ? "DAC_INIT" :
               state == RDX_DAC_STATUS_START ? "DAC_START" :
               state == RDX_DAC_STATUS_STOP ? "DAC_STOP" : "DAC_UNKNOWN";
    }
    return state == DAC_ANALOG_OPEN_PREPARE ? "ANA_OPEN_PREP" :
           state == DAC_ANALOG_OPEN_FINISH ? "ANA_OPEN_DONE" :
           state == DAC_ANALOG_CLOSE_PREPARE ? "ANA_CLOSE_PREP" :
           state == DAC_ANALOG_CLOSE_FINISH ? "ANA_CLOSE_DONE" : "ANA_UNKNOWN";
}

static void rdx_amp_trace_flush(void *unused)
{
    struct rdx_amp_trace item;
    u32 dropped;

    (void)unused;
    while (1) {
        local_irq_disable();
        if (!g_rdx_amp_trace_count) {
            dropped = g_rdx_amp_trace_dropped;
            g_rdx_amp_trace_dropped = 0;
            g_rdx_amp_trace_queued = 0;
            local_irq_enable();
            break;
        }
        item = g_rdx_amp_trace[(u8)(g_rdx_amp_trace_head - g_rdx_amp_trace_count) % RDX_AMP_TRACE_SIZE];
        --g_rdx_amp_trace_count;
        local_irq_enable();
        r_printf("[AMP] pe5=%u ev=%s analog=%u output=%u seq=%u t=%u\n",
                 item.pe5, rdx_amp_trace_event_name(item.src, item.state),
                 item.analog_ready, item.output_running, item.seq, item.ms);
    }
    if (dropped) {
        r_printf("[AMP] dropped=%u\n", dropped);
    }
}

/* 仅记录 PE5 实际翻转；返回 1 表示需要投递一次打印任务。 */
static u8 rdx_amp_trace_record(u8 src, u8 state, u8 old_pe5)
{
    struct rdx_amp_trace *item;

    if (old_pe5 == g_rdx_amp_enabled) {
        return 0;
    }
    if (g_rdx_amp_trace_count >= RDX_AMP_TRACE_SIZE) {
        ++g_rdx_amp_trace_dropped;
        --g_rdx_amp_trace_count;
    }
    item = &g_rdx_amp_trace[g_rdx_amp_trace_head];
    item->seq = ++g_rdx_amp_trace_seq;
    item->ms = sys_timer_get_ms();
    item->src = src;
    item->state = state;
    item->pe5 = g_rdx_amp_enabled;
    item->analog_ready = g_rdx_amp_analog_ready;
    item->output_running = g_rdx_amp_output_running;
    g_rdx_amp_trace_head = (g_rdx_amp_trace_head + 1) % RDX_AMP_TRACE_SIZE;
    ++g_rdx_amp_trace_count;
    if (g_rdx_amp_trace_queued) {
        return 0;
    }
    g_rdx_amp_trace_queued = 1;
    return 1;
}

/* 在临界区外投递；投递失败时清除排队标记，下一次翻转重试。 */
static void rdx_amp_trace_post(void)
{
    /* 必须覆盖参数描述，避免复用缓冲中的旧标志触发返回值写回或信号量操作。 */
    int msg[3] = { (int)rdx_amp_trace_flush, 1, 0 };

    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        local_irq_disable();
        g_rdx_amp_trace_queued = 0;
        local_irq_enable();
    }
}
#endif

u8 rdx_peripheral_power_amp_is_enabled(void)
{
    return g_rdx_amp_enabled;
}

/*
 * PE5 = 模拟就绪 && 数字输出运行。
 * BLE 连接后 SDK 不关闭 DAC 模拟，只跟随模拟生命周期会让功放常开；
 * 这里只控制功放，不强关 DAC 模拟，避免影响共享资源的 ADC/MIC。
 */
static void rdx_amp_apply(u8 src, u8 state)
{
    u8 old_pe5;
    u8 post = 0;

    local_irq_disable();
    old_pe5 = g_rdx_amp_enabled;
    if (src == RDX_AMP_SRC_ANALOG) {
        /* 只有 OPEN_FINISH 表示模拟就绪；其余状态同时作废输出状态，需重新 START。 */
        g_rdx_amp_analog_ready = state == DAC_ANALOG_OPEN_FINISH;
        if (!g_rdx_amp_analog_ready) {
            g_rdx_amp_output_running = 0;
        }
    } else {
        /* INIT 表示 DAC 重新初始化，模拟就绪需等待新的 OPEN_FINISH；未知状态按停止处理。 */
        g_rdx_amp_output_running = state == RDX_DAC_STATUS_START;
        if (state == RDX_DAC_STATUS_INIT) {
            g_rdx_amp_analog_ready = 0;
        }
    }
    rdx_peripheral_power_amp_set(g_rdx_amp_analog_ready && g_rdx_amp_output_running);
#if TCFG_T2620_AMP_TRACE_ENABLE
    post = rdx_amp_trace_record(src, state, old_pe5);
#endif
    local_irq_enable();
#if TCFG_T2620_AMP_TRACE_ENABLE
    if (post) {
        rdx_amp_trace_post();
    }
#endif
    (void)old_pe5;
    (void)post;
}

/* SDK 模拟通路回调：OPEN_FINISH 后才允许开功放。 */
void audio_dac_power_state(u8 state)
{
    rdx_amp_apply(RDX_AMP_SRC_ANALOG, state);
}

/* SDK 数字输出回调（覆盖 media.a 弱函数）：START 开、STOP（淡出后）立即关。 */
void _audio_dac_status_hook(u8 status)
{
    rdx_amp_apply(RDX_AMP_SRC_DIGITAL, status);
}

#else

void rdx_peripheral_power_amp_set(u8 enable)
{
    (void)enable;
}

u8 rdx_peripheral_power_amp_is_enabled(void)
{
    return 0;
}

#endif /* TCFG_T2620_AMP_POWER_ENABLE */
