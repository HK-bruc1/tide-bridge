"""Exercise archive admission wrappers and production OTA dispatch."""
import ctypes
import os
import re
import subprocess
import tempfile
from pathlib import Path

from llvmlite import binding as llvm
from host_c_test_lib import ROOT, function, run_c_checks


def run_conflict_checks():
    folder = ROOT / 'SDK/apps/common/third_party_profile/rdx_protocol'
    archive = Path(os.environ.get('RDX_TEST_ARCHIVE', folder / 'librdxApp_patched.a'))
    with tempfile.TemporaryDirectory(prefix='cdc_protocol_') as work:
        directory = Path(work)
        for name in ('protocol', 'uxfile'):
            subprocess.run(['C:/JL/pi32/bin/llvm-ar.exe', 'x',
                            str(archive), f'rdx_{name}.c.o'],
                           cwd=directory, check=True, capture_output=True, timeout=60)
            subprocess.run(['C:/JL/pi32/bin/clang.exe', '-S', '-emit-llvm',
                            '-target', 'pi32v2', '-mcpu=r3', '-x', 'ir',
                            str(directory / f'rdx_{name}.c.o'),
                            '-o', str(directory / f'{name}.ll')],
                           check=True, capture_output=True, timeout=60)
        actual = (directory / 'protocol.ll').read_text(encoding='utf-8')
        uxfile_actual = (directory / 'uxfile.ll').read_text(encoding='utf-8')

    def extract(name, text=actual):
        match = re.search(r'^define [^\n]*@' + re.escape(name) +
                          r'\([^\n]*\).*?^}', text, re.M | re.S)
        assert match, name
        text = re.sub(r', ![a-z_.]+ !\d+| !dbg !\d+| #\d+', '', match.group())
        return text.replace('define internal ', 'define ')

    source = r'''
@download = global i32 0
@users = global i32 0
@format = global i1 false
@ran = global i32 0
@rejected = global i32 0
@unreserved = global i32 0
define i32 @rdx_record_format_protocol_busy() {
 %value = load i32, i32* @download
 ret i32 %value
}
define zeroext i1 @rdx_uxfile_sd_format_status_check() {
 %value = load i1, i1* @format
 ret i1 %value
}
define void @rdx_record_format_command_enter() {
 %value = load i32, i32* @users
 %next = add i32 %value, 1
 store i32 %next, i32* @users
 ret void
}
define i32 @rdx_record_format_io_enter() {
 %value = load i32, i32* @download
 %busy = icmp ne i32 %value, 0
 br i1 %busy, label %reject, label %run
reject:
 ret i32 1
run:
 call void @rdx_record_format_command_enter()
 ret i32 0
}
define void @rdx_record_format_command_leave() {
 call void @rdx_record_format_io_leave()
 ret void
}
define void @rdx_record_format_io_leave() {
 %value = load i32, i32* @users
 %next = sub i32 %value, 1
 store i32 %next, i32* @users
 ret void
}
define void @observe() {
 %value = load i32, i32* @users
 %missing = icmp eq i32 %value, 0
 %result = zext i1 %missing to i32
 store i32 %result, i32* @unreserved
 %count = load i32, i32* @ran
 %next = add i32 %count, 1
 store i32 %next, i32* @ran
 ret void
}
define void @rdx_protocol_file_list_ack_indicate(i8 zeroext %result) {
 %value = zext i8 %result to i32
 store i32 %value, i32* @rejected
 ret void
}
define void @rdx_protocol_record_file_info_indicate_admitted() {
 call void @observe()
 ret void
}
'''
    source += extract('_rdx_is_storage_busy')
    source += extract('_rdx_record_control_parse_busy')
    for name in ('rdx_protocol_recieve_data_handle', 'rdx_protocol_ota_handle'):
        source += extract(name)
        source += f'''
define void @{name}_admitted(i8* %data, i16 zeroext %length) {{
 call void @observe()
 ret void
}}
'''
    source += extract('rdx_protocol_record_file_info_indicate')
    source += extract('rdx_protocol_list_queue_rejected')
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
    target = llvm.Target.from_default_triple().create_target_machine()
    module = llvm.parse_assembly(source)
    module.triple = llvm.get_default_triple()
    module.data_layout = str(target.target_data)
    module.verify()
    engine = llvm.create_mcjit_compiler(module, target)
    engine.finalize_object()

    def integer(name):
        return ctypes.c_int.from_address(engine.get_global_value_address(name))

    reject = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                             ctypes.c_int, ctypes.c_void_p)(
        engine.get_function_address('rdx_protocol_list_queue_rejected'))
    assert reject(None, 0, 0, None) == -1 and integer('rejected').value == 1
    integer('rejected').value = 0
    # 直接读取实际库，确认 GETLIST 没有遗漏的未跟踪生产者或队列失败旁路。
    assert re.search(r'@rdx_adv_uxfile_post\([^\n]*i32 1, i32 233\)', actual)
    assert not re.search(r'@os_taskq_post_msg\([^\n]*i32 1, i32 233\)', actual)
    busy = ctypes.CFUNCTYPE(ctypes.c_bool)(
        engine.get_function_address('_rdx_is_storage_busy'))
    record_parse_busy = ctypes.CFUNCTYPE(ctypes.c_bool)(
        engine.get_function_address('_rdx_record_control_parse_busy'))
    assert not busy()
    assert not record_parse_busy()
    integer('download').value = 1
    assert busy()
    assert not record_parse_busy(), 'CDC must delegate recording controls to app_core'
    integer('download').value = 0
    ctypes.c_bool.from_address(engine.get_global_value_address('format')).value = True
    assert busy() and record_parse_busy()
    integer('download').value = 1
    assert busy() and record_parse_busy(), 'CDC must not hide the original format rejection'
    integer('download').value = 0
    ctypes.c_bool.from_address(engine.get_global_value_address('format')).value = False
    for name in ('rdx_protocol_recieve_data_handle', 'rdx_protocol_ota_handle'):
        entry = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint16)(
            engine.get_function_address(name))
        for download in (0, 1):
            integer('download').value = download
            before = integer('ran').value
            entry(None, 0)
            assert integer('ran').value == before + 1
            assert integer('users').value == 0 and integer('unreserved').value == 0
    entry = ctypes.CFUNCTYPE(None)(
        engine.get_function_address('rdx_protocol_record_file_info_indicate'))
    before = integer('ran').value
    entry()
    assert integer('ran').value == before and integer('rejected').value == 1
    assert integer('users').value == 0
    integer('download').value = 0
    entry()
    assert integer('ran').value == before + 1 and integer('users').value == 0
    parser = extract('rdx_protocol_recieve_data_handle_admitted')
    assert '@_rdx_record_control_parse_busy()' in parser
    assert '@_rdx_is_storage_busy()' in parser
    for response in ('rdx_protocol_file_list_ack_indicate(i8 zeroext 1)',
                     'rdx_protocol_fileTransfer_stop_indicate()',
                     'rdx_protocol_recordDatFileInfo_indicateByRequestNum(i32 0, i32 1)'):
        assert response in parser, response

    # 空间查询由协议解析任务异步转交 UXFILE worker。入口检查无法覆盖
    # “已排队但尚未执行”的窗口，因此执行函数本身必须再次取得存储占用，
    # 并保证 fget_free_space/fget_partition 只在占用有效时运行。
    mem_worker = extract('rdx_uxfile_sd_mem_check', uxfile_actual)
    assert '@rdx_record_format_io_enter()' in mem_worker
    assert '@rdx_record_format_io_leave()' in mem_worker
    assert '@rdx_uxfile_sd_mem_check_ready()' in mem_worker
    assert '@fget_free_space(' not in mem_worker
    ready_worker = extract('rdx_uxfile_sd_mem_check_ready', uxfile_actual)
    assert '@fget_free_space(' in ready_worker
    assert '@fget_partition(' in ready_worker

    mem_source = r'''
@busy = global i32 0
@holds = global i32 0
@max_holds = global i32 0
@ready_calls = global i32 0
@ready_held = global i32 0
define i32 @rdx_record_format_io_enter() {
 %value = load i32, i32* @busy
 %blocked = icmp ne i32 %value, 0
 br i1 %blocked, label %reject, label %acquire
reject:
 ret i32 1
acquire:
 %held = load i32, i32* @holds
 %next = add i32 %held, 1
 store i32 %next, i32* @holds
 store i32 %next, i32* @max_holds
 ret i32 0
}
define void @rdx_record_format_io_leave() {
 %held = load i32, i32* @holds
 %next = sub i32 %held, 1
 store i32 %next, i32* @holds
 ret void
}
define i32 @rdx_uxfile_sd_mem_check_ready() {
 %count = load i32, i32* @ready_calls
 %next = add i32 %count, 1
 store i32 %next, i32* @ready_calls
 %held = load i32, i32* @holds
 %inside = icmp eq i32 %held, 1
 %inside_value = zext i1 %inside to i32
 store i32 %inside_value, i32* @ready_held
 ret i32 0
}
'''
    mem_source += mem_worker
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
    target = llvm.Target.from_default_triple().create_target_machine()
    mem_module = llvm.parse_assembly(mem_source)
    mem_module.triple = llvm.get_default_triple()
    mem_module.data_layout = str(target.target_data)
    mem_module.verify()
    mem_engine = llvm.create_mcjit_compiler(mem_module, target)
    mem_engine.finalize_object()

    def mem_integer(name):
        return ctypes.c_int.from_address(mem_engine.get_global_value_address(name))

    mem_check = ctypes.CFUNCTYPE(ctypes.c_int)(
        mem_engine.get_function_address('rdx_uxfile_sd_mem_check'))
    mem_integer('busy').value = 1
    mem_check()  # worker 忽略返回值，关键是不能执行容量读取。
    assert mem_integer('ready_calls').value == 0
    assert mem_integer('holds').value == 0
    mem_integer('busy').value = 0
    assert mem_check() == 0
    assert mem_integer('ready_calls').value == 1
    assert mem_integer('max_holds').value == 1
    assert mem_integer('ready_held').value == 1
    assert mem_integer('holds').value == 0

    ota = r'''
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int RecordStatus;
typedef int ReqFileInfo;
#define NULL ((void *)0)
#define CMD_UP_UPGRADE "*DEV#upgrade#"
#define OTA_UPGRADE_BEGIN 1
#define OTA_DATA_DL 2
#define log_info(...) ((void)0)
#define y_printf(...) ((void)0)
static int g_rdx_ota_finalizing, busy, owner, held, binds, timers, starts, data_calls, replies;
RecordStatus *rdx_record_get_status(void) { return NULL; }
ReqFileInfo *rdx_protocol_get_uploadfileInfo(void) { return NULL; }
int rdx_record_format_io_enter(void) { if(busy) return 1; ++held; return 0; }
int rdx_record_format_service_status(void) { return busy; }
void rdx_ota_session_clear(void) {}
void rdx_record_format_io_leave(void) { --held; }
int rdx_record_format_download_conflict(const char *operation) { return busy; }
int rdx_ota_session_bind_current(void) { ++binds; return owner; }
int rdx_ota_session_is_current(void) { return owner; }
void rdx_ota_get_data_timer_start(void) { ++timers; }
void rdx_ota_upgrade_cmd_handler(u8 *data,u32 len) { if(held==1) ++starts; }
void rdx_ota_get_data_handler(u8 *data,u32 len) { ++data_calls; }
int rdx_ble_server_ota_send(u8 *data,u32 length) {
    if(length==15 && data[13]=='1') ++replies;
    return 0;
}
'''
    ota += function((folder / 'rdx_ota.c').read_text(encoding='utf-8'), 'rdx_ota_proc')
    ota += r'''
#define CHECK(value) do { if(!(value)) return __LINE__; } while(0)
int test_ota_conflicts(void) {
    busy=1; owner=1;
    rdx_ota_proc(OTA_UPGRADE_BEGIN,NULL,0);
    CHECK(replies==1 && !held && !binds && !starts && !timers);
    busy=0; owner=0;
    rdx_ota_proc(OTA_UPGRADE_BEGIN,NULL,0);
    CHECK(binds==1 && !held && !starts && !timers);
    owner=1;
    rdx_ota_proc(OTA_UPGRADE_BEGIN,NULL,0);
    CHECK(binds==2 && !held && starts==1 && timers==1);
    busy=1;
    rdx_ota_proc(OTA_DATA_DL,NULL,0);
    CHECK(data_calls==1 && !held);
    g_rdx_ota_finalizing=1;
    rdx_ota_proc(OTA_UPGRADE_BEGIN,NULL,0);
    CHECK(replies==1 && binds==2 && !held);
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='cdc_ota_') as work:
        path = Path(work) / 'checks.c'
        path.write_text(ota, encoding='utf-8')
        run_c_checks(path, ('test_ota_conflicts',), native=True)
    print('PASS: actual archive admission/reservations/list worker and OTA conflict cleanup')


if __name__ == '__main__':
    run_conflict_checks()
