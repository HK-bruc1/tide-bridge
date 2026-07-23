#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$AppText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_app.c')
$RecordText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_record.c')
$RecordHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_record.h')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$Failed = 0

function Get-SourceSlice {
    param(
        [string]$Text,
        [string]$StartMarker,
        [string]$EndMarker
    )

    $start = $Text.IndexOf($StartMarker)
    if ($start -lt 0) {
        return ''
    }
    $end = $Text.IndexOf($EndMarker, $start + $StartMarker.Length)
    if ($end -lt 0) {
        return ''
    }
    return $Text.Substring($start, $end - $start)
}

function Test-Contract {
    param([string]$Name, [bool]$Passed, [string]$Message)
    if ($Passed) {
        Write-Host "PASS: $Name"
        return
    }
    Write-Host "FAIL: ${Name}: $Message"
    $script:Failed++
}

$AppCoreBody = Get-SourceSlice $AppText `
    'static void rdx_app_record_cmd_on_app_core(' `
    '/**'
$RecordEventBody = Get-SourceSlice $AppText `
    'case PROTOCOL_EVENT_CMD_RECORD: {' `
    '#if TDX_HAS_FLASHNOTE_ABILITY'
$DelayBody = Get-SourceSlice $RecordText `
    'static void rdx_record_cmd_delay_cb(' `
    '/**************************************************************************
 * function: rdx_record_cmd_handle'
$RecordInternalBody = Get-SourceSlice $RecordText `
    'static void rdx_record_cmd_handle_internal(' `
    'void rdx_record_cmd_handle(Record_info *r_info)'
$RdxWriteBody = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_phase2_rdx_write(' `
    '/* Phase 0A still exposes'

Test-Contract 'PHASE2B_RECORD_ASYNC_REQUEST_OWNS_TOKEN' `
    ($AppText -match 'typedef struct\s*\{\s*Record_info info;\s*rdx_ble_async_token_t token;' -and
     $AppText -match 'rdx_app_record_cmd_request_t' -and
     $RecordHeaderText -match 'rdx_record_cmd_handle_from_rdx' -and
     $RecordHeaderText -match 'const rdx_ble_async_token_t \*token') `
    'the app-core request and record API must retain the RDX owner token'

Test-Contract 'PHASE2B_RECORD_APP_CORE_VALIDATES_BEFORE_EFFECTS' `
    ($AppCoreBody -match 'rdx_ble_session_rdx_token_resolve\s*\(\s*&request->token\s*,\s*1\s*\)' -and
     $AppCoreBody -match 'drop stale app_core record cmd' -and
     $AppCoreBody -match 'rdx_record_cmd_handle_from_rdx\s*\(\s*&info\s*,\s*&request->token\s*\)' -and
     $AppCoreBody.IndexOf('rdx_ble_session_rdx_token_resolve') -lt
        $AppCoreBody.IndexOf('rdx_record_cmd_handle_from_rdx') -and
     $AppCoreBody -match 'free\s*\(\s*request\s*\)') `
    'queued record commands must resolve the owner token before touching playback or recording state'

Test-Contract 'PHASE2B_RECORD_EVENT_CAPTURES_AND_CLEANS_REQUEST' `
    ($RecordEventBody -match 'malloc\s*\(\s*sizeof\(\*request\)\s*\)' -and
     $RecordEventBody -match 'rdx_ble_session_rdx_token_capture\s*\(\s*&request->token\s*,\s*1\s*\)' -and
     $RecordEventBody -match 'msg\[2\]\s*=\s*\(int\)request' -and
     $RecordEventBody -match 'os_taskq_post_type\s*\(\s*"app_core"\s*,\s*Q_CALLBACK' -and
     $RecordEventBody -match 'free\s*\(\s*request\s*\)') `
    'the protocol callback must capture the active RDX owner and free a request rejected by app_core'

Test-Contract 'PHASE2B_RECORD_DELAY_TIMER_TOKEN_GATED' `
    ($RecordText -match 'g_pending_record_token' -and
     $RecordText -match 'g_pending_record_token_valid' -and
     $DelayBody -match 'rdx_record_rdx_token_is_current\s*\(\s*&g_pending_record_token\s*\)' -and
     $DelayBody -match 'drop stale delayed record cmd' -and
     $DelayBody -match 'drop stale ready record cmd' -and
     $DelayBody -match 'rdx_record_cmd_handle_internal\s*\(\s*&g_pending_record_info\s*,\s*token_valid\s*\?\s*&token\s*:\s*NULL\s*\)' -and
     $DelayBody.IndexOf('rdx_record_rdx_token_is_current') -lt
        $DelayBody.IndexOf('rdx_ble_server_is_stream_tx_ready')) `
    'the delayed stream-ready retry must reject stale work before reading current link readiness or executing it'

Test-Contract 'PHASE2B_RECORD_DELAY_REPLACEMENT_IS_STALE_SAFE' `
    ($RecordInternalBody -match 'replace stale delayed record cmd' -and
     $RecordInternalBody -match 'sys_timeout_del\s*\(\s*g_record_cmd_delay_timer\s*\)' -and
     $RecordInternalBody -match 'g_pending_record_token\s*=\s*\*token' -and
     $RecordInternalBody -match 'rdx_record_rdx_token_is_current\s*\(\s*token\s*\)' -and
     $RecordInternalBody.IndexOf('rdx_record_rdx_token_is_current(token)') -lt
        $RecordInternalBody.IndexOf('record_status.run = RECORD_STATE_START')) `
    'a reconnect may replace an obsolete delay, and every tokenized command is validated before record state changes'

Test-Contract 'PHASE2B_RECORD_INPUT_RUNTIME_GATED' `
    ($RdxWriteBody -match 'rdx_ble_server_phase2_rdx_attach\s*\(' -and
     $RdxWriteBody -match 'rdx_ble_server_gatt_receive_data\s*\(' -and
     $RdxWriteBody -match 'rdx_protocol_ota_handle\s*\(' -and
     $ServerText -match 'one-session-per-boot') `
    'record input may run only in the first RDX runtime session of the boot'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 2B record async contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 2B record async contract checks failed."
exit 1
