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
#define log_info(...) ((void)0)
#define printf(...) ((void)0)
#define putchar(...) ((void)0)
#define NULL ((void *)0)
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define RDX_SUPPORT_ALGORITHM 1
#define AUDIO_CH_NUM(mode) 1
#define EINVAL 22
enum { MIC_TO_MONO_OPUS, DAC_TO_MONO_OPUS, MIC_TO_STERO_OPUS,
       DAC_TO_STERO_OPUS, MIC_DAC_TO_STERO_OPUS };
enum { MIC = 1, DAC = 2, AUDIO_CH_L = 1, AUDIO_CH_R = 2, AUDIO_CH_LR = 3 };
enum { NODE_UUID_ADC, NODE_UUID_SOURCE_DEV1, AUDIO_CODING_OPUS };
int strcmp(const char *, const char *);
__SIZE_TYPE__ strlen(const char *);
static u8 ref_cnt, clk_adjust_step;
static u16 clk_ref_timer;
static u32 clk_ref_deadline, clk_ref_generation;
static struct { u32 freq, name; } clk_locker;
static int locked, assertions, irq, disabled, fixed, calculations, resets;
static int created, live, timer_fail, affinity_errors, context_errors, max_live, clock_sets;
static u32 now, frequency, due;
static u8 record_clock_locked, record_streams, fail_dac;
static u32 clock_read_override;
static void (*pending)(void *);
static void *pending_arg;
static const char *task;
static u32 JBHash(const u8 *name, u32 len) {
    u32 hash = 0;
    while (len--) hash = hash * 31 + *name++;
    return hash;
}
static u32 clk_get(const char *name) { return clock_read_override ? clock_read_override : frequency; }
static int rdx_record_session_allowed(void) { return 1; }
static int esco_player_runing(void) { return 0; }
static void set_global_ch_mode(u8 mode) {}
static int translation_ear_recoder_open_impl(int type, int source, int coding, int ai_type) {
    if (!record_clock_locked || frequency != TCFG_T2620_RECORD_CLOCK_HZ) ++context_errors;
    type &= 0xf;
    if (fail_dac && type == DAC) return -3;
    record_streams |= type;
    return 0;
}
static void translation_ear_recoder_close(u8 type) {
    if ((record_streams & type) && !record_clock_locked) ++context_errors;
    record_streams &= ~type;
}
u32 sys_timer_get_ms(void) { return now; }
int cpu_in_irq(void) { return irq; }
int cpu_irq_disabled(void) { return disabled; }
int os_taskq_post_type(const char *name, int type, int argc, int *argv) { return 0; }
void clk_set_api(const char *name, u32 value) {
    if (!locked) ++context_errors;
    ++clock_sets;
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
    timer_fail=affinity_errors=context_errors=max_live=clock_sets=0;
    now=frequency=due=clk_locker.freq=clk_locker.name=0; pending=NULL; pending_arg=NULL;
    record_clock_locked=record_streams=fail_dac=clock_read_override=0;
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
int test_clock_refurbish_preserves_lock(void) {
    reset();
    frequency=clk_locker.freq=24000000;
    clock_refurbish();
    CHECK(frequency==24000000 && !clock_sets && live==1 && due==10000);
    now=1000; clock_refurbish();
    CHECK(frequency==24000000 && !clock_sets && clk_ref_deadline==11000);
    fire(); fire();
    CHECK(calculations==1 && clean());
    clk_locker.freq=0;
    clock_refurbish();
    CHECK(frequency==CLOCK_MAXIMUM_FREQ && clock_sets==1 && ref_cnt==0 && clean());
    return 0;
}
int test_record_clock_stop_and_resume(void) {
    reset();
    CHECK(!translation_ear_recoder_open_all_impl(MIC_TO_MONO_OPUS));
    CHECK(record_clock_locked && record_streams==(MIC|DAC));
    CHECK(frequency==TCFG_T2620_RECORD_CLOCK_HZ && clock_sets==1);
    clock_refurbish();
    CHECK(clock_sets==1);
    translation_ear_recoder_close_all();
    CHECK(!record_streams && !record_clock_locked && !clk_locker.freq);
    CHECK(frequency==CLOCK_MAXIMUM_FREQ && clock_sets==2 && live==1);
    translation_ear_recoder_close_all();
    CHECK(clock_sets==2);
    CHECK(!translation_ear_recoder_open_all_impl(MIC_TO_MONO_OPUS));
    CHECK(record_clock_locked && record_streams==(MIC|DAC));
    translation_ear_recoder_close_all();
    CHECK(!record_streams && !record_clock_locked && !clk_locker.freq && clean());
    return 0;
}
int test_record_clock_failed_open(void) {
    reset();
    CHECK(!clock_lock("other", CLOCK_MAXIMUM_FREQ));
    CHECK(translation_ear_recoder_open_all_impl(MIC_TO_MONO_OPUS)!=0);
    translation_ear_recoder_close_all();
    CHECK(!record_clock_locked && !record_streams && clk_locker.freq==CLOCK_MAXIMUM_FREQ);
    CHECK(!clock_unlock("other"));
    clock_read_override=CLOCK_MAXIMUM_FREQ;
    CHECK(translation_ear_recoder_open_all_impl(MIC_TO_MONO_OPUS)==-EINVAL);
    CHECK(!record_clock_locked && !record_streams && !clk_locker.freq);
    clock_read_override=0;
    fail_dac=1;
    CHECK(translation_ear_recoder_open_all_impl(MIC_TO_MONO_OPUS)==-3);
    CHECK(!record_clock_locked && !record_streams && !clk_locker.freq);
    CHECK(frequency==CLOCK_MAXIMUM_FREQ && clean());
    return 0;
}
'''


def run_clock_checks():
    source = (ROOT / 'SDK/apps/common/clock_manager/clock_manager.c').read_text(encoding='utf-8')
    recorder = (ROOT / 'SDK/audio/interface/recoder/translation_ear_recoder.c').read_text(encoding='utf-8')
    config = (ROOT / 'SDK/apps/earphone/include/t2620_project_config.h').read_text(encoding='utf-8')
    record_hz = next(line for line in config.splitlines()
                     if line.startswith('#define TCFG_T2620_RECORD_CLOCK_HZ '))
    functions = '\n'.join(function(source, name) for name in
                          ('clk_ref_arm', 'clk_ref_fun', 'clock_refurbish',
                           'clock_lock_dump', 'clock_lock', 'clock_unlock'))
    record_functions = '\n'.join(function(recorder, name) for name in
                                 ('translation_ear_recoder_close_all', 'translation_ear_recoder_open_all_impl'))
    harness = record_hz + STUBS + '\nstatic void clk_ref_fun(void *p);\n' + functions + record_functions + CHECKS
    with tempfile.TemporaryDirectory(prefix='clock_worker_') as work:
        path = Path(work) / 'checks.c'
        path.write_text(harness, encoding='utf-8')
        run_c_checks(path, ('test_clock_idle_and_restart', 'test_clock_refresh_deadline',
                            'test_clock_queued_expiry_and_wrap', 'test_clock_allocation_failure',
                            'test_clock_refurbish_preserves_lock', 'test_record_clock_stop_and_resume',
                            'test_record_clock_failed_open'), native=True)
    print('PASS: actual clock scheduling, lock preservation, recorder stop/resume and failed-open cleanup')
