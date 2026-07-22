#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ServerPath = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c'
$ServerText = Get-Content -Raw $ServerPath
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

function Get-SourceSlice {
    param([string]$Text, [string]$StartToken, [string]$EndToken)
    $start = $Text.IndexOf($StartToken)
    if ($start -lt 0) { return '' }
    $end = $Text.IndexOf($EndToken, $start + $StartToken.Length)
    if ($end -lt 0) { return $Text.Substring($start) }
    return $Text.Substring($start, $end - $start)
}

$Phase0bPath = Get-SourceSlice $ServerText `
    'static void *g_rdx_ble_secondary_hdl' `
    '#endif'
$PacketHandler = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_packet_handler' `
    'static void rdx_ble_server_sm_event_callback'
$ControlHarness = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_phase0a_hogp_control_write' `
    'static int rdx_ble_server_att_write_callback'
$DualLinkBuild = Get-SourceSlice $ServerText `
    '#if TCFG_RDX_HOGP_DUAL_LINK_ENABLE' `
    '#else'

Test-Contract 'PHASE0B_LINK_REGISTRY' `
    ($ServerText -match 'rdx_ble_phase0b_link_t' -and
     $ServerText -match 'g_rdx_ble_phase0b_links\[RDX_BLE_PHASE0A_WRAPPER_MAX\]' -and
     $ServerText -match 'rdx_ble_server_phase0b_link_find') `
    'Phase 0B needs one two-slot transport registry keyed by wrapper and connection handle'

Test-Contract 'PHASE0B_PER_LINK_LIFECYCLE' `
    ($ServerText -match 'link->con_handle\s*=\s*con_handle' -and
     $ServerText -match 'link->connected\s*=\s*1' -and
     $ServerText -match 'link->connected\s*=\s*0' -and
     $ServerText -match 'link->generation\+\+') `
    'connect and disconnect must update only the matched slot and advance its generation'

Test-Contract 'PHASE0B_PER_CONNECTION_MTU' `
    ($PacketHandler -match 'ble_op_multi_att_set_send_mtu\s*\(\s*con_handle\s*,\s*mtu\s*\)' -and
     $PacketHandler -match 'link->mtu\s*=\s*mtu' -and
     $PacketHandler -notmatch 'ble_op_att_set_send_mtu') `
    'the dual-link packet path must configure MTU by connection handle'

Test-Contract 'PHASE0B_PER_CONNECTION_CCC' `
    ($ControlHarness -match 'multi_att_set_ccc_config\s*\(\s*connection_handle' -and
     $ServerText -match 'multi_att_clear_ccc_config\s*\(\s*con_handle\s*\)') `
    'CCC writes and disconnect cleanup must use multi-ATT connection-scoped APIs'

Test-Contract 'PHASE0B_TARGETED_NOTIFY_HARNESS' `
    ($ControlHarness -match 'ATT_CHARACTERISTIC_2A19_01_CLIENT_CONFIGURATION_HANDLE' -and
     $ControlHarness -match 'ble_op_multi_att_send_data\s*\(\s*connection_handle' -and
     $ControlHarness -match 'Battery test notify') `
    'the POC needs a harmless notify routed to the connection that enabled Battery CCC'

$HidCccCase = Get-SourceSlice $ControlHarness `
    'case HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE:' `
    'case HID_PROTOCOL_MODE_VALUE_HANDLE:'

Test-Contract 'PHASE0B_HID_PAIRING_HAS_NO_SIDE_EFFECT' `
    ($HidCccCase -match '(?s)cfg\s*==\s*0x0001\s*&&\s*!encrypted.*?sm_api_request_pairing\s*\(\s*connection_handle\s*\).*?return\s+RDX_BLE_PHASE0A_ATT_ERR_INSUFFICIENT_ENCRYPTION' -and
     $HidCccCase.IndexOf('multi_att_set_ccc_config(connection_handle, att_handle, cfg)') -gt
        $HidCccCase.IndexOf('return RDX_BLE_PHASE0A_ATT_ERR_INSUFFICIENT_ENCRYPTION')) `
    'unencrypted HID CCC enable must request pairing and return 0x0f before writing CCC'

Test-Contract 'PHASE0B_CAN_SEND_NOW_ISOLATED' `
    ($PacketHandler -match 'ATT_EVENT_CAN_SEND_NOW' -and
     $PacketHandler -match 'can_send_now.*ignored' -and
     $PacketHandler -notmatch 'rdx_protocol_set_ble_sent|os_sem_post') `
    'the detached POC must not let either wrapper wake the RDX sender'

Test-Contract 'PHASE0B_STALE_ADV_TOKEN' `
    ($ServerText -match 'rdx_ble_phase0b_adv_token_t' -and
     $ServerText -match 'slot_index|wrapper_index' -and
     $ServerText -match 'generation' -and
     $ServerText -match 'transport_epoch' -and
     ([regex]::Matches($ServerText, 'rdx_ble_server_phase0b_adv_token_is_current\s*\(')).Count -ge 3) `
    'both deferred advertising paths must reject stale slot generation or transport epoch'

Test-Contract 'PHASE0B_BUSINESS_RUNTIME_DETACHED' `
    ($DualLinkBuild -notmatch 'rdx_protocol_packet_recv|rdx_hogp_on_encryption_change|rdx_ble_session_on_connected') `
    'Phase 0B must remain an ATT-isolation POC rather than attach production business singletons'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 0B static contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 0B static contract checks failed."
exit 1
