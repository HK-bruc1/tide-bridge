"""Execute the actual storage-patch LLVM helpers with fault-injected JL I/O.

Requires Python + llvmlite. This focused patch validation is separate from the
five PowerShell source contracts; it does not emulate the firmware or SD driver.
"""
import ctypes
from pathlib import Path

from llvmlite import binding as llvm

ROOT = Path(__file__).resolve().parents[2]
HELPERS = ROOT / "SDK/apps/common/third_party_profile/rdx_protocol/rdx_storage_patch.ll"
STUBS = r'''
%struct.FILE = type { i8 }
%struct.uxfile_data_t = type { i32, [64 x i8] }
@uxOperateFileData = global %struct.uxfile_data_t zeroinitializer
@is_uxfile_task_created = global i1 true, align 4
@delete_queue_head = global i8 0
@delete_queue_tail = global i8 0
@.str.20 = constant [7 x i8] c"uxfile\00"
@.str.34 = constant [15 x i8] zeroinitializer
@test_write = global i32 12
@test_close = global i32 0
@test_delete = global i32 0
@test_flush = global i32 0
@test_cache = global i32 0
@test_post = global i32 0
@test_scan = global i8 0
@test_drain = global i32 0
@test_sync = global i32 0
define i32 @rdx_uxfile_dat_1_save_gen() { ret i32 0 }
define i32 @rdx_uxfile_sync_files_with_dat() {
 %r = load i32, i32* @test_sync
 ret i32 %r
}
define i32 @fwrite(i8* %b, i32 %s, i32 %c, %struct.FILE* %f) {
 %r = load i32, i32* @test_write
 ret i32 %r
}
define i32 @fclose(%struct.FILE* %f) {
 %r = load i32, i32* @test_close
 ret i32 %r
}
define i32 @fdelete(%struct.FILE* %f) {
 %r = load i32, i32* @test_delete
 ret i32 %r
}
define i32 @f_flush_wbuf(i8* %p) {
 %r = load i32, i32* @test_flush
 ret i32 %r
}
define i32 @os_taskq_post_msg(i8* %n, i32 %c, ...) {
 %r = load i32, i32* @test_post
 ret i32 %r
}
define internal fastcc void @rdx_uxfile_process_delete_queue() {
 %v = load i32, i32* @test_drain
 %next = add i32 %v, 1
 store i32 %next, i32* @test_drain
 %tail = load i8, i8* @delete_queue_tail
 store i8 %tail, i8* @delete_queue_head
 ret void
}
define void @rdx_uxfile_close_read_file_handle() { ret void }
define i32 @rdx_uxfile_flush_cache() {
 %r = load i32, i32* @test_cache
 ret i32 %r
}
define zeroext i8 @rdx_uxfile_is_scan_active() {
 %r = load i8, i8* @test_scan
 ret i8 %r
}
'''


def main():
    llvm.initialize_native_target()
    llvm.initialize_native_asmprinter()
    target = llvm.Target.from_default_triple().create_target_machine()
    module = llvm.parse_assembly(STUBS + HELPERS.read_text(encoding="utf-8"))
    module.triple = llvm.get_default_triple()
    module.data_layout = str(target.target_data)
    module.verify()
    engine = llvm.create_mcjit_compiler(module, target)
    engine.finalize_object()

    def fn(name, result=ctypes.c_int, args=(ctypes.c_int,)):
        address = engine.get_function_address(name)
        assert address, name
        return ctypes.CFUNCTYPE(result, *args)(address)

    def setv(name, value, typ=ctypes.c_int):
        address = engine.get_global_value_address(name)
        assert address, name
        typ.from_address(address).value = value

    def getv(name):
        return ctypes.c_int.from_address(engine.get_global_value_address(name)).value

    checked = fn("rdx_storage_checked_result")
    saved = fn("rdx_storage_save_result")
    write = fn("rdx_storage_fwrite", args=(ctypes.c_void_p, ctypes.c_int,
                                          ctypes.c_int, ctypes.c_void_p))
    request = fn("rdx_uxfile_fence_request")
    poll = fn("rdx_uxfile_fence_poll")
    arrive = fn("rdx_storage_fence_arrive", result=None)

    # The real wrapper must use size*count bytes, not count items.
    assert write(None, 4, 3, None) == 12 and checked(0) == 0
    for written in (11, 3, 0, -1):
        setv("rdx_storage_io_error", 0)
        setv("test_write", written)
        write(None, 4, 3, None)
        assert checked(0) == -1, written
        setv("test_write", 12)
        write(None, 4, 3, None)
        assert checked(0) == -1, "later success must not erase an error"
    for api, slot in (("fclose", "close"), ("fdelete", "delete"),
                      ("f_flush_wbuf", "flush")):
        call = fn("rdx_storage_" + api, args=(ctypes.c_void_p,))
        for failure in (-1, 1):
            setv("rdx_storage_io_error", 0)
            setv("test_" + slot, failure)
            assert call(None) == failure and checked(0) == -1
        setv("test_" + slot, 0)
    setv("rdx_storage_io_error", 0)
    assert checked(1) == 1 and saved(-1) == -1 and checked(0) == -1
    setv("rdx_storage_io_error", 0)

    assert request(0) == -1 and poll(0) == -2
    setv("is_uxfile_task_created", 0, ctypes.c_uint8)
    assert request(1) == -1
    setv("is_uxfile_task_created", 1, ctypes.c_uint8)
    setv("test_post", 1)
    assert request(1) == -1 and poll(1) == -2
    setv("test_post", 0)
    assert request(2) == 0 and request(3) == -1
    assert poll(2) == -2
    arrive(3)
    assert poll(2) == -2 and getv("test_drain") == 0
    setv("delete_queue_tail", 4, ctypes.c_uint8)
    arrive(2)
    assert poll(2) == 0 and poll(3) == -2 and getv("test_drain") == 1
    assert request(3) == -1, "completed fence must not reopen a frozen worker"
    assert getv("rdx_storage_quiet") == 2
    for ticket, slot, value, typ in ((4, "test_cache", -1, ctypes.c_int),
                                     (5, "test_scan", 1, ctypes.c_uint8),
                                     (6, "test_flush", -1, ctypes.c_int)):
        setv("rdx_storage_fence_done", 0)  # Independent boot, keep I/O fault below.
        setv("rdx_storage_quiet", 0)
        setv(slot, value, typ)
        assert request(ticket) == 0
        arrive(ticket)
        assert poll(ticket) == -1
        setv(slot, 0, typ)
    setv("rdx_storage_fence_done", 0)
    assert request(7) == 0
    arrive(7)
    assert poll(7) == -1, "earlier I/O failure must block subsequent fences"
    setv("rdx_storage_io_error", 0)
    returned = fn("rdx_uxfile_pc_returned", result=None, args=())
    refresh = fn("rdx_uxfile_pc_refresh_status", args=())
    reconcile = fn("rdx_storage_boot_reconcile", result=None)
    for initial, sync_result, expected in ((0, 0, 0), (-1, 0, -1),
                                            (0, -1, -1), (0, 1, -1)):
        returned()
        assert refresh() == 1
        setv("test_sync", sync_result)
        reconcile(initial)
        assert refresh() == expected
    print("PASS: actual LLVM helpers: short write, close/delete/flush errors, "
          "sticky failure, enqueue rejection, correlated FIFO completion and scan/cache failures")


if __name__ == "__main__":
    main()
