function Update-RdxProtocolIr {
    param([Parameter(Mandatory = $true)][string]$Ir)
    $Ir = $Ir.Replace("`r`n", "`n")

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
