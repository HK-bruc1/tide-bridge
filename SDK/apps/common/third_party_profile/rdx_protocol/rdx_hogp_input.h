#ifndef RDX_HOGP_INPUT_H
#define RDX_HOGP_INPUT_H
#include "key_driver.h"
#include "rdx_hogp_key_action.h"

enum rdx_hogp_input_route {
    RDX_HOGP_INPUT_DISCARD = 0,
    RDX_HOGP_INPUT_OFFLINE,
    RDX_HOGP_INPUT_HID,
};

enum rdx_local_key_mode {
    RDX_LOCAL_KEY_BLOCKED = 0,
    RDX_LOCAL_KEY_NORMAL,
    RDX_LOCAL_KEY_DUT,
};

/* 产品私有事件封装，不改变 SDK key_event 或 VM ABI。 */
typedef struct {
    u32 id, epoch, map, down_ms;
    rdx_hogp_key_action_keyboard_t keymap;
    u8 physical_key, usage, mode, hid_admitted;
} rdx_hogp_input_cycle_t;

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
    u32 local_epoch;
    u32 local_overflows;
    u8 local_queued;
    u8 local_admitted;
    u8 local_cleanup_pending;
} rdx_hogp_input_stats_t;

void rdx_hogp_input_stats_get(rdx_hogp_input_stats_t *stats);

/* All business/report work runs on app_core. The scan APIs only copy data. */
u8 rdx_hogp_input_current_locked(u32 epoch);
void rdx_hogp_input_init(void);
/* poll 仅在 app_core 调用；needs_poll 只读状态，供恢复桥接和休眠准入查询。 */
void rdx_hogp_input_poll(void);
int rdx_hogp_input_needs_poll(void);
void rdx_hogp_input_wake(void);
/* 仅使 HID 失效：连接、配键或上报失败不能取消本地归属。 */
void rdx_hogp_input_invalidate(void);
/* 系统保护或输入来源不可信时，取消两个通道。 */
void rdx_hogp_input_cancel(void);
void rdx_hogp_input_scan(u8 type, u8 previous, u8 current, u8 filtered);
u8 rdx_hogp_input_cycle_get(u8 value, rdx_hogp_input_cycle_t *cycle);
u8 rdx_hogp_input_cycle_valid(const rdx_hogp_input_cycle_t *cycle);
u8 rdx_hogp_input_dut_pending(const rdx_hogp_input_cycle_t *cycle);
void rdx_hogp_input_gesture(const struct key_event *key, const rdx_hogp_input_cycle_t *cycle);
#endif
