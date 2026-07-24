param(
    [string]$BaselineRef = '5d0284f17006bd333de992ed22d5a1c7484c37a0'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$libraryRel = 'SDK/apps/common/third_party_profile/rdx_protocol/librdxApp.a'
$protocolRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_protocol.h'
$recordHeaderRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.h'
$recordSourceRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
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

Write-Host 'All RDX static-library ABI checks passed.'
