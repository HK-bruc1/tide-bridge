# 仅在 patch_librdxApp.ps1 校验原始静态库哈希通过后应用。
function Update-RdxAdvIr {
    param([string]$Ir)

    # 所有 UXFILE 消息发送方均使用带准入标记的封装，包括内部
    # 关闭和刷新消息；保持原有可变参数 ABI。
    $Ir = $Ir.Replace('@os_taskq_post_msg(', '@rdx_adv_uxfile_post(')
    $Ir += "`ndeclare i32 @rdx_adv_policy_business_enter()`ndeclare void @rdx_adv_policy_business_exit()`ndeclare void @rdx_adv_uxfile_done(i32, i32)`n"
    $Ir = Replace-ExactlyOnce $Ir `
        '  %18 = load i32, i32* %11, align 4, !dbg !6209, !tbaa !970' `
        "  %rdx_adv_message = load i32, i32* %11, align 4`n  %18 = and i32 %rdx_adv_message, -536870913" `
        'strip UXFILE admission tag'
    $Ir = Replace-ExactlyOnce $Ir `
        '  br label %14, !dbg !6216, !llvm.loop !6218' `
        "  %rdx_adv_received = zext i1 %16 to i32`n  %rdx_adv_finished = load i32, i32* %11, align 4`n  call void @rdx_adv_uxfile_done(i32 %rdx_adv_received, i32 %rdx_adv_finished), !dbg !6216`n  br label %14, !dbg !6216, !llvm.loop !6218" `
        'release UXFILE admission after dispatch'

    # 同步文件操作也可能由工作线程之外发起，因此封装完整操作，
    # 而非单次 fwrite 调用或仅查询忙状态的接口。
    $specs = @(
        @('rdx_dat_cache_save', 'i32', '', 'fastcc', '-1'),
        @('rdx_dat_cache_load', 'i32', '', 'fastcc', '-1'),
        @('rdx_uxfile_dat_1_gen', 'i32', 'i8', '', '-1'),
        @('rdx_uxfile_dat_1_save_gen', 'i32', '', '', '-1'),
        @('rdx_uxfile_dat_file_update', 'i32', '', '', '-1'),
        @('rdx_uxfile_sd_format', 'i32', 'void (i8)*', '', '-1'),
        @('rdx_uxfile_sync_files_with_dat', 'i32', '', '', '-1'),
        @('rdx_uxfile_recordFile_delete_handle', 'i32', 'i32|i8*', '', '-1'),
        @('rdx_uxfile_delete_file_by_fnumAndfilename', 'i32', 'i32|i8*', '', '-1'),
        @('rdx_uxfile_get_file_data_by_sn', '%struct.uxfile_data_t*', 'i32|i8|i32', '', '@rdx_empty_file'),
        @('rdx_uxfile_get_file_data_by_filename', '%struct.uxfile_data_t*', 'i8*', '', '@rdx_empty_file'),
        @('rdx_uxfile_get_datFileInfo', '%struct.uxfile_datfile_info_t*', '', '', '@rdx_empty_list')
    )
    foreach ($spec in $specs) {
        $name, $ret, $argTypes, $cc, $failure = $spec
        $pattern = '(?m)^define [^\n]*@' + $name + '\([^\n]+\{'
        $m = [regex]::Match($Ir, $pattern)
        if (!$m.Success) { throw "Missing advertising admission target: $name" }
        $Ir = Replace-ExactlyOnce $Ir $m.Value ($m.Value.Replace("@$name(", "@${name}_adv_impl(")) "admission $name"
        $args = @()
        if ($argTypes) {
            $i = 0
            foreach ($type in $argTypes.Split('|')) {
                $attr = if ($type -eq 'i8') { ' zeroext' } else { '' }
                $args += "$type$attr %arg$i"
                $i++
            }
        }
        $argsText = $args -join ', '
        $scope = if ($cc) { "internal $cc " } else { '' }
        $callcc = if ($cc) { "$cc " } else { '' }
        # 查询调用者依赖结构体返回；失败沿用恢复门禁的独立空对象。
        $clear = if ($failure.StartsWith('@rdx_empty_')) { "  store $($ret.TrimEnd('*')) zeroinitializer, $ret $failure, align 4`n" } else { '' }
        $Ir += "`ndefine $scope$ret @$name($argsText) {`nentry:`n  %admit = call i32 @rdx_adv_policy_business_enter()`n  %ok = icmp eq i32 %admit, 0`n  br i1 %ok, label %run, label %fail`nrun:`n  %result = call $callcc$ret @${name}_adv_impl($argsText)`n  call void @rdx_adv_policy_business_exit()`n  ret $ret %result`nfail:`n$clear  ret $ret $failure`n}`n"
    }
    # 这些存储事实长于单次函数调用；必须在卸载前读取，不能只看传输忙位。
    $Ir += @'

define zeroext i8 @rdx_uxfile_power_busy() {
  %dirty = load volatile i8, i8* @g_dat_cache.4, align 4
  %file = load volatile i8*, i8** getelementptr inbounds (%struct.uxfile_data_t, %struct.uxfile_data_t* @uxReadFileData, i32 0, i32 12), align 4
  %error = load volatile i32, i32* @rdx_storage_io_error, align 4
  %head = load volatile i8, i8* @delete_queue_head, align 1
  %tail = load volatile i8, i8* @delete_queue_tail, align 1
  %deleting = load volatile i8, i8* @delete_in_progress, align 1
  %a = icmp ne i8 %dirty, 0
  %b = icmp ne i8* %file, null
  %c = icmp ne i32 %error, 0
  %d = icmp ne i8 %head, %tail
  %e = icmp ne i8 %deleting, 0
  %ab = or i1 %a, %b
  %cd = or i1 %c, %d
  %abcd = or i1 %ab, %cd
  %busy = or i1 %abcd, %e
  %result = zext i1 %busy to i8
  ret i8 %result
}
'@
    return $Ir
}
