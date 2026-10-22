#include "app_config.h"
#include "system/includes.h"
#include "rdx_hogp_input.h"
#include "rdx_hogp_keyboard.h"
#include "rdx_hogp_key_action.h"
#include "rdx_app.h"
#include "rdx_ble_entry.h"

#define INPUT_FIFO_SIZE 24
#define LOCAL_FIFO_SIZE 24

/* HID 边沿与本地手势各自使用有界存储，独立推进。 */
struct input_event {
    u32 epoch, map;
    rdx_hogp_token_t token;
    rdx_hogp_key_action_keyboard_t keymap;
    u8 previous, current, route, admitted;
};
struct local_event {
    struct key_event key;
    rdx_hogp_input_cycle_t cycle;
};
static struct input_event input_fifo[INPUT_FIFO_SIZE];
static u8 input_head, input_count;
static u32 input_epoch = 1, scan_seq, scan_epoch, scan_baseline;
static u8 stable_key = NO_KEY, scan_filtered;
static volatile u8 published_route;
static u8 admission, cleanup_pending = 1;
static volatile u32 published_map;
static rdx_hogp_token_t published_token;
static volatile u16 input_timer;
static volatile u8 input_initialized, input_pending, input_posted, input_servicing;
static u32 overflow_count, reported_overflows;
static u8 input_high_water;

static struct local_event local_fifo[LOCAL_FIFO_SIZE];
static rdx_hogp_input_cycle_t scan_cycle, local_owner;
static u32 local_epoch = 1, cycle_sequence, local_baseline, local_overflows;
static u8 local_head, local_count, local_admission, local_cleanup_pending = 1;
static u8 local_mode, local_down, local_owner_pending;

static void input_service(void *priv);
static void input_event(void *priv)
{
    local_irq_disable();
    input_posted = 0;
    local_irq_enable();
    input_service(NULL);
}

/* 队列满导致投递失败时，由已有产品服务兜底重试；中断中不执行业务。 */
static void input_wake_locked(void)
{
    int message[] = { (int)input_event, 1, 0 };
    input_pending = 1;
    if (!input_initialized || input_timer || input_posted || input_servicing) return;
    input_posted = 1;
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, message)) input_posted = 0;
}

void rdx_hogp_input_wake(void)
{
    local_irq_disable();
    input_wake_locked();
    local_irq_enable();
}

static u8 product_key(u8 value)
{
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
    input_wake_locked();
}

static void local_invalidate_locked(void)
{
    ++local_epoch;
    if (!local_epoch) ++local_epoch;
    local_admission = local_down = 0;
    local_baseline = scan_seq;
    local_head = local_count = 0;
    local_cleanup_pending = 1;
    /* local_owner 由 app_core 持有，队列丢失时仍保留，直至完成取消。 */
    input_wake_locked();
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

void rdx_hogp_input_cancel(void)
{
    local_irq_disable();
    invalidate_locked();
    local_invalidate_locked();
    local_irq_enable();
}

static void enqueue_locked(const struct input_event *event)
{
    if (!input_initialized) return;
    if (input_count == INPUT_FIFO_SIZE) {
        ++overflow_count;
        /* 只丢弃 HID 积压，本地手势仍保留对应的 Up。 */
        invalidate_locked();
        return;
    }
    input_fifo[(input_head + input_count) % INPUT_FIFO_SIZE] = *event;
    ++input_count;
    if (input_count > input_high_water) input_high_water = input_count;
    input_wake_locked();
}

void rdx_hogp_input_scan(u8 type, u8 previous, u8 current, u8 filtered)
{
    struct input_event event = {0};
    if (type != KEY_DRIVER_TYPE_IO) return;
    if (previous != current && (product_key(previous) || product_key(current))) {
        if (!filtered || current == NO_KEY)
            rdx_ble_entry_key(product_key(current) && !filtered);
    }
    local_irq_disable();
    ++scan_seq;
    scan_epoch = input_epoch;
    stable_key = current;
    if (filtered && !scan_filtered) {
        invalidate_locked();
        local_invalidate_locked();
    }
    scan_filtered = filtered;
    if (previous != current) {
        /* SDK 单键驱动无法判定重叠按键的归属，不能将高优先级按键
         * 替换当前按键误判为一次新的业务按下。 */
        if (product_key(previous) && current != NO_KEY) {
            invalidate_locked();
            local_invalidate_locked();
        }
        local_down = 0;
        event.epoch = input_epoch;
        event.token = published_token;
        event.previous = previous;
        event.current = current;
        event.route = filtered ? 0 : published_route;
        event.admitted = admission && !filtered;
        event.map = rdx_hogp_key_action_generation();
        if (product_key(current)) {
            memset(&scan_cycle, 0, sizeof(scan_cycle));
            ++cycle_sequence;
            if (!cycle_sequence) ++cycle_sequence;
            scan_cycle.id = cycle_sequence;
            scan_cycle.epoch = local_epoch;
            scan_cycle.physical_key = current;
            scan_cycle.map = rdx_hogp_key_action_snapshot_locked(
                current - KEY_IO_NUM0, &scan_cycle.keymap);
            scan_cycle.usage = rdx_hogp_key_action_plain_usage(&scan_cycle.keymap);
            scan_cycle.mode = local_mode;
            scan_cycle.hid_admitted = event.admitted &&
                event.route == RDX_HOGP_INPUT_HID && scan_cycle.map == published_map;
            event.map = scan_cycle.map;
            event.keymap = scan_cycle.keymap;
            local_down = local_admission && !filtered &&
                (local_mode == RDX_LOCAL_KEY_NORMAL ||
                 (local_mode == RDX_LOCAL_KEY_DUT && current == KEY_IO_NUM4));
            if (!local_down) scan_cycle.id = 0;
        }
        if (product_key(previous) || product_key(current)) enqueue_locked(&event);
    }
    /* 新的稳定松键采样可重新开放输入接纳，无需常驻周期服务。 */
    if (!filtered && current == NO_KEY &&
        ((!admission && !cleanup_pending && published_route && scan_seq != scan_baseline) ||
         (!local_admission && !local_cleanup_pending && local_mode && scan_seq != local_baseline))) {
        input_wake_locked();
    }
    local_irq_enable();
}

u8 rdx_hogp_input_cycle_valid(const rdx_hogp_input_cycle_t *cycle)
{
    u8 valid;
    if (!cycle) return 0;
    local_irq_disable();
    valid = cycle->id && cycle->epoch == local_epoch &&
            !local_cleanup_pending && !scan_filtered;
    local_irq_enable();
    return valid;
}

u8 rdx_hogp_input_cycle_get(u8 value, rdx_hogp_input_cycle_t *cycle)
{
    u8 valid;
    if (!cycle || !product_key(value)) return 0;
    local_irq_disable();
    valid = scan_cycle.id && scan_cycle.physical_key == value &&
            scan_cycle.epoch == local_epoch && !local_cleanup_pending && !scan_filtered;
    if (valid) *cycle = scan_cycle;
    local_irq_enable();
    return valid;
}

void rdx_hogp_input_gesture(const struct key_event *key, const rdx_hogp_input_cycle_t *cycle)
{
    if (!key || !cycle || key->value != cycle->physical_key) return;
    local_irq_disable();
    if (input_initialized && cycle->id && cycle->epoch == local_epoch &&
        !local_cleanup_pending && !scan_filtered) {
        if (local_count == LOCAL_FIFO_SIZE) {
            ++local_overflows;
            local_invalidate_locked();
        } else {
            struct local_event *event = &local_fifo[(local_head + local_count) % LOCAL_FIFO_SIZE];
            event->key = *key;
            event->cycle = *cycle;
            ++local_count;
            input_wake_locked();
        }
    }
    local_irq_enable();
}

static u8 local_cycle_down(const rdx_hogp_input_cycle_t *cycle)
{
    u8 down;
    local_irq_disable();
    down = cycle->id && cycle->id == scan_cycle.id && cycle->epoch == local_epoch &&
           local_down && !local_cleanup_pending;
    local_irq_enable();
    return down;
}

/* 归属与取消票据独立于两个 FIFO；停止失败时由已有产品恢复服务重试，
 * 即使无法创建定时器也不能丢失收尾责任。 */
static u8 local_owner_release(void)
{
    local_owner_pending = 1;
    if (!rdx_app_local_key_cancel(&local_owner)) {
        local_irq_disable();
        if (!local_cleanup_pending) local_invalidate_locked();
        input_wake_locked();
        local_irq_enable();
        return 0;
    }
    memset(&local_owner, 0, sizeof(local_owner));
    local_owner_pending = 0;
    return 1;
}

static void local_service(void)
{
    struct local_event event;
    u8 mode = rdx_app_local_key_mode();
    local_irq_disable();
    if (mode != local_mode) {
        local_invalidate_locked();
        local_mode = mode;
    }
    local_irq_enable();

    if (local_owner.id && (local_owner_pending || !local_cycle_down(&local_owner)) &&
        !local_owner_release()) return;
    local_irq_disable();
    local_cleanup_pending = 0;
    if (!scan_filtered && local_mode && scan_seq != local_baseline && stable_key == NO_KEY) {
        local_admission = 1;
    }
    local_irq_enable();

    for (u8 i = 0; i < LOCAL_FIFO_SIZE; ++i) {
        /* 回调执行期间，扫描中断可能释放或替换当前归属。 */
        if (local_owner.id && !local_cycle_down(&local_owner) &&
            !local_owner_release()) return;
        local_irq_disable();
        if (!local_count || local_cleanup_pending) {
            local_irq_enable();
            break;
        }
        event = local_fifo[local_head];
        local_head = (local_head + 1) % LOCAL_FIFO_SIZE;
        --local_count;
        local_irq_enable();
        if (!rdx_hogp_input_cycle_valid(&event.cycle)) continue;
        if (rdx_app_local_key_mode() != event.cycle.mode) {
            local_irq_disable();
            local_invalidate_locked();
            local_irq_enable();
            break;
        }
        if (event.key.event == KEY_ACTION_UP) {
            /* 实体松开已独立于手势处理锁存。 */
            continue;
        }
        if (event.key.event == KEY_ACTION_LONG || event.key.event == KEY_ACTION_HOLD ||
            (event.key.event >= KEY_ACTION_HOLD_1SEC && event.key.event <= KEY_ACTION_HOLD_10SEC)) {
            if (!local_cycle_down(&event.cycle)) continue;
            if (event.key.event == KEY_ACTION_LONG) {
                if (local_owner.id == event.cycle.id) continue;
                /* 循环顶部检查后，中断仍可能替换扫描周期。 */
                if (local_owner.id && !local_owner_release()) return;
                if (!local_cycle_down(&event.cycle)) continue;
                local_owner = event.cycle;
            } else if (local_owner.id != event.cycle.id) {
                continue;
            }
        }
        rdx_app_local_key_gesture(&event.key, &event.cycle);
    }
}

static u8 tokens_equal(const rdx_hogp_token_t *a, const rdx_hogp_token_t *b)
{
    return a->hid_epoch == b->hid_epoch && a->slot_index == b->slot_index &&
           a->slot_generation == b->slot_generation;
}

static void input_service_run(void *priv)
{
    struct input_event event;
    rdx_hogp_token_t token = {0};
    u8 route, cleanup, blocked;
    u32 map, overflows;
    (void)priv;
    /* HID 忙碌或出错退出时，也不能跳过本地松开与取消处理。 */
    local_service();
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
    if (overflows != reported_overflows &&
        (!reported_overflows || overflows - reported_overflows >= 128)) {
        reported_overflows = overflows;
        y_printf("[HOGP_INPUT] FIFO overflow total=%u; release and rearm\n", overflows);
    }
    if (cleanup) rdx_hogp_key_action_cancel();
    else rdx_hogp_key_action_service();
    /* ATT 待发送仅暂停此 FIFO，不阻塞上面的本地通道。 */
    if (rdx_hogp_key_action_busy()) return;
    blocked = rdx_hogp_key_action_blocked();
    if (blocked) {
        rdx_hogp_input_invalidate();
        return;
    }
    local_irq_disable();
    if (!cleanup_pending && route && !scan_filtered &&
        scan_epoch == input_epoch && scan_seq != scan_baseline && stable_key == NO_KEY) {
        admission = 1;
    }
    local_irq_enable();

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
        if (product_key(event.previous)) {
            rdx_hogp_key_action_release(event.previous - KEY_IO_NUM0);
        }
        if (rdx_hogp_key_action_busy()) break;
        if (rdx_hogp_key_action_blocked()) {
            rdx_hogp_input_invalidate();
            break;
        }
        if (event.admitted && event.route == RDX_HOGP_INPUT_HID && route == RDX_HOGP_INPUT_HID &&
            event.map == map && tokens_equal(&event.token, &token) && product_key(event.current)) {
            rdx_hogp_key_action_press_snapshot(event.current - KEY_IO_NUM0, event.epoch, &event.keymap);
        }
        local_irq_disable();
        if (input_count && event.epoch == input_epoch) {
            input_head = (input_head + 1) % INPUT_FIFO_SIZE;
            --input_count;
        }
        local_irq_enable();
        if (rdx_hogp_key_action_busy()) break;
    }
}

static void input_service(void *priv)
{
    if (!input_initialized) return;
    if (rdx_hogp_keyboard_is_ready()) {
        if (!input_timer) input_timer = sys_timer_add(NULL, input_service, 10);
    } else if (input_timer) {
        sys_timer_del(input_timer);
        input_timer = 0;
    }
    local_irq_disable();
    input_pending = 0;
    input_servicing = 1;
    local_irq_enable();
    input_service_run(priv);
    local_irq_disable();
    input_servicing = 0;
    if (local_count || local_cleanup_pending || local_owner_pending) input_pending = 1;
    /* 不连续自投递重试，避免忙循环；由外部事件或已有产品服务恢复。 */
    local_irq_enable();
}

static int input_changed(void)
{
    return input_pending || rdx_app_hogp_input_route() != published_route ||
           rdx_app_local_key_mode() != local_mode ||
           rdx_hogp_key_action_generation() != published_map ||
           !!input_timer != !!rdx_hogp_keyboard_is_ready();
}

void rdx_hogp_input_poll(void)
{
    if (input_initialized && input_changed()) input_service(NULL);
}

int rdx_hogp_input_needs_poll(void)
{
    /* 只读查询供优先级 0 恢复入口使用，业务执行仍限于 app_core。 */
    return input_initialized && !input_posted && !input_servicing && input_changed();
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
    stats->worker_available = input_initialized;
    stats->periodic_running = input_timer != 0;
    stats->local_epoch = local_epoch;
    stats->local_overflows = local_overflows;
    stats->local_queued = local_count;
    stats->local_admitted = local_admission;
    stats->local_cleanup_pending = local_cleanup_pending || local_owner_pending;
    local_irq_enable();
}

void rdx_hogp_input_init(void)
{
    input_initialized = 1;
    rdx_hogp_input_cancel();
}
