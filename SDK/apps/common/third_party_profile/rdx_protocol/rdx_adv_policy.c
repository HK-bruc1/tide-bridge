#include "system/includes.h"
#include "rdx_adv_policy.h"
#include "rdx_peripheral_power.h"
#include <stdarg.h>

#define ADV_IDLE_MS 120000u
#define ADV_RECHECK_MS 1000u
#define ADV_RETRY_MAX 3

enum { ADV_DEFERRED, ADV_FAST_HOLD, ADV_FAST_IDLE, ADV_SLOW_IDLE };
static struct {
    volatile u32 version;
    volatile u32 pending;
    volatile u8 enabled;
    volatile u8 dirty;
    volatile u8 qualified;
    volatile u8 queued;
    u32 consumed;
    u32 deadline;
    u32 timer_serial;
    u16 timer;
    u8 state;
    u8 radio; /* 0 未知，1 快广播，2 慢广播 */
    u8 retries;
    u8 window_armed;
    u8 logged_state;
    u32 logged_hold;
    u32 logged_version;
    u8 logged;
} adv;

static void adv_run(void *unused);
static void adv_post(void);

static void adv_timer(void *serial)
{
    if ((u32)serial != adv.timer_serial) return;
    adv.timer = 0;
    adv_post();
}

static void adv_cancel(void)
{
    ++adv.timer_serial;
    if (adv.timer) sys_timeout_del(adv.timer);
    adv.timer = 0;
}

static int adv_schedule(u32 delay)
{
    adv_cancel();
    adv.timer = sys_timeout_add((void *)adv.timer_serial, adv_timer, delay);
    if (!adv.timer) {
        adv.qualified = 0;
        printf("[ADV_POLICY] timer allocation failed; cutoff blocked\n");
        /* 队列重试时可重新申请定时器，不将当前空闲窗口视为已到期。 */
        if (++adv.retries <= ADV_RETRY_MAX) adv_post();
        return -1;
    }
    return 0;
}

static void adv_post(void)
{
    int msg[3] = { (int)adv_run, 1, 0 };
    local_irq_disable();
    if (adv.queued) {
        local_irq_enable();
        return;
    }
    adv.queued = 1;
    local_irq_enable();
    if (os_taskq_post_type("app_core", Q_CALLBACK, 3, msg)) {
        adv.queued = 0;
        adv.qualified = 0;
        adv.dirty = 1;
        if (++adv.retries <= ADV_RETRY_MAX) adv_schedule(100);
        else printf("[ADV_POLICY] queue retry exhausted; cutoff blocked\n");
    }
}

static void adv_publish(u8 activity)
{
    local_irq_disable();
    if (activity) {
        ++adv.version;
        adv.qualified = 0;
    }
    adv.retries = 0; /* 新的外部事件重新启动一轮恢复尝试。 */
    adv.dirty = 1;
    local_irq_enable();
    adv_post();
}

void rdx_adv_policy_activity(void) { adv_publish(1); }
void rdx_adv_policy_changed(void) { adv_publish(0); }

void rdx_adv_policy_enable(void)
{
    adv.enabled = 1;
    adv.radio = 0;
    adv_publish(1);
}

void rdx_adv_policy_disable(void)
{
    adv.enabled = 0;
    adv_publish(1);
}

int rdx_adv_policy_business_enter(void)
{
    local_irq_disable();
    ++adv.pending;
    adv.retries = 0;
    ++adv.version;
    adv.qualified = 0;
    adv.dirty = 1;
    local_irq_enable();
    adv_post();
    if (rdx_peripheral_power_vdd_ensure_on(RDX_SHARED_VDD_WAKE_BUSINESS)) {
        rdx_adv_policy_business_exit();
        return -1;
    }
    return 0;
}

void rdx_adv_policy_business_exit(void)
{
    local_irq_disable();
    if (adv.pending) {
        --adv.pending;
        adv.retries = 0;
        ++adv.version; /* 即使操作在两次队列读取之间已完成，也保留其活动记录。 */
        adv.qualified = 0;
    }
    adv.dirty = 1;
    local_irq_enable();
    adv_post();
}

u8 rdx_adv_policy_idle_valid(void)
{
    return adv.enabled && adv.qualified && !adv.pending &&
           adv.consumed == adv.version;
}

static void adv_run(void *unused)
{
    u32 version, pending, hold, now;
    u8 activity, slow;
    (void)unused;
    local_irq_disable();
    version = adv.version;
    pending = adv.pending;
    adv.dirty = 0;
    adv.queued = 0;
    local_irq_enable();
    if (!adv.enabled || !rdx_adv_policy_allowed()) {
        adv_cancel();
        adv.qualified = 0;
        adv.state = ADV_DEFERRED;
        adv.radio = 0;
        return;
    }
    hold = rdx_adv_policy_hold_snapshot() | (pending ? 0x80000000u : 0);
    now = sys_timer_get_ms();
    activity = version != adv.consumed;
    if (hold) {
        adv.qualified = 0;
        adv.state = ADV_FAST_HOLD;
        adv.window_armed = 0;
    } else if (activity || adv.state == ADV_FAST_HOLD || adv.state == ADV_DEFERRED) {
        adv.qualified = 0;
        adv.state = ADV_FAST_IDLE;
        adv.window_armed = 0;
        adv.deadline = now + ADV_IDLE_MS;
    }
    if (adv.state == ADV_FAST_IDLE && !adv.window_armed)
        adv.deadline = now + ADV_IDLE_MS;
    slow = !hold && adv.state != ADV_DEFERRED &&
           (adv.state == ADV_SLOW_IDLE ||
            (adv.window_armed && (s32)(now - adv.deadline) >= 0));
    if (adv.radio != (slow ? 2 : 1)) {
        if (rdx_adv_policy_radio_set(slow)) {
            adv.qualified = 0;
            adv.radio = 0;
            if (++adv.retries <= ADV_RETRY_MAX) adv_schedule(100);
            else printf("[ADV_POLICY] radio retry exhausted; cutoff blocked\n");
            return;
        }
        adv.radio = slow ? 2 : 1;
    }
    local_irq_disable();
    adv.consumed = version;
    adv.qualified = slow && version == adv.version && !adv.pending;
    local_irq_enable();
    if (version != adv.version || adv.dirty) {
        adv_post();
        return;
    }
    if (slow) {
        u8 first = adv.state != ADV_SLOW_IDLE;
        adv.state = ADV_SLOW_IDLE;
        adv_cancel();
        if (first) rdx_adv_policy_slow_committed();
    } else if (hold) {
        if (adv_schedule(ADV_RECHECK_MS)) return;
    } else {
        /* 广播或 VDD 恢复可能阻塞，完成后重新采样时间。 */
        now = sys_timer_get_ms();
        if (adv_schedule((s32)(adv.deadline - now) > 0 ? adv.deadline - now : 1)) {
            adv.window_armed = 0;
            return;
        }
        adv.window_armed = 1;
    }
    adv.retries = 0;
    if (!adv.logged || adv.logged_state != adv.state ||
        adv.logged_hold != hold || adv.logged_version != version) {
        printf("[ADV_POLICY] state=%u hold=0x%x version=%u\n", adv.state, hold, version);
        adv.logged = 1;
        adv.logged_state = adv.state;
        adv.logged_hold = hold;
        adv.logged_version = version;
    }
}

/* UXFILE 消息发送与接收 ABI 由静态库哈希校验约束。该标记将一条已接纳的
 * 队列请求与一次完成通知对应，包括被忽略的消息。 */
int rdx_adv_uxfile_post(const char *task, int argc, ...)
{
    va_list ap;
    int args[3] = {0}, ret, i;
    if (argc < 1 || argc > 3) return -1;
    va_start(ap, argc);
    for (i = 0; i < argc; ++i) args[i] = va_arg(ap, int);
    va_end(ap);
    if (rdx_adv_policy_business_enter()) return -1;
    args[0] |= 0x40000000;
    ret = os_taskq_post_msg(task, argc, args[0], args[1], args[2]);
    if (ret) rdx_adv_policy_business_exit();
    return ret;
}

void rdx_adv_uxfile_done(int received, int message)
{
    if (received && (message & 0x40000000)) rdx_adv_policy_business_exit();
}
