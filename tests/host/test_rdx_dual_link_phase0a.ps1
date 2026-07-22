#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
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

$ServerPath = Join-Path $ProtocolDir 'rdx_ble_server.c'
$HogpConfigPath = Join-Path $ProtocolDir 'rdx_hogp_config.h'
$ProjectConfigPath = Join-Path $RepoRoot 'SDK/apps/earphone/include/t2620_project_config.h'
$StackConfigPath = Join-Path $RepoRoot 'SDK/apps/earphone/log_config/lib_btstack_config.c'

$ServerText = Get-Content -Raw $ServerPath
$HogpConfigText = Get-Content -Raw $HogpConfigPath
$ProjectConfigText = Get-Content -Raw $ProjectConfigPath
$StackConfigText = Get-Content -Raw $StackConfigPath

$PacketHandler = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_packet_handler' `
    'static void rdx_ble_server_sm_event_callback'
$WriteCallback = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_att_write_callback' `
    'static u8 rdx_ble_server_adv_append_data'
$ReadCallback = Get-SourceSlice $ServerText `
    'static uint16_t rdx_ble_server_att_read_callback' `
    'void rdx_ble_server_gatt_receive_data'
$AdvSelectorMatch = [regex]::Match($ServerText,
    '(?sm)^int\s+rdx_ble_server_adv_enable\s*\(u8\s+enable\)\s*\{.*?^\}')
$AdvSelector = if ($AdvSelectorMatch.Success) {
    $AdvSelectorMatch.Value
} else {
    ''
}
$InitBody = Get-SourceSlice $ServerText `
    'void rdx_ble_server_init(void)' `
    'void rdx_ble_server_exit(void)'
$ExitBody = Get-SourceSlice $ServerText `
    'void rdx_ble_server_exit(void)' `
    'u8 rdx_ble_server_is_stream_tx_ready(void)'
$RegisterBody = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_wrapper_register' `
    'void rdx_ble_server_init(void)'
$ConnectHandler = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_link_connected' `
    'static void rdx_ble_server_phase0a_link_disconnected'
$HogpControlHarness = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_phase0a_hogp_control_write' `
    'static int rdx_ble_server_att_write_callback'

Test-Contract 'PHASE0A_PROJECT_SWITCH_ENABLED' `
    ($ProjectConfigText -match '#define\s+TCFG_RDX_HOGP_DUAL_LINK_ENABLE\s+1' -and
     $HogpConfigText -match '#define\s+TCFG_RDX_HOGP_DUAL_LINK_ENABLE\s+0') `
    'T2620 must enable the POC while shared RDX builds retain an opt-in default'

Test-Contract 'PHASE0A_STACK_CAPACITY' `
    ($StackConfigText -match '(?s)TCFG_RDX_HOGP_DUAL_LINK_ENABLE.*?config_le_hci_connection_num\s*=\s*2' -and
     $StackConfigText -match 'config_le_gatt_server_num\s*=\s*1' -and
     $StackConfigText -match 'config_le_gatt_client_num\s*=\s*0') `
    'Phase 0A requires two ACL links but one GATT Server role and no GATT client'

Test-Contract 'PHASE0A_TWO_WRAPPERS_ALLOCATED' `
    ($InitBody -match 'g_rdx_ble_server_info\.rdx_ble_server_hdl\s*=\s*app_ble_hdl_alloc\s*\(' -and
     $InitBody -match 'g_rdx_ble_secondary_hdl\s*=\s*app_ble_hdl_alloc\s*\(') `
    'the POC must allocate one wrapper per Peripheral link'

Test-Contract 'PHASE0A_SHARED_PROFILE_AND_CALLBACKS' `
    ($RegisterBody -match 'app_ble_profile_set\s*\(\s*hdl\s*,\s*rdx_profile_data\s*\)' -and
     $RegisterBody -match 'app_ble_att_read_callback_register' -and
     $RegisterBody -match 'app_ble_att_write_callback_register' -and
     $RegisterBody -match 'app_ble_hci_event_callback_register' -and
     ([regex]::Matches($InitBody, 'rdx_ble_server_wrapper_register\s*\(')).Count -eq 2) `
    'both wrappers must register the same composite profile and callback set'

Test-Contract 'PHASE0A_SHARED_ADDRESS' `
    ($RegisterBody -match 'app_ble_adv_address_type_set\s*\(\s*hdl\s*,\s*0\s*\)' -and
     $RegisterBody -match 'app_ble_set_mac_addr\s*\(\s*hdl\s*,\s*ble_addr\s*\)' -and
     $InitBody -match 'rdx_ble_server_wrapper_register\s*\(\s*g_rdx_ble_secondary_hdl\s*,\s*tmp_ble_addr\s*\)') `
    'both wrappers must initialize public advertising address type and use the same controller-derived address'

Test-Contract 'PHASE0A_EVENT_ROUTE_FILTER' `
    ($ServerText -match 'rdx_ble_server_phase0a_event_matches' -and
     $ServerText -match 'wrapper_con_handle\s*!=\s*event_con_handle' -and
     $ServerText -match 'wrapper=%u cb_hdl=%p event_con=0x%04x wrapper_con=0x%04x') `
    'event acceptance must log and compare wrapper identity plus connection handle'

Test-Contract 'PHASE0A_PACKET_PATH_IS_OBSERVATION_ONLY' `
    ($PacketHandler -match 'rdx_ble_server_phase0a_link_connected' -and
     $PacketHandler -match 'rdx_ble_server_phase0a_link_disconnected' -and
     $PacketHandler -notmatch 'rdx_protocol_set_ble_sent|os_sem_post|rdx_hogp_on_|rdx_ble_session_on_') `
    'the Phase 0A packet path must not mutate the RDX/HOGP single-link runtime'

Test-Contract 'PHASE0A_CONNECTION_ADV_DEFERRED' `
    ($ConnectHandler -match 'hdl\s*!=\s*g_rdx_ble_advertising_hdl' -and
     $ConnectHandler -match 'rdx_ble_server_phase0a_connect_adv_restart_schedule\s*\(' -and
     $ConnectHandler -notmatch 'rdx_ble_server_adv_enable\s*\(\s*1\s*\)' -and
     $ServerText -match 'Connection Complete fan-out done; advertise on idle wrapper') `
    'Connection Complete must be accepted only by the active advertiser and may not start the idle wrapper inline'

Test-Contract 'PHASE0A_HOGP_ENUMERATION_CONTROL_PLANE' `
    ($HogpControlHarness -match 'HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE' -and
     $HogpControlHarness -match 'multi_att_set_ccc_config\s*\(\s*connection_handle' -and
     $HogpControlHarness -match 'RDX_BLE_PHASE0A_ATT_ERR_INSUFFICIENT_ENCRYPTION' -and
     $HogpControlHarness -notmatch 'rdx_hogp_att_write|rdx_hogp_on_|rdx_ble_session|rdx_protocol' -and
     $WriteCallback -match 'rdx_ble_server_phase0a_hogp_control_write' -and
     $WriteCallback -match 'write rejected att=0x%04x; business runtime detached') `
    'Windows-required HOGP control writes may enumerate per connection while RDX and both business singletons stay detached'

Test-Contract 'PHASE0A_STATIC_READS_DO_NOT_ATTACH' `
    ($ReadCallback -match '(?s)#if\s+TCFG_RDX_HOGP_DUAL_LINK_ENABLE.*?rdx_ble_server_phase0a_event_matches.*?#else' -and
     $ReadCallback -match '(?s)#if\s+!TCFG_RDX_HOGP_DUAL_LINK_ENABLE.*?rdx_ble_server_hogp_attach') `
    'service discovery/static reads may run without claiming either business capability'

Test-Contract 'PHASE0A_SINGLE_IDLE_ADVERTISER' `
    ($AdvSelector -match 'rdx_ble_server_phase0a_idle_wrapper_get' -and
     $AdvSelector -match 'hdl\s*!=\s*target\s*&&\s*app_ble_adv_state_get' -and
     $AdvSelector -match 'rdx_ble_server_adv_enable_on_hdl\s*\(\s*target\s*,\s*1\s*\)') `
    'only one idle wrapper may carry the unified connectable advertisement'

Test-Contract 'PHASE0A_SECOND_ALLOC_FAILURE_ROLLS_BACK' `
    ($InitBody -match '(?s)g_rdx_ble_secondary_hdl\s*==\s*NULL.*?app_ble_hdl_free\s*\(\s*g_rdx_ble_server_info\.rdx_ble_server_hdl\s*\).*?rdx_ble_server_hdl\s*=\s*NULL') `
    'failure to allocate wrapper 1 must release wrapper 0'

Test-Contract 'PHASE0A_EXIT_FREES_BOTH_WRAPPERS' `
    ($ExitBody -match 'app_ble_hdl_free\s*\(\s*g_rdx_ble_secondary_hdl\s*\)' -and
     $ExitBody -match 'app_ble_hdl_free\s*\(\s*g_rdx_ble_server_info\.rdx_ble_server_hdl\s*\)') `
    'server exit must free both wrapper allocations'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 0A static contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 0A static contract checks failed."
exit 1
