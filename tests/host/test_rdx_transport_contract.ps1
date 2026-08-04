#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$ProtocolRoot = 'SDK\apps\common\third_party_profile\rdx_protocol'
$Server = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_server.c"
$ServerHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_server.h"
$Session = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_session.c"
$SessionHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_session.h"
$Keyboard = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keyboard.c"
$HogpConfig = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_config.h"
$ProjectConfig = Read-RepoFile $RepoRoot 'SDK\apps\earphone\include\t2620_project_config.h'
$StackConfig = Read-RepoFile $RepoRoot 'SDK\apps\earphone\log_config\lib_btstack_config.c'

$InitBody = Get-SourceSlice $Server `
    'void rdx_ble_server_init(void)' `
    'void rdx_ble_server_exit(void)'
$EventMatchBody = Get-SourceSlice $Server `
    'static u8 rdx_ble_server_phase0a_event_matches(' `
    'static void rdx_ble_server_phase0a_wrapper_connection_clear('
$ConnectBody = Get-SourceSlice $Server `
    'static void rdx_ble_server_phase0a_link_connected(' `
    'static void rdx_ble_server_phase0a_link_disconnected('
$DisconnectBody = Get-SourceSlice $Server `
    'static void rdx_ble_server_phase0a_link_disconnected(' `
    'static void rdx_ble_server_phase0a_packet_handler('
$AdvRestartBody = Get-SourceSlice $Server `
    'static void rdx_ble_server_phase0a_connect_adv_restart_deferred(' `
    'static void rdx_ble_server_disconnected_idle_policy_resume('
$SendBody = Get-SourceSlice $Server `
    'static int rdx_ble_server_send_internal(' `
    'int rdx_ble_server_send('
$PendingBody = Get-SourceSlice $Server `
    'static void rdx_ble_server_rdx_send_pending_reset(' `
    'static void *rdx_ble_server_phase0a_wrapper_get('
$PacketHandlerBody = Get-SourceSlice $Server `
    'static void rdx_ble_server_phase0a_packet_handler(' `
    'static void rdx_ble_server_sm_event_callback('

$RuntimeText = $Server + $Session + $SessionHeader + $HogpConfig + $ProjectConfig
Assert-Contract 'FIXED_DUAL_LINK_TOPOLOGY' `
    ($RuntimeText -notmatch 'TCFG_RDX_HOGP_DUAL_LINK_ENABLE' -and
     $StackConfig -match '(?s)#if\s*\(THIRD_PARTY_PROTOCOLS_SEL\s*&\s*RDX_EN\).*?config_le_hci_connection_num\s*=\s*2' -and
     $StackConfig -match 'config_le_gatt_server_num\s*=\s*1' -and
     $StackConfig -match 'config_le_gatt_client_num\s*=\s*0') `
    'RDX production must use two Peripheral links over one GATT server without a single-link fallback'

Assert-Contract 'TWO_WRAPPERS_SHARE_ONE_PROFILE' `
    ($InitBody -match 'g_rdx_ble_secondary_hdl\s*=\s*app_ble_hdl_alloc\s*\(' -and
     (Test-TokensInOrder $InitBody @(
        'rdx_ble_server_wrapper_register(',
        'g_rdx_ble_server_info.rdx_ble_server_hdl, tmp_ble_addr);',
        'rdx_ble_server_wrapper_register(g_rdx_ble_secondary_hdl,',
        'rdx_ble_session_transport_init('
     )) -and
     $Server -match '(?s)static void rdx_ble_server_wrapper_register.*?app_ble_set_mac_addr\s*\(\s*hdl\s*,\s*ble_addr\s*\).*?app_ble_profile_set\s*\(\s*hdl\s*,\s*rdx_profile_data\s*\)') `
    'both wrappers must register the same address and composite ATT profile before transport activation'

$ClaimBody = Get-SourceSlice $Session `
    'static rdx_ble_claim_result_t rdx_ble_session_claim(' `
    'rdx_ble_claim_result_t rdx_ble_session_claim_rdx('
$HidAttachBody = Get-SourceSlice $Server `
    'static u8 rdx_ble_server_phase2_hid_attach(' `
    'static int rdx_ble_server_phase2_rdx_write('
$RdxAttachBody = Get-SourceSlice $Server `
    'static u8 rdx_ble_server_phase2_rdx_attach(' `
    'static void rdx_ble_server_phase2_rdx_detach('
Assert-Contract 'REGISTRY_OWNS_COMPOSABLE_CAPABILITIES' `
    ($SessionHeader -match '#define\s+RDX_BLE_LINK_MAX\s+2' -and
     ([regex]::Matches($Session, 'static\s+rdx_ble_link_state_t\s+s_rdx_ble_links\s*\[RDX_BLE_LINK_MAX\]')).Count -eq 1 -and
     $SessionHeader -match 'RDX_BLE_CAPABILITY_RDX_HID' -and
     $ClaimBody -match 'link->capability\s*\|=\s*capability' -and
     $ClaimBody -match 'return\s+RDX_BLE_CLAIM_BUSY' -and
     $HidAttachBody -match 'rdx_ble_session_claim_hid\s*\(\s*link' -and
     $RdxAttachBody -match 'rdx_ble_session_claim_rdx\s*\(\s*link') `
    'one registry must own both slots while allowing one ACL to compose RDX and HID in either order'

Assert-Contract 'EVENTS_AND_DISCONNECT_ARE_SLOT_SCOPED' `
    ($EventMatchBody -match 'rdx_ble_server_phase0a_wrapper_index\s*\(\s*hdl\s*\)' -and
     $EventMatchBody -match 'app_ble_get_hdl_con_handle\s*\(\s*hdl\s*\)' -and
     $EventMatchBody -match 'wrapper_con_handle\s*!=\s*event_con_handle' -and
     $ConnectBody -match 'rdx_ble_session_link_accept\s*\(\s*hdl\s*,\s*con_handle\s*\)' -and
     $DisconnectBody -match 'rdx_ble_session_link_release\s*\(\s*hdl\s*,\s*con_handle\s*\)' -and
     $DisconnectBody -notmatch 'rdx_ble_session_transport_deinit') `
    'fan-out events must match wrapper and handle, and one disconnect must release only its slot'

Assert-Contract 'CCC_MTU_AND_SEND_ARE_OWNER_SCOPED' `
    (($Server + $Keyboard) -notmatch '(?m)^\s*(?!//)[^/\r\n]*\batt_(?:get|set)_ccc_config\s*\(' -and
     $Server -notmatch 'ble_op_att_set_send_mtu\s*\(' -and
     $Server -match 'multi_att_set_ccc_config\s*\(\s*(?:link->con_handle|connection_handle)' -and
     $SessionHeader -match 'u16\s+mtu_size' -and
     $SendBody -match 'rdx_ble_server_rdx_transport_snapshot_capture\s*\(\s*&snapshot\s*\)' -and
     $SendBody -match 'send_hdl\s*=\s*snapshot\.ble_hdl' -and
     $SendBody -match 'rdx_ble_server_rdx_transport_snapshot_is_current\s*\(\s*&snapshot\s*\)' -and
     $SendBody -match 'app_ble_att_send_data\s*\(\s*send_hdl') `
    'CCC, MTU and RDX sends must remain bound to the owning connection snapshot'

$retryArmOrder = Test-TokensInOrder $SendBody @(
    'rdx_ble_server_rdx_transport_snapshot_is_current(&snapshot)',
    'app_ble_att_vaild_len_get(send_hdl) < len',
    'rdx_ble_server_rdx_send_retry_arm(&snapshot)',
    'return -1'
)
$retryWakeOrder = Test-TokensInOrder $PacketHandlerBody @(
    'rdx_ble_server_rdx_send_pending_consume(link)',
    'rdx_protocol_clear_send_confirm_flag()',
    'os_sem_post(&ble_send_data->send_sem)'
)
Assert-Contract 'RDX_CAN_SEND_NOW_RETRY_IS_OWNER_SCOPED' `
    ($PendingBody -match 'g_rdx_ble_send_retry_pending\s*=\s*1' -and
     $PendingBody -match 'rdx_ble_server_rdx_transport_snapshot_is_current\s*\(\s*snapshot\s*\)' -and
     $PendingBody -match 'rdx_ble_session_rdx_token_resolve\s*\(' -and
     $PendingBody -match '!g_rdx_ble_send_pending_count\s*&&\s*!g_rdx_ble_send_retry_pending' -and
     $PendingBody -match 'g_rdx_ble_send_retry_pending\s*=\s*0' -and
     $retryArmOrder -and
     $SendBody -match '(?s)if\s*\(ret\).*?rdx_ble_server_rdx_send_pending_cancel\s*\(\s*&snapshot\s*\).*?rdx_ble_server_rdx_send_retry_arm\s*\(\s*&snapshot\s*\)' -and
     $SendBody -match '(?s)else\s*\{.*?rdx_ble_server_rdx_send_retry_cancel\s*\(\s*&snapshot\s*\)' -and
     $retryWakeOrder) `
    'a transient ATT failure must retain the current owner token so the first readiness event can wake the immutable bulk worker'

$AdvBody = Get-SourceSlice $Server `
    'static u8 rdx_ble_server_fill_adv_data(' `
    'static u8 rdx_ble_server_fill_rsp_data('
$RspBody = Get-SourceSlice $Server `
    'static u8 rdx_ble_server_fill_rsp_data(' `
    'void rdx_ble_server_adv_interval_change_timer_stop('
$AdvBuildBody = Get-SourceSlice $Server `
    'static int rdx_ble_server_adv_enable_on_hdl(' `
    'int rdx_ble_server_adv_enable(' -Last
$advertisingOk = Test-TokensInOrder $AdvBody @(
    'HCI_EIR_DATATYPE_FLAGS',
    'name_type, name_p, name_len'
)
$advertisingOk = $advertisingOk -and
    (Test-TokensInOrder $RspBody @(
        'HCI_EIR_DATATYPE_MANUFACTURER_SPECIFIC_DATA',
        'HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS'
    )) -and
    $AdvBody -match 'const\s+u8\s+flags\[\]\s*=\s*\{\s*0x0A\s*\}' -and
    $AdvBody -match 'name_capacity\s*=\s*ADV_RSP_PACKET_MAX\s*-\s*offset\s*-\s*2' -and
    $RspBody -match 'PRODUCT_CODE' -and
    $RspBody -match 'le_controller_get_mac' -and
    $RspBody -match 'FACTORY_CODE' -and
    $RspBody -match 'RDX_DEVICE_ABILITY' -and
    $RspBody -match 'PRODUCT_TYPE' -and
    $RspBody -match 'RDX_SELF_MARK' -and
    $RspBody -match 'const\s+u8\s+hid_uuid\[\]\s*=\s*\{\s*0x12\s*,\s*0x18\s*\}' -and
    $RspBody -match '#if\s+TCFG_RDX_HOGP_ENABLE' -and
    $AdvBody -notmatch 'APPEARANCE|Appearance|appearance' -and
    $Server -match '\(u16\)\(\*offset\)\s*\+\s*2\s*\+\s*data_len\s*>\s*ADV_RSP_PACKET_MAX' -and
    $AdvBuildBody -match 'rdx_ble_server_fill_rsp_data\s*\(\s*rspData\s*\)' -and
    $Server -notmatch 'rdx_hogp_adv_start|rdx_hogp_adv_stop|rdx_ble_mode_start_hogp_advertising'
Assert-Contract 'UNIFIED_ADVERTISING_PACKET' $advertisingOk `
    'ADV must contain only Flags+Name without Appearance, and Scan Response must contain bounded RDX manufacturer data then HID UUID'

Assert-Contract 'ADVERTISING_RESTART_REVALIDATES_SLOT' `
    ($AdvRestartBody -match 'rdx_ble_server_phase0b_adv_token_is_current\s*\(\s*\)' -and
     $AdvRestartBody -match 'rdx_ble_server_phase0a_connected_count\s*\(\s*\)\s*>=\s*RDX_BLE_PHASE0A_WRAPPER_MAX' -and
     $AdvRestartBody -match 'rdx_ble_server_phase0a_idle_wrapper_get\s*\(\s*\)' -and
     $AdvRestartBody -match 'rdx_ble_server_broadcast_suppressed\s*\(\s*\)') `
    'a delayed advertiser may start only for a current idle slot while capacity remains'

$SmBody = Get-SourceSlice $Server `
    'static void rdx_ble_server_sm_event_callback(' `
    'static void rdx_ble_server_cbk_packet_handler('
Assert-Contract 'FRESH_HID_PAIRING_DOES_NOT_PRECLAIM_OWNER' `
    ($SmBody -match '!rdx_ble_session_get_hid_link\s*\(\s*\)' -and
     $SmBody -match 'rdx_ble_session_link_set_hid_pairing_pending\s*\(\s*link\s*,\s*1\s*\)' -and
     $SmBody -match 'sm_just_works_confirm\s*\(\s*con_handle\s*\)' -and
     $SmBody -notmatch 'rdx_ble_session_claim_hid') `
    'Just Works may confirm a provisional link, but encrypted CCC must claim HID ownership'

Write-Host 'RDX transport contracts passed.'
