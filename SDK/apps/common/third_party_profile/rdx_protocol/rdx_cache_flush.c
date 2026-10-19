#include "system/includes.h"
#include "rdx_cache_flush.h"

#define CACHE_FLUSH_DELAY_MS 5000u
#define CACHE_FLUSH_RETRY_MS 500u

static struct {
    volatile u32 epoch, revision;
    volatile u8 running, pending, queued;
    u32 deadline, timer_serial;
    u16 timer;
    u8 waiting;
} cache_flush;

static void cache_flush_run(void *epoch);

static void cache_flush_cancel(void)
{
    ++cache_flush.timer_serial;
    if (cache_flush.timer) sys_timeout_del(cache_flush.timer);
    cache_flush.timer = 0;
}

void rdx_cache_flush_stop(void)
{
    local_irq_disable();
    cache_flush.running = 0;
    ++cache_flush.epoch;
    cache_flush.pending = cache_flush.queued = 0;
    local_irq_enable();
    cache_flush_cancel();
    cache_flush.waiting = 0;
}

void rdx_cache_flush_start(void)
{
    rdx_cache_flush_stop();
    cache_flush.running = 1;
    rdx_cache_flush_changed();
}

static void cache_flush_post(void)
{
    int message[] = { (int)cache_flush_run, 1, 0 };
    u32 epoch;
    local_irq_disable();
    if (!cache_flush.running || cache_flush.queued) {
        local_irq_enable();
        return;
    }
    epoch = cache_flush.epoch;
    message[2] = epoch;
    cache_flush.queued = 1;
    local_irq_enable();
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, message)) {
        local_irq_disable();
        if (epoch == cache_flush.epoch) cache_flush.queued = 0;
        local_irq_enable();
    }
}

void rdx_cache_flush_changed(void)
{
    local_irq_disable();
    if (!cache_flush.running) {
        local_irq_enable();
        return;
    }
    ++cache_flush.revision;
    cache_flush.pending = 1;
    local_irq_enable();
    cache_flush_post();
}

u8 rdx_cache_flush_needs_service(void)
{
    return cache_flush.running && cache_flush.pending && !cache_flush.queued;
}

void rdx_cache_flush_service(void)
{
    if (rdx_cache_flush_needs_service()) cache_flush_post();
}

static void cache_flush_timeout(void *serial)
{
    if (!cache_flush.running || (u32)serial != cache_flush.timer_serial) return;
    cache_flush.timer = 0;
    rdx_cache_flush_changed();
}

static void cache_flush_run(void *epoch)
{
    u32 revision, now;
    int state;
    local_irq_disable();
    if (!cache_flush.running || (u32)epoch != cache_flush.epoch) {
        local_irq_enable();
        return;
    }
    revision = cache_flush.revision;
    cache_flush.pending = cache_flush.queued = 0;
    local_irq_enable();
    cache_flush_cancel();
    state = rdx_uxfile_cache_flush_state();
    now = sys_timer_get_ms();
    if (state <= 0) {
        cache_flush.waiting = 0;
    } else {
        /* 首次发现脏缓存给出固定期限，重复修改不向后推迟保存。 */
        if (!cache_flush.waiting) {
            cache_flush.waiting = 1;
            cache_flush.deadline = now + CACHE_FLUSH_DELAY_MS;
        }
        if ((s32)(now - cache_flush.deadline) >= 0) {
            state = rdx_uxfile_cache_flush_once();
            cache_flush.waiting = state > 0;
            cache_flush.deadline = sys_timer_get_ms() + CACHE_FLUSH_RETRY_MS;
        }
        if (cache_flush.waiting) {
            now = sys_timer_get_ms();
            u32 delay = (s32)(cache_flush.deadline - now) > 0 ?
                        cache_flush.deadline - now : 1;
            cache_flush.timer = sys_timeout_add_2_task(
                (void *)cache_flush.timer_serial, cache_flush_timeout, delay, "app_core");
            /* 仅失败待办复用 DIP 优先级 0 桥接；稳定干净态没有定时器。 */
            if (!cache_flush.timer) cache_flush.pending = 1;
        }
    }
    if (revision != cache_flush.revision) cache_flush_post();
}
