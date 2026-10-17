"""执行真实调频调度函数，验证工作任务提频后的检测归属、期限与停表重启。"""
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, function, run_c_checks


STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
#define CLOCK_DETECT_PERIOD 10000
#define CLOCK_DETECT_COUNTER 7
#define CLOCK_MAXIMUM_FREQ 160000000
#define Q_CALLBACK 1
#define CLOCK_MANAGER_ENTER_CRITICAL() (++locked)
#define CLOCK_MANAGER_EXIT_CRITICAL() (--locked)
#define ASSERT(x) do { if (!(x)) ++assertions; } while (0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define NULL ((void *)0)
int strcmp(const char *, const char *);
static u8 ref_cnt, clk_adjust_step;
static u16 clk_ref_timer;
static u32 clk_ref_deadline, clk_ref_generation;
static int locked, assertions, irq, disabled, fixed, calculations, resets;
static int created, live, timer_fail, affinity_errors, context_errors, max_live;
static u32 now, frequency, due;
static void (*pending)(void *);
static void *pending_arg;
static const char *task;
u32 sys_timer_get_ms(void) { return now; }
int cpu_in_irq(void) { return irq; }
int cpu_irq_disabled(void) { return disabled; }
int os_taskq_post_type(const char *name, int type, int argc, int *argv) { return 0; }
void clk_set_api(const char *name, u32 value) {
    if (!locked) ++context_errors;
    frequency=value;
}
void task_info_reset(void) { ++resets; }
void clk_ref_cal(void) {
    if (!locked || strcmp(task,"app_core")) ++context_errors;
    ++calculations;
}
u16 sys_timeout_add_2_task(void *p, void (*f)(void *), u32 ms, const char *name) {
    if (strcmp(name,"app_core") || !locked || !ms || ms>CLOCK_DETECT_PERIOD) ++affinity_errors;
    if (timer_fail) return 0;
    if (++live>max_live) max_live=live;
    due=now+ms; pending=f; pending_arg=p;
    return ++created;
}
'''

CHECKS = r'''
static void reset(void) {
    ref_cnt=clk_adjust_step=clk_ref_timer=0;
    clk_ref_deadline=clk_ref_generation=0;
    locked=assertions=irq=disabled=calculations=resets=created=live=0;
    timer_fail=affinity_errors=context_errors=max_live=0;
    now=frequency=due=0; pending=NULL; pending_arg=NULL;
    task="protocol_recv_task";
}
static void fire(void) {
    void (*f)(void *)=pending;
    void *p=pending_arg;
    now=due; --live; pending=NULL; task="app_core";
    f(p);
    task="protocol_recv_task";
}
static int clean(void) {
    return !locked && !assertions && !affinity_errors && !context_errors && max_live==1;
}
int test_clock_idle_and_restart(void) {
    int i;
    reset();
    CHECK(!clk_ref_timer && !created);
    clock_refurbish();
    CHECK(frequency==CLOCK_MAXIMUM_FREQ && resets==1 && live==1 && due==10000);
    for (i=1;i<=7;i++) {
        fire();
        CHECK(calculations==i && ref_cnt==i);
    }
    CHECK(!clk_ref_timer && !live && !pending && created==7 && now==70000);
    now=120000;
    clock_refurbish();
    CHECK(ref_cnt==0 && resets==2 && live==1 && due==130000);
    for (i=0;i<7;i++) fire();
    CHECK(calculations==14 && !live && !clk_ref_timer && clean());
    return 0;
}
int test_clock_refresh_deadline(void) {
    int i;
    void (*old_cb)(void *);
    void *old_arg;
    reset(); clock_refurbish();
    old_cb=pending; old_arg=pending_arg;
    for (i=1;i<=9;i++) { now=i*1000; clock_refurbish(); }
    CHECK(created==1 && resets==10 && clk_ref_deadline==19000);
    fire();
    CHECK(!calculations && !ref_cnt && due==19000 && live==1);
    task="app_core"; old_cb(old_arg);
    CHECK(!calculations && created==2 && due==19000 && live==1);
    task="protocol_recv_task";
    fire();
    CHECK(calculations==1 && due==29000);
    for (i=1;i<7;i++) fire();
    CHECK(!live && !clk_ref_timer && clean());
    return 0;
}
int test_clock_queued_expiry_and_wrap(void) {
    void (*queued)(void *);
    void *arg;
    reset(); clock_refurbish();
    queued=pending; arg=pending_arg; now=due; --live; pending=NULL;
    clock_refurbish();
    CHECK(created==1 && clk_ref_deadline==20000);
    task="app_core"; queued(arg);
    CHECK(!calculations && live==1 && due==20000);
    fire();
    CHECK(calculations==1 && clean());

    reset(); now=0xfffffff0u; clock_refurbish();
    now+=9000; clock_refurbish();
    fire();
    CHECK(!calculations && due==(u32)(0xfffffff0u+19000));
    fire();
    CHECK(calculations==1 && clean());

    reset(); clock_refurbish();
    queued=pending; arg=pending_arg;
    while (live) fire();
    clock_refurbish();
    task="app_core"; queued(arg);
    CHECK(ref_cnt==0 && live==1 && calculations==7 && clean());
    return 0;
}
int test_clock_allocation_failure(void) {
    reset(); timer_fail=1; clock_refurbish();
    CHECK(assertions && !clk_ref_timer && !live && !locked);
    reset(); clock_refurbish();
    timer_fail=1; fire();
    CHECK(assertions && calculations==1 && !clk_ref_timer && !live && !locked);
    return 0;
}
'''


def run_clock_checks():
    source = (ROOT / 'SDK/apps/common/clock_manager/clock_manager.c').read_text(encoding='utf-8')
    functions = '\n'.join(function(source, name) for name in
                          ('clk_ref_arm', 'clk_ref_fun', 'clock_refurbish'))
    harness = STUBS + '\nstatic void clk_ref_fun(void *p);\n' + functions + CHECKS
    with tempfile.TemporaryDirectory(prefix='clock_worker_') as work:
        path = Path(work) / 'checks.c'
        path.write_text(harness, encoding='utf-8')
        run_c_checks(path, ('test_clock_idle_and_restart', 'test_clock_refresh_deadline',
                            'test_clock_queued_expiry_and_wrap', 'test_clock_allocation_failure'), native=True)
    print('PASS: actual clock scheduling, app_core affinity, deadlines, stale expiry, restart and failure')
