#ifndef RDX_HOGP_INPUT_H
#define RDX_HOGP_INPUT_H
#include "key_driver.h"

enum rdx_hogp_input_route {
    RDX_HOGP_INPUT_DISCARD = 0,
    RDX_HOGP_INPUT_OFFLINE,
    RDX_HOGP_INPUT_HID,
};

/* Read-only snapshot for diagnostics; counters are cumulative since boot. */
typedef struct {
    u32 epoch;
    u32 overflows;
    u8 queued;
    u8 high_water;
    u8 route;
    u8 admitted;
    u8 cleanup_pending;
    u8 worker_available;
    u8 periodic_running;
} rdx_hogp_input_stats_t;

void rdx_hogp_input_stats_get(rdx_hogp_input_stats_t *stats);

/* All business/report work runs on app_core. The scan APIs only copy data. */
u8 rdx_hogp_input_current_locked(u32 epoch);
void rdx_hogp_input_init(void);
/* 仅在 app_core 调用；复用已有产品服务处理通知丢失后的恢复。 */
void rdx_hogp_input_poll(void);
void rdx_hogp_input_wake(void);
void rdx_hogp_input_invalidate(void);
void rdx_hogp_input_scan(u8 type, u8 previous, u8 current, u8 filtered);
u32 rdx_hogp_input_feedback_epoch(u8 value);
u8 rdx_hogp_input_feedback_epoch_valid(u32 epoch);
u32 rdx_hogp_input_gesture_epoch(u8 value);
u8 rdx_hogp_input_epoch_valid(u32 epoch);
void rdx_hogp_input_gesture(const struct key_event *key, u32 epoch);
#endif
