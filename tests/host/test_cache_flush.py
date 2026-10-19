"""执行实际缓存调度器，覆盖空闲停表、固定期限及失败/旧回调恢复。"""
import re
from host_c_test_lib import ROOT, run_c_checks

DIRECTORY = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'

STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;
#define NULL ((void*)0)
#define Q_CALLBACK 1
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
void *memset(void*, int, unsigned long long);
void rdx_cache_flush_changed(void);
static u32 now, queue[256];
static int qr, qw, queue_fail, timer_fail, core, irq, invalid;
static int state, saves, save_result, save_notify, state_notify;
static struct { void (*callback)(void*); void *arg; u32 due; int active; } timers[256];
static u16 timer_id;
void local_irq_disable(void) { ++irq; }
void local_irq_enable(void) { --irq; }
u32 sys_timer_get_ms(void) { return now; }
int os_taskq_post_type(const char *task, int type, int n, int *msg) {
    if (task[0]!='a' || type!=Q_CALLBACK || n!=3 || msg[1]!=1 || qw>=256) ++invalid;
    if (queue_fail) return -1;
    queue[qw++]=(u32)msg[2]; return 0;
}
u16 sys_timeout_add_2_task(void *arg, void (*callback)(void*), u32 delay, const char *task) {
    if (!core || irq || task[0]!='a' || !delay || timer_id>=254) ++invalid;
    if (timer_fail) return 0;
    ++timer_id;
    timers[timer_id].callback=callback; timers[timer_id].arg=arg;
    timers[timer_id].due=now+delay; timers[timer_id].active=1;
    return timer_id;
}
void sys_timeout_del(u16 id) {
    if (!core || irq) ++invalid;
    timers[id].active=0;
}
int rdx_uxfile_cache_flush_state(void) {
    if (!core || irq) ++invalid;
    int snapshot=state;
    if (state_notify) { state_notify=0; state=1; rdx_cache_flush_changed(); }
    return snapshot;
}
int rdx_uxfile_cache_flush_once(void) {
    if (!core || irq) ++invalid;
    ++saves; state=save_result;
    if (save_notify) rdx_cache_flush_changed();
    return state;
}
'''

TESTS = r'''
static int timer_count(void) {
    int count=0; for(int i=1;i<=timer_id;++i) count+=timers[i].active;
    return count;
}
static int drain(void) {
    int limit=32; core=1;
    while(qr<qw && limit--) cache_flush_run((void*)queue[qr++]);
    core=0;
    return limit>0 && !invalid && !irq;
}
static void reset(void) {
    memset(&cache_flush,0,sizeof(cache_flush)); memset(timers,0,sizeof(timers));
    now=0; qr=qw=queue_fail=timer_fail=irq=invalid=0;
    state=saves=save_result=save_notify=state_notify=0; timer_id=0;
    core=1; rdx_cache_flush_start(); core=0;
}
static int advance(u32 ms) {
    now+=ms; core=1;
    for(int i=1;i<=timer_id;++i) {
        if(timers[i].active && (s32)(now-timers[i].due)>=0) {
            timers[i].active=0; timers[i].callback(timers[i].arg);
        }
    }
    core=0; return drain();
}
int check_idle_and_dirty_deadline(void) {
    reset(); CHECK(drain()); CHECK(!timer_count() && !saves);
    CHECK(advance(600000)); CHECK(!timer_count() && !saves);
    state=1; rdx_cache_flush_changed(); CHECK(drain());
    CHECK(timer_count()==1 && cache_flush.deadline==605000);
    for(int i=0;i<4;++i) {
        CHECK(advance(1000)); rdx_cache_flush_changed(); CHECK(drain());
        CHECK(cache_flush.deadline==605000 && !saves && timer_count()==1);
    }
    save_notify=1; CHECK(advance(1000)); CHECK(saves==1 && !timer_count());
    CHECK(advance(600000)); CHECK(saves==1 && !timer_count());
    return 0;
}
int check_clean_cancel_and_stale_timer(void) {
    reset(); state=1; CHECK(drain()); u16 old=timer_id;
    state=0; rdx_cache_flush_changed(); CHECK(drain()); CHECK(!timer_count());
    state=1; now=6000; rdx_cache_flush_changed(); CHECK(drain());
    u16 current=cache_flush.timer; CHECK(cache_flush.deadline==11000);
    core=1; timers[old].callback(timers[old].arg); core=0;
    CHECK(drain()); CHECK(!saves && cache_flush.timer==current);
    CHECK(advance(5000)); CHECK(saves==1 && !timer_count()); return 0;
}
int check_busy_fault_and_recovery(void) {
    reset(); state=2; save_result=2; CHECK(drain());
    CHECK(advance(5000)); CHECK(saves==1 && timer_count()==1);
    CHECK(advance(500)); CHECK(saves==2 && timer_count()==1);
    state=1; save_result=0; CHECK(advance(500)); CHECK(saves==3 && !timer_count());
    state=1; rdx_cache_flush_changed(); CHECK(drain()); save_result=-1;
    CHECK(advance(5000)); CHECK(saves==4 && !timer_count());
    for(int i=0;i<5;++i) { rdx_cache_flush_changed(); CHECK(drain()); }
    CHECK(!timer_count() && saves==4);
    state=0; rdx_cache_flush_changed(); CHECK(drain());
    state=1; save_result=0; rdx_cache_flush_changed(); CHECK(drain());
    CHECK(advance(5000)); CHECK(saves==5 && !timer_count()); return 0;
}
int check_queue_timer_failures(void) {
    reset(); CHECK(drain()); queue_fail=1; state=1; rdx_cache_flush_changed();
    CHECK(rdx_cache_flush_needs_service() && !timer_count());
    queue_fail=0; timer_fail=1; rdx_cache_flush_service(); CHECK(drain());
    CHECK(rdx_cache_flush_needs_service() && cache_flush.deadline==5000);
    now=3000; timer_fail=0; rdx_cache_flush_service(); CHECK(drain());
    CHECK(!rdx_cache_flush_needs_service() && timer_count()==1 && cache_flush.deadline==5000);
    queue_fail=1; CHECK(advance(2000)); CHECK(!saves && rdx_cache_flush_needs_service());
    queue_fail=0; rdx_cache_flush_service(); CHECK(drain()); CHECK(saves==1 && !timer_count());
    return 0;
}
int check_stop_restart_and_races(void) {
    reset(); state=1; CHECK(drain()); u16 old=timer_id;
    rdx_cache_flush_changed();
    core=1; rdx_cache_flush_stop(); core=0;
    CHECK(drain()); CHECK(!timer_count() && !saves && state==1);
    rdx_cache_flush_changed(); CHECK(!rdx_cache_flush_needs_service());
    now=20000; core=1; rdx_cache_flush_start(); core=0; CHECK(drain());
    CHECK(cache_flush.deadline==25000);
    core=1; timers[old].callback(timers[old].arg); core=0; CHECK(drain());
    CHECK(!saves && timer_count()==1);
    CHECK(advance(5000)); CHECK(saves==1 && !timer_count());
    state_notify=1; rdx_cache_flush_changed(); CHECK(drain());
    CHECK(timer_count()==1 && cache_flush.deadline==30000);
    CHECK(advance(5000)); CHECK(saves==2 && !timer_count()); return 0;
}
int check_wrap_and_failed_timer_deadline(void) {
    reset(); CHECK(drain()); now=0xfffffff0u;
    state=1; timer_fail=1; rdx_cache_flush_changed(); CHECK(drain());
    CHECK(cache_flush.deadline==4984 && rdx_cache_flush_needs_service());
    now+=4999; rdx_cache_flush_service(); CHECK(drain()); CHECK(!saves);
    ++now; rdx_cache_flush_service(); CHECK(drain());
    CHECK(saves==1 && !timer_count() && !rdx_cache_flush_needs_service()); return 0;
}
'''


def main():
    source = (DIRECTORY / 'rdx_cache_flush.c').read_text(encoding='utf-8')
    source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
    audit = ROOT / 'cache/cache-flush-tests'
    audit.mkdir(parents=True, exist_ok=True)
    path = audit / 'cache_flush.c'
    path.write_text(STUBS + source + TESTS, encoding='utf-8')
    run_c_checks(path, re.findall(r'int (check_\w+)\(void\)', TESTS))
    dip = (DIRECTORY / 'rdx_dip_switch.c').read_text(encoding='utf-8')
    assert 'rdx_cache_flush_needs_service()' in dip
    assert 'rdx_cache_flush_service();' in dip
    assert 'rdx_cache_flush.c' in (ROOT / 'SDK/Makefile').read_text(encoding='utf-8')
    print('Cache flush: clean idle, fixed deadline, recovery, stale work and wrap checks passed.')


if __name__ == '__main__':
    main()
