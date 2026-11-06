#ifndef RDX_VOLUME_H
#define RDX_VOLUME_H

#include "typedef.h"
#include "rdx_ble_session.h"

#define RDX_VOLUME_CUSTOM_CMD "vol"

void rdx_volume_init(void);
void rdx_volume_handle_custom(const char *value);
void rdx_volume_local_music_changed(s16 previous, s16 current);
void rdx_volume_cancel_for_token(const rdx_ble_async_token_t *token);

/* app_core only: trusted raw playback volume range from the playback node. */
int rdx_volume_playback_max_get(u16 *max_level);

#endif
