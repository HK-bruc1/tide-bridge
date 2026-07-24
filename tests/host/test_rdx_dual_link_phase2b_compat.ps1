#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$SessionText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.c')
$HeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.h')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$RecordText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_record.c')
$LedText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_led_ctrl.c')
$BusinessText = (Get-ChildItem $ProtocolDir -Filter '*.c' |
    Where-Object { $_.Name -ne 'rdx_ble_server.c' } |
    ForEach-Object { Get-Content -Raw $_.FullName }) -join "`n"
$RdxReconnectGate = $SessionText -match `
    '(?s)rdx_ble_claim_result_t\s+rdx_ble_session_claim_rdx\s*\(.*?s_rdx_runtime_state\s*!=\s*RDX_BLE_RUNTIME_READY.*?s_rdx_runtime_consumed_this_boot.*?rdx_ble_session_rebind_peer_check\s*\(\s*link\s*\).*?s_rdx_runtime_state\s*=\s*RDX_BLE_RUNTIME_ACTIVE'
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

$CompatBody = Get-SourceSlice $ServerText `
    'rdx_ble_server_info_t * rdx_ble_server_get_info(void)' `
    '/**************************************************************************'
$ActiveLinkBody = Get-SourceSlice $ServerText `
    'u8 rdx_ble_server_has_active_link(void)' `
    '/**************************************************************************'
$SendBody = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_send_internal(' `
    'int rdx_ble_server_send(u8 *data, u32 len)'
$RdxWriteBody = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_phase2_rdx_write(' `
    '/* Phase 0A still exposes'

Test-Contract 'PHASE2B_COMPAT_ABI_IS_EXPLICIT' `
    ($HeaderText -match 'Binary compatibility only' -and
     $HeaderText -match 'rdx_ble_server_has_active_link' -and
     $HeaderText -match 'rdx_ble_server_is_stream_tx_ready') `
    'the legacy structure getter must be marked ABI-only and narrow queries must be public'

Test-Contract 'PHASE2B_COMPAT_VIEW_USES_RDX_OWNER' `
    ($CompatBody -match 'rdx_ble_server_rdx_transport_snapshot_capture\s*\(' -and
     $CompatBody -match 'rdx_ble_session_rdx_token_resolve\s*\(' -and
     $CompatBody -match 'ble_con_handle\s*=\s*snapshot\.con_handle' -and
     $CompatBody -match 'ble_mtu_size\s*=\s*snapshot\.mtu_size' -and
     $CompatBody -match 'ble_conn\s*=\s*TRUE' -and
     $CompatBody -match 'ble_conn\s*=\s*FALSE' -and
     $CompatBody -notmatch 'rdx_ble_server_hdl\s*=') `
    'the binary compatibility view must resolve the active RDX owner without replacing the device wrapper'

Test-Contract 'PHASE2B_SOURCE_MODULES_DO_NOT_READ_LEGACY_INFO' `
    ($BusinessText -notmatch 'rdx_ble_server_get_info\s*\(' -and
     $BusinessText -notmatch 'rdx_ble_server_info_t\s*\*') `
    'source business modules must not depend on the legacy mutable connection structure'

Test-Contract 'PHASE2B_LED_USES_PHYSICAL_LINK_QUERY' `
    ($ActiveLinkBody -match 'rdx_ble_session_active_count\s*\(' -and
     $LedText -match 'rdx_ble_server_has_active_link\s*\(' -and
     $LedText -notmatch 'rdx_ble_server_get_info\s*\(') `
    'LED state must query physical link presence instead of the RDX compatibility view'

Test-Contract 'PHASE2B_RECORD_USES_RDX_READY_QUERY' `
    (([regex]::Matches($RecordText,
        'rdx_ble_server_is_stream_tx_ready\s*\(').Count -ge 2) -and
     $RecordText -notmatch 'pd->ccc_configured' -and
     $RecordText -notmatch 'pd->stream_tx_ready') `
    'record streaming must use the current RDX owner readiness query'

Test-Contract 'PHASE2B_OWNER_WRAPPER_CAPACITY_IS_AUTHORITATIVE' `
    ($SendBody -match 'send_hdl\s*=\s*snapshot\.ble_hdl' -and
     $SendBody -match 'app_ble_att_vaild_len_get\s*\(\s*send_hdl\s*\)' -and
     $SendBody -match 'rdx_ble_server_rdx_transport_snapshot_is_current\s*\(') `
    'the final enqueue path must recheck capacity and ownership on the selected RDX wrapper'

Test-Contract 'PHASE2B_COMPAT_UNIT_USES_RECONNECT_GATE' `
    ($RdxWriteBody -match 'rdx_ble_server_phase2_rdx_attach\s*\(' -and
     $RdxWriteBody -match 'rdx_ble_server_gatt_receive_data\s*\(' -and
     $RdxWriteBody -match 'rdx_protocol_ota_handle\s*\(' -and
     $RdxReconnectGate) `
    'the compatibility view may expose input only after a READY, same-peer claim enters ACTIVE'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 2B compatibility contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 2B compatibility contract checks failed."
exit 1
