"""Execute the production rectone parser, VM commit and token-bound dispatcher."""
import re
import ctypes
import tempfile
from pathlib import Path
from llvmlite import binding as llvm
from host_c_test_lib import ROOT, function, run_c_checks

BASE = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'

STUBS = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int bool;
typedef unsigned long long size_t;
void *malloc(size_t);
void free(void *);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
int strcmp(const char *, const char *);
int snprintf(char *, size_t, const char *, ...);
#define printf(...) ((void)0)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define NULL ((void *)0)
#define THIRD_PARTY_PROTOCOLS_SEL 1
#define RDX_EN 1
#define VM_RDX_RECORD_TONE 167
#define CUSTOM_VALUE_MAX_LENGTH 100
#define Q_CALLBACK 1
typedef struct { u8 slot_index; u32 slot_generation, transport_epoch; } rdx_ble_async_token_t;
typedef struct { u8 rdx_ccc_configured; u16 mtu_size; } rdx_ble_link_state_t;
static rdx_ble_async_token_t owner={0,1,1};
static rdx_ble_link_state_t link={1,244};
static struct { int goto_poweroff_flag; } app_var;
static int live=1, recording, dut, keys, factory, file_busy, storage, formatting, ota, poweroff;
static int fail_alloc, fail_queue, fail_send, allocations, frees, sends, writes, reads;
static int fail_write, fail_read, bad_read, revoke_busy, revoke_write, flash_len;
static u8 flash;
static char wire[40];
static u32 wire_len;
static void *last_alloc, *queued;
static void *test_malloc(size_t n) {
    if (fail_alloc) return NULL;
    last_alloc=malloc(n); ++allocations; return last_alloc;
}
static void test_free(void *p) { ++frees; free(p); }
#define malloc test_malloc
#define free test_free
static int os_taskq_post_type(const char *task,int type,int n,int *msg) {
    if (strcmp(task,"app_core") || type!=Q_CALLBACK || n!=3 || msg[1]!=1) return -1;
    if (fail_queue || queued) return -1;
    queued=last_alloc; return 0;
}
static int syscfg_read(int id,void *p,int n) {
    ++reads;
    if (id!=VM_RDX_RECORD_TONE || fail_read || flash_len!=n) return -1;
    *(u8 *)p=bad_read ? 0xff : flash; return n;
}
static int syscfg_write(int id,const void *p,int n) {
    ++writes;
    if (id!=VM_RDX_RECORD_TONE || fail_write) return 0;
    flash=*(const u8 *)p; flash_len=n;
    if(revoke_write) { live=0; ++owner.transport_epoch; }
    return n;
}
static rdx_ble_link_state_t *rdx_ble_session_rdx_token_resolve(const rdx_ble_async_token_t *t,int ready) {
    return live && t && t->slot_index==owner.slot_index &&
        t->slot_generation==owner.slot_generation && t->transport_epoch==owner.transport_epoch ? &link : NULL;
}
static int rdx_ble_session_rdx_token_capture(rdx_ble_async_token_t *t,int ready) { *t=owner; return live; }
static int rdx_ble_server_send_for_token(u8 *p,u32 n,const rdx_ble_async_token_t *t) {
    if(fail_send || !rdx_ble_session_rdx_token_resolve(t,1) || !link.rdx_ccc_configured ||
       n>link.mtu_size || wire_len+n>34) return -1;
    memcpy(wire+wire_len,p,n); wire_len+=n; wire[wire_len]=0; ++sends; return 0;
}
static u8 get_ota_status(void) { return ota; }
static bool rdx_app_get_poweroff_flag(void) { return poweroff; }
static int rdx_dut_is_in_mode(void) { return dut; }
static int rdx_dut_test_keys_active(void) { return keys; }
static int rdx_factory_audio_owned(void) { return factory; }
static int rdx_factory_audio_file_busy(void) { return file_busy; }
static int rdx_storage_lifecycle_business_blocked(void) { return storage; }
static int rdx_uxfile_is_formatting(void) { return formatting; }
static int rdx_record_tone_config_busy(void) { if(revoke_busy) live=0; return recording; }
'''

TESTS = r'''
static void pump(void) {
    void *p=queued; queued=NULL;
    if(p) rdx_record_tone_execute(p);
}
static void request(const char *s) { wire_len=0; wire[0]=0; rdx_record_tone_handle_custom(s); pump(); }
static void boot(int len,int value) {
    live=link.rdx_ccc_configured=1; link.mtu_size=244; owner=(rdx_ble_async_token_t){0,1,1};
    recording=dut=keys=factory=file_busy=storage=formatting=ota=poweroff=0;
    app_var.goto_poweroff_flag=0;
    fail_alloc=fail_queue=fail_send=fail_write=fail_read=bad_read=revoke_busy=revoke_write=0;
    allocations=frees=sends=writes=reads=wire_len=0; wire[0]=0; queued=NULL;
    flash_len=len; flash=value; rdx_record_tone_init();
}
int test_boot_get_and_durability(void) {
    for(int mode=0;mode<4;++mode) {
        boot(mode==0?0:1,mode==1?255:mode==2?0:1);
        CHECK(rdx_record_tone_enabled()==(mode==2?0:1));
        int count=reads;
        request("get,001");
        CHECK(!strcmp(wire,mode==2?"*DEV#custom#rectone#001,ok,0#":"*DEV#custom#rectone#001,ok,1#"));
        CHECK(reads==count && !writes && allocations==frees);
        request(mode==2?"set,2,0":"set,2,1");
        CHECK(writes==(mode<2) && tone_durable);
        request(mode==2?"set,3,1":"set,3,0");
        CHECK(writes==(mode<2?2:1));
        rdx_record_tone_init(); CHECK(tone_enabled==(mode==2?1:0));
    }
    boot(1,0); fail_read=1; rdx_record_tone_init();
    CHECK(tone_enabled==1 && !tone_durable && !writes);
    fail_read=0; request("set,4,1"); CHECK(writes==1 && tone_durable && flash==1);
    return 0;
}
int test_parser_and_limits(void) {
    const char *bad[]={"GET,001","get,001,0","get,001,","get,001 ","set,001", "set,001,", "set,001,00",
        "set,001,2","set,001,-1","set,001,1,0", "set ,001,1","x,001","set,001, 1","get,001#"};
    boot(1,1);
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        int before=sends; request(bad[i]);
        if(i==3 || i==13) CHECK(sends==before); /* invalid characters in the ID */
        else CHECK(sends==before+1 && !strcmp(wire,"*DEV#custom#rectone#001,arg,1#"));
        CHECK(!writes);
    }
    const char *noid[]={"get","get,","get,0","get,00000","get,65536","get,000001", "get,+1","get,-1","get, 1","get,a"};
    for(unsigned i=0;i<sizeof(noid)/sizeof(noid[0]);++i) { int n=sends; request(noid[i]); CHECK(n==sends && !writes); }
    request("set,65535,0"); CHECK(!strcmp(wire,"*DEV#custom#rectone#65535,ok,0#"));
    request("get,00001"); CHECK(!strcmp(wire,"*DEV#custom#rectone#00001,ok,0#"));
    fail_write=1; request("set,65535,1");
    CHECK(!strcmp(wire,"*DEV#custom#rectone#65535,vmerr,0#"));
    char long_value[101]; memset(long_value,'x',100); memcpy(long_value,"set,12345,",10); long_value[100]=0;
    request(long_value); CHECK(!strcmp(wire,"*DEV#custom#rectone#12345,arg,0#"));
    CHECK(allocations==frees);
    return 0;
}
int test_busy_and_factory_reset(void) {
    int *gates[]={&recording,&dut,&keys,&factory,&file_busy,&storage,&formatting,&ota,&poweroff,&app_var.goto_poweroff_flag};
    for(unsigned i=0;i<sizeof(gates)/sizeof(gates[0]);++i) {
        boot(1,1); *gates[i]=1;
        request("get,1"); CHECK(!strcmp(wire,"*DEV#custom#rectone#1,ok,1#"));
        request("set,2,0"); CHECK(!writes && !strcmp(wire,"*DEV#custom#rectone#2,busy,1#"));
        *gates[i]=0; request("set,3,0"); CHECK(writes==1 && tone_enabled==0);
    }
    boot(1,0); CHECK(!rdx_record_tone_factory_reset() && flash==1 && tone_enabled==1);
    request("set,1,0"); CHECK(writes==1 && !strcmp(wire,"*DEV#custom#rectone#1,busy,1#"));
    rdx_record_tone_init(); request("set,2,0"); CHECK(writes==2 && tone_enabled==0);
    fail_write=1; CHECK(rdx_record_tone_factory_reset()==-1 && !tone_resetting && tone_enabled==0);
    fail_write=0; dut=1; CHECK(!rdx_record_tone_factory_reset()); /* existing packaging entry */
    return 0;
}
int test_vm_failures_and_reconciliation(void) {
    for(int mode=0;mode<3;++mode) {
        boot(1,1); fail_write=mode==0; fail_read=mode==1; bad_read=mode==2;
        request("set,1,0"); CHECK(tone_enabled==1 && !tone_durable && writes==1);
        CHECK(!strcmp(wire,"*DEV#custom#rectone#1,vmerr,1#"));
        int n=reads; request("get,2"); CHECK(reads==n && !strcmp(wire,"*DEV#custom#rectone#2,ok,1#"));
        fail_write=fail_read=bad_read=0;
        request("set,3,1"); CHECK(writes==2 && flash==1 && tone_durable);
        request("set,4,1"); CHECK(writes==2);
    }
    boot(1,1); fail_read=1; request("set,1,0"); CHECK(flash==0 && tone_enabled==1);
    fail_read=0; rdx_record_tone_init(); CHECK(tone_enabled==0);
    return 0;
}
int test_token_queue_and_send_failures(void) {
    for(int mode=0;mode<4;++mode) {
        boot(1,1); rdx_record_tone_handle_custom("set,001,0"); CHECK(queued);
        if(mode==0) live=0;
        if(mode==1) ++owner.transport_epoch;
        if(mode==2) ++owner.slot_generation;
        if(mode==3) link.rdx_ccc_configured=0;
        pump(); CHECK(!writes && !sends && allocations==frees && tone_enabled==1);
    }
    boot(1,1); revoke_busy=1; request("set,1,0"); CHECK(!writes && !sends);
    boot(1,1); revoke_write=1; request("set,1,0"); CHECK(writes==1 && !sends && tone_enabled==0);
    live=1; request("get,2"); CHECK(!strcmp(wire,"*DEV#custom#rectone#2,ok,0#"));
    boot(1,1); fail_send=1; request("set,1,0"); CHECK(writes==1 && tone_enabled==0 && !sends);
    fail_send=0; request("set,2,0"); CHECK(writes==1 && !strcmp(wire,"*DEV#custom#rectone#2,ok,0#"));
    boot(1,1); fail_queue=1; request("set,1,0"); CHECK(!writes && allocations==frees);
    fail_queue=0; fail_alloc=1; request("set,1,0"); CHECK(!writes && allocations==frees);
    fail_alloc=0; link.rdx_ccc_configured=0; request("set,1,0"); CHECK(!writes && allocations==frees);
    return 0;
}
int test_default_mtu_fragments(void) {
    boot(1,1); link.mtu_size=20;
    request("set,65535,0"); CHECK(sends==2 && !strcmp(wire,"*DEV#custom#rectone#65535,ok,0#"));
    link.mtu_size=19; request("get,1"); CHECK(sends==2 && !wire_len);
    return 0;
}
'''


def main():
    crt = ctypes.CDLL('msvcrt')
    llvm.add_symbol('snprintf', ctypes.cast(crt._snprintf, ctypes.c_void_p).value)
    module = (BASE / 'rdx_record_tone.c').read_text(encoding='utf-8')
    ble = (BASE / 'rdx_ble_server.c').read_text(encoding='utf-8')
    source = (STUBS + function(ble, 'rdx_ble_server_send_text_for_token') +
              re.sub(r'^#include[^\n]*', '', module, flags=re.M) + TESTS)
    vm = (BASE / 'rdx_vm.c').read_text(encoding='utf-8')
    app = (BASE / 'rdx_app.c').read_text(encoding='utf-8')
    assert 'rdx_record_tone_init();' in function(vm, 'rdx_vm_init')
    assert 'rdx_record_tone_init' not in function(vm, 'rdx_vm_bound_transition_lock')
    reset = function(vm, 'rdx_vm_reset_defaults_no_poweroff')
    assert reset.index('rdx_record_tone_factory_reset') < reset.index('rdx_vm_ble_pairing_state_reset')
    assert 'rdx_record_tone_factory_reset' not in (BASE / 'rdx_hogp_keymap_config.c').read_text(encoding='utf-8')
    custom = function(app, 'rdx_app_custom_command_parse')
    assert custom.index('RDX_BLE_RUNTIME_ACTIVE') < custom.index('RDX_RECORD_TONE_CUSTOM_CMD')
    assert 'rdx_record_tone_handle_custom(value)' in custom
    assert 'rdx_record_tone.c' in (ROOT / 'SDK/Makefile').read_text(encoding='utf-8')
    record = (BASE / 'rdx_record.c').read_text(encoding='utf-8')
    assert len(re.findall(r'if \(rdx_record_start_tone_wait\(\)\)', record)) == 2
    assert not re.search(r'sys_(?:timer|timeout)_add|power_amp_set|audio_dac|custom_msg_indicate', module)
    with tempfile.TemporaryDirectory(prefix='record-tone-') as tmp:
        path = Path(tmp) / 'checks.c'
        path.write_text(source, encoding='utf-8')
        run_c_checks(path, re.findall(r'int (test_\w+)\(void\)', TESTS), native=True)
    print('Recording tone: strict protocol, VM/default/reset, full-idle admission and token-bound request/response passed.')


if __name__ == '__main__':
    main()
