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

function Assert-ReadOnlyRecordMigration(
    [string]$Text,
    [string]$StartToken,
    [string]$EndToken,
    [string]$Label
) {
    $slice = Get-FunctionSlice $Text $StartToken $EndToken $Label
    $legacyPattern = '\b(?:RecordStatus|rdx_record_get_status|rdx_record_process)\b'
    if ([regex]::IsMatch($slice, $legacyPattern)) {
        Add-Failure "$Label still accesses the legacy record boundary"
    } elseif ((Count-Pattern $slice '\brdx_record_service_is_running\s*\(') -ne 1) {
        Add-Failure "$Label does not use exactly one semantic running query"
    } else {
        Add-Pass "$Label uses only the semantic running query"
    }
}

function Assert-SemanticRecordMigration(
    [string]$Text,
    [string]$StartToken,
    [string]$EndToken,
    [string]$Label,
    [hashtable]$ExpectedQueries
) {
    $slice = Get-FunctionSlice $Text $StartToken $EndToken $Label
    $legacyPattern = '\b(?:RecordStatus|rdx_record_get_status|rdx_record_process)\b'
    if ([regex]::IsMatch($slice, $legacyPattern)) {
        Add-Failure "$Label still accesses the legacy record boundary"
        return
    }

    $actualTotal = Count-Pattern $slice '\brdx_record_service_(?:is_running|get_activity|get_scene|get_path)\s*\('
    $expectedTotal = 0
    foreach ($query in $ExpectedQueries.Keys) {
        $expected = [int]$ExpectedQueries[$query]
        $actual = Count-Pattern $slice ("\b{0}\s*\(" -f [regex]::Escape([string]$query))
        $expectedTotal += $expected
        if ($actual -ne $expected) {
            Add-Failure "$Label query count drifted for ${query}: expected=$expected actual=$actual"
            return
        }
    }
    if ($actualTotal -ne $expectedTotal) {
        Add-Failure "$Label has an unexpected semantic record query: expected=$expectedTotal actual=$actualTotal"
    } else {
        Add-Pass "$Label uses the exact semantic record query contract"
    }
}

function Assert-StopNowMigration(
    [string]$Text,
    [string]$StartToken,
    [string]$EndToken,
    [string]$Reason,
    [string]$Label
) {
    $slice = Get-FunctionSlice $Text $StartToken $EndToken $Label
    if ([regex]::IsMatch($slice, '\b(?:RecordStatus|rdx_record_get_status|rdx_record_process)\b')) {
        Add-Failure "$Label still accesses the legacy record boundary"
    } elseif ((Count-Pattern $slice '\brdx_record_service_stop_now\s*\(') -ne 1 -or
              (Count-Pattern $slice ("\b{0}\b" -f [regex]::Escape($Reason))) -ne 1 -or
              (Count-Pattern $slice '\brdx_record_service_stop_post\s*\(') -ne 0) {
        Add-Failure "$Label does not use the exact synchronous stop reason contract"
    } else {
        Add-Pass "$Label uses the exact synchronous stop reason contract"
    }
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

Write-Host ""
Write-Host '=== P11.1 record query contract ==='
$recordDomain = Read-Working "$rdxRel/internal/rdx_record_domain.c"
$recordQuery = Read-Working "$rdxRel/service/rdx_record_query.c"
$activityQuery = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_get_activity' `
    'rdx_err_t rdx_record_domain_get_scene' `
    'record-domain activity query'
Assert-OrderedTokens $activityQuery `
    @('case RECORD_STATE_STOP:', 'RDX_RECORD_ACTIVITY_IDLE',
      'case RECORD_STATE_PAUSE:', 'RDX_RECORD_ACTIVITY_PAUSED',
      'case RECORD_STATE_START:', 'case RECORD_STATE_RESUME:',
      'RDX_RECORD_ACTIVITY_ACTIVE', 'default:', 'RDX_ERR_INVAL') `
    'record-domain activity mapping'
$sceneQuery = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_get_scene' `
    'rdx_err_t rdx_record_domain_get_path' `
    'record-domain scene query'
Assert-OrderedTokens $sceneQuery `
    @('case RECORD_SCENE_CHAT:', 'RDX_RECORD_SCENE_CHAT',
      'case RECORD_SCENE_CALL:', 'RDX_RECORD_SCENE_CALL',
      'default:', 'RDX_ERR_INVAL') `
    'record-domain scene mapping'
$pathQuery = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_get_path' `
    'bool rdx_record_domain_is_offline_active' `
    'record-domain path query'
Assert-OrderedTokens $pathQuery `
    @('case RECORD_MODE_OFFLINE:', 'RDX_RECORD_PATH_OFFLINE',
      'case RECORD_MODE_ONLINE:', 'RDX_RECORD_PATH_ONLINE',
      'default:', 'RDX_ERR_INVAL') `
    'record-domain path mapping'
$runningQuery = Get-FunctionSlice $recordQuery `
    'bool rdx_record_service_is_running' `
    'bool rdx_record_service_can_auto_shutdown' `
    'record-service running query'
Assert-OrderedTokens $runningQuery `
    @('rdx_record_domain_get_activity(&activity) == RDX_OK',
      'activity == RDX_RECORD_ACTIVITY_ACTIVE') `
    'record-service running fail-closed query'
$shutdownQuery = Get-FunctionSlice $recordQuery `
    'bool rdx_record_service_can_auto_shutdown' `
    'u8 rdx_record_service_is_active' `
    'record-service auto-shutdown query'
Assert-OrderedTokens $shutdownQuery `
    @('rdx_record_domain_get_activity(&activity) == RDX_OK',
      'activity == RDX_RECORD_ACTIVITY_IDLE') `
    'record-service auto-shutdown fail-closed query'
$legacyActive = Get-FunctionSlice $recordQuery `
    'u8 rdx_record_service_is_active' `
    'rdx_err_t rdx_record_service_get_activity' `
    'legacy is-active compatibility query'
if ((Count-Pattern $legacyActive '\breturn\s+0\s*;') -eq 1) {
    Add-Pass 'legacy is-active compatibility query remains fixed at zero'
} else {
    Add-Failure 'legacy is-active compatibility query no longer returns exactly one fixed zero'
}

Write-Host ""
Write-Host '=== P11.2a read-only caller migration ==='
$ledControl = Read-Working "$rdxRel/rdx_led_ctrl.c"
Assert-ReadOnlyRecordMigration $ledControl `
    "static bool _rdx_led_can_show_transfer_effect(void)`n{" `
    "static bool _rdx_led_refresh_transfer_scene(void)`n{" `
    'LED transfer-effect gate'
Assert-ReadOnlyRecordMigration $ledControl `
    "static void _rdx_led_restore_system_state(void)`n{" `
    "static void _rdx_led_engine_solid_timeout(const rdx_led_effect_cfg_t *cfg)`n{" `
    'LED system-state restore'

$chargeControl = Read-Working "$rdxRel/rdx_charge.c"
Assert-ReadOnlyRecordMigration $chargeControl `
    "void rdx_app_incharge_batPercent_show_cb(void* priv)`n{" `
    "void rdx_app_incharge_batPercent_show_stop(void)`n{" `
    'charge OLED battery display gate'
Assert-ReadOnlyRecordMigration $chargeControl `
    "void rdx_app_charge_stop(void)`n{" `
    "void rdx_app_charge_start(void)`n{" `
    'charge-stop OLED restore gate'

$storageControl = Read-Working "$rdxRel/service/rdx_storage_service.c"
Assert-ReadOnlyRecordMigration $storageControl `
    "static void rdx_cmd_handle_sd_format(ProtocolEvents event, void *data, u32 len)`n{" `
    "static void rdx_storage_on_format_done(rdx_event_id_t event, void *payload, u32 len, void *ctx)`n{" `
    'storage format busy gate'

$deviceControl = Read-Working "$rdxRel/service/rdx_device_service.c"
Assert-ReadOnlyRecordMigration $deviceControl `
    "rdx_err_t rdx_device_service_factory_reset(void)`n{" `
    "void rdx_device_service_user_para_reset(void)`n{" `
    'device factory-reset busy gate'
Assert-ReadOnlyRecordMigration $deviceControl `
    "void rdx_device_service_user_para_reset(void)`n{" `
    "static void rdx_cmd_handle_sys_reset(ProtocolEvents event, void *data, u32 len)`n{" `
    'device user-parameter-reset busy gate'
Assert-ReadOnlyRecordMigration $deviceControl `
    "static void rdx_cmd_handle_sys_reset(ProtocolEvents event, void *data, u32 len)`n{" `
    "static void rdx_cmd_handle_bound(ProtocolEvents event, void *data, u32 len)`n{" `
    'device APP reset busy gate'
Assert-ReadOnlyRecordMigration $deviceControl `
    "static void rdx_cmd_handle_unbound(ProtocolEvents event, void *data, u32 len)`n{" `
    "static void rdx_cmd_handle_device_pair(ProtocolEvents event, void *data, u32 len)`n{" `
    'device APP unbound busy gate'

Write-Host ""
Write-Host '=== P11.2b app/DUT read-only caller migration ==='
$appControl = Read-Working "$rdxRel/rdx_app.c"
Assert-SemanticRecordMigration $appControl `
    "void rdx_app_earphone_key_remap(int *value, int *msg)`n{" `
    "int rdx_app_earphone_state_set_page_scan_enable()`n{" `
    'app key-remap recording gate' `
    @{ rdx_record_service_is_running = 1 }
Assert-SemanticRecordMigration $appControl `
    "void rdx_app_single_click_handle(void)`n{" `
    "void rdx_app_double_click_handle(void)`n{" `
    'app single-click recording gate' `
    @{ rdx_record_service_is_running = 1 }
Assert-SemanticRecordMigration $appControl `
    "void rdx_app_triple_click_handle(void)`n{" `
    "void rdx_app_quadruple_click_handle(void)`n{" `
    'app triple-click recording gate' `
    @{ rdx_record_service_is_running = 1 }
Assert-SemanticRecordMigration $appControl `
    "void rdx_app_quadruple_click_handle(void)`n{" `
    "void rdx_app_quintuple_click_handle(void)`n{" `
    'app quadruple-click dead record read removal' `
    @{}
Assert-SemanticRecordMigration $appControl `
    'case APP_MSG_LONG_PRESS_HOLDUP:' `
    'case APP_MSG_RECORD_SWITCH:' `
    'app long-press release scene query' `
    @{ rdx_record_service_get_scene = 1 }
$longPressRelease = Get-FunctionSlice $appControl `
    'case APP_MSG_LONG_PRESS_HOLDUP:' `
    'case APP_MSG_RECORD_SWITCH:' `
    'app long-press release scene fallback'
Assert-OrderedTokens $longPressRelease `
    @(
        'scene = RDX_RECORD_SCENE_CALL',
        'rdx_record_service_get_scene(&scene)',
        'if(scene == RDX_RECORD_SCENE_CHAT)'
    ) `
    'app long-press release CALL fallback'
Assert-SemanticRecordMigration $appControl `
    'case APP_MSG_DUT:' `
    'case APP_MSG_PC_MODE_ON:' `
    'app DUT-entry recording gate' `
    @{ rdx_record_service_is_running = 1 }
Assert-SemanticRecordMigration $appControl `
    'u8 err_boot = rdx_record_err_reboot_flag_read_from_vm();' `
    'rdx_record_err_reboot_flag_write_into_vm(0);' `
    'app abnormal-reboot scene query' `
    @{ rdx_record_service_get_scene = 1 }
$abnormalReboot = Get-FunctionSlice $appControl `
    'u8 err_boot = rdx_record_err_reboot_flag_read_from_vm();' `
    'rdx_record_err_reboot_flag_write_into_vm(0);' `
    'app abnormal-reboot scene fallback'
Assert-OrderedTokens $abnormalReboot `
    @(
        'scene = RDX_RECORD_SCENE_CALL',
        'rdx_record_service_get_scene(&scene)',
        'if(scene == RDX_RECORD_SCENE_CHAT)'
    ) `
    'app abnormal-reboot CALL fallback'

$dutControl = Read-Working "$rdxRel/rdx_dut.c"
Assert-SemanticRecordMigration $dutControl `
    "void rdx_dut_rec_start(void)`n{" `
    "void rdx_dut_rec_stop(void)`n{" `
    'DUT record-start activity and scene query' `
    @{ rdx_record_service_get_activity = 1; rdx_record_service_get_scene = 1 }
$dutStart = Get-FunctionSlice $dutControl `
    "void rdx_dut_rec_start(void)`n{" `
    "void rdx_dut_rec_stop(void)`n{" `
    'DUT record-start fallback contract'
Assert-OrderedTokens $dutStart `
    @(
        'activity = RDX_RECORD_ACTIVITY_PAUSED',
        'scene = RDX_RECORD_SCENE_CHAT',
        'rdx_record_service_get_activity(&activity)',
        'rdx_record_service_get_scene(&scene)',
        'if(activity == RDX_RECORD_ACTIVITY_IDLE)'
    ) `
    'DUT record-start fail-closed activity and CHAT fallback'
Assert-SemanticRecordMigration $dutControl `
    "void rdx_dut_msg_handle(void)`n{" `
    'void rdx_dut_show_refresh(void)' `
    'DUT mode-entry recording gate' `
    @{ rdx_record_service_is_running = 1 }

$otaControl = Read-Working "$rdxRel/rdx_ota.c"
Assert-SemanticRecordMigration $otaControl `
    "void rdx_ota_proc(u16 type, u8 *recv_data, u32 recv_len)`n{" `
    "void rdx_ota_stop(void)`n{" `
    'OTA process dead record read removal' `
    @{}
if ([regex]::IsMatch($otaControl, '\b(?:RecordStatus|rdx_record_get_status)\b')) {
    Add-Failure 'OTA module still accesses the legacy record boundary'
} else {
    Add-Pass 'OTA module has zero legacy record reads'
}

Write-Host ""
Write-Host '=== P11.3a DUT/charge synchronous stop migration ==='
Assert-StopNowMigration $dutControl `
    'void rdx_dut_rec_stop' `
    'bool rdx_dut_rec_is_running' `
    'RDX_RECORD_STOP_DUT' `
    'DUT record stop'
$dutStop = Get-FunctionSlice $dutControl `
    'void rdx_dut_rec_stop' `
    'bool rdx_dut_rec_is_running' `
    'DUT record-stop order'
Assert-OrderedTokens $dutStop `
    @('rdx_record_service_stop_now(RDX_RECORD_STOP_DUT)',
      'rdx_dut_info.current_func = DUT_FUNC_NONE',
      'rdx_dut_show()') `
    'DUT stop/process before local state and LED refresh'

Assert-StopNowMigration $chargeControl `
    'void rdx_app_charge_prepare' `
    'int rdx_app_battery_msg_handler' `
    'RDX_RECORD_STOP_CHARGE_PREPARE' `
    'charge prepare record stop'
$chargePrepare = Get-FunctionSlice $chargeControl `
    'void rdx_app_charge_prepare' `
    'int rdx_app_battery_msg_handler' `
    'charge prepare stop order'
Assert-OrderedTokens $chargePrepare `
    @('rdx_ota_stop()',
      'rdx_record_service_stop_now(RDX_RECORD_STOP_CHARGE_PREPARE)',
      'rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF)') `
    'charge OTA, record and WiFi shutdown order'

$prepareStopDomain = Get-FunctionSlice $recordDomain `
    'static rdx_err_t rdx_record_domain_prepare_stop_internal' `
    'rdx_err_t rdx_record_domain_prepare_stop' `
    'record-domain STOP transition'
Assert-OrderedTokens $prepareStopDomain `
    @('rdx_record_domain_stop_reason_valid(reason)',
      'rdx_record_get_status()',
      '*changed = rp->run != RECORD_STATE_STOP',
      'if (rp->run != RECORD_STATE_STOP)',
      'rp->run = RECORD_STATE_STOP') `
    'record-domain exact STOP transition'
$stopNowDomain = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_stop_now' `
    'rdx_err_t rdx_record_domain_stop_post' `
    'record-domain synchronous stop command'
Assert-OrderedTokens $stopNowDomain `
    @('rdx_record_domain_prepare_stop_internal(reason, &changed)',
      'if (ret == RDX_OK && changed)',
      'rdx_record_process()') `
    'record-domain synchronous stop transition/process'
$stopPostDomain = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_stop_post' `
    "`n}" `
    'record-domain posted stop command'
Assert-OrderedTokens $stopPostDomain `
    @('rdx_record_domain_prepare_stop_internal(reason, &changed)',
      'if (ret != RDX_OK || !changed)',
      'rdx_os_task_post_callback0("app_core", rdx_record_process)',
      'return RDX_ERR_IO') `
    'record-domain posted stop transition/failure retention'

Write-Host ""
Write-Host '=== P11.3b poweroff/idle/APP-message stop migration ==='
Assert-StopNowMigration $deviceControl `
    'void rdx_device_service_soft_poweroff' `
    'void rdx_device_service_reboot' `
    'RDX_RECORD_STOP_POWEROFF' `
    'soft-poweroff record stop'
$softPoweroff = Get-FunctionSlice $deviceControl `
    'void rdx_device_service_soft_poweroff' `
    'void rdx_device_service_reboot' `
    'soft-poweroff stop order'
Assert-OrderedTokens $softPoweroff `
    @('rdx_record_service_stop_now(RDX_RECORD_STOP_POWEROFF)',
      'rdx_wifi_power_off()',
      'rdx_ble_server_app_disconnect()',
      'rdx_ble_server_exit()',
      'rdx_os_timer_add(rdx_device_service_poweroff_cb, NULL, 500)') `
    'soft-poweroff record, WiFi, BLE and delayed poweroff order'

Assert-StopNowMigration $appControl `
    "void rdx_app_enter_idle(void)`n{" `
    "void rdx_app_auto_shutdown(void)`n{" `
    'RDX_RECORD_STOP_IDLE' `
    'idle record stop'
$enterIdle = Get-FunctionSlice $appControl `
    "void rdx_app_enter_idle(void)`n{" `
    "void rdx_app_auto_shutdown(void)`n{" `
    'idle stop order'
Assert-OrderedTokens $enterIdle `
    @('if(app_is_idle == TRUE)',
      'app_is_idle = TRUE',
      'rdx_record_service_stop_now(RDX_RECORD_STOP_IDLE)',
      'rdx_protocol_file_cmd_handle(RDX_APP_FILE_CMD_STOP)',
      'rdx_app_wifi_handle(TRANSFER_BY_WIFI_OFF)',
      'rdx_app_idle_handle(0)') `
    'idle guard, record, file, WiFi and peripheral cleanup order'

$appMessageHandler = Get-FunctionSlice $appControl `
    'int rdx_app_msg_handler' `
    'int rdx_app_key_msg_handler' `
    'APP message handler'
$recordOffStart = $appMessageHandler.IndexOf('case APP_MSG_RECORD_OFF:', [System.StringComparison]::Ordinal)
$recordOffEnd = $appMessageHandler.IndexOf('case APP_MSG_RECORD_CHAT_MODE:', $recordOffStart, [System.StringComparison]::Ordinal)
if ($recordOffStart -lt 0 -or $recordOffEnd -lt 0) {
    Add-Failure 'APP_MSG_RECORD_OFF case cannot be isolated'
} else {
    $recordOff = $appMessageHandler.Substring($recordOffStart, $recordOffEnd - $recordOffStart)
    if ([regex]::IsMatch($recordOff, '\b(?:RecordStatus|rdx_record_get_status|rdx_record_process)\b')) {
        Add-Failure 'APP_MSG_RECORD_OFF still accesses the legacy record boundary'
    } elseif ((Count-Pattern $recordOff '\brdx_record_service_stop_post\s*\(') -ne 1 -or
              (Count-Pattern $recordOff '\bRDX_RECORD_STOP_APP_REQUEST\b') -ne 1 -or
              (Count-Pattern $recordOff '\brdx_record_service_stop_now\s*\(') -ne 0) {
        Add-Failure 'APP_MSG_RECORD_OFF does not use the exact posted stop reason contract'
    } else {
        Add-Pass 'APP_MSG_RECORD_OFF uses the exact posted stop reason contract'
    }
    Assert-OrderedTokens $recordOff `
        @('APP_MSG_RECORD_OFF, con_hdl',
          'rdx_record_service_stop_post(RDX_RECORD_STOP_APP_REQUEST)',
          'record taskq post err',
          'ret = TRUE') `
        'APP_MSG_RECORD_OFF log, post failure log and handled-result order'
}

Write-Host ""
Write-Host '=== P11.3c key/switch/BLE/recovery command migration ==='
$recordServiceHeader = Read-Working "$rdxRel/service/rdx_record_service.h"
$cleanCommandSignatures = @(
    'rdx_err_t\s+rdx_record_service_device_toggle\s*\(rdx_record_scene_t\s+scene\s*\)',
    'rdx_err_t\s+rdx_record_service_switch_scene\s*\(rdx_record_scene_t\s+original_scene\s*\)',
    'rdx_err_t\s+rdx_record_service_set_path\s*\(rdx_record_path_t\s+path\s*\)',
    'rdx_err_t\s+rdx_record_service_mark_key_triggered\s*\(void\s*\)',
    'rdx_err_t\s+rdx_record_service_complete_switch\s*\(bool\s*\*\s*restart\s*,\s*rdx_record_scene_t\s*\*\s*scene\s*\)'
)
foreach ($signature in $cleanCommandSignatures) {
    if ((Count-Pattern $recordServiceHeader $signature) -ne 1) {
        Add-Failure "P11.3c clean command signature missing or duplicated: $signature"
    }
}
if (-not ($script:Errors | Where-Object { $_ -like 'P11.3c clean command signature*' })) {
    Add-Pass 'P11.3c clean command signatures are exact and unique'
}
$recordCommand = Read-Working "$rdxRel/service/rdx_record_command.c"
$keyCommandFacade = Get-FunctionSlice $recordCommand `
    'rdx_err_t rdx_record_service_mark_key_triggered' `
    'rdx_err_t rdx_record_service_complete_switch' `
    'key-trigger command facade'
Assert-OrderedTokens $keyCommandFacade `
    @('rdx_record_domain_mark_key_triggered()') `
    'key-trigger command facade'
$switchCompletionFacade = Get-FunctionSlice $recordCommand `
    'rdx_err_t rdx_record_service_complete_switch' `
    "`n}" `
    'switch-completion command facade'
Assert-OrderedTokens $switchCompletionFacade `
    @('rdx_record_domain_complete_switch(restart, scene)') `
    'switch-completion command facade'

$keyDomain = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_mark_key_triggered' `
    'rdx_err_t rdx_record_domain_complete_switch' `
    'key-trigger domain command'
Assert-OrderedTokens $keyDomain `
    @('rdx_record_get_status()',
      'if (rp->run != RECORD_STATE_STOP)',
      'return RDX_ERR_BUSY',
      'rp->key_trigger = true') `
    'key-trigger exact-idle transition'
$switchCompletionDomain = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_complete_switch' `
    'rdx_err_t rdx_record_domain_handle_ble_disconnected' `
    'switch-completion domain command'
Assert-OrderedTokens $switchCompletionDomain `
    @('*restart = false',
      '*scene = RDX_RECORD_SCENE_CALL',
      'rp->is_switch = false',
      'if (rp->run == RECORD_STATE_STOP)',
      '*restart = true',
      'if (rp->scene == RECORD_SCENE_CHAT)',
      '*scene = RDX_RECORD_SCENE_CHAT') `
    'switch-completion clear, exact-idle and CALL-fallback order'

$switchTimer = Get-FunctionSlice $appControl `
    "void rdx_app_switch_keep_timer_cb(void *priv)`n{" `
    "void rdx_app_switch_keep_timer_restart(void)`n{" `
    'app switch timer callback'
if ([regex]::IsMatch($switchTimer, '\b(?:RecordStatus|rdx_record_get_status|rdx_record_process)\b')) {
    Add-Failure 'app switch timer callback still accesses the legacy record boundary'
} elseif ((Count-Pattern $switchTimer '\brdx_record_service_complete_switch\s*\(') -ne 1) {
    Add-Failure 'app switch timer callback does not complete exactly one semantic switch'
} else {
    Add-Pass 'app switch timer callback uses one semantic switch completion'
}
Assert-OrderedTokens $switchTimer `
    @('rdx_app_switch_keep_timer_stop()',
      'rdx_record_service_complete_switch(&restart, &scene)',
      'if(scene == RDX_RECORD_SCENE_CHAT)',
      'app_send_message(APP_MSG_RECORD_CHAT_MODE, 0)',
      'app_send_message(APP_MSG_RECORD_CALL_MODE, 0)') `
    'switch timer stop, owner completion and restart-message order'

$recordSwitchStart = $appMessageHandler.IndexOf('case APP_MSG_RECORD_SWITCH:', [System.StringComparison]::Ordinal)
$recordSwitchEnd = $appMessageHandler.IndexOf('case APP_MSG_TWS_START_PAIR:', $recordSwitchStart, [System.StringComparison]::Ordinal)
if ($recordSwitchStart -lt 0 -or $recordSwitchEnd -lt 0) {
    Add-Failure 'APP_MSG_RECORD_SWITCH case cannot be isolated'
} else {
    $recordSwitch = $appMessageHandler.Substring($recordSwitchStart, $recordSwitchEnd - $recordSwitchStart)
    if ([regex]::IsMatch($recordSwitch, '\b(?:RecordStatus|rdx_record_get_status|rdx_record_process)\b')) {
        Add-Failure 'APP_MSG_RECORD_SWITCH still accesses the legacy record boundary'
    } elseif ((Count-Pattern $recordSwitch '\brdx_record_service_mark_key_triggered\s*\(') -ne 1 -or
              (Count-Pattern $recordSwitch '\brdx_record_service_get_scene\s*\(') -ne 1) {
        Add-Failure 'APP_MSG_RECORD_SWITCH semantic command/query count drifted'
    } else {
        Add-Pass 'APP_MSG_RECORD_SWITCH uses the exact semantic command/query contract'
    }
    Assert-OrderedTokens $recordSwitch `
        @('rdx_app_emmc_poweron(0)',
          'rdx_record_service_mark_key_triggered()',
          'rdx_hook_motor_start(200)',
          'scene = RDX_RECORD_SCENE_CALL',
          'rdx_record_service_get_scene(&scene)',
          'app_send_message(APP_MSG_RECORD_CHAT_MODE, 0)',
          'app_send_message(APP_MSG_RECORD_CALL_MODE, 0)',
          'key_press_record_ready_flag = 1') `
        'key-trigger mark/motor or fallback-message decision order'
}

$chatToggle = Get-FunctionSlice $appMessageHandler `
    'case APP_MSG_RECORD_CHAT_MODE:' `
    'case APP_MSG_RECORD_CALL_MODE:' `
    'APP chat toggle'
$callToggle = Get-FunctionSlice $appMessageHandler `
    'case APP_MSG_RECORD_CALL_MODE:' `
    'case APP_MSG_LONG_PRESS_HOLDUP:' `
    'APP call toggle'
if ((Count-Pattern $chatToggle '\brdx_record_service_device_toggle\s*\(') -eq 1 -and
    $chatToggle.Contains('RDX_RECORD_SCENE_CHAT') -and
    (Count-Pattern $callToggle '\brdx_record_service_device_toggle\s*\(') -eq 1 -and
    $callToggle.Contains('RDX_RECORD_SCENE_CALL')) {
    Add-Pass 'APP chat/call messages use typed device-toggle scenes'
} else {
    Add-Failure 'APP chat/call typed device-toggle mapping drifted'
}
$dutStartCommand = Get-FunctionSlice $dutControl `
    "void rdx_dut_rec_start(void)`n{" `
    "void rdx_dut_rec_stop(void)`n{" `
    'DUT typed record start'
Assert-OrderedTokens $dutStartCommand `
    @('rdx_record_service_get_activity(&activity)',
      'rdx_record_service_get_scene(&scene)',
      'if(activity == RDX_RECORD_ACTIVITY_IDLE)',
      'rdx_record_service_device_toggle(scene)') `
    'DUT query and typed device-toggle order'
if ($dutControl.Contains('rdx_app_device_record_handle')) {
    Add-Failure 'DUT still depends on the legacy app record-toggle wrapper'
} else {
    Add-Pass 'DUT no longer depends on the legacy app record-toggle wrapper'
}

$recordService = Read-Working "$rdxRel/service/rdx_record_service.c"
$typedDeviceToggle = Get-FunctionSlice $recordService `
    'rdx_err_t rdx_record_service_device_toggle' `
    '/* ---- record mode ---- */' `
    'typed device-toggle facade'
Assert-OrderedTokens $typedDeviceToggle `
    @('case RDX_RECORD_SCENE_CHAT:', 'legacy_scene = RECORD_SCENE_CHAT',
      'case RDX_RECORD_SCENE_CALL:', 'legacy_scene = RECORD_SCENE_CALL',
      'default:', 'return RDX_ERR_INVAL',
      'get_ota_status()', 'return RDX_ERR_BUSY',
      'rdx_record_service_device_record_handle(legacy_scene)') `
    'typed device-toggle validation, gate and legacy mapping'
$typedSwitch = Get-FunctionSlice $recordService `
    'rdx_err_t rdx_record_service_switch_scene' `
    '/* ---- BLE mode helpers ---- */' `
    'typed switch facade'
Assert-OrderedTokens $typedSwitch `
    @('case RDX_RECORD_SCENE_CHAT:', 'legacy_scene = RECORD_SCENE_CHAT',
      'case RDX_RECORD_SCENE_CALL:', 'legacy_scene = RECORD_SCENE_CALL',
      'default:', 'return RDX_ERR_INVAL',
      'rdx_dut_is_in_mode() || get_ota_status()', 'return RDX_ERR_BUSY',
      'rdx_record_service_switch(legacy_scene)') `
    'typed switch validation, gate and legacy mapping'

$pathDomain = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_set_path' `
    'rdx_err_t rdx_record_domain_mark_key_triggered' `
    'record-path domain command'
Assert-OrderedTokens $pathDomain `
    @('case RDX_RECORD_PATH_ONLINE:',
      'rp->mode = RECORD_MODE_ONLINE',
      'if (rp->run == RECORD_STATE_STOP)',
      'rp->orig_mode = RECORD_MODE_ONLINE',
      'case RDX_RECORD_PATH_OFFLINE:',
      'if (rp->orig_mode != RECORD_MODE_OFFLINE)',
      'rp->mode = RECORD_MODE_OFFLINE',
      'rp->orig_mode = RECORD_MODE_OFFLINE') `
    'online/offline legacy path transition order'
$bleDisconnectDomain = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_handle_ble_disconnected' `
    "`n}" `
    'BLE-disconnect domain command'
Assert-OrderedTokens $bleDisconnectDomain `
    @('if (switch_to_offline)',
      'if (rp->orig_mode != RECORD_MODE_OFFLINE)',
      'rp->mode = RECORD_MODE_OFFLINE',
      'rp->orig_mode = RECORD_MODE_OFFLINE',
      'if (rp->orig_mode != RECORD_MODE_OFFLINE',
      'rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME',
      'if (rerun)', 'rp->rerun = true',
      'rp->run = RECORD_STATE_STOP',
      'rdx_record_process()') `
    'BLE disconnect offline-or-rerun-stop transition order'
$bleDisconnectService = Get-FunctionSlice $recordService `
    'rdx_err_t rdx_record_service_handle_ble_disconnected' `
    'rdx_err_t rdx_record_service_sync_state_after_ble_write_ready' `
    'BLE-disconnect service command'
Assert-OrderedTokens $bleDisconnectService `
    @('rdx_record_domain_handle_ble_disconnected(',
      'RDX_RECORD_DISCONNECT_TO_OFFLINE != 0',
      'RDX_RECORD_DISCONNECT_RERUN != 0') `
    'BLE-disconnect product-policy forwarding'
$bleSyncService = Get-FunctionSlice $recordService `
    'rdx_err_t rdx_record_service_sync_state_after_ble_write_ready' `
    'void rdx_record_service_start' `
    'BLE write-ready record sync'
Assert-OrderedTokens $bleSyncService `
    @('rdx_record_domain_get_running(&running)',
      'if (ret != RDX_OK)',
      'if (running)',
      'rdx_protocol_record_state_indicate()',
      'rdx_record_service_mode_active_check(false)') `
    'BLE write-ready query, indicate and mode-sync order'
$bleStopService = Get-FunctionSlice $recordService `
    'rdx_err_t rdx_record_service_stop_from_ble' `
    "`n}" `
    'legacy BLE stop wrapper'
Assert-OrderedTokens $bleStopService `
    @('rdx_record_domain_stop_running_now(',
      'RDX_RECORD_STOP_BLE_DISCONNECT',
      'return ret == RDX_ERR_INVAL ? RDX_OK : ret') `
    'legacy BLE active-only synchronous stop mapping'
$runningStopDomain = Get-FunctionSlice $recordDomain `
    'rdx_err_t rdx_record_domain_stop_running_now' `
    'rdx_err_t rdx_record_domain_set_path' `
    'active-only synchronous stop domain command'
Assert-OrderedTokens $runningStopDomain `
    @('rdx_record_domain_stop_reason_valid(reason)',
      'rdx_record_get_status()',
      'rp->run == RECORD_STATE_START || rp->run == RECORD_STATE_RESUME',
      'rp->run = RECORD_STATE_STOP',
      'rdx_record_process()',
      'return RDX_OK') `
    'active-only stop single-observation transition/process'

if ([regex]::IsMatch($appControl, '\brdx_record_get_status\s*\(|\brdx_record_process\s*\(')) {
    Add-Failure 'app module still calls the legacy record owner/process boundary'
} else {
    Add-Pass 'app module has zero legacy record owner/process calls'
}

$deviceService = $deviceControl
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
