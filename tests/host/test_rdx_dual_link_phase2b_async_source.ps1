#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$AppText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_app.c')
$RecordText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_record.c')
$RecordHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_record.h')
$OtaText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ota.c')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$ServerHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.h')
$RdxWriteStart = $ServerText.IndexOf('static int rdx_ble_server_phase2_rdx_write(')
$RdxWriteEnd = $ServerText.IndexOf('/* Phase 0A still exposes', $RdxWriteStart)
$RdxWriteBody = if ($RdxWriteStart -ge 0 -and $RdxWriteEnd -gt $RdxWriteStart) {
    $ServerText.Substring($RdxWriteStart, $RdxWriteEnd - $RdxWriteStart)
} else { '' }
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

Test-Contract 'PHASE2B_RECORD_TRIGGER_OWNS_TOKEN' `
    ($AppText -match 'rdx_app_record_trigger_request_t' -and
     $AppText -match 'rdx_app_record_trigger_post\s*\(' -and
     $AppText -match 'rdx_ble_session_rdx_token_resolve\s*\(\s*&request->token\s*,\s*1\s*\)') `
    'record trigger indications must carry and resolve the originating RDX token'

Test-Contract 'PHASE2B_RECORD_STATE_QUEUE_OWNS_SESSION' `
    ($RecordText -match 'rdx_record_state_request_t' -and
     $RecordText -match 'rdx_record_online_session_token_capture\s*\(' -and
     $RecordText -match 'rdx_record_state_indicate_if_current\s*\(' -and
     $RecordHeaderText -match 'rdx_record_online_session_token_is_current') `
    'queued record state indications must retain the fixed online-session token'

Test-Contract 'PHASE2B_OTA_SESSION_IS_STICKY' `
    ($OtaText -match 'g_rdx_ota_session_token' -and
     $OtaText -match 'rdx_ota_session_bind_current\s*\(' -and
     $OtaText -match 'rdx_ota_session_is_current\s*\(' -and
     $OtaText -match 'drop stale OTA data') `
    'OTA begin/data processing must reject stale or ownerless sessions'

Test-Contract 'PHASE2B_OTA_OUTPUT_IS_TOKEN_BOUND' `
    ($OtaText -match 'rdx_ble_server_ota_send_for_token\s*\(' -and
     $ServerHeaderText -match 'rdx_ble_server_ota_send_for_token\s*\(' -and
     $ServerText -match 'drop stale token-bound OTA send') `
    'OTA responses must enqueue through the originating token, not current-owner lookup'

Test-Contract 'PHASE2B_NORMAL_OUTPUT_IS_TOKEN_BOUND' `
    ($ServerHeaderText -match 'rdx_ble_server_send_for_token\s*\(' -and
     $ServerText -match 'drop stale token-bound RDX send') `
    'source-controlled asynchronous normal responses must have a token-bound send API'

Test-Contract 'PHASE2B_DELAYED_DISCONNECT_CLEANUP_CANNOT_CLEAR_NEW_OWNER' `
    ($ServerText -match 'skip stale delayed RDX cleanup: new owner active' -and
     $ServerText -match 'rdx_ble_session_get_rdx_link\s*\(\s*\)') `
    'delayed disconnect cleanup must not clear replacement-owner file/bulk state'

Test-Contract 'PHASE2B_ASYNC_SOURCE_INPUT_RUNTIME_GATED' `
    ($RdxWriteBody -match 'rdx_ble_server_phase2_rdx_attach\s*\(' -and
     $RdxWriteBody -match 'rdx_ble_server_gatt_receive_data\s*\(' -and
     $ServerText -match 'one-session-per-boot') `
    'source async input must be exposed only through the non-reusable runtime owner'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 2B source async contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 2B source async contract checks failed."
exit 1
