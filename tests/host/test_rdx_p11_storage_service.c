#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#include "system/includes.h"
#include "rdx_err.h"
#include "rdx_uxfile.h"

#define __RDX_STORAGE_SERVICE_H__
#define __RDX_EVENT_BUS_H__
#define __RDX_COMMAND_DISPATCH_H__
#define __RDX_RECORD_SERVICE_H__
#define __RDX_WIFI_SERVICE_H__
#define __RDX_FILE_TRANSFER_CLEANUP_COMPAT_H__
#define __RDX_STORAGE_DOMAIN_H__

typedef enum {
    PROTOCOL_EVENT_CMD_SD_FORMAT = 1,
    PROTOCOL_EVENT_CMD_SD_MEM_QUERY,
    PROTOCOL_EVENT_CMD_FILE_DELETE,
} ProtocolEvents;

typedef struct {
    int file_sn;
    char file_name[64];
} ProtocolFileDeleteParams;

typedef struct {
    void (*sd_mem_indicate)(u32 left, u32 total);
    void (*file_delete_ack_indicate)(u8 result, int file_sn, char *file_name);
    void (*sd_format_ack_indicate)(u8 result);
} RdxProtocolIndicateOps;

typedef void (*rdx_cmd_handler_t)(ProtocolEvents event, void *data, u32 len);

typedef enum {
    RDX_EVENT_STORAGE_FORMAT_DONE = 0x20,
} rdx_event_id_t;

typedef void (*rdx_event_callback_t)(rdx_event_id_t event, void *payload,
                                     u32 len, void *user_ctx);

const RdxProtocolIndicateOps *rdx_protocol_get_indicate_ops(void);
void rdx_cmd_register(ProtocolEvents event, rdx_cmd_handler_t handler);
bool rdx_record_service_is_running(void);
int rdx_wifi_service_is_file_send_busy(void);
int rdx_event_subscribe(rdx_event_id_t event, rdx_event_callback_t callback,
                        void *user_ctx);
void rdx_event_publish(rdx_event_id_t event, void *payload, u32 len);
rdx_err_t rdx_file_transfer_compat_cleanup_record_disconnect(void);
rdx_err_t rdx_file_transfer_compat_cleanup_ble_delayed(void);
rdx_err_t rdx_storage_domain_adjust_active_record_time(int delta_seconds);
rdx_err_t rdx_storage_service_format_for_app(void);

#include "../../SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c"

enum trace_event {
    TRACE_RUNTIME_INIT,
    TRACE_OTA_QUERY,
    TRACE_RECORD_QUERY,
    TRACE_WIFI_QUERY,
    TRACE_ACK_OK,
    TRACE_ACK_BUSY,
    TRACE_FORMAT_REQUEST,
    TRACE_FORMAT_DONE_EVENT,
};

static enum trace_event g_trace[16];
static unsigned g_trace_count;
static const RdxProtocolIndicateOps *g_ops;
static u8 g_ota_busy;
static bool g_record_busy;
static int g_wifi_busy;
static int g_format_result;
static uxfile_format_cb g_format_callback;

static void trace(enum trace_event event)
{
    assert(g_trace_count < (sizeof(g_trace) / sizeof(g_trace[0])));
    g_trace[g_trace_count++] = event;
}

static void format_ack(u8 result)
{
    trace(result == 0 ? TRACE_ACK_OK : TRACE_ACK_BUSY);
}

static const RdxProtocolIndicateOps g_indicate_ops = {
    .sd_format_ack_indicate = format_ack,
};

static void reset_test(void)
{
    g_trace_count = 0;
    g_ops = &g_indicate_ops;
    g_ota_busy = 0;
    g_record_busy = false;
    g_wifi_busy = 0;
    g_format_result = 0;
    g_format_callback = NULL;
}

static void assert_trace(const enum trace_event *expected, unsigned count)
{
    unsigned i;

    assert(g_trace_count == count);
    for (i = 0; i < count; i++) {
        assert(g_trace[i] == expected[i]);
    }
}

const RdxProtocolIndicateOps *rdx_protocol_get_indicate_ops(void)
{
    return g_ops;
}

void rdx_uxfile_init(void)
{
    trace(TRACE_RUNTIME_INIT);
}

u8 get_ota_status(void)
{
    trace(TRACE_OTA_QUERY);
    return g_ota_busy;
}

bool rdx_record_service_is_running(void)
{
    trace(TRACE_RECORD_QUERY);
    return g_record_busy;
}

int rdx_wifi_service_is_file_send_busy(void)
{
    trace(TRACE_WIFI_QUERY);
    return g_wifi_busy;
}

int rdx_uxfile_sd_format(uxfile_format_cb callback)
{
    trace(TRACE_FORMAT_REQUEST);
    g_format_callback = callback;
    return g_format_result;
}

void rdx_event_publish(rdx_event_id_t event, void *payload, u32 len)
{
    assert(event == RDX_EVENT_STORAGE_FORMAT_DONE);
    assert(payload == NULL);
    assert(len == 0);
    trace(TRACE_FORMAT_DONE_EVENT);
}

int p11_test_y_printf(const char *format, ...)
{
    (void)format;
    return 0;
}

int p11_test_g_printf(const char *format, ...)
{
    (void)format;
    return 0;
}

void rdx_cmd_register(ProtocolEvents event, rdx_cmd_handler_t handler)
{
    (void)event;
    (void)handler;
}

int rdx_event_subscribe(rdx_event_id_t event, rdx_event_callback_t callback,
                        void *user_ctx)
{
    (void)event;
    (void)callback;
    (void)user_ctx;
    return RDX_OK;
}

void rdx_uxfile_device_sd_mem_check(void)
{
}

int rdx_uxfile_recordFile_delete_handle(int file_num, char *file_name)
{
    (void)file_num;
    (void)file_name;
    return 0;
}

bool rdx_uxfile_sd_format_status_check(void)
{
    return false;
}

rdx_err_t rdx_file_transfer_compat_cleanup_record_disconnect(void)
{
    return RDX_OK;
}

rdx_err_t rdx_file_transfer_compat_cleanup_ble_delayed(void)
{
    return RDX_OK;
}

rdx_err_t rdx_storage_domain_adjust_active_record_time(int delta_seconds)
{
    (void)delta_seconds;
    return RDX_OK;
}

void sd_set_power(u8 enable)
{
    (void)enable;
}

static void test_ops_null(void)
{
    reset_test();
    g_ops = NULL;

    rdx_cmd_handle_sd_format(PROTOCOL_EVENT_CMD_SD_FORMAT, NULL, 0);
    assert(g_trace_count == 0);
}

static void test_runtime_init(void)
{
    static const enum trace_event expected[] = {
        TRACE_RUNTIME_INIT,
    };

    reset_test();
    assert(rdx_storage_service_runtime_init() == RDX_OK);
    assert_trace(expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_busy_gates(void)
{
    static const enum trace_event ota_busy[] = {
        TRACE_OTA_QUERY, TRACE_ACK_BUSY,
    };
    static const enum trace_event record_busy[] = {
        TRACE_OTA_QUERY, TRACE_RECORD_QUERY, TRACE_ACK_BUSY,
    };
    static const enum trace_event wifi_busy[] = {
        TRACE_OTA_QUERY, TRACE_RECORD_QUERY, TRACE_WIFI_QUERY, TRACE_ACK_BUSY,
    };

    reset_test();
    g_ota_busy = 1;
    rdx_cmd_handle_sd_format(PROTOCOL_EVENT_CMD_SD_FORMAT, NULL, 0);
    assert_trace(ota_busy, sizeof(ota_busy) / sizeof(ota_busy[0]));

    reset_test();
    g_record_busy = true;
    rdx_cmd_handle_sd_format(PROTOCOL_EVENT_CMD_SD_FORMAT, NULL, 0);
    assert_trace(record_busy, sizeof(record_busy) / sizeof(record_busy[0]));

    reset_test();
    g_wifi_busy = 1;
    rdx_cmd_handle_sd_format(PROTOCOL_EVENT_CMD_SD_FORMAT, NULL, 0);
    assert_trace(wifi_busy, sizeof(wifi_busy) / sizeof(wifi_busy[0]));
}

static void test_success_ack_precedes_request(void)
{
    static const enum trace_event expected[] = {
        TRACE_OTA_QUERY, TRACE_RECORD_QUERY, TRACE_WIFI_QUERY,
        TRACE_ACK_OK, TRACE_FORMAT_REQUEST,
    };

    reset_test();
    rdx_cmd_handle_sd_format(PROTOCOL_EVENT_CMD_SD_FORMAT, NULL, 0);
    assert_trace(expected, sizeof(expected) / sizeof(expected[0]));
    assert(g_format_callback == rdx_storage_service_format_cb);
}

static void test_initiation_error_does_not_change_ack(void)
{
    static const enum trace_event expected[] = {
        TRACE_OTA_QUERY, TRACE_RECORD_QUERY, TRACE_WIFI_QUERY,
        TRACE_ACK_OK, TRACE_FORMAT_REQUEST,
    };

    reset_test();
    g_format_result = -1;
    rdx_cmd_handle_sd_format(PROTOCOL_EVENT_CMD_SD_FORMAT, NULL, 0);
    assert_trace(expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_clean_result_and_legacy_wrapper(void)
{
    reset_test();
    assert(rdx_storage_service_format_for_app() == RDX_OK);
    assert(g_trace_count == 1);
    assert(g_format_callback == rdx_storage_service_format_cb);

    reset_test();
    g_format_result = 7;
    assert(rdx_storage_service_format_for_app() == RDX_ERR_IO);
    assert(g_trace_count == 1);

    reset_test();
    g_format_result = -1;
    assert(rdx_storage_service_format_for_app() == RDX_ERR_IO);
    assert(g_trace_count == 1);

    reset_test();
    g_format_result = -1;
    rdx_storage_service_format_handle();
    assert(g_trace_count == 1);
    assert(g_format_callback == rdx_storage_service_format_cb);
}

static void test_callback_events(void)
{
    static const enum trace_event success[] = {
        TRACE_FORMAT_DONE_EVENT,
    };

    reset_test();
    rdx_storage_service_format_cb(MEM_FORMAT_RESULT_OK);
    assert_trace(success, sizeof(success) / sizeof(success[0]));

    reset_test();
    rdx_storage_service_format_cb(MEM_FORMAT_RESULT_FAIL);
    assert(g_trace_count == 0);
}

int main(void)
{
    test_runtime_init();
    test_ops_null();
    test_busy_gates();
    test_success_ack_precedes_request();
    test_initiation_error_does_not_change_ack();
    test_clean_result_and_legacy_wrapper();
    test_callback_events();
    puts("P11 APP storage format Host tests passed.");
    return 0;
}
