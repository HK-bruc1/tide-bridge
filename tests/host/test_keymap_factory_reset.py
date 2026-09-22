"""Factory reset persistence/reboot and fault injection with production A/B store."""
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
int lengths[4], fail_write=-1, fail_read=-1, apply_fail, writes;
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
    fail_write=fail_read=-1; apply_fail=0; writes=0;
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
    CHECK(!rdx_hogp_keymap_config_factory_reset());
    CHECK(s_rdx_hogpkm_factory_reset);
    CHECK(!s_rdx_hogpkm_pending.frame.valid && !s_rdx_hogpkm_cache.valid);
    CHECK(!memcmp(applied,s_rdx_hogpkm_default_keymap,35));
    CHECK(!rdx_hogpkm_store_load(&boot));
    CHECK(boot.revision==8 && !memcmp(boot.payload,s_rdx_hogpkm_default_keymap,35));
    int before=writes;
    CHECK(!rdx_hogp_keymap_config_factory_reset() && writes==before);
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
int result, reset_calls, reset_attempts, ack, freed;
int rdx_ble_session_rdx_token_resolve(rdx_ble_async_token_t *p,int ready) { return p->valid; }
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
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='keymap-reset-') as tmp:
    path = Path(tmp) / 'checks.c'
    path.write_text(code,encoding='utf-8')
    run_c_checks(path, ['check_reset','check_failures','check_callback'], native=True)
print('Keymap factory reset behavior passed.')
