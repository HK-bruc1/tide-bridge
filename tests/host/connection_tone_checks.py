"""RDX-only connection tones with real BLE entry points and mocked boundaries."""
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
#define y_printf(...) ((void)0)
#define printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
enum stream_event { STREAM_EVENT_INIT, STREAM_EVENT_START, STREAM_EVENT_STOP };
static struct { int goto_poweroff_flag; } app_var;
static u8 g_rdx_ble_exiting, hid_ready, hid_connected, hid_encrypted;
static u8 hid_subscribed, hid_saved_subscription;
static u16 hid_con_handle;
static int composite_full, composite_events, composite_wait, composite_clears;
static int barrier_fail, cleanup_calls, abort_calls, recv_calls;
static u32 composite_release_epoch;
static int wrappers[2], core, irq, context_errors, queue_fail, play_fail;
static int qr, qw, tr, tw, starts, drops, hid_writes, hid_encryptions;
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

ENTRY_STUBS = r'''
#define FALSE 0
#define RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR 0x0e
#define HID_CCC_HANDLE 0x001b
#define RECORD_STATE_STOP 0
#define TCFG_RDX_HOGP_ENABLE 1
#define RDX_DISCONNECTION_COMPLETE_MIN_SIZE 5
#define RDX_LIFECYCLE_BARRIER_PACKET_SIZE 64
#define RDX_LIFECYCLE_CUSTOM_CMD "__rdxlc"
enum rdx_detach_cause { RDX_DETACH_PHYSICAL, RDX_DETACH_LOGICAL_COMMAND };
typedef struct { int run; } RecordStatus;
static struct { u16 ble_mtu_size, ble_con_handle; u8 ble_conn, ccc_configured, stream_tx_ready; }
    g_rdx_ble_server_info;
static volatile u8 g_rdx_lifecycle_barrier_armed;
static char g_rdx_lifecycle_barrier_value[17];
static void *g_rdx_ble_phase0a_disconnect_pending_hdl;
int sprintf(char *, const char *, ...);
unsigned long long strlen(const char *);
static u32 rand32(void) { static u32 value=1; return value++; }
static void rdx_ble_server_rdx_runtime_try_rearm(void) {}
static void rdx_ble_entry_lock(void) { local_irq_disable(); }
static void rdx_ble_entry_unlock(void) { local_irq_enable(); }
static void rdx_ble_entry_composite_owned_locked(void) {
    if (!irq) ++context_errors;
    if (!composite_full) { composite_full=1; ++composite_events; }
}
static void rdx_ble_entry_changed(void) { if (irq) ++context_errors; }
static void rdx_ble_entry_composite_releasing(u32 epoch) {
    composite_wait=1; composite_full=0; composite_release_epoch=epoch;
}
static void rdx_ble_entry_composite_disconnected(void) {
    composite_full=composite_wait=0; ++composite_clears;
}
static void rdx_ble_server_set_conn_handle(u16 con_handle) { g_rdx_ble_server_info.ble_con_handle=con_handle; }
static void rdx_ble_server_reset_send_fail_cnt(void) {}
static void rdx_session_control_attach(rdx_ble_link_state_t *link) { (void)link; }
static void rdx_session_control_reset(void) {}
static void rdx_flashnote_cancel_for_token(const rdx_ble_async_token_t *token) { (void)token; }
static void rdx_ble_server_transfer_param_cancel(void) {}
static void rdx_dut_test_session_revoke(void) {}
static void rdx_ble_server_syn_data_timers_cancel(void) {}
static void rdx_ble_server_rdx_send_pending_reset(void) {}
static void rdx_ble_server_rdx_disconnected_cleanup_internal(void) { ++cleanup_calls; }
static void rdx_ble_server_rdx_session_abort(void) { ++abort_calls; }
static void rdx_ble_server_rdx_send_worker_quiesce(void) {}
static int rdx_protocol_packet_recv(const char *packet, int len) {
    ++recv_calls; return barrier_fail ? len-1 : len;
}
static int rdx_ble_server_phase0a_event_matches(void *hdl, u16 con, const char *event) {
    (void)event; return rdx_ble_session_find(hdl,con)!=NULL;
}
static rdx_ble_link_state_t *rdx_ble_server_phase0b_link_find(void *hdl, u16 con) {
    return rdx_ble_session_find(hdl,con);
}
static u16 hci_event_disconnection_complete_get_connection_handle(const u8 *p) { return p[3] | p[4]<<8; }
static u8 hci_event_disconnection_complete_get_status(const u8 *p) { return p[2]; }
static u8 hci_event_disconnection_complete_get_reason(const u8 *p) { return p[5]; }
static void rdx_hogp_on_disconnected(u16 con) {
    if(con!=hid_con_handle) ++context_errors;
    hid_connected=hid_ready=hid_encrypted=hid_subscribed=0;
}
static void multi_att_clear_ccc_config(u16 con) { (void)con; }
static void rdx_ble_server_phase0a_connect_adv_restart_cancel(void) {}
static void rdx_hogp_input_invalidate(void) {}
static void rdx_peripheral_power_vdd_ble_links_changed_notify(void) {}
static void rdx_ble_server_phase0b_adv_token_capture(void *hdl) { (void)hdl; }
static void rdx_ble_server_disconnected_adv_restart_schedule(void) {}
static void rdx_ble_server_rdx_detach_common(rdx_ble_link_state_t *link, enum rdx_detach_cause cause);
static RecordStatus *rdx_record_get_status(void) { return NULL; }
static void rdx_record_online_session_bind_current(void) {}
static void rdx_ble_server_rdx_connected_handle(void) {}
static void rdx_ble_server_conn_param_changed(void) {}
static u8 rdx_hogp_keyboard_is_connected(void) { return hid_connected; }
static u8 rdx_hogp_peer_has_persisted_subscription(u16 con_handle) {
    (void)con_handle; return hid_saved_subscription;
}
static void rdx_hogp_on_connected_with_hdl(void *hdl, u16 con_handle, u8 encrypted) {
    (void)hdl;
    hid_con_handle=con_handle; hid_connected=1; hid_encrypted=encrypted;
    hid_subscribed=hid_saved_subscription;
    hid_ready=hid_encrypted && hid_subscribed;
}
static void rdx_hogp_on_encryption_change(u16 con_handle, u8 encrypted, u8 status) {
    if (!hid_connected || con_handle!=hid_con_handle) ++context_errors;
    ++hid_encryptions; hid_encrypted=encrypted && !status;
    hid_ready=hid_encrypted && hid_subscribed;
}
static int rdx_hogp_att_write(u16 con_handle, u16 handle, u16 transaction_mode,
                              u16 offset, u8 *buffer, u16 buffer_size) {
    if (!hid_connected || con_handle!=hid_con_handle || handle!=HID_CCC_HANDLE ||
        transaction_mode || offset || !buffer || buffer_size!=2) {
        ++context_errors; return -1;
    }
    ++hid_writes; hid_subscribed=buffer[0] && !buffer[1];
    hid_ready=hid_encrypted && hid_subscribed;
    return 0;
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
    app_var.goto_poweroff_flag=g_rdx_ble_exiting=hid_ready=hid_connected=0;
    hid_encrypted=hid_subscribed=hid_saved_subscription=0;
    hid_con_handle=0; hid_writes=hid_encryptions=0;
    composite_full=composite_events=composite_wait=composite_clears=0;
    composite_release_epoch=barrier_fail=cleanup_calls=abort_calls=recv_calls=0;
    g_rdx_lifecycle_barrier_armed=0; g_rdx_ble_phase0a_disconnect_pending_hdl=NULL;
    memset(g_rdx_lifecycle_barrier_value,0,sizeof(g_rdx_lifecycle_barrier_value));
    memset(&g_rdx_ble_server_info,0,sizeof(g_rdx_ble_server_info));
    core=irq=context_errors=queue_fail=play_fail=0;
    qr=qw=tr=tw=starts=drops=0;
    s_rdx_runtime_consumed_this_boot=0;
    rdx_ble_server_conn_tone_reset();
    rdx_ble_session_transport_init(&wrappers[0],&wrappers[1]);
}
static rdx_ble_link_state_t *connect(int slot) {
    return rdx_ble_session_link_accept(&wrappers[slot],0x50+slot);
}
static void disconnect_hid(void) {
    hid_ready=hid_connected=hid_encrypted=hid_subscribed=0;
}
static int attach(rdx_ble_link_state_t *link, int hid) {
    return hid ? rdx_ble_server_phase2_hid_attach(link) :
                 rdx_ble_server_phase2_rdx_attach(link);
}
static void encrypt_hid(rdx_ble_link_state_t *link, u8 encrypted) {
    link->encrypted=encrypted;
    rdx_ble_server_hid_encryption_change(link,encrypted,0);
}
static int subscribe_hid(rdx_ble_link_state_t *link, u8 enabled) {
    u8 value[2]={enabled,0};
    return rdx_ble_server_hid_write(link,HID_CCC_HANDLE,0,0,value,sizeof(value));
}
static int ready_hid(rdx_ble_link_state_t *link) {
    CHECK(!attach(link,1));
    encrypt_hid(link,1);
    CHECK(!subscribe_hid(link,1) && hid_ready);
    return 0;
}
static int release_rdx(rdx_ble_link_state_t *link) {
    return rdx_ble_session_rdx_runtime_begin_quiesce(link) &&
           rdx_ble_session_release_rdx(link,link->slot_generation) &&
           rdx_ble_session_rdx_runtime_barrier_arrive() &&
           rdx_ble_session_rdx_runtime_rearm();
}
int test_hid_only_silent_including_restored_subscription(void) {
    boot(); rdx_ble_link_state_t *hid=connect(0);
    CHECK(!ready_hid(hid) && hid_encryptions==1 && hid_writes==1 && !qw);
    for (int i=0;i<3;++i) {
        CHECK(!subscribe_hid(hid,0) && !hid_ready);
        CHECK(!subscribe_hid(hid,1) && hid_ready);
        encrypt_hid(hid,0); CHECK(!hid_ready);
        encrypt_hid(hid,1); CHECK(hid_ready);
        CHECK(!attach(hid,1) && !qw);
    }
    CHECK(rdx_ble_session_link_release(&wrappers[0],0x50));
    disconnect_hid(); hid_saved_subscription=1; hid=connect(0);
    encrypt_hid(hid,1); /* Restored subscription claims HID on encryption. */
    CHECK(hid_ready && rdx_ble_session_link_is_hid(hid) && !qw);
    drain_core(); drain_audio();
    CHECK(!starts && !drops && !context_errors && !irq);
    return 0;
}
int test_both_orders_and_rdx_dedup(void) {
    for (int same=0;same<2;++same) for (int hid_first=0;hid_first<2;++hid_first) {
        boot(); rdx_ble_link_state_t *rdx=connect(0), *hid=same ? rdx : connect(1);
        rdx_ble_server_conn_tone_post(rdx,0);
        CHECK(!qw); /* No RDX owner before attachment. */
        if (hid_first) {
            CHECK(!ready_hid(hid) && !qw && !attach(rdx,0));
        } else {
            CHECK(!attach(rdx,0) && qw==1 && !ready_hid(hid));
        }
        CHECK(composite_full==same && composite_events==same);
        for (int i=0;i<3;++i) {
            CHECK(!attach(rdx,0) && !attach(hid,1));
            CHECK(!subscribe_hid(hid,0) && !subscribe_hid(hid,1));
            encrypt_hid(hid,0); encrypt_hid(hid,1);
        }
        CHECK(hid_ready && qw==1);
        CHECK(composite_full==same && composite_events==same);
        drain_core(); drain_audio();
        CHECK(starts==1 && !drops && !context_errors && !irq);
    }
    return 0;
}
int test_restored_hid_subscription_claims_composite(void) {
    boot(); rdx_ble_link_state_t *link=connect(0);
    CHECK(!attach(link,0) && !composite_full);
    hid_saved_subscription=1; encrypt_hid(link,1);
    CHECK(hid_ready && rdx_ble_session_link_is_hid(link));
    CHECK(composite_full && composite_events==1 && !context_errors && !irq);
    return 0;
}
int test_logical_release_uses_real_server_lifecycle(void) {
    for (int fail=0;fail<2;++fail) {
        boot(); rdx_ble_link_state_t *link=connect(0);
        CHECK(!ready_hid(link) && !attach(link,0) && composite_full);
        rdx_ble_async_token_t token=rdx_ble_session_token_capture(link);
        u32 generation=link->slot_generation;
        barrier_fail=fail;
        rdx_ble_server_session_detach(&token,RDX_DETACH_LOGICAL_COMMAND);
        CHECK(link->connected && link->slot_generation==generation && hid_ready);
        CHECK(rdx_ble_session_link_is_hid(link) && !rdx_ble_session_link_is_rdx(link));
        CHECK(composite_wait && !composite_full &&
              composite_release_epoch==rdx_ble_session_rdx_runtime_epoch_get());
        CHECK(cleanup_calls==1 && abort_calls==1 && recv_calls==1);
        CHECK(s_rdx_runtime_state==(fail ? RDX_BLE_RUNTIME_FAILED : RDX_BLE_RUNTIME_QUIESCING));
        CHECK(g_rdx_lifecycle_barrier_armed==!fail);
        rdx_ble_server_session_detach(&token,RDX_DETACH_LOGICAL_COMMAND);
        CHECK(cleanup_calls==1 && hid_ready && !context_errors && !irq);
    }
    boot(); rdx_ble_link_state_t *rdx=connect(0), *hid=connect(1);
    CHECK(!attach(rdx,0) && !ready_hid(hid) && !composite_full);
    rdx_ble_async_token_t token=rdx_ble_session_token_capture(rdx);
    rdx_ble_server_session_detach(&token,RDX_DETACH_LOGICAL_COMMAND);
    CHECK(!composite_wait && rdx_ble_session_link_is_hid(hid) && hid_ready);
    CHECK(rdx->connected && hid->connected && !context_errors && !irq);
    return 0;
}
int test_unrelated_and_composite_physical_disconnect(void) {
    boot(); rdx_ble_link_state_t *link=connect(0), *other=connect(1);
    CHECK(!ready_hid(link) && !attach(link,0) && composite_full);
    u8 other_packet[6]={5,4,0,0x51,0,0x13};
    rdx_ble_server_phase0a_link_disconnected(&wrappers[1],other_packet,sizeof(other_packet));
    CHECK(!other->connected && link->connected && composite_full && !composite_clears);
    CHECK(rdx_ble_session_link_is_hid(link) && rdx_ble_session_link_is_rdx(link) && hid_ready);
    u8 link_packet[6]={5,4,0,0x50,0,0x13};
    rdx_ble_server_phase0a_link_disconnected(&wrappers[0],link_packet,sizeof(link_packet));
    CHECK(!link->connected && !composite_full && composite_clears==1 && !hid_ready);
    CHECK(!rdx_ble_session_get_hid_link() && !rdx_ble_session_get_rdx_link());
    CHECK(s_rdx_runtime_state==RDX_BLE_RUNTIME_QUIESCING && recv_calls==1);
    CHECK(!context_errors && !irq);
    return 0;
}
int test_rdx_release_reconnect_drops_stale_prompt(void) {
    for (int queued=0;queued<2;++queued) {
        boot(); rdx_ble_link_state_t *link=connect(0);
        CHECK(!ready_hid(link) && !attach(link,0) && qw==1);
        if (queued) drain_core();
        CHECK(release_rdx(link));
        CHECK(!attach(link,1) && !subscribe_hid(link,1) && qw==1);
        CHECK(!attach(link,0) && !attach(link,1) && qw==2);
        drain_core(); drain_audio();
        CHECK(starts==1 && drops==queued && hid_ready && !context_errors && !irq);
    }
    return 0;
}
int test_rdx_reconnect_after_playback_prompts_again(void) {
    boot(); rdx_ble_link_state_t *link=connect(0);
    CHECK(!ready_hid(link) && !attach(link,0));
    drain_core(); drain_audio(); CHECK(starts==1);
    CHECK(release_rdx(link) && !attach(link,0) && qw==2);
    drain_core(); drain_audio();
    CHECK(starts==2 && !drops && !context_errors && !irq);
    return 0;
}
int test_disconnect_reuse_keeps_only_valid_rdx_prompt(void) {
    for (int queued=0;queued<2;++queued) for (int hid_lost=0;hid_lost<2;++hid_lost) {
        boot(); rdx_ble_link_state_t *rdx=connect(0), *hid=connect(1);
        CHECK(!attach(rdx,0) && !ready_hid(hid) && qw==1);
        if (queued) drain_core();
        int slot=hid_lost ? 1 : 0;
        if (!hid_lost) CHECK(release_rdx(rdx));
        CHECK(rdx_ble_session_link_release(&wrappers[slot],0x50+slot));
        if (hid_lost) {
            disconnect_hid();
            CHECK(!ready_hid(connect(slot)) && qw==1);
        } else {
            CHECK(!attach(connect(slot),0) && qw==2);
        }
        drain_core(); drain_audio();
        CHECK(starts==1 && drops==(queued && !hid_lost) && !context_errors && !irq);
    }
    return 0;
}
int test_shutdown_and_reset_discard_old_work(void) {
    for (int queued=0;queued<2;++queued) for (int mode=0;mode<3;++mode) {
        boot(); rdx_ble_link_state_t *link=connect(0);
        CHECK(!attach(link,0) && !ready_hid(link) && qw==1);
        if (queued) drain_core();
        if (mode==0) app_var.goto_poweroff_flag=1;
        if (mode==1) g_rdx_ble_exiting=1;
        if (mode==2) {
            rdx_ble_server_conn_tone_reset();
            rdx_ble_session_transport_deinit();
            rdx_ble_session_transport_init(&wrappers[0],&wrappers[1]);
            disconnect_hid(); link=connect(0);
            /* A consumed RDX runtime fails closed after transport reinit. */
            CHECK(!ready_hid(link) &&
                  attach(link,0)==RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR && qw==1);
        }
        drain_core(); drain_audio();
        CHECK(!starts && drops==queued && !context_errors && !irq);
    }
    return 0;
}
int test_failures_do_not_block_or_repeat(void) {
    boot(); rdx_ble_link_state_t *link=connect(0);
    queue_fail=1; CHECK(!attach(link,0) && !ready_hid(link) && !qw);
    queue_fail=0; CHECK(!attach(link,0) && !attach(link,1) && qw==1);
    play_fail=1; drain_core(); CHECK(!tw && !context_errors);
    play_fail=0; CHECK(!attach(link,0) && !attach(link,1) && qw==1);
    CHECK(release_rdx(link) && !attach(link,0) && qw==2);
    drain_core(); drain_audio();
    CHECK(starts==1 && !drops && !context_errors && !irq);
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
    entries = ('rdx_ble_server_phase2_claim_to_att_error',
               'rdx_ble_server_phase2_hid_attach', 'rdx_ble_server_hid_write',
               'rdx_ble_server_hid_encryption_change', 'rdx_ble_server_phase2_rdx_attach',
               'rdx_ble_server_phase2_rdx_detach', 'rdx_ble_server_session_detach',
               'rdx_ble_server_rdx_detach_common', 'rdx_ble_server_phase0a_link_disconnected')
    entry_code = ''.join(function(server, name) for name in entries)
    code = (STUBS + registry + state + '\n' +
            ''.join(function(server, n) for n in names) + ENTRY_STUBS + entry_code + TESTS)
    # RDX is the only production caller: HID entry points must stay silent.
    rdx_entry = function(server, 'rdx_ble_server_phase2_rdx_attach')
    calls = re.findall(r'\brdx_ble_server_conn_tone_post\s*\(([^)]*)\)', rdx_entry)
    assert calls and all(re.fullmatch(r'\s*link\s*,\s*0\s*', call) for call in calls)
    other_source = server.replace(function(server, 'rdx_ble_server_conn_tone_post'), '', 1)
    other_source = other_source.replace(rdx_entry, '', 1)
    assert not re.search(r'\brdx_ble_server_conn_tone_post\s*\(', other_source)
    checks = re.findall(r'int (test_\w+)\(void\)', TESTS)
    with tempfile.TemporaryDirectory(prefix='connection-tone-') as tmp:
        path = Path(tmp) / 'checks.c'
        path.write_text(code, encoding='utf-8')
        run_c_checks(path, checks, native=True)
    print(f'RDX-only connection tones: {len(checks)} focused C checks passed (HID/OS/audio mocked).')


if __name__ == '__main__':
    main()
