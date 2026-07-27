#include "test_minimal.h"
#include "rdx_record_service.h"
#include "rdx_record.h"
#include "rdx_jl_osal.h"

#include <string.h>

static RecordStatus g_status;
static int g_status_available;
static int g_process_calls;
static int g_post_calls;
static rdx_err_t g_post_result;
static const char *g_post_task;
static void (*g_post_callback)(void);

RecordStatus *rdx_record_get_status(void)
{
    return g_status_available ? &g_status : NULL;
}

void rdx_record_process(void)
{
    g_process_calls++;
}

rdx_err_t rdx_os_task_post_callback0(const char *task_name,
                                     void (*callback)(void))
{
    g_post_calls++;
    g_post_task = task_name;
    g_post_callback = callback;
    return g_post_result;
}

static void reset_fixture(u8 run)
{
    g_status.run = run;
    g_status_available = 1;
    g_process_calls = 0;
    g_post_calls = 0;
    g_post_result = RDX_OK;
    g_post_task = NULL;
    g_post_callback = NULL;
}

static int test_stop_now_preserves_legacy_transition(void)
{
    reset_fixture(RECORD_STATE_START);
    TEST_ASSERT(rdx_record_service_stop_now(RDX_RECORD_STOP_DUT) == RDX_OK);
    TEST_ASSERT(g_status.run == RECORD_STATE_STOP);
    TEST_ASSERT(g_process_calls == 1);
    TEST_ASSERT(g_post_calls == 0);

    reset_fixture(RECORD_STATE_PAUSE);
    TEST_ASSERT(rdx_record_service_stop_now(RDX_RECORD_STOP_CHARGE_PREPARE) == RDX_OK);
    TEST_ASSERT(g_status.run == RECORD_STATE_STOP);
    TEST_ASSERT(g_process_calls == 1);
    TEST_PASS();
}

static int test_exact_stop_is_idempotent(void)
{
    reset_fixture(RECORD_STATE_STOP);
    TEST_ASSERT(rdx_record_service_stop_now(RDX_RECORD_STOP_DUT) == RDX_OK);
    TEST_ASSERT(g_status.run == RECORD_STATE_STOP);
    TEST_ASSERT(g_process_calls == 0);

    TEST_ASSERT(rdx_record_service_stop_post(RDX_RECORD_STOP_APP_REQUEST) == RDX_OK);
    TEST_ASSERT(g_post_calls == 0);
    TEST_PASS();
}

static int test_stop_post_queues_once(void)
{
    reset_fixture(RECORD_STATE_RESUME);
    TEST_ASSERT(rdx_record_service_stop_post(RDX_RECORD_STOP_APP_REQUEST) == RDX_OK);
    TEST_ASSERT(g_status.run == RECORD_STATE_STOP);
    TEST_ASSERT(g_process_calls == 0);
    TEST_ASSERT(g_post_calls == 1);
    TEST_ASSERT(strcmp(g_post_task, "app_core") == 0);
    TEST_ASSERT(g_post_callback == rdx_record_process);

    g_post_callback();
    TEST_ASSERT(g_process_calls == 1);
    TEST_PASS();
}

static int test_stop_post_failure_keeps_stop(void)
{
    reset_fixture(RECORD_STATE_START);
    g_post_result = RDX_ERR_IO;
    TEST_ASSERT(rdx_record_service_stop_post(RDX_RECORD_STOP_UPLOAD_FALLBACK) == RDX_ERR_IO);
    TEST_ASSERT(g_status.run == RECORD_STATE_STOP);
    TEST_ASSERT(g_process_calls == 0);
    TEST_ASSERT(g_post_calls == 1);
    TEST_PASS();
}

static int test_command_validation_has_no_side_effects(void)
{
    reset_fixture(RECORD_STATE_START);
    TEST_ASSERT(rdx_record_service_stop_now((rdx_record_stop_reason_t)-1) == RDX_ERR_INVAL);
    TEST_ASSERT(g_status.run == RECORD_STATE_START);
    TEST_ASSERT(g_process_calls == 0);

    g_status_available = 0;
    TEST_ASSERT(rdx_record_service_stop_now(RDX_RECORD_STOP_DUT) == RDX_ERR_INVAL);
    TEST_ASSERT(g_process_calls == 0);
    TEST_ASSERT(g_post_calls == 0);
    TEST_PASS();
}

int main(void)
{
    TEST_RUN(test_stop_now_preserves_legacy_transition);
    TEST_RUN(test_exact_stop_is_idempotent);
    TEST_RUN(test_stop_post_queues_once);
    TEST_RUN(test_stop_post_failure_keeps_stop);
    TEST_RUN(test_command_validation_has_no_side_effects);
    return 0;
}
