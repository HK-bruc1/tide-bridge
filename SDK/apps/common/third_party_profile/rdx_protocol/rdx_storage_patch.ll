; Appended to the hash-pinned UXFILE IR by patch_librdxApp_storage.ps1.
; No new library dependencies. The existing JL FILE and OS ABI types are used.
; Error is sticky until reboot: a FIFO fence can never erase an earlier failure.
@rdx_storage_io_error = internal global i32 0, align 4
@rdx_storage_fence_pending = internal global i32 0, align 4
@rdx_storage_fence_done = internal global i32 0, align 4
@rdx_storage_fence_result = internal global i32 -2, align 4
@rdx_storage_quiet = internal global i32 0, align 4
@rdx_storage_refresh = internal global i32 0, align 4

define i32 @rdx_uxfile_storage_status() {
  %result = call i32 @rdx_storage_checked_result(i32 0)
  ret i32 %result
}

define i32 @rdx_uxfile_finish_record() {
  %name = load i8, i8* getelementptr inbounds (%struct.uxfile_data_t, %struct.uxfile_data_t* @uxOperateFileData, i32 0, i32 1, i32 0), align 1
  %empty = icmp eq i8 %name, 0
  br i1 %empty, label %nothing, label %save
save:
  %result = call i32 @rdx_uxfile_dat_1_save_gen()
  %checked = call i32 @rdx_storage_save_result(i32 %result)
  ret i32 %checked
nothing:
  %status = call i32 @rdx_uxfile_storage_status()
  ret i32 %status
}

; Called after MSC STOP/remount, before the first UXFILE task starts.
define void @rdx_uxfile_pc_returned() {
  store volatile i32 1, i32* @rdx_storage_refresh, align 4
  ret void
}

; 0 ready, 1 awaiting boot reconciliation, -1 failed.
define i32 @rdx_uxfile_pc_refresh_status() {
  %state = load volatile i32, i32* @rdx_storage_refresh, align 4
  ret i32 %state
}

; Queued after the creator finishes flags/mutex setup, never racing task start.
define internal void @rdx_storage_boot_refresh_post() {
  %state = load volatile i32, i32* @rdx_storage_refresh, align 4
  %needed = icmp eq i32 %state, 1
  br i1 %needed, label %post, label %done
post:
  %ret = call i32 (i8*, i32, ...) @os_taskq_post_msg(i8* getelementptr inbounds ([7 x i8], [7 x i8]* @.str.20, i32 0, i32 0), i32 1, i32 1380210770)
  %ok = icmp eq i32 %ret, 0
  br i1 %ok, label %done, label %fail
fail:
  store volatile i32 -1, i32* @rdx_storage_refresh, align 4
  br label %done
done:
  ret void
}

define internal void @rdx_storage_boot_reconcile(i32 %init_result) {
  %required = load volatile i32, i32* @rdx_storage_refresh, align 4
  %need = icmp eq i32 %required, 1
  br i1 %need, label %sync, label %done
sync:
  %ret = call i32 @rdx_uxfile_sync_files_with_dat()
  %checked = call i32 @rdx_storage_checked_result(i32 %ret)
  %ok = icmp eq i32 %checked, 0
  %init_ok = icmp sge i32 %init_result, 0
  %all_ok = and i1 %ok, %init_ok
  %state = select i1 %all_ok, i32 0, i32 -1
  store volatile i32 %state, i32* @rdx_storage_refresh, align 4
  br label %done
done:
  ret void
}

define internal i32 @rdx_storage_checked_result(i32 %result) {
  %error = load volatile i32, i32* @rdx_storage_io_error, align 4
  %failed = icmp ne i32 %error, 0
  %checked = select i1 %failed, i32 -1, i32 %result
  ret i32 %checked
}

define internal i32 @rdx_storage_save_result(i32 %result) {
  %failed = icmp slt i32 %result, 0
  br i1 %failed, label %fail, label %done
fail:
  store volatile i32 -1, i32* @rdx_storage_io_error, align 4
  br label %done
done:
  %checked = call i32 @rdx_storage_checked_result(i32 %result)
  ret i32 %checked
}

define internal i32 @rdx_storage_fwrite(i8* %buffer, i32 %size, i32 %count, %struct.FILE* %file) {
  %written = call i32 @fwrite(i8* %buffer, i32 %size, i32 %count, %struct.FILE* %file)
  %expected = mul i32 %size, %count
  %ok = icmp eq i32 %written, %expected
  br i1 %ok, label %done, label %fail
fail:
  store volatile i32 -1, i32* @rdx_storage_io_error, align 4
  br label %done
done:
  ret i32 %written
}

define internal i32 @rdx_storage_fclose(%struct.FILE* %file) {
  %result = call i32 @fclose(%struct.FILE* %file)
  %ok = icmp eq i32 %result, 0
  br i1 %ok, label %done, label %fail
fail:
  store volatile i32 -1, i32* @rdx_storage_io_error, align 4
  br label %done
done:
  ret i32 %result
}

define internal i32 @rdx_storage_fdelete(%struct.FILE* %file) {
  %result = call i32 @fdelete(%struct.FILE* %file)
  %ok = icmp eq i32 %result, 0
  br i1 %ok, label %done, label %fail
fail:
  store volatile i32 -1, i32* @rdx_storage_io_error, align 4
  br label %done
done:
  ret i32 %result
}

define internal i32 @rdx_storage_f_flush_wbuf(i8* %path) {
  %result = call i32 @f_flush_wbuf(i8* %path)
  %ok = icmp eq i32 %result, 0
  br i1 %ok, label %done, label %fail
fail:
  store volatile i32 -1, i32* @rdx_storage_io_error, align 4
  br label %done
done:
  ret i32 %result
}

; app_core is the sole request owner. Call only after admission is closed and
; other producers have stopped. At most one outstanding request; no ticket reuse.
define i32 @rdx_uxfile_fence_request(i32 %ticket) {
  %created = load i1, i1* @is_uxfile_task_created, align 4
  %valid = icmp ne i32 %ticket, 0
  %pending = load volatile i32, i32* @rdx_storage_fence_pending, align 4
  %idle = icmp eq i32 %pending, 0
  %done = load volatile i32, i32* @rdx_storage_fence_done, align 4
  %unused = icmp eq i32 %done, 0
  %a = and i1 %created, %valid
  %c = and i1 %a, %idle
  %b = and i1 %c, %unused
  br i1 %b, label %post, label %reject
post:
  store volatile i32 1, i32* @rdx_storage_quiet, align 4
  store volatile i32 0, i32* @rdx_storage_fence_done, align 4
  store volatile i32 -2, i32* @rdx_storage_fence_result, align 4
  store volatile i32 %ticket, i32* @rdx_storage_fence_pending, align 4
  %ret = call i32 (i8*, i32, ...) @os_taskq_post_msg(i8* getelementptr inbounds ([7 x i8], [7 x i8]* @.str.20, i32 0, i32 0), i32 2, i32 1380210758, i32 %ticket)
  %ok = icmp eq i32 %ret, 0
  br i1 %ok, label %accepted, label %enqueue_failed
enqueue_failed:
  store volatile i32 0, i32* @rdx_storage_fence_pending, align 4
  ret i32 -1
accepted:
  ret i32 0
reject:
  ret i32 -1
}

; 0 = completed successfully, -1 = failed, -2 = not completed for this ticket.
; Library timers stop at request; worker stops dispatching after success.
; This does NOT stop external producers: the coordinator must stop those first.
; No direct timeout-to-success path. Quiet state is only cleared by reboot.
define i32 @rdx_uxfile_fence_poll(i32 %ticket) {
  %done = load volatile i32, i32* @rdx_storage_fence_done, align 4
  %match = icmp eq i32 %ticket, %done
  %valid = icmp ne i32 %ticket, 0
  %ok = and i1 %match, %valid
  br i1 %ok, label %complete, label %waiting
complete:
  %result = load volatile i32, i32* @rdx_storage_fence_result, align 4
  %checked = call i32 @rdx_storage_checked_result(i32 %result)
  ret i32 %checked
waiting:
  ret i32 -2
}

define internal void @rdx_storage_fence_arrive(i32 %ticket) {
  %pending = load volatile i32, i32* @rdx_storage_fence_pending, align 4
  %match = icmp eq i32 %ticket, %pending
  %valid = icmp ne i32 %ticket, 0
  %ok = and i1 %match, %valid
  br i1 %ok, label %drain, label %ignored
drain:
  call fastcc void @rdx_uxfile_process_delete_queue()
  call void @rdx_uxfile_close_read_file_handle()
  %flush = call i32 @rdx_uxfile_flush_cache()
  %device_flush = call i32 @rdx_storage_f_flush_wbuf(i8* getelementptr inbounds ([15 x i8], [15 x i8]* @.str.34, i32 0, i32 0))
  %head = load volatile i8, i8* @delete_queue_head, align 1
  %tail = load volatile i8, i8* @delete_queue_tail, align 1
  %empty = icmp eq i8 %head, %tail
  %syn = call zeroext i8 @rdx_uxfile_is_scan_active()
  %sync_idle = icmp eq i8 %syn, 0
  %flush_ok = icmp eq i32 %flush, 0
  %a = and i1 %empty, %sync_idle
  %b = and i1 %a, %flush_ok
  %result = select i1 %b, i32 0, i32 -1
  %checked = call i32 @rdx_storage_checked_result(i32 %result)
  %success = icmp eq i32 %checked, 0
  %quiet = select i1 %success, i32 2, i32 1
  store volatile i32 %quiet, i32* @rdx_storage_quiet, align 4
  store volatile i32 %checked, i32* @rdx_storage_fence_result, align 4
  store volatile i32 0, i32* @rdx_storage_fence_pending, align 4
  store volatile i32 %ticket, i32* @rdx_storage_fence_done, align 4
  br label %ignored
ignored:
  ret void
}
