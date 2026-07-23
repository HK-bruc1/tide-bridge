#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$SessionText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.c')
$SessionHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.h')
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

$TokenCaptureBody = Get-SourceSlice $SessionText `
    'u8 rdx_ble_session_rdx_token_capture(' `
    'rdx_ble_link_state_t *rdx_ble_session_rdx_token_resolve('
$TokenResolveBody = Get-SourceSlice $SessionText `
    'rdx_ble_link_state_t *rdx_ble_session_rdx_token_resolve(' `
    'void rdx_ble_session_link_set_mtu('
$SnapshotBody = Get-SourceSlice $ServerText `
    'static u8 rdx_ble_server_rdx_transport_snapshot_capture(' `
    'static u8 rdx_ble_server_rdx_transport_snapshot_is_current('
$SnapshotCurrentBody = Get-SourceSlice $ServerText `
    'static u8 rdx_ble_server_rdx_transport_snapshot_is_current(' `
    'static void rdx_ble_server_rdx_send_pending_arm('
$PendingConsumeBody = Get-SourceSlice $ServerText `
    'static u8 rdx_ble_server_rdx_send_pending_consume(' `
    'static void *rdx_ble_server_phase0a_wrapper_get('
$PacketHandlerBody = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_packet_handler(' `
    'static void rdx_ble_server_sm_event_callback('
$SendBody = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_send_internal(' `
    'int rdx_ble_server_send(u8 *data, u32 len)'
$OtaSendBody = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_ota_send_internal(' `
    'int rdx_ble_server_ota_send(u8 *data, u32 len)'
$RdxWriteBody = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_phase2_rdx_write(' `
    '/* Phase 0A still exposes'

Test-Contract 'PHASE2B_RDX_OWNER_TOKEN_API' `
    ($SessionHeaderText -match 'rdx_ble_session_rdx_token_capture' -and
     $SessionHeaderText -match 'rdx_ble_session_rdx_token_resolve' -and
     $TokenCaptureBody -match 'rdx_ble_session_get_rdx_link\s*\(' -and
     $TokenCaptureBody -match 'rdx_ble_session_token_capture\s*\(' -and
     $TokenResolveBody -match 'rdx_ble_session_link_token_resolve\s*\(' -and
     $TokenResolveBody -match 'rdx_ble_session_link_is_rdx\s*\(' -and
     $TokenResolveBody -match 'rdx_runtime_active') `
    'RDX async work must capture and resolve the current owner generation and runtime'

Test-Contract 'PHASE2B_TRANSPORT_SNAPSHOT_ROUTED' `
    ($ServerText -match 'rdx_ble_rdx_transport_snapshot_t' -and
     $SnapshotBody -match 'rdx_ble_session_rdx_token_capture\s*\(' -and
     $SnapshotBody -match 'app_ble_get_hdl_con_handle\s*\(' -and
     $SnapshotBody -match 'snapshot->ble_hdl\s*=\s*link->ble_hdl' -and
     $SnapshotBody -match 'snapshot->con_handle\s*=\s*link->con_handle' -and
     $SnapshotBody -match 'snapshot->mtu_size\s*=\s*link->mtu_size' -and
     $SnapshotCurrentBody -match 'rdx_ble_session_rdx_token_resolve\s*\(' -and
     $SnapshotCurrentBody -match 'app_ble_get_hdl_con_handle\s*\(') `
    'a send snapshot must freeze and revalidate token, wrapper, connection handle, and MTU'

Test-Contract 'PHASE2B_SENDS_ARM_PENDING_TOKEN' `
    ($SendBody -match 'rdx_ble_server_rdx_transport_snapshot_capture\s*\(' -and
     $SendBody -match 'rdx_ble_server_rdx_transport_snapshot_is_current\s*\(' -and
     $SendBody -match 'app_ble_att_send_data\s*\(\s*send_hdl' -and
     $SendBody -match 'rdx_ble_server_rdx_send_pending_arm\s*\(\s*&snapshot\s*\)' -and
     $SendBody -match 'rdx_ble_server_rdx_send_pending_cancel\s*\(\s*&snapshot\s*\)' -and
     $SendBody.IndexOf('rdx_ble_server_rdx_send_pending_arm(&snapshot)') -lt
        $SendBody.IndexOf('app_ble_att_send_data(send_hdl') -and
     $OtaSendBody -match 'rdx_ble_server_rdx_transport_snapshot_capture\s*\(' -and
     $OtaSendBody -match 'rdx_ble_server_rdx_transport_snapshot_is_current\s*\(' -and
     $OtaSendBody -match 'app_ble_att_send_data\s*\(\s*send_hdl' -and
     $OtaSendBody -match 'rdx_ble_server_rdx_send_pending_arm\s*\(\s*&snapshot\s*\)' -and
     $OtaSendBody -match 'rdx_ble_server_rdx_send_pending_cancel\s*\(\s*&snapshot\s*\)' -and
     $OtaSendBody.IndexOf('rdx_ble_server_rdx_send_pending_arm(&snapshot)') -lt
        $OtaSendBody.IndexOf('app_ble_att_send_data(send_hdl')) `
    'normal and OTA sends must arm before enqueue and roll back the owner token when enqueue fails'

Test-Contract 'PHASE2B_CAN_SEND_NOW_TOKEN_GATED' `
    ($PacketHandlerBody -match 'ATT_EVENT_CAN_SEND_NOW' -and
     $PacketHandlerBody -match 'rdx_ble_server_phase0b_link_find\s*\(' -and
     $PacketHandlerBody -match 'rdx_ble_session_link_is_rdx\s*\(' -and
     $PacketHandlerBody -match 'rdx_ble_server_rdx_send_pending_consume\s*\(\s*link\s*\)' -and
     $PendingConsumeBody -match 'rdx_ble_session_rdx_token_resolve\s*\(' -and
     $PendingConsumeBody -match 'pending_link\s*!=\s*event_link' -and
     $PacketHandlerBody.IndexOf('rdx_ble_server_rdx_send_pending_consume(link)') -lt
        $PacketHandlerBody.IndexOf('rdx_protocol_clear_send_confirm_flag()') -and
     $PacketHandlerBody.IndexOf('rdx_ble_server_rdx_send_pending_consume(link)') -lt
        $PacketHandlerBody.IndexOf('os_sem_post(&ble_send_data->send_sem)')) `
    'only a matching current RDX pending token may mutate library send state or post its semaphore'

Test-Contract 'PHASE2B_PENDING_TOKEN_LIFECYCLE' `
    ($ServerText -match 'g_rdx_ble_send_pending_count\+\+' -and
     $PendingConsumeBody -match 'g_rdx_ble_send_pending_count--' -and
     $ServerText -match 'rdx_ble_server_rdx_send_pending_reset\s*\(\s*\)' -and
     $ServerText -match 'static void rdx_ble_server_phase2_rdx_detach' -and
     $ServerText -match 'rdx_ble_server_phase2_rdx_detach[\s\S]*?rdx_ble_server_rdx_send_pending_reset\s*\(\s*\)' -and
     $ServerText -match 'rdx_ble_server_init[\s\S]*?rdx_ble_server_rdx_send_pending_reset\s*\(\s*\)' -and
     $ServerText -match 'rdx_ble_server_exit[\s\S]*?rdx_ble_server_rdx_send_pending_reset\s*\(\s*\)') `
    'pending completion state must be invalidated on owner detach and transport lifecycle changes'

Test-Contract 'PHASE2B_PRODUCTION_INPUT_RUNTIME_GATED' `
    ($RdxWriteBody -match 'rdx_ble_server_phase2_rdx_attach\s*\(' -and
     $RdxWriteBody -match 'rdx_ble_server_gatt_receive_data\s*\(' -and
     $RdxWriteBody -match 'rdx_protocol_ota_handle\s*\(' -and
     $ServerText -match 'one-session-per-boot') `
    'production input must enter only the active, non-reusable RDX runtime'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 2B transport contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 2B transport contract checks failed."
exit 1
