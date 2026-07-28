#include "rdx_record_domain.h"
#include "rdx_record.h"
#include "rdx_jl_osal.h"

_Static_assert(RECORD_STATE_START == 0u, "legacy record start value changed");
_Static_assert(RECORD_STATE_PAUSE == 1u, "legacy record pause value changed");
_Static_assert(RECORD_STATE_RESUME == 2u, "legacy record resume value changed");
_Static_assert(RECORD_STATE_STOP == 3u, "legacy record stop value changed");
_Static_assert(RECORD_SCENE_CHAT == 0u, "legacy record chat scene value changed");
_Static_assert(RECORD_SCENE_CALL == 1u, "legacy record call scene value changed");
_Static_assert(RECORD_MODE_OFFLINE == 0u, "legacy record offline mode value changed");
_Static_assert(RECORD_MODE_ONLINE == 1u, "legacy record online mode value changed");
_Static_assert(RECORD_FORMATE_OPUS_16K_STERO == 2u,
               "legacy stereo record format value changed");

static rdx_err_t rdx_record_domain_scene_to_legacy(rdx_record_scene_t scene,
                                                   u8 *legacy_scene)
{
    if (!legacy_scene) {
        return RDX_ERR_INVAL;
    }
    switch (scene) {
    case RDX_RECORD_SCENE_CHAT:
        *legacy_scene = RECORD_SCENE_CHAT;
        return RDX_OK;
    case RDX_RECORD_SCENE_CALL:
        *legacy_scene = RECORD_SCENE_CALL;
        return RDX_OK;
    default:
        return RDX_ERR_INVAL;
    }
}

static void rdx_record_domain_copy_state(const RecordStatus *rp,
                                         rdx_record_domain_state_t *out)
{
    out->run = rp->run;
    out->format = rp->formate;
    out->scene = rp->scene;
    out->mode = rp->mode;
    out->original_mode = rp->orig_mode;
}

rdx_err_t rdx_record_domain_get_activity(rdx_record_activity_t *out)
{
    RecordStatus *rp;

    if (!out || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    switch (rp->run) {
    case RECORD_STATE_STOP:
        *out = RDX_RECORD_ACTIVITY_IDLE;
        break;
    case RECORD_STATE_PAUSE:
        *out = RDX_RECORD_ACTIVITY_PAUSED;
        break;
    case RECORD_STATE_START:
    case RECORD_STATE_RESUME:
        *out = RDX_RECORD_ACTIVITY_ACTIVE;
        break;
    default:
        return RDX_ERR_INVAL;
    }
    return RDX_OK;
}

rdx_err_t rdx_record_domain_get_scene(rdx_record_scene_t *out)
{
    RecordStatus *rp;

    if (!out || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    switch (rp->scene) {
    case RECORD_SCENE_CHAT:
        *out = RDX_RECORD_SCENE_CHAT;
        return RDX_OK;
    case RECORD_SCENE_CALL:
        *out = RDX_RECORD_SCENE_CALL;
        return RDX_OK;
    default:
        return RDX_ERR_INVAL;
    }
}

rdx_err_t rdx_record_domain_get_path(rdx_record_path_t *out)
{
    RecordStatus *rp;

    if (!out || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    switch (rp->mode) {
    case RECORD_MODE_OFFLINE:
        *out = RDX_RECORD_PATH_OFFLINE;
        return RDX_OK;
    case RECORD_MODE_ONLINE:
        *out = RDX_RECORD_PATH_ONLINE;
        return RDX_OK;
    default:
        return RDX_ERR_INVAL;
    }
}

rdx_err_t rdx_record_domain_get_running(bool *out)
{
    RecordStatus *rp;

    if (!out || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    *out = rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME;
    return RDX_OK;
}

rdx_err_t rdx_record_domain_get_state(rdx_record_domain_state_t *out)
{
    RecordStatus *rp;

    if (!out || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    rdx_record_domain_copy_state(rp, out);
    return RDX_OK;
}

bool rdx_record_domain_is_offline_active(void)
{
    RecordStatus *rp = rdx_record_get_status();
    return rp && rp->orig_mode == RECORD_MODE_OFFLINE &&
           (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME);
}

static bool rdx_record_domain_stop_reason_valid(rdx_record_stop_reason_t reason)
{
    switch (reason) {
    case RDX_RECORD_STOP_APP_REQUEST:
    case RDX_RECORD_STOP_BLE_DISCONNECT:
    case RDX_RECORD_STOP_CHARGE_PREPARE:
    case RDX_RECORD_STOP_DUT:
    case RDX_RECORD_STOP_POWEROFF:
    case RDX_RECORD_STOP_IDLE:
    case RDX_RECORD_STOP_UPLOAD_FALLBACK:
        return true;
    default:
        return false;
    }
}

static rdx_err_t rdx_record_domain_prepare_stop_internal(
    rdx_record_stop_reason_t reason, bool *changed)
{
    RecordStatus *rp;

    if (!rdx_record_domain_stop_reason_valid(reason) ||
        !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    if (changed) {
        *changed = rp->run != RECORD_STATE_STOP;
    }
    if (rp->run != RECORD_STATE_STOP) {
        rp->run = RECORD_STATE_STOP;
    }
    return RDX_OK;
}

rdx_err_t rdx_record_domain_prepare_stop(rdx_record_stop_reason_t reason)
{
    return rdx_record_domain_prepare_stop_internal(reason, NULL);
}

rdx_err_t rdx_record_domain_stop_now(rdx_record_stop_reason_t reason)
{
    bool changed;
    rdx_err_t ret = rdx_record_domain_prepare_stop_internal(reason, &changed);

    if (ret == RDX_OK && changed) {
        rdx_record_process();
    }
    return ret;
}

rdx_err_t rdx_record_domain_stop_post(rdx_record_stop_reason_t reason)
{
    bool changed;
    rdx_err_t ret = rdx_record_domain_prepare_stop_internal(reason, &changed);

    if (ret != RDX_OK || !changed) {
        return ret;
    }
    if (rdx_os_task_post_callback0("app_core", rdx_record_process) != RDX_OK) {
        return RDX_ERR_IO;
    }
    return RDX_OK;
}

rdx_err_t rdx_record_domain_post_process(void)
{
    return rdx_os_task_post_callback(
               "app_core",
               (void (*)(void *))rdx_record_process,
               NULL) == RDX_OK
               ? RDX_OK
               : RDX_ERR_IO;
}

rdx_err_t rdx_record_domain_stop_running_now(rdx_record_stop_reason_t reason)
{
    RecordStatus *rp;

    if (!rdx_record_domain_stop_reason_valid(reason) ||
        !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    if (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME) {
        rp->run = RECORD_STATE_STOP;
        rdx_record_process();
    }
    return RDX_OK;
}

rdx_err_t rdx_record_domain_set_path(rdx_record_path_t path)
{
    RecordStatus *rp = rdx_record_get_status();

    if (!rp) {
        return RDX_ERR_INVAL;
    }
    switch (path) {
    case RDX_RECORD_PATH_ONLINE:
        rp->mode = RECORD_MODE_ONLINE;
        if (rp->run == RECORD_STATE_STOP) {
            rp->orig_mode = RECORD_MODE_ONLINE;
        }
        return RDX_OK;
    case RDX_RECORD_PATH_OFFLINE:
        if (rp->orig_mode != RECORD_MODE_OFFLINE) {
            rp->mode = RECORD_MODE_OFFLINE;
            rp->orig_mode = RECORD_MODE_OFFLINE;
        }
        return RDX_OK;
    default:
        return RDX_ERR_INVAL;
    }
}

rdx_err_t rdx_record_domain_mark_key_triggered(void)
{
    RecordStatus *rp = rdx_record_get_status();

    if (!rp) {
        return RDX_ERR_INVAL;
    }
    if (rp->run != RECORD_STATE_STOP) {
        return RDX_ERR_BUSY;
    }
    rp->key_trigger = true;
    return RDX_OK;
}

rdx_err_t rdx_record_domain_complete_switch(bool *restart,
                                            rdx_record_scene_t *scene)
{
    RecordStatus *rp;

    if (!restart || !scene || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    *restart = false;
    *scene = RDX_RECORD_SCENE_CALL;
    rp->is_switch = false;
    if (rp->run == RECORD_STATE_STOP) {
        *restart = true;
        if (rp->scene == RECORD_SCENE_CHAT) {
            *scene = RDX_RECORD_SCENE_CHAT;
        }
    }
    return RDX_OK;
}

rdx_err_t rdx_record_domain_handle_ble_disconnected(bool switch_to_offline,
                                                    bool rerun)
{
    RecordStatus *rp = rdx_record_get_status();

    if (!rp) {
        return RDX_ERR_INVAL;
    }
    if (switch_to_offline) {
        if (rp->orig_mode != RECORD_MODE_OFFLINE) {
            rp->mode = RECORD_MODE_OFFLINE;
            rp->orig_mode = RECORD_MODE_OFFLINE;
        }
        return RDX_OK;
    }
    if (rp->orig_mode != RECORD_MODE_OFFLINE &&
        (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME)) {
        if (rerun) {
            rp->rerun = true;
        }
        rp->run = RECORD_STATE_STOP;
        rdx_record_process();
    }
    return RDX_OK;
}

rdx_err_t rdx_record_domain_prepare_upload_fallback(
    rdx_record_stop_reason_t reason,
    rdx_record_domain_connection_query_t connection_query,
    u16 *connection_handle,
    rdx_record_domain_state_t *trigger)
{
    RecordStatus *rp;

    if (reason != RDX_RECORD_STOP_UPLOAD_FALLBACK || !connection_query ||
        !connection_handle || !trigger ||
        !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    if (rp->run != RECORD_STATE_START && rp->run != RECORD_STATE_RESUME) {
        return RDX_ERR_BUSY;
    }
    *connection_handle = connection_query();
    rp->run = RECORD_STATE_STOP;
    rdx_record_domain_copy_state(rp, trigger);
    return RDX_OK;
}

rdx_err_t rdx_record_domain_prepare_connected_toggle(
    rdx_record_scene_t scene,
    rdx_record_domain_toggle_result_t *out)
{
    RecordStatus *rp;
    u8 legacy_scene;
    rdx_err_t ret = rdx_record_domain_scene_to_legacy(scene, &legacy_scene);

    if (ret != RDX_OK || !out || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    out->start_upload_timer = false;
    out->process_post_result = RDX_OK;
    if (rp->run == RECORD_STATE_STOP) {
        out->trigger.run = RECORD_STATE_START;
        out->trigger.format = RECORD_FORMATE_OPUS_16K_STERO;
        out->trigger.scene = legacy_scene;
        out->trigger.mode = rp->mode;
        out->trigger.original_mode = rp->orig_mode;
        out->start_upload_timer = true;
    } else if (rp->orig_mode == RECORD_MODE_OFFLINE) {
        rp->run = RECORD_STATE_STOP;
        out->process_post_result = rdx_record_domain_post_process();
        out->trigger.run = RECORD_STATE_STOP;
        out->trigger.format = RECORD_FORMATE_OPUS_16K_STERO;
        out->trigger.scene = legacy_scene;
        out->trigger.mode = rp->mode;
        out->trigger.original_mode = rp->orig_mode;
    } else {
        rdx_record_domain_copy_state(rp, &out->trigger);
        out->trigger.run = RECORD_STATE_STOP;
    }
    return RDX_OK;
}

rdx_err_t rdx_record_domain_toggle_post(rdx_record_scene_t scene)
{
    RecordStatus *rp;
    u8 legacy_scene;
    rdx_err_t ret = rdx_record_domain_scene_to_legacy(scene, &legacy_scene);

    if (ret != RDX_OK || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    if (rp->run == RECORD_STATE_STOP) {
        rp->run = RECORD_STATE_START;
        rp->formate = RECORD_FORMATE_OPUS_16K_STERO;
        rp->scene = legacy_scene;
    } else {
        rp->run = RECORD_STATE_STOP;
    }
    return rdx_record_domain_post_process();
}

rdx_err_t rdx_record_domain_mode_active_check(
    bool *mode_changed,
    rdx_record_domain_state_t *state)
{
    RecordStatus *rp;

    if (!mode_changed || !state || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    *mode_changed = false;
    if (rp->run == RECORD_STATE_STOP) {
        rp->scene = RECORD_SCENE_CALL;
        rp->orig_scene = rp->scene;
        *mode_changed = true;
    }
    rdx_record_domain_copy_state(rp, state);
    return RDX_OK;
}

static rdx_err_t rdx_record_domain_prepare_switch_internal(
    u8 legacy_scene,
    bool ble_connected,
    bool *trigger_required,
    rdx_record_domain_state_t *trigger)
{
    RecordStatus *rp;

    if (!trigger_required || !trigger || !(rp = rdx_record_get_status())) {
        return RDX_ERR_INVAL;
    }
    *trigger_required = false;
    if (rp->run == RECORD_STATE_STOP) {
        return RDX_OK;
    }

    rp->noshow = 1;
    if (!ble_connected) {
        rp->run = RECORD_STATE_STOP;
        rdx_record_process();
    }
    rp->is_switch = 1;
    rp->switch_orig_scene = legacy_scene;

    rdx_record_domain_copy_state(rp, trigger);
    trigger->run = RECORD_STATE_STOP;
    trigger->scene = legacy_scene;
    *trigger_required = true;
    return RDX_OK;
}

rdx_err_t rdx_record_domain_prepare_switch(
    rdx_record_scene_t original_scene,
    bool ble_connected,
    bool *trigger_required,
    rdx_record_domain_state_t *trigger)
{
    u8 legacy_scene;

    if (rdx_record_domain_scene_to_legacy(original_scene, &legacy_scene) !=
        RDX_OK) {
        return RDX_ERR_INVAL;
    }
    return rdx_record_domain_prepare_switch_internal(
        legacy_scene, ble_connected, trigger_required, trigger);
}

rdx_err_t rdx_record_domain_prepare_switch_compat(
    u8 original_scene,
    bool ble_connected,
    bool *trigger_required,
    rdx_record_domain_state_t *trigger)
{
    return rdx_record_domain_prepare_switch_internal(
        original_scene, ble_connected, trigger_required, trigger);
}
