# 仅在原始静态库哈希及存储补丁检查通过后应用。
function Update-RdxRecoveryIr {
    param([Parameter(Mandatory = $true)][string]$Ir)
    $Ir = Replace-ExactlyOnce $Ir '  %rdx_patch_scan_active = or i1 %rdx_patch_sync_active, %rdx_patch_rebuild_pending' `
        "  %rdx_patch_scan_base = or i1 %rdx_patch_sync_active, %rdx_patch_rebuild_pending`n  %rdx_boot_state = call i32 @rdx_record_format_service_status(), !dbg !6112`n  %rdx_boot_busy = icmp sgt i32 %rdx_boot_state, 0`n  %rdx_patch_scan_active = or i1 %rdx_patch_scan_base, %rdx_boot_busy" 'hold storage power during recovery'
    $Ir = Replace-ExactlyOnce $Ir '    i32 233, label %28' `
        "    i32 233, label %28`n    i32 306463820, label %rdx_delete_request" 'checked delete dispatch'
    $Ir = Replace-ExactlyOnce $Ir '  br label %21, !dbg !6233' `
        "  br label %21, !dbg !6233`nrdx_delete_request:`n  %delete_context_int = load i32, i32* %12, align 4`n  %delete_context = inttoptr i32 %delete_context_int to i8*`n  call void @rdx_app_file_delete_worker(i8* %delete_context), !dbg !6233`n  br label %21" 'checked delete completion'
    # 仅由 UXFILE 恢复 DAT；任务创建失败时也不得在 app_core 上扫描。
    $Ir = Replace-ExactlyOnce $Ir '  %29 = call i32 @rdx_uxfile_dat_init() #12, !dbg !6167' `
        '  %29 = add i32 0, -1' 'no app_core index recovery fallback'
    $Ir = Replace-ExactlyOnce $Ir 'define i32 @rdx_uxfile_dat_init() local_unnamed_addr #0 !dbg !4935 {' `
        'define i32 @rdx_uxfile_dat_init_impl() local_unnamed_addr #0 !dbg !4935 {' 'exclusive file worker cache handoff'
    # 初始化必须传播真实加载结果，不能把 loaded 标志当作成功返回值。
    $pattern = '(?ms)^define i32 @rdx_uxfile_dat_init_impl\([^\n]*\{.*?^\}'
    $init = [regex]::Match($Ir, $pattern).Value
    if (!$init) { throw 'Missing cache init implementation' }
    $patched = [regex]::Replace($init, '(?m)^(  %4 = tail call fastcc i32 @rdx_dat_cache_load\(\)[^\n]*)$', '$1' + "`n  %rdx_loaded_ok = icmp sge i32 %4, 0`n  %rdx_load_status = select i1 %rdx_loaded_ok, i32 0, i32 -1`n  store i32 %rdx_load_status, i32* @rdx_cache_load_result`n  store i1 %rdx_loaded_ok, i1* @g_dat_cache.3`n  store i1 %rdx_loaded_ok, i1* @dat_init_flag")
    $patched = $patched.Replace('  ret i32 0,', "  %rdx_result = load i32, i32* @rdx_cache_load_result`n  ret i32 %rdx_result,")
    if ($patched -eq $init -or !$patched.Contains('store i32 %rdx_load_status, i32* @rdx_cache_load_result')) { throw 'Cache result patch mismatch' }
    $Ir = Replace-ExactlyOnce $Ir $init $patched 'propagate cache load result'
    $Ir += @'

@rdx_cache_load_result = internal global i32 -1
@rdx_empty_file = internal global %struct.uxfile_data_t zeroinitializer
@rdx_empty_list = internal global %struct.uxfile_datfile_info_t zeroinitializer

declare i32 @rdx_record_format_boot()
declare i32 @rdx_record_format_service_status()
declare i32 @rdx_record_format_io_enter()
declare i32 @rdx_record_format_flush_enter()
declare void @rdx_record_format_command_leave()
declare void @rdx_record_format_io_leave()
declare void @rdx_record_format_service_ready(i32)
declare i32 @rdx_record_format_delete(i32, i8*)
declare void @rdx_app_file_delete_worker(i8*)

define zeroext i8 @rdx_uxfile_is_index_active() {
  %state = load volatile i32, i32* @g_sync_state, align 4
  %started = icmp uge i32 %state, 1
  %pending = icmp ule i32 %state, 3
  %active = and i1 %started, %pending
  %rebuild = load i1, i1* @g_dat_need_upgrade_rebuild, align 4
  %busy = or i1 %active, %rebuild
  %result = zext i1 %busy to i8
  ret i8 %result
}

define i32 @rdx_uxfile_download_snapshot() {
  %lock = call i32 @os_mutex_pend(%struct.OS_SEM* @dat_opera_mutex, i32 0)
  store volatile i32 0, i32* @rdx_storage_io_error, align 4
  %flush = call i32 @rdx_uxfile_flush_cache()
  %io_error = load volatile i32, i32* @rdx_storage_io_error, align 4
  %flushed = icmp eq i32 %flush, 0
  %io_ok = icmp eq i32 %io_error, 0
  %ok = and i1 %flushed, %io_ok
  %result = select i1 %ok, i32 0, i32 -2
  %unlock = call i32 @os_mutex_post(%struct.OS_SEM* @dat_opera_mutex)
  ret i32 %result
}

define i32 @rdx_uxfile_delete_submit(i8* %context) {
  %value = ptrtoint i8* %context to i32
  %result = call i32 (i8*, i32, ...) @os_taskq_post_msg(i8* getelementptr inbounds ([7 x i8], [7 x i8]* @.str.20, i32 0, i32 0), i32 2, i32 306463820, i32 %value)
  ret i32 %result
}

define i32 @rdx_uxfile_delete_checked(i32 %sn, i8* %name) {
  %lock = call i32 @os_mutex_pend(%struct.OS_SEM* @dat_opera_mutex, i32 0)
  store volatile i32 0, i32* @rdx_storage_io_error, align 4
  %flush = call i32 @rdx_uxfile_flush_cache()
  %flushed = icmp eq i32 %flush, 0
  br i1 %flushed, label %remove, label %done
remove:
  call void @rdx_uxfile_close_read_file_handle()
  %deleted = call i32 @rdx_record_format_delete(i32 %sn, i8* %name)
  ; 索引替换即使中断也必须使旧快照失效；失败时
  ; 保持文件服务关闭，由启动恢复重放已持久化的事务。
  call void @rdx_uxfile_invalidate_dat_cache()
  call void @rdx_uxfile_datFileInfo_sendBuf_free()
  store i1 false, i1* @dat_init_flag, align 4
  ; 身份拒绝也重新加载缓存，确保此前事务重放后的快照一致。
  %deleted_ok = icmp eq i32 %deleted, 0
  %rejected = icmp eq i32 %deleted, -2
  %ok = or i1 %deleted_ok, %rejected
  br i1 %ok, label %reload, label %done
reload:
  %cache_result = call i32 @rdx_uxfile_dat_init_impl()
  %loaded = load i1, i1* @g_dat_cache.3, align 4
  %io_error = load volatile i32, i32* @rdx_storage_io_error, align 4
  %io_ok = icmp eq i32 %io_error, 0
  %cache_ok = and i1 %loaded, %io_ok
  %cache_return_ok = icmp eq i32 %cache_result, 0
  %reload_ok = and i1 %cache_ok, %cache_return_ok
  %cache = select i1 %reload_ok, i32 %deleted, i32 -1
  br label %done
done:
  %result = phi i32 [ %flush, %0 ], [ %deleted, %remove ], [ %cache, %reload ]
  %unlock = call i32 @os_mutex_post(%struct.OS_SEM* @dat_opera_mutex)
  ret i32 %result
}

define i32 @rdx_uxfile_dat_init() {
entry:
  br label %wait
wait:
  %created = load i1, i1* @is_uxfile_task_created, align 4
  br i1 %created, label %prepare, label %yield
yield:
  call void @os_time_dly(i32 1)
  br label %wait
prepare:
  %result = call i32 @rdx_record_format_boot()
  %safe = icmp eq i32 %result, 0
  br i1 %safe, label %init, label %done
init:
  store i1 false, i1* @g_dat_need_upgrade_rebuild, align 4
  store volatile i32 0, i32* @g_sync_state, align 4
  %cache_result = call i32 @rdx_uxfile_dat_init_impl()
  %loaded = load i1, i1* @g_dat_cache.3, align 4
  %io_error = load volatile i32, i32* @rdx_storage_io_error, align 4
  %io_ok = icmp eq i32 %io_error, 0
  %cache_ok = and i1 %loaded, %io_ok
  %cache = select i1 %cache_ok, i32 %cache_result, i32 -1
  br label %done
done:
  %status = phi i32 [ %result, %prepare ], [ %cache, %init ]
  call void @rdx_record_format_service_ready(i32 %status)
  ret i32 %status
}
'@
    # 旧快速修复会猜测双声道或丢弃残缺音频；启动恢复现由单一
    # 执行者负责，交接后仍保留正常的缓存刷写。
    foreach ($name in @('rdx_uxfile_sync_timer_callback', 'rdx_uxfile_boot_quick_fix')) {
        $pattern = '(?ms)^define internal void @' + $name + '\(i8\* %arg\) \{.*?^\}'
        if ([regex]::Matches($Ir, $pattern).Count -ne 1) { throw "Missing recovery timer $name" }
        $Ir = [regex]::Replace($Ir, $pattern, "define internal void @$name(i8* %arg) {`n  ret void`n}")
    }
    # 保护面向文件的公开 ABI，包括闭源协议解析器绕过
    # APP 回调发起的调用；指针查询返回空结构体。
    $names = @('rdx_uxfile_get_all_size','rdx_uxfile_get_dat_cout',
        'rdx_uxfile_dat_1_gen','rdx_uxfile_dat_1_save_gen','rdx_uxfile_raw_write',
        'rdx_uxfile_raw_read','rdx_uxfile_get_file_data_by_filename',
        'rdx_uxfile_get_file_data_by_sn','rdx_uxfile_get_datFileInfo',
        'rdx_uxfile_get_read_file_name','rdx_uxfile_dat_file_update',
        'rdx_uxfile_recordFile_delete_handle','rdx_uxfile_sync_files_with_dat')
    foreach ($name in $names) {
        $pattern = '(?m)^define (?:nonnull )?(?:(zeroext) )?([^@\r\n]+) @' + $name + '\(([^\r\n]*)\)[^{\r\n]*\{'
        $m = [regex]::Match($Ir, $pattern)
        if (!$m.Success) { throw "Missing file admission ABI $name" }
        $ret = $m.Groups[2].Value.Trim()
        $returnAttrs = if ($m.Groups[1].Success) { 'zeroext ' } else { '' }
        $args = @(); $call = @(); $index = 0
        if ($m.Groups[3].Value) {
            foreach ($arg in $m.Groups[3].Value.Split(',')) {
                $type = ($arg.Trim() -split ' ')[0]
                if ($arg.Trim().StartsWith('void (i8)*')) { $type = 'void (i8)*' }
                $args += "$type %arg$index"
                $call += "$type %arg$index"
                $index++
            }
        }
        $Ir = Replace-ExactlyOnce $Ir $m.Value ($m.Value.Replace("@$name(", "@${name}_ready(")) "file gate $name"
        $fallback = if ($ret.EndsWith('*')) {
            if ($name -eq 'rdx_uxfile_get_datFileInfo') { '@rdx_empty_list' } else { '@rdx_empty_file' }
        } elseif ($name -match 'get_all_size|get_dat_cout') { '0' } else { '-1' }
        $Ir += "`ndefine $returnAttrs$ret @$name($($args -join ', ')) {`n  %state = call i32 @rdx_record_format_io_enter()`n  %ready = icmp eq i32 %state, 0`n  br i1 %ready, label %run, label %busy`nrun:`n  %result = call $returnAttrs$ret @${name}_ready($($call -join ', '))`n  call void @rdx_record_format_io_leave()`n  ret $ret %result`nbusy:`n  ret $ret $fallback`n}`n"
    }
    # Explicit erase is a recovery operation, not a healthy-index operation.
    # Never flush dirty pre-erase DAT back onto the empty volume, or report
    # success before the new index/cache loads.
    $pattern = '(?ms)^define i32 @rdx_uxfile_sd_format\([^\n]*\{.*?^\}'
    $original = [regex]::Match($Ir, $pattern).Value
    if (!$original) { throw 'Missing explicit formatter' }
    $format = $original.Replace('@rdx_uxfile_sd_format(', '@rdx_uxfile_sd_format_ready(')
    # A failed/unmounted volume has no partition object to dereference.
    $format = [regex]::Replace($format, '(?m)^  (%1[01]) = [^\n]*$', '  $1 = add i32 0, 0')
    $format = [regex]::Replace($format, '(?m)^  %12 = load i32, i32\* %11,[^\n]*$', '  %12 = call i32 @rdx_record_format_capacity()')
    $format = [regex]::Replace($format, '(?m)^  call void @rdx_protocol_sd_format_result_indicate\(i8 zeroext 0,[^\n]*\n', '')
    $format = [regex]::Replace($format, '(?m)^  call void %0\(i8 zeroext (?:1|16)\)[^\n]*$', '  ; Completion callback is owned by the recovery wrapper.')
    $format = [regex]::Replace($format, '(?m)^(  call void @rdx_uxfile_invalidate_dat_cache\(\)[^\n]*)$', @'
  store i8 0, i8* @g_dat_cache.4, align 4
  store volatile i32 0, i32* @rdx_storage_io_error, align 4
  store volatile i32 0, i32* @rdx_storage_refresh, align 4
  call void @rdx_record_format_erased()
$1
'@)
    $format = [regex]::Replace($format, '(?m)^(  %31 = call i32 @rdx_uxfile_dat_init\(\)[^\n]*)$', @'
$1
  %rdx_marker_ok = icmp eq i32 %30, 1
  %rdx_index_ok = icmp eq i32 %31, 0
  %rdx_format_ok = and i1 %rdx_marker_ok, %rdx_index_ok
  %rdx_format_status = select i1 %rdx_format_ok, i32 0, i32 -1
  %rdx_format_reply = select i1 %rdx_format_ok, i8 0, i8 1
  call void @rdx_protocol_sd_format_result_indicate(i8 zeroext %rdx_format_reply, i32 %29, i32 %12)
'@)
    $format = Replace-ExactlyOnce $format `
        '[ 0, %42 ], [ 0, %41 ]' `
        '[ %rdx_format_status, %42 ], [ %rdx_format_status, %41 ]' 'format result after index reload'
    if (!$format.Contains('call void @rdx_record_format_erased()') -or
        !$format.Contains('%rdx_format_ok = and')) { throw 'Format recovery patch mismatch' }
    # LTO requires call-site locations when adding inlinable calls to an
    # existing debug-info function, even though standalone bitcode accepts it.
    $formatLocation = [regex]::Match($original, 'call i32 @f_format[^\n]*(!dbg !\d+)').Groups[1].Value
    if (!$formatLocation) { throw 'Missing format call-site debug location' }
    $format = [regex]::Replace($format, '(?m)^(  (?:%\w+ = )?call (?:void|i32) @[^\n]+)$', {
        param($match)
        if ($match.Value.Contains('!dbg')) { return $match.Value }
        return $match.Value + ', ' + $formatLocation
    })
    $Ir = Replace-ExactlyOnce $Ir $original $format 'recover failed storage on explicit erase'
    $Ir += @'

declare i32 @rdx_record_format_erase_enter()
declare i32 @rdx_record_format_capacity()
declare void @rdx_record_format_erased()
declare void @rdx_record_format_erase_leave(i32)
define i32 @rdx_uxfile_sd_format(void (i8)* %callback) {
entry:
  %admit = call i32 @rdx_record_format_erase_enter()
  %allowed = icmp eq i32 %admit, 0
  br i1 %allowed, label %run, label %done
run:
  %lock = call i32 @os_mutex_pend(%struct.OS_SEM* @dat_opera_mutex, i32 0)
  call void @rdx_uxfile_close_read_file_handle()
  call void @rdx_uxfile_datFileInfo_sendBuf_free()
  %result = call i32 @rdx_uxfile_sd_format_ready(void (i8)* null)
  %unlock = call i32 @os_mutex_post(%struct.OS_SEM* @dat_opera_mutex)
  call void @rdx_record_format_erase_leave(i32 %result)
  br label %done
done:
  %status = phi i32 [ -1, %entry ], [ %result, %run ]
  %has_callback = icmp ne void (i8)* %callback, null
  br i1 %has_callback, label %notify, label %exit
notify:
  %success = icmp eq i32 %status, 0
  %code = select i1 %success, i8 1, i8 16
  call void %callback(i8 zeroext %code)
  br label %exit
exit:
  ret i32 %status
}
'@
    # 先遵守静默/关闭门禁；临时恢复忙时沿用现有 defer 续调。
    $pattern = '(?ms)^define internal void @rdx_uxfile_flush_cache_timer_cb\(i8\* %arg\) \{.*?^\}'
    $timer = [regex]::Match($Ir, $pattern).Value
    $patched = $timer.Replace("run:`n  call void", "run:`n  %boot = call i32 @rdx_record_format_service_status()`n  %boot_ready = icmp eq i32 %boot, 0`n  br i1 %boot_ready, label %flush, label %boot_busy`nboot_busy:`n  %retry = icmp sgt i32 %boot, 0`n  br i1 %retry, label %defer, label %done`nflush:`n  call void")
    if (!$timer -or $patched -eq $timer) { throw 'Missing flush retry gate' }
    $patched = $patched.Replace("flush:`n  call void", "flush:`n  %admit = call i32 @rdx_record_format_flush_enter()`n  %admitted = icmp eq i32 %admit, 0`n  br i1 %admitted, label %flush_run, label %flush_busy`nflush_busy:`n  %admit_retry = icmp sgt i32 %admit, 0`n  br i1 %admit_retry, label %defer, label %done`nflush_run:`n  call void")
    $patched = [regex]::Replace($patched, '(?m)^(  call void @rdx_uxfile_flush_cache_timer_cb_impl[^\n]*)$', '$1' + "`n  call void @rdx_record_format_command_leave()")
    $Ir = Replace-ExactlyOnce $Ir $timer $patched 'flush after cache handoff'
    $pattern = '(?m)^define [^\n]*@rdx_uxfile_get_datFileInfo_ready\([^\n]+\{'
    $m = [regex]::Match($Ir, $pattern)
    if (!$m.Success) { throw 'Missing complete management list adapter' }
    $Ir = Replace-ExactlyOnce $Ir $m.Value ($m.Value.Replace('@rdx_uxfile_get_datFileInfo_ready(', '@rdx_uxfile_get_datFileInfo_legacy(')) 'full index listing'
    $Ir += @'

declare %struct.uxfile_datfile_info_t* @rdx_record_format_list(%struct.uxfile_datfile_info_t*)
define %struct.uxfile_datfile_info_t* @rdx_uxfile_get_datFileInfo_ready() {
  %lock = call i32 @os_mutex_pend(%struct.OS_SEM* @dat_opera_mutex, i32 0)
  %flush = call i32 @rdx_uxfile_flush_cache()
  %ok = icmp eq i32 %flush, 0
  br i1 %ok, label %list, label %done
list:
  %info = call %struct.uxfile_datfile_info_t* @rdx_record_format_list(%struct.uxfile_datfile_info_t* @uxDatFileInfo)
  br label %done
done:
  %unlock = call i32 @os_mutex_post(%struct.OS_SEM* @dat_opera_mutex)
  ret %struct.uxfile_datfile_info_t* @uxDatFileInfo
}

; Management entries are not tracks, even when their library-valid flag is set.
define internal i1 @rdx_neighbor_track(%struct.dat_entry_t* %track) {
entry:
  %validp = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %track, i32 0, i32 8
  %validbyte = load i8, i8* %validp
  %valid = icmp ne i8 %validbyte, 0
  %framep = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %track, i32 0, i32 7
  %frame = load i32, i32* %framep
  %legacy = icmp eq i32 %frame, 0
  %mono = icmp eq i32 %frame, 40
  %stereo = icmp eq i32 %frame, 80
  %fixed = or i1 %mono, %stereo
  %supported = or i1 %legacy, %fixed
  %usable = and i1 %valid, %supported
  br i1 %usable, label %scan, label %bad
scan:
  %i = phi i32 [0, %entry], [%next, %advance]
  %namep = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %track, i32 0, i32 1, i32 %i
  %character = load i8, i8* %namep
  %end = icmp eq i8 %character, 0
  br i1 %end, label %suffix, label %advance
advance:
  %next = add i32 %i, 1
  %bounded = icmp ult i32 %next, 64
  br i1 %bounded, label %scan, label %bad
suffix:
  %long_enough = icmp uge i32 %i, 5
  br i1 %long_enough, label %inspect, label %bad
inspect:
  %dot_index = add i32 %i, -4
  %r_index = add i32 %i, -3
  %a_index = add i32 %i, -2
  %w_index = add i32 %i, -1
  %dotp = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %track, i32 0, i32 1, i32 %dot_index
  %rp = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %track, i32 0, i32 1, i32 %r_index
  %ap = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %track, i32 0, i32 1, i32 %a_index
  %wp = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %track, i32 0, i32 1, i32 %w_index
  %dot = load i8, i8* %dotp
  %r = load i8, i8* %rp
  %a = load i8, i8* %ap
  %w = load i8, i8* %wp
  %lower_r = or i8 %r, 32
  %lower_a = or i8 %a, 32
  %lower_w = or i8 %w, 32
  %has_dot = icmp eq i8 %dot, 46
  %has_r = icmp eq i8 %lower_r, 114
  %has_a = icmp eq i8 %lower_a, 97
  %has_w = icmp eq i8 %lower_w, 119
  %dr = and i1 %has_dot, %has_r
  %aw = and i1 %has_a, %has_w
  %raw = and i1 %dr, %aw
  ret i1 %raw
bad:
  ret i1 false
}

; DAT is newest-first. SN is an opaque identity, not an ordering key.
define i32 @rdx_uxfile_neighbor_sn(i32 %anchor, i32 %direction) {
entry:
  %lock = call i32 @os_mutex_pend(%struct.OS_SEM* @dat_opera_mutex, i32 0)
  %service = call i32 @rdx_record_format_service_status()
  %ready = icmp eq i32 %service, 0
  br i1 %ready, label %ensure, label %done
ensure:
  ; 录音保存会使缓存失效，播放查找前按需重新加载。
  %was_loaded = load i1, i1* @g_dat_cache.3
  br i1 %was_loaded, label %inspect, label %reload
reload:
  %load_result = call fastcc i32 @rdx_dat_cache_load()
  %load_ok = icmp sge i32 %load_result, 0
  store i1 %load_ok, i1* @g_dat_cache.3
  br i1 %load_ok, label %inspect, label %done
inspect:
  %entries = load %struct.dat_entry_t*, %struct.dat_entry_t** @g_dat_cache.0
  %present = icmp ne %struct.dat_entry_t* %entries, null
  %loaded = load i1, i1* @g_dat_cache.3
  %usable = and i1 %ready, %present
  %ok = and i1 %usable, %loaded
  br i1 %ok, label %prepare, label %done
prepare:
  %count16 = load i16, i16* @g_dat_cache.1
  %count = zext i16 %count16 to i32
  %newer = icmp sgt i32 %direction, 0
  br label %loop
loop:
  %i = phi i32 [0, %prepare], [%next, %body]
  %seen_anchor = phi i1 [false, %prepare], [%seen_next, %body]
  %first = phi i32 [0, %prepare], [%first_next, %body]
  %last = phi i32 [0, %prepare], [%last_next, %body]
  %before = phi i32 [0, %prepare], [%before_next, %body]
  %after = phi i32 [0, %prepare], [%after_next, %body]
  %end = icmp eq i32 %i, %count
  br i1 %end, label %found, label %body
body:
  %idp = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %entries, i32 %i, i32 0
  %id = load i32, i32* %idp
  %item = getelementptr %struct.dat_entry_t, %struct.dat_entry_t* %entries, i32 %i
  %valid = call i1 @rdx_neighbor_track(%struct.dat_entry_t* %item)
  %nonzero = icmp ne i32 %id, 0
  %active = and i1 %valid, %nonzero
  %first_empty = icmp eq i32 %first, 0
  %pick_first = and i1 %active, %first_empty
  %first_next = select i1 %pick_first, i32 %id, i32 %first
  %last_next = select i1 %active, i32 %id, i32 %last
  %matches = icmp eq i32 %id, %anchor
  %is_anchor = and i1 %matches, %nonzero
  %not_anchor = xor i1 %is_anchor, true
  %neighbor = and i1 %active, %not_anchor
  %not_seen = xor i1 %seen_anchor, true
  %pick_before = and i1 %neighbor, %not_seen
  %before_next = select i1 %pick_before, i32 %id, i32 %before
  %after_empty = icmp eq i32 %after, 0
  %eligible_after = and i1 %neighbor, %seen_anchor
  %pick_after = and i1 %eligible_after, %after_empty
  %after_next = select i1 %pick_after, i32 %id, i32 %after
  %seen_next = or i1 %seen_anchor, %is_anchor
  %next = add i32 %i, 1
  br label %loop
found:
  %best = select i1 %newer, i32 %before, i32 %after
  %wrap = select i1 %newer, i32 %last, i32 %first
  %has_best = icmp ne i32 %best, 0
  %selected = select i1 %has_best, i32 %best, i32 %wrap
  br label %done
done:
  %result = phi i32 [0, %entry], [0, %reload], [0, %inspect], [%selected, %found]
  %unlock = call i32 @os_mutex_post(%struct.OS_SEM* @dat_opera_mutex)
  ret i32 %result
}

'@
    return $Ir
}
