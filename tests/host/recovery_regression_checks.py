"""对重新生成的实际静态库执行恢复门禁、缓存结果、定时器及播放回归。"""
import ctypes
import re
import subprocess
from llvmlite import binding as llvm
from host_c_test_lib import ROOT, function


def run_recovery_checks():
    directory = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    audit = ROOT / 'cache/recovery-regression'
    audit.mkdir(parents=True, exist_ok=True)
    subprocess.run(['C:/JL/pi32/bin/llvm-ar.exe', 'x',
                    str(directory / 'librdxApp_patched.a'), 'rdx_uxfile.c.o'],
                   cwd=audit, check=True, capture_output=True)
    subprocess.run(['C:/JL/pi32/bin/clang.exe', '-S', '-emit-llvm',
                    '-target', 'pi32v2', '-mcpu=r3', '-x', 'ir',
                    str(audit / 'rdx_uxfile.c.o'), '-o', str(audit / 'actual.ll')],
                   check=True, capture_output=True)
    ir = (audit / 'actual.ll').read_text(encoding='utf-8')

    def extract(name):
        text = re.search(r'^define [^\n]*@' + re.escape(name) +
                         r'\([^\n]*\).*?^}', ir, re.M | re.S).group()
        text = re.sub(r', ![a-z_.]+ !\d+| !dbg !\d+| #\d+', '', text)
        text = re.sub(r'^.*@llvm\.(?:dbg|lifetime)\.[^\n]*\n', '', text, flags=re.M)
        return text.replace('define internal ', 'define ')

    def globals_for(names):
        return '\n'.join(re.sub(r', !dbg !\d+', '', re.search(
            r'^@' + re.escape(name) + r' = .*$', ir, re.M).group()) for name in names)

    def types_for(names):
        return '\n'.join(re.search(r'^%struct\.' + name + r' = type .*$',
                                   ir, re.M).group() for name in names)

    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()

    def engine_for(source):
        target = llvm.Target.from_default_triple().create_target_machine()
        module = llvm.parse_assembly(source)
        module.triple = llvm.get_default_triple()
        module.data_layout = str(target.target_data)
        module.verify()
        engine = llvm.create_mcjit_compiler(module, target)
        engine.finalize_object()
        return engine

    def integer(engine, name):
        return ctypes.c_int.from_address(engine.get_global_value_address(name))

    index_gate = engine_for('''
@g_sync_state = global i32 0
@g_dat_need_upgrade_rebuild = global i1 false
''' + extract('rdx_uxfile_is_index_active'))
    index_busy = ctypes.CFUNCTYPE(ctypes.c_uint8)(
        index_gate.get_function_address('rdx_uxfile_is_index_active'))
    for state in range(6):
        integer(index_gate, 'g_sync_state').value = state
        assert bool(index_busy()) == (1 <= state <= 3)
    integer(index_gate, 'g_sync_state').value = 0
    ctypes.c_bool.from_address(index_gate.get_global_value_address(
        'g_dat_need_upgrade_rebuild')).value = True
    assert index_busy()

    service = '''
@service = global i32 0
@io_users = global i32 0
define i32 @rdx_record_format_service_status() {
 %s = load i32, i32* @service
 ret i32 %s
}
define i32 @rdx_record_format_io_enter() {
 %s = load i32, i32* @service
 %ready = icmp eq i32 %s, 0
 br i1 %ready, label %enter, label %done
enter:
 %users = load i32, i32* @io_users
 %next = add i32 %users, 1
 store i32 %next, i32* @io_users
 br label %done
done:
 ret i32 %s
}
define void @rdx_record_format_io_leave() {
 %users = load i32, i32* @io_users
 %next = sub i32 %users, 1
 store i32 %next, i32* @io_users
 ret void
}
'''
    timer = engine_for(service + '''
@rdx_storage_quiet = global i32 0
@rdx_storage_refresh = global i32 0
@scheduled = global i32 0
@flushed = global i32 0
define zeroext i16 @sys_timeout_add(i8* %p, void(i8*)* %f, i32 %ms) {
 store i32 1, i32* @scheduled
 ret i16 1
}
define void @rdx_uxfile_flush_cache_timer_cb_impl(i8* %p) {
 store i32 1, i32* @flushed
 ret void
}
''' + extract('rdx_uxfile_flush_cache_timer_cb'))
    tick = ctypes.CFUNCTYPE(None, ctypes.c_void_p)(
        timer.get_function_address('rdx_uxfile_flush_cache_timer_cb'))
    for state, quiet, refresh, scheduled, flushed in [
        (1, 0, 0, 1, 0), (0, 0, 0, 0, 1), (-1, 0, 0, 0, 0),
        (1, 1, 0, 0, 0), (1, 2, 0, 0, 0), (0, 0, 1, 1, 0),
        (1, 0, -1, 0, 0),
    ]:
        for name, value in [('service', state), ('rdx_storage_quiet', quiet),
                            ('rdx_storage_refresh', refresh), ('scheduled', 0), ('flushed', 0)]:
            integer(timer, name).value = value
        tick(None)
        assert integer(timer, 'scheduled').value == scheduled
        assert integer(timer, 'flushed').value == flushed
        assert integer(timer, 'io_users').value == 0

    gate_types = types_for(['uxfile_data_t', 'uxfile_datfile_info_t'])
    gate_globals = globals_for(['uxReadFileData', 'uxDatFileInfo', 'rdx_empty_file', 'rdx_empty_list'])
    gate_stubs = ''
    gate_fns = ''
    for name, ret, args, shared in [
        ('get_file_data_by_sn', 'uxfile_data_t', 'i32 %s, i8 %t, i32 %o', 'uxReadFileData'),
        ('get_file_data_by_filename', 'uxfile_data_t', 'i8* %name', 'uxReadFileData'),
        ('get_datFileInfo', 'uxfile_datfile_info_t', '', 'uxDatFileInfo'),
    ]:
        full = 'rdx_uxfile_' + name
        fn_name = full + '_adv_impl'
        gate_fns += extract(fn_name) + '\n'
        gate_stubs += f'define %struct.{ret}* @{full}_ready({args}) {{ ret %struct.{ret}* @{shared} }}\n'
    gate = engine_for(service + gate_types + '\n' + gate_globals + '\n' + gate_stubs + gate_fns)
    # 所有忙/失败结果都必须指向独立的全零对象，且保留旧传输快照。
    for state in [1, -1]:
        integer(gate, 'service').value = state
        integer(gate, 'uxReadFileData').value = 123
        integer(gate, 'uxDatFileInfo').value = 456
        for name, argtypes, args, shared, empty in [
            ('get_file_data_by_sn_adv_impl', [ctypes.c_uint, ctypes.c_ubyte, ctypes.c_int], [999, 0, 0], 'uxReadFileData', 'rdx_empty_file'),
            ('get_file_data_by_filename_adv_impl', [ctypes.c_void_p], [None], 'uxReadFileData', 'rdx_empty_file'),
            ('get_datFileInfo_adv_impl', [], [], 'uxDatFileInfo', 'rdx_empty_list'),
        ]:
            call = ctypes.CFUNCTYPE(ctypes.c_void_p, *argtypes)(gate.get_function_address('rdx_uxfile_' + name))
            result = call(*args)
            assert result == gate.get_global_value_address(empty)
            assert result != gate.get_global_value_address(shared)
            assert ctypes.c_uint.from_address(result).value == 0
        assert integer(gate, 'uxReadFileData').value == 123
        assert integer(gate, 'uxDatFileInfo').value == 456
        assert integer(gate, 'io_users').value == 0

    format_gate = engine_for(service + '''
@format_called = global i32 0
@format_inflight = global i32 0
define i32 @rdx_uxfile_sd_format_ready(void (i8)* %callback) {
 store i32 1, i32* @format_called
 %users = load i32, i32* @io_users
 store i32 %users, i32* @format_inflight
 ret i32 12
}
''' + extract('rdx_uxfile_sd_format_adv_impl'))
    format_call = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)(
        format_gate.get_function_address('rdx_uxfile_sd_format_adv_impl'))
    for state in (1, -1, 0):
        integer(format_gate, 'service').value = state
        integer(format_gate, 'format_called').value = 0
        assert format_call(None) == (-1 if state else 12)
        assert integer(format_gate, 'format_called').value == (0 if state else 1)
        assert integer(format_gate, 'io_users').value == 0
    assert integer(format_gate, 'format_inflight').value == 1

    snapshot = engine_for(types_for(['OS_SEM']) + '\n' +
                          globals_for(['dat_opera_mutex', 'rdx_storage_io_error']) + '''
@locked = global i32 0
@flush_result = global i32 0
@flush_io_error = global i32 0
@flush_locked = global i32 0
define i32 @os_mutex_pend(%struct.OS_SEM* %mutex, i32 %timeout) {
 store i32 1, i32* @locked
 ret i32 0
}
define i32 @os_mutex_post(%struct.OS_SEM* %mutex) {
 store i32 0, i32* @locked
 ret i32 0
}
define i32 @rdx_uxfile_flush_cache() {
 %lock = load i32, i32* @locked
 store i32 %lock, i32* @flush_locked
 %error = load i32, i32* @flush_io_error
 store i32 %error, i32* @rdx_storage_io_error
 %result = load i32, i32* @flush_result
 ret i32 %result
}
''' + extract('rdx_uxfile_download_snapshot'))
    snapshot_call = ctypes.CFUNCTYPE(ctypes.c_int)(
        snapshot.get_function_address('rdx_uxfile_download_snapshot'))
    for result, io_error in ((0, 0), (-1, 0), (0, -1)):
        integer(snapshot, 'flush_result').value = result
        integer(snapshot, 'flush_io_error').value = io_error
        assert snapshot_call() == (-2 if result or io_error else 0)
        assert integer(snapshot, 'locked').value == 0
        assert integer(snapshot, 'flush_locked').value == 1

    init = engine_for(types_for(['dat_entry_t', 'uxfile_filelist_t']) + '\n' +
                      globals_for(['g_dat_cache.0', 'g_dat_cache.1', 'g_dat_cache.3',
                                   'dat_init_flag', 'g_uxfile_all_size', 'g_uxfile_dat_cout',
                                   'uxFileList', '.str.111', '.str.112', 'rdx_cache_load_result']) + '''
@load_result = global i32 -1
define internal fastcc i32 @rdx_dat_cache_load() {
 store i1 true, i1* @g_dat_cache.3
 %r = load i32, i32* @load_result
 ret i32 %r
}
define i32 @syscfg_read(i16 %id, i8* %p, i16 %n) { ret i32 -1 }
define void @log_print(i32 %level, i8* %tag, i8* %fmt, ...) { ret void }
''' + extract('rdx_uxfile_dat_init_impl'))
    initialize = ctypes.CFUNCTYPE(ctypes.c_int)(init.get_function_address('rdx_uxfile_dat_init_impl'))
    assert initialize() == -1
    assert not ctypes.c_bool.from_address(init.get_global_value_address('g_dat_cache.3')).value
    assert not ctypes.c_bool.from_address(init.get_global_value_address('dat_init_flag')).value
    integer(init, 'load_result').value = 2
    assert initialize() == 0
    assert ctypes.c_bool.from_address(init.get_global_value_address('dat_init_flag')).value

    # 使用实际缓存布局和实际导航代码，覆盖空洞、环回、无可播放文件。
    neighbor_ir = service + types_for(['dat_entry_t', 'OS_SEM']) + '\n' + globals_for([
        'g_dat_cache.0', 'g_dat_cache.1', 'g_dat_cache.3', 'dat_opera_mutex']) + '''
@test_entries = global [3 x %struct.dat_entry_t] zeroinitializer
define i32 @os_mutex_pend(%struct.OS_SEM* %p, i32 %n) { ret i32 0 }
define i32 @os_mutex_post(%struct.OS_SEM* %p) { ret i32 0 }
define void @seed() {
 %p = getelementptr [3 x %struct.dat_entry_t], [3 x %struct.dat_entry_t]* @test_entries, i32 0, i32 0
 store %struct.dat_entry_t* %p, %struct.dat_entry_t** @g_dat_cache.0
 store i16 3, i16* @g_dat_cache.1
 store i1 true, i1* @g_dat_cache.3
 %a = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %p, i32 0, i32 0
 %b = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %p, i32 1, i32 0
 %c = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %p, i32 2, i32 0
 store i32 1, i32* %a
 store i32 268435457, i32* %b
 store i32 2147483646, i32* %c
 %av = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %p, i32 0, i32 8
 %bv = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %p, i32 1, i32 8
 store i8 1, i8* %av
 store i8 1, i8* %bv
 ret void
}
@load_calls = global i32 0
@load_result = global i32 3
define internal fastcc i32 @rdx_dat_cache_load() {
 %calls = load i32, i32* @load_calls
 %next = add i32 %calls, 1
 store i32 %next, i32* @load_calls
 %result = load i32, i32* @load_result
 %ok = icmp sge i32 %result, 0
 br i1 %ok, label %restore, label %done
restore:
 call void @seed()
 %count = trunc i32 %result to i16
 store i16 %count, i16* @g_dat_cache.1
 br label %done
done:
 ret i32 %result
}
''' + extract('rdx_uxfile_neighbor_sn')
    playback = (directory / 'rdx_playback.c').read_text(encoding='utf-8')
    c_source = r'''
typedef unsigned int u32;
typedef unsigned short u16;
typedef int pb_direction_t, pb_state_t, pb_candidate_result_t;
typedef struct { char filename[16]; } uxfile_data_t;
#define PB_CANDIDATE_STARTED 1
#define PB_CANDIDATE_FATAL 2
#define PB_RESULT_OK 0
#define PB_RESULT_NO_FILE (-1)
#define PB_LOG(...) ((void)0)
static struct { int state,last_error; u16 total_count; } pb;
static int probes, start_result;
static u32 selected;
static uxfile_data_t yes={{'a',0}};
u32 rdx_uxfile_neighbor_sn(u32 sn, int direction);
void seed(void);
uxfile_data_t *rdx_uxfile_get_file_data_by_sn(u32 sn,int a,int b) {
 ++probes; selected=sn; return &yes;
}
int pb_start_candidate(uxfile_data_t *f,int s) { return start_result; }
void pb_restore_stable_state(int s) { pb.state=s; }
'''
    for name in ['pb_find_latest_valid_sn', 'pb_step_probe_sn', 'pb_navigate_from']:
        c_source += function(playback, name)
    c_source += r'''
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
int check_navigation(void) {
 seed(); pb.total_count=2; start_result=1; probes=0;
 CHECK(pb_find_latest_valid_sn()==0x10000001u);
 CHECK(pb_navigate_from(0x10000001u,-1,0,0)==0 && selected==1 && probes==1);
 CHECK(pb_navigate_from(1,1,0,0)==0 && selected==0x10000001u);
 CHECK(pb_navigate_from(1,-1,0,0)==0 && selected==0x10000001u);
 CHECK(pb_navigate_from(0x10000001u,1,0,0)==0 && selected==1);
 CHECK(pb_navigate_from(100,-1,0,0)==0 && selected==1);
 start_result=0; probes=0;
 CHECK(pb_navigate_from(1,1,0,0)==PB_RESULT_NO_FILE && probes==2);
 probes=0;
 CHECK(pb_navigate_from(1,1,1,0)==PB_RESULT_NO_FILE && probes==1);
 start_result=2; pb.last_error=-7; probes=0;
 CHECK(pb_navigate_from(1,1,0,0)==-7 && probes==1);
 return 0;
}
'''
    (audit / 'playback.c').write_text(c_source, encoding='utf-8')
    subprocess.run(['C:/JL/pi32/bin/clang.exe', '-target', llvm.get_default_triple(),
                    '-O0', '-S', '-emit-llvm', str(audit / 'playback.c'), '-o',
                    str(audit / 'playback.ll')], check=True, capture_output=True)
    c_ir = (audit / 'playback.ll').read_text(encoding='utf-8')
    c_ir = re.sub(r' "target-(?:cpu|features)"="[^"]*"', '', c_ir)
    module = llvm.parse_assembly(c_ir)
    other = llvm.parse_assembly(neighbor_ir)
    other.triple = module.triple
    other.data_layout = module.data_layout
    module.link_in(other)
    navigation = engine_for(str(module))
    check = ctypes.CFUNCTYPE(ctypes.c_int)(navigation.get_function_address('check_navigation'))
    assert check() == 0
    neighbor = ctypes.CFUNCTYPE(ctypes.c_uint, ctypes.c_uint, ctypes.c_int)(
        navigation.get_function_address('rdx_uxfile_neighbor_sn'))
    # 录音保存后缓存失效，首次播放必须重新加载；失败后允许重试。
    loaded = ctypes.c_bool.from_address(navigation.get_global_value_address('g_dat_cache.3'))
    loaded.value = False
    integer(navigation, 'load_result').value = -1
    assert neighbor(0, -1) == 0
    assert integer(navigation, 'load_calls').value == 1
    integer(navigation, 'load_result').value = 3
    assert neighbor(0, -1) == 0x10000001
    assert integer(navigation, 'load_calls').value == 2
    assert neighbor(1, 1) == 0x10000001
    assert integer(navigation, 'load_calls').value == 2
    loaded.value = False
    integer(navigation, 'service').value = 1
    assert neighbor(0, -1) == 0
    assert integer(navigation, 'load_calls').value == 2
    integer(navigation, 'service').value = -1
    assert neighbor(0, -1) == 0
    assert integer(navigation, 'load_calls').value == 2
    integer(navigation, 'service').value = 0
    integer(navigation, 'load_result').value = 0
    assert neighbor(0, -1) == 0
    assert integer(navigation, 'load_calls').value == 3
    ctypes.c_ushort.from_address(navigation.get_global_value_address('g_dat_cache.1')).value = 0
    assert neighbor(0, -1) == 0
    print('Recovery regression: actual archive gates, init failure, timer retry and sparse playback passed.')


if __name__ == '__main__':
    run_recovery_checks()
