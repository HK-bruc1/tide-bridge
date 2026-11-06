"""Execute the RDX playback-volume protocol, token and notification paths."""
import ctypes
import re
import tempfile
from pathlib import Path
from llvmlite import binding as llvm

from host_c_test_lib import ROOT, function, run_c_checks

BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'

STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed short s16;
typedef int bool;
typedef unsigned long long size_t;
void *malloc(size_t);
void free(void *);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
int snprintf(char *, size_t, const char *, ...);
#define printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define NULL ((void *)0)
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define CUSTOM_VALUE_MAX_LENGTH 100
#define Q_CALLBACK 1
#define APP_MODE_PC 3
#define APP_AUDIO_STATE_IDLE 0
#define APP_AUDIO_STATE_MUSIC 1
#define APP_AUDIO_STATE_CALL 2
#define DEV_FLOW_PLAYER_VOLUME_NODE_NAME "74E325"

struct volume_cfg { u8 bypass; u16 cfg_level_max; };
typedef struct { u8 file_send_busy; } ReqFileInfo;
typedef struct { u8 slot_index; u32 slot_generation, transport_epoch; } rdx_ble_async_token_t;
typedef struct { u8 rdx_ccc_configured; u16 mtu_size; } rdx_ble_link_state_t;
void rdx_volume_cancel_for_token(const rdx_ble_async_token_t *token);
static rdx_ble_async_token_t owner={0,1,1};
static rdx_ble_link_state_t link={1,244};
static struct { int goto_poweroff_flag; } app_var;
static int live=1, ccc=1, poweroff, dip, ota, dut, keys, factory, factory_file;
static int storage, pc, transfer, sync_busy, dat_loading, index_active;
static int formatting, sd_format, record_format, record_busy, record_worker;
static int audio_state, a2dp, esco, ring, key_tone;
static int cfg_fail, cfg_max=16, music_volume=6, set_volume_calls, def_state_calls;
static int fail_alloc, fail_queue, fail_send, allocations, frees, sends;
static int revoke_on_set, cancel_on_alloc, irq_disables, irq_enables;
static void *last_alloc;
static char wire[160];
static u32 wire_len;
static ReqFileInfo file_info;

typedef struct {
    void *priv;
    void (*callback)(void *);
    u32 delay;
    u8 active;
} fake_timer_t;
static fake_timer_t timers[16];
static int timer_count;

typedef struct {
    void (*callback)(void *);
    void *priv;
} queued_callback_t;
static queued_callback_t queued[16];
static int queued_count;

static void *test_malloc(size_t n) {
    if (fail_alloc) return NULL;
    if (cancel_on_alloc) {
        cancel_on_alloc=0;
        rdx_volume_cancel_for_token(&owner);
    }
    last_alloc=malloc(n); ++allocations; return last_alloc;
}
static void test_free(void *p) { ++frees; free(p); }
#define malloc test_malloc
#define free test_free

static int os_taskq_post_type(const char *task,int type,int n,int *msg) {
    if (strcmp(task,"app_core") || type!=Q_CALLBACK || n!=3 || msg[1]!=1) return -1;
    if (fail_queue || queued_count>=16) return -1;
    queued[queued_count].callback=(void (*)(void *))msg[0];
    queued[queued_count].priv=(void *)msg[2];
    ++queued_count;
    return 0;
}
static u16 sys_timeout_add(void *priv,void (*fn)(void *),u32 delay) {
    if (timer_count>=16) return 0;
    timers[timer_count].priv=priv; timers[timer_count].callback=fn;
    timers[timer_count].delay=delay; timers[timer_count].active=1;
    return ++timer_count;
}
static void pump(void) {
    int i=0;
    while (i<queued_count) {
        queued_callback_t q=queued[i++];
        q.callback(q.priv);
    }
    queued_count=0;
}
static void run_timer(void *priv) {
    int i;
    for (i=0;i<timer_count;++i) {
        if (timers[i].active && timers[i].priv==priv) {
            timers[i].active=0;
            timers[i].callback(timers[i].priv);
            return;
        }
    }
}
static int active_timers(void) {
    int i,n=0; for (i=0;i<timer_count;++i) n+=timers[i].active; return n;
}
static rdx_ble_link_state_t *rdx_ble_session_rdx_token_resolve(
    const rdx_ble_async_token_t *t,int ready) {
    return live && t && t->slot_index==owner.slot_index &&
        t->slot_generation==owner.slot_generation &&
        t->transport_epoch==owner.transport_epoch ? &link : NULL;
}
static int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t,int ready) {
    *t=owner; return live;
}
static int rdx_ble_server_send_for_token(u8 *p,u32 n,
                                         const rdx_ble_async_token_t *t) {
    if (fail_send || !rdx_ble_session_rdx_token_resolve(t,1) || !ccc ||
        n>link.mtu_size || wire_len+n>=sizeof(wire)) return -1;
    for (u32 i=0;i<n;++i) wire[wire_len+i]=(char)p[i];
    wire_len+=n; wire[wire_len]=0; ++sends; return 0;
}
static int rdx_app_get_poweroff_flag(void) { return poweroff; }
static int rdx_dip_switch_shutdown_pending(void) { return dip; }
static u8 get_ota_status(void) { return ota; }
static int rdx_dut_is_in_mode(void) { return dut; }
static int rdx_dut_test_keys_active(void) { return keys; }
static int rdx_factory_audio_owned(void) { return factory; }
static int rdx_factory_audio_file_busy(void) { return factory_file; }
static int rdx_storage_lifecycle_business_blocked(void) { return storage; }
static int app_in_mode(int mode) { return mode==APP_MODE_PC ? pc : 0; }
static ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) { return &file_info; }
static u8 rdx_is_file_transfer_active(void) { return transfer; }
static u8 rdx_is_file_sync_busy(void) { return sync_busy; }
static u8 rdx_uxfile_is_datFileInfo_loading(void) { return dat_loading; }
static u8 rdx_uxfile_is_index_active(void) { return index_active; }
static u8 rdx_uxfile_is_formatting(void) { return formatting; }
static bool rdx_uxfile_sd_format_status_check(void) { return sd_format; }
static int rdx_record_format_service_status(void) { return record_format; }
static u8 rdx_record_config_busy(void) { return record_busy; }
static bool rdx_record_process_is_busy_check(void) { return record_worker; }
static int a2dp_player_runing(void) { return a2dp; }
static u8 bt_get_esco_coder_busy_flag(void) { return esco; }
static bool ring_player_runing(void) { return ring; }
static u8 key_tone_player_running(void) { return key_tone; }
static u8 app_audio_get_state(void) { return audio_state; }
static s16 app_audio_get_volume(u8 state) {
    return state==APP_AUDIO_STATE_MUSIC ? music_volume : 0;
}
static void app_audio_set_volume(u8 state,s16 volume,u8 fade) {
    ++set_volume_calls;
    if (state!=APP_AUDIO_STATE_MUSIC) return;
    music_volume=volume;
    if (revoke_on_set) { live=0; ++owner.transport_epoch; }
}
static void app_audio_set_volume_def_state(u8 state) { ++def_state_calls; }
static int volume_ioc_get_cfg(const char *name,struct volume_cfg *cfg) {
    if (cfg_fail) { cfg->cfg_level_max=16; return -1; }
    cfg->cfg_level_max=cfg_max;
    return 0;
}
static void local_irq_disable(void) { ++irq_disables; }
static void local_irq_enable(void) { ++irq_enables; }
'''

TESTS = r'''
static void boot(void) {
    live=ccc=1; link.rdx_ccc_configured=1; link.mtu_size=244;
    owner=(rdx_ble_async_token_t){0,1,1};
    app_var.goto_poweroff_flag=0;
    poweroff=dip=ota=dut=keys=factory=factory_file=0;
    storage=pc=transfer=sync_busy=dat_loading=index_active=0;
    formatting=sd_format=record_format=record_busy=record_worker=0;
    audio_state=APP_AUDIO_STATE_IDLE; a2dp=esco=ring=key_tone=0;
    cfg_fail=0; cfg_max=16; music_volume=6;
    set_volume_calls=def_state_calls=fail_alloc=fail_queue=fail_send=0;
    allocations=frees=sends=revoke_on_set=cancel_on_alloc=irq_disables=irq_enables=0;
    wire_len=0; wire[0]=0; queued_count=0; timer_count=0;
    file_info.file_send_busy=0;
    rdx_volume_init();
}
static void request(const char *value) {
    wire_len=0; wire[0]=0;
    rdx_volume_handle_custom(value);
    pump();
}
#define REPLY(fields) "*DEV#custom#vol#" fields "#"
int test_protocol_and_set(void) {
    boot();
    request("get,1"); CHECK(!strcmp(wire,REPLY("1,ok,6,16")));
    request("get,00001"); CHECK(!strcmp(wire,REPLY("00001,ok,6,16")));
    request("get,65535"); CHECK(!strcmp(wire,REPLY("65535,ok,6,16")));

    const char *no_reply[]={"get","get,","get,0","get,00000","get,65536",
        "get,000001","get,+1","get,-1","get, 1","get,a","set,0,1",
        "get,1 ","get,1#"};
    for(unsigned i=0;i<sizeof(no_reply)/sizeof(no_reply[0]);++i) {
        int before=sends; request(no_reply[i]); CHECK(sends==before);
    }
    const char *arg[]={"GET,1","get,1,0","get,1,",
        "set,1","set,1,","set,1,000","set,1,1,0","set,1,+1","set,1, 1",
        "set ,1,1","foo,1","set,1,x","set,1,1x"};
    for(unsigned i=0;i<sizeof(arg)/sizeof(arg[0]);++i) {
        request(arg[i]); CHECK(!strcmp(wire,REPLY("1,arg,6,16")));
        CHECK(!set_volume_calls);
    }

    request("set,2,8"); CHECK(!strcmp(wire,REPLY("2,ok,8,16")));
    CHECK(set_volume_calls==1 && def_state_calls==1 && music_volume==8);
    request("set,3,7"); CHECK(!strcmp(wire,REPLY("3,ok,7,16")));
    CHECK(set_volume_calls==2 && music_volume==7);
    music_volume=8;
    request("set,4,08"); CHECK(!strcmp(wire,REPLY("4,ok,8,16")));
    CHECK(set_volume_calls==2 && def_state_calls==3);
    request("set,5,17"); CHECK(!strcmp(wire,REPLY("5,arg,8,16")));
    request("set,00006,0"); CHECK(!strcmp(wire,REPLY("00006,ok,0,16")));
    CHECK(allocations==frees);
    return 0;
}
int test_trusted_range_and_get_busy(void) {
    boot(); cfg_fail=1; request("get,1"); CHECK(!strcmp(wire,REPLY("1,busy,0,0")));
    boot(); cfg_max=0; request("get,1"); CHECK(!strcmp(wire,REPLY("1,busy,0,0")));
    boot(); cfg_max=100; request("get,1"); CHECK(!strcmp(wire,REPLY("1,busy,0,0")));
    boot(); music_volume=-1; request("get,1"); CHECK(!strcmp(wire,REPLY("1,busy,0,0")));
    boot(); music_volume=17; request("get,1"); CHECK(!strcmp(wire,REPLY("1,busy,0,0")));

    int *gates[]={&poweroff,&app_var.goto_poweroff_flag,&dip,&ota,&dut,&keys,
        &factory,&factory_file,&storage,&pc,&transfer,&sync_busy,&dat_loading,
        &index_active,&formatting,&sd_format,&record_format};
    for(unsigned i=0;i<sizeof(gates)/sizeof(gates[0]);++i) {
        boot(); *gates[i]=1; request("get,1");
        CHECK(!strcmp(wire,REPLY("1,busy,0,0")));
    }
    boot(); file_info.file_send_busy=1; request("get,1");
    CHECK(!strcmp(wire,REPLY("1,busy,0,0")));
    boot(); record_busy=record_worker=1; request("get,1");
    CHECK(!strcmp(wire,REPLY("1,ok,6,16")));
    boot(); CHECK(rdx_volume_playback_max_get(NULL));
    u16 max=0; CHECK(!rdx_volume_playback_max_get(&max) && max==16);
    cfg_max=0; max=0; CHECK(rdx_volume_playback_max_get(&max)==-1 && !max);
    return 0;
}
int test_set_busy_and_commit_races(void) {
    int *gates[]={&record_busy,&record_worker,&a2dp,&esco,&ring,&key_tone};
    for(unsigned i=0;i<sizeof(gates)/sizeof(gates[0]);++i) {
        boot(); *gates[i]=1; request("set,1,8");
        CHECK(!strcmp(wire,REPLY("1,busy,0,0")) && !set_volume_calls);
    }
    boot(); audio_state=APP_AUDIO_STATE_CALL; request("set,1,8");
    CHECK(!strcmp(wire,REPLY("1,busy,0,0")) && !set_volume_calls);
    request("get,2"); CHECK(!strcmp(wire,REPLY("2,ok,6,16")));

    boot(); revoke_on_set=1; request("set,1,8");
    CHECK(set_volume_calls==1 && music_volume==8 && !sends && allocations==frees);
    live=1; request("get,2"); CHECK(!strcmp(wire,REPLY("2,ok,8,16")));

    boot(); fail_send=1; request("set,1,8");
    CHECK(set_volume_calls==1 && music_volume==8 && !sends && allocations==frees);
    boot(); fail_queue=1; request("set,1,8");
    CHECK(!set_volume_calls && allocations==frees);
    boot(); fail_alloc=1; request("set,1,8");
    CHECK(!set_volume_calls && allocations==frees);
    boot(); rdx_volume_handle_custom("set,1,8"); CHECK(queued_count==1);
    live=0; pump(); CHECK(!set_volume_calls && allocations==frees);
    boot(); ccc=0; link.rdx_ccc_configured=0; request("set,1,8");
    CHECK(!set_volume_calls && allocations==frees);
    return 0;
}
int test_notification_coalescing_and_latest_value(void) {
    boot(); rdx_volume_local_music_changed(6,8);
    CHECK(active_timers()==1 && timers[0].delay==150 && !queued_count && !sends);
    void *batch=timers[0].priv;
    rdx_volume_local_music_changed(8,10);
    CHECK(active_timers()==1 && timers[0].priv==batch);
    music_volume=12; cfg_max=20; run_timer(batch); pump();
    CHECK(sends==1 && !strcmp(wire,REPLY("0,ntf,12,20")) && allocations==frees);
    boot(); rdx_volume_local_music_changed(6,6); CHECK(!active_timers());
    rdx_volume_local_music_changed(6,8); CHECK(active_timers()==1);
    rdx_volume_local_music_changed(6,8); CHECK(active_timers()==1);
    run_timer(timers[0].priv); pump(); CHECK(sends==1 && allocations==frees);
    boot(); ccc=0; link.rdx_ccc_configured=0;
    rdx_volume_local_music_changed(6,8); CHECK(!active_timers());
    return 0;
}
int test_notification_token_and_ccc_lifecycle(void) {
    void *batch;
    boot(); rdx_volume_local_music_changed(6,8);
    void *old_batch=timers[0].priv;
    ++owner.transport_epoch;
    rdx_volume_local_music_changed(8,10);
    void *new_batch=timers[1].priv;
    CHECK(new_batch!=old_batch && active_timers()==2);
    run_timer(old_batch); CHECK(!queued_count && active_timers()==1);
    run_timer(new_batch); pump();
    CHECK(sends==1 && !strcmp(wire,REPLY("0,ntf,6,16")) && allocations==frees);

    boot(); cancel_on_alloc=1;
    rdx_volume_local_music_changed(6,8);
    CHECK(!active_timers() && allocations==frees);

    boot(); rdx_volume_local_music_changed(6,8);
    old_batch=timers[0].priv;
    rdx_ble_async_token_t cancel_token=owner;
    int before_disables=irq_disables, before_enables=irq_enables;
    rdx_volume_cancel_for_token(&cancel_token);
    CHECK(irq_disables==before_disables+1 && irq_enables==before_enables+1);
    rdx_volume_local_music_changed(8,10);
    new_batch=timers[1].priv;
    CHECK(new_batch!=old_batch && active_timers()==2);
    run_timer(old_batch); CHECK(active_timers()==1);
    run_timer(new_batch); pump();
    CHECK(sends==1 && !strcmp(wire,REPLY("0,ntf,6,16")) && allocations==frees);

    boot(); rdx_volume_local_music_changed(6,8);
    batch=timers[0].priv;
    request("set,1,6");
    CHECK(!strcmp(wire,REPLY("1,ok,6,16")) && set_volume_calls==0 && def_state_calls==1);
    run_timer(batch); pump(); CHECK(sends==1 && allocations==frees);

    boot(); rdx_volume_local_music_changed(6,8);
    batch=timers[0].priv; live=0; run_timer(batch); pump();
    CHECK(!sends && allocations==frees);
    boot(); fail_queue=1; rdx_volume_local_music_changed(6,8);
    batch=timers[0].priv; run_timer(batch); CHECK(!sends && allocations==frees);
    return 0;
}
'''

LOCAL_STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef signed short s16;
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define APP_MSG_LOCAL_VOL_UP 1
#define APP_MSG_LOCAL_VOL_DOWN 2
#define APP_AUDIO_STATE_IDLE 0
#define APP_AUDIO_STATE_MUSIC 1
#define APP_AUDIO_STATE_CALL 2
#define APP_AUDIO_CURRENT_STATE 6
#define APP_MSG_VOL_CHANGED 3
static int current_state=APP_AUDIO_STATE_MUSIC, music=6, call=4;
static int blocked, tone, notify_count, sent_count;
static s16 notify_previous, notify_current;
static int rdx_app_local_player_message_blocked(int message) { return blocked; }
static int tone_player_runing(void) { return tone; }
static s16 app_audio_get_volume(u8 state) {
    if (state==APP_AUDIO_STATE_MUSIC) return music;
    if (state==APP_AUDIO_STATE_CALL) return call;
    return current_state==APP_AUDIO_STATE_CALL ? call : music;
}
static void app_audio_volume_up(u8 step) {
    if (current_state==APP_AUDIO_STATE_CALL) call+=step; else music+=step;
}
static void app_audio_volume_down(u8 step) {
    if (current_state==APP_AUDIO_STATE_CALL) call-=step; else music-=step;
}
static void rdx_volume_local_music_changed(s16 previous,s16 current) {
    ++notify_count; notify_previous=previous; notify_current=current;
}
static void app_send_message(int message,int value) { ++sent_count; }
#define y_printf(...) ((void)0)
'''

LOCAL_TESTS = r'''
int test_local_only_notifies_music_change(void) {
    current_state=APP_AUDIO_STATE_MUSIC; music=6; call=4;
    blocked=tone=notify_count=sent_count=0;
    rdx_app_local_volume_change(APP_MSG_LOCAL_VOL_UP);
    CHECK(music==8 && call==4 && notify_count==1);
    CHECK(notify_previous==6 && notify_current==8);
    current_state=APP_AUDIO_STATE_CALL;
    rdx_app_local_volume_change(APP_MSG_LOCAL_VOL_UP);
    CHECK(music==8 && call==6 && notify_count==1);
    rdx_app_local_volume_change(APP_MSG_LOCAL_VOL_DOWN);
    CHECK(music==8 && call==4 && notify_count==1);
    current_state=APP_AUDIO_STATE_MUSIC;
    rdx_app_local_volume_change(APP_MSG_LOCAL_VOL_DOWN);
    CHECK(music==6 && notify_count==2);
    blocked=1; rdx_app_local_volume_change(APP_MSG_LOCAL_VOL_UP);
    CHECK(music==6 && notify_count==2);
    blocked=0; tone=1; rdx_app_local_volume_change(APP_MSG_LOCAL_VOL_UP);
    CHECK(music==6 && notify_count==2);
    return 0;
}
'''


def main():
    crt = ctypes.CDLL('msvcrt')
    llvm.add_symbol('snprintf', ctypes.cast(crt._snprintf, ctypes.c_void_p).value)

    volume = (BASE / 'rdx_volume.c').read_text(encoding='utf-8')
    ble = (BASE / 'rdx_ble_server.c').read_text(encoding='utf-8')
    app = (BASE / 'rdx_app.c').read_text(encoding='utf-8')
    vm = (BASE / 'rdx_vm.c').read_text(encoding='utf-8')
    makefile = (ROOT / 'SDK/Makefile').read_text(encoding='utf-8')
    custom = function(app, 'rdx_app_custom_command_parse')
    detach = function(ble, 'rdx_ble_server_phase2_rdx_write')
    assert 'rdx_volume.c' in makefile
    assert 'rdx_volume_init();' in function(vm, 'rdx_vm_init')
    assert custom.index('RDX_BLE_RUNTIME_ACTIVE') < custom.index('RDX_VOLUME_CUSTOM_CMD')
    assert 'rdx_volume_handle_custom(value)' in custom
    assert 'rdx_volume_cancel_for_token(&ccc_token)' in detach
    reset_default = function(vm, 'rdx_vm_playback_volume_set_default')
    assert 'rdx_volume_playback_max_get' in reset_default
    assert 'app_audio_volume_max_query' not in reset_default

    assert len('*APP#custom#vol#get,1#') == 22
    assert len('*APP#custom#vol#set,1,0#') == 24
    assert len('*APP#custom#vol#set,65535,99#') == 29
    assert len('*DEV#custom#vol#65535,arg,99,99#') == 32

    source = (STUBS + function(ble, 'rdx_ble_server_send_text_for_token') +
              re.sub(r'^#include[^\n]*', '', volume, flags=re.M) + TESTS)
    with tempfile.TemporaryDirectory(prefix='volume-') as tmp:
        path = Path(tmp) / 'checks.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)

    local_source = (LOCAL_STUBS +
                    function(app, 'rdx_app_local_volume_change') +
                    LOCAL_TESTS)
    with tempfile.TemporaryDirectory(prefix='volume-local-') as tmp:
        path = Path(tmp) / 'checks.c'
        path.write_text(local_source, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', LOCAL_TESTS), native=True)

    print('Playback volume: strict protocol, trusted range, busy gates, same-value SET, '
          'token races, 150 ms notification coalescing and CCC lifecycle passed.')


if __name__ == '__main__':
    main()
