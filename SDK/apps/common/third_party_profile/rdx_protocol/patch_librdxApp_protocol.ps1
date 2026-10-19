function Update-RdxProtocolIr {
    param([Parameter(Mandatory = $true)][string]$Ir)
    $Ir = $Ir.Replace("`r`n", "`n")

    # 接收任务空闲无限等待；仅成功出队后刷新原生算力，保留连续消费和自通知。
    $worker = [regex]::Match($Ir, '(?ms)^define internal void @rdx_protocol_recv_task\([^\n]*\).*?^\}').Value
    if (!$worker) { throw 'Missing receive worker' }
    $updated = Replace-ExactlyOnce $worker `
        'i32 100) #12, !dbg !7740' 'i32 0) #12, !dbg !7740' 'receive event wait'
    $updated = Replace-ExactlyOnce $updated `
        '  tail call void @clock_refurbish() #12, !dbg !7744' `
        '  ; 空唤醒不刷新原生调频观察窗口。' 'remove idle clock refresh'
    $dispatch = '  store i8 1, i8* getelementptr inbounds (%struct.BLE_RecvData, %struct.BLE_RecvData* @ble_recv_data, i32 0, i32 2), align 4, !dbg !7757, !tbaa !5326'
    $updated = Replace-ExactlyOnce $updated $dispatch `
        ($dispatch + "`n  call void @clock_refurbish() #12, !dbg !7757") 'receive business clock refresh'
    $Ir = Replace-ExactlyOnce $Ir $worker $updated 'event driven receive worker'

    # OS_SEM_OVF=28 表示已有唤醒计数，不是丢通知；其他错误不能谎报屏障投递成功。
    $notify = '  %48 = call i32 @os_sem_post(%struct.OS_SEM* getelementptr inbounds (%struct.BLE_RecvData, %struct.BLE_RecvData* @ble_recv_data, i32 0, i32 1)) #12, !dbg !5358'
    $checkedNotify = @'
  %rdx_recv_post_ok = icmp eq i32 %48, 0
  %rdx_recv_post_full = icmp eq i32 %48, 28
  %rdx_recv_notified = or i1 %rdx_recv_post_ok, %rdx_recv_post_full
  %rdx_recv_accepted = select i1 %rdx_recv_notified, i32 %9, i32 0
'@
    $Ir = Replace-ExactlyOnce $Ir $notify ($notify + "`n" + $checkedNotify) 'receive notification result'
    $Ir = Replace-ExactlyOnce $Ir `
        '%50 = phi i32 [ 0, %11 ], [ 0, %17 ], [ %9, %47 ], [ 0, %36 ], [ 0, %43 ]' `
        '%50 = phi i32 [ 0, %11 ], [ 0, %17 ], [ %rdx_recv_accepted, %47 ], [ 0, %36 ], [ 0, %43 ]' `
        'receive admission result'

    # GETLIST 在协议目标文件投递，需单独补上从入队到消费结束的业务占用。
    $listPosts = [regex]::Matches($Ir, '(?m)^.*call i32 \(i8\*, i32, \.\.\.\) @os_taskq_post_msg\(i8\* getelementptr inbounds \(\[7 x i8\], \[7 x i8\]\* @\.str\.\d+, i32 0, i32 0\), i32 1, i32 233\)[^\r\n]*$')
    if ($listPosts.Count -ne 1) { throw 'Expected one GETLIST UXFILE producer' }
    $listPost = $listPosts[0].Value
    $Ir = Replace-ExactlyOnce $Ir $listPost ($listPost.Replace(
        '@os_taskq_post_msg(', '@rdx_adv_uxfile_post(')) 'GETLIST queued power admission'

    # 队列失败同步返回原协议失败响应，不再投递无占用的旁路回调。
    $fallbacks = [regex]::Matches($Ir, '(?ms)^  store i32 ptrtoint \(void \(\)\* @rdx_protocol_record_file_info_indicate to i32\)[^\r\n]*\r?\n(?:(?!^\}).)*?^  %\d+ = call i32 @os_taskq_post_type\([^\r\n]*')
    if ($fallbacks.Count -ne 1) { throw 'Expected one GETLIST fallback callback' }
    $fallback = $fallbacks[0].Value
    $Ir = Replace-ExactlyOnce $Ir $fallback ($fallback.Replace(
        '@os_taskq_post_type(', '@rdx_protocol_list_queue_rejected(')) 'GETLIST bounded failure response'

    $pattern = '(?ms)^define internal fastcc zeroext i1 @_rdx_is_storage_busy\(\)[^\r\n]*\{.*?^\}'
    $original = [regex]::Match($Ir, $pattern).Value
    if (!$original) { throw 'Missing protocol storage admission predicate' }
    $replacement = @'
define internal fastcc zeroext i1 @_rdx_is_storage_busy() {
  %cdc = call i32 @rdx_record_format_protocol_busy()
  %blocked = icmp ne i32 %cdc, 0
  br i1 %blocked, label %busy, label %legacy
busy:
  ret i1 true
legacy:
  %format = call zeroext i1 @rdx_uxfile_sd_format_status_check()
  ret i1 %format
}
'@
    $Ir = Replace-ExactlyOnce $Ir $original $replacement 'CDC protocol storage admission'
    $pattern = '(?m)^define void @rdx_protocol_record_file_info_indicate\(\)[^\r\n]*\{'
    $original = [regex]::Match($Ir, $pattern).Value
    if (!$original) { throw 'Missing asynchronous BLE list entry' }
    $Ir = Replace-ExactlyOnce $Ir $original ($original.Replace(
        '@rdx_protocol_record_file_info_indicate(', '@rdx_protocol_record_file_info_indicate_admitted(')) 'BLE list worker admission'
    $Ir += @'

define internal i32 @rdx_protocol_list_queue_rejected(i8* %task, i32 %type, i32 %argc, i32* %args) {
  call void @rdx_protocol_file_list_ack_indicate(i8 zeroext 1)
  ret i32 -1
}

define void @rdx_protocol_record_file_info_indicate() {
  %state = call i32 @rdx_record_format_io_enter()
  %ready = icmp eq i32 %state, 0
  br i1 %ready, label %run, label %busy
run:
  call void @rdx_protocol_record_file_info_indicate_admitted()
  call void @rdx_record_format_io_leave()
  ret void
busy:
  %blocked = call i32 @rdx_record_format_protocol_busy()
  call void @rdx_protocol_file_list_ack_indicate(i8 zeroext 1)
  ret void
}
'@
    foreach ($name in @('rdx_protocol_recieve_data_handle', 'rdx_protocol_ota_handle')) {
        $pattern = '(?m)^define void @' + $name + '\(i8\*, i16 zeroext\)[^\r\n]*\{'
        $original = [regex]::Match($Ir, $pattern).Value
        if (!$original) { throw "Missing protocol command entry: $name" }
        $Ir = Replace-ExactlyOnce $Ir $original ($original.Replace("@$name(", "@${name}_admitted(")) "reserve $name"
        $Ir += @"

define void @$name(i8* %data, i16 zeroext %length) {
  call void @rdx_record_format_command_enter()
  call void @${name}_admitted(i8* %data, i16 zeroext %length)
  call void @rdx_record_format_command_leave()
  ret void
}
"@
    }
    $Ir += @'

declare i32 @rdx_record_format_protocol_busy()
declare i32 @rdx_record_format_io_enter()
declare void @rdx_record_format_command_enter()
declare void @rdx_record_format_command_leave()
declare void @rdx_record_format_io_leave()
declare i32 @rdx_adv_uxfile_post(i8*, i32, ...)
'@
    return $Ir
}
