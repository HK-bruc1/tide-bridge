# 仅在原始库哈希及既有存储/恢复补丁检查通过后应用。
function Update-RdxCacheIr {
    param([string]$Ir)

    # 覆盖全部 dirty 写入，包括同步保存、加载和显式格式化后的清除。
    # 原库 ABI 改变时必须重新审阅，不能仅调整计数绕过检查。
    $stores = '(?m)^  store i8 [^\n]*i8\* @g_dat_cache\.4[^\n]*$'
    if ([regex]::Matches($Ir, $stores).Count -ne 14) {
        throw 'Cache patch: unexpected dirty store count'
    }
    $Ir = [regex]::Replace($Ir, '(?ms)^define [^\n]+\{.*?^\}', {
        param($function)
        $body = $function.Value
        $location = [regex]::Match($body, '(?m)^  [^\n]+(!dbg !\d+)').Groups[1].Value
        [regex]::Replace($body, $stores, {
            param($store)
            $site = [regex]::Match($store.Value, '!dbg !\d+').Value
            if (!$site) { $site = $location }
            $suffix = if ($site) { ', ' + $site } else { '' }
            $store.Value + "`n  call void @rdx_cache_flush_changed()$suffix"
        })
    })

    # 删除启动后 10 秒起步、随后每 5 秒自续约的业务轮询。
    $Ir = Replace-ExactlyOnce $Ir `
        '  %33 = call zeroext i16 @sys_timeout_add(i8* null, void (i8*)* nonnull @rdx_uxfile_flush_cache_timer_cb, i32 10000) #11, !dbg !6174' `
        '  %33 = add i16 0, 0' 'remove periodic cache startup'
    foreach ($name in @('rdx_uxfile_flush_cache_timer_cb_impl', 'rdx_uxfile_flush_cache_timer_cb')) {
        $pattern = '(?ms)^define [^\n]*@' + $name + '\([^\n]*\{.*?^\}\n'
        if ([regex]::Matches($Ir, $pattern).Count -ne 1) { throw "Missing cache timer: $name" }
        $Ir = [regex]::Replace($Ir, $pattern, '')
    }
    $Ir = Replace-ExactlyOnce $Ir `
        '  call void @rdx_storage_boot_refresh_post()' `
        "  call void @rdx_cache_flush_start(), !dbg !6174`n  call void @rdx_storage_boot_refresh_post()" 'start cache coordinator'
    $pattern = '(?m)^(define void @rdx_uxfile_task_free\([^\n]*\{)$'
    if ([regex]::Matches($Ir, $pattern).Count -ne 1) { throw 'Missing cache stop owner' }
    $Ir = [regex]::Replace($Ir, $pattern, '$1' + "`n  call void @rdx_cache_flush_stop(), !dbg !6503")
    $helpers = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'rdx_cache_patch.ll')).Replace("`r`n", "`n")
    return $Ir + "`n" + $helpers
}
