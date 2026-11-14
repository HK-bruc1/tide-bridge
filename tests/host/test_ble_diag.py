"""Execute the diagnostic recorder/snapshot and verify the pinned controller patch.

Does not model radio timing or the controller scheduler. Vendor functions must
remain byte-for-byte identical in IR after removing the added scalar captures.
"""
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, function, run_c_checks

DIR = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
PATCH = ROOT / 'tools/ble_diag/patch_btctrler.ps1'
VENDOR = ROOT / 'SDK/cpu/br28/liba/btctrler.a'
PIN = '6ecbbd876ea08e6d260fbcd1c8c7b8cd214bfde70edf1145e3f7d02eccfabc50'

STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define NULL ((void*)0)
#define Q_CALLBACK 1
#define DEFINE_SPINLOCK(name) int name
typedef int spinlock_t;
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
void *memset(void*, int, unsigned long long);
static int irq, held, invalid, queue_fail, pending, posts, prints, worker;
static u32 now;
static void diag_enter(void);
static void diag_leave(void);
static void diag_flush(void*);
void local_irq_disable(void) { ++irq; }
void local_irq_enable(void) { if (!irq) ++invalid; else --irq; }
void test_lock(int *p) { if (!irq || held) ++invalid; held = 1; }
void test_unlock(int *p) { if (!irq || !held) ++invalid; held = 0; }
#define arch_spin_lock(p) test_lock(p)
#define arch_spin_unlock(p) test_unlock(p)
u32 sys_timer_get_ms(void) { return now; }
int rdx_ble_diag_patch_revision(void) { return 2; }
int diag_printf(const char *format, ...) {
    if (irq || held || !worker) ++invalid;
    ++prints;
    return 0;
}
#define printf diag_printf
int os_taskq_post_type(const char *task, int type, int n, int *msg) {
    if (held || task[0]!='a' || type!=Q_CALLBACK || n!=3 || msg[1]!=1 || msg[2]!=0)
        ++invalid;
    ++posts;
    if (queue_fail) return -1;
    ++pending;
    return 0;
}
'''

TESTS = r'''
static void reset(void) {
    memset(&diag, 0, sizeof(diag));
    irq=held=invalid=queue_fail=pending=posts=prints=worker=0; now=100;
}
static int drain(void) {
    int budget=16;
    worker=1;
    while (pending && budget--) { --pending; diag_flush(0); }
    worker=0;
    return budget>0 && !invalid && !irq && !held;
}
int check_two_links_handle_reuse_and_buffered_first_cause(void) {
    reset();
    worker=1; rdx_ble_diag_init(); rdx_ble_diag_init(); worker=0;
    CHECK(prints==1);
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x40,0,0);
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x41,0,0);
    CHECK(diag.records[0].connection!=diag.records[1].connection);
    u32 old=diag.records[0].connection, other=diag.records[1].connection;
    CHECK(posts==1 && drain());
    int before=prints;
    irq=1;
    rdx_ble_diag_record(RDX_BLE_DIAG_MIC_CHECK,0x40,0x3d,0x01030404);
    irq=0;
    CHECK(prints==before && !pending && diag.count==1);
    CHECK(diag.records[diag.head].connection==old);
    CHECK(diag.records[diag.head].detail==0x01030404);
    rdx_ble_diag_record(RDX_BLE_DIAG_REMOTE_TERM,0x41,0x13,0);
    rdx_ble_diag_record(RDX_BLE_DIAG_DISCONNECTED,0x40,0x3d,0);
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x40,0,0);
    rdx_ble_diag_record(RDX_BLE_DIAG_HOST_REQUEST,0x41,0x13,0);
    CHECK(diag.records[(diag.head+1)%DIAG_DEPTH].connection==other);
    CHECK(diag.records[(diag.head+2)%DIAG_DEPTH].connection==old);
    CHECK(diag.records[(diag.head+3)%DIAG_DEPTH].connection!=old);
    CHECK(diag.records[(diag.head+4)%DIAG_DEPTH].connection==other);
    CHECK(diag.records[(diag.head+2)%DIAG_DEPTH].reason==0x3d);
    CHECK(drain() && !diag.count);
    CHECK(!diag.queued && !pending && prints==before+6);
    return 0;
}
int check_queue_failure_retains_evidence_and_retries(void) {
    reset(); queue_fail=1;
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x40,0,0);
    rdx_ble_diag_record(RDX_BLE_DIAG_REMOTE_TERM,0x40,0x3d,0);
    rdx_ble_diag_record(RDX_BLE_DIAG_DISCONNECTED,0x40,0x3d,0);
    CHECK(diag.count==3 && diag.post_failed==2 && !diag.queued && !pending);
    CHECK(diag.records[1].event==RDX_BLE_DIAG_REMOTE_TERM);
    queue_fail=0; rdx_ble_diag_flush_request();
    CHECK(drain() && prints==4);
    CHECK(!diag.count && !diag.post_failed);
    return 0;
}
int check_overflow_keeps_first_cause_and_reports_loss(void) {
    reset(); queue_fail=1;
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x40,0,0);
    rdx_ble_diag_record(RDX_BLE_DIAG_MIC_CHECK,0x40,0x3d,4);
    for (unsigned i=0;i<DIAG_DEPTH;++i)
        rdx_ble_diag_record(RDX_BLE_DIAG_RX_ERROR,0x40,0x3d,i);
    rdx_ble_diag_record(RDX_BLE_DIAG_DISCONNECTED,0x40,0x3d,0);
    CHECK(diag.count==DIAG_DEPTH && diag.dropped==3);
    CHECK(diag.records[1].event==RDX_BLE_DIAG_MIC_CHECK);
    u32 old=diag.records[0].connection;
    queue_fail=0; rdx_ble_diag_flush_request(); CHECK(drain());
    CHECK(!diag.dropped && !diag.count && prints==DIAG_DEPTH+1);
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x40,0,0);
    CHECK(diag.records[diag.head].connection!=old);
    CHECK(drain());
    return 0;
}
int check_missing_begin_and_slot_exhaustion_are_explicit(void) {
    reset();
    rdx_ble_diag_record(RDX_BLE_DIAG_REMOTE_TERM,0x40,0x3d,0);
    CHECK(!diag.records[0].began && diag.records[0].connection);
    u32 partial=diag.records[0].connection;
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x40,0,0);
    CHECK(diag.records[1].began && diag.records[1].connection==partial);
    for (unsigned h=0x41;h<0x45;++h)
        rdx_ble_diag_record(RDX_BLE_DIAG_HOST_REQUEST,h,0x13,0);
    CHECK(diag.untracked==1);
    CHECK(!diag.records[5].connection && !diag.records[5].began);
    CHECK(drain());
    return 0;
}
int check_disabled_idle_has_no_work_and_clock_wrap_is_preserved(void) {
    reset();
    for (unsigned i=0;i<10;++i) rdx_ble_diag_flush_request();
    CHECK(!pending && !posts && !prints);
    now=0xfffffff0u;
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x40,0,0);
    now=5;
    rdx_ble_diag_record(RDX_BLE_DIAG_ENC_ACL,0x40,0x3d,0x01030400);
    CHECK(diag.records[0].ms==0xfffffff0u && diag.records[1].ms==5);
    CHECK(diag.records[0].order<diag.records[1].order);
    CHECK(drain());
    int count=posts;
    rdx_ble_diag_flush_request();
    CHECK(posts==count);
    return 0;
}
static void sup_alloc(u16 h, u32 timer) {
    rdx_ble_diag_sup_state(RDX_BLE_SUP_ALLOC,h,0,0,6,600,0,0);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_REGISTER,h,timer,6000,6,600,0,0);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_CONTEXT,h,timer,6000,6,600,0,0);
}
int check_sup_hot_path_no_queue_and_preconnect_state_survives(void) {
    reset(); sup_alloc(0x50,10); sup_alloc(0x51,11);
    struct diag_link *a=sup_link_find(0x50), *b=sup_link_find(0x51);
    CHECK(a && b && a!=b && a->connection!=b->connection);
    u32 old=a->connection;
    now=150;
    for (unsigned i=0;i<1000;++i) {
        rdx_ble_diag_sup_activity(RDX_BLE_SUP_RX,0x50,i,10);
        rdx_ble_diag_sup_activity(RDX_BLE_SUP_EVENT,0x50,i,0);
    }
    CHECK(!posts && !prints && !diag.count && !diag.sup_count && !invalid);
    CHECK(a->sup.rx_calls==1000 && a->sup.reset_calls==1000);
    CHECK(a->sup.event_calls==1000 && a->sup.eligible_ms==150);
    CHECK(!b->sup.rx_calls && b->sup.timer_id==11);
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x50,0,0);
    CHECK(a->connection==old && a->sup.rx_calls==1000 && a->began);
    CHECK(drain());
    now=200;
    rdx_ble_diag_sup_state(RDX_BLE_SUP_TIMEOUT_ENTER,0x50,10,0,7,600,1001,0x04010000);
    CHECK(diag.sup_count==1 && !pending);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_REMOVE,0x50,10,0,7,600,1001,0);
    rdx_ble_diag_sup_activity(RDX_BLE_SUP_RX,0x50,1002,10);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_TIMEOUT_REASON,0x50,0,8,7,600,1001,0);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_COMMIT,0x50,0,8,7,600,1001,0);
    CHECK(diag.sup_count==3);
    CHECK(diag.sup_records[0].observed_timer==10 && diag.sup_records[0].ms==200);
    CHECK(diag.sup_records[1].observed_timer==0 && diag.sup_records[1].timer.timer_id==10);
    CHECK(diag.sup_records[2].timer.rx_calls==1000 && diag.sup_records[2].timer.reset_ms==150);
    CHECK(diag.sup_records[0].order<diag.sup_records[2].order);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_FREE,0x50,0,0,8,600,1001,0);
    rdx_ble_diag_record(RDX_BLE_DIAG_DISCONNECTED,0x50,8,0);
    CHECK(drain() && !diag.sup_count && !a->active && !invalid);
    return 0;
}
int check_sup_period_result_timer_reuse_and_wrap(void) {
    reset(); now=0; sup_alloc(0x50,10);
    struct sup_state *s=&sup_link_find(0x50)->sup;
    CHECK((s->valid & SUP_REGISTERED) && s->register_ms==0);
    now=10;
    rdx_ble_diag_sup_state(RDX_BLE_SUP_UPDATE_TEMP,0x50,10,40000,7,600,2,0);
    rdx_ble_diag_sup_modify_result(10,40000,-7);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_PERIOD,0x50,10,40000,7,600,2,0);
    CHECK(s->period_ms==40000 && s->origin==RDX_BLE_SUP_UPDATE_TEMP);
    CHECK(s->modify_result==-7 && (s->valid & SUP_MODIFY));
    /* Requested timing is retained even on an opaque/nonzero OS result. */
    CHECK(s->basis_ms==10 && s->modify_period_ms==40000);
    rdx_ble_diag_sup_modify_result(999,123,0);
    CHECK(s->modify_result==-7 && !diag.sup_count && !posts);
    now=20;
    rdx_ble_diag_sup_state(RDX_BLE_SUP_UPDATE_RESTORE,0x50,10,6000,7,600,3,0);
    rdx_ble_diag_sup_modify_result(10,6000,0);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_PERIOD,0x50,10,6000,7,600,3,0);
    CHECK(s->period_ms==6000 && s->modify_ms==20 && s->origin==RDX_BLE_SUP_UPDATE_RESTORE);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_REMOVE,0x50,10,0,7,600,3,0);
    CHECK(!(s->valid & SUP_LIVE) && s->removed_id==10 && s->remove_ms==20);
    rdx_ble_diag_sup_modify_result(10,300,5);
    CHECK(s->modify_result==0); /* Removed timer cannot match another owner's reuse. */
    rdx_ble_diag_sup_state(RDX_BLE_SUP_REGISTER,0x50,20,6000,7,600,4,0);
    CHECK(s->timer_gen==2 && s->timer_id==20 && !(s->valid & SUP_MODIFY));
    now=0xfffffff0u;
    rdx_ble_diag_sup_activity(RDX_BLE_SUP_RX,0x50,(3u<<16)|65535u,20);
    CHECK(s->eligible_rx==1 && s->reset_ms==now && s->rx_event==65535);
    now=5;
    rdx_ble_diag_sup_activity(RDX_BLE_SUP_EVENT,0x50,0,0);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_TIMEOUT_ENTER,0x50,20,0,7,600,0,0);
    CHECK(diag.sup_records[0].ms-diag.sup_records[0].timer.reset_ms==21);
    CHECK(diag.sup_records[0].timer.event_counter==0 && !invalid);
    return 0;
}
int check_sup_rx_classification_and_missing_timer(void) {
    reset(); sup_alloc(0x50,0); /* Preserve failed register, no timer is live. */
    struct sup_state *s=&sup_link_find(0x50)->sup;
    CHECK(s->valid & SUP_REGISTERED); CHECK(!(s->valid & SUP_LIVE));
    rdx_ble_diag_sup_activity(RDX_BLE_SUP_RX,0x50,7,0);
    rdx_ble_diag_sup_activity(RDX_BLE_SUP_RX,0x50,(3u<<16)|8,0);
    rdx_ble_diag_sup_activity(RDX_BLE_SUP_RX,0x50,(4u<<16)|9,0);
    CHECK(s->rx_calls==3 && s->eligible_rx==2 && s->rx_other==1);
    CHECK(s->rx_no_timer==2 && !s->reset_calls && !(s->valid & SUP_RESET));
    rdx_ble_diag_sup_activity(RDX_BLE_SUP_RX,0x50,10,99);
    CHECK(s->mismatches==1 && s->reset_calls==1);
    rdx_ble_diag_sup_activity(RDX_BLE_SUP_RX,0x99,11,99);
    CHECK(diag.sup_untracked==1 && !sup_link_find(0x99));
    CHECK(!diag.sup_count && !posts && !invalid);
    return 0;
}
int check_sup_frozen_generation_queue_failure_overflow(void) {
    reset(); sup_alloc(0x50,10);
    rdx_ble_diag_record(RDX_BLE_DIAG_CONNECT,0x50,0,0); CHECK(drain());
    u32 first=sup_link_find(0x50)->connection;
    now=123;
    rdx_ble_diag_sup_state(RDX_BLE_SUP_TIMEOUT_ENTER,0x50,10,0,7,600,5,0);
    rdx_ble_diag_sup_state(RDX_BLE_SUP_TIMEOUT_REASON,0x50,0,0x122,7,600,5,0);
    CHECK(diag.sup_records[1].value==0x122);
    queue_fail=1;
    rdx_ble_diag_record(RDX_BLE_DIAG_DISCONNECTED,0x50,0x22,0);
    CHECK(!pending && diag.sup_count==2 && diag.post_failed==1);
    sup_alloc(0x50,10); /* Reused handle AND timer ID, distinct connection. */
    CHECK(sup_link_find(0x50)->connection!=first);
    CHECK(!sup_link_find(0x50)->sup.frozen && !sup_link_find(0x50)->sup.rx_calls);
    CHECK(diag.sup_records[0].connection==first && diag.sup_records[0].ms==123);
    for (unsigned i=0;i<SUP_DEPTH;++i)
        rdx_ble_diag_sup_state(RDX_BLE_SUP_COMMIT,0x50,10,8,7,600,6,0);
    CHECK(diag.sup_count==SUP_DEPTH && diag.sup_dropped==2);
    CHECK(diag.sup_records[0].connection==first);
    queue_fail=0; rdx_ble_diag_flush_request();
    CHECK(drain() && !diag.sup_count && !diag.sup_dropped && !invalid);
    return 0;
}
'''


def check_recorder(directory):
    header = re.sub(r'^#include[^\n]*\n', '', (DIR / 'rdx_ble_diag.h').read_text(), flags=re.M)
    source = re.sub(r'^#include[^\n]*\n', '', (DIR / 'rdx_ble_diag.c').read_text(), flags=re.M)
    path = directory / 'recorder.c'
    path.write_text('#define TCFG_T2620_BLE_DISCONNECT_DIAG_ENABLE 1\n' +
                    STUBS + header + source + TESTS, encoding='utf-8')
    run_c_checks(path, re.findall(r'int (check_\w+)\(void\)', TESTS), native=True)
    disabled = directory / 'disabled.c'
    disabled.write_text('#define TCFG_T2620_BLE_DISCONNECT_DIAG_ENABLE 0\n' +
                        STUBS + header + source + r'''
int check_off(void) {
    rdx_ble_diag_init();
    rdx_ble_diag_record(2,0x40,0x3d,4);
    rdx_ble_diag_app_request(0x40,1);
    rdx_ble_diag_sup_state(1,0x40,0,0,6,600,0,0);
    rdx_ble_diag_sup_activity(1,0x40,3u<<16,12);
    rdx_ble_diag_sup_modify_result(12,40000,-1);
    rdx_ble_diag_flush_request();
    return rdx_ble_diag_enabled || posts || prints || irq || held;
}
''', encoding='utf-8')
    run_c_checks(disabled, ['check_off'], native=True)


def check_disconnect_snapshot(directory):
    server = (DIR / 'rdx_ble_server.c').read_text(encoding='utf-8-sig')
    header = (DIR / 'rdx_ble_server.h').read_text(encoding='utf-8-sig')
    tx_type = re.search(r'typedef struct \{[^{}]*\} rdx_ble_tx_window_t;', header).group()
    helper = function(server, 'rdx_ble_server_disconnect_snapshot')
    handler = function(server, 'rdx_ble_server_phase0a_link_disconnected')
    # Reject invalid/stale links before snapshot; capture before any detach/release.
    assert handler.index('if (!link)') < handler.index('rdx_ble_server_disconnect_snapshot(')
    assert handler.index('rdx_ble_server_disconnect_snapshot(') < handler.index('rdx_hogp_on_disconnected(')
    assert handler.index('rdx_ble_server_disconnect_snapshot(') < handler.index('rdx_ble_server_phase2_rdx_detach(')
    for field in ('interval_1p25ms=', 'latency=', 'timeout_10ms=', 'target_latency=',
                  'sleep_block=', 'submitted=', 'attempt_age='):
        assert field in helper
    for forbidden in ('sys_timer_add', 'sys_timeout_add', 'ble_op_', 'app_ble_att_send_data'):
        assert forbidden not in helper
    stubs = r'''
#include <stdarg.h>
typedef unsigned char u8;
typedef unsigned short u16;
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define TCFG_T2620_CONN_SLAVE_LATENCY 0
typedef struct {
    u16 con_handle, conn_interval, conn_latency, supervision_timeout, mtu_size;
    u32 slot_generation;
    u8 encrypted;
} rdx_ble_link_state_t;
static u8 g_rdx_ble_dual_acl_sleep_blocked=1, g_rdx_ble_send_retry_pending=1;
static u16 g_rdx_ble_send_pending_count=3;
static int owner, takes, prints;
static u32 span, age, now=1000;
static rdx_ble_tx_window_t window;
u8 rdx_ble_session_active_count(void) { return 2; }
u8 rdx_ble_session_link_is_rdx(const rdx_ble_link_state_t *l) { return owner; }
u8 rdx_ble_session_link_is_hid(const rdx_ble_link_state_t *l) { return 1; }
u32 sys_timer_get_ms(void) { return now; }
void rdx_ble_server_tx_window_take(rdx_ble_tx_window_t *out) {
    *out=window; ++takes;
    rdx_ble_tx_window_t empty={0}; window=empty;
}
void r_printf(const char *format, ...) { ++prints; }
int snapshot_printf(const char *format, ...) {
    va_list ap; va_start(ap,format);
    for (int i=0;i<6;++i) (void)va_arg(ap,unsigned int);
    span=va_arg(ap,unsigned int); age=va_arg(ap,unsigned int);
    va_end(ap); ++prints; return 0;
}
#define printf snapshot_printf
'''
    checks = r'''
int check_snapshot(void) {
    rdx_ble_link_state_t link={0x51,12,49,600,514,7,1};
    owner=1;
    window.bytes=18144; window.chunks=36; window.buffer_full=100;
    window.first_ms=800; window.last_ms=900;
    rdx_ble_server_disconnect_snapshot(&link,8);
#if TCFG_T2620_BLE_DISCONNECT_DIAG_ENABLE
#if TCFG_T2620_BLE_TX_BLOCK_TRACE_ENABLE
    CHECK(prints==2 && takes==1 && span==100 && age==100);
    CHECK(!window.chunks && link.conn_latency==49 && link.con_handle==0x51);
    owner=0;
    window.bytes=123; window.chunks=1;
    rdx_ble_server_disconnect_snapshot(&link,8);
    CHECK(prints==3 && takes==1 && window.bytes==123);
    owner=1; window.chunks=0; window.bytes=0;
    rdx_ble_server_disconnect_snapshot(&link,8);
    CHECK(prints==5 && takes==2 && !span && !age);
    window.buffer_full=2; window.first_ms=0xfffffff0u; window.last_ms=5; now=20;
    rdx_ble_server_disconnect_snapshot(&link,8);
    CHECK(prints==7 && takes==3 && span==21 && age==15);
#else
    CHECK(prints==1 && !takes);
#endif
#else
    CHECK(!prints && !takes);
#endif
    return 0;
}
'''
    for diag, trace in ((1, 1), (1, 0), (0, 1), (0, 0)):
        path = directory / f'snapshot-{diag}-{trace}.c'
        path.write_text(f'#define TCFG_T2620_BLE_DISCONNECT_DIAG_ENABLE {diag}\n'
                        f'#define TCFG_T2620_BLE_TX_BLOCK_TRACE_ENABLE {trace}\n' +
                        'typedef unsigned int u32;\n' + tx_type + stubs + helper + checks,
                        encoding='utf-8')
        run_c_checks(path, ['check_snapshot'], native=True)


def ps(*args, ok=True):
    p = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                        '-File', str(PATCH), *map(str, args)],
                       capture_output=True, text=True, timeout=120)
    if ok and p.returncode:
        raise AssertionError(p.stdout + p.stderr)
    if not ok:
        assert p.returncode, 'bad archive unexpectedly accepted'
    return p


def check_patch(directory):
    assert hashlib.sha256(VENDOR.read_bytes()).hexdigest() == PIN
    output = directory / 'btctrler_diag.a'
    evidence = directory / 'evidence'
    ps('-OutputArchive', output, '-EvidenceDirectory', evidence)
    manifest = json.loads(output.with_suffix('.a.json').read_text(encoding='utf-8-sig'))
    expected = {'bb_le_link.c.o': 1, 'll_slave.c.o': 2, 'll_master.c.o': 1,
                'llcp_sm_common.c.o': 4, 'link_layer.c.o': 18,
                'll_events.c.o': 2, 'hci_ll.c.o': 1,
                'llcp_sm_conn_update.c.o': 2, 'llcp_sm_conn_subrate.c.o': 1,
                'sys_timer.c.o': 1}
    assert manifest['revision'] == 2
    assert manifest['hooks_per_member'] == expected
    for original in evidence.glob('*.original.ll'):
        member = original.name.removesuffix('.original.ll')
        patched = (evidence / f'{member}.patched.ll').read_text()
        # Strip only injected lines before the appended helper/declarations.
        vendor_body = patched[:patched.index('\ndeclare void @rdx_ble_diag_')]
        restored = re.sub(r'^  (?:%diag_\w+ = .*|call void @rdx_ble_diag_\w+\(.*)\n',
                          '', vendor_body, flags=re.M)
        assert restored == original.read_text(), member
    # Verify capture precedes procedure cleanup, and remote reason is unchanged.
    common = (evidence / 'llcp_sm_common.c.o.patched.ll').read_text()
    assert 'i32 7, i32 %diag_reason' in common
    assert common.index('i32 7, i32 %diag_reason') < common.index(
        '  store i8 %6, i8* %7, align 1, !dbg !3900')
    slave = (evidence / 'll_slave.c.o.patched.ll').read_text()
    assert slave.index('i32 6, i32 61, i32 12') < slave.index(
        '  tail call void @llcp_sm_end(%struct.le_link* nonnull %3) #3, !dbg !992')
    layer = (evidence / 'link_layer.c.o.patched.ll').read_text()
    def ir_function(name):
        return re.search(r'^define [^\n]*@' + name + r'\([^\n]*\{.*?^\}',
                         layer, re.M | re.S).group()
    timer = ir_function('ll_conn_supervision_timer_handler')
    assert timer.index('i32 6, i32 -1, i32 0)') < timer.index(
        '%5 = load %struct.ble_operation*')
    assert timer.index('i32 7, i32 -1, i32 %diag_sup_reason_value)') < timer.index(
        'tail call void @ll_disconnect_process')
    assert 'icmp ne i64 %24, 0' in timer and 'zext i8 %33 to i32' in timer
    rx = ir_function('conn_rx_probe_handler')
    assert '%diag_rx_reset_id = phi i32 [ %43, %45 ], [ 0, %40 ], [ 0, %35 ]' in rx
    assert 'zext i8 %38 to i32' in rx
    assert rx.index('%48 = phi') < rx.index('%diag_rx_reset_id = phi') < rx.index('%49 = and')
    assert 'i32 1, i32 %diag_rx_sample, i32 %diag_rx_reset_id)' in rx
    assert 'i32 2, i32 %diag_event_sample, i32 0)' in ir_function('conn_evt_probe_handler')
    for helper in ('rdx_ble_diag_sup_link', 'rdx_ble_diag_sup_activity_link'):
        body = ir_function(helper)
        assert body.index('@rdx_ble_diag_enabled') < body.index('br i1 %diag_run')
        assert body.index('br i1 %diag_run') < body.index('getelementptr inbounds %struct.le_link')
    # Snapshot APIs remain observational; hot probes cannot queue/print/touch timers.
    source = (DIR / 'rdx_ble_diag.c').read_text(encoding='utf-8-sig')
    hot = function(source, 'rdx_ble_diag_sup_activity')
    for forbidden in ('printf(', 'os_taskq_', 'diag_record(', 'sup_capture(',
                      'sys_timer_add(', 'sys_timer_modify(', 'sys_timer_re_run('):
        assert forbidden not in hot
    # All non-instrumented archive members must remain exactly unchanged.
    ar = 'C:/JL/pi32/bin/llvm-ar.exe'
    before, after = directory / 'vendor', directory / 'diag'
    before.mkdir()
    after.mkdir()
    subprocess.run([ar, 'x', str(VENDOR)], cwd=before, check=True, capture_output=True)
    subprocess.run([ar, 'x', str(output)], cwd=after, check=True, capture_output=True)
    assert {p.name for p in before.iterdir()} == {p.name for p in after.iterdir()}
    changed = {p.name for p in before.iterdir() if p.read_bytes() != (after / p.name).read_bytes()}
    assert changed == set(expected)
    bad = directory / 'unsupported.a'
    bad.write_bytes(b'unsupported controller')
    old_output = output.read_bytes()
    ps('-InputArchive', bad, '-OutputArchive', output, ok=False)
    assert output.read_bytes() == old_output
    ps('-InputArchive', VENDOR, '-OutputArchive', VENDOR, ok=False)
    assert hashlib.sha256(VENDOR.read_bytes()).hexdigest() == PIN


def main():
    with tempfile.TemporaryDirectory(prefix='ble-diag-test-') as name:
        directory = Path(name)
        check_recorder(directory)
        check_disconnect_snapshot(directory)
        check_patch(directory)
    print('BLE diagnostic recorder, disconnect snapshot and controller patch checks passed.')


if __name__ == '__main__':
    main()
