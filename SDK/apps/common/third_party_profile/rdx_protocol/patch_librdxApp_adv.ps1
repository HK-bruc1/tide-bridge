# 仅在 patch_librdxApp.ps1 校验原始静态库哈希通过后应用。
function Update-RdxAdvIr {
    param([string]$Ir)

    # 所有 UXFILE 消息发送方均使用带准入标记的封装，包括内部
    # 关闭和刷新消息；保持原有可变参数 ABI。
    $Ir = $Ir.Replace('@os_taskq_post_msg(', '@rdx_adv_uxfile_post(')
    $Ir += "`ndeclare i32 @rdx_adv_policy_business_enter()`ndeclare void @rdx_adv_policy_business_exit()`ndeclare void @rdx_adv_uxfile_done(i32, i32)`n"
    $Ir = Replace-ExactlyOnce $Ir `
        '  %18 = load i32, i32* %11, align 4, !dbg !6209, !tbaa !970' `
        "  %rdx_adv_message = load i32, i32* %11, align 4`n  %18 = and i32 %rdx_adv_message, -1073741825" `
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
        @('rdx_uxfile_get_file_data_by_sn', '%struct.uxfile_data_t*', 'i32|i8|i32', '', 'null'),
        @('rdx_uxfile_get_file_data_by_filename', '%struct.uxfile_data_t*', 'i8*', '', 'null'),
        @('rdx_uxfile_get_datFileInfo', '%struct.uxfile_datfile_info_t*', '', '', 'null')
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
        $Ir += "`ndefine $scope$ret @$name($argsText) {`nentry:`n  %admit = call i32 @rdx_adv_policy_business_enter()`n  %ok = icmp eq i32 %admit, 0`n  br i1 %ok, label %run, label %fail`nrun:`n  %result = call $callcc$ret @${name}_adv_impl($argsText)`n  call void @rdx_adv_policy_business_exit()`n  ret $ret %result`nfail:`n  ret $ret $failure`n}`n"
    }
    return $Ir
}
