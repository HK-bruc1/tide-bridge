#ifndef RDX_P11_TRACE_SPY_H
#define RDX_P11_TRACE_SPY_H

#include <stddef.h>
#include "rdx_p11_trace_schema.h"

#define RDX_P11_TRACE_CAPACITY 256u

typedef void (*rdx_p11_trace_state_probe_t)(rdx_p11_trace_owner_state_t *out);

void rdx_p11_trace_spy_reset(void);
void rdx_p11_trace_spy_set_context(rdx_p11_execution_context_t context);
void rdx_p11_trace_spy_set_state_probe(rdx_p11_trace_state_probe_t probe);
void rdx_p11_trace_spy_emit(rdx_p11_trace_operation_t operation,
                            int16_t result,
                            uint32_t arg0,
                            uint32_t arg1);
size_t rdx_p11_trace_spy_count(void);
const rdx_p11_trace_sample_t *rdx_p11_trace_spy_samples(void);
int rdx_p11_trace_compare(const rdx_p11_trace_sample_t *expected,
                          size_t expected_count,
                          const rdx_p11_trace_sample_t *actual,
                          size_t actual_count);

#endif
