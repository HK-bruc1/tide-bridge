#ifndef __RDX_RECORD_SERVICE_TYPES_H__
#define __RDX_RECORD_SERVICE_TYPES_H__

typedef enum {
    RDX_RECORD_ACTIVITY_IDLE = 0,
    RDX_RECORD_ACTIVITY_ACTIVE,
    RDX_RECORD_ACTIVITY_PAUSED,
} rdx_record_activity_t;

typedef enum {
    RDX_RECORD_SCENE_CHAT = 0,
    RDX_RECORD_SCENE_CALL,
} rdx_record_scene_t;

typedef enum {
    RDX_RECORD_PATH_OFFLINE = 0,
    RDX_RECORD_PATH_ONLINE,
} rdx_record_path_t;

#endif
