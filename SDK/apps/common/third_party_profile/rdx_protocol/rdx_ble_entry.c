#include "system/includes.h"
#include "rdx_ble_server.h"
#include "rdx_ble_entry.h"

#define ENTRY_WINDOW_MS 120000u
#define ENTRY_RETRY_MS 100u
#define ENTRY_RETRY_MAX 3

static struct {
    volatile u32 epoch;
    volatile u8 running;
    volatile u32 topology;
    volatile u32 revision;
    volatile u32 refresh, wake_time;
    volatile u8 requested, dirty, queued;
    volatile u8 active, key_down, wake;
    u32 consumed_topology, consumed_refresh, deadline, timer_serial;
    u16 timer;
    u8 links, window, retries;
} entry;

static void entry_run(void *epoch);
static void entry_cancel(void);

void rdx_ble_entry_stop(void)
{
    local_irq_disable();
    entry.running = 0;
    ++entry.epoch;
    entry.requested = entry.dirty = entry.queued = 0;
    entry.active = entry.key_down = entry.wake = entry.window = 0;
    local_irq_enable();
    entry_cancel();
}

void rdx_ble_entry_start(void)
{
    rdx_ble_entry_stop();
    local_irq_disable();
    ++entry.topology;
    ++entry.revision;
    entry.consumed_refresh = entry.refresh;
    entry.links = 0xff;
    entry.retries = 0;
    entry.running = 1;
    local_irq_enable();
}

static void entry_post(void)
{
    int msg[] = { (int)entry_run, 1, 0 };
    u32 epoch;
    local_irq_disable();
    if (!entry.running || entry.queued) {
        local_irq_enable();
        return;
    }
    entry.queued = 1;
    epoch = entry.epoch;
    msg[2] = epoch;
    local_irq_enable();
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        local_irq_disable();
        if (epoch == entry.epoch) entry.queued = 0;
        local_irq_enable();
        /* 待办由现有优先级 0 恢复桥接保留并重投。 */
    }
}

void rdx_ble_entry_changed(void)
{
    local_irq_disable();
    if (!entry.running) {
        local_irq_enable();
        return;
    }
    ++entry.revision;
    entry.dirty = 1;
    local_irq_enable();
    entry_post();
}

void rdx_ble_entry_request(u8 enable, u8 refresh)
{
    local_irq_disable();
    if (!entry.running) {
        local_irq_enable();
        return;
    }
    entry.requested = enable;
    if (refresh) ++entry.refresh;
    local_irq_enable();
    rdx_ble_entry_changed();
}

void rdx_ble_entry_topology_changed(void)
{
    local_irq_disable();
    if (!entry.running) {
        local_irq_enable();
        return;
    }
    ++entry.topology;
    local_irq_enable();
    rdx_ble_entry_changed();
}

void rdx_ble_entry_activity(void)
{
    local_irq_disable();
    if (!entry.running) {
        local_irq_enable();
        return;
    }
    if (!entry.active) {
        entry.active = 1;
        entry.wake = 1;
        entry.wake_time = sys_timer_get_ms();
    }
    local_irq_enable();
    rdx_ble_entry_changed();
}

void rdx_ble_entry_key(u8 down)
{
    local_irq_disable();
    if (!entry.running) {
        local_irq_enable();
        return;
    }
    entry.key_down = down;
    local_irq_enable();
    if (down) rdx_ble_entry_activity();
    else rdx_ble_entry_changed();
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
    return !links || (entry.window && entry.topology == entry.consumed_topology &&
                     (s32)(sys_timer_get_ms() - entry.deadline) < 0);
}

static void entry_run(void *epoch)
{
    u32 topology, revision, refresh, wake_time, now, due = 0;
    u8 links, wake, timed = 0;
    int ret;
    local_irq_disable();
    if (!entry.running || (u32)epoch != entry.epoch) {
        local_irq_enable();
        return;
    }
    topology = entry.topology;
    revision = entry.revision;
    wake = entry.wake;
    wake_time = entry.wake_time;
    entry.wake = 0;
    entry.dirty = entry.queued = 0;
    refresh = entry.refresh;
    local_irq_enable();
    links = rdx_ble_server_get_connected_count();
    now = sys_timer_get_ms();
    if (links != entry.links || topology != entry.consumed_topology) {
        entry.links = links;
        entry.consumed_topology = topology;
        entry.window = links == 1;
        entry.deadline = now + ENTRY_WINDOW_MS;
        entry.retries = 0;
        if (entry.window) printf("[BLE_ENTRY] topology window=120s\n");
    }
    if (entry.window && (s32)(now - entry.deadline) >= 0) {
        entry.window = 0;
        printf("[BLE_ENTRY] window expired\n");
    }
    if (wake && links == 1 && !entry.window &&
        (s32)(wake_time - entry.deadline) >= 0 && !rdx_ble_entry_suppressed()) {
        entry.window = 1;
        entry.deadline = now + ENTRY_WINDOW_MS;
        entry.retries = 0;
        printf("[BLE_ENTRY] business wake window=120s\n");
    }
    /* 同一业务占用资源期间，重复数据包或按键报告不能再次触发重开。
     * 业务完成后才重新受理活动，不以 PA4 电平或 CPU 睡眠回调判定。 */
    if (!rdx_ble_entry_business_busy()) {
        local_irq_disable();
        if (revision == entry.revision && !entry.key_down) entry.active = 0;
        local_irq_enable();
    }
    ret = rdx_ble_entry_radio_apply(rdx_ble_entry_allowed(), refresh != entry.consumed_refresh);
    if (!ret) entry.consumed_refresh = refresh;
    now = sys_timer_get_ms();
    if (entry.window) {
        due = entry.deadline;
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
            /* 无法保证有限期限时，不能继续开放第二入口。 */
            entry.window = 0;
            (void)rdx_ble_entry_radio_apply(0, 0);
            if (entry.retries++ < ENTRY_RETRY_MAX) entry.dirty = 1;
        }
    }
    if (revision != entry.revision) entry_post();
}
