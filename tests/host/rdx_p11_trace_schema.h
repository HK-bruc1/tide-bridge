#ifndef RDX_P11_TRACE_SCHEMA_H
#define RDX_P11_TRACE_SCHEMA_H

#include <stdint.h>

/* Host/diagnostic evidence only. Production sources do not include this file. */
typedef enum {
    RDX_P11_TRACE_FIELD_TRANSITION = 0,
    RDX_P11_TRACE_APP_MESSAGE,
    RDX_P11_TRACE_TASK_POST,
    RDX_P11_TRACE_LOCAL_PROCESS,
    RDX_P11_TRACE_PROTOCOL_INDICATE,
    RDX_P11_TRACE_POOL_ALLOC,
    RDX_P11_TRACE_POOL_RELEASE,
    RDX_P11_TRACE_TIMER_START,
    RDX_P11_TRACE_TIMER_STOP,
    RDX_P11_TRACE_TIMER_RESTART,
    RDX_P11_TRACE_STORAGE_CLEANUP,
    RDX_P11_TRACE_FORMAT_CALLBACK,
    RDX_P11_TRACE_FORMAT_REQUEST
} rdx_p11_trace_operation_t;

typedef enum {
    RDX_P11_CONTEXT_UNKNOWN = 0,
    RDX_P11_CONTEXT_APP_MESSAGE,
    RDX_P11_CONTEXT_APP_CORE,
    RDX_P11_CONTEXT_TIMER,
    RDX_P11_CONTEXT_BLE_EVENT,
    RDX_P11_CONTEXT_DUT,
    RDX_P11_CONTEXT_CURRENT_SYNC,
    RDX_P11_CONTEXT_FORMAT_CALLBACK
} rdx_p11_execution_context_t;

typedef struct {
    uint32_t sequence_id;
    uint8_t  execution_context;
    uint8_t  operation;
    int16_t  result;
    uint32_t arg0;
    uint32_t arg1;
} rdx_p11_trace_entry_t;

#define RDX_P11_TRACE_ENTRY_SIZE 16u

typedef struct {
    uint8_t run;
    uint8_t format;
    uint8_t scene;
    uint8_t mode;
    uint8_t original_mode;
    uint8_t switching;
    uint8_t key_triggered;
    uint8_t rerun_pending;
} rdx_p11_trace_owner_state_t;

typedef struct {
    rdx_p11_trace_entry_t entry;
    rdx_p11_trace_owner_state_t owner_state;
} rdx_p11_trace_sample_t;

#endif
