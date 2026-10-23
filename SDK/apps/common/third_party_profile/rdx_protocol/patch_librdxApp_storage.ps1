# Loaded by patch_librdxApp.ps1 after its original-archive hash check.
# These edits target that exact IR revision; never patch an arbitrary archive.
function Update-RdxStorageIr {
    param([Parameter(Mandatory = $true)][string]$Ir)

    # JL fwrite returns BYTES, not the ISO C number of items. Preserve each
    # call's ABI and latch incomplete I/O until reboot, including async work.
    foreach ($spec in @(@('fwrite', 8), @('fclose', 37), @('f_flush_wbuf', 2), @('fdelete', 8))) {
        $name = $spec[0]
        $pattern = '(?m)^(  .*call i32 )@' + $name + '\('
        $count = [regex]::Matches($Ir, $pattern).Count
        if ($count -ne $spec[1]) { throw "Storage patch: unexpected $name call count $count" }
        $Ir = [regex]::Replace($Ir, $pattern, '${1}@rdx_storage_' + $name + '(')
    }

    # The original cache writer clears dirty even after short writes or a
    # failed close/flush. A latched failure must retain dirty and reach callers.
    $Ir = Replace-ExactlyOnce $Ir `
        '  store i8 0, i8* @g_dat_cache.4, align 4, !dbg !1230, !tbaa !871' `
        "  %rdx_dirty_error = load volatile i32, i32* @rdx_storage_io_error, align 4`n  %rdx_keep_dirty = icmp ne i32 %rdx_dirty_error, 0`n  %rdx_dirty_value = zext i1 %rdx_keep_dirty to i8`n  store i8 %rdx_dirty_value, i8* @g_dat_cache.4, align 4" `
        'retain dirty on storage I/O failure'

    foreach ($spec in @(@('%190', '!1242'), @('%81', '!2803'), @('%77', '!3452'), @('%477', '!5811'))) {
        $value = $spec[0]
        $dbg = $spec[1]
        $Ir = Replace-ExactlyOnce $Ir "  ret i32 $value, !dbg $dbg" `
            "  %rdx_checked = call i32 @rdx_storage_checked_result(i32 $value)`n  ret i32 %rdx_checked, !dbg $dbg" `
            "propagate storage result $dbg"
    }
    # A successfully discarded <4000-byte recording is distinct from I/O
    # failure. Keep the existing short-file policy, but make its result usable.
    $Ir = Replace-ExactlyOnce $Ir `
        '  %81 = phi i32 [ -1, %0 ], [ -1, %11 ], [ -1, %14 ], [ -1, %5 ], [ -1, %20 ], [ -1, %76 ], [ 0, %77 ]' `
        '  %81 = phi i32 [ -1, %0 ], [ 1, %11 ], [ -1, %14 ], [ -1, %5 ], [ -1, %20 ], [ -1, %76 ], [ 0, %77 ]' `
        'distinguish discarded short recording'

    # The format layer owns the low-range allocation watermark, including an
    # empty DAT and migration from a VM polluted by recovery-management IDs.
    # Name the old numeric SSA slots before replacing its allocation CFG.
    $pattern = '(?ms)^define i32 @rdx_uxfile_dat_1_gen\(i8 zeroext\)[^\n]*\{.*?^\}'
    $generator = [regex]::Match($Ir, $pattern).Value
    if (!$generator) { throw 'Missing recording identity generator' }
    $namedGenerator = [regex]::Replace($generator, '%(\d+)\b', '%rdx_gen_$1')
    $namedGenerator = [regex]::Replace($namedGenerator,
        '(?m)^; <label>:(\d+):[^\n]*$', 'rdx_gen_$1:')
    $namedGenerator = $namedGenerator.Replace(
        '@rdx_uxfile_dat_1_gen(i8 zeroext)',
        '@rdx_uxfile_dat_1_gen(i8 zeroext %rdx_gen_0)')
    $namedGenerator = $namedGenerator.Replace("{`n", "{`nrdx_gen_1:`n")
    $allocationPattern = '(?ms)^  %rdx_gen_8 = load i1, i1\* @g_dat_cache\.3, align 4\n.*?(?=^  %rdx_gen_24 = tail call i32)'
    $oldAllocation = [regex]::Match($namedGenerator, $allocationPattern).Value
    if (!$oldAllocation) { throw 'Missing complete recording allocation branches' }
    $allocationLocation = [regex]::Match($oldAllocation, '@rdx_dat_cache_persist_max_sn[^\n]*(!dbg !\d+)').Groups[1].Value
    if (!$allocationLocation) { throw 'Missing recording allocation call-site location' }
    $allocation = @'
  store i32 0, i32* getelementptr inbounds (%struct.uxfile_filelist_t, %struct.uxfile_filelist_t* @uxFileList, i32 0, i32 0), align 4
  %rdx_sn_status = call i32 @rdx_storage_reserve_sn(i32* getelementptr inbounds (%struct.uxfile_filelist_t, %struct.uxfile_filelist_t* @uxFileList, i32 0, i32 0))
  %rdx_sn = load i32, i32* getelementptr inbounds (%struct.uxfile_filelist_t, %struct.uxfile_filelist_t* @uxFileList, i32 0, i32 0), align 4
  %rdx_sn_ok = icmp eq i32 %rdx_sn_status, 0
  %rdx_sn_nonzero = icmp ne i32 %rdx_sn, 0
  %rdx_sn_low = icmp ule i32 %rdx_sn, 268435455
  %rdx_sn_valid = and i1 %rdx_sn_nonzero, %rdx_sn_low
  %rdx_sn_reserved = and i1 %rdx_sn_ok, %rdx_sn_valid
  br i1 %rdx_sn_reserved, label %rdx_sn_publish, label %rdx_sn_failed
rdx_sn_failed:
  store i32 0, i32* getelementptr inbounds (%struct.uxfile_filelist_t, %struct.uxfile_filelist_t* @uxFileList, i32 0, i32 0), align 4
  call void @llvm.lifetime.end(i64 4, i8* nonnull %rdx_gen_7)
  call void @llvm.lifetime.end(i64 64, i8* nonnull %rdx_gen_6)
  ret i32 -1
rdx_sn_publish:
  store i32 %rdx_sn, i32* getelementptr inbounds (%struct.uxfile_data_t, %struct.uxfile_data_t* @uxOperateFileData, i32 0, i32 0), align 4
'@
    $allocation = [regex]::Replace($allocation, '(?m)^(  (?:%\w+ = )?call [^\n]+)$', '$1, ' + $allocationLocation)
    $namedGenerator = Replace-ExactlyOnce $namedGenerator $oldAllocation ($allocation + "`n") 'single recording SN allocator'
    $Ir = Replace-ExactlyOnce $Ir $generator $namedGenerator 'replace legacy recording allocation'

    # Cache insertion also called the old unchecked VM writer. Retire it so
    # only a successful format-layer reservation can advance VM147.
    $pattern = '(?ms)^define internal fastcc void @rdx_dat_cache_persist_max_sn\([^\n]*\{.*?^\}'
    $persist = [regex]::Match($Ir, $pattern).Value
    if (!$persist) { throw 'Missing legacy SN watermark writer' }
    $Ir = Replace-ExactlyOnce $Ir $persist "define internal fastcc void @rdx_dat_cache_persist_max_sn(i32 %unused) unnamed_addr {`n  ret void`n}" 'retire unchecked SN watermark writes'
    # Old deletion/auto-delete paths reset this public writer to zero. Only
    # the format layer may reset the watermark after a successful explicit erase.
    $pattern = '(?ms)^define void @rdx_uxfile_write_serial_number_into_vm\([^\n]*\{.*?^\}'
    $serialWriter = [regex]::Match($Ir, $pattern).Value
    if (!$serialWriter) { throw 'Missing legacy public SN watermark writer' }
    $Ir = Replace-ExactlyOnce $Ir $serialWriter "define void @rdx_uxfile_write_serial_number_into_vm(i32 %unused) {`n  ret void`n}" 'retire deletion-driven SN watermark resets'
    if ($Ir -match '(?m)^\s*(?:%\w+ = )?(?:tail )?call i32 @syscfg_write\(i16 zeroext 147,') {
        throw 'Unexpected library-side recording SN watermark write remains'
    }

    # The helper consumes the already opened RAW handle, then lets the format
    # layer durably remove RAW and MTA together. Never close that handle twice.
    $pattern = '(?ms)^define i32 @rdx_uxfile_dat_1_save_gen\([^\n]*\{.*?^\}'
    $save = [regex]::Match($Ir, $pattern).Value
    if (!$save) { throw 'Missing recording save implementation' }
    $shortDelete = [regex]::Match($save, '(?m)^  %12 = tail call i32 @rdx_storage_fdelete\(%struct\.FILE\* nonnull %6\)[^\n]*$').Value
    $shortClose = [regex]::Match($save, '(?m)^  %15 = tail call i32 @rdx_storage_fclose\(%struct\.FILE\* nonnull %6\)[^\n]*$').Value
    if (!$shortDelete -or !$shortClose) { throw 'Missing short-recording handle ownership branches' }
    $shortLocation = [regex]::Match($shortDelete, '!dbg !\d+').Value
    if (!$shortLocation) { throw 'Missing short-recording discard call-site location' }
    $safeSave = Replace-ExactlyOnce $save $shortDelete ('  %12 = call i32 @rdx_storage_discard_short(%struct.FILE* %6), ' + $shortLocation) 'transactional short recording discard'
    $safeSave = Replace-ExactlyOnce $safeSave $shortClose '  %15 = add i32 0, 0' 'short discard helper owns RAW handle'
    $Ir = Replace-ExactlyOnce $Ir $save $safeSave 'safe short recording save path'

    # The legacy RAW writer has its own generator fallback. A rejected SN
    # reservation must not proceed to any file open/write through that path.
    $pattern = '(?ms)^define i32 @rdx_uxfile_raw_write\([^\n]*\{.*?^\}'
    $raw = [regex]::Match($Ir, $pattern).Value
    if (!$raw) { throw 'Missing RAW writer allocation fallback' }
    $fallback = [regex]::Match($raw, '(?m)^(  %23 = tail call i32 @rdx_uxfile_dat_1_gen\(i8 zeroext %2\)[^\n]*)\n  br label %24[^\n]*$')
    if (!$fallback.Success) { throw 'Missing unchecked RAW generation branch' }
    $fallbackLocation = [regex]::Match($fallback.Value, '!dbg !\d+').Value
    if (!$fallbackLocation) { throw 'Missing RAW generation fallback call-site location' }
    $checkedFallback = $fallback.Groups[1].Value + @'

  %rdx_generated = icmp eq i32 %23, 0
  br i1 %rdx_generated, label %24, label %rdx_generate_failed
rdx_generate_failed:
  %rdx_generate_error = call i32 @rdx_storage_save_result(i32 -1)
  ret i32 %rdx_generate_error
'@
    $checkedFallback = $checkedFallback.Replace('  %rdx_generate_error = call i32 @rdx_storage_save_result(i32 -1)',
        ('  %rdx_generate_error = call i32 @rdx_storage_save_result(i32 -1), ' + $fallbackLocation))
    $safeRaw = Replace-ExactlyOnce $raw $fallback.Value $checkedFallback 'fail closed after RAW generation rejection'
    $Ir = Replace-ExactlyOnce $Ir $raw $safeRaw 'checked RAW writer allocation fallback'

    # Keep a save/scan failure sticky even when the old caller only logs it.
    # This catches open failures as well as the wrapped write/close failures.
    $Ir = Replace-ExactlyOnce $Ir `
        '  %rdx_checked = call i32 @rdx_storage_checked_result(i32 %190)' `
        '  %rdx_checked = call i32 @rdx_storage_save_result(i32 %190)' `
        'latch cache save failure'
    $Ir = Replace-ExactlyOnce $Ir `
        '  %rdx_checked = call i32 @rdx_storage_checked_result(i32 %77)' `
        '  %rdx_checked = call i32 @rdx_storage_save_result(i32 %77)' `
        'latch every raw recording failure'

    # Private UXFILE message 0x52445846 (RDXF), accompanied by a nonzero ticket.
    # It is explicitly handled by the worker; it is NOT a guessed Q_CALLBACK.
    $Ir = Replace-ExactlyOnce $Ir '    i32 233, label %28' `
        "    i32 233, label %28`n    i32 1380210758, label %rdx_fence`n    i32 1380210770, label %rdx_refresh" 'file worker fence dispatch'
    $Ir = Replace-ExactlyOnce $Ir `
        '  br label %21, !dbg !6233' `
        "  br label %21, !dbg !6233`n`nrdx_fence:`n  %rdx_ticket = load i32, i32* %12, align 4`n  call void @rdx_storage_fence_arrive(i32 %rdx_ticket)`n  br label %21`n`nrdx_refresh:`n  call void @rdx_storage_boot_reconcile(i32 %9)`n  br label %21" `
        'file worker fence completion'

    # Exhausted RAW-open retries must return failure to the coordinator, not
    # reset before it can retain the dirty recording and refuse SD export.
    $Ir = Replace-ExactlyOnce $Ir `
        '  tail call void @rdx_cpu_reset() #11, !dbg !3433' `
        '  store volatile i32 -1, i32* @rdx_storage_io_error, align 4' `
        'raw open failure cannot bypass safe shutdown'

    $Ir = Replace-ExactlyOnce $Ir `
        '  br i1 %16, label %17, label %21, !dbg !6208' `
        "  %rdx_quiet_state = load volatile i32, i32* @rdx_storage_quiet, align 4`n  %rdx_worker_open = icmp ne i32 %rdx_quiet_state, 2`n  %rdx_dispatch = and i1 %16, %rdx_worker_open`n  br i1 %rdx_dispatch, label %17, label %21, !dbg !6208" `
        'freeze file worker after successful fence'
    $Ir = Replace-ExactlyOnce $Ir `
        '  br label %34, !dbg !6175' `
        "  call void @rdx_storage_boot_refresh_post()`n  br label %34, !dbg !6175" `
        'reconcile PC changes before business admission'

    # Wrapper entry blocks even an already queued timer callback. The caller is
    # app_core, so an active timer finishes before app_core requests quiescence.
    foreach ($name in @('rdx_uxfile_sync_timer_callback', 'rdx_uxfile_boot_quick_fix', 'rdx_uxfile_flush_cache_timer_cb')) {
        $pattern = '(?m)^define internal void @' + $name + '\(i8\* nocapture readnone\) #0 (!dbg !\d+) \{'
        $match = [regex]::Match($Ir, $pattern)
        if (!$match.Success) { throw "Missing timer definition: $name" }
        $Ir = Replace-ExactlyOnce $Ir $match.Value ($match.Value.Replace('@' + $name + '(', '@' + $name + '_impl(')) "quiet timer $name"
        $Ir += "`ndefine internal void @$name(i8* %arg) {`n  %state = load volatile i32, i32* @rdx_storage_quiet, align 4`n  %open = icmp eq i32 %state, 0`n  br i1 %open, label %check_refresh, label %done`ncheck_refresh:`n  %refresh = load volatile i32, i32* @rdx_storage_refresh, align 4`n  %ready = icmp eq i32 %refresh, 0`n  br i1 %ready, label %run, label %defer_check`ndefer_check:`n  %pending = icmp eq i32 %refresh, 1`n  br i1 %pending, label %defer, label %done`ndefer:`n  %timer = call zeroext i16 @sys_timeout_add(i8* %arg, void (i8*)* @$name, i32 500)`n  br label %done`nrun:`n  call void @${name}_impl(i8* %arg)`n  br label %done`ndone:`n  ret void`n}`n"
    }

    $helpers = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'rdx_storage_patch.ll')).Replace("`r`n", "`n")
    $snHelpers = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'rdx_sn_patch.ll')).Replace("`r`n", "`n")
    return $Ir + "`n" + $helpers + "`n" + $snHelpers
}
