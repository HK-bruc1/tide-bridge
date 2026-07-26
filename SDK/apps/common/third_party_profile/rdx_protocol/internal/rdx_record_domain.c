#include "rdx_record_domain.h"
#include "rdx_record.h"

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

bool rdx_record_domain_is_offline_active(void)
{
    RecordStatus *rp = rdx_record_get_status();
    return rp && rp->orig_mode == RECORD_MODE_OFFLINE &&
           (rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME);
}
