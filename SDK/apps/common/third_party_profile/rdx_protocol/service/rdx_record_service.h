#ifndef __RDX_RECORD_SERVICE_H__
#define __RDX_RECORD_SERVICE_H__

#include "typedef.h"
#include "rdx_err.h"
#include "rdx_record_service_types.h"
#include <stdbool.h>

void rdx_record_service_init(void);
void rdx_record_service_exit(void);

/* recording orchestration (moved from rdx_app.c) */
void rdx_record_service_device_record_handle(u8 scene);
void rdx_record_service_switch(u8 orig_scene);
void rdx_record_service_upload_timer_cb(void *priv);
void rdx_record_service_upload_timer_stop(void);
void rdx_record_service_upload_timer_start(void);

/* record mode state */
u8   rdx_record_service_get_mode(void);
void rdx_record_service_set_mode(u8 d);
void rdx_record_service_mode_active_check(bool show);

/* BLE-driven mode switching */
void rdx_record_service_set_mode_online(void);
void rdx_record_service_set_mode_offline(void);
bool rdx_record_service_is_running(void);
bool rdx_record_service_can_auto_shutdown(void);
rdx_err_t rdx_record_service_get_activity(rdx_record_activity_t *out);
rdx_err_t rdx_record_service_get_scene(rdx_record_scene_t *out);
rdx_err_t rdx_record_service_get_path(rdx_record_path_t *out);
bool rdx_record_service_is_offline_active(void);
rdx_err_t rdx_record_service_handle_ble_disconnected(void);
rdx_err_t rdx_record_service_sync_state_after_ble_write_ready(void);

/* caller-selected execution context; commands never infer the current task */
rdx_err_t rdx_record_service_stop_now(rdx_record_stop_reason_t reason);
rdx_err_t rdx_record_service_stop_post(rdx_record_stop_reason_t reason);

/* shell stubs — filled during extraction */
void rdx_record_service_start(u8 mode);
void rdx_record_service_stop(void);
u8   rdx_record_service_get_state(void);
u8   rdx_record_service_is_active(void);

/* BLE cutover API — Stage 5 */
rdx_err_t rdx_record_service_stop_from_ble(void);

#endif
