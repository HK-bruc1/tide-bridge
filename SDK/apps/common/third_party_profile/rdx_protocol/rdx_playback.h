#ifndef __RDX_PLAYBACK_H__
#define __RDX_PLAYBACK_H__

#include "rdx_playback_config.h"
#include "typedef.h"

#if TCFG_RDX_LOCAL_PLAYBACK_ENABLE

#include <stdbool.h>

typedef enum {
    PB_STATE_UNREADY = 0,
    PB_STATE_STOPPED,
    PB_STATE_STARTING,
    PB_STATE_PLAYING,
    PB_STATE_SWITCHING,
    PB_STATE_DRAINING,
} pb_state_t;

typedef enum {
    PB_RESULT_OK = 0,
    PB_RESULT_NO_FILE = -1,
    PB_RESULT_NOT_READY = -2,
    PB_RESULT_BUSY = -3,
    PB_RESULT_IO_ERROR = -4,
    PB_RESULT_PLAYER_ERROR = -5,
    PB_RESULT_INVALID_STATE = -6,
} pb_result_t;

typedef enum {
    PB_PLAYLIST_CONTENT_CHANGED = 0,
    PB_PLAYLIST_STORAGE_UNAVAILABLE,
    PB_PLAYLIST_FORMATTING,
} pb_playlist_invalidate_reason_t;

typedef enum {
    PB_INTENT_NONE = 0,
    PB_INTENT_STOP,
    PB_INTENT_SWITCH,
} pb_intent_t;

typedef struct {
    u32 selected_sn;
    u32 current_sn;
    u32 pending_sn;
    u16 total_count;
    pb_state_t state;
    pb_intent_t intent;
    u8 playlist_dirty;
    int last_error;
    u32 seek_base_frame;
    u32 duration_frames;
} rdx_playback_t;

typedef struct {
    u32 selected_sn;
    u32 current_sn;
    u32 pending_sn;
    u16 total_count;
    pb_state_t state;
    int last_error;
    u32 position_ms;
    u32 duration_ms;
} pb_public_info_t;

void rdx_playback_init(void);
bool rdx_playback_can_start(void);
int rdx_playback_prev(void);
int rdx_playback_next(void);
void rdx_playback_ff(void);
void rdx_playback_fr(void);
void rdx_playback_stop(void);
void rdx_playback_invalidate_playlist(pb_playlist_invalidate_reason_t reason);
void rdx_playback_on_file_deleted(u32 sn);
void rdx_playback_get_info(pb_public_info_t *info);

#endif

#endif // __RDX_PLAYBACK_H__
