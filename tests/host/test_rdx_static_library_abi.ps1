param(
    [string]$BaselineRef = '5d0284f17006bd333de992ed22d5a1c7484c37a0',
    [string]$NmTool = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$libraryRel = 'SDK/apps/common/third_party_profile/rdx_protocol/librdxApp.a'
$protocolRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_protocol.h'
$recordHeaderRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.h'
$recordSourceRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
$appHeaderRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.h'
$appSourceRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c'
$recordServiceHeaderRel = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_record_service.h'
$recordServiceSourceRel = 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_record_service.c'
$uxfileHeaderRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_uxfile.h'
$vmHeaderRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_vm.h'
$bleSourceRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c'
$adcSourceRel = 'SDK/audio/framework/plugs/source/adc_file.c'
$expectedLibrarySha256 = 'c540d70540dc4d61e15d1ca13579cd2342d4ea972ff0a74a1afccc04b1ef4aca'

function Normalize-LineEndings([string]$Text) {
    return $Text.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Read-Working([string]$RelativePath) {
    $path = Join-Path $repo $RelativePath
    return Normalize-LineEndings ([System.IO.File]::ReadAllText($path))
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
    $errorOutput = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Unable to read ${BaselineRef}:$RelativePath`: $errorOutput"
    }
    return Normalize-LineEndings $content
}

function Extract-One([string]$Text, [string]$Pattern, [string]$Label) {
    $match = [regex]::Match($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) {
        throw "FAIL: cannot locate $Label"
    }
    return $match.Value.Trim()
}

function Assert-Equal([string]$Actual, [string]$Expected, [string]$Message) {
    if ((Normalize-LineEndings $Actual).TrimEnd("`n") -cne
        (Normalize-LineEndings $Expected).TrimEnd("`n")) {
        throw "FAIL: $Message"
    }
    Write-Host "PASS: $Message"
}

function Resolve-NmExecutable([string]$Requested) {
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        if (Test-Path -LiteralPath $Requested) {
            return (Resolve-Path -LiteralPath $Requested).Path
        }
        $requestedCommand = Get-Command $Requested -ErrorAction SilentlyContinue
        if ($null -ne $requestedCommand) {
            return $requestedCommand.Source
        }
        throw "Unable to resolve requested nm tool: $Requested"
    }

    $candidates = @()
    if ($env:OS -eq 'Windows_NT') {
        $candidates += 'C:\JL\pi32\bin\llvm-nm.exe'
        $candidates += 'C:\JL\pi32\bin\nm.exe'
    }
    $candidates += 'llvm-nm'
    $candidates += 'nm'

    foreach ($candidate in $candidates) {
        if ([System.IO.Path]::IsPathRooted($candidate) -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            return $command.Source
        }
    }
    throw 'Unable to locate llvm-nm/nm. Pass -NmTool with the JL production symbol tool.'
}

function Assert-NmDependency([string]$NmText, [string]$Symbol) {
    $pattern = '(?m)\bU\s+' + [regex]::Escape($Symbol) + '\s*$'
    if (-not [regex]::IsMatch($NmText, $pattern)) {
        throw "FAIL: librdxApp.a undefined symbol missing from frozen P11 dependency set: $Symbol"
    }
    Write-Host "PASS: librdxApp.a keeps undefined dependency $Symbol"
}

function Assert-NmMemberDependency([string]$NmText, [string]$Member, [string]$Symbol) {
    $pattern = '(?m)' + [regex]::Escape($Member) + '.*\bU\s+' + [regex]::Escape($Symbol) + '\s*$'
    if (-not [regex]::IsMatch($NmText, $pattern)) {
        throw "FAIL: archive member dependency missing: $Member -> $Symbol"
    }
    Write-Host "PASS: archive member dependency $Member -> $Symbol"
}

$baselineCommit = (& git -C $repo rev-parse --verify "${BaselineRef}^{commit}").Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($baselineCommit)) {
    throw "Unable to resolve ABI baseline commit: $BaselineRef"
}
Write-Host "ABI baseline: $baselineCommit"

$workingBlob = (& git -C $repo hash-object $libraryRel).Trim()
$baselineBlob = (& git -C $repo rev-parse "${BaselineRef}:$libraryRel").Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Unable to read librdxApp.a from ABI baseline: $BaselineRef"
}
Assert-Equal $workingBlob $baselineBlob 'librdxApp.a binary blob matches the P8 baseline'

$workingLibrarySha256 = (Get-FileHash -Algorithm SHA256 (Join-Path $repo $libraryRel)).Hash.ToLowerInvariant()
Assert-Equal $workingLibrarySha256 $expectedLibrarySha256 'librdxApp.a SHA256 matches the frozen P8 fingerprint'

$protocol = Read-Working $protocolRel
$protocolBaseline = Read-Baseline $protocolRel
Assert-Equal $protocol $protocolBaseline 'Static-library protocol/callback compatibility header matches the P8 baseline'

$recordHeader = Read-Working $recordHeaderRel
$recordHeaderBaseline = Read-Baseline $recordHeaderRel
foreach ($entry in @(
    @{ Pattern = 'typedef\s+struct\s*\{.*?\}\s*RecordStatus\s*;'; Label = 'RecordStatus layout' },
    @{ Pattern = 'typedef\s+struct\s*\{\s*bool\s+chat_mic_flag.*?\}\s*MicGainPara\s*;'; Label = 'MicGainPara layout' },
    @{ Pattern = 'void\s+rdx_record_process\s*\(void\)\s*;'; Label = 'rdx_record_process signature' },
    @{ Pattern = 'RecordStatus\s*\*\s*rdx_record_get_status\s*\(void\)\s*;'; Label = 'rdx_record_get_status signature' },
    @{ Pattern = 'void\s+rdx_record_clear_marks\s*\(void\)\s*;'; Label = 'rdx_record_clear_marks signature' },
    @{ Pattern = 'u8\s+rdx_record_get_marks\s*\(u32\s*\*out,\s*u8\s+max\)\s*;'; Label = 'rdx_record_get_marks signature' }
)) {
    $working = Extract-One $recordHeader $entry.Pattern $entry.Label
    $baseline = Extract-One $recordHeaderBaseline $entry.Pattern $entry.Label
    Assert-Equal $working $baseline "$($entry.Label) matches the P8 baseline"
}

$recordSource = Read-Working $recordSourceRel
$recordSourceBaseline = Read-Baseline $recordSourceRel
$recordStatusObjectPattern = 'static\s+RecordStatus\s+record_status\s*=\s*\{.*?\}\s*;'
Assert-Equal (Extract-One $recordSource $recordStatusObjectPattern 'record_status object and initializer') `
            (Extract-One $recordSourceBaseline $recordStatusObjectPattern 'P8 record_status object and initializer') `
            'record_status object declaration and initializer match the P8 baseline'

$recordStatusGetterPattern = 'RecordStatus\s*\*\s*rdx_record_get_status\s*\(void\)\s*\{.*?return\s+&record_status\s*;.*?\}'
Assert-Equal (Extract-One $recordSource $recordStatusGetterPattern 'rdx_record_get_status definition') `
            (Extract-One $recordSourceBaseline $recordStatusGetterPattern 'P8 rdx_record_get_status definition') `
            'rdx_record_get_status keeps the stable record_status object identity'

$legacyGainPattern = 'void\s+rdx_record_mic_gain_check\s*\(void\)'
Assert-Equal (Extract-One $recordSource $legacyGainPattern 'rdx_record_mic_gain_check') `
            (Extract-One $recordSourceBaseline $legacyGainPattern 'P8 baseline rdx_record_mic_gain_check') `
            'rdx_record_mic_gain_check keeps its historical return/argument ABI'

foreach ($entry in @(
    @{ Pattern = '(?:^|\n)\s*u8\s+rdx_record_err_reboot_flag_read_from_vm\s*\(void\)'; Label = 'rdx_record_err_reboot_flag_read_from_vm' },
    @{ Pattern = '(?:^|\n)\s*int\s+rdx_record_err_reboot_flag_write_into_vm\s*\(u8\s+err_reboot_flag\)'; Label = 'rdx_record_err_reboot_flag_write_into_vm' }
)) {
    $working = Extract-One $recordSource $entry.Pattern $entry.Label
    $baseline = Extract-One $recordSourceBaseline $entry.Pattern "P8 baseline $($entry.Label)"
    Assert-Equal $working $baseline "$($entry.Label) keeps its P8 ABI"
}

$appHeader = Read-Working $appHeaderRel
$appHeaderBaseline = Read-Baseline $appHeaderRel
foreach ($entry in @(
    @{ Pattern = 'typedef\s+struct\s*\{\s*RecordStatus\s+record_status\s*;\s*\}\s*rdx_tws_sync_record_t\s*;'; Label = 'rdx_tws_sync_record_t layout' },
    @{ Pattern = 'extern\s+RdxWifiInfo\s*\*\s*rdx_app_get_wifi_info\s*\(void\)\s*;'; Label = 'rdx_app_get_wifi_info declaration' }
)) {
    $working = Extract-One $appHeader $entry.Pattern $entry.Label
    $baseline = Extract-One $appHeaderBaseline $entry.Pattern "P8 $($entry.Label)"
    Assert-Equal $working $baseline "$($entry.Label) matches the P8 baseline"
}

$appSource = Read-Working $appSourceRel
$appSourceBaseline = Read-Baseline $appSourceRel
foreach ($entry in @(
    @{ Pattern = '(?:^|\n)\s*RdxWifiInfo\s*\*\s*rdx_app_get_wifi_info\s*\(void\)\s*\{\s*return\s+rdx_wifi_service_get_wifi_info\s*\(\s*\)\s*;\s*\}'; Label = 'rdx_app_get_wifi_info wrapper' },
    @{ Pattern = '(?:^|\n)\s*u8\s+rdx_app_get_record_mode\s*\(void\)\s*\{\s*return\s+rdx_record_service_get_mode\s*\(\s*\)\s*;\s*\}'; Label = 'rdx_app_get_record_mode wrapper' },
    @{ Pattern = '(?:^|\n)\s*void\s+rdx_record_mode_active_check\s*\(bool\s+show\)\s*\{\s*rdx_record_service_mode_active_check\s*\(show\)\s*;\s*\}'; Label = 'rdx_record_mode_active_check wrapper' },
    @{ Pattern = '(?:^|\n)\s*void\s+rdx_app_emmc_poweron\s*\(u8\s+check_en\)\s*\{\s*rdx_device_service_emmc_poweron\s*\(check_en\)\s*;\s*\}'; Label = 'rdx_app_emmc_poweron wrapper' },
    @{ Pattern = '(?:^|\n)\s*void\s+rdx_app_emmc_poweroff_check_timer_stop\s*\(void\)\s*\{\s*rdx_device_service_emmc_poweroff_check_timer_stop\s*\(\s*\)\s*;\s*\}'; Label = 'rdx_app_emmc_poweroff_check_timer_stop wrapper' },
    @{ Pattern = '(?:^|\n)\s*void\s+rdx_app_emmc_poweroff_check\s*\(void\)\s*\{\s*rdx_device_service_emmc_poweroff_check\s*\(\s*\)\s*;\s*\}'; Label = 'rdx_app_emmc_poweroff_check wrapper' }
)) {
    $working = Extract-One $appSource $entry.Pattern $entry.Label
    $baseline = Extract-One $appSourceBaseline $entry.Pattern "P8 $($entry.Label)"
    Assert-Equal $working $baseline "$($entry.Label) matches the P8 baseline"
}

$recordServiceHeader = Read-Working $recordServiceHeaderRel
$recordServiceHeaderBaseline = Read-Baseline $recordServiceHeaderRel
$recordServiceSource = Read-Working $recordServiceSourceRel
$recordServiceSourceBaseline = Read-Baseline $recordServiceSourceRel
$recordServicePublicSignatures = @(
    @{ Pattern = 'void\s+rdx_record_service_init\s*\(void\)'; Label = 'rdx_record_service_init' },
    @{ Pattern = 'void\s+rdx_record_service_exit\s*\(void\)'; Label = 'rdx_record_service_exit' },
    @{ Pattern = 'void\s+rdx_record_service_device_record_handle\s*\(u8\s+scene\)'; Label = 'rdx_record_service_device_record_handle' },
    @{ Pattern = 'void\s+rdx_record_service_switch\s*\(u8\s+orig_scene\)'; Label = 'rdx_record_service_switch' },
    @{ Pattern = 'void\s+rdx_record_service_upload_timer_cb\s*\(void\s*\*\s*priv\)'; Label = 'rdx_record_service_upload_timer_cb' },
    @{ Pattern = 'void\s+rdx_record_service_upload_timer_stop\s*\(void\)'; Label = 'rdx_record_service_upload_timer_stop' },
    @{ Pattern = 'void\s+rdx_record_service_upload_timer_start\s*\(void\)'; Label = 'rdx_record_service_upload_timer_start' },
    @{ Pattern = 'u8\s+rdx_record_service_get_mode\s*\(void\)'; Label = 'rdx_record_service_get_mode' },
    @{ Pattern = 'void\s+rdx_record_service_set_mode\s*\(u8\s+d\)'; Label = 'rdx_record_service_set_mode' },
    @{ Pattern = 'void\s+rdx_record_service_mode_active_check\s*\(bool\s+show\)'; Label = 'rdx_record_service_mode_active_check' },
    @{ Pattern = 'void\s+rdx_record_service_set_mode_online\s*\(void\)'; Label = 'rdx_record_service_set_mode_online' },
    @{ Pattern = 'void\s+rdx_record_service_set_mode_offline\s*\(void\)'; Label = 'rdx_record_service_set_mode_offline' },
    @{ Pattern = 'bool\s+rdx_record_service_is_running\s*\(void\)'; Label = 'rdx_record_service_is_running' },
    @{ Pattern = 'bool\s+rdx_record_service_can_auto_shutdown\s*\(void\)'; Label = 'rdx_record_service_can_auto_shutdown' },
    @{ Pattern = 'rdx_err_t\s+rdx_record_service_handle_ble_disconnected\s*\(void\)'; Label = 'rdx_record_service_handle_ble_disconnected' },
    @{ Pattern = 'rdx_err_t\s+rdx_record_service_sync_state_after_ble_write_ready\s*\(void\)'; Label = 'rdx_record_service_sync_state_after_ble_write_ready' },
    @{ Pattern = 'void\s+rdx_record_service_start\s*\(u8\s+mode\)'; Label = 'rdx_record_service_start' },
    @{ Pattern = 'void\s+rdx_record_service_stop\s*\(void\)'; Label = 'rdx_record_service_stop' },
    @{ Pattern = 'u8\s+rdx_record_service_get_state\s*\(void\)'; Label = 'rdx_record_service_get_state' },
    @{ Pattern = 'u8\s+rdx_record_service_is_active\s*\(void\)'; Label = 'rdx_record_service_is_active' },
    @{ Pattern = 'rdx_err_t\s+rdx_record_service_stop_from_ble\s*\(void\)'; Label = 'rdx_record_service_stop_from_ble' }
)
foreach ($entry in $recordServicePublicSignatures) {
    $declarationPattern = $entry.Pattern + '\s*;'
    Assert-Equal (Extract-One $recordServiceHeader $declarationPattern "$($entry.Label) declaration") `
                (Extract-One $recordServiceHeaderBaseline $declarationPattern "P8 $($entry.Label) declaration") `
                "$($entry.Label) public declaration matches the P8 baseline"
    Assert-Equal (Extract-One $recordServiceSource $entry.Pattern "$($entry.Label) definition") `
                (Extract-One $recordServiceSourceBaseline $entry.Pattern "P8 $($entry.Label) definition") `
                "$($entry.Label) definition signature matches the P8 baseline"
}

$uxfileHeader = Read-Working $uxfileHeaderRel
$uxfileHeaderBaseline = Read-Baseline $uxfileHeaderRel
$reqFileInfoPattern = 'typedef\s+struct\s*\{\s*int\s+ack\s*;.*?\}\s*ReqFileInfo\s*;'
Assert-Equal (Extract-One $uxfileHeader $reqFileInfoPattern 'ReqFileInfo layout') `
            (Extract-One $uxfileHeaderBaseline $reqFileInfoPattern 'P8 ReqFileInfo layout') `
            'ReqFileInfo layout matches the P8 baseline'

$vmHeader = Read-Working $vmHeaderRel
$vmHeaderBaseline = Read-Baseline $vmHeaderRel
foreach ($entry in @(
    @{ Pattern = 'u8\s+rdx_vm_get_bound_status\s*\(void\)\s*;'; Label = 'rdx_vm_get_bound_status' },
    @{ Pattern = 'void\s+rdx_vm_set_bound_status\s*\(u8\s+d,\s*u8\s+show_en\)\s*;'; Label = 'rdx_vm_set_bound_status' },
    @{ Pattern = 'int\s+rdx_vm_write_ep_info_intoVM\s*\(EarphoneInfo\s*\*\s*data\)\s*;'; Label = 'rdx_vm_write_ep_info_intoVM' },
    @{ Pattern = 'void\s+rdx_vm_read_ep_info_fromVM\s*\(void\)\s*;'; Label = 'rdx_vm_read_ep_info_fromVM' }
)) {
    $working = Extract-One $vmHeader $entry.Pattern $entry.Label
    $baseline = Extract-One $vmHeaderBaseline $entry.Pattern "P8 baseline $($entry.Label)"
    Assert-Equal $working $baseline "$($entry.Label) keeps its P8 ABI"
}

$bleSource = Read-Working $bleSourceRel
$bleSourceBaseline = Read-Baseline $bleSourceRel
foreach ($entry in @(
    @{ Pattern = '(?:^|\n)\s*int\s+rdx_ble_server_reset_local_name\s*\(void\)'; Label = 'rdx_ble_server_reset_local_name' },
    @{ Pattern = '(?:^|\n)\s*char\s*\*\s*rdx_ble_server_get_local_name\s*\(void\)'; Label = 'rdx_ble_server_get_local_name' },
    @{ Pattern = '(?:^|\n)\s*int\s+rdx_ble_server_set_local_name\s*\(char\s*\*\s*name,\s*u8\s+len\)'; Label = 'rdx_ble_server_set_local_name' },
    @{ Pattern = '(?:^|\n)\s*int\s+rdx_ble_server_get_ble_mac\s*\(void\s*\*\s*addr\)'; Label = 'rdx_ble_server_get_ble_mac' }
)) {
    $working = Extract-One $bleSource $entry.Pattern $entry.Label
    $baseline = Extract-One $bleSourceBaseline $entry.Pattern "P8 baseline $($entry.Label)"
    Assert-Equal $working $baseline "$($entry.Label) keeps its P8 ABI"
}

$adcSource = Read-Working $adcSourceRel
$adcSourceBaseline = Read-Baseline $adcSourceRel
foreach ($entry in @(
    @{ Pattern = 'void\s+rdx_audio_adc_file_set_gain\s*\(u8\s+mic_index,\s*u8\s+mic_gain\)'; Label = 'rdx_audio_adc_file_set_gain' },
    @{ Pattern = 'u8\s+rdx_audio_adc_file_get_gain\s*\(u8\s+mic_index\)'; Label = 'rdx_audio_adc_file_get_gain' }
)) {
    $working = Extract-One $adcSource $entry.Pattern $entry.Label
    $baseline = Extract-One $adcSourceBaseline $entry.Pattern "P8 baseline $($entry.Label)"
    Assert-Equal $working $baseline "$($entry.Label) keeps its P8 ABI"
}

$nmExecutable = Resolve-NmExecutable $NmTool
$libraryPath = Join-Path $repo $libraryRel
$nmLines = @(& $nmExecutable -A -u $libraryPath 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "nm undefined-symbol scan failed with exit code $LASTEXITCODE using $nmExecutable"
}
$nmText = Normalize-LineEndings ($nmLines -join "`n")
Write-Host "Symbol tool: $nmExecutable"

$p11RequiredUndefinedSymbols = @(
    'rdx_app_emmc_poweroff_check',
    'rdx_app_emmc_poweroff_check_timer_stop',
    'rdx_app_emmc_poweron',
    'rdx_app_get_record_mode',
    'rdx_app_get_wifi_info',
    'rdx_record_clear_marks',
    'rdx_record_err_reboot_flag_write_into_vm',
    'rdx_record_get_marks',
    'rdx_record_get_status',
    'rdx_record_mode_active_check',
    'rdx_record_process',
    'rdx_uxfile_datFileInfo_sendBuf_free',
    'rdx_uxfile_device_sd_mem_check',
    'rdx_uxfile_get_datFileInfo',
    'rdx_uxfile_get_file_data_by_sn',
    'rdx_uxfile_get_operateFile_info',
    'rdx_uxfile_get_wifi_pack_size',
    'rdx_uxfile_raw_read',
    'rdx_uxfile_recordFileData_sendBuf_free',
    'rdx_uxfile_recordFileData_send_finish',
    'rdx_uxfile_sd_format',
    'rdx_uxfile_sd_format_status_check'
)
foreach ($symbol in $p11RequiredUndefinedSymbols) {
    Assert-NmDependency $nmText $symbol
}

foreach ($dependency in @(
    @{ Member = 'rdx_protocol.c.o'; Symbol = 'rdx_record_get_status' },
    @{ Member = 'rdx_protocol.c.o'; Symbol = 'rdx_record_process' },
    @{ Member = 'rdx_protocol.c.o'; Symbol = 'rdx_record_mode_active_check' },
    @{ Member = 'rdx_protocol.c.o'; Symbol = 'rdx_app_get_record_mode' },
    @{ Member = 'rdx_protocol.c.o'; Symbol = 'rdx_app_get_wifi_info' },
    @{ Member = 'rdx_uxfile.c.o'; Symbol = 'rdx_record_get_status' },
    @{ Member = 'rdx_uxfile.c.o'; Symbol = 'rdx_app_get_wifi_info' },
    @{ Member = 'rdx_uxfile.c.o'; Symbol = 'rdx_record_clear_marks' },
    @{ Member = 'rdx_uxfile.c.o'; Symbol = 'rdx_record_get_marks' },
    @{ Member = 'rdx_uxfile.c.o'; Symbol = 'rdx_record_err_reboot_flag_write_into_vm' },
    @{ Member = 'rdx_uxfile.c.o'; Symbol = 'rdx_app_emmc_poweron' },
    @{ Member = 'rdx_uxfile.c.o'; Symbol = 'rdx_app_emmc_poweroff_check_timer_stop' },
    @{ Member = 'xxpUart.c.o'; Symbol = 'rdx_record_mode_active_check' },
    @{ Member = 'xxpUart.c.o'; Symbol = 'rdx_app_get_wifi_info' },
    @{ Member = 'xxpUart.c.o'; Symbol = 'rdx_app_emmc_poweroff_check' },
    @{ Member = 'xxpUart.c.o'; Symbol = 'rdx_app_emmc_poweroff_check_timer_stop' }
)) {
    Assert-NmMemberDependency $nmText $dependency.Member $dependency.Symbol
}

Write-Host 'All RDX static-library ABI checks passed.'
