#ifndef __RDX_PLAYBACK_H__
#define __RDX_PLAYBACK_H__

#include <stdbool.h>

#define PB_STATUS_STOP                              0
#define PB_STATUS_PLAYING                           1
#define PB_STATUS_PAUSE                             2

typedef enum {
    PB_INTENT_NONE = 0,
    PB_INTENT_STOP,
    PB_INTENT_SWITCH,
} pb_intent_t;

typedef struct {
    u32  cur_sn;
    u32  total_count;
    u8   status;
    pb_intent_t intent;
} rdx_playback_t;

void rdx_playback_init(void);
bool rdx_playback_can_start(void);
void rdx_playback_prev(void);
void rdx_playback_next(void);
void rdx_playback_ff(void);
void rdx_playback_fr(void);
void rdx_playback_stop(void);

#endif // __RDX_PLAYBACK_H__
