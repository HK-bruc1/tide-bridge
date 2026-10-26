"""Focused connection-tone behavior; reuse registry code, mock HID/OS/audio."""
import re
import tempfile
from pathlib import Path

from host_c_test_lib import ROOT, function, run_c_checks

BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'


def source(name):
    return (BASE / name).read_text(encoding='utf-8')


STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
void *memset(void *, int, unsigned long long);
void *memcpy(void *, const void *, unsigned long long);
int strcmp(const char *, const char *);
#define NULL ((void *)0)
#define Q_CALLBACK 1
#define r_printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
enum stream_event { STREAM_EVENT_INIT, STREAM_EVENT_START, STREAM_EVENT_STOP };
static struct { int goto_poweroff_flag; } app_var;
static u8 g_rdx_ble_exiting, hid_ready;
static int wrappers[2], core, irq, context_errors, queue_fail, play_fail;
static int qr, qw, tr, tw, starts, drops;
static u32 queue[32];
static struct { void *priv; int (*cb)(void *, enum stream_event); } tones[32];
static void local_irq_disable(void) { ++irq; }
static void local_irq_enable(void) { --irq; }
static u8 rdx_hogp_keyboard_is_ready(void) { return hid_ready; }
static void rdx_ble_server_conn_tone_play(u32 ticket);
static int os_taskq_post_type(const char *task, int type, int n, int *msg) {
    if (irq || strcmp(task,"app_core") || type!=Q_CALLBACK || n!=3 || msg[1]!=1 ||
        msg[0]!=(int)rdx_ble_server_conn_tone_play) ++context_errors;
    if (queue_fail) return -1;
    queue[qw++]=msg[2]; return 0;
}
static const struct { const char *conn; } tone_files={"tone_en/conn.*"};
#define get_tone_files() (&tone_files)
static int play_tone_file_callback(const char *file, void *priv,
                                    int (*cb)(void *, enum stream_event)) {
    if (irq || !core || strcmp(file,tone_files.conn)) ++context_errors;
    if (play_fail) return -1;
    tones[tw].priv=priv; tones[tw++].cb=cb; return 0;
}
'''

TESTS = r'''
static void drain_core(void) {
    core=1;
    while (qr<qw) rdx_ble_server_conn_tone_play(queue[qr++]);
    core=0;
}
static void drain_audio(void) {
    while (tr<tw) {
        int i=tr++;
        if (tones[i].cb(tones[i].priv,STREAM_EVENT_INIT)) ++drops;
        else ++starts;
        tones[i].cb(tones[i].priv,STREAM_EVENT_STOP);
    }
}
static void boot(void) {
    app_var.goto_poweroff_flag=g_rdx_ble_exiting=hid_ready=0;
    core=irq=context_errors=queue_fail=play_fail=0;
    qr=qw=tr=tw=starts=drops=0;
    s_rdx_runtime_consumed_this_boot=0;
    rdx_ble_server_conn_tone_reset();
    rdx_ble_session_transport_init(&wrappers[0],&wrappers[1]);
}
static rdx_ble_link_state_t *connect(int slot) {
    return rdx_ble_session_link_accept(&wrappers[slot],0x50+slot);
}
static int attach(rdx_ble_link_state_t *link, int hid) {
    int result=hid ? rdx_ble_session_claim_hid(link,link->slot_generation) :
                     rdx_ble_session_claim_rdx(link,link->slot_generation);
    if (!result) rdx_ble_server_conn_tone_post(link,hid);
    return result;
}
static int release_rdx(rdx_ble_link_state_t *link) {
    return rdx_ble_session_rdx_runtime_begin_quiesce(link) &&
           rdx_ble_session_release_rdx(link,link->slot_generation) &&
           rdx_ble_session_rdx_runtime_barrier_arrive() &&
           rdx_ble_session_rdx_runtime_rearm();
}
int test_both_orders_and_dedup(void) {
    for (int same=0;same<2;++same) for (int hid_first=0;hid_first<2;++hid_first) {
        boot(); rdx_ble_link_state_t *rdx=connect(0), *hid=same ? rdx : connect(1);
        rdx_ble_server_conn_tone_post(rdx,0);
        CHECK(!attach(hid,1) && !qw); /* Owning HID is not enough: wait for ready. */
        hid_ready=1;
        if (hid_first) CHECK(!attach(hid,1) && !attach(rdx,0));
        else CHECK(!attach(rdx,0) && !attach(hid,1));
        for (int i=0;i<3;++i) {
            CHECK(!attach(rdx,0) && !attach(hid,1));
            hid_ready=0; CHECK(!attach(hid,1));
            hid_ready=1; CHECK(!attach(hid,1));
        }
        CHECK(qw==2);
        drain_core(); drain_audio();
        CHECK(starts==2 && !drops && !context_errors && !irq);
    }
    return 0;
}
int test_rdx_release_keeps_hid_prompt(void) {
    for (int queued=0;queued<2;++queued) {
        boot(); rdx_ble_link_state_t *link=connect(0); hid_ready=1;
        CHECK(!attach(link,1) && !attach(link,0));
        if (queued) drain_core();
        CHECK(release_rdx(link));
        CHECK(!attach(link,0) && !attach(link,1) && qw==3);
        drain_core(); drain_audio();
        CHECK(starts==2 && drops==queued && !context_errors);
    }
    return 0;
}
int test_disconnect_reuse_discards_only_old_service(void) {
    for (int queued=0;queued<2;++queued) for (int hid_lost=0;hid_lost<2;++hid_lost) {
        boot(); rdx_ble_link_state_t *rdx=connect(0), *hid=connect(1); hid_ready=1;
        CHECK(!attach(rdx,0) && !attach(hid,1));
        if (queued) drain_core();
        int slot=hid_lost ? 1 : 0;
        if (!hid_lost) CHECK(release_rdx(rdx));
        CHECK(rdx_ble_session_link_release(&wrappers[slot],0x50+slot));
        CHECK(!attach(connect(slot),hid_lost) && qw==3);
        drain_core(); drain_audio();
        CHECK(starts==2 && drops==queued && !context_errors);
    }
    return 0;
}
int test_shutdown_and_reset_discard_old_work(void) {
    for (int queued=0;queued<2;++queued) for (int mode=0;mode<3;++mode) {
        boot(); rdx_ble_link_state_t *link=connect(0); hid_ready=1;
        CHECK(!attach(link,0) && !attach(link,1));
        if (queued) drain_core();
        if (mode==0) app_var.goto_poweroff_flag=1;
        if (mode==1) g_rdx_ble_exiting=1;
        if (mode==2) {
            rdx_ble_server_conn_tone_reset();
            rdx_ble_session_transport_deinit();
            rdx_ble_session_transport_init(&wrappers[0],&wrappers[1]);
            CHECK(!attach(connect(0),1));
        }
        drain_core(); drain_audio();
        CHECK(starts==(mode==2) && drops==2*queued && !context_errors);
    }
    return 0;
}
int test_failures_do_not_block_or_repeat(void) {
    boot(); rdx_ble_link_state_t *link=connect(0); hid_ready=1;
    queue_fail=1; CHECK(!attach(link,0) && !attach(link,1) && !qw);
    queue_fail=0; CHECK(!attach(link,0) && !attach(link,1) && qw==2);
    play_fail=1; drain_core(); CHECK(!tw && !context_errors);
    play_fail=0; CHECK(!attach(link,0) && !attach(link,1) && qw==2);
    return 0;
}
'''


def main():
    server = source('rdx_ble_server.c')
    state = re.search(r'typedef struct\s*\{[^{}]*\}\s*rdx_ble_conn_tone_t;', server)[0]
    state += '\n' + '\n'.join(re.findall(
        r'^static[^;\n]*\bg_rdx_ble_conn_tone\w*[^;]*;', server, re.M))
    names = re.findall(r'^static\s+\w+\s+(rdx_ble_server_conn_tone_\w+)\(', server, re.M)
    registry = '\n'.join(re.sub(r'^#include[^\n]*', '', source(name), flags=re.M)
                         for name in ('rdx_ble_session.h', 'rdx_ble_session.c'))
    code = STUBS + registry + state + '\n' + ''.join(function(server, n) for n in names) + TESTS
    # Check service entry hooks once, without pinning call counts or expressions.
    for name in ('rdx_ble_server_phase2_rdx_attach', 'rdx_ble_server_hid_write',
                 'rdx_ble_server_hid_encryption_change'):
        assert re.search(r'\brdx_ble_server_conn_tone_\w+\s*\(', function(server, name)), name
    checks = re.findall(r'int (test_\w+)\(void\)', TESTS)
    with tempfile.TemporaryDirectory(prefix='connection-tone-') as tmp:
        path = Path(tmp) / 'checks.c'
        path.write_text(code, encoding='utf-8')
        run_c_checks(path, checks, native=True)
    print(f'Connection tones: {len(checks)} focused C checks passed (HID/OS/audio mocked).')


if __name__ == '__main__':
    main()
