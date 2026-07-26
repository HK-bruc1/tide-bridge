param(
    [ValidateSet('Baseline', 'Progress', 'Final')]
    [string]$Mode = 'Baseline',
    [string]$BaselineRef = '128eb8d'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$rdxRel = 'SDK/apps/common/third_party_profile/rdx_protocol'
$rdxRoot = Join-Path $repo $rdxRel
$script:Errors = @()

function Normalize-LineEndings([string]$Text) {
    return $Text.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Add-Pass([string]$Message) {
    Write-Host "PASS: $Message"
}

function Add-Failure([string]$Message) {
    $script:Errors += $Message
    Write-Host "FAIL: $Message"
}

function Read-Baseline([string]$RelativePath) {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = 'git'
    $startInfo.Arguments = "-C `"$repo`" show `"${BaselineRef}:$RelativePath`""
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [System.Text.Encoding]::UTF8

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $content = $process.StandardOutput.ReadToEnd()
    $process.StandardError.ReadToEnd() | Out-Null
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        return ''
    }
    return Normalize-LineEndings $content
}

function Read-Working([string]$RelativePath) {
    $path = Join-Path $repo $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        return ''
    }
    return Normalize-LineEndings ([System.IO.File]::ReadAllText($path))
}

function Count-Pattern([string]$Text, [string]$Pattern) {
    return [regex]::Matches(
        $Text,
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Singleline
    ).Count
}

function Assert-OrderedTokens(
    [string]$Text,
    [string[]]$Tokens,
    [string]$Label
) {
    $position = 0
    foreach ($token in $Tokens) {
        $next = $Text.IndexOf($token, $position, [System.StringComparison]::Ordinal)
        if ($next -lt 0) {
            Add-Failure "$Label missing or out-of-order token: $token"
            return
        }
        $position = $next + $token.Length
    }
    Add-Pass "$Label keeps the frozen call order"
}

function Get-FunctionSlice(
    [string]$Text,
    [string]$StartToken,
    [string]$EndToken,
    [string]$Label
) {
    $start = $Text.IndexOf($StartToken, [System.StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "Unable to locate $Label start token: $StartToken"
    }
    $end = $Text.IndexOf($EndToken, $start + $StartToken.Length, [System.StringComparison]::Ordinal)
    if ($end -lt 0) {
        throw "Unable to locate $Label end token: $EndToken"
    }
    return $Text.Substring($start, $end - $start)
}

$baselineCommit = (& git -C $repo rev-parse --verify "${BaselineRef}^{commit}").Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($baselineCommit)) {
    throw "Unable to resolve P11 ownership baseline: $BaselineRef"
}

Write-Host "P11 ownership mode: $Mode"
Write-Host "P10 source baseline: $baselineCommit"

$baselineFiles = @(& git -C $repo ls-tree -r --name-only $BaselineRef -- $rdxRel) |
    Where-Object { $_ -match '\.c$' }
$workingFiles = @(Get-ChildItem -Path $rdxRoot -Recurse -File -Filter '*.c' | ForEach-Object {
    $_.FullName.Substring($repo.Length + 1).Replace('\', '/')
})
$allFiles = @(($baselineFiles + $workingFiles) | Sort-Object -Unique)

$metrics = @(
    @{ Name = 'RecordStatus pointer'; Pattern = '\bRecordStatus\s*\*' },
    @{ Name = 'rdx_record_get_status reference'; Pattern = '\brdx_record_get_status\s*\(' },
    @{ Name = 'rdx_record_process reference'; Pattern = '\brdx_record_process\s*\(' },
    @{ Name = 'legacy record field write'; Pattern = '->\s*(?:run|formate|scene|orig_scene|is_switch|switch_orig_scene|noshow|process_state|mode|orig_mode|key_trigger|rerun)\s*(?:\+\+|--|[+\-*/|&^]?=(?!=))' },
    @{ Name = 'rdx_uxfile call/reference'; Pattern = '\brdx_uxfile_[A-Za-z0-9_]*\s*\(' }
)

$recordAdapterAllow = @(
    "$rdxRel/rdx_record.c",
    "$rdxRel/internal/rdx_record_domain.c",
    "$rdxRel/compat/rdx_record_protocol_adapter.c"
)
$uxfileFinalAllow = @(
    "$rdxRel/rdx_record.c",
    "$rdxRel/service/rdx_storage_service.c",
    "$rdxRel/compat/rdx_storage_format_compat.c",
    "$rdxRel/compat/rdx_file_transfer_cleanup_compat.c"
)
$wifiTransferException = "$rdxRel/service/rdx_wifi_service.c"

foreach ($metric in $metrics) {
    Write-Host ""
    Write-Host "=== $($metric.Name) ==="
    $baselineTotal = 0
    $workingTotal = 0

    foreach ($file in $allFiles) {
        $baselineText = Read-Baseline $file
        $workingText = Read-Working $file
        $baselineCount = Count-Pattern $baselineText $metric.Pattern
        $workingCount = Count-Pattern $workingText $metric.Pattern
        $baselineTotal += $baselineCount
        $workingTotal += $workingCount

        if ($baselineCount -eq 0 -and $workingCount -eq 0) {
            continue
        }
        Write-Host ("{0}: baseline={1}, working={2}" -f $file, $baselineCount, $workingCount)

        if ($Mode -eq 'Baseline') {
            if ($workingCount -ne $baselineCount) {
                Add-Failure "$($metric.Name) drifted before P11 migration in $file"
            }
            continue
        }

        if ($Mode -eq 'Progress') {
            $isRecordAdapter = $recordAdapterAllow -contains $file
            $isUxfileAdapter = $uxfileFinalAllow -contains $file
            $isAllowedNewAdapter = if ($metric.Name -eq 'rdx_uxfile call/reference') {
                $isUxfileAdapter
            } else {
                $isRecordAdapter
            }
            if (-not $isAllowedNewAdapter -and $workingCount -gt $baselineCount) {
                Add-Failure "$($metric.Name) increased outside an approved adapter in $file"
            }
            continue
        }

        if ($metric.Name -eq 'rdx_uxfile call/reference') {
            if ($uxfileFinalAllow -contains $file) {
                continue
            }
            if ($file -eq $wifiTransferException) {
                if ($workingCount -gt $baselineCount) {
                    Add-Failure "WiFi TX compatibility exception expanded in $file"
                }
                continue
            }
            if ($workingCount -ne 0) {
                Add-Failure "Final ownership still has $($metric.Name) in $file"
            }
        } elseif (-not ($recordAdapterAllow -contains $file) -and $workingCount -ne 0) {
            Add-Failure "Final ownership still has $($metric.Name) in $file"
        }
    }

    Write-Host ("TOTAL: baseline={0}, working={1}" -f $baselineTotal, $workingTotal)
}

$deviceService = Read-Working "$rdxRel/service/rdx_device_service.c"
$emmcDisabledPattern = 'void\s+rdx_device_service_emmc_poweroff_check\s*\(void\)\s*\{\s*return\s*;\s*\}'
if ([regex]::IsMatch($deviceService, $emmcDisabledPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
    Add-Pass 'eMMC auto-poweroff check remains an immediate-return disabled policy'
} else {
    Add-Failure 'eMMC auto-poweroff check no longer has return as its only executable statement'
}

$storageService = Read-Working "$rdxRel/service/rdx_storage_service.c"
$formatHandler = Get-FunctionSlice `
    $storageService `
    'static void rdx_cmd_handle_sd_format' `
    'static void rdx_storage_on_format_done' `
    'APP format handler'
Assert-OrderedTokens `
    $formatHandler `
    @('sd_format_ack_indicate(0)', 'rdx_storage_service_format_handle()') `
    'APP success ACK-before-format'

$recordService = Read-Working "$rdxRel/service/rdx_record_service.c"
$bleEventHandler = Get-FunctionSlice `
    $recordService `
    'static void rdx_record_on_ble_event' `
    'static void rdx_record_on_time_event' `
    'record BLE event handler'
Assert-OrderedTokens `
    $bleEventHandler `
    @(
        'rdx_protocol_uploadFileInfo_clean()',
        'rdx_uxfile_recordFileData_sendBuf_free()',
        'rdx_protocol_file_sync_busy_timer_stop()',
        'rdx_protocol_send_buffer_reinit()'
    ) `
    'Immediate record-disconnect cleanup'
if ($bleEventHandler.Contains('rdx_uxfile_datFileInfo_sendBuf_free()')) {
    Add-Failure 'Immediate record-disconnect cleanup unexpectedly frees the DAT list buffer'
} else {
    Add-Pass 'Immediate record-disconnect cleanup does not free the DAT list buffer'
}

$delayedCleanup = Get-FunctionSlice `
    $storageService `
    'rdx_err_t rdx_storage_service_cleanup_ble_buffers' `
    'rdx_err_t rdx_storage_service_sdmmc_set_power' `
    'delayed BLE cleanup compatibility wrapper'
Assert-OrderedTokens `
    $delayedCleanup `
    @(
        'rdx_protocol_uploadFileInfo_clean()',
        'rdx_uxfile_recordFileData_sendBuf_free()',
        'rdx_uxfile_datFileInfo_sendBuf_free()',
        'rdx_protocol_file_sync_busy_timer_stop()',
        'rdx_protocol_send_buffer_reinit()'
    ) `
    'Delayed BLE cleanup'

$bleServer = Read-Working "$rdxRel/rdx_ble_server.c"
$delayedBleHandler = Get-FunctionSlice `
    $bleServer `
    'void rdx_ble_server_disconnected_delay_handle' `
    'void rdx_ble_server_disconnected_handle' `
    'delayed BLE disconnect handler'
Assert-OrderedTokens `
    $delayedBleHandler `
    @(
        'if (k->onoff == TRANSFER_BY_WIFI_ON)',
        'return;',
        'rdx_ble_service_cleanup_protocol_state()'
    ) `
    'Delayed BLE cleanup WiFi-active skip'

$delayedCleanupTimerPattern = 'rdx_os_timer_add\s*\(\s*rdx_ble_server_disconnected_delay_handle\s*,\s*NULL\s*,\s*500\s*\)'
if ((Count-Pattern $bleServer $delayedCleanupTimerPattern) -eq 1) {
    Add-Pass 'Delayed BLE cleanup remains scheduled once at 500 ms'
} else {
    Add-Failure 'Delayed BLE cleanup scheduling no longer matches the single 500 ms baseline'
}

$triggerPostPattern = 'rdx_os_task_post_callback2\s*\(\s*"app_core"\s*,\s*rpx_pool_cb\s*,\s*rp_slot\s*,\s*NULL\s*\)'
$triggerPostCount = Count-Pattern $recordService $triggerPostPattern
if ($triggerPostCount -eq 3) {
    Add-Pass 'UPLOAD/DEVICE/SWITCH trigger factor baseline remains NULL/0 at all three posts'
} else {
    Add-Failure "Expected 3 NULL-factor trigger posts, found $triggerPostCount"
}

Write-Host ""
if ($script:Errors.Count -gt 0) {
    Write-Host "P11 ownership checks failed: $($script:Errors.Count) error(s)"
    exit 1
}

Write-Host 'All RDX P11 ownership checks passed.'
exit 0
