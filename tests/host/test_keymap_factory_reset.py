"""验证出厂默认值：配键 A/B 持久化、回放音量及恢复失败处理。"""
import re
import tempfile
from pathlib import Path
from host_c_test_lib import ROOT, function, run_c_checks

BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
def read(name):
    return (BASE / name).read_text(encoding='utf-8')
def strip_includes(text):
    return re.sub(r'^#include[^\n]*', '', text, flags=re.M)

STUBS = r'''
typedef unsigned long long size_t;
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
#define NULL ((void *)0)
#define TCFG_RDX_HOGP_ENABLE 1
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define VM_RDX_HOGP_KEYMAP_SLOT_A 0
#define VM_RDX_HOGP_KEYMAP_SLOT_B 1
#define VM_RDX_HOGP_KEYMAP_COMMIT_A 2
#define VM_RDX_HOGP_KEYMAP_COMMIT_B 3
#define y_printf(...) ((void)0)
#define HOGPKM_TRACE(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
u8 flash[4][64], applied[35];
int lengths[4], fail_write=-1, fail_read=-1, apply_fail, writes, cancels;
void rdx_hogp_input_cancel(void) { ++cancels; }
int syscfg_write(int id,const void *p,int n) {
    ++writes;
    if(id==fail_write) return -1;
    memcpy(flash[id],p,n); lengths[id]=n; return n;
}
int syscfg_read(int id,void *p,int n) {
    if(id==fail_read || lengths[id]!=n) return -1;
    memcpy(p,flash[id],n); return n;
}
/* Checksum backend is mocked; record construction/validation is production. */
u32 rdx_hogpkm_crc32(const u8 *p,u16 n) {
    u32 v=5381; while(n--) v=(v*33)^*p++; return v;
}
int rdx_hogpkm_apply_payload(const u8 *p) {
    if(apply_fail) return -1;
    memcpy(applied,p,35); return 0;
}
'''

CHECKS = r'''
static void seed(void) {
    rdx_hogpkm_store_transaction_t t;
    memset(flash,0,sizeof(flash)); memset(lengths,0,sizeof(lengths));
    fail_write=fail_read=-1; apply_fail=0; writes=cancels=0;
    memcpy(s_rdx_hogpkm_current_keymap,s_rdx_hogpkm_default_keymap,35);
    s_rdx_hogpkm_current_keymap[1]=4;
    memcpy(applied,s_rdx_hogpkm_current_keymap,35);
    s_rdx_hogpkm_current_revision=7;
    s_rdx_hogpkm_current_keymap_crc32=rdx_hogpkm_crc32(applied,35);
    rdx_hogpkm_store_prepare(255,7,applied,s_rdx_hogpkm_current_keymap_crc32,&t);
    rdx_hogpkm_store_commit(&t,&s_rdx_hogpkm_active_slot);
    s_rdx_hogpkm_pending.frame.valid=1; s_rdx_hogpkm_cache.valid=1;
    s_rdx_hogpkm_factory_reset=0;
}
int check_reset(void) {
    rdx_hogpkm_store_entry_t boot;
    seed();
    const u8 expected[]={0x68,0x69,0x6c,0x6d,0x6e};
    for(int key=0;key<5;++key) {
        CHECK(s_rdx_hogpkm_default_keymap[key*7]==0);
        CHECK(s_rdx_hogpkm_default_keymap[key*7+1]==expected[key]);
        for(int slot=2;slot<7;++slot) CHECK(s_rdx_hogpkm_default_keymap[key*7+slot]==0);
    }
    CHECK(!rdx_hogp_keymap_config_factory_reset());
    CHECK(s_rdx_hogpkm_factory_reset && cancels==1);
    CHECK(!s_rdx_hogpkm_pending.frame.valid && !s_rdx_hogpkm_cache.valid);
    CHECK(!memcmp(applied,s_rdx_hogpkm_default_keymap,35));
    CHECK(!rdx_hogpkm_store_load(&boot));
    CHECK(boot.revision==8 && !memcmp(boot.payload,s_rdx_hogpkm_default_keymap,35));
    int before=writes;
    CHECK(!rdx_hogp_keymap_config_factory_reset() && writes==before && cancels==2);
    return 0;
}
int check_saved_old_defaults(void) {
    seed();
    rdx_hogpkm_store_transaction_t t;
    rdx_hogpkm_store_entry_t boot;
    u8 old[35]={0};
    for(int key=0;key<5;++key) old[key*7+1]=0x68+key;
    CHECK(!rdx_hogpkm_store_prepare(s_rdx_hogpkm_active_slot,9,old,rdx_hogpkm_crc32(old,35),&t));
    CHECK(!rdx_hogpkm_store_commit(&t,&s_rdx_hogpkm_active_slot));
    CHECK(!rdx_hogpkm_store_load(&boot));
    CHECK(boot.revision==9 && !memcmp(boot.payload,old,35));
    CHECK(boot.payload[15]==0x6a && boot.payload[22]==0x6b && boot.payload[29]==0x6c);
    return 0;
}
int check_failures(void) {
    rdx_hogpkm_store_entry_t boot;
    int mode;
    for(mode=0;mode<5;mode++) {
        seed();
        if(mode==0) fail_write=1; /* new data write */
        if(mode==1) fail_read=1; /* new data readback */
        if(mode==2) fail_write=3; /* commit write */
        if(mode==3) apply_fail=1;
        if(mode==4) s_rdx_hogpkm_current_revision=0xffffffffU;
        CHECK(rdx_hogp_keymap_config_factory_reset()!=0);
        CHECK(!s_rdx_hogpkm_factory_reset && applied[1]==4);
        fail_read=-1;
        CHECK(!rdx_hogpkm_store_load(&boot) && boot.revision==7 && boot.payload[1]==4);
        if(mode!=4) {
            fail_write=-1; apply_fail=0;
            CHECK(!rdx_hogp_keymap_config_factory_reset());
            CHECK(!rdx_hogpkm_store_load(&boot) && boot.payload[1]==0x68);
        }
    }
    return 0;
}
'''

def check_playback_volume_defaults():
    vm = read('rdx_vm.c')
    source = r'''
typedef unsigned char u8;
typedef short s16;
void *memset(void *, int, unsigned long long);
#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1
#define CFG_MUSIC_VOL 10
#define CFG_BT_NAME 12
#define LOCAL_NAME_LEN 32
#define APP_AUDIO_STATE_MUSIC 1
#define AppVol_BT_MUSIC 0
#define RECORD_STATE_START 1
#define RECORD_STATE_RESUME 2
#define RDX_RTC_PATH_SEL 1
#define RDX_RTC_PATH_SOFTWARE 2
#define RDX_DEFAULT_SHUT_DOWN_TIME 300
#define true 1
#define log_info(...) ((void)0)
#define y_printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
typedef struct { u8 run; } RecordStatus;
typedef struct { u8 file_send_busy; } ReqFileInfo;
static struct { u8 volume_def_state; s16 music_volume; } app_var;
static RecordStatus record;
static ReqFileInfo upload;
static s16 maximum, vm_music;
static int write_result, writes, keymap_calls, keymap_result, ota, pairs, resets, bad_audio_state;
static RecordStatus *rdx_record_get_status(void) { return &record; }
static ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) { return &upload; }
static int get_ota_status(void) { return ota; }
static s16 app_audio_volume_max_query(int index) {
    if (index!=AppVol_BT_MUSIC) bad_audio_state=1;
    return maximum;
}
static int syscfg_write(int id, const void *data, int length) {
    if (id==CFG_MUSIC_VOL) {
        ++writes;
        if (write_result!=length) return write_result;
        vm_music=*(const s16 *)data;
    }
    return length;
}
static void app_audio_set_volume(int state, s16 volume, int fade) {
    if (state!=APP_AUDIO_STATE_MUSIC || fade!=1) bad_audio_state=1;
    app_var.music_volume=volume;
}
static void app_audio_set_volume_def_state(u8 state) { app_var.volume_def_state=state; }
static int rdx_hogp_keymap_config_factory_reset(void) { ++keymap_calls; return keymap_result; }
static int tone_reset_result, tone_resets, tone_busy;
static int rdx_record_tone_config_busy(void) { return tone_busy; }
static int rdx_record_tone_factory_reset(void) { ++tone_resets; return tone_reset_result; }
static void rdx_vm_ble_pairing_state_reset(void) { ++pairs; }
static int syscfg_read_string(int id, void *data, int length, int mode) { return 0; }
static void rdx_ble_server_reset_local_name(void) {}
static void rdx_record_err_reboot_flag_write_into_vm(int value) {}
static void sys_set_auto_off_time(int value) {}
static void rdx_record_mic_gain_set_default(void) {}
static void rdx_app_time_to_reset(void) { ++resets; }
'''
    for name in ('rdx_vm_playback_volume_set_default', 'rdx_vm_reset_defaults_no_poweroff',
                 'rdx_vm_sys_reset_to_defaults'):
        source += function(vm, name)
    source += r'''
static void seed(void) {
    app_var.music_volume=vm_music=4; app_var.volume_def_state=1;
    maximum=12; write_result=sizeof(s16);
    record.run=upload.file_send_busy=0;
    writes=keymap_calls=keymap_result=ota=pairs=resets=bad_audio_state=0;
    tone_reset_result=tone_resets=tone_busy=0;
}
int check_volume_reset(void) {
    seed(); CHECK(!rdx_vm_reset_defaults_no_poweroff());
    CHECK(writes==1 && vm_music==12 && app_var.music_volume==12 && !app_var.volume_def_state);
    CHECK(keymap_calls==1 && pairs==1 && !resets && !bad_audio_state);
    CHECK(tone_resets==1);
    seed(); maximum=16; rdx_vm_sys_reset_to_defaults();
    CHECK(resets==1 && vm_music==16 && app_var.music_volume==16);
    return 0;
}
int check_volume_reset_failures(void) {
    for (int mode=0; mode<4; ++mode) {
        seed();
        if (mode==0) ota=1;
        if (mode==1) record.run=RECORD_STATE_START;
        if (mode==2) record.run=RECORD_STATE_RESUME;
        if (mode==3) upload.file_send_busy=1;
        rdx_vm_sys_reset_to_defaults();
        CHECK(!writes && !keymap_calls && !pairs && !resets);
    }
    for (int result=-1; result<(int)sizeof(s16); ++result) {
        seed(); write_result=result; rdx_vm_sys_reset_to_defaults();
        CHECK(writes==1 && !keymap_calls && !pairs && !resets);
        CHECK(app_var.music_volume==4 && vm_music==4 && app_var.volume_def_state);
    }
    seed(); maximum=0; rdx_vm_sys_reset_to_defaults();
    CHECK(!writes && !keymap_calls && !resets);
    seed(); keymap_result=-1; rdx_vm_sys_reset_to_defaults();
    CHECK(keymap_calls==1 && !pairs && !resets);
    seed(); tone_busy=1; rdx_vm_sys_reset_to_defaults();
    CHECK(!writes && !keymap_calls && !tone_resets && !pairs && !resets);
    seed(); tone_reset_result=-1; rdx_vm_sys_reset_to_defaults();
    CHECK(tone_resets==1 && !pairs && !resets);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='playback-volume-reset-') as tmp:
        path = Path(tmp) / 'checks.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, ['check_volume_reset', 'check_volume_reset_failures'], native=True)
        disabled = source.replace('#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 1',
                                  '#define TCFG_RDX_LOCAL_PLAYBACK_ENABLE 0')
        disabled += r'''
int check_playback_disabled(void) {
    seed(); write_result=-1; rdx_vm_sys_reset_to_defaults();
    CHECK(resets==1 && keymap_calls==1 && pairs==1 && !writes);
    CHECK(app_var.music_volume==4 && vm_music==4);
    return 0;
}
'''
        path.write_text(disabled, encoding='utf-8')
        run_c_checks(path, ['check_playback_disabled'], native=True)


service = read('rdx_hogp_keymap_config.c')
protocol = read('rdx_hogp_keymap_protocol.c')
header = strip_includes(read('rdx_hogp_keymap_internal.h'))
# Locate the SDK usage constants independently of SDK include-path aliases.
usage = next((ROOT / 'SDK').rglob('hid_keyboard_usage.h'))
code = STUBS + header + strip_includes(usage.read_text(encoding='utf-8'))
for name in ('rdx_hogpkm_get_le16','rdx_hogpkm_get_le32','rdx_hogpkm_put_le16',
             'rdx_hogpkm_put_le32','rdx_hogpkm_usage_is_supported','rdx_hogpkm_validate_keymap'):
    code += function(protocol,name)
code += strip_includes(read('rdx_hogp_keymap_store.c'))
code += re.search(r'static const u8 s_rdx_hogpkm_default_keymap.*?\};',service,re.S)[0]
code += r'''
u8 s_rdx_hogpkm_current_keymap[35],s_rdx_hogpkm_active_slot,s_rdx_hogpkm_factory_reset;
u32 s_rdx_hogpkm_current_revision,s_rdx_hogpkm_current_keymap_crc32,s_rdx_hogpkm_generation;
struct { rdx_hogpkm_request_t frame; } s_rdx_hogpkm_pending;
struct { int valid; } s_rdx_hogpkm_cache;
'''
code += function(service,'rdx_hogp_keymap_config_on_disconnect')
code += function(service,'rdx_hogp_keymap_config_factory_reset') + CHECKS
code += r'''
typedef struct { int valid; } rdx_ble_async_token_t;
int result, reset_calls, reset_attempts, ack, freed, download_busy;
int rdx_record_format_download_conflict(const char *operation) { return download_busy; }
int rdx_ble_session_rdx_token_resolve(rdx_ble_async_token_t *p,int ready) { return p->valid; }
int rdx_factory_audio_file_busy(void) { return 0; }
int rdx_factory_audio_owned(void) { return 0; }
int rdx_vm_reset_defaults_no_poweroff(void) { ++reset_attempts; return result; }
void acknowledge(int value) { ack=value; }
struct { void (*sys_set_default_ack_indicate)(int); } ops={acknowledge}, *g_protocol_ops=&ops;
void test_free(void *p) { ++freed; }
#define free test_free
void rdx_app_time_to_reset(void) { if(ack==0) ++reset_calls; }
'''
code += function(read('rdx_app.c'),'rdx_app_factory_reset_on_app_core')
code += r'''
int check_callback(void) {
    rdx_ble_async_token_t token={0};
    ack=-1;
    rdx_app_factory_reset_on_app_core(&token);
    CHECK(freed==1 && !reset_attempts && ack==-1 && !reset_calls);
    token.valid=1; result=-1;
    rdx_app_factory_reset_on_app_core(&token);
    CHECK(freed==2 && reset_attempts==1 && ack==1 && !reset_calls);
    result=0;
    rdx_app_factory_reset_on_app_core(&token);
    CHECK(freed==3 && reset_attempts==2 && ack==0 && reset_calls==1);
    download_busy=1; ack=-1;
    rdx_app_factory_reset_on_app_core(&token);
    CHECK(freed==4 && reset_attempts==2 && ack==1 && reset_calls==1);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='keymap-reset-') as tmp:
    path = Path(tmp) / 'checks.c'
    path.write_text(code,encoding='utf-8')
    run_c_checks(path, ['check_reset','check_saved_old_defaults','check_failures','check_callback'], native=True)
check_playback_volume_defaults()
print('Factory defaults: keymap persistence and playback volume reset/failures passed.')
