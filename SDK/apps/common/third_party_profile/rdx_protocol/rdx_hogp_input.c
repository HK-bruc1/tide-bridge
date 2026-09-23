#include "app_config.h"
#include "system/includes.h"
#include "rdx_hogp_input.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_hogp_key_action.h"
#include "rdx_app.h"

#define INPUT_FIFO_SIZE 24

/* Private metadata never changes the SDK key_event ABI. */
struct input_event {
    u32 epoch, map;
    rdx_hogp_token_t token;
    u8 previous, current, route, admitted, gesture;
    struct key_event key;
};
static struct input_event input_fifo[INPUT_FIFO_SIZE];
static u8 input_head, input_count;
static u32 input_epoch = 1, scan_seq, scan_epoch, scan_baseline;
static u32 cycle_epoch[5];
static u8 cycle_route[5];
static u8 stable_key = NO_KEY, scan_filtered;
static u8 published_route, admission, cleanup_pending = 1;
static u32 published_map;
static rdx_hogp_token_t published_token;
static u16 input_timer;
static u32 overflow_count, reported_overflows;
static u8 input_high_water;

static u8 product_key(u8 value)
{
    /* 五个按键均上报 HID 按下和松开事件。KEY5 的录音手势仍由
     * 按键适配层和 rdx_app_key5_remap() 独立处理。 */
    return value >= KEY_IO_NUM0 && value <= KEY_IO_NUM4;
}

/* Caller holds the short IRQ critical section. No ATT/VM/business calls. */
static void invalidate_locked(void)
{
    ++input_epoch;
    if (!input_epoch) ++input_epoch;
    admission = 0;
    scan_baseline = scan_seq;
    input_head = input_count = 0;
    cleanup_pending = 1;
}

u8 rdx_hogp_input_current_locked(u32 epoch)
{
    return epoch == input_epoch && !cleanup_pending;
}

void rdx_hogp_input_invalidate(void)
{
    local_irq_disable();
    invalidate_locked();
    local_irq_enable();
}

static void enqueue_locked(const struct input_event *event)
{
    if (!input_timer) return; /* Failure to create the worker fails closed. */
    if (input_count == INPUT_FIFO_SIZE) {
        ++overflow_count;
        invalidate_locked();
        return;
    }
    input_fifo[(input_head + input_count) % INPUT_FIFO_SIZE] = *event;
    ++input_count;
    if (input_count > input_high_water) input_high_water = input_count;
}

void rdx_hogp_input_scan(u8 type, u8 previous, u8 current, u8 filtered)
{
    struct input_event event = {0};
    if (type != KEY_DRIVER_TYPE_IO) return;
    local_irq_disable();
    ++scan_seq;
    scan_epoch = input_epoch;
    stable_key = current;
    scan_filtered = filtered;
    if (filtered && admission) invalidate_locked();
    if (previous != current) {
        event.epoch = input_epoch;
        event.map = published_map;
        event.token = published_token;
        event.previous = previous;
        event.current = current;
        event.route = filtered ? 0 : published_route;
        event.admitted = admission && !filtered;
        if (product_key(current)) {
            cycle_epoch[current - KEY_IO_NUM0] = input_epoch;
            cycle_route[current - KEY_IO_NUM0] = event.admitted ? event.route : 0;
        }
        if (product_key(previous) || product_key(current)) enqueue_locked(&event);
    }
    local_irq_enable();
}

/* Feedback tracks the same physical cycle in every route, without granting
 * offline business admission or HID report permission. */
u32 rdx_hogp_input_feedback_epoch(u8 value)
{
    u32 epoch = 0;
    if (!product_key(value)) return 0;
    local_irq_disable();
    if (!scan_filtered && !cleanup_pending &&
        cycle_epoch[value - KEY_IO_NUM0] == input_epoch) epoch = input_epoch;
    local_irq_enable();
    return epoch;
}

u8 rdx_hogp_input_feedback_epoch_valid(u32 epoch)
{
    u8 valid;
    local_irq_disable();
    valid = epoch && epoch == input_epoch && !cleanup_pending && !scan_filtered;
    local_irq_enable();
    return valid;
}

u32 rdx_hogp_input_gesture_epoch(u8 value)
{
    u32 epoch = 0;
    if (!product_key(value)) return 0;
    local_irq_disable();
    if (cycle_route[value - KEY_IO_NUM0] == RDX_HOGP_INPUT_OFFLINE &&
        cycle_epoch[value - KEY_IO_NUM0] == input_epoch) epoch = input_epoch;
    local_irq_enable();
    return epoch;
}

u8 rdx_hogp_input_epoch_valid(u32 epoch)
{
    u8 valid;
    local_irq_disable();
    valid = epoch && epoch == input_epoch && published_route == RDX_HOGP_INPUT_OFFLINE;
    local_irq_enable();
    return valid;
}

void rdx_hogp_input_gesture(const struct key_event *key, u32 epoch)
{
    struct input_event event = {0};
    event.gesture = 1;
    event.key = *key;
    event.epoch = epoch;
    event.route = RDX_HOGP_INPUT_OFFLINE;
    local_irq_disable();
    if (epoch && epoch == input_epoch) enqueue_locked(&event);
    local_irq_enable();
}

static u8 tokens_equal(const rdx_hogp_token_t *a, const rdx_hogp_token_t *b)
{
    return a->hid_epoch == b->hid_epoch && a->slot_index == b->slot_index &&
           a->slot_generation == b->slot_generation;
}

static void input_service(void *priv)
{
    struct input_event event;
    rdx_hogp_token_t token = {0};
    u8 route, cleanup, blocked;
    u32 map, overflows;
    (void)priv;
    route = rdx_app_hogp_input_route();
    if (route == RDX_HOGP_INPUT_HID && !rdx_hogp_token_capture(&token)) route = 0;
    map = rdx_hogp_key_action_generation();
    local_irq_disable();
    if (route != published_route || map != published_map ||
        (route == RDX_HOGP_INPUT_HID && !tokens_equal(&token, &published_token))) {
        invalidate_locked();
        published_route = route;
        published_map = map;
        published_token = token;
    }
    overflows = overflow_count;
    cleanup = cleanup_pending;
    cleanup_pending = 0;
    local_irq_enable();
    /* Log from app_core only, never from the scan IRQ critical section. */
    if (overflows != reported_overflows &&
        (!reported_overflows || overflows - reported_overflows >= 128)) {
        reported_overflows = overflows;
        y_printf("[HOGP_INPUT] FIFO overflow total=%u; release and rearm\n", overflows);
    }
    if (cleanup) rdx_hogp_key_action_cancel();
    else rdx_hogp_key_action_service();
    /* Queue admission is not report completion. Keep edges in the FIFO while
     * btstack submits; normal asynchronous latency must not discard clicks. */
    if (rdx_hogp_key_action_busy()) return;
    blocked = rdx_hogp_key_action_blocked();
    if (blocked) {
        /* An actual queue/ATT failure requires recovery; pending work above
         * merely pauses the FIFO. Do not retry twice and hide this failure. */
        rdx_hogp_input_invalidate();
        return;
    }
    local_irq_disable();
    if (!cleanup_pending && !blocked && route && !scan_filtered &&
        scan_epoch == input_epoch && scan_seq != scan_baseline && stable_key == NO_KEY) {
        admission = 1;
    }
    local_irq_enable();

    /* Bounded work per tick. New scans can append while app_core sends. */
    for (int i = 0; i < INPUT_FIFO_SIZE; ++i) {
        local_irq_disable();
        if (!input_count) {
            local_irq_enable();
            break;
        }
        event = input_fifo[input_head];
        cleanup = event.epoch != input_epoch || cleanup_pending;
        local_irq_enable();
        if (cleanup) {
            local_irq_disable();
            if (input_count && event.epoch == input_fifo[input_head].epoch) {
                input_head = (input_head + 1) % INPUT_FIFO_SIZE;
                --input_count;
            }
            local_irq_enable();
            continue;
        }
        if (rdx_app_hogp_input_route() != route) {
            rdx_hogp_input_invalidate();
            rdx_hogp_key_action_cancel();
            break;
        }
        if (event.gesture) {
            if (route == RDX_HOGP_INPUT_OFFLINE && rdx_hogp_input_epoch_valid(event.epoch)) {
                /* Unpack the private envelope explicitly at the business entry. */
                rdx_app_key_msg_handler((int *)&event.key);
            }
        } else {
            /* Release is cleanup: it is independent of new-Down admission. */
            if (product_key(event.previous)) {
                rdx_hogp_key_action_release(event.previous - KEY_IO_NUM0);
            }
            if (rdx_hogp_key_action_busy()) break; /* Retry this edge after Up completes. */
            if (rdx_hogp_key_action_blocked()) {
                rdx_hogp_input_invalidate();
                break;
            }
            if (event.admitted && event.route == RDX_HOGP_INPUT_HID && route == RDX_HOGP_INPUT_HID &&
                event.map == map && tokens_equal(&event.token, &token) &&
                product_key(event.current)) {
                rdx_hogp_key_action_press(event.current - KEY_IO_NUM0, event.epoch);
            }
        }
        local_irq_disable();
        /* An ISR overflow may already have cleared the old FIFO. */
        if (input_count && event.epoch == input_epoch) {
            input_head = (input_head + 1) % INPUT_FIFO_SIZE;
            --input_count;
        }
        local_irq_enable();
        if (rdx_hogp_key_action_busy()) break;
    }
}

void rdx_hogp_input_stats_get(rdx_hogp_input_stats_t *stats)
{
    if (!stats) return;
    local_irq_disable();
    stats->epoch = input_epoch;
    stats->overflows = overflow_count;
    stats->queued = input_count;
    stats->high_water = input_high_water;
    stats->route = published_route;
    stats->admitted = admission;
    stats->cleanup_pending = cleanup_pending;
    stats->worker_available = input_timer != 0;
    local_irq_enable();
}

void rdx_hogp_input_init(void)
{
    rdx_hogp_input_invalidate();
    /* Persistent recovery source: do not stop while idle without providing
     * an independent wakeup for ready changes and failed release retries.
     * Measure idle current and app_core load on target before tuning 10 ms. */
    if (!input_timer) input_timer = sys_timer_add(NULL, input_service, 10);
    if (!input_timer) y_printf("[HOGP_INPUT] worker unavailable; input disabled\n");
}
