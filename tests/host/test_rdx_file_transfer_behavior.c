#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Keep production headers out of this Host-only dependency injection harness. */
#define __RDX_FILE_TRANSFER_COMPAT_H__

typedef unsigned char u8;
typedef unsigned int u32;
typedef int rdx_err_t;

enum {
    RDX_OK = 0,
    RDX_ERR_INVAL = -22,
};

enum {
    false = 0,
    true = 1,
};

typedef struct {
    int ack;
    int file_num;
    u8 is_first_pack;
    int pack_num;
    int orig_pack_num;
    int sent_size;
    int auto_del;
    int file_offset;
    u32 total_pack;
    u32 chunk;
    u32 block_cnt;
    u8 file_send_busy;
    u8 loop;
    u8 interrupt;
    u8 send_stop;
    u8 ble_upload_cancel;
} ReqFileInfo;

typedef struct {
    int available;
    int busy;
    int stopped;
} rdx_file_transfer_compat_status_t;

typedef enum {
    RDX_FILE_TRANSFER_TIMER_STOP = 0,
    RDX_FILE_TRANSFER_TIMER_START,
} rdx_file_transfer_timer_action_t;

typedef void (*rdx_file_transfer_timer_control_t)(
    rdx_file_transfer_timer_action_t action,
    const void *ctx);

#define RDX_PROTOCOL_SEND_TASK_NAME "protocol_send_task"
#define OS_NO_ERR 0

ReqFileInfo *rdx_protocol_get_uploadfileInfo(void);
u8 rdx_is_file_transfer_active(void);
u8 rdx_is_file_sync_busy(void);
void rdx_protocol_send_buffer_reinit(void);
void rdx_protocol_uploadFileInfo_clean(void);
void rdx_uxfile_recordFileData_sendBuf_free(void);
void rdx_uxfile_datFileInfo_sendBuf_free(void);
void rdx_protocol_file_sync_busy_timer_stop(void);
void rdx_protocol_prepared_data_clean(void);
int os_taskq_post_msg(const char *name, int argc, ...);
void rdx_os_time_dly(u32 ticks);

#include "../../SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_file_transfer_compat.c"

enum test_event {
    EVENT_UPLOAD_CLEAN = 1,
    EVENT_RECORD_BUFFER_FREE,
    EVENT_DAT_BUFFER_FREE,
    EVENT_BUSY_TIMER_STOP,
    EVENT_SEND_BUFFER_REINIT,
    EVENT_PREPARED_DATA_CLEAN,
    EVENT_TIMER_STOP,
    EVENT_TIMER_START,
    EVENT_DELAY_2,
    EVENT_DELAY_1,
    EVENT_POST,
};

#define MAX_EVENTS 32
#define MAX_POSTS 4

static ReqFileInfo g_info;
static ReqFileInfo *g_owner;
static u8 g_active;
static u8 g_sync_busy;
static int g_events[MAX_EVENTS];
static int g_event_count;
static int g_post_results[MAX_POSTS];
static int g_post_result_count;
static int g_post_calls;
static ReqFileInfo *g_post_owners[MAX_POSTS];
static int g_post_argc[MAX_POSTS];
static const char *g_post_task[MAX_POSTS];
static const void *g_timer_ctx_seen;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            return __LINE__; \
        } \
    } while (0)

#define CHECK_EVENT(index, event) CHECK(g_events[(index)] == (event))

static void log_event(int event)
{
    if (g_event_count < MAX_EVENTS) {
        g_events[g_event_count++] = event;
    }
}

static void reset_fixture(void)
{
    memset(&g_info, 0, sizeof(g_info));
    memset(g_events, 0, sizeof(g_events));
    memset(g_post_results, 0, sizeof(g_post_results));
    memset(g_post_owners, 0, sizeof(g_post_owners));
    memset(g_post_argc, 0, sizeof(g_post_argc));
    memset(g_post_task, 0, sizeof(g_post_task));
    g_owner = &g_info;
    g_active = 0;
    g_sync_busy = 0;
    g_event_count = 0;
    g_post_result_count = 0;
    g_post_calls = 0;
    g_timer_ctx_seen = NULL;
}

ReqFileInfo *rdx_protocol_get_uploadfileInfo(void)
{
    return g_owner;
}

u8 rdx_is_file_transfer_active(void)
{
    return g_active;
}

u8 rdx_is_file_sync_busy(void)
{
    return g_sync_busy;
}

void rdx_protocol_send_buffer_reinit(void)
{
    log_event(EVENT_SEND_BUFFER_REINIT);
}

void rdx_protocol_uploadFileInfo_clean(void)
{
    log_event(EVENT_UPLOAD_CLEAN);
}

void rdx_uxfile_recordFileData_sendBuf_free(void)
{
    log_event(EVENT_RECORD_BUFFER_FREE);
}

void rdx_uxfile_datFileInfo_sendBuf_free(void)
{
    log_event(EVENT_DAT_BUFFER_FREE);
}

void rdx_protocol_file_sync_busy_timer_stop(void)
{
    log_event(EVENT_BUSY_TIMER_STOP);
}

void rdx_protocol_prepared_data_clean(void)
{
    log_event(EVENT_PREPARED_DATA_CLEAN);
}

int os_taskq_post_msg(const char *name, int argc, ...)
{
    va_list args;
    ReqFileInfo *owner;
    int result = OS_NO_ERR;

    va_start(args, argc);
    owner = va_arg(args, ReqFileInfo *);
    va_end(args);

    if (g_post_calls < MAX_POSTS) {
        g_post_task[g_post_calls] = name;
        g_post_argc[g_post_calls] = argc;
        g_post_owners[g_post_calls] = owner;
        if (g_post_calls < g_post_result_count) {
            result = g_post_results[g_post_calls];
        }
    }
    g_post_calls++;
    log_event(EVENT_POST);
    return result;
}

void rdx_os_time_dly(u32 ticks)
{
    if (ticks == 2) {
        log_event(EVENT_DELAY_2);
    } else if (ticks == 1) {
        log_event(EVENT_DELAY_1);
    } else {
        log_event(-1);
    }
}

static void mock_timer_control(
    rdx_file_transfer_timer_action_t action,
    const void *ctx)
{
    g_timer_ctx_seen = ctx;
    log_event(action == RDX_FILE_TRANSFER_TIMER_STOP
              ? EVENT_TIMER_STOP : EVENT_TIMER_START);
}

static int test_queries(void)
{
    rdx_file_transfer_compat_status_t status;
    int value = -1;

    reset_fixture();
    CHECK(rdx_file_transfer_compat_get_status(NULL) == RDX_ERR_INVAL);
    CHECK(rdx_file_transfer_compat_get_active(NULL) == RDX_ERR_INVAL);
    CHECK(rdx_file_transfer_compat_get_sync_busy(NULL) == RDX_ERR_INVAL);

    g_owner = NULL;
    CHECK(rdx_file_transfer_compat_get_status(&status) == RDX_OK);
    CHECK(status.available == 0);
    CHECK(status.busy == 0);
    CHECK(status.stopped == 0);

    g_owner = &g_info;
    g_info.file_send_busy = true;
    g_info.send_stop = true;
    CHECK(rdx_file_transfer_compat_get_status(&status) == RDX_OK);
    CHECK(status.available == 1);
    CHECK(status.busy == 1);
    CHECK(status.stopped == 1);

    g_active = 3;
    g_sync_busy = 7;
    CHECK(rdx_file_transfer_compat_get_active(&value) == RDX_OK);
    CHECK(value == 3);
    CHECK(rdx_file_transfer_compat_get_sync_busy(&value) == RDX_OK);
    CHECK(value == 7);
    return 0;
}

static int test_lifecycle_profiles(void)
{
    reset_fixture();
    rdx_file_transfer_compat_on_ble_connected();
    CHECK(g_event_count == 1);
    CHECK_EVENT(0, EVENT_SEND_BUFFER_REINIT);

    reset_fixture();
    CHECK(rdx_file_transfer_compat_cleanup_record_disconnect() == RDX_OK);
    CHECK(g_event_count == 4);
    CHECK_EVENT(0, EVENT_UPLOAD_CLEAN);
    CHECK_EVENT(1, EVENT_RECORD_BUFFER_FREE);
    CHECK_EVENT(2, EVENT_BUSY_TIMER_STOP);
    CHECK_EVENT(3, EVENT_SEND_BUFFER_REINIT);

    reset_fixture();
    CHECK(rdx_file_transfer_compat_cleanup_ble_delayed() == RDX_OK);
    CHECK(g_event_count == 5);
    CHECK_EVENT(0, EVENT_UPLOAD_CLEAN);
    CHECK_EVENT(1, EVENT_RECORD_BUFFER_FREE);
    CHECK_EVENT(2, EVENT_DAT_BUFFER_FREE);
    CHECK_EVENT(3, EVENT_BUSY_TIMER_STOP);
    CHECK_EVENT(4, EVENT_SEND_BUFFER_REINIT);
    return 0;
}

static int test_null_owner(void)
{
    reset_fixture();
    g_owner = NULL;
    rdx_file_transfer_compat_on_tx_done(mock_timer_control, &g_info);
    rdx_file_transfer_compat_retry_on_stuck();
    CHECK(g_event_count == 0);
    CHECK(g_post_calls == 0);
    return 0;
}

static int test_send_stop_branch(void)
{
    reset_fixture();
    g_info.file_send_busy = true;
    g_info.send_stop = true;
    g_info.interrupt = true;
    rdx_file_transfer_compat_on_tx_done(mock_timer_control, &g_info);
    CHECK(g_info.file_send_busy == false);
    CHECK(g_info.interrupt == false);
    CHECK(g_event_count == 5);
    CHECK_EVENT(0, EVENT_BUSY_TIMER_STOP);
    CHECK_EVENT(1, EVENT_RECORD_BUFFER_FREE);
    CHECK_EVENT(2, EVENT_PREPARED_DATA_CLEAN);
    CHECK_EVENT(3, EVENT_TIMER_STOP);
    CHECK_EVENT(4, EVENT_TIMER_START);
    CHECK(g_timer_ctx_seen == &g_info);
    CHECK(g_post_calls == 0);

    reset_fixture();
    g_info.send_stop = true;
    g_info.interrupt = true;
    rdx_file_transfer_compat_on_tx_done(NULL, NULL);
    CHECK(g_info.interrupt == false);
    CHECK(g_event_count == 3);
    CHECK_EVENT(0, EVENT_BUSY_TIMER_STOP);
    CHECK_EVENT(1, EVENT_RECORD_BUFFER_FREE);
    CHECK_EVENT(2, EVENT_PREPARED_DATA_CLEAN);
    return 0;
}

static int test_interrupt_branch(void)
{
    reset_fixture();
    g_info.file_send_busy = true;
    g_info.interrupt = true;
    g_info.loop = true;
    rdx_file_transfer_compat_on_tx_done(NULL, NULL);
    CHECK(g_info.file_send_busy == false);
    CHECK(g_info.interrupt == false);
    CHECK(g_event_count == 1);
    CHECK_EVENT(0, EVENT_POST);
    CHECK(g_post_calls == 1);
    CHECK(g_post_argc[0] == 1);
    CHECK(g_post_owners[0] == &g_info);
    CHECK(strcmp(g_post_task[0], RDX_PROTOCOL_SEND_TASK_NAME) == 0);

    reset_fixture();
    g_info.interrupt = true;
    g_info.loop = false;
    rdx_file_transfer_compat_on_tx_done(NULL, NULL);
    CHECK(g_info.interrupt == false);
    CHECK(g_event_count == 0);
    CHECK(g_post_calls == 0);
    return 0;
}

static int test_normal_loop_branch(void)
{
    reset_fixture();
    g_info.loop = true;
    g_info.total_pack = 10;
    g_info.pack_num = 3;
    g_info.ack = 9;
    rdx_file_transfer_compat_on_tx_done(NULL, NULL);
    CHECK(g_info.pack_num == 4);
    CHECK(g_info.ack == 0);
    CHECK(g_event_count == 2);
    CHECK_EVENT(0, EVENT_DELAY_2);
    CHECK_EVENT(1, EVENT_POST);
    CHECK(g_post_calls == 1);
    CHECK(g_post_owners[0] == &g_info);

    reset_fixture();
    g_info.loop = true;
    g_info.total_pack = 3;
    g_info.pack_num = 3;
    g_info.ack = 9;
    rdx_file_transfer_compat_on_tx_done(NULL, NULL);
    CHECK(g_info.pack_num == 0);
    CHECK(g_info.ack == 0);
    CHECK(g_post_calls == 1);

    reset_fixture();
    g_info.loop = false;
    g_info.file_send_busy = true;
    rdx_file_transfer_compat_on_tx_done(NULL, NULL);
    CHECK(g_info.file_send_busy == false);
    CHECK(g_event_count == 0);
    CHECK(g_post_calls == 0);
    return 0;
}

static int test_first_post_failure_retries_once(void)
{
    reset_fixture();
    g_info.loop = true;
    g_info.total_pack = 4;
    g_info.pack_num = 1;
    g_post_result_count = 2;
    g_post_results[0] = -1;
    g_post_results[1] = -2;
    rdx_file_transfer_compat_on_tx_done(NULL, NULL);
    CHECK(g_event_count == 4);
    CHECK_EVENT(0, EVENT_DELAY_2);
    CHECK_EVENT(1, EVENT_POST);
    CHECK_EVENT(2, EVENT_DELAY_1);
    CHECK_EVENT(3, EVENT_POST);
    CHECK(g_post_calls == 2);
    CHECK(g_post_argc[0] == 1);
    CHECK(g_post_argc[1] == 1);
    CHECK(g_post_owners[0] == &g_info);
    CHECK(g_post_owners[1] == &g_info);
    CHECK(strcmp(g_post_task[0], RDX_PROTOCOL_SEND_TASK_NAME) == 0);
    CHECK(strcmp(g_post_task[1], RDX_PROTOCOL_SEND_TASK_NAME) == 0);
    return 0;
}

static int test_stuck_retry(void)
{
    reset_fixture();
    g_info.loop = true;
    rdx_file_transfer_compat_retry_on_stuck();
    CHECK(g_info.ack == 1);
    CHECK(g_post_calls == 1);
    CHECK(g_post_owners[0] == &g_info);
    CHECK(g_event_count == 1);
    CHECK_EVENT(0, EVENT_POST);

    reset_fixture();
    g_info.loop = false;
    rdx_file_transfer_compat_retry_on_stuck();
    CHECK(g_post_calls == 0);

    reset_fixture();
    g_info.loop = true;
    g_info.send_stop = true;
    rdx_file_transfer_compat_retry_on_stuck();
    CHECK(g_post_calls == 0);

    reset_fixture();
    g_info.loop = true;
    g_info.interrupt = true;
    rdx_file_transfer_compat_retry_on_stuck();
    CHECK(g_post_calls == 0);

    reset_fixture();
    g_info.loop = true;
    g_post_result_count = 1;
    g_post_results[0] = -1;
    rdx_file_transfer_compat_retry_on_stuck();
    CHECK(g_post_calls == 1);
    CHECK(g_event_count == 1);
    return 0;
}

typedef int (*test_fn_t)(void);

static int run_test(const char *name, test_fn_t test)
{
    int line = test();
    if (line != 0) {
        printf("FAIL: %s at line %d\n", name, line);
        return 1;
    }
    printf("PASS: %s\n", name);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += run_test("queries preserve unavailable and raw activity values", test_queries);
    failures += run_test("lifecycle cleanup profiles preserve exact order", test_lifecycle_profiles);
    failures += run_test("NULL owner has no side effects", test_null_owner);
    failures += run_test("send-stop branch preserves cleanup and timer order", test_send_stop_branch);
    failures += run_test("interrupt branch preserves loop post semantics", test_interrupt_branch);
    failures += run_test("normal loop preserves pack and ACK progression", test_normal_loop_branch);
    failures += run_test("first post failure retries exactly once", test_first_post_failure_retries_once);
    failures += run_test("stuck retry preserves guards and single post", test_stuck_retry);

    if (failures != 0) {
        printf("RDX file-transfer behavior failures: %d\n", failures);
        return 1;
    }
    return 0;
}
