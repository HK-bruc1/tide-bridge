"""执行实际 APP 删除状态机和恢复补丁的 LLVM 包装器。"""
import ctypes
import re
from llvmlite import binding as llvm
from host_c_test_lib import ROOT, function, run_c_checks


def run_delete_checks():
    directory = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    source = (directory / 'rdx_app.c').read_text(encoding='utf-8')
    request = re.search(r'typedef struct \{\s*ProtocolFileDeleteParams params;\s*rdx_ble_async_token_t token;\s*int result;\s*int previous_service;\s*\} rdx_app_file_delete_request_t;', source).group(0)
    stubs = r'''
#define RDX_RECORD_DELETE_REJECTED (-2)
#define RDX_STORAGE_PLAYBACK_PREEMPT 2
#define Q_CALLBACK 1
typedef struct { unsigned int file_sn; char file_name[64]; } ProtocolFileDeleteParams;
typedef int rdx_ble_async_token_t;
static int state, busy, queued, checked, ack, freed, token_valid=1;
void free(void *p) { ++freed; }
int rdx_ble_session_rdx_token_resolve(int *p,int n) { return token_valid; }
int rdx_record_format_service_status(void) { return state; }
void rdx_record_format_service_ready(int s) { state=s; }
int rdx_app_storage_activity_is_busy(const char *s,int a,int b) { return busy; }
int rdx_uxfile_delete_submit(void *p) { return queued; }
int rdx_uxfile_delete_checked(unsigned int sn,const char *name) { return checked; }
int os_taskq_post_type(const char *s,int t,int n,int *m) { return 1; }
'''
    complete = r'''
static void rdx_app_file_delete_complete(rdx_app_file_delete_request_t *r) { ack=r->result; }
'''
    tests = r'''
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)
int test_delete_admission(void) {
    rdx_app_file_delete_request_t r={0};
    for(int initial=-1;initial<=0;++initial) {
        state=initial; busy=0; queued=-1; ack=0;
        rdx_app_file_delete_on_app_core(&r);
        CHECK(state==initial && ack<0);
        queued=0;
        rdx_app_file_delete_on_app_core(&r);
        CHECK(state==1 && r.previous_service==initial);
        checked=RDX_RECORD_DELETE_REJECTED;
        rdx_app_file_delete_worker(&r);
        CHECK(state==initial && r.result==RDX_RECORD_DELETE_REJECTED);
    }
    state=0; queued=0; rdx_app_file_delete_on_app_core(&r);
    checked=-1; rdx_app_file_delete_worker(&r); CHECK(state==-1);
    rdx_app_file_delete_on_app_core(&r);
    checked=0; rdx_app_file_delete_worker(&r); CHECK(state==0);
    state=1; ack=0; rdx_app_file_delete_on_app_core(&r);
    CHECK(state==1 && ack<0);
    return 0;
}
'''
    path = ROOT / 'cache/record-format-tests/delete_app.c'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(stubs + request + complete +
                    function(source, 'rdx_app_file_delete_worker') +
                    function(source, 'rdx_app_file_delete_on_app_core') + tests, encoding='utf-8')
    run_c_checks(path, ['test_delete_admission'])

    # 直接执行补丁中的函数；模拟锁、刷写和缓存加载的结果。
    patch = (directory / 'patch_librdxApp_recovery.ps1').read_text(encoding='utf-8-sig')
    actual_ir = re.search(r'define i32 @rdx_uxfile_delete_checked\(.*?^\}', patch, re.M | re.S).group(0)
    ir = r'''
%struct.OS_SEM = type { i32 }
@dat_opera_mutex = global %struct.OS_SEM zeroinitializer
@rdx_storage_io_error = global i32 0
@g_dat_cache.3 = global i1 true
@dat_init_flag = global i1 true
@flush_result = global i32 0
@delete_result = global i32 0
@init_result = global i32 0
@init_loaded = global i1 true
@init_io_error = global i32 0
@reload_count = global i32 0
@lock_count = global i32 0
define i32 @os_mutex_pend(%struct.OS_SEM* %p,i32 %n) {
  store i32 1,i32* @lock_count
  ret i32 0
}
define i32 @os_mutex_post(%struct.OS_SEM* %p) {
  store i32 0,i32* @lock_count
  ret i32 0
}
define i32 @rdx_uxfile_flush_cache() {
  %r=load i32,i32* @flush_result
  ret i32 %r
}
define i32 @rdx_record_format_delete(i32 %sn,i8* %name) {
  %r=load i32,i32* @delete_result
  ret i32 %r
}
define void @rdx_uxfile_close_read_file_handle() { ret void }
define void @rdx_uxfile_datFileInfo_sendBuf_free() { ret void }
define void @rdx_uxfile_invalidate_dat_cache() {
  store i1 false,i1* @g_dat_cache.3
  ret void
}
define i32 @rdx_uxfile_dat_init_impl() {
  store i32 1,i32* @reload_count
  %loaded=load i1,i1* @init_loaded
  store i1 %loaded,i1* @g_dat_cache.3
  %io=load i32,i32* @init_io_error
  store i32 %io,i32* @rdx_storage_io_error
  %r=load i32,i32* @init_result
  ret i32 %r
}
'''
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
    target = llvm.Target.from_default_triple().create_target_machine()
    module = llvm.parse_assembly(ir + actual_ir)
    module.triple = llvm.get_default_triple()
    module.data_layout = str(target.target_data)
    module.verify()
    engine = llvm.create_mcjit_compiler(module, target)
    engine.finalize_object()
    checked_fn = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_uint, ctypes.c_void_p)(
        engine.get_function_address('rdx_uxfile_delete_checked'))

    def value(name, byte=False):
        kind = ctypes.c_uint8 if byte else ctypes.c_int
        return kind.from_address(engine.get_global_value_address(name))

    for deleted, flush, loaded, io_error, init, expected in [
            (-2, 0, 1, 0, 0, -2), (0, 0, 1, 0, 0, 0),
            (-1, 0, 1, 0, 0, -1), (-2, 0, 0, 0, 0, -1),
            (-2, 0, 1, -1, 0, -1), (-2, 0, 1, 0, -1, -1),
            (0, -1, 1, 0, 0, -1)]:
        for name, number in [('delete_result', deleted), ('flush_result', flush),
                             ('init_io_error', io_error), ('init_result', init),
                             ('reload_count', 0)]:
            value(name).value = number
        value('init_loaded', True).value = loaded
        assert checked_fn(7, None) == expected
        assert value('lock_count').value == 0
        assert value('reload_count').value == int(flush == 0 and deleted in (0, -2))
    print('Delete: rejected identity, queue failure, prior fault, cache reload and I/O failures passed.')
