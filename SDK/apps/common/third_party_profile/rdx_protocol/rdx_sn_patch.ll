; The format layer owns SN reservation and replayable RAW/MTA removal.
declare i32 @rdx_record_format_reserve_sn(i32*)
declare i32 @rdx_record_format_discard_short(%struct.uxfile_data_t*)

; Reservation and discard share the existing DAT mutex, never the later save lock.
define internal i32 @rdx_storage_reserve_sn(i32* %sn) {
  store i32 0, i32* %sn
  %lock = call i32 @os_mutex_pend(%struct.OS_SEM* @dat_opera_mutex, i32 0)
  %locked = icmp eq i32 %lock, 0
  br i1 %locked, label %reserve, label %failed
reserve:
  %result = call i32 @rdx_record_format_reserve_sn(i32* %sn)
  %unlock = call i32 @os_mutex_post(%struct.OS_SEM* @dat_opera_mutex)
  %unlocked = icmp eq i32 %unlock, 0
  %status = select i1 %unlocked, i32 %result, i32 -1
  %checked = call i32 @rdx_storage_save_result(i32 %status)
  ret i32 %checked
failed:
  %error = call i32 @rdx_storage_save_result(i32 -1)
  ret i32 %error
}

; A failed fclose still consumes the handle. No caller may close it again.
define internal i32 @rdx_storage_discard_short(%struct.FILE* %file) {
  %closed = call i32 @rdx_storage_fclose(%struct.FILE* %file)
  %ok = icmp eq i32 %closed, 0
  br i1 %ok, label %acquire, label %failed
acquire:
  %lock = call i32 @os_mutex_pend(%struct.OS_SEM* @dat_opera_mutex, i32 0)
  %locked = icmp eq i32 %lock, 0
  br i1 %locked, label %discard, label %failed
discard:
  %result = call i32 @rdx_record_format_discard_short(%struct.uxfile_data_t* @uxOperateFileData)
  %unlock = call i32 @os_mutex_post(%struct.OS_SEM* @dat_opera_mutex)
  %unlocked = icmp eq i32 %unlock, 0
  %status = select i1 %unlocked, i32 %result, i32 -1
  %checked = call i32 @rdx_storage_save_result(i32 %status)
  ret i32 %checked
failed:
  %error = call i32 @rdx_storage_save_result(i32 -1)
  ret i32 %error
}
