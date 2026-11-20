# Applied only to the hash-pinned archive, after recovery and before ADV/cache.
function Get-RdxDownloadFunction {
    param([string]$Ir, [string]$Name)
    $matches = [regex]::Matches($Ir, '(?ms)^define [^\n]*@' + [regex]::Escape($Name) + '\([^\n]*\{.*?^\}')
    if ($matches.Count -ne 1) { throw "Missing unique download ABI: $Name" }
    return $matches[0].Value
}

function Clear-RdxDownloadMetadata {
    param([string]$Text)
    $Text = [regex]::Replace($Text, '(?m)^.*@llvm\.(dbg|lifetime)\.[^\n]*\n', '')
    return [regex]::Replace($Text, ', ![\w.]+ !\d+| !dbg !\d+| #\d+', '')
}

function Update-RdxDownloadUxIr {
    param([string]$Ir)
    $old = Get-RdxDownloadFunction $Ir 'rdx_uxfile_recordFileData_send_finish'
    $new = @'
define void @rdx_uxfile_recordFileData_send_finish(%struct.ReqFileInfo* %request) {
  call void @rdx_file_download_abort()
  call void @rdx_uxfile_close_read_file_handle()
  call void @rdx_uxfile_recordFileData_sendBuf_free()
  %bytes = bitcast %struct.ReqFileInfo* %request to i8*
  call void @llvm.memset.p0i8.i32(i8* %bytes, i8 0, i32 52, i32 4, i1 false)
  ret void
}
'@
    $Ir = Replace-ExactlyOnce $Ir $old $new 'generic finish must never delete'
    $Ir = Replace-ExactlyOnce $Ir '    i32 306463820, label %rdx_delete_request' `
        "    i32 306463820, label %rdx_delete_request`n    i32 306463821, label %rdx_auto_delete_request" 'automatic delete dispatch'
    $anchor = '  call void @rdx_app_file_delete_worker(i8* %delete_context), !dbg !6233'
    $Ir = Replace-ExactlyOnce $Ir $anchor ($anchor + @'

  br label %21
rdx_auto_delete_request:
  %auto_context_int = load i32, i32* %12, align 4
  %auto_context = inttoptr i32 %auto_context_int to i8*
  call void @rdx_file_download_delete_worker(i8* %auto_context), !dbg !6233
'@) 'automatic delete worker'

    # One executor body, with an extra authorization check immediately before
    # durable intent. Manual-delete ABI and behavior remain unchanged.
    $checked = Get-RdxDownloadFunction $Ir 'rdx_uxfile_delete_checked'
    $guarded = Replace-ExactlyOnce $checked `
        '@rdx_uxfile_delete_checked(i32 %sn, i8* %name)' `
        '@rdx_uxfile_delete_checked_guarded(i32 %sn, i8* %name, i32 (i8*)* %authorize, i8* %context)' 'guarded delete ABI'
    $guarded = Replace-ExactlyOnce $guarded `
        '@rdx_record_format_delete(i32 %sn, i8* %name)' `
        '@rdx_record_format_delete_guarded(i32 %sn, i8* %name, i32 (i8*)* %authorize, i8* %context)' 'durable intent authorization'
    $Ir += "`n" + $guarded + @'

declare void @rdx_file_download_abort()
declare void @rdx_file_download_delete_worker(i8*)
declare i32 @rdx_record_format_delete_guarded(i32, i8*, i32 (i8*)*, i8*)

define i32 @rdx_uxfile_auto_delete_submit(i8* %context) {
  %value = ptrtoint i8* %context to i32
  %result = call i32 (i8*, i32, ...) @os_taskq_post_msg(i8* getelementptr inbounds ([7 x i8], [7 x i8]* @.str.20, i32 0, i32 0), i32 2, i32 306463821, i32 %value)
  ret i32 %result
}
'@
    return $Ir
}

function Update-RdxDownloadProtocolIr {
    param([string]$Ir)
    $old = Get-RdxDownloadFunction $Ir 'rdx_protocol_handle_recordFileData_req'
    $new = Clear-RdxDownloadMetadata $old
    # A recording run or OTA rejects every chunk. That is a device-side
    # interrupt, so record it for the wrapper to answer with one stop
    # indication; silence here made the APP retry until it reported failure.
    foreach ($tag in @('([60 x i8], [60 x i8]* @.str.24', '([65 x i8], [65 x i8]* @.str.25')) {
        $reason = if ($tag -like '*str.24*') { 'OTA' } else { 'recording' }
        $block = "  tail call void (i32, i8*, i8*, ...) @log_print(i32 2, i8* null, i8* getelementptr inbounds " +
                 "$tag, i32 0, i32 0), i8* getelementptr inbounds ([39 x i8], [39 x i8]* @__func__.rdx_protocol_handle_recordFileData_req, i32 0, i32 0))`n" +
                 "  tail call void @rdx_uxfile_recordFileData_sendBuf_free()`n" +
                 "  tail call void @rdx_protocol_file_sync_busy_timer_stop()`n" +
                 "  tail call void @rdx_uxfile_recordFileData_send_finish(%struct.ReqFileInfo* nonnull @r_file_info)"
        $new = Replace-ExactlyOnce $new $block ($block + "`n  call void @rdx_file_download_interrupt_notify()") "$reason conflict is an interrupt"
    }
    $new = Replace-ExactlyOnce $new '@rdx_protocol_handle_recordFileData_req(' `
        '@rdx_protocol_handle_recordFileData_req_legacy(' 'guard before shared request mutation'
    # No delayed callback may borrow mutable r_file_info. The receive worker
    # already performs the same read/send synchronously for non-duplicates.
    $post = [regex]::Match($new, '(?m)^  %128 = call i32 @os_taskq_post_type[^\n]*$').Value
    if (!$post) { throw 'Missing duplicate-download callback producer' }
    $new = Replace-ExactlyOnce $new $post @'
  %128 = add i32 0, 0
  call void @rdx_protocol_recordFileData_indicateByRequestNum_ble(%struct.ReqFileInfo* @r_file_info)
'@ 'synchronous duplicate request'
    $Ir = Replace-ExactlyOnce $Ir $old $new 'download request adapter'

    $old = Get-RdxDownloadFunction $Ir 'rdx_protocol_recordFileData_indicateByRequestNum_ble'
    $new = Clear-RdxDownloadMetadata $old
    $new = Replace-ExactlyOnce $new '  %2 = alloca i32, align 4' `
        "  %2 = alloca i32, align 4`n  %download_generation = call i32 @rdx_file_download_generation()" 'sender generation'
    # Derive every chunk from the request, not previously decremented left_len.
    # This fixes duplicate tail packets and non-block-aligned resume offsets.
    $new = Replace-ExactlyOnce $new '  %109 = select i1 %108, i32 %107, i32 24480' `
        '  %109 = call i32 @rdx_file_download_chunk(%struct.ReqFileInfo* %0, %struct.uxfile_data_t* %30)' 'bounded stateless chunk size'
    $new = Replace-ExactlyOnce $new '  %110 = mul i32 %106, 24480' `
        "  %download_packet = load i32, i32* %32, align 4`n  %110 = mul i32 %download_packet, 24480" 'stateless chunk offset'
    $new = Replace-ExactlyOnce $new '  %146 = icmp slt i32 %145, 1' `
        '  %146 = icmp ne i32 %145, %109' 'short reads are transfer failures'
    $anchor = '  %167 = call i32 @rdx_protocol_send_bulk_data(i8* nonnull %115, i32 %158, i1 zeroext true)'
    $new = Replace-ExactlyOnce $new $anchor ($anchor + "`n  call void @rdx_file_download_sent(i32 %download_generation, %struct.ReqFileInfo* %0, %struct.uxfile_data_t* %30, i32 %143, i32 %109, i32 %145, i32 %167)") 'track only actual read/enqueue result'
    $new = Replace-ExactlyOnce $new `
        '%176 = phi i8* [ null, %117 ], [ %115, %139 ], [ null, %151 ], [ null, %169 ]' `
        '%176 = phi i8* [ null, %117 ], [ null, %139 ], [ null, %151 ], [ null, %169 ]' 'allocation failure double free'
    foreach ($register in @(52, 173, 185)) {
        $anchor = [regex]::Match($new, '(?m)^  %' + $register + ' = (?:tail )?call i32 @clock_free[^\n]*$').Value
        if (!$anchor) { throw "Missing download failure cleanup $register" }
        $new = Replace-ExactlyOnce $new $anchor ($anchor + "`n  call void @rdx_protocol_download_cleanup()") "terminal download failure $register"
    }
    $Ir = Replace-ExactlyOnce $Ir $old $new 'download sender completion evidence'

    # Cancellation invalidates progress even if a legacy cleanup returns early.
    foreach ($name in @('rdx_protocol_uploadFileInfo_clean', 'rdx_protocol_prepared_data_clean',
                         'rdx_protocol_file_sync_busy_timer_cb')) {
        $old = Get-RdxDownloadFunction $Ir $name
        $new = Clear-RdxDownloadMetadata $old
        $new = $new.Replace(" {`n", " {`n  call void @rdx_file_download_abort()`n")
        $Ir = Replace-ExactlyOnce $Ir $old $new "invalidate download on $name"
    }
    $old = Get-RdxDownloadFunction $Ir 'rdx_protocol_file_cmd_handle'
    $new = Clear-RdxDownloadMetadata $old
    $anchor = '; <label>:4:                                      ; preds = %1'
    $new = Replace-ExactlyOnce $new $anchor ($anchor + "`n  call void @rdx_file_download_abort()") 'stop invalidates transfer before early return'
    $Ir = Replace-ExactlyOnce $Ir $old $new 'stop download generation'

    # A bulk frame is one length-prefixed APP message (*DEV#filect# header plus
    # payload). The sender used to slip queued packets between its chunks, which
    # the APP's length parser swallows as payload. Once a frame starts it now
    # runs to its end; queued packets follow the frame.
    $old = Get-RdxDownloadFunction $Ir '_rdx_protocol_ble_send'
    $branch = [regex]::Match($old, '(?m)^  br i1 %23, label %26, label %24(, !dbg !\d+)?$')
    if (!$branch.Success) { throw 'Missing bulk frame interleave branch' }
    $new = Replace-ExactlyOnce $old $branch.Value ('  br label %31' + $branch.Groups[1].Value) 'frames are never interleaved'
    $Ir = Replace-ExactlyOnce $Ir $old $new 'bulk frame integrity'

    $Ir += @'

declare void @rdx_file_download_abort()
declare i32 @rdx_file_download_generation()
declare void @rdx_file_download_interrupt_notify()
declare zeroext i8 @rdx_file_download_take_interrupt()
declare i32 @rdx_file_download_request(i8*, i16 zeroext)
declare i32 @rdx_file_download_chunk(%struct.ReqFileInfo*, %struct.uxfile_data_t*)
declare void @rdx_file_download_sent(i32, %struct.ReqFileInfo*, %struct.uxfile_data_t*, i32, i32, i32, i32)
declare void @rdx_file_download_preempt_done(i32)
declare void @rdx_file_download_preempt_wait()

define i32 @rdx_protocol_handle_recordFileData_req(i8* %data, i16 zeroext %length) {
  %lock = call i32 @os_mutex_pend(%struct.OS_SEM* @file_trans_mutex, i32 0)
  %result = call i32 @rdx_file_download_request(i8* %data, i16 zeroext %length)
  %unlock = call i32 @os_mutex_post(%struct.OS_SEM* @file_trans_mutex)
  %interrupted = call zeroext i8 @rdx_file_download_take_interrupt()
  %is_interrupted = icmp ne i8 %interrupted, 0
  br i1 %is_interrupted, label %stop_transfer, label %done
stop_transfer:
  call void @rdx_protocol_fileTransfer_stop_indicate()
  br label %done
done:
  ret i32 %result
}

; Local recording outranks a BLE download. Admission posts this to
; protocol_send_task (the busy timer's task) so app_core never waits behind an
; SD read. Only the generation seen at admission is stopped: a transfer that
; already ended, or a newer one, is left to the parser's conflict branch.
; The stop indication is not queued here: the C fence sends it straight to ATT
; at the first frame boundary and keeps it ahead of every later packet. The
; prologue lets the frame in flight finish (bounded) so cleanup never cuts a
; frame the APP is already counting; the epilogue reports whether this
; callback cleaned up.
define void @rdx_protocol_download_preempt(i32 %generation) {
  call void @rdx_file_download_preempt_wait()
  %lock = call i32 @os_mutex_pend(%struct.OS_SEM* @file_trans_mutex, i32 0)
  %current = call i32 @rdx_file_download_generation()
  %live = icmp ne i32 %current, 0
  %same = icmp eq i32 %current, %generation
  %stop = and i1 %live, %same
  br i1 %stop, label %cleanup, label %release
cleanup:
  call void @rdx_protocol_download_cleanup()
  br label %release
release:
  %unlock = call i32 @os_mutex_post(%struct.OS_SEM* @file_trans_mutex)
  %stopped = zext i1 %stop to i32
  call void @rdx_file_download_preempt_done(i32 %stopped)
  ret void
}

; Called with file_trans_mutex held, before the immutable delete is queued.
; Also used for read/allocation/enqueue failure: no lingering 96 MHz request.
define void @rdx_protocol_download_cleanup() {
  call void @rdx_file_download_abort()
  call void @rdx_protocol_file_sync_busy_timer_stop()
  store i1 false, i1* @g_send_ctx.0, align 4
  %buffer = load i8*, i8** @g_send_ctx.1, align 4
  %allocated = icmp ne i8* %buffer, null
  br i1 %allocated, label %free, label %clear
free:
  call fastcc void @_bulk_data_copy_free(i8* %buffer)
  br label %clear
clear:
  store i8* null, i8** @g_send_ctx.1, align 4
  store i32 0, i32* @g_send_ctx.2, align 4
  store i32 0, i32* @g_send_ctx.3, align 4
  store i1 false, i1* @g_send_ctx.9, align 4
  store i1 false, i1* @g_send_ctx.10, align 4
  store i1 false, i1* @g_send_ctx.11, align 4
  store i8 0, i8* getelementptr inbounds (%struct.BleBulkSendData, %struct.BleBulkSendData* @bulk_send_data, i32 0, i32 10), align 1
  call void @rdx_protocol_download_purge_frames()
  store i8 0, i8* getelementptr inbounds (%struct.BLE_SendData, %struct.BLE_SendData* @ble_send_data, i32 0, i32 2), align 4
  call void @rdx_uxfile_recordFileData_send_finish(%struct.ReqFileInfo* @r_file_info)
  call void @rdx_protocol_prepared_data_clean()
  call fastcc void @rdx_protocol_file_transfer_track_reset()
  %clock = call i32 @clock_free(i8* getelementptr inbounds ([12 x i8], [12 x i8]* @.str.32, i32 0, i32 0))
  call void @rdx_ble_server_post_transfer_lowpower()
  ret void
}

; Nonzero while a started bulk frame still has bytes to send.
define i32 @rdx_protocol_bulk_frame_busy() {
  %active = load i1, i1* @g_send_ctx.0, align 4
  %buffer = load i8*, i8** @g_send_ctx.1, align 4
  %allocated = icmp ne i8* %buffer, null
  %sent = load i32, i32* @g_send_ctx.3, align 4
  %total = load i32, i32* @g_send_ctx.2, align 4
  %remaining = icmp ult i32 %sent, %total
  %started = icmp ne i32 %sent, 0
  %owned = and i1 %active, %allocated
  %partial = and i1 %started, %remaining
  %busy = and i1 %owned, %partial
  %result = zext i1 %busy to i32
  ret i32 %result
}

; Drop download frames that never started; keep every other queued packet
; (recording status, stream, stop) in order. Clearing the whole queue lost a
; status that was enqueued just before cleanup. The sender restarts on the
; semaphore if anything is left.
define void @rdx_protocol_download_purge_frames() {
entry:
  %packet = alloca %struct.DataPacket, align 4
  %count = call i32 @rdx_queue_getQueueSize(%struct.Queue* getelementptr inbounds (%struct.BLE_SendData, %struct.BLE_SendData* @ble_send_data, i32 0, i32 0))
  br label %loop
loop:
  %index = phi i32 [ 0, %entry ], [ %next, %advance ]
  %more = icmp ult i32 %index, %count
  br i1 %more, label %take, label %done
take:
  %taken = call zeroext i1 @rdx_queue_dequeue(%struct.Queue* getelementptr inbounds (%struct.BLE_SendData, %struct.BLE_SendData* @ble_send_data, i32 0, i32 0), %struct.DataPacket* %packet)
  br i1 %taken, label %inspect, label %done
inspect:
  %data_field = getelementptr inbounds %struct.DataPacket, %struct.DataPacket* %packet, i32 0, i32 0
  %data = load i8*, i8** %data_field, align 4
  %length_field = getelementptr inbounds %struct.DataPacket, %struct.DataPacket* %packet, i32 0, i32 1
  %length = load i32, i32* %length_field, align 4
  %empty = icmp eq i8* %data, null
  br i1 %empty, label %advance, label %sized
sized:
  %descriptor_sized = icmp eq i32 %length, 16
  br i1 %descriptor_sized, label %kind, label %keep
kind:
  %word = bitcast i8* %data to i32*
  %tag = load i32, i32* %word, align 4
  %bulk = icmp eq i32 %tag, 1
  br i1 %bulk, label %drop, label %keep
drop:
  call void @rdx_protocol_free_send_queue_packet(%struct.DataPacket* %packet)
  br label %advance
keep:
  %kept = call zeroext i1 @rdx_queue_enqueue(%struct.Queue* getelementptr inbounds (%struct.BLE_SendData, %struct.BLE_SendData* @ble_send_data, i32 0, i32 0), %struct.DataPacket* byval align 4 %packet)
  call void @free(i8* %data)
  br label %advance
advance:
  %next = add i32 %index, 1
  br label %loop
done:
  %left = call i32 @rdx_queue_getQueueSize(%struct.Queue* getelementptr inbounds (%struct.BLE_SendData, %struct.BLE_SendData* @ble_send_data, i32 0, i32 0))
  %idle = icmp eq i32 %left, 0
  br i1 %idle, label %finish, label %wake
wake:
  %posted = call i32 @os_sem_post(%struct.OS_SEM* getelementptr inbounds (%struct.BLE_SendData, %struct.BLE_SendData* @ble_send_data, i32 0, i32 1))
  br label %finish
finish:
  ret void
}
'@
    return $Ir
}
