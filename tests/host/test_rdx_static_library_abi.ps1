$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$libraryRel = 'SDK/apps/common/third_party_profile/rdx_protocol/librdxApp.a'
$protocolRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_protocol.h'
$recordHeaderRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.h'
$recordSourceRel = 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c'
$adcSourceRel = 'SDK/audio/framework/plugs/source/adc_file.c'

function Normalize-LineEndings([string]$Text) {
    return $Text.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Read-Working([string]$RelativePath) {
    $path = Join-Path $repo $RelativePath
    return Normalize-LineEndings ([System.IO.File]::ReadAllText($path))
}

function Read-Head([string]$RelativePath) {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = 'git'
    $startInfo.Arguments = "-C `"$repo`" show `"HEAD:$RelativePath`""
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [System.Text.Encoding]::UTF8

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $content = $process.StandardOutput.ReadToEnd()
    $errorOutput = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Unable to read HEAD:$RelativePath`: $errorOutput"
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

$workingBlob = (& git -C $repo hash-object $libraryRel).Trim()
$headBlob = (& git -C $repo rev-parse "HEAD:$libraryRel").Trim()
Assert-Equal $workingBlob $headBlob 'librdxApp.a binary blob is unchanged'

$protocol = Read-Working $protocolRel
$protocolHead = Read-Head $protocolRel
Assert-Equal $protocol $protocolHead 'Static-library protocol/callback compatibility header is unchanged'

$recordHeader = Read-Working $recordHeaderRel
$recordHeaderHead = Read-Head $recordHeaderRel
foreach ($entry in @(
    @{ Pattern = 'typedef\s+struct\s*\{.*?\}\s*RecordStatus\s*;'; Label = 'RecordStatus layout' },
    @{ Pattern = 'typedef\s+struct\s*\{\s*bool\s+chat_mic_flag.*?\}\s*MicGainPara\s*;'; Label = 'MicGainPara layout' },
    @{ Pattern = 'void\s+rdx_record_process\s*\(void\)\s*;'; Label = 'rdx_record_process signature' },
    @{ Pattern = 'RecordStatus\s*\*\s*rdx_record_get_status\s*\(void\)\s*;'; Label = 'rdx_record_get_status signature' },
    @{ Pattern = 'void\s+rdx_record_clear_marks\s*\(void\)\s*;'; Label = 'rdx_record_clear_marks signature' },
    @{ Pattern = 'u8\s+rdx_record_get_marks\s*\(u32\s*\*out,\s*u8\s+max\)\s*;'; Label = 'rdx_record_get_marks signature' }
)) {
    $working = Extract-One $recordHeader $entry.Pattern $entry.Label
    $head = Extract-One $recordHeaderHead $entry.Pattern $entry.Label
    Assert-Equal $working $head "$($entry.Label) is unchanged"
}

$recordSource = Read-Working $recordSourceRel
$recordSourceHead = Read-Head $recordSourceRel
$legacyGainPattern = 'void\s+rdx_record_mic_gain_check\s*\(void\)'
Assert-Equal (Extract-One $recordSource $legacyGainPattern 'rdx_record_mic_gain_check') `
            (Extract-One $recordSourceHead $legacyGainPattern 'HEAD rdx_record_mic_gain_check') `
            'rdx_record_mic_gain_check keeps its historical return/argument ABI'

$adcSource = Read-Working $adcSourceRel
$adcSourceHead = Read-Head $adcSourceRel
foreach ($entry in @(
    @{ Pattern = 'void\s+rdx_audio_adc_file_set_gain\s*\(u8\s+mic_index,\s*u8\s+mic_gain\)'; Label = 'rdx_audio_adc_file_set_gain' },
    @{ Pattern = 'u8\s+rdx_audio_adc_file_get_gain\s*\(u8\s+mic_index\)'; Label = 'rdx_audio_adc_file_get_gain' }
)) {
    $working = Extract-One $adcSource $entry.Pattern $entry.Label
    $head = Extract-One $adcSourceHead $entry.Pattern "HEAD $($entry.Label)"
    Assert-Equal $working $head "$($entry.Label) keeps its historical ABI"
}

Write-Host 'All RDX static-library ABI checks passed.'
