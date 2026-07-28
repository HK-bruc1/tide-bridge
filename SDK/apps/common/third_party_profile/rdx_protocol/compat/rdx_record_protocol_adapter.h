#ifndef __RDX_RECORD_PROTOCOL_ADAPTER_H__
#define __RDX_RECORD_PROTOCOL_ADAPTER_H__

#include <stdbool.h>
#include "typedef.h"
#include "rdx_err.h"

typedef enum {
    RDX_RECORD_TRIGGER_PAYLOAD_UPLOAD = 0,
    RDX_RECORD_TRIGGER_PAYLOAD_DEVICE,
    RDX_RECORD_TRIGGER_PAYLOAD_SWITCH,
} rdx_record_trigger_payload_kind_t;

typedef struct {
    rdx_record_trigger_payload_kind_t kind;
    u8 run;
    u8 format;
    u8 scene;
    u8 mode;
    u8 factor;
} rdx_record_trigger_payload_t;

typedef struct {
    u8 slot;
    u8 factor;
    rdx_record_trigger_payload_kind_t kind;
    bool active;
    bool filled;
} rdx_record_protocol_reservation_t;

void rdx_record_protocol_adapter_init(void);
rdx_err_t rdx_record_protocol_reserve(
    rdx_record_trigger_payload_kind_t kind,
    rdx_record_protocol_reservation_t *reservation);
rdx_err_t rdx_record_protocol_fill_reserved(
    rdx_record_protocol_reservation_t *reservation,
    const rdx_record_trigger_payload_t *payload);
rdx_err_t rdx_record_protocol_post_reserved(
    rdx_record_protocol_reservation_t *reservation);
void rdx_record_protocol_cancel_reserved(
    rdx_record_protocol_reservation_t *reservation);
rdx_err_t rdx_record_protocol_post_trigger(
    const rdx_record_trigger_payload_t *payload);

#endif
