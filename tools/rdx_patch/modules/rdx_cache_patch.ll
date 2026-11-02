; 只替换 RDX 的缓存定时调度，不改变原有同步保存、文件格式或故障锁定。
declare void @rdx_cache_flush_changed()
declare void @rdx_cache_flush_start()
declare void @rdx_cache_flush_stop()

define i32 @rdx_uxfile_cache_flush_state() {
  %dirty = load volatile i8, i8* @g_dat_cache.4, align 4
  %clean = icmp eq i8 %dirty, 0
  br i1 %clean, label %idle, label %check
check:
  %created = load i1, i1* @is_uxfile_task_created, align 4
  %quiet = load volatile i32, i32* @rdx_storage_quiet, align 4
  %refresh = load volatile i32, i32* @rdx_storage_refresh, align 4
  %error = load volatile i32, i32* @rdx_storage_io_error, align 4
  %service = call i32 @rdx_record_format_service_status()
  %open = icmp eq i32 %quiet, 0
  %refresh_ok = icmp sge i32 %refresh, 0
  %io_ok = icmp eq i32 %error, 0
  %service_ok = icmp sge i32 %service, 0
  %a = and i1 %created, %open
  %b = and i1 %refresh_ok, %io_ok
  %c = and i1 %a, %b
  %ok = and i1 %c, %service_ok
  br i1 %ok, label %ready, label %closed
ready:
  %loaded = load i1, i1* @g_dat_cache.3, align 4
  %rebuild = load i1, i1* @g_dat_need_upgrade_rebuild, align 4
  %not_rebuilding = xor i1 %rebuild, true
  %service_ready = icmp eq i32 %service, 0
  %refresh_ready = icmp eq i32 %refresh, 0
  %d = and i1 %loaded, %not_rebuilding
  %e = and i1 %service_ready, %refresh_ready
  %available = and i1 %d, %e
  %state = select i1 %available, i32 1, i32 2
  ret i32 %state
idle:
  ret i32 0
closed:
  ret i32 -1
}

define i32 @rdx_uxfile_cache_flush_once() {
  %state = call i32 @rdx_uxfile_cache_flush_state()
  %ready = icmp eq i32 %state, 1
  br i1 %ready, label %admit, label %unchanged
admit:
  %admission = call i32 @rdx_record_format_flush_enter()
  %allowed = icmp eq i32 %admission, 0
  br i1 %allowed, label %lock, label %blocked
lock:
  ; 获取锁最多等待 1 个 OS tick，超时不保存，稍后再试；不无限阻塞 app_core。
  %locked = call i32 @os_mutex_pend(%struct.OS_SEM* @dat_opera_mutex, i32 1)
  %acquired = icmp eq i32 %locked, 0
  br i1 %acquired, label %recheck, label %busy
recheck:
  %current = call i32 @rdx_uxfile_cache_flush_state()
  %can_save = icmp eq i32 %current, 1
  br i1 %can_save, label %save, label %unlock
save:
  %result = call fastcc i32 @rdx_dat_cache_save()
  %after = call i32 @rdx_uxfile_cache_flush_state()
  %failed = icmp slt i32 %result, 0
  ; 准入暂时失败可以重试，真实 IO 失败由已有错误锁存使 after 为负。
  %retry = icmp sgt i32 %after, 0
  %defer = and i1 %failed, %retry
  %saved_state = select i1 %defer, i32 2, i32 %after
  br label %unlock
unlock:
  %final = phi i32 [ %current, %recheck ], [ %saved_state, %save ]
  %unlocked = call i32 @os_mutex_post(%struct.OS_SEM* @dat_opera_mutex)
  call void @rdx_record_format_command_leave()
  ret i32 %final
busy:
  call void @rdx_record_format_command_leave()
  ret i32 2
blocked:
  %temporary = icmp sgt i32 %admission, 0
  %blocked_state = select i1 %temporary, i32 2, i32 -1
  ret i32 %blocked_state
unchanged:
  ret i32 %state
}
