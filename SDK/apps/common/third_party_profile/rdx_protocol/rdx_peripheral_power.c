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
#include "rdx_playback.h"
#include "rdx_protocol.h"
#include "rdx_record.h"
#include "rdx_uxfile.h"

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
} rdx_shared_vdd_event_t;

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

static const char *rdx_peripheral_power_vdd_mode_name(void)
{
#if TCFG_T2620_SHARED_VDD_MODE == T2620_SHARED_VDD_MODE_DRY_RUN
    return "DRY_RUN";
#elif TCFG_T2620_SHARED_VDD_MODE == T2620_SHARED_VDD_MODE_UNMOUNT_ONLY
    return "UNMOUNT_ONLY";
#else
    return "POWER_CUT";
#endif
}

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
    if (!g_rdx_shared_vdd.slow_adv ||
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
    if (!g_rdx_shared_vdd.slow_adv ||
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

static void rdx_peripheral_power_vdd_stop_abort(const char *step)
{
    int err = 0;

    /* A canceled transition must be eligible for a later business-complete
     * retry within the same slow-advertising generation. */
    g_rdx_shared_vdd.last_would_off_generation = (u32)-1;

    if (g_rdx_shared_vdd.owner == RDX_SHARED_VDD_OWNER_IDLE_BLOCKED) {
        err = rdx_peripheral_power_vdd_restore_idle_domain(
            RDX_SHARED_VDD_WAKE_BUSINESS);
    } else {
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_ON_READY;
    }
    r_printf("[PWR] stop_aborted step=%s err=%d links=%u busy=0x%02x pa4=%u\n",
             step, err, rdx_ble_server_get_connected_count(),
             g_rdx_shared_vdd.busy_mask,
             g_rdx_shared_vdd.pa4_enabled);
    if (!err && g_rdx_shared_vdd.slow_adv &&
        g_rdx_shared_vdd.busy_mask) {
        rdx_peripheral_power_vdd_idle_recheck_schedule(
            g_rdx_shared_vdd.wake_epoch);
    }
}

static void rdx_peripheral_power_vdd_idle_stop(u32 idle_epoch)
{
    u32 stop_epoch;
    u8 canceled;
    int err;

#if TCFG_T2620_SHARED_VDD_MODE == T2620_SHARED_VDD_MODE_DRY_RUN
    r_printf("[PWR] would_off gen=%u mode=%s pa4=%u state=ON_READY\n",
             g_rdx_shared_vdd.generation,
             rdx_peripheral_power_vdd_mode_name(),
             g_rdx_shared_vdd.pa4_enabled);
    return;
#endif

    if (g_rdx_shared_vdd.transition_mutex_initialized) {
        os_mutex_pend(&g_rdx_shared_vdd.transition_mutex, 0);
    }

    local_irq_disable();
    stop_epoch = idle_epoch;
    canceled = g_rdx_shared_vdd.wake_requested ||
               g_rdx_shared_vdd.wake_epoch != stop_epoch ||
               g_rdx_shared_vdd.state != RDX_SHARED_VDD_STATE_ON_READY;
    if (canceled) {
        local_irq_enable();
        r_printf("[PWR] stop_aborted step=PRECHECK idle_epoch=%u wake_epoch=%u state=%u\n",
                 stop_epoch, g_rdx_shared_vdd.wake_epoch,
                 g_rdx_shared_vdd.state);
        goto __exit;
    }
    g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_STOPPING;
    local_irq_enable();
    r_printf("[PWR] stopping gen=%u mode=%s epoch=%u\n",
             g_rdx_shared_vdd.generation,
             rdx_peripheral_power_vdd_mode_name(), stop_epoch);

    err = dev_manager_takeover("sd0");
    if (err) {
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_ON_READY;
        r_printf("[PWR] stop_failed step=SD0_TAKEOVER err=%d pa4=%u\n",
                 err, g_rdx_shared_vdd.pa4_enabled);
        goto __exit;
    }
    g_rdx_shared_vdd.owner = RDX_SHARED_VDD_OWNER_IDLE_BLOCKED;
    r_printf("[PWR] sd0_owner=IDLE_BLOCKED\n");

    g_rdx_shared_vdd.busy_mask =
        rdx_peripheral_power_vdd_busy_snapshot();
    if (g_rdx_shared_vdd.wake_requested ||
        g_rdx_shared_vdd.wake_epoch != stop_epoch ||
        rdx_ble_server_get_connected_count() ||
        g_rdx_shared_vdd.busy_mask || !g_rdx_shared_vdd.slow_adv) {
        rdx_peripheral_power_vdd_stop_abort("POST_TAKEOVER_RECHECK");
        goto __exit;
    }

    err = rdx_led_hardware_deinit();
    if (err) {
        rdx_peripheral_power_vdd_stop_abort("RGB_QUIESCE");
        goto __exit;
    }
    r_printf("[PWR] rgb_quiesced data=LOW spi=OFF\n");

    g_rdx_shared_vdd.busy_mask =
        rdx_peripheral_power_vdd_busy_snapshot();

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
               !g_rdx_shared_vdd.slow_adv;
    if (!canceled) {
#if TCFG_T2620_SHARED_VDD_MODE == T2620_SHARED_VDD_MODE_POWER_CUT
        rdx_peripheral_power_vdd_storage_io_safe();
        rdx_peripheral_power_vdd_hw_set(0);
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_OFF;
#else
        g_rdx_shared_vdd.state = RDX_SHARED_VDD_STATE_QUIESCED_ON;
#endif
    }
    local_irq_enable();

    if (canceled) {
        rdx_peripheral_power_vdd_stop_abort("FINAL_RECHECK");
        goto __exit;
    }

#if TCFG_T2620_SHARED_VDD_MODE == T2620_SHARED_VDD_MODE_POWER_CUT
    r_printf("[PWR] pa4=0 state=OFF owner=IDLE_BLOCKED\n");
#else
    r_printf("[PWR] quiesced mode=UNMOUNT_ONLY pa4=1 state=QUIESCED_ON owner=IDLE_BLOCKED\n");
#endif

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
    if (!g_rdx_shared_vdd.slow_adv) {
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
    rdx_peripheral_power_vdd_idle_stop(idle_epoch);
}

static void rdx_peripheral_power_vdd_event_on_app_core(int event, int epoch)
{
    u8 links;

    if (!g_rdx_shared_vdd.initialized) {
        rdx_peripheral_power_vdd_early_init();
    }

    switch ((rdx_shared_vdd_event_t)event) {
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
        if (g_rdx_shared_vdd.slow_adv) {
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
    rdx_peripheral_power_vdd_event_post(
        RDX_SHARED_VDD_EVENT_BUSINESS_CHANGED,
        g_rdx_shared_vdd.wake_epoch);
}

int rdx_peripheral_power_vdd_usb_prepare(void)
{
    int err;

    g_rdx_shared_vdd.usb_reserved = 1;
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

u8 rdx_peripheral_power_amp_is_enabled(void)
{
    return g_rdx_amp_enabled;
}

/*
 * JL's DAC implementation calls this application override synchronously.
 * Keep the external amplifier disabled until the DAC analog path is ready,
 * and disable it before the DAC analog path starts closing.
 */
void audio_dac_power_state(u8 state)
{
    switch (state) {
    case DAC_ANALOG_OPEN_FINISH:
        rdx_peripheral_power_amp_set(1);
        break;

    case DAC_ANALOG_OPEN_PREPARE:
    case DAC_ANALOG_CLOSE_PREPARE:
    case DAC_ANALOG_CLOSE_FINISH:
    default:
        rdx_peripheral_power_amp_set(0);
        break;
    }
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
