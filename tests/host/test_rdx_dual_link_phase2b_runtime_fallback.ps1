#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$SessionText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.c')
$SessionHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.h')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$ProtocolHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_protocol.h')
$AppText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_app.c')
$ArchivePath = Join-Path $ProtocolDir 'librdxApp.a'
$ArchiveHash = (Get-FileHash -Algorithm SHA256 $ArchivePath).Hash
$Failed = 0

function Get-SourceSlice {
    param([string]$Text, [string]$StartMarker, [string]$EndMarker)

    $start = $Text.IndexOf($StartMarker)
    if ($start -lt 0) { return '' }
    $end = $Text.IndexOf($EndMarker, $start + $StartMarker.Length)
    if ($end -lt 0) { return '' }
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

$TransportInitBody = Get-SourceSlice $SessionText `
    'void rdx_ble_session_transport_init(' `
    'void rdx_ble_session_transport_deinit('
$ClaimBody = Get-SourceSlice $SessionText `
    'rdx_ble_claim_result_t rdx_ble_session_claim_rdx(' `
    'rdx_ble_claim_result_t rdx_ble_session_claim_hid('
$QuiesceBody = Get-SourceSlice $SessionText `
    'u8 rdx_ble_session_rdx_runtime_begin_quiesce(' `
    'void rdx_ble_session_rdx_runtime_fail_closed('
$FailClosedBody = Get-SourceSlice $SessionText `
    'void rdx_ble_session_rdx_runtime_fail_closed(' `
    'void rdx_ble_session_link_set_mtu('
$AttachBody = Get-SourceSlice $ServerText `
    'static u8 rdx_ble_server_phase2_rdx_attach(' `
    'static void rdx_ble_server_phase2_rdx_detach('
$DetachStart = $ServerText.LastIndexOf(
    'static void rdx_ble_server_phase2_rdx_detach(')
$DetachEnd = if ($DetachStart -ge 0) {
    $ServerText.IndexOf('#if TCFG_RDX_HOGP_ENABLE', $DetachStart)
} else { -1 }
$DetachBody = if ($DetachEnd -gt $DetachStart) {
    $ServerText.Substring($DetachStart, $DetachEnd - $DetachStart)
} else { '' }
$WriteBody = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_phase2_rdx_write(' `
    '/* Phase 0A still exposes'
$DisconnectBody = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_link_disconnected(' `
    'static void rdx_ble_server_phase0a_packet_handler('
$SyncReadyBody = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_stream_tx_ready_cb(' `
    'static u8 rdx_ble_server_phase2_claim_to_att_error('

Test-Contract 'RDX_STATIC_LIBRARY_PINNED' `
    ($ArchiveHash -eq 'C540D70540DC4D61E15D1CA13579CD2342D4EA972FF0A74A1AFCCC04B1EF4ACA') `
    'librdxApp.a must remain byte-for-byte unchanged'

Test-Contract 'RDX_STATIC_LIBRARY_ABI_PINNED' `
    ($ProtocolHeaderText -match '(?s)typedef\s+struct\s*\{\s*int\s+app_select;\s*int\s+device_select;\s*char\s*\*fw_version;\s*char\s*\*hw_version;\s*void\s*\(\*rdx_protocol_cb\)\(ProtocolEvents event, void\* data, u32 len\);\s*\}\s*RdxProtocolCallbacks;' -and
     $AppText -match 'void\s+rdx_app_custom_command_parse\s*\(\s*char\s*\*\s*cmd\s*,\s*char\s*\*\s*value\s*\)') `
    'legacy callback structure and custom-command signature must not change'

Test-Contract 'RDX_RUNTIME_STATE_MODEL' `
    ($SessionHeaderText -match 'RDX_BLE_RUNTIME_OFF' -and
     $SessionHeaderText -match 'RDX_BLE_RUNTIME_READY' -and
     $SessionHeaderText -match 'RDX_BLE_RUNTIME_ACTIVE' -and
     $SessionHeaderText -match 'RDX_BLE_RUNTIME_QUIESCING' -and
     $SessionHeaderText -match 'RDX_BLE_RUNTIME_FAILED') `
    'the immutable singleton must have an explicit fail-closed lifecycle'

Test-Contract 'RDX_ONE_SESSION_PER_BOOT' `
    ($SessionText -match 'static\s+u8\s+s_rdx_runtime_consumed_this_boot' -and
     $TransportInitBody -notmatch 's_rdx_runtime_consumed_this_boot\s*=\s*0' -and
     $TransportInitBody -match 's_rdx_runtime_consumed_this_boot\s*\?' -and
     $TransportInitBody -match 'RDX_BLE_RUNTIME_FAILED' -and
     $ClaimBody -match 's_rdx_runtime_state\s*!=\s*RDX_BLE_RUNTIME_READY' -and
     $ClaimBody -match 's_rdx_runtime_consumed_this_boot\s*=\s*1' -and
     $ClaimBody -match 's_rdx_runtime_state\s*=\s*RDX_BLE_RUNTIME_ACTIVE') `
    'server reinit must not create a second static-library session during the same boot'

Test-Contract 'RDX_ACTIVATION_IS_ACCESS_DRIVEN' `
    ($AttachBody -match 'rdx_ble_session_claim_rdx\s*\(' -and
     $AttachBody -match 'rdx_ble_server_rdx_connected_handle\s*\(' -and
     $WriteBody -match '(?s)rdx_ble_server_phase2_rdx_attach\s*\(\s*link\s*\).*?rdx_ble_server_gatt_receive_data\s*\(' -and
     $WriteBody -match '(?s)rdx_ble_server_phase2_rdx_attach\s*\(\s*link\s*\).*?rdx_protocol_ota_handle\s*\(') `
    'command and OTA input must activate the selected RDX owner before entering the library'

Test-Contract 'RDX_DISCONNECT_FAILS_CLOSED' `
    ($QuiesceBody -match 'RDX_BLE_RUNTIME_ACTIVE' -and
     $QuiesceBody -match 's_rdx_runtime_state\s*=\s*RDX_BLE_RUNTIME_QUIESCING' -and
     $QuiesceBody -match 'link->rdx_runtime_active\s*=\s*0' -and
     $QuiesceBody -match 'rdx_ble_session_runtime_epoch_advance\s*\(' -and
     $FailClosedBody -match 's_rdx_runtime_state\s*=\s*RDX_BLE_RUNTIME_FAILED' -and
     $DetachBody.IndexOf('rdx_ble_session_rdx_runtime_begin_quiesce(link)') -lt
        $DetachBody.IndexOf('rdx_ble_server_rdx_disconnected_cleanup_internal()') -and
     $DetachBody.IndexOf('rdx_ble_server_rdx_disconnected_cleanup_internal()') -lt
        $DetachBody.IndexOf('rdx_ble_session_rdx_runtime_fail_closed()')) `
    'disconnect must invalidate old tokens before cleanup and permanently reject replacement owners'

Test-Contract 'RDX_NO_UNSAFE_LIBRARY_REBUILD' `
    (($DetachBody + $DisconnectBody) -notmatch 'rdx_protocol_task_(free|create)|rdx_uxfile_task_free|rdx_uxfile_init' -and
     $ServerText -notmatch 'RDX_BLE_RUNTIME_RESETTING\s*;') `
    'the audited incomplete free/create APIs must not be used as a runtime barrier'

Test-Contract 'HID_LIFECYCLE_REMAINS_INDEPENDENT' `
    ($DisconnectBody -match '(?s)rdx_ble_session_link_is_hid\s*\(\s*link\s*\).*?rdx_hogp_on_disconnected\s*\(' -and
     $DisconnectBody -match '(?s)rdx_ble_session_link_is_rdx\s*\(\s*link\s*\).*?rdx_ble_server_phase2_rdx_detach\s*\(' -and
     $DetachBody -notmatch 'rdx_hogp_(on_disconnected|deinit)') `
    'fencing RDX must not tear down the HOGP owner on the other link'

Test-Contract 'RDX_SYNC_TIMERS_ARE_TOKEN_GATED' `
    ($ServerText -match 'g_syn_data_token_valid' -and
     $WriteBody -match 'rdx_ble_session_rdx_token_capture\s*\(\s*&g_syn_data_token\s*,\s*1\s*\)' -and
     ([regex]::Matches($SyncReadyBody,
        'rdx_ble_session_rdx_token_resolve\s*\(\s*&g_syn_data_token\s*,\s*1\s*\)')).Count -ge 2 -and
     $DetachBody -match 'rdx_ble_server_syn_data_timers_cancel\s*\(') `
    'sync-data and ready timers must not survive RDX owner invalidation'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX Phase 2B immutable-runtime fallback contracts passed.'
    exit 0
}

Write-Host "$Failed RDX Phase 2B immutable-runtime fallback contract check(s) failed."
exit 1
