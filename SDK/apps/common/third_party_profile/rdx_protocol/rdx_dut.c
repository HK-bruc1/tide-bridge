/*=====================================================================================
 HEADER NAME: rdx_dut.c
 MODULE NAME: DUT (Device Under Test) application module.
 
 PRE-INCLUDE FILES DESCRIPTION: 	
 
 GENERAL DESCRIPTION: 	
 	DUT mode functions for factory testing (PIN project).
    Ported from BJ Tide project, adapted for PIN hardware (LED + motor, no OLED).
    
    PIN DUT测试项目:
    - 单击: LED灯光测试 (ft_oled / 兼容命名)
    - 双击: 马达震动测试 (ft_motor)
    - 三击: 录音测试     (ft_rec)
    - 四击: WiFi测试      (ft_wifi)
    - 五击: 格式化存储     (ft_format)
    - 六击: 关机          (ft_poweroff)
 =======================================================================================	
 Revision History: 
 ---------------------------------------
 Author: auto-ported from BJ Tide project
 Date: 2026-02-24
 FilePath: \SDK\apps\common\third_party_profile\rdx_protocol\rdx_dut.c
 
 Self-documenting Code
=====================================================================================*/

/******************************************************************************
* Include files
******************************************************************************/ 
#ifdef SUPPORT_MS_EXTENSIONS
#pragma bss_seg(".rdx_dut.data.bss")
#pragma data_seg(".rdx_dut.data")
#pragma const_seg(".rdx_dut.text.const")
#pragma code_seg(".rdx_dut.text")
#endif

#include "rdx_hogp_key_action.h"
#include "rdx_dut.h"
#include "rdx_record.h"
#include "rdx_factory_audio.h"
#include "rdx_protocol.h"
#include "rdx_ble_server.h"
#include "rdx_uxfile.h"
#include "rdx_app.h"
#include "rdx_app_config.h"
#include "rdx_charge.h"
#include "motor.h"
#include "app_msg.h"
#include "app_main.h"
#include "poweroff.h"
#include "rdx_led_ctrl.h"
#include "syscfg_id.h"
#include "rdx_dip_switch.h"
#include "rdx_storage_lifecycle.h"
#include "usb/device/usb_factory.h"
#include "rdx_dut_speaker.h"
#include "key_driver.h"
#include "generic/jiffies.h"
#include "usb/device/usb_factory_cdc.h"

/******************************************************************************
* Macro Define Section
******************************************************************************/ 
#define DUT_LOG(str, ...)   y_printf("[DUT]%s,L%d--> "str,__FUNCTION__,__LINE__,##__VA_ARGS__)

/******************************************************************************
* External Function Declaration
******************************************************************************/ 
extern void rdx_app_bt_open(void);
extern void rdx_app_bt_shutdown(void);
extern void rdx_app_motor_run_once(void);
extern void rdx_app_wifi_handle(u8 cmd);
extern void rdx_app_device_record_handle(u8 scene);
extern void rdx_app_emmc_poweron(u8 check_en);
extern void rdx_app_emmc_poweroff_check_timer_stop(void);
extern void rdx_app_emmc_poweroff_check(void);
extern void rdx_spp_init(void);
extern void rdx_spp_exit(void);
extern void rdx_ble_server_app_disconnect(void);
extern void rdx_ble_server_adv_data_changed(void);
extern void rdx_ble_server_auto_shut_down_enable(u8 enable);
extern int rdx_ble_server_adv_enable(u8 enable);
extern void bt_bredr_enter_dut_mode(u8 a, u8 b);
extern void bt_bredr_exit_dut_mode(void);
extern u8 get_ota_status(void);
extern ReqFileInfo *rdx_protocol_get_uploadfileInfo(void);
extern void rdx_app_normal_poweroff(void);

/******************************************************************************
* Function Declaration Section
******************************************************************************/ 
static bool g_finalpack_end_pending;
static bool g_finalpack_format_waiting;
extern bool rdx_uxfile_sd_format_status_check(void);
static volatile u8 factory_usb_revoked;

int rdx_dut_factory_usb_ready(void)
{
    /* RDX owns product admission; USB retains cable/role/resource checks. */
    return !factory_usb_revoked &&
        get_power_on_status() && rdx_app_business_started() &&
        !app_var.goto_poweroff_flag;
}

static void rdx_dut_factory_usb_revoke(void)
{
    factory_usb_revoked = 1;
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
    usb_factory_service();
#endif
}

/* Physical entry keeps its own qualification; USB communication is independent. */
void rdx_dut_key_mode_handle(void)
{
#if !TCFG_T2620_FACTORY_USB_CDC_ENABLE
    rdx_dut_msg_handle();
#else
    u8 entering = !rdx_dut_is_in_mode();
    if (entering && (rdx_dut_is_key_dut_disabled() ||
        !get_power_on_status() || !rdx_app_business_started() ||
        app_var.goto_poweroff_flag || rdx_storage_lifecycle_business_blocked() ||
        rdx_storage_lifecycle_shutdown_deferred())) {
        return;
    }
    rdx_dut_msg_handle();
    usb_factory_service();
#endif
}

static void rdx_dut_motor_timer_cb(void *priv);
static void rdx_dut_show(void);
static void rdx_dut_format_stop(void);

/******************************************************************************
* Local Variables Section
******************************************************************************/ 
static rdx_dut_info_t rdx_dut_info = {
    .dut_mode = FALSE,
    .key_dut_disabled = FALSE,
    .current_func = DUT_FUNC_NONE,
    .motor_timer = 0,
    .motor_run = FALSE,
};

/******************************************************************************
* DUT命令类型定义（用于异步处理）
******************************************************************************/ 
typedef enum {
    DUT_CMD_DUT_MODE = 0,
    DUT_CMD_OLED,
    DUT_CMD_MOTOR,
    DUT_CMD_REC,
    DUT_CMD_REC_CALL,
    DUT_CMD_WIFI,
    DUT_CMD_FORMAT,
    DUT_CMD_POWEROFF,
    DUT_CMD_FINALPACK_END,
    DUT_CMD_KEY_DUT_ENABLE,
    DUT_CMD_KEY_DUT_DISABLE,
    DUT_CMD_KEY_LAYOUT,
    DUT_CMD_KEY_STOP,
    DUT_CMD_SPEAKER,
} DUT_CMD_TYPE;

/* DUT_TEST_CORE_BEGIN: serialized by the app_core service timer. */
#define DUT_QUEUE_CAPACITY 16
static const struct {
    u8 value;
    char id;
    u8 row;
} dut_keys[] = {
    { KEY_IO_NUM0, 'A', 0 }, { KEY_IO_NUM1, 'B', 0 },
    { KEY_IO_NUM2, 'C', 1 }, { KEY_IO_NUM3, 'D', 1 },
    { KEY_IO_NUM4, 'E', 2 },
};
struct dut_request {
    rdx_ble_async_token_t token;
    u8 command;
    u8 value;
    u8 source; /* 0 BLE, 1 CDC, 2 physical key */
    u32 generation;
    u32 id;
};
static struct dut_request dut_queue[DUT_QUEUE_CAPACITY];
static u8 dut_head, dut_count;
static u16 dut_service_timer;
static u16 dut_service_period = 1000;
static volatile u8 dut_emergency_cancel;
static volatile u8 dut_queue_abort;
static volatile u8 dut_key_gate;
static volatile u32 dut_key_generation;
static u32 dut_key_cutoff[5];
static u8 dut_key_quarantined[5];
static rdx_ble_async_token_t dut_test_token;
static struct dut_request dut_pending;
static struct dut_request dut_owner;
static struct dut_request dut_last;
static u32 dut_next_id, dut_key_test_id;
static u8 dut_key_seen;
static u8 dut_last_state; /* 0 NONE, 1 RUNNING, 2 DONE, 3 FAILED */
static const char *dut_last_error = "NONE";
static u32 dut_action_since;
static volatile u8 dut_ble_revoked;
static volatile u32 dut_ble_revoked_id;
static volatile u32 dut_completion_id;
static volatile int dut_completion_result;
static u32 dut_format_id;
static void rdx_dut_finalpack_commit(int result);
static void dut_poll_action(void);
static const char *dut_accept(struct dut_request *request);
static void dut_mode_apply(void);
static u8 dut_waiting; /* 1=start, 2=stop */
static u8 dut_stopping;
static u32 dut_wait_started;
static u8 dut_record_wait, dut_record_posted;
/* Storage errors are latched by the library until reboot. */
static u8 dut_record_failed;
static u32 dut_record_ticket;
static void
rdx_dut_cmd_async_handle(u8 cmd_type, u8 onoff);
static void dut_service(void *priv);

static bool dut_token_ready(const rdx_ble_async_token_t *token)
{
    rdx_ble_link_state_t *link = rdx_ble_session_rdx_token_resolve(token, 1);
    return link && link->rdx_ccc_configured && link->rdx_stream_tx_ready;
}

static bool dut_request_ready(const struct dut_request *request)
{
    if (request->source == 2) return true;
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
    if (request->source == 1) return request->generation &&
        request->generation == factory_cdc_generation();
#endif
    return request->source == 0 && dut_token_ready(&request->token);
}

static void dut_finish(u32 id, const char *error)
{
    if (!id || id != dut_last.id || dut_last_state != 1) return;
    dut_last_state = error ? 3 : 2;
    dut_last_error = error ? error : "NONE";
}

static bool dut_business_ready(void)
{
    return rdx_dut_info.dut_mode && !dut_record_failed && !g_finalpack_end_pending &&
        get_power_on_status() && !app_var.goto_poweroff_flag &&
        !rdx_storage_lifecycle_business_blocked() &&
        !rdx_storage_lifecycle_shutdown_deferred();
}

static int dut_send(const rdx_ble_async_token_t *token,
                    const char *name, const char *value)
{
    char text[64];
    int length = snprintf(text, sizeof(text), "*DEV#custom#%s#%s#", name, value);
    int offset = 0;
    rdx_ble_link_state_t *link = rdx_ble_session_rdx_token_resolve(token, 1);
    if (!dut_token_ready(token) || !link || length <= 0 || length >= sizeof(text)) {
        return -1;
    }
    /* Session mtu_size is the ATT payload (MTU - 3). Text fragments use the
     * same characteristic and FIFO; no event is submitted before the tail. */
    int payload = link->mtu_size;
    if (payload < 20) {
        return -1;
    }
    while (offset < length) {
        int count = MIN(payload, length - offset);
        if (rdx_ble_server_send_for_token((u8 *)text + offset, count, token)) {
            DUT_LOG("Test response failed: %s offset=%d\n", name, offset);
            return -1;
        }
        offset += count;
    }
    return 0;
}

static int dut_layout(char *out, int capacity)
{
    int size = ARRAY_SIZE(dut_keys);
    int offset = 0;
    if (!size) {
        if (capacity < 5) return -1;
        strcpy(out, "NONE");
        return 0;
    }
    if (capacity < size * 2 || dut_keys[0].row != 0) return -1;
    for (int i = 0; i < size; ++i) {
        if (dut_keys[i].value < KEY_IO_NUM0 || dut_keys[i].value > KEY_IO_NUM4 ||
            dut_keys[i].id != 'A' + i) return -1;
        for (int j = 0; j < i; ++j) {
            if (dut_keys[j].value == dut_keys[i].value) return -1;
        }
        if (i) {
            if (dut_keys[i].row < dut_keys[i - 1].row ||
                dut_keys[i].row > dut_keys[i - 1].row + 1) return -1;
            out[offset++] = dut_keys[i].row == dut_keys[i - 1].row ? ',' : '&';
        }
        out[offset++] = dut_keys[i].id;
    }
    out[offset] = 0;
    return size;
}

bool rdx_dut_test_keys_active(void)
{
    return rdx_dut_info.current_func == DUT_FUNC_KEY ||
           rdx_dut_info.current_func == DUT_FUNC_SPEAKER;
}

static void dut_key_event(u32 generation, u32 value)
{
    char id[2] = {0};
    if (!dut_key_gate || generation != dut_key_generation ||
        rdx_dut_info.current_func != DUT_FUNC_KEY || !dut_business_ready() ||
        !dut_request_ready(&dut_owner)) {
        return;
    }
    for (int i = 0; i < ARRAY_SIZE(dut_keys); ++i) {
        if (dut_keys[i].value == value) {
            dut_key_seen |= 1 << i;
            if (dut_owner.source != 0) return;
            id[0] = dut_keys[i].id;
            if (dut_send(&dut_test_token, FT_KEY_EVENT, id)) {
                rdx_dut_test_cancel();
            }
            return;
        }
    }
}

/* Called after debounce, before click/hold aggregation. Consuming the scan
 * resets its aggregation counters, so a test press never becomes a late
 * normal click. Keep consuming a held key across stop until release. */
bool rdx_dut_key_scan(u8 value, u8 previous)
{
    bool active = rdx_dut_test_keys_active();
    bool consume = active;
    u32 now = jiffies_to_msecs(jiffies);
    if (previous >= KEY_IO_NUM0 && previous <= KEY_IO_NUM4 &&
        dut_key_quarantined[previous - KEY_IO_NUM0]) {
        consume = true;
    }
    if (value >= KEY_IO_NUM0 && value <= KEY_IO_NUM4) {
        int index = value - KEY_IO_NUM0;
        if (active || (value == previous && dut_key_quarantined[index])) {
            dut_key_quarantined[index] = 1;
            dut_key_cutoff[index] = now;
            consume = true;
            if (value != previous && dut_key_gate) {
                int msg[4] = {(int)dut_key_event, 2, dut_key_generation, value};
                if (os_taskq_post_type("app_core", Q_CALLBACK, 4, msg)) {
                    dut_emergency_cancel = 1;
                }
            }
        } else if (value != previous) {
            dut_key_quarantined[index] = 0;
        }
    }
    return consume;
}

bool rdx_dut_key_consume(u8 value, u32 timestamp)
{
    if (value < KEY_IO_NUM0 || value > KEY_IO_NUM4) {
        return rdx_dut_test_keys_active();
    }
    int index = value - KEY_IO_NUM0;
    /* Bound the stale-message window. Retaining a signed timestamp cutoff
     * forever would suppress normal keys after the millisecond counter wraps. */
    if (!dut_key_quarantined[index] &&
        (u32)(jiffies_to_msecs(jiffies) - dut_key_cutoff[index]) > 60000) {
        dut_key_cutoff[index] = 0;
    }
    return rdx_dut_test_keys_active() || dut_key_quarantined[index] ||
        (dut_key_cutoff[index] && (s32)(timestamp - dut_key_cutoff[index]) <= 0);
}

void rdx_dut_test_cancel(void)
{
    /* app_core only. A queued event retains its old generation. */
    dut_key_gate = 0;
    ++dut_key_generation;
    dut_waiting = 0;
    if (rdx_dut_info.current_func == DUT_FUNC_KEY) {
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    if (rdx_dut_info.current_func == DUT_FUNC_SPEAKER) {
        dut_stopping = 1;
        rdx_dut_speaker_cancel();
    }
}

static int dut_enqueue(u8 command, u8 value)
{
    struct dut_request request = {0};
    request.command = command;
    request.value = value;
    if (!dut_service_timer ||
        !rdx_ble_session_rdx_token_capture(&request.token, 1)) return -1;
    local_irq_disable();
    if (dut_count == DUT_QUEUE_CAPACITY || dut_emergency_cancel) {
        /* Fail closed, including an unqueueable stop. The service timer is
         * independent of app_core's Q_CALLBACK capacity. */
        if (dut_count == DUT_QUEUE_CAPACITY) dut_queue_abort = 1;
        if (dut_owner.source != 1) dut_emergency_cancel = 1;
        local_irq_enable();
        DUT_LOG("DUT queue full; cancel requested\n");
        return -1;
    }
    dut_queue[(dut_head + dut_count) % DUT_QUEUE_CAPACITY] = request;
    ++dut_count;
    local_irq_enable();
    DUT_LOG("Queued cmd=%u value=%u func=%u speaker=%d wait=%u stopping=%u record_wait=%u\n",
            command, value, rdx_dut_info.current_func, rdx_dut_speaker_state(),
            dut_waiting, dut_stopping, dut_record_wait);
    int kick[3] = {(int)dut_service, 1, 0};
    /* Queue owns the request even if this wakeup is full. The periodic
     * service retries; while DUT is active it runs every 10 ms. */
    os_taskq_post_type("app_core", Q_CALLBACK, 3, kick);
    return 0;
}

void rdx_dut_test_session_revoke(void)
{
    rdx_record_dut_ble_revoke();
    /* BLE task: revoke producers immediately; timer performs app_core cleanup. */
    dut_ble_revoked_id = dut_owner.id;
    dut_ble_revoked = 1; /* app_core checks resource ownership before cleanup. */
}

static void dut_execute_test(const struct dut_request *request)
{
    u8 command = request->command;
    u8 value = request->value;
    if (!dut_business_ready() || !dut_request_ready(request)) {
        dut_finish(request->id, "STATE");
        DUT_LOG("Test command rejected: DUT/session not ready\n");
        return;
    }
    if (command == DUT_CMD_KEY_STOP) {
        if (rdx_dut_info.current_func == DUT_FUNC_KEY) rdx_dut_test_cancel();
        return;
    }
    if (command == DUT_CMD_KEY_LAYOUT || value) {
        RecordStatus *record = rdx_record_get_status();
        if (record->run != RECORD_STATE_STOP ||
            record->process_state == REC_PROCESS_STATE_BUSY ||
            rdx_uxfile_sd_format_status_check() || get_ota_status()) {
            DUT_LOG("Test start rejected: audio/storage busy\n");
            return;
        }
    }
    if (command == DUT_CMD_KEY_LAYOUT) {
        char layout[24];
        if (rdx_dut_info.current_func != DUT_FUNC_NONE &&
            rdx_dut_info.current_func != DUT_FUNC_KEY) {
            DUT_LOG("Key test rejected: another test owns resources\n");
            return;
        }
        int keys = dut_layout(layout, sizeof(layout));
        if (keys < 0) {
            DUT_LOG("Invalid DUT key layout\n");
            return;
        }
        rdx_hogp_key_action_reset();
        dut_key_gate = 0;
        ++dut_key_generation;
        dut_test_token = request->token;
        dut_owner = *request;
        dut_key_test_id = request->id;
        dut_key_seen = 0;
        rdx_dut_info.current_func = keys ? DUT_FUNC_KEY : DUT_FUNC_NONE;
        /* Enable capture before enqueueing the layout. Scan events execute
         * later on app_core, after the complete layout has been accepted. */
        dut_key_gate = keys != 0;
        if (request->source == 0 && dut_send(&request->token, FT_KEY_LAYOUT, layout)) {
            dut_finish(request->id, "IO");
            rdx_dut_test_cancel();
        }
        return;
    }
    if (!value) {
        if (rdx_dut_info.current_func != DUT_FUNC_SPEAKER) {
            if (request->source == 0) dut_send(&request->token, FT_SPEAKER, "0");
            return;
        }
        dut_stopping = 1;
        dut_pending = *request;
        dut_waiting = 2;
        dut_wait_started = jiffies_to_msecs(jiffies);
        rdx_dut_speaker_cancel();
        return;
    }
    if (rdx_dut_info.current_func == DUT_FUNC_SPEAKER) {
        if (!dut_stopping && rdx_dut_speaker_state() == DUT_AUDIO_PLAYING &&
            request->source == 0 && dut_send(&request->token, FT_SPEAKER, "1")) {
            rdx_dut_test_cancel();
        }
        return;
    }
    if (rdx_dut_info.current_func != DUT_FUNC_NONE) {
        DUT_LOG("Speaker test rejected: another test owns resources\n");
        return;
    }
    rdx_hogp_key_action_reset();
    rdx_dut_info.current_func = DUT_FUNC_SPEAKER;
    dut_test_token = request->token;
    dut_owner = *request;
    dut_stopping = 0;
    if (rdx_dut_speaker_open()) {
        rdx_dut_info.current_func = DUT_FUNC_NONE;
        dut_finish(request->id, "IO");
        DUT_LOG("Speaker open rejected\n");
        return;
    }
    dut_pending = *request;
    dut_waiting = 1;
    dut_wait_started = jiffies_to_msecs(jiffies);
}

static void dut_service(void *priv)
{
    if (dut_completion_id) {
        u32 id = dut_completion_id;
        int result = dut_completion_result;
        dut_completion_id = 0;
        if (id == dut_last.id && dut_last_state == 1) {
            if (dut_last.command == DUT_CMD_FINALPACK_END) rdx_dut_finalpack_commit(result);
            else {
                rdx_dut_info.current_func = DUT_FUNC_NONE;
                dut_finish(id, result == MEM_FORMAT_RESULT_OK ? NULL : "IO");
                rdx_dut_show();
            }
        }
    }
    u16 period = (rdx_dut_info.dut_mode || dut_count || dut_stopping ||
                  dut_record_wait || rdx_dut_test_keys_active()) ? 10 : 1000;
    if (period != dut_service_period) {
        sys_timer_modify(dut_service_timer, period);
        dut_service_period = period;
    }
    if (rdx_dut_info.dut_mode && rdx_dut_test_keys_active() && !get_power_on_status()) {
        dut_mode_apply();
    }
    if (dut_ble_revoked) {
        dut_ble_revoked = 0;
        /* Accepted format/finalpack operations outlive the BLE session.
         * Keep their completion eligible; only the real result or action
         * timeout may finish them, never a transport disconnect. */
        if (dut_owner.source == 0 && dut_owner.id == dut_ble_revoked_id &&
            dut_last.command != DUT_CMD_FORMAT &&
            dut_last.command != DUT_CMD_FINALPACK_END) {
            if (rdx_dut_rec_is_running() || rdx_dut_rec_call_is_running())
                rdx_dut_close_current_func();
            else rdx_dut_test_cancel();
            dut_finish(dut_last.id, "STATE");
        }
    }
    if (dut_owner.source == 1 && !dut_request_ready(&dut_owner) &&
        rdx_dut_test_keys_active()) {
        dut_finish(dut_last.id, "STATE");
        rdx_dut_test_cancel();
    }
    if (dut_queue_abort && dut_owner.source == 1 && !dut_emergency_cancel) {
        /* A saturated BLE producer cannot revoke a CDC-owned resource. */
        local_irq_disable();
        dut_head = dut_count = dut_queue_abort = dut_emergency_cancel = 0;
        local_irq_enable();
    }
    if (dut_emergency_cancel) {
        dut_finish(dut_last.id, "IO");
        /* A lost key event invalidates this capture even after start DONE. */
        if (rdx_dut_info.current_func == DUT_FUNC_KEY) dut_key_seen = 0;
        local_irq_disable();
        u8 abort_all = dut_queue_abort;
        dut_head = dut_count = 0;
        dut_queue_abort = dut_emergency_cancel = 0;
        local_irq_enable();
        /* Overflow can discard a legacy stop too. Use the existing resource
         * cleanup; an in-flight format has no cancellation API and must keep
         * its ownership until the format callback completes. Session revoke
         * also drains recording after its owner authorization is revoked. */
        if ((abort_all || rdx_dut_rec_is_running() || rdx_dut_rec_call_is_running()) &&
            rdx_dut_info.current_func != DUT_FUNC_FORMAT) {
            rdx_dut_close_current_func();
        } else {
            rdx_dut_test_cancel();
        }
    }
    if (dut_record_wait) {
        if (!dut_record_posted && !rdx_record_dut_stop_request(dut_record_ticket)) {
            dut_record_posted = 1;
        }
        int result = dut_record_posted ? rdx_record_dut_stop_poll(dut_record_ticket) : -2;
        if (result != -2) {
            if (dut_owner.source == 1) rdx_factory_audio_stopped(
                dut_last.command == DUT_CMD_REC && !dut_last.value ? dut_last.id : 0, result);
            if (result < 0) {
                dut_record_failed = 1;
                dut_finish(dut_last.id, "IO");
                DUT_LOG("Record save FAILED; exit/poweroff allowed, reboot required\n");
            }
            dut_record_wait = dut_record_posted = 0;
            if (rdx_dut_info.current_func == DUT_FUNC_REC ||
                rdx_dut_info.current_func == DUT_FUNC_REC_CALL) {
                rdx_dut_info.current_func = DUT_FUNC_NONE;
            }
        } else {
            dut_poll_action();
            return; /* Retain ownership until the worker acknowledges cleanup. */
        }
    }
    if (rdx_dut_test_keys_active() &&
        (!dut_business_ready() || !dut_request_ready(&dut_owner))) {
        dut_finish(dut_last.id, "STATE");
        rdx_dut_test_cancel();
    }
    int state = rdx_dut_speaker_state();
    if (state == DUT_AUDIO_DONE) {
        rdx_dut_speaker_reap();
        if (rdx_dut_info.current_func == DUT_FUNC_SPEAKER) {
            rdx_dut_info.current_func = DUT_FUNC_NONE;
        }
        if (dut_waiting == 1) dut_finish(dut_pending.id, "IO");
        if (dut_waiting == 2 && dut_business_ready() && dut_pending.source == 0) {
            dut_send(&dut_pending.token, FT_SPEAKER, "0");
        }
        dut_waiting = dut_stopping = 0;
    } else if (dut_waiting == 1 && state == DUT_AUDIO_PLAYING) {
        dut_waiting = 0;
        if (dut_pending.source == 0 && dut_send(&dut_pending.token, FT_SPEAKER, "1")) {
            dut_finish(dut_pending.id, "IO");
            rdx_dut_test_cancel();
        }
    } else if (dut_waiting == 1 &&
               (u32)(jiffies_to_msecs(jiffies) - dut_wait_started) > 2000) {
        dut_finish(dut_pending.id, "IO");
        DUT_LOG("Speaker start timeout\n");
        rdx_dut_test_cancel();
    }
    dut_poll_action();
    if (dut_waiting || dut_stopping) return;
    for (int i = 0; i < DUT_QUEUE_CAPACITY; ++i) {
        struct dut_request request;
        local_irq_disable();
        if (!dut_count) {
            local_irq_enable();
            break;
        }
        request = dut_queue[dut_head];
        dut_head = (dut_head + 1) % DUT_QUEUE_CAPACITY;
        --dut_count;
        local_irq_enable();
        /* Admission and LAST belong to app_core, never the BLE producer. */
        if (request.source == 0 &&
            !rdx_ble_session_rdx_token_resolve(&request.token, 1)) continue;
        dut_accept(&request);
        if (dut_waiting || dut_stopping || dut_record_wait) break;
    }
}
static void dut_record_stop(void)
{
    if (dut_record_wait) return;
    if (!++dut_record_ticket) ++dut_record_ticket;
    dut_record_wait = 1;
    dut_record_posted = !rdx_record_dut_stop_request(dut_record_ticket);
}
static bool dut_parse_test(const char *cmd, const char *value)
{
    if (!strcmp(cmd, FT_KEY_LAYOUT) || !strcmp(cmd, FT_KEY) ||
        !strcmp(cmd, FT_SPEAKER)) {
        int type;
        if (!strcmp(cmd, FT_KEY_LAYOUT) && (!value[0] || !strcmp(value, "1"))) {
            type = DUT_CMD_KEY_LAYOUT;
        } else if (!strcmp(cmd, FT_KEY) && !strcmp(value, "0")) {
            type = DUT_CMD_KEY_STOP;
        } else if (!strcmp(cmd, FT_SPEAKER) &&
                   (!strcmp(value, "0") || !strcmp(value, "1"))) {
            type = DUT_CMD_SPEAKER;
        } else {
            DUT_LOG("Invalid test value\n");
            return true;
        }
        if (dut_enqueue(type, value[0] == '1')) {
            DUT_LOG("Test command enqueue failed\n");
        }
        return true;
    }
    return false;
}

static const char *dut_command_name(u8 command)
{
    static const char *names[] = {
        "ft_dut", "ft_led_cycle", "ft_motor", "ft_rec", "ft_rec_call",
        "ft_wifi", "ft_format", "ft_poweroff", "ft_finalpack_end",
        "ft_key_dut_enable", "ft_key_dut_disabled", "ft_key_layout", "ft_key", "ft_speaker"
    };
    return command < ARRAY_SIZE(names) ? names[command] : "NONE";
}

static int dut_target_func(u8 command)
{
    switch (command) {
    case DUT_CMD_OLED: return DUT_FUNC_OLED;
    case DUT_CMD_MOTOR: return DUT_FUNC_MOTOR;
    case DUT_CMD_REC: return DUT_FUNC_REC;
    case DUT_CMD_REC_CALL: return DUT_FUNC_REC_CALL;
    case DUT_CMD_WIFI: return DUT_FUNC_WIFI;
    case DUT_CMD_FORMAT: return DUT_FUNC_FORMAT;
    case DUT_CMD_KEY_LAYOUT: case DUT_CMD_KEY_STOP: return DUT_FUNC_KEY;
    case DUT_CMD_SPEAKER: return DUT_FUNC_SPEAKER;
    default: return DUT_FUNC_NONE;
    }
}

static void dut_poll_action(void)
{
    if (dut_last_state != 1) return;
    if ((u32)(jiffies_to_msecs(jiffies) - dut_action_since) >= 60000) {
        dut_finish(dut_last.id, "IO");
        return; /* Keep resource fences; timeout is not cleanup. */
    }
    if (dut_last.command == DUT_CMD_FORMAT || dut_last.command == DUT_CMD_FINALPACK_END)
        return; /* Explicit callback completion, never infer success from idle. */
    if (dut_record_wait || dut_stopping || dut_waiting) return;
    if (dut_last.source == 1 && dut_last.command == DUT_CMD_REC && dut_last.value &&
        rdx_dut_info.current_func == DUT_FUNC_REC && !rdx_factory_audio_started()) return;
    if (dut_last.command == DUT_CMD_DUT_MODE) {
        dut_finish(dut_last.id, !!rdx_dut_info.dut_mode == !!dut_last.value ? NULL : "STATE");
    } else if (dut_last.command == DUT_CMD_KEY_DUT_ENABLE ||
               dut_last.command == DUT_CMD_KEY_DUT_DISABLE || dut_last.command == DUT_CMD_POWEROFF) {
        /* VM completion is explicit in the setters. */
        if (dut_last.command == DUT_CMD_POWEROFF) dut_finish(dut_last.id, NULL);
    } else {
        int target = dut_target_func(dut_last.command);
        int starting = dut_last.value || dut_last.command == DUT_CMD_KEY_LAYOUT;
        if (dut_last.command == DUT_CMD_KEY_STOP) starting = 0;
        dut_finish(dut_last.id, starting ?
            (rdx_dut_info.current_func == target ? NULL : "IO") :
            (rdx_dut_info.current_func == target ? "IO" : NULL));
    }
}

/* Called only by app_core. Validate before ID allocation, then reserve LAST
 * under the same interrupt exclusion as the CDC generation check. No device
 * calls, logging or waits occur in that short admission critical section. */
static const char *dut_accept(struct dut_request *request)
{
    int target = dut_target_func(request->command);
    if (request->source == 1 && request->command == DUT_CMD_REC && request->value &&
        !rdx_factory_audio_ready(request->generation)) return "STATE";
    /* A live recording can only be stopped/changed by its initiating source. */
    if ((rdx_dut_rec_is_running() || rdx_dut_rec_call_is_running()) &&
        (request->source != dut_owner.source ||
         (request->source == 1 && request->generation != dut_owner.generation))) return "BUSY";
    int active = rdx_dut_info.current_func;
    int exit = request->command == DUT_CMD_DUT_MODE && !request->value;
    int cleanup = exit || request->command == DUT_CMD_POWEROFF;
    RecordStatus *rp = rdx_record_get_status();
    if (!dut_service_timer || !get_power_on_status() || app_var.goto_poweroff_flag)
        return "STATE";
    if (g_finalpack_end_pending || dut_last_state == 1 || dut_emergency_cancel || dut_queue_abort || dut_waiting || dut_stopping ||
        dut_record_wait || rdx_uxfile_sd_format_status_check()) return "BUSY";
    if (dut_record_failed && !cleanup) return "STATE";
    if (rdx_storage_lifecycle_shutdown_deferred() ||
        (rdx_storage_lifecycle_business_blocked() && !cleanup)) return "BUSY";
    if (request->command != DUT_CMD_DUT_MODE && !rdx_dut_info.dut_mode &&
        !(dut_record_failed && request->command == DUT_CMD_POWEROFF)) return "STATE";
    if (request->command >= DUT_CMD_KEY_LAYOUT && !dut_request_ready(request)) return "STATE";
    /* Preserve the BLE APP's KEY -> next legacy test transition. */
    int release_key = request->source == 0 && active == DUT_FUNC_KEY && request->value == 1 &&
        request->command >= DUT_CMD_OLED && request->command <= DUT_CMD_FORMAT;
    if (!cleanup && active != DUT_FUNC_NONE && active != target && !release_key) return "BUSY";
    if (request->command == DUT_CMD_DUT_MODE && request->value && active != DUT_FUNC_NONE) return "BUSY";
    if (!cleanup && !(!request->value && active == target) &&
        (rp->process_state == REC_PROCESS_STATE_BUSY || get_ota_status() ||
        rdx_protocol_get_uploadfileInfo()->file_send_busy)) return "BUSY";
    if (!cleanup && rp->run != RECORD_STATE_STOP &&
        !(target == active && (active == DUT_FUNC_REC || active == DUT_FUNC_REC_CALL))) return "BUSY";
    if (request->command == DUT_CMD_KEY_LAYOUT) {
        char layout[24];
        if (dut_layout(layout, sizeof(layout)) <= 0) return "UNSUPPORTED";
    }
    if (dut_next_id == 0xffffffffu) return "STATE";
    local_irq_disable();
#if TCFG_T2620_FACTORY_USB_CDC_ENABLE
    if (request->source == 1 && (!request->generation ||
        request->generation != factory_cdc_generation())) {
        local_irq_enable();
        return "STATE";
    }
#endif
    request->id = ++dut_next_id;
    dut_last = *request;
    dut_last_state = 1;
    dut_last_error = "NONE";
    local_irq_enable();
    dut_action_since = jiffies_to_msecs(jiffies);
    if (release_key) rdx_dut_test_cancel();
    /* Repeated start doesn't transfer a running resource to another source. */
    if (active == DUT_FUNC_NONE || release_key || request->command == DUT_CMD_KEY_LAYOUT)
        dut_owner = *request;
    if (request->command >= DUT_CMD_KEY_LAYOUT) dut_execute_test(request);
    else if (target != DUT_FUNC_NONE && target == active && request->value) {
        /* Idempotent start: avoid resetting LED/audio/recording. */
    } else rdx_dut_cmd_async_handle(request->command, request->value);
    dut_poll_action();
    return NULL;
}

void rdx_dut_factory_record_stop(void)
{
    if (dut_owner.source != 1 || dut_record_wait || !rdx_factory_audio_owned()) return;
    struct dut_request request = dut_owner;
    request.command = DUT_CMD_REC;
    request.value = 0;
    if (!dut_accept(&request)) return;
    dut_finish(dut_last.id, "IO");
    dut_record_stop();
}

void rdx_dut_cdc_status(char *out, unsigned int capacity, int keys)
{
    if (keys) {
        char layout[24] = "NONE";
        if (dut_key_test_id) dut_layout(layout, sizeof(layout));
        snprintf(out, capacity, "OK TEST_ID=%u;LAYOUT=%s;SEEN=%c%c%c%c%c;ACTIVE=%u\r\n",
            dut_key_test_id, layout, '0'+!!(dut_key_seen&1), '0'+!!(dut_key_seen&2),
            '0'+!!(dut_key_seen&4), '0'+!!(dut_key_seen&8), '0'+!!(dut_key_seen&16),
            !!dut_key_gate && !dut_emergency_cancel && dut_request_ready(&dut_owner));
        return;
    }
    static const char *active[] = {"NONE", "OLED", "MOTOR", "REC", "REC_CALL", "WIFI", "FORMAT", "KEY", "SPEAKER"};
    static const char *states[] = {"NONE", "RUNNING", "DONE", "FAILED"};
    snprintf(out, capacity, "OK DUT=%u;ACTIVE=%s;LAST_ID=%u;LAST_CMD=%s;LAST_STATE=%s;LAST_ERR=%s\r\n",
        !!rdx_dut_info.dut_mode, g_finalpack_end_pending ? "FINALPACK" : active[rdx_dut_info.current_func],
        dut_last.id, dut_last.id ? dut_command_name(dut_last.command) : "NONE",
        states[dut_last_state], dut_last_error);
}

void rdx_dut_cdc_request(u32 generation, const char *name, u8 value, char *out, unsigned int capacity)
{
    struct dut_request request = {0};
    request.source = 1;
    request.generation = generation;
    request.value = value;
    if (!strcmp(name, "FT_DUT")) request.command = DUT_CMD_DUT_MODE;
    else if (!strcmp(name, "FT_LED_CYCLE") || !strcmp(name, "FT_OLED")) request.command = DUT_CMD_OLED;
    else if (!strcmp(name, "FT_REC") || !strcmp(name, "FT_REC_CHAT")) request.command = DUT_CMD_REC;
    else if (!strcmp(name, "FT_SPEAKER")) request.command = DUT_CMD_SPEAKER;
    else if (!strcmp(name, "FT_KEY_LAYOUT")) request.command = DUT_CMD_KEY_LAYOUT;
    else if (!strcmp(name, "FT_KEY")) request.command = DUT_CMD_KEY_STOP;
    else if (!strcmp(name, "FT_FINALPACK_END")) request.command = DUT_CMD_FINALPACK_END;
    else {
        const char *error = "CMD";
        static const char *unsupported[] = {"FT_MOTOR", "FT_VIBRATE",
            "FT_REC_CALL", "FT_WIFI", "FT_FORMAT", "FT_POWEROFF",
            "FT_KEY_DUT_ENABLE", "FT_KEY_DUT_DISABLED"};
        for (int i = 0; i < ARRAY_SIZE(unsupported); ++i)
            if (!strcmp(name, unsupported[i])) error = "UNSUPPORTED";
        snprintf(out, capacity, "ERR %s\r\n", error); return;
    }
    if (((request.command == DUT_CMD_KEY_LAYOUT || request.command == DUT_CMD_FINALPACK_END) && value != 1) ||
        (request.command == DUT_CMD_KEY_STOP && value != 0)) {
        snprintf(out, capacity, "ERR ARG\r\n"); return;
    }
    const char *error = dut_accept(&request);
    if (error) snprintf(out, capacity, "ERR %s\r\n", error);
    else snprintf(out, capacity, "OK ID=%u;STATE=%s\r\n", request.id,
        dut_last_state == 2 ? "DONE" : "RUNNING");
}

/* DUT_TEST_CORE_END */


/******************************************************************************
* Function Section - 基础接口
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_cmd_async_handle
 * description: DUT命令异步处理函数（在app_core任务中执行）
 **************************************************************************/
static void rdx_dut_cmd_async_handle(u8 cmd_type, u8 onoff)
{
    if (cmd_type != DUT_CMD_DUT_MODE &&
        !(dut_record_failed && cmd_type == DUT_CMD_POWEROFF) &&
        !dut_business_ready()) return;
    if (g_finalpack_end_pending) {
        DUT_LOG("Finalpack busy, command blocked\r");
        return;
    }
    DUT_LOG("Async handle: cmd_type=%d, onoff=%d\r", cmd_type, onoff);
    
    switch(cmd_type) {
        case DUT_CMD_DUT_MODE:
            if (!!onoff != !!rdx_dut_info.dut_mode) {
                dut_mode_apply();
            }
            break;
            
        case DUT_CMD_OLED:
            if(onoff == 1) {
                rdx_dut_oled_start();
            } else {
                rdx_dut_oled_stop();
            }
            break;
            
        case DUT_CMD_MOTOR:
            if(onoff == 1) {
                rdx_dut_motor_start();
            } else {
                rdx_dut_motor_stop();
            }
            break;
            
        case DUT_CMD_REC:
            if(onoff == 1) {
                rdx_dut_rec_start();
            } else {
                rdx_dut_rec_stop();
            }
            break;
            
        case DUT_CMD_REC_CALL:
            if(onoff == 1) {
                rdx_dut_rec_call_start();
            } else {
                rdx_dut_rec_call_stop();
            }
            break;
            
        case DUT_CMD_WIFI:
            if(onoff == 1) {
                rdx_dut_wifi_start();
            } else {
                rdx_dut_wifi_stop();
            }
            break;
            
        case DUT_CMD_FORMAT:
            if(onoff == 1) {
                rdx_dut_format_start();
            } else {
                rdx_dut_format_stop();
            }
            break;
            
        case DUT_CMD_POWEROFF:
            rdx_dut_poweroff();
            break;
            
        case DUT_CMD_FINALPACK_END:
            rdx_dut_finalpack_end();
            break;
            
        case DUT_CMD_KEY_DUT_ENABLE:
            rdx_dut_key_dut_enable();
            break;
            
        case DUT_CMD_KEY_DUT_DISABLE:
            rdx_dut_key_dut_disable();
            break;
            
        default:
            DUT_LOG("Unknown cmd_type: %d\r", cmd_type);
            break;
    }
}

/**************************************************************************
 * function: rdx_dut_init
 * description: DUT模块初始化，从VM读取掉电保存的配置
 **************************************************************************/
void rdx_dut_init(void)
{
    if (!dut_service_timer) {
        dut_service_timer = sys_timer_add(NULL, dut_service, dut_service_period);
    }
    factory_usb_revoked = 0;
    u8 vm_value = 0xFF;
    int ret = syscfg_read(VM_RDX_KEY_DUT_DISABLED, &vm_value, 1);
    
    DUT_LOG("Init: VM read ret=%d, vm_value=0x%02X\r", ret, vm_value);
    
    if(ret > 0 && vm_value == KEY_DUT_DISABLED_FLAG){
        rdx_dut_info.key_dut_disabled = true;
        DUT_LOG("Init: key_dut_disabled = 1 (disabled)\r");
    }else{
        rdx_dut_info.key_dut_disabled = false;
        DUT_LOG("Init: key_dut_disabled = 0 (enabled)\r");
    }
}

/**************************************************************************
 * function: rdx_dut_get_info
 **************************************************************************/
rdx_dut_info_t* rdx_dut_get_info(void)
{
    return &rdx_dut_info;
}

/**************************************************************************
 * function: rdx_dut_is_in_mode
 **************************************************************************/
bool rdx_dut_is_in_mode(void)
{
    return rdx_dut_info.dut_mode;
}

/**************************************************************************
 * function: rdx_dut_get_current_func_name
 **************************************************************************/
static const char* rdx_dut_get_current_func_name(void)
{
    switch(rdx_dut_info.current_func){
        case DUT_FUNC_OLED:     return "LED";
        case DUT_FUNC_MOTOR:    return "MOTOR";
        case DUT_FUNC_REC:      return "REC";
        case DUT_FUNC_REC_CALL: return "REC_CALL";
        case DUT_FUNC_WIFI:     return "WIFI";
        case DUT_FUNC_FORMAT:   return "FORMAT";
        case DUT_FUNC_KEY:      return "KEY";
        case DUT_FUNC_SPEAKER:  return "SPEAKER";
        default:                return "NONE";
    }
}

/**************************************************************************
 * function: rdx_dut_close_current_func
 **************************************************************************/
void rdx_dut_close_current_func(void)
{
    local_irq_disable();
    dut_head = dut_count = 0;
    local_irq_enable();
    switch(rdx_dut_info.current_func){
        case DUT_FUNC_KEY:
        case DUT_FUNC_SPEAKER:
            rdx_dut_test_cancel();
            return; /* SPEAKER retains ownership until worker close completes. */
        case DUT_FUNC_OLED:
            rdx_dut_oled_stop();
            break;
        case DUT_FUNC_MOTOR:
            rdx_dut_motor_stop();
            break;
        case DUT_FUNC_REC:
            rdx_dut_rec_stop();
            return;
        case DUT_FUNC_REC_CALL:
            rdx_dut_rec_call_stop();
            return;
        case DUT_FUNC_WIFI:
            rdx_dut_wifi_stop();
            break;
        case DUT_FUNC_FORMAT:
            break;
        default:
            break;
    }
    rdx_dut_info.current_func = DUT_FUNC_NONE;
}

/**************************************************************************
 * function: rdx_dut_show
 * description: DUT模式显示（PIN: LED灯效）
 **************************************************************************/
static void rdx_dut_show(void)
{
    DUT_LOG("in DUT mode now!\r");
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_ENTER);
}

/******************************************************************************
* Function Section - 1. LED灯光测试（兼容BJ的OLED测试命名）
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_oled_start
 * description: PIN: 开始LED灯光测试（常亮白色测试灯效）
 **************************************************************************/
void rdx_dut_oled_start(void)
{
    DUT_LOG("LED test START\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    rdx_dut_info.current_func = DUT_FUNC_OLED;
    
    /* Factory LED test: green solid until the explicit stop command. */
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_LED_TEST);
}

/**************************************************************************
 * function: rdx_dut_oled_stop
 **************************************************************************/
void rdx_dut_oled_stop(void)
{
    DUT_LOG("LED test STOP\r");
    
    if(rdx_dut_info.current_func == DUT_FUNC_OLED){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    
    rdx_dut_show();
}

/**************************************************************************
 * function: rdx_dut_oled_is_running
 **************************************************************************/
bool rdx_dut_oled_is_running(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_OLED);
}

/******************************************************************************
* Function Section - 2. 马达震动测试
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_motor_timer_cb
 * description: 马达震动定时器回调（震动2秒，停1秒，循环）
 **************************************************************************/
static void rdx_dut_motor_timer_cb(void *priv)
{
#if (RDX_SUPPORT_MOTOR == 1)
    if(rdx_dut_info.current_func != DUT_FUNC_MOTOR){
        return;
    }

    if(rdx_dut_info.motor_run == TRUE){
        motor_off();
        rdx_dut_info.motor_run = FALSE;
        sys_timer_modify(rdx_dut_info.motor_timer, 1000);
    }else{
        motor_on();
        rdx_dut_info.motor_run = TRUE;
        sys_timer_modify(rdx_dut_info.motor_timer, 2000);
    }
#endif
}

/**************************************************************************
 * function: rdx_dut_motor_start
 **************************************************************************/
void rdx_dut_motor_start(void)
{
#if (RDX_SUPPORT_MOTOR == 1)
    DUT_LOG("Motor test START\r");

    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }

    rdx_dut_info.current_func = DUT_FUNC_MOTOR;

    if(rdx_dut_info.motor_timer == 0){
        rdx_dut_info.motor_timer = sys_timer_add(NULL, rdx_dut_motor_timer_cb, 2000);
        motor_on();
        rdx_dut_info.motor_run = TRUE;
    }
#endif
}

/**************************************************************************
 * function: rdx_dut_motor_stop
 **************************************************************************/
void rdx_dut_motor_stop(void)
{
#if (RDX_SUPPORT_MOTOR == 1)
    DUT_LOG("Motor test STOP\r");

    if(rdx_dut_info.motor_timer){
        sys_timer_del(rdx_dut_info.motor_timer);
        rdx_dut_info.motor_timer = 0;
    }

    motor_off();
    rdx_dut_info.motor_run = FALSE;

    if(rdx_dut_info.current_func == DUT_FUNC_MOTOR){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }

    rdx_dut_show();
#endif
}

/**************************************************************************
 * function: rdx_dut_motor_is_running
 **************************************************************************/
bool rdx_dut_motor_is_running(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_MOTOR);
}

/******************************************************************************
* Function Section - 3. 录音测试
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_rec_start
 **************************************************************************/
/* Called on app_core when recording is revoked or audio startup fails. */
void rdx_dut_record_reset(void)
{
    rdx_record_dut_authorization_revoke();
    if (dut_owner.source == 1 && rdx_factory_audio_owned()) {
        dut_record_stop();
        return;
    }
    if (dut_record_wait) return;
    if (rdx_dut_info.current_func == DUT_FUNC_REC ||
        rdx_dut_info.current_func == DUT_FUNC_REC_CALL) {
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
}

void rdx_dut_rec_start(void)
{
    DUT_LOG("Record test START\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    if (!dut_business_ready() ||
        !(dut_owner.source == 1 ? rdx_record_dut_authorize_usb(dut_owner.generation) : rdx_record_dut_authorize())) {
        DUT_LOG("Record test rejected: session/audio/storage not ready\n");
        return;
    }
    if (dut_owner.source == 1 && rdx_factory_audio_begin(dut_owner.generation, dut_owner.id)) {
        rdx_record_dut_authorization_revoke();
        return;
    }

    rdx_dut_info.current_func = DUT_FUNC_REC;
    
    RecordStatus* rp = rdx_record_get_status();
    DUT_LOG("RecordStatus: run = %d, scene = %d\r", rp->run, rp->scene);
    if(rp->run == RECORD_STATE_STOP){
        /* 与APP下发record指令保持一致: 绑定当前RDX会话为在线录音会话,
         * 走在线流+本地落盘+状态上报; 无RDX连接时维持原离线落盘行为 */
        if(dut_owner.source != 1 && rdx_record_online_session_bind_current()){
            DUT_LOG("Chat record online session bound (stream + local save)\r");
        }else{
            DUT_LOG("No live RDX link, chat record falls back to offline\r");
        }
        rp->run = RECORD_STATE_START;
        rp->scene = RECORD_SCENE_CHAT;
        rdx_record_prepare_new_session();
        rdx_record_process();
    }
}

/**************************************************************************
 * function: rdx_dut_rec_stop
 **************************************************************************/
void rdx_dut_rec_stop(void)
{
    if (rdx_dut_info.current_func != DUT_FUNC_REC) return;
    dut_record_stop();
}

/**************************************************************************
 * function: rdx_dut_rec_is_running
 **************************************************************************/
bool rdx_dut_rec_is_running(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_REC);
}

/******************************************************************************
* Function Section - 3b. Call recording test
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_rec_call_start
 **************************************************************************/
void rdx_dut_rec_call_start(void)
{
    DUT_LOG("Record CALL test START\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    if (!dut_business_ready() || !rdx_record_dut_authorize()) {
        DUT_LOG("Record test rejected: session/audio/storage not ready\n");
        return;
    }

    rdx_dut_info.current_func = DUT_FUNC_REC_CALL;
    
    RecordStatus* rp = rdx_record_get_status();
    DUT_LOG("RecordStatus: run = %d, scene = %d\r", rp->run, rp->scene);
    if(rp->run == RECORD_STATE_STOP){
        /* 与APP下发record指令保持一致: 绑定当前RDX会话为在线录音会话,
         * 走在线流+本地落盘+状态上报; 无RDX连接时维持原离线落盘行为 */
        if(rdx_record_online_session_bind_current()){
            DUT_LOG("Call record online session bound (stream + local save)\r");
        }else{
            DUT_LOG("No live RDX link, call record falls back to offline\r");
        }
        rp->run = RECORD_STATE_START;
        rp->scene = RECORD_SCENE_CALL;
        rdx_record_prepare_new_session();
        rdx_record_process();
    }
}

/**************************************************************************
 * function: rdx_dut_rec_call_stop
 **************************************************************************/
void rdx_dut_rec_call_stop(void)
{
    if (rdx_dut_info.current_func != DUT_FUNC_REC_CALL) return;
    dut_record_stop();
}

/**************************************************************************
 * function: rdx_dut_rec_call_is_running
 **************************************************************************/
bool rdx_dut_rec_call_is_running(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_REC_CALL);
}

/******************************************************************************
* Function Section - 4. WiFi测试
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_wifi_start
 **************************************************************************/
void rdx_dut_wifi_start(void)
{
    DUT_LOG("WiFi test START\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    rdx_dut_info.current_func = DUT_FUNC_WIFI;
    
    rdx_app_wifi_handle(TRANSFER_BY_WIFI_ON);
    DUT_LOG("WiFi module power on\r");
}

/**************************************************************************
 * function: rdx_dut_wifi_stop
 **************************************************************************/
void rdx_dut_wifi_stop(void)
{
    DUT_LOG("WiFi test STOP\r");
    
    rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
    DUT_LOG("WiFi module power off\r");
    
    if(rdx_dut_info.current_func == DUT_FUNC_WIFI){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    
    rdx_dut_show();
}

/**************************************************************************
 * function: rdx_dut_wifi_is_running
 **************************************************************************/
bool rdx_dut_wifi_is_running(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_WIFI);
}

/******************************************************************************
* Function Section - 5. 格式化存储
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_format_stop
 **************************************************************************/
static void rdx_dut_format_stop(void)
{
    if(rdx_dut_info.current_func == DUT_FUNC_FORMAT){
        rdx_dut_info.current_func = DUT_FUNC_NONE;
    }
    
    rdx_dut_show();
    DUT_LOG("Format stopped, DUT display restored\r");
}

/**************************************************************************
 * function: rdx_dut_format_cb
 **************************************************************************/
static void rdx_dut_format_cb(u8 result)
{
    dut_completion_result = result;
    dut_completion_id = dut_format_id;
}

/**************************************************************************
 * function: rdx_dut_format_start
 **************************************************************************/
void rdx_dut_format_start(void)
{
    DUT_LOG("=== Format storage START ===\r");
    
    if(rdx_dut_info.current_func != DUT_FUNC_NONE){
        DUT_LOG("Blocked! Current test: [%s]\r", rdx_dut_get_current_func_name());
        return;
    }
    
    rdx_dut_info.current_func = DUT_FUNC_FORMAT;
    
    dut_format_id = dut_last.id;
    rdx_uxfile_device_sd_format(rdx_dut_format_cb);
}

/******************************************************************************
* Function Section - 6. 关机（船运模式）
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_poweroff
 **************************************************************************/
void rdx_dut_poweroff(void)
{
    DUT_LOG("Power off (ship mode)\r");
    
    rdx_dut_factory_usb_revoke();
    rdx_dut_close_current_func();
    rdx_dut_info.dut_mode = FALSE;
    
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_EXIT);

    sys_timeout_add(NULL, (void (*)(void *))rdx_app_normal_poweroff, 200);
}

/******************************************************************************
* Function Section - 7. 包装测试 / 按键进DUT禁用
******************************************************************************/ 

static void rdx_dut_finalpack_fail(void)
{
    g_finalpack_end_pending = false;
    g_finalpack_format_waiting = false;
    dut_finish(dut_format_id, "IO");
    DUT_LOG("Finalpack FAILED; remain in DUT, no automatic poweroff\r");
}

static int rdx_dut_key_dut_store(bool disabled)
{
    u8 value = disabled ? KEY_DUT_DISABLED_FLAG : 0xff;
    u8 verified = 0;
    if (syscfg_write(VM_RDX_KEY_DUT_DISABLED, &value, 1) != 1 ||
        syscfg_read(VM_RDX_KEY_DUT_DISABLED, &verified, 1) != 1 ||
        verified != value) {
        DUT_LOG("Key DUT VM write/readback failed\r");
        return -1;
    }
    rdx_dut_info.key_dut_disabled = disabled;
    return 0;
}

/* The vendor format callback may run outside app_core. Commit only here. */
static void rdx_dut_finalpack_commit(int result)
{
    if (!g_finalpack_end_pending || g_finalpack_format_waiting) {
        return;
    }
    if (result != MEM_FORMAT_RESULT_OK) {
        DUT_LOG("Finalpack format failed: %d\r", result);
        rdx_dut_finalpack_fail();
        return;
    }
    if (rdx_vm_reset_defaults_no_poweroff() ||
        rdx_vm_set_bound_status(0, 0) || rdx_dut_key_dut_store(true)) {
        rdx_dut_finalpack_fail();
        return;
    }
    dut_finish(dut_format_id, NULL);
    DUT_LOG("Finalpack committed: bound=0, key DUT disabled (verified)\r");
    rdx_ble_server_auto_shut_down_enable(0);
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_FINALPACK_DONE);
    DUT_LOG("Finalpack complete: solid red LED; turn DIP switch OFF to power off\r");
}

/**************************************************************************
 * function: rdx_dut_finalpack_end_format_cb
 **************************************************************************/
void rdx_dut_finalpack_end_format_cb(u8 result)
{
    DUT_LOG("Finalpack end format cb, result: %d\r", result);
    
    if(!g_finalpack_end_pending || !g_finalpack_format_waiting){
        return;
    }
    g_finalpack_format_waiting = false;
    dut_completion_result = result;
    dut_completion_id = dut_format_id;
}

/**************************************************************************
 * function: rdx_dut_finalpack_end
 * description: 包装测试结束
 *              流程：格式化成功 → 重置参数/解绑 → 禁止按键进DUT → 红灯常亮，等待拨码关机
 **************************************************************************/
void rdx_dut_finalpack_end(void)
{
    DUT_LOG("=== Finalpack End ===\r");
    
    RecordStatus *rp = rdx_record_get_status();
    ReqFileInfo *rf = rdx_protocol_get_uploadfileInfo();
    if (g_finalpack_end_pending || !rdx_dut_info.dut_mode ||
        rdx_dut_info.current_func != DUT_FUNC_NONE || get_ota_status() ||
        rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME ||
        rf->file_send_busy ||
        rdx_uxfile_sd_format_status_check() || app_var.goto_poweroff_flag ||
        rdx_storage_lifecycle_business_blocked() ||
        rdx_storage_lifecycle_shutdown_deferred()) {
        DUT_LOG("Finalpack rejected: busy or not in DUT\r");
        return;
    }
    g_finalpack_end_pending = true;
    g_finalpack_format_waiting = true;
    rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_ENTER);
    dut_format_id = dut_last.id;
    rdx_uxfile_device_sd_format(rdx_dut_finalpack_end_format_cb);
}

/**************************************************************************
 * function: rdx_dut_key_dut_enable
 **************************************************************************/
void rdx_dut_key_dut_enable(void)
{
    DUT_LOG("Key DUT ENABLED\r");
    
    if (rdx_dut_key_dut_store(false)) {
        dut_finish(dut_last.id, "IO");
        return;
    }
    dut_finish(dut_last.id, NULL);
    DUT_LOG("Saved to VM: key_dut_disabled = 0 (cleared flag)\r");
}

/**************************************************************************
 * function: rdx_dut_key_dut_disable
 **************************************************************************/
void rdx_dut_key_dut_disable(void)
{
    DUT_LOG("Key DUT DISABLED\r");
    
    if (rdx_dut_key_dut_store(true)) {
        dut_finish(dut_last.id, "IO");
        return;
    }
    dut_finish(dut_last.id, NULL);
    DUT_LOG("Saved to VM: key_dut_disabled = 1 (set flag)\r");
}

/**************************************************************************
 * function: rdx_dut_is_key_dut_disabled
 **************************************************************************/
bool rdx_dut_is_key_dut_disabled(void)
{
    return rdx_dut_info.key_dut_disabled;
}

/******************************************************************************
* Function Section - BLE指令处理
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_ble_cmd_handle
 * description: DUT BLE指令处理入口
 *              命令异步发送到app_core任务执行
 **************************************************************************/
/* Acknowledge the factory APP's packaging mode handshake without reopening
 * tests or replacing the completion indication. DIP OFF ends this session. */
static bool rdx_dut_finalpack_mode_ack(const char *cmd, const char *value)
{
    if (g_finalpack_end_pending && !strcmp(cmd, FT_DUT) &&
        (!strcmp(value, "0") || !strcmp(value, "1"))) {
        rdx_protocol_custom_msg_indicate(FT_DUT, (char *)value);
        return true;
    }
    return false;
}

void rdx_dut_ble_cmd_handle(const char* cmd, const char* value)
{
    if (!cmd || !value) return;
    if (dut_parse_test(cmd, value)) return;
    if (g_finalpack_end_pending) {
        if (rdx_dut_finalpack_mode_ack(cmd, value)) {
            return;
        }
        DUT_LOG("Finalpack busy, BLE command blocked\r");
        return;
    }
    char* p = NULL;
    int ret;
    
    p = strstr(cmd, FT_DUT);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("DUT cmd, onoff: %d, current_mode: %d\r", onoff, rdx_dut_info.dut_mode);
        
        if(onoff == 0 || onoff == 1){
            ret = dut_enqueue(DUT_CMD_DUT_MODE, onoff);
            if(ret) {
                DUT_LOG("DUT taskq post err: %d\r", ret);
            }
        }else{
            DUT_LOG("DUT state already %s, skip\r", onoff ? "ON" : "OFF");
        }
        rdx_protocol_custom_msg_indicate(FT_DUT, (char*)value);
        return;
    }
    
    if(!rdx_dut_info.dut_mode){
        DUT_LOG("Not in DUT mode, ignore cmd: %s\r", cmd);
        return;
    }
    
    p = strstr(cmd, FT_LED_CYCLE);
    if(!p) p = strstr(cmd, FT_OLED);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("LED cmd, onoff: %d\r", onoff);
        
        ret = dut_enqueue(DUT_CMD_OLED, onoff);
        if(ret) {
            DUT_LOG("LED taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_LED_CYCLE, (char*)value);
        return;
    }
    
    p = strstr(cmd, FT_VIBRATE);
    if(!p) p = strstr(cmd, FT_MOTOR);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("Motor cmd, onoff: %d\r", onoff);
        
        ret = dut_enqueue(DUT_CMD_MOTOR, onoff);
        if(ret) {
            DUT_LOG("Motor taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_VIBRATE, (char*)value);
        return;
    }
    
    p = strstr(cmd, FT_REC_CALL);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("Record CALL cmd, onoff: %d\r", onoff);
        
        ret = dut_enqueue(DUT_CMD_REC_CALL, onoff);
        if(ret) {
            DUT_LOG("Record CALL taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_REC_CALL, (char*)value);
        return;
    }
    
    p = strstr(cmd, FT_REC_CHAT);
    if(!p) p = strstr(cmd, FT_REC);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("Record CHAT cmd, onoff: %d\r", onoff);
        
        ret = dut_enqueue(DUT_CMD_REC, onoff);
        if(ret) {
            DUT_LOG("Record CHAT taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_REC_CHAT, (char*)value);
        return;
    }
    
    p = strstr(cmd, FT_WIFI);
    if(p){
        u8 onoff = atoi(value);
        DUT_LOG("WiFi cmd, onoff: %d\r", onoff);
        
        ret = dut_enqueue(DUT_CMD_WIFI, onoff);
        if(ret) {
            DUT_LOG("WiFi taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_WIFI, (char*)value);
        return;
    }
    
    p = strstr(cmd, FT_FORMAT);
    if(p){
        DUT_LOG("Format cmd\r");
        
        ret = dut_enqueue(DUT_CMD_FORMAT, 1);
        if(ret) {
            DUT_LOG("Format taskq post err: %d\r", ret);
        }
        rdx_protocol_custom_msg_indicate(FT_FORMAT, "0");
        return;
    }
    
    p = strstr(cmd, FT_POWEROFF);
    if(p){
        DUT_LOG("Poweroff cmd\r");
        rdx_protocol_custom_msg_indicate(FT_POWEROFF, "0");
        
        ret = dut_enqueue(DUT_CMD_POWEROFF, 0);
        if(ret) {
            DUT_LOG("Poweroff taskq post err: %d\r", ret);
        }
        return;
    }
    
    p = strstr(cmd, FT_FINALPACK_END);
    if(p){
        DUT_LOG("Finalpack end cmd\r");
        rdx_protocol_custom_msg_indicate(FT_FINALPACK_END, "0");
        
        ret = dut_enqueue(DUT_CMD_FINALPACK_END, 0);
        if(ret) {
            DUT_LOG("Finalpack end taskq post err: %d\r", ret);
        }
        return;
    }
    
    p = strstr(cmd, FT_KEY_DUT_DISABLED);
    if(p){
        DUT_LOG("Key DUT disable cmd\r");
        rdx_protocol_custom_msg_indicate(FT_KEY_DUT_DISABLED, "0");
        
        ret = dut_enqueue(DUT_CMD_KEY_DUT_DISABLE, 0);
        if(ret) {
            DUT_LOG("Key DUT disable taskq post err: %d\r", ret);
        }
        return;
    }
    
    p = strstr(cmd, FT_KEY_DUT_ENABLE);
    if(p){
        DUT_LOG("Key DUT enable cmd\r");
        rdx_protocol_custom_msg_indicate(FT_KEY_DUT_ENABLE, "0");
        
        ret = dut_enqueue(DUT_CMD_KEY_DUT_ENABLE, 0);
        if(ret) {
            DUT_LOG("Key DUT enable taskq post err: %d\r", ret);
        }
        return;
    }
}

/******************************************************************************
* Function Section - 按键处理
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_key_handle
 * description: DUT按键事件处理
 * 单击 - LED灯光测试
 * 双击 - 马达震动测试
 * 三击 - 录音测试
 * 四击 - WiFi测试
 * 五击 - 格式化存储 (PIN: APP_MSG_BT_PAIR_SET_DEFAULT)
 * 六击 - 关机 (PIN: APP_MSG_SEXTUPLE_CLICK)
 **************************************************************************/
void rdx_dut_key_handle(int key_msg)
{
    if (rdx_dut_test_keys_active() || !rdx_dut_info.dut_mode) return;
    struct dut_request request = {0};
    request.source = 2;
    switch (key_msg) {
    case APP_MSG_SINGLE_CLICK: request.command = DUT_CMD_OLED; request.value = !rdx_dut_oled_is_running(); break;
    case APP_MSG_DOUBLE_CLICK: request.command = DUT_CMD_MOTOR; request.value = !rdx_dut_motor_is_running(); break;
    case APP_MSG_TRIPLE_CLICK: request.command = DUT_CMD_REC; request.value = !rdx_dut_rec_is_running(); break;
    case APP_MSG_QUADRUPLE_CLICK: request.command = DUT_CMD_WIFI; request.value = !rdx_dut_wifi_is_running(); break;
    case APP_MSG_BT_PAIR_SET_DEFAULT: request.command = DUT_CMD_FORMAT; request.value = 1; break;
    case APP_MSG_SEXTUPLE_CLICK: request.command = DUT_CMD_POWEROFF; break;
    default: return;
    }
    dut_accept(&request);
}

void rdx_dut_msg_handle(void)
{
    struct dut_request request = {0};
    request.source = 2;
    request.command = DUT_CMD_DUT_MODE;
    request.value = !rdx_dut_info.dut_mode;
    dut_accept(&request);
}

/******************************************************************************
* Function Section - DUT消息处理（APP_MSG_DUT）
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_msg_handle
 * description: APP_MSG_DUT消息处理入口（进入/退出DUT模式）
 **************************************************************************/
static void dut_mode_apply(void)
{
    RecordStatus* rp = rdx_record_get_status();
    
    if(rdx_dut_info.dut_mode == FALSE){
        if (dut_record_failed) return; /* Reboot required before another test. */
        /*--- 进入DUT模式 ---*/
        DUT_LOG("=== Enter DUT mode request ===\r");
        
        if(rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) {
            DUT_LOG("Recording in progress, blocked!\r");
            return;
        }
        
        if(get_ota_status()) {
            DUT_LOG("OTA in progress, blocked!\r");
            return;
        }
        
        DUT_LOG("【 Enter DUT mode! 】\r");
        
        RdxWifiInfo* pw = rdx_app_get_wifi_info();
        if(pw->onoff == TRANSFER_BY_WIFI_ON) {
            DUT_LOG("Closing WiFi transfer...\r");
            rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF);
        }
        
        if (dut_stopping || dut_record_wait) return;
        rdx_dut_info.dut_mode = TRUE;
        rdx_dut_info.current_func = DUT_FUNC_NONE;
        
        rdx_app_emmc_poweron(0);
        rdx_app_emmc_poweroff_check_timer_stop();
        
        rdx_app_bt_open();
        bt_bredr_enter_dut_mode(0, 0);
        
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_ENTER);
        
        /* Keep BLE advertising/connection alive for the factory APP while classic BT SPP is initialized below. */
        rdx_ble_server_auto_shut_down_enable(0);
        DUT_LOG("Auto shutdown disabled!\r");
        
        gpio_set_mode(IO_PORT_SPILT(VDD_POWER_PORT_IO), PORT_OUTPUT_HIGH);
        
        rdx_spp_init();
        
    }else{
        /*--- 退出DUT模式 ---*/
        DUT_LOG("【 Exit DUT mode! 】\r");

        if(g_finalpack_end_pending == true){
            DUT_LOG("Finalpack end pending, skip exit\r");
            return;
        }

        rdx_record_dut_authorization_revoke();
        rdx_dut_info.dut_mode = FALSE;
        
        rdx_dut_close_current_func();
        
        bt_bredr_exit_dut_mode();
        rdx_spp_exit();
        rdx_app_bt_shutdown();
        
        /* Refresh the unified advertising data after DUT. */
        rdx_ble_server_adv_data_changed();
        
        rdx_led_ctrl_set_scene(RDX_LED_SCENE_BLE_ADV_START);
        
        rdx_ble_server_auto_shut_down_enable(1);
        DUT_LOG("Auto shutdown enabled!\r");
        
        u8 s = rdx_app_get_charge_state();
        if(s == RDX_CHARGE_IN){
            /* PIN: 充电中恢复充电灯效，由rdx_charge模块自动管理 */
        }
    }
    
#if (RDX_SUPPORT_MOTOR == 1)    
    rdx_app_motor_run_once();
#endif
}

/******************************************************************************
* Function Section - DUT显示刷新
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_show_refresh
 * description: DUT模式下的显示刷新（PIN: 仅打印日志，LED由各功能自行管理）
 **************************************************************************/
void rdx_dut_show_refresh(void)
{
    DUT_LOG("DUT show refresh, current_func=%d\r", rdx_dut_info.current_func);
    
    switch(rdx_dut_info.current_func){
        case DUT_FUNC_KEY:
        case DUT_FUNC_SPEAKER:
        case DUT_FUNC_MOTOR:
        case DUT_FUNC_OLED:
        case DUT_FUNC_REC:
        case DUT_FUNC_REC_CALL:
        case DUT_FUNC_WIFI:
            break;
        default:
            rdx_led_ctrl_set_scene(RDX_LED_SCENE_DUT_ENTER);
            break;
    }
}

/******************************************************************************
* Function Section - DUT state query
******************************************************************************/ 

/**************************************************************************
 * function: rdx_dut_is_formatting
 **************************************************************************/
bool rdx_dut_is_formatting(void)
{
    return (rdx_dut_info.current_func == DUT_FUNC_FORMAT);
}
