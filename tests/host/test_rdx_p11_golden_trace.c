#include "test_minimal.h"
#include "rdx_p11_trace_spy.h"

#include <string.h>

enum {
    TRACE_ARG_FORMAT_ACK_SUCCESS = 0,
    TRACE_ARG_FORMAT_ACK_FAILURE = 1,
    TRACE_ARG_RECORD_STOP = 3,
    TRACE_RESULT_POOL_FULL = -2,
    TRACE_RESULT_TASK_POST_FAILED = -3,
    TRACE_RESULT_TIMER_START_FAILED = -4
};

static rdx_p11_trace_owner_state_t g_owner_state;

static void owner_state_probe(rdx_p11_trace_owner_state_t *out)
{
    *out = g_owner_state;
}

static void begin_trace(rdx_p11_execution_context_t context)
{
    memset(&g_owner_state, 0, sizeof(g_owner_state));
    rdx_p11_trace_spy_reset();
    rdx_p11_trace_spy_set_context(context);
    rdx_p11_trace_spy_set_state_probe(owner_state_probe);
}

static int test_format_success_trace(void)
{
    const rdx_p11_trace_sample_t expected[2] = {
        {
            .entry = {
                .sequence_id = 0,
                .execution_context = RDX_P11_CONTEXT_APP_MESSAGE,
                .operation = RDX_P11_TRACE_PROTOCOL_INDICATE,
                .result = 0,
                .arg0 = TRACE_ARG_FORMAT_ACK_SUCCESS,
                .arg1 = 0
            }
        },
        {
            .entry = {
                .sequence_id = 1,
                .execution_context = RDX_P11_CONTEXT_APP_MESSAGE,
                .operation = RDX_P11_TRACE_FORMAT_REQUEST,
                .result = 0,
                .arg0 = 0,
                .arg1 = 0
            }
        }
    };

    begin_trace(RDX_P11_CONTEXT_APP_MESSAGE);
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_PROTOCOL_INDICATE,
                           0, TRACE_ARG_FORMAT_ACK_SUCCESS, 0);
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_FORMAT_REQUEST, 0, 0, 0);
    TEST_ASSERT(rdx_p11_trace_compare(expected, 2,
                                      rdx_p11_trace_spy_samples(),
                                      rdx_p11_trace_spy_count()) == 0);
    TEST_PASS();
}

static int test_wrong_context_is_detected(void)
{
    rdx_p11_trace_sample_t expected[1];

    begin_trace(RDX_P11_CONTEXT_APP_MESSAGE);
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_LOCAL_PROCESS, 0,
                           TRACE_ARG_RECORD_STOP, 0);
    expected[0] = rdx_p11_trace_spy_samples()[0];
    expected[0].entry.execution_context = RDX_P11_CONTEXT_APP_CORE;

    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(), 1) != 0);
    TEST_PASS();
}

static int test_wrong_parameter_and_result_are_detected(void)
{
    rdx_p11_trace_sample_t expected[1];

    begin_trace(RDX_P11_CONTEXT_TIMER);
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_POOL_ALLOC,
                           TRACE_RESULT_POOL_FULL, 0, 0);
    expected[0] = rdx_p11_trace_spy_samples()[0];
    expected[0].entry.result = 0;
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(), 1) != 0);

    expected[0] = rdx_p11_trace_spy_samples()[0];
    expected[0].entry.arg0 = 1;
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(), 1) != 0);
    TEST_PASS();
}

static int test_duplicate_ack_or_process_is_detected(void)
{
    rdx_p11_trace_sample_t expected[1];

    begin_trace(RDX_P11_CONTEXT_APP_MESSAGE);
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_PROTOCOL_INDICATE,
                           0, TRACE_ARG_FORMAT_ACK_FAILURE, 0);
    expected[0] = rdx_p11_trace_spy_samples()[0];
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_PROTOCOL_INDICATE,
                           0, TRACE_ARG_FORMAT_ACK_FAILURE, 0);
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(),
                                      rdx_p11_trace_spy_count()) != 0);

    begin_trace(RDX_P11_CONTEXT_APP_CORE);
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_LOCAL_PROCESS,
                           0, TRACE_ARG_RECORD_STOP, 0);
    expected[0] = rdx_p11_trace_spy_samples()[0];
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_LOCAL_PROCESS,
                           0, TRACE_ARG_RECORD_STOP, 0);
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(),
                                      rdx_p11_trace_spy_count()) != 0);
    TEST_PASS();
}

static int test_pool_failure_sequence_has_no_followup(void)
{
    const rdx_p11_trace_sample_t expected[1] = {
        {
            .entry = {
                .sequence_id = 0,
                .execution_context = RDX_P11_CONTEXT_TIMER,
                .operation = RDX_P11_TRACE_POOL_ALLOC,
                .result = TRACE_RESULT_POOL_FULL,
                .arg0 = 0,
                .arg1 = 0
            }
        }
    };

    begin_trace(RDX_P11_CONTEXT_TIMER);
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_POOL_ALLOC,
                           TRACE_RESULT_POOL_FULL, 0, 0);
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(),
                                      rdx_p11_trace_spy_count()) == 0);

    rdx_p11_trace_spy_emit(RDX_P11_TRACE_TIMER_START, 0, 3000, 0);
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(),
                                      rdx_p11_trace_spy_count()) != 0);
    TEST_PASS();
}

static int test_schema_size_is_frozen(void)
{
    TEST_ASSERT(sizeof(rdx_p11_trace_entry_t) == RDX_P11_TRACE_ENTRY_SIZE);
    TEST_PASS();
}

static int test_task_post_failure_has_no_process(void)
{
    rdx_p11_trace_sample_t expected[1];

    begin_trace(RDX_P11_CONTEXT_APP_MESSAGE);
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_TASK_POST,
                           TRACE_RESULT_TASK_POST_FAILED,
                           RDX_P11_CONTEXT_APP_CORE, 0);
    expected[0] = rdx_p11_trace_spy_samples()[0];
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(), 1) == 0);

    rdx_p11_trace_spy_emit(RDX_P11_TRACE_LOCAL_PROCESS,
                           0, TRACE_ARG_RECORD_STOP, 0);
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(),
                                      rdx_p11_trace_spy_count()) != 0);
    TEST_PASS();
}

static int test_timer_failure_has_no_dependent_post(void)
{
    rdx_p11_trace_sample_t expected[1];

    begin_trace(RDX_P11_CONTEXT_TIMER);
    rdx_p11_trace_spy_emit(RDX_P11_TRACE_TIMER_START,
                           TRACE_RESULT_TIMER_START_FAILED, 3000, 0);
    expected[0] = rdx_p11_trace_spy_samples()[0];
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(), 1) == 0);

    rdx_p11_trace_spy_emit(RDX_P11_TRACE_PROTOCOL_INDICATE, 0, 0, 0);
    TEST_ASSERT(rdx_p11_trace_compare(expected, 1,
                                      rdx_p11_trace_spy_samples(),
                                      rdx_p11_trace_spy_count()) != 0);
    TEST_PASS();
}

int main(void)
{
    TEST_RUN(test_format_success_trace);
    TEST_RUN(test_wrong_context_is_detected);
    TEST_RUN(test_wrong_parameter_and_result_are_detected);
    TEST_RUN(test_duplicate_ack_or_process_is_detected);
    TEST_RUN(test_pool_failure_sequence_has_no_followup);
    TEST_RUN(test_schema_size_is_frozen);
    TEST_RUN(test_task_post_failure_has_no_process);
    TEST_RUN(test_timer_failure_has_no_dependent_post);
    return 0;
}
