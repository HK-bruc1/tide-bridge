#ifndef __RDX_RECORD_DOMAIN_H__
#define __RDX_RECORD_DOMAIN_H__

#include "typedef.h"
#include "rdx_err.h"
#include "rdx_record_service_types.h"

typedef struct {
    u8 run;
    u8 format;
    u8 scene;
    u8 mode;
    u8 original_mode;
} rdx_record_domain_state_t;

typedef struct {
    rdx_record_domain_state_t trigger;
    bool start_upload_timer;
    rdx_err_t process_post_result;
} rdx_record_domain_toggle_result_t;

typedef u16 (*rdx_record_domain_connection_query_t)(void);

rdx_err_t rdx_record_domain_get_activity(rdx_record_activity_t *out);
rdx_err_t rdx_record_domain_get_scene(rdx_record_scene_t *out);
rdx_err_t rdx_record_domain_get_path(rdx_record_path_t *out);
rdx_err_t rdx_record_domain_get_running(bool *out);
rdx_err_t rdx_record_domain_get_state(rdx_record_domain_state_t *out);
bool rdx_record_domain_is_offline_active(void);
rdx_err_t rdx_record_domain_prepare_stop(rdx_record_stop_reason_t reason);
rdx_err_t rdx_record_domain_stop_now(rdx_record_stop_reason_t reason);
rdx_err_t rdx_record_domain_stop_post(rdx_record_stop_reason_t reason);
rdx_err_t rdx_record_domain_post_process(void);
rdx_err_t rdx_record_domain_stop_running_now(rdx_record_stop_reason_t reason);
rdx_err_t rdx_record_domain_set_path(rdx_record_path_t path);
rdx_err_t rdx_record_domain_mark_key_triggered(void);
rdx_err_t rdx_record_domain_complete_switch(bool *restart,
                                            rdx_record_scene_t *scene);
rdx_err_t rdx_record_domain_handle_ble_disconnected(bool switch_to_offline,
                                                    bool rerun);
rdx_err_t rdx_record_domain_prepare_upload_fallback(
    rdx_record_stop_reason_t reason,
    rdx_record_domain_connection_query_t connection_query,
    u16 *connection_handle,
    rdx_record_domain_state_t *trigger);
rdx_err_t rdx_record_domain_prepare_connected_toggle(
    rdx_record_scene_t scene,
    rdx_record_domain_toggle_result_t *out);
rdx_err_t rdx_record_domain_toggle_post(rdx_record_scene_t scene);
rdx_err_t rdx_record_domain_mode_active_check(
    bool *mode_changed,
    rdx_record_domain_state_t *state);
rdx_err_t rdx_record_domain_prepare_switch(
    rdx_record_scene_t original_scene,
    bool ble_connected,
    bool *trigger_required,
    rdx_record_domain_state_t *trigger);
rdx_err_t rdx_record_domain_prepare_switch_compat(
    u8 original_scene,
    bool ble_connected,
    bool *trigger_required,
    rdx_record_domain_state_t *trigger);

#endif
