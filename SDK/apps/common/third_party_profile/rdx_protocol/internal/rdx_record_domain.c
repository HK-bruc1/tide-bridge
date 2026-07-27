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
