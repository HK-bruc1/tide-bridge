#include <assert.h>
#include <stdio.h>

#include "../../SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_file_transfer_cleanup_compat.c"

typedef enum {
    TRACE_UPLOAD_CLEAN = 1,
    TRACE_RECORD_BUFFER_FREE,
    TRACE_DAT_LIST_BUFFER_FREE,
    TRACE_BUSY_TIMER_STOP,
    TRACE_SEND_BUFFER_REINIT,
} trace_event_t;

static trace_event_t g_trace[8];
static unsigned g_trace_count;

static void trace(trace_event_t event)
{
    assert(g_trace_count < (sizeof(g_trace) / sizeof(g_trace[0])));
    g_trace[g_trace_count++] = event;
}

void rdx_protocol_uploadFileInfo_clean(void)
{
    trace(TRACE_UPLOAD_CLEAN);
}

void rdx_uxfile_recordFileData_sendBuf_free(void)
{
    trace(TRACE_RECORD_BUFFER_FREE);
}

void rdx_uxfile_datFileInfo_sendBuf_free(void)
{
    trace(TRACE_DAT_LIST_BUFFER_FREE);
}

void rdx_protocol_file_sync_busy_timer_stop(void)
{
    trace(TRACE_BUSY_TIMER_STOP);
}

void rdx_protocol_send_buffer_reinit(void)
{
    trace(TRACE_SEND_BUFFER_REINIT);
}

static void reset_trace(void)
{
    g_trace_count = 0;
}

static void test_record_disconnect_profile(void)
{
    reset_trace();
    assert(rdx_file_transfer_compat_cleanup_record_disconnect() == RDX_OK);
    assert(g_trace_count == 4);
    assert(g_trace[0] == TRACE_UPLOAD_CLEAN);
    assert(g_trace[1] == TRACE_RECORD_BUFFER_FREE);
    assert(g_trace[2] == TRACE_BUSY_TIMER_STOP);
    assert(g_trace[3] == TRACE_SEND_BUFFER_REINIT);
}

static void test_delayed_ble_profile(void)
{
    reset_trace();
    assert(rdx_file_transfer_compat_cleanup_ble_delayed() == RDX_OK);
    assert(g_trace_count == 5);
    assert(g_trace[0] == TRACE_UPLOAD_CLEAN);
    assert(g_trace[1] == TRACE_RECORD_BUFFER_FREE);
    assert(g_trace[2] == TRACE_DAT_LIST_BUFFER_FREE);
    assert(g_trace[3] == TRACE_BUSY_TIMER_STOP);
    assert(g_trace[4] == TRACE_SEND_BUFFER_REINIT);
}

int main(void)
{
    test_record_disconnect_profile();
    test_delayed_ble_profile();
    puts("P11 file-transfer cleanup host tests passed.");
    return 0;
}
