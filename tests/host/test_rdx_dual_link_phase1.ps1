#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ServerPath = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.c'
$SessionPath = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_session.c'
$SessionHeaderPath = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_session.h'
$ServerText = Get-Content -Raw $ServerPath
$SessionText = Get-Content -Raw $SessionPath
$SessionHeaderText = Get-Content -Raw $SessionHeaderPath
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

$DualLinkBuild = Get-SourceSlice $ServerText `
    '#if TCFG_RDX_HOGP_DUAL_LINK_ENABLE' `
    '#else'
$PacketHandler = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_packet_handler' `
    'static void rdx_ble_server_sm_event_callback'
$ControlHarness = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_phase0a_hogp_control_write' `
    'static int rdx_ble_server_att_write_callback'
$DisconnectHandler = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_link_disconnected' `
    'static void rdx_ble_server_phase0a_packet_handler'

Test-Contract 'PHASE1_SINGLE_LINK_REGISTRY' `
    ($SessionText -match 's_rdx_ble_links\[RDX_BLE_LINK_MAX\]' -and
     $SessionHeaderText -match 'RDX_BLE_LINK_MAX\s+2' -and
     $SessionText -match 'rdx_ble_session_transport_init' -and
     $ServerText -notmatch 'g_rdx_ble_phase0b_links') `
    'only rdx_ble_session may own the fixed two-slot link array'

Test-Contract 'PHASE1_WRAPPER_HANDLE_ROUTING' `
    ($ServerText -match 'rdx_ble_session_link_accept\s*\(\s*hdl\s*,\s*con_handle\s*\)' -and
     $ServerText -match 'rdx_ble_session_link_release\s*\(\s*hdl\s*,\s*con_handle\s*\)' -and
     $ServerText -match 'rdx_ble_session_find\s*\(\s*hdl\s*,\s*con_handle\s*\)') `
    'connect, disconnect, and dynamic ATT paths must resolve the same wrapper plus handle slot'

Test-Contract 'PHASE1_PER_LINK_TRANSPORT_STATE' `
    ($SessionHeaderText -match 'void \*ble_hdl;' -and
     $SessionHeaderText -match 'u16 con_handle;' -and
     $SessionHeaderText -match 'u16 mtu_size;' -and
     $SessionHeaderText -match 'u8 encrypted;' -and
     $PacketHandler -match 'rdx_ble_session_link_set_mtu\s*\(\s*link\s*,\s*mtu\s*\)' -and
     $PacketHandler -match 'rdx_ble_session_link_set_encrypted\s*\(\s*link\s*,\s*encrypted\s*\)') `
    'MTU and encryption must be stored only on the matched link slot'

Test-Contract 'PHASE1_ASYNC_TOKEN_EPOCH' `
    ($SessionHeaderText -match 'rdx_ble_async_token_t' -and
     $SessionText -match 's_rdx_ble_transport_epoch' -and
     $SessionText -match 'rdx_ble_session_token_capture' -and
     $SessionText -match 'rdx_ble_session_token_resolve' -and
     $ServerText -match 'rdx_ble_session_token_capture' -and
     $ServerText -match 'rdx_ble_session_token_resolve') `
    'deferred advertising must use slot generation plus transport epoch validation'

Test-Contract 'PHASE1_MULTI_ATT_TRANSPORT' `
    ($PacketHandler -match 'ble_op_multi_att_set_send_mtu\s*\(\s*con_handle\s*,\s*mtu\s*\)' -and
     $ControlHarness -match 'multi_att_set_ccc_config\s*\(\s*connection_handle' -and
     $ServerText -match 'multi_att_clear_ccc_config\s*\(\s*con_handle\s*\)') `
    'dual-link transport must retain connection-scoped MTU and CCC operations'

Test-Contract 'PHASE1_DISCONNECT_ADV_TOKEN' `
    ($DisconnectHandler.IndexOf('rdx_ble_session_link_release(hdl, con_handle)') -ge 0 -and
     $DisconnectHandler.IndexOf('rdx_ble_server_phase0b_adv_token_capture(hdl)') -gt
        $DisconnectHandler.IndexOf('rdx_ble_session_link_release(hdl, con_handle)')) `
    'disconnect advertising must capture the released slot generation after release'

Test-Contract 'PHASE1_CAPABILITY_DETACHED' `
    ($DualLinkBuild -notmatch 'rdx_protocol_packet_recv|rdx_hogp_on_encryption_change|rdx_ble_session_activate_rdx') `
    'Phase 1 must not attach RDX or HOGP business capability'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 1 static contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 1 static contract checks failed."
exit 1
