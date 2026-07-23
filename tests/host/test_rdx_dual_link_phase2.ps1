#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$SessionText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.c')
$SessionHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.h')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$KeyboardText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_keyboard.c')
$ReadStart = $ServerText.IndexOf('static uint16_t rdx_ble_server_att_read_callback')
$ReadEnd = $ServerText.IndexOf('void rdx_ble_server_gatt_receive_data', $ReadStart)
$ReadCallback = if ($ReadStart -ge 0 -and $ReadEnd -gt $ReadStart) {
    $ServerText.Substring($ReadStart, $ReadEnd - $ReadStart)
} else {
    ''
}
$RdxWriteStart = $ServerText.IndexOf('static int rdx_ble_server_phase2_rdx_write')
$RdxWriteEnd = $ServerText.IndexOf('/* Phase 0A still exposes', $RdxWriteStart)
$RdxWriteBody = if ($RdxWriteStart -ge 0 -and $RdxWriteEnd -gt $RdxWriteStart) {
    $ServerText.Substring($RdxWriteStart, $RdxWriteEnd - $RdxWriteStart)
} else {
    ''
}
$DisconnectStart = $ServerText.IndexOf('static void rdx_ble_server_phase0a_link_disconnected')
$DisconnectEnd = $ServerText.IndexOf('static void rdx_ble_server_phase0a_packet_handler', $DisconnectStart)
$DisconnectBody = if ($DisconnectStart -ge 0 -and $DisconnectEnd -gt $DisconnectStart) {
    $ServerText.Substring($DisconnectStart, $DisconnectEnd - $DisconnectStart)
} else {
    ''
}
$SmStart = $ServerText.IndexOf('static void rdx_ble_server_sm_event_callback')
$SmEnd = $ServerText.IndexOf('static void rdx_ble_server_cbk_packet_handler', $SmStart)
$SmBody = if ($SmStart -ge 0 -and $SmEnd -gt $SmStart) {
    $ServerText.Substring($SmStart, $SmEnd - $SmStart)
} else {
    ''
}
$SendStart = $ServerText.IndexOf('static int rdx_ble_server_send_internal(')
$SendEnd = $ServerText.IndexOf('int rdx_ble_server_send(u8 *data, u32 len)', $SendStart)
$SendBody = if ($SendStart -ge 0 -and $SendEnd -gt $SendStart) {
    $ServerText.Substring($SendStart, $SendEnd - $SendStart)
} else {
    ''
}
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

Test-Contract 'PHASE2_CAPABILITY_MODEL' `
    ($SessionHeaderText -match 'RDX_BLE_CAPABILITY_NONE' -and
     $SessionHeaderText -match 'RDX_BLE_CAPABILITY_RDX' -and
     $SessionHeaderText -match 'RDX_BLE_CAPABILITY_HID' -and
     $SessionHeaderText -match 'RDX_BLE_CLAIM_BUSY' -and
     $SessionHeaderText -match 'RDX_BLE_CLAIM_CONFLICT' -and
     $SessionHeaderText -match 'RDX_BLE_CLAIM_STALE') `
    'the registry must expose explicit capability and internal claim results'

Test-Contract 'PHASE2_SINGLE_OWNER_REGISTRY' `
    ($SessionText -match 's_rdx_rdx_link_index' -and
     $SessionText -match 's_rdx_hid_link_index' -and
     $SessionText -match 'rdx_ble_session_claim_rdx' -and
     $SessionText -match 'rdx_ble_session_claim_hid' -and
     $SessionText -match 'link->capability\s*=\s*capability' -and
     $ServerText -notmatch 'g_rdx_ble_phase0b_links') `
    'only rdx_ble_session may own capability indexes and transitions'

Test-Contract 'PHASE2_STICKY_MUTUAL_EXCLUSION' `
    ($SessionText -match 'link->capability\s*==\s*capability' -and
     $SessionText -match 'link->capability\s*!=\s*RDX_BLE_CAPABILITY_NONE' -and
     $SessionText -match 'return\s+RDX_BLE_CLAIM_CONFLICT' -and
     $SessionText -match 'return\s+RDX_BLE_CLAIM_BUSY' -and
     $SessionText -match 'rdx_ble_session_link_release') `
    'claims must be idempotent, mutually exclusive, and released with the physical link'

Test-Contract 'PHASE2_HID_CLAIM_BOUNDARY' `
    ($ServerText -match 'rdx_ble_server_phase2_hid_attach' -and
     $ServerText -match 'rdx_ble_session_claim_hid' -and
     $ServerText -match 'cfg\s*==\s*0x0001' -and
     $ServerText -match 'RDX_BLE_PHASE0A_ATT_ERR_INSUFFICIENT_ENCRYPTION' -and
     $ServerText -match 'rdx_hogp_on_connected_with_hdl' -and
     $KeyboardText -match 's_hogp_app_ble_hdl\s*=\s*app_ble_hdl') `
    'only encrypted Input CCC enable may claim HID and bind its wrapper; disable remains owner-sticky'

Test-Contract 'PHASE2_RDX_CLAIM_BOUNDARY' `
    ($ServerText -match 'rdx_ble_server_phase2_rdx_write' -and
     $ServerText -match 'rdx_ble_session_claim_rdx' -and
     $RdxWriteBody -match 'RDX value write fenced' -and
     $RdxWriteBody -notmatch 'rdx_ble_server_gatt_receive_data\s*\(' -and
     $RdxWriteBody -notmatch 'rdx_protocol_ota_handle\s*\(' -and
     $ServerText -match 'multi_att_set_ccc_config\s*\(\s*link->con_handle') `
    'CCC enable may claim RDX, but production command and OTA entry must remain fenced until async-token migration'

Test-Contract 'PHASE2_STATIC_READS_OWNER_FREE' `
    ($ReadCallback -match 'rdx_ble_server_phase0a_event_matches' -and
     $ReadCallback -notmatch 'rdx_ble_session_claim_(rdx|hid)|rdx_ble_server_phase2_(rdx|hid)_attach') `
    'dual-link static reads must not claim either capability'

Test-Contract 'PHASE2_DIRECTIONAL_RUNTIME' `
    ($ServerText -match 'rdx_ble_session_link_is_rdx\s*\(\s*link\s*\)' -and
     $ServerText -match 'rdx_ble_session_link_is_hid\s*\(\s*link\s*\)' -and
     $ServerText -match 'rdx_hogp_on_encryption_change' -and
     $ServerText -match 'rdx_ble_server_rdx_send_pending_consume\s*\(\s*link\s*\)') `
    'encryption and send-ready events must be gated by the matching capability owner'

Test-Contract 'PHASE2_HID_PAIRING_CANDIDATE_ROUTED' `
    ($SessionHeaderText -match 'hid_pairing_pending' -and
     $ServerText -match 'rdx_ble_session_link_set_hid_pairing_pending' -and
     $SmBody -match 'SM_EVENT_JUST_WORKS_REQUEST' -and
     $SmBody -match 'rdx_ble_server_phase0a_event_matches' -and
     $SmBody -match 'rdx_ble_session_link_is_hid_pairing_pending' -and
     $SmBody -match 'sm_just_works_confirm\s*\(\s*con_handle\s*\)' -and
     $SmBody -notmatch 'size=%u ignored') `
    'fresh HID pairing must be wrapper/handle routed and confirmed only for the HID owner or pending candidate'

Test-Contract 'PHASE2_CAPABILITY_AWARE_DISCONNECT' `
    ($DisconnectBody -match 'rdx_hogp_on_disconnected\s*\(\s*con_handle\s*\)' -and
     $DisconnectBody -match 'rdx_ble_server_phase2_rdx_detach\s*\(\s*link\s*\)' -and
     $DisconnectBody.IndexOf('rdx_hogp_on_disconnected(con_handle)') -lt
        $DisconnectBody.IndexOf('rdx_ble_session_link_release(hdl, con_handle)') -and
     $DisconnectBody.IndexOf('rdx_ble_server_phase2_rdx_detach(link)') -lt
        $DisconnectBody.IndexOf('rdx_ble_session_link_release(hdl, con_handle)')) `
    'business runtime must be cleaned while capability identity is still available, before slot release'

Test-Contract 'PHASE2_RDX_OWNER_WRAPPER_SEND' `
    ($SendBody -match 'rdx_ble_server_rdx_transport_snapshot_capture\s*\(\s*&snapshot\s*\)' -and
     $SendBody -match 'send_hdl\s*=\s*snapshot.ble_hdl' -and
     $SendBody -match 'rdx_ble_server_rdx_transport_snapshot_is_current\s*\(\s*&snapshot\s*\)' -and
     $SendBody -match 'app_ble_att_vaild_len_get\s*\(\s*send_hdl\s*\)' -and
     $SendBody -match 'app_ble_att_send_data\s*\(\s*send_hdl' -and
     $ServerText -match 'runtime=fenced') `
    'RDX send transport must resolve the current owner wrapper and remain inactive until runtime migration'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 2 capability contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 2 capability contract checks failed."
exit 1
