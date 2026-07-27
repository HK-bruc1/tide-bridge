#ifndef __RDX_RECORD_DOMAIN_H__
#define __RDX_RECORD_DOMAIN_H__

#include "typedef.h"
#include "rdx_err.h"
#include "rdx_record_service_types.h"

rdx_err_t rdx_record_domain_get_activity(rdx_record_activity_t *out);
rdx_err_t rdx_record_domain_get_scene(rdx_record_scene_t *out);
rdx_err_t rdx_record_domain_get_path(rdx_record_path_t *out);
rdx_err_t rdx_record_domain_get_running(bool *out);
bool rdx_record_domain_is_offline_active(void);
rdx_err_t rdx_record_domain_prepare_stop(rdx_record_stop_reason_t reason);
rdx_err_t rdx_record_domain_stop_now(rdx_record_stop_reason_t reason);
rdx_err_t rdx_record_domain_stop_post(rdx_record_stop_reason_t reason);
rdx_err_t rdx_record_domain_stop_running_now(rdx_record_stop_reason_t reason);
rdx_err_t rdx_record_domain_set_path(rdx_record_path_t path);
rdx_err_t rdx_record_domain_mark_key_triggered(void);
rdx_err_t rdx_record_domain_complete_switch(bool *restart,
                                            rdx_record_scene_t *scene);
rdx_err_t rdx_record_domain_handle_ble_disconnected(bool switch_to_offline,
                                                    bool rerun);

#endif
