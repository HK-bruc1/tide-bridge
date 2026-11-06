#include "system/includes.h"
#include "rdx_ble_server.h"
#include "rdx_ble_entry.h"

#define ENTRY_WINDOW_MS 120000u
#define ENTRY_RETRY_MS 100u
#define ENTRY_RETRY_MAX 3

enum {
    ENTRY_CAPACITY_AVAILABLE,
    ENTRY_CAPACITY_COMPOSITE_FULL,
    ENTRY_CAPACITY_RELEASE_WAIT,
};

static struct {
    volatile u32 epoch;
    volatile u8 running;
    volatile u32 topology;
    volatile u32 revision;
    volatile u32 refresh, wake_time;
    volatile u32 release, release_time, release_epoch, rearm;
    volatile u8 requested, dirty, queued;
    volatile u8 active, key_down, wake;
    volatile u8 capacity;
    u32 consumed_topology, consumed_refresh, consumed_release, consumed_rearm;
    u32 deadline, timer_serial, rearm_deadline;
    u16 timer;
    u8 links, window, retries, rearm_pending, rearm_attempts;
} entry;

static void entry_run(void *epoch);
static void entry_cancel(void);
/* SDK spin_lock() is configured as IRQ-only on this dual-core target. */
static DEFINE_SPINLOCK(entry_state_lock);

void rdx_ble_entry_lock(void)
{
    spinlock_t *lock = &entry_state_lock;

    local_irq_disable();
    arch_spin_lock(lock);
}

void rdx_ble_entry_unlock(void)
{
    spinlock_t *lock = &entry_state_lock;

    arch_spin_unlock(lock);
    local_irq_enable();
}

void rdx_ble_entry_stop(void)
{
    rdx_ble_entry_lock();
    entry.running = 0;
    ++entry.epoch;
    entry.requested = entry.dirty = entry.queued = 0;
    entry.active = entry.key_down = entry.wake = entry.window = 0;
    entry.capacity = ENTRY_CAPACITY_AVAILABLE;
    entry.rearm_pending = entry.rearm_attempts = 0;
    rdx_ble_entry_unlock();
    entry_cancel();
}

void rdx_ble_entry_start(void)
{
    rdx_ble_entry_stop();
    rdx_ble_entry_lock();
    ++entry.topology;
    ++entry.revision;
    entry.consumed_refresh = entry.refresh;
    entry.consumed_release = entry.release;
    entry.consumed_rearm = entry.rearm;
    entry.links = 0xff;
    entry.retries = 0;
    entry.running = 1;
    rdx_ble_entry_unlock();
}

static void entry_post(void)
{
    int msg[] = { (int)entry_run, 1, 0 };
    u32 epoch;
    rdx_ble_entry_lock();
    if (!entry.running || entry.queued) {
        rdx_ble_entry_unlock();
        return;
    }
    entry.queued = 1;
    epoch = entry.epoch;
    msg[2] = epoch;
    rdx_ble_entry_unlock();
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        rdx_ble_entry_lock();
        if (epoch == entry.epoch) entry.queued = 0;
        rdx_ble_entry_unlock();
        /* 待办由现有优先级 0 恢复桥接保留并重投。 */
    }
}

void rdx_ble_entry_changed(void)
{
    rdx_ble_entry_lock();
    if (!entry.running) {
        rdx_ble_entry_unlock();
        return;
    }
    ++entry.revision;
    entry.dirty = 1;
    rdx_ble_entry_unlock();
    entry_post();
}

void rdx_ble_entry_request(u8 enable, u8 refresh)
{
    rdx_ble_entry_lock();
    if (!entry.running) {
        rdx_ble_entry_unlock();
        return;
    }
    entry.requested = enable;
    if (refresh) ++entry.refresh;
    rdx_ble_entry_unlock();
    rdx_ble_entry_changed();
}

void rdx_ble_entry_topology_changed(void)
{
    rdx_ble_entry_lock();
    if (!entry.running) {
        rdx_ble_entry_unlock();
        return;
    }
    ++entry.topology;
    rdx_ble_entry_unlock();
    rdx_ble_entry_changed();
}

void rdx_ble_entry_activity(void)
{
    rdx_ble_entry_lock();
    if (!entry.running) {
        rdx_ble_entry_unlock();
        return;
    }
    if (!entry.active) {
        entry.active = 1;
        if (entry.capacity == ENTRY_CAPACITY_AVAILABLE) {
            entry.wake = 1;
            entry.wake_time = sys_timer_get_ms();
        }
    }
    rdx_ble_entry_unlock();
    rdx_ble_entry_changed();
}

void rdx_ble_entry_key(u8 down)
{
    rdx_ble_entry_lock();
    if (!entry.running) {
        rdx_ble_entry_unlock();
        return;
    }
    entry.key_down = down;
    rdx_ble_entry_unlock();
    if (down) rdx_ble_entry_activity();
    else rdx_ble_entry_changed();
}

void rdx_ble_entry_composite_owned_locked(void)
{
    if (!entry.running || entry.capacity == ENTRY_CAPACITY_COMPOSITE_FULL) {
        return;
    }
    entry.capacity = ENTRY_CAPACITY_COMPOSITE_FULL;
    entry.wake = 0;
    ++entry.revision;
    entry.dirty = 1;
}

void rdx_ble_entry_composite_releasing(u32 runtime_epoch)
{
    rdx_ble_entry_lock();
    if (!entry.running) {
        rdx_ble_entry_unlock();
        return;
    }
    entry.capacity = ENTRY_CAPACITY_RELEASE_WAIT;
    entry.release_epoch = runtime_epoch;
    entry.wake = 0;
    ++entry.revision;
    entry.dirty = 1;
    rdx_ble_entry_unlock();
    entry_post();
}

void rdx_ble_entry_composite_disconnected(void)
{
    rdx_ble_entry_lock();
    if (!entry.running || entry.capacity == ENTRY_CAPACITY_AVAILABLE) {
        rdx_ble_entry_unlock();
        return;
    }
    entry.capacity = ENTRY_CAPACITY_AVAILABLE;
    entry.wake = 0;
    ++entry.revision;
    entry.dirty = 1;
    rdx_ble_entry_unlock();
    entry_post();
}

u8 rdx_ble_entry_commit_rearm(u32 runtime_epoch)
{
    u8 publish = 0;
    u8 ready = 0;

    /* READY and entry capacity must become visible together: an ATT claim
     * must not race between the two and revive a superseded release. */
    rdx_ble_entry_lock();
    if (entry.running &&
        rdx_ble_session_rdx_runtime_epoch_get() == runtime_epoch &&
        rdx_ble_session_rdx_runtime_state_get() == RDX_BLE_RUNTIME_RESETTING &&
        rdx_ble_session_rdx_runtime_rearm()) {
        ready = 1;
        if (entry.capacity == ENTRY_CAPACITY_RELEASE_WAIT &&
            entry.release_epoch == runtime_epoch) {
            entry.capacity = ENTRY_CAPACITY_AVAILABLE;
            entry.release_time = sys_timer_get_ms();
            ++entry.release;
            entry.wake = 0;
            ++entry.revision;
            entry.dirty = 1;
            publish = 1;
        }
    }
    rdx_ble_entry_unlock();
    if (publish) entry_post();
    return ready;
}

void rdx_ble_entry_rearm_requested(void)
{
    rdx_ble_entry_lock();
    if (!entry.running) {
        rdx_ble_entry_unlock();
        return;
    }
    ++entry.rearm;
    rdx_ble_entry_unlock();
    rdx_ble_entry_changed();
}

u8 rdx_ble_entry_needs_service(void)
{
    return entry.running && entry.dirty && !entry.queued;
}

void rdx_ble_entry_service(void)
{
    if (rdx_ble_entry_needs_service()) entry_post();
}

static void entry_cancel(void)
{
    ++entry.timer_serial;
    if (entry.timer) sys_timeout_del(entry.timer);
    entry.timer = 0;
}

static void entry_timeout(void *serial)
{
    if (!entry.running || (u32)serial != entry.timer_serial) return;
    entry.timer = 0;
    rdx_ble_entry_changed();
}

u8 rdx_ble_entry_allowed(void)
{
    u8 links;
    if (!entry.running || !entry.requested) return 0;
    links = rdx_ble_server_get_connected_count();
    if (links >= 2 || rdx_ble_entry_suppressed()) return 0;
    if (links && entry.capacity != ENTRY_CAPACITY_AVAILABLE) return 0;
    return !links || (entry.window && entry.topology == entry.consumed_topology &&
                     (s32)(sys_timer_get_ms() - entry.deadline) < 0);
}

static void entry_run(void *epoch)
{
    u32 topology, revision, refresh, wake_time, release, release_time, rearm;
    u32 now, due = 0;
    u8 links, wake, capacity, timed = 0;
    int ret;
    rdx_ble_entry_lock();
    if (!entry.running || (u32)epoch != entry.epoch) {
        rdx_ble_entry_unlock();
        return;
    }
    topology = entry.topology;
    revision = entry.revision;
    wake = entry.wake;
    wake_time = entry.wake_time;
    entry.wake = 0;
    entry.dirty = entry.queued = 0;
    refresh = entry.refresh;
    release = entry.release;
    release_time = entry.release_time;
    capacity = entry.capacity;
    rearm = entry.rearm;
    rdx_ble_entry_unlock();
    links = rdx_ble_server_get_connected_count();
    now = sys_timer_get_ms();
    if (links != entry.links || topology != entry.consumed_topology) {
        entry.links = links;
        entry.consumed_topology = topology;
        entry.window = links == 1 && capacity == ENTRY_CAPACITY_AVAILABLE;
        entry.deadline = now + ENTRY_WINDOW_MS;
        entry.retries = 0;
        if (entry.window) printf("[BLE_ENTRY] topology window=120s\n");
    }
    if (capacity != ENTRY_CAPACITY_AVAILABLE) entry.window = 0;
    if (release != entry.consumed_release) {
        entry.consumed_release = release;
        if (links == 1 && capacity == ENTRY_CAPACITY_AVAILABLE) {
            entry.deadline = release_time + ENTRY_WINDOW_MS;
            entry.window = (s32)(now - entry.deadline) < 0;
            entry.retries = 0;
            if (entry.window) printf("[BLE_ENTRY] RDX released window=120s\n");
        }
    }
    if (entry.window && (s32)(now - entry.deadline) >= 0) {
        entry.window = 0;
        printf("[BLE_ENTRY] window expired\n");
    }
    if (wake && links == 1 && !entry.window &&
        capacity == ENTRY_CAPACITY_AVAILABLE &&
        (s32)(wake_time - entry.deadline) >= 0 && !rdx_ble_entry_suppressed()) {
        entry.window = 1;
        entry.deadline = now + ENTRY_WINDOW_MS;
        entry.retries = 0;
        printf("[BLE_ENTRY] business wake window=120s\n");
    }
    /* 同一业务占用资源期间，重复数据包或按键报告不能再次触发重开。
     * 业务完成后才重新受理活动，不以 PA4 电平或 CPU 睡眠回调判定。 */
    if (!rdx_ble_entry_business_busy()) {
        rdx_ble_entry_lock();
        if (revision == entry.revision && !entry.key_down) entry.active = 0;
        rdx_ble_entry_unlock();
    }
    /* Runtime cleanup outlives an ACL or capacity change. */
    if (rearm != entry.consumed_rearm ||
        (entry.rearm_pending && (s32)(now - entry.rearm_deadline) >= 0)) {
        u32 delay;
        if (rearm != entry.consumed_rearm) {
            entry.consumed_rearm = rearm;
            entry.rearm_attempts = 0;
        }
        entry.rearm_pending = rdx_ble_entry_rearm_service();
        if (entry.rearm_pending) {
            if (entry.rearm_attempts < 25) ++entry.rearm_attempts;
            delay = entry.rearm_attempts <= 8 ? 250u :
                    entry.rearm_attempts <= 16 ? 1000u :
                    entry.rearm_attempts <= 24 ? 5000u : 30000u;
            /* Saturate at the slow retry interval rather than polling forever
             * at the initial rate while a long-running worker owns storage. */
            entry.rearm_deadline = sys_timer_get_ms() + delay;
        }
    }
    ret = rdx_ble_entry_radio_apply(rdx_ble_entry_allowed(), refresh != entry.consumed_refresh);
    if (!ret) entry.consumed_refresh = refresh;
    now = sys_timer_get_ms();
    if (entry.window) {
        due = entry.deadline;
        timed = 1;
    }
    if (entry.rearm_pending &&
        (!timed || (s32)(entry.rearm_deadline - due) < 0)) {
        due = entry.rearm_deadline;
        timed = 1;
    }
    if (ret > 0) {
        u32 radio_due = rdx_ble_entry_radio_deadline();
        if (!timed || (s32)(radio_due - due) < 0) due = radio_due;
        timed = 1;
    } else if (ret < 0 && entry.retries < ENTRY_RETRY_MAX) {
        ++entry.retries;
        if (!timed || (s32)(now + ENTRY_RETRY_MS - due) < 0)
            due = now + ENTRY_RETRY_MS;
        timed = 1;
    }
    entry_cancel();
    if (timed) {
        entry.timer = sys_timeout_add_2_task((void *)entry.timer_serial, entry_timeout,
            (s32)(due - now) > 0 ? due - now : 1, "app_core");
        if (!entry.timer) {
            /* A failed rearm timer alone must not close zero-ACL advertising.
             * A bounded window or pending radio operation still fails closed. */
            if (entry.window || ret != 0) {
                entry.window = 0;
                (void)rdx_ble_entry_radio_apply(0, 0);
            }
            if (entry.retries++ < ENTRY_RETRY_MAX) entry.dirty = 1;
            else if (entry.rearm_pending)
                printf("[BLE_ENTRY] rearm timer unavailable; activity retries\n");
        }
    }
    if (revision != entry.revision) entry_post();
}
