#include "rdx_p11_trace_spy.h"

#include <string.h>

static rdx_p11_trace_sample_t g_samples[RDX_P11_TRACE_CAPACITY];
static size_t g_sample_count;
static rdx_p11_execution_context_t g_context;
static rdx_p11_trace_state_probe_t g_state_probe;

void rdx_p11_trace_spy_reset(void)
{
    memset(g_samples, 0, sizeof(g_samples));
    g_sample_count = 0;
    g_context = RDX_P11_CONTEXT_UNKNOWN;
    g_state_probe = NULL;
}

void rdx_p11_trace_spy_set_context(rdx_p11_execution_context_t context)
{
    g_context = context;
}

void rdx_p11_trace_spy_set_state_probe(rdx_p11_trace_state_probe_t probe)
{
    g_state_probe = probe;
}

void rdx_p11_trace_spy_emit(rdx_p11_trace_operation_t operation,
                            int16_t result,
                            uint32_t arg0,
                            uint32_t arg1)
{
    rdx_p11_trace_sample_t *sample;

    if (g_sample_count >= RDX_P11_TRACE_CAPACITY) {
        return;
    }

    sample = &g_samples[g_sample_count];
    sample->entry.sequence_id = (uint32_t)g_sample_count;
    sample->entry.execution_context = (uint8_t)g_context;
    sample->entry.operation = (uint8_t)operation;
    sample->entry.result = result;
    sample->entry.arg0 = arg0;
    sample->entry.arg1 = arg1;
    if (g_state_probe != NULL) {
        g_state_probe(&sample->owner_state);
    }
    g_sample_count++;
}

size_t rdx_p11_trace_spy_count(void)
{
    return g_sample_count;
}

const rdx_p11_trace_sample_t *rdx_p11_trace_spy_samples(void)
{
    return g_samples;
}

static int sample_equal(const rdx_p11_trace_sample_t *expected,
                        const rdx_p11_trace_sample_t *actual)
{
    return expected->entry.sequence_id == actual->entry.sequence_id &&
           expected->entry.execution_context == actual->entry.execution_context &&
           expected->entry.operation == actual->entry.operation &&
           expected->entry.result == actual->entry.result &&
           expected->entry.arg0 == actual->entry.arg0 &&
           expected->entry.arg1 == actual->entry.arg1 &&
           memcmp(&expected->owner_state,
                  &actual->owner_state,
                  sizeof(expected->owner_state)) == 0;
}

int rdx_p11_trace_compare(const rdx_p11_trace_sample_t *expected,
                          size_t expected_count,
                          const rdx_p11_trace_sample_t *actual,
                          size_t actual_count)
{
    size_t i;

    if (expected == NULL || actual == NULL || expected_count != actual_count) {
        return -1;
    }
    for (i = 0; i < expected_count; i++) {
        if (!sample_equal(&expected[i], &actual[i])) {
            return (int)i + 1;
        }
    }
    return 0;
}
