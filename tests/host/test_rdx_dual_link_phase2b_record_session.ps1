#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$RecordText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_record.c')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$Failed = 0

function Test-Contract {
    param([string]$Name, [bool]$Passed, [string]$Message)
    if ($Passed) {
        Write-Host "PASS: $Name"
        return
    }
    Write-Host "FAIL: ${Name}: $Message"
    $script:Failed++
}

function Get-FunctionBodies {
    param([string]$Text, [string]$FunctionName)
    $pattern = "(?ms)^int\s+$([regex]::Escape($FunctionName))\s*\([^)]*\)\s*\{.*?^\}"
    return [regex]::Matches($Text, $pattern) | ForEach-Object { $_.Value }
}

$RunInitBodies = @(Get-FunctionBodies $RecordText 'rdx_record_run_init')
$DataBodies = @(Get-FunctionBodies $RecordText 'rdx_record_run_data_handle')
$ExitBodies = @(Get-FunctionBodies $RecordText 'rdx_record_run_exit')
$RdxWriteStart = $ServerText.IndexOf('static int rdx_ble_server_phase2_rdx_write(')
$RdxWriteEnd = $ServerText.IndexOf('/* Phase 0A still exposes', $RdxWriteStart)
$RdxWriteBody = if ($RdxWriteStart -ge 0 -and $RdxWriteEnd -gt $RdxWriteStart) {
    $ServerText.Substring($RdxWriteStart, $RdxWriteEnd - $RdxWriteStart)
} else { '' }

Test-Contract 'PHASE2B_RECORD_SESSION_OWNS_FIXED_TOKEN' `
    ($RecordText -match 'g_record_session_token' -and
     $RecordText -match 'g_record_session_token_valid' -and
     $RecordText -match 'rdx_record_online_session_bind\s*\(' -and
     $RecordText -match 'rdx_record_token_equal\s*\(') `
    'an online recording session must retain an immutable RDX owner token'

Test-Contract 'PHASE2B_RECORD_SESSION_REJECTS_OWNER_REPLACEMENT' `
    ($RecordText -match 'rdx_record_online_session_accepts\s*\(\s*token\s*\)' -and
     $RecordText -match 'drop record cmd for another session' -and
     $RecordText -match 'rdx_record_online_session_bind\s*\(\s*token\s*\)\s*;\s*record_status\.run\s*=\s*RECORD_STATE_START') `
    'a reconnecting owner must not control or inherit an older online recording session'

Test-Contract 'PHASE2B_RECORD_MODE_REQUIRES_SESSION_TOKEN' `
    ($RunInitBodies.Count -eq 2 -and
     (@($RunInitBodies | Where-Object {
        $_ -match '!rdx_record_online_session_is_current\s*\(\s*\)' -and
        $_ -match 'rp->mode\s*=\s*RECORD_MODE_OFFLINE'
     }).Count -eq 2)) `
    'both product branches must classify a recording as online only for its fixed current token'

Test-Contract 'PHASE2B_RECORD_AUDIO_DATA_IS_SESSION_GATED' `
    (($DataBodies.Count -eq 2) -and
     (@($DataBodies | Where-Object {
        $_ -match 'rdx_record_online_session_is_current\s*\(\s*\)' -and
        $_ -match 'rdx_protocol_audio_data_indicate\s*\(\s*d\s*,\s*len\s*\)'
     }).Count -eq 2)) `
    'every audio-frame indication must validate the fixed session token immediately before protocol output'

Test-Contract 'PHASE2B_RECORD_STREAM_RESUME_TIMER_OWNS_TOKEN' `
    ($RecordText -match 'g_stream_resume_token\s*=\s*g_record_session_token' -and
     $RecordText -match 'rdx_record_token_equal\s*\(\s*&g_stream_resume_token\s*,\s*&g_record_session_token\s*\)' -and
     $RecordText -match 'drop stale record stream resume') `
    'the delayed stream-resume callback must retain and revalidate the session token captured at scheduling'

Test-Contract 'PHASE2B_RECORD_STOP_RELEASES_SESSION' `
    ($ExitBodies.Count -eq 2 -and
     (@($ExitBodies | Where-Object {
        $_ -match 'rp->run\s*==\s*RECORD_STATE_STOP' -and
        $_ -match 'rdx_record_online_session_clear\s*\(\s*\)'
     }).Count -eq 2)) `
    'both record-exit branches must release the fixed token only after a physical STOP'

Test-Contract 'PHASE2B_RECORD_SESSION_INPUT_REMAINS_FENCED' `
    ($RdxWriteBody -match 'RDX value write fenced' -and
     $RdxWriteBody -notmatch 'rdx_ble_server_gatt_receive_data\s*\(') `
    'record data-plane migration must not expose production RDX input prematurely'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 2B record session contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 2B record session contract checks failed."
exit 1
