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

function Get-FunctionBody {
    param([string]$Text, [string]$Signature)
    $match = [regex]::Match($Text, "(?sm)$Signature\s*\{(.*?)^\}")
    if ($match.Success) { return $match.Groups[1].Value }
    return ''
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
$ServerHeaderPath = Join-Path $ProtocolDir 'rdx_ble_server.h'
$SessionPath = Join-Path $ProtocolDir 'rdx_ble_session.c'
$SessionHeaderPath = Join-Path $ProtocolDir 'rdx_ble_session.h'
$KeyboardPath = Join-Path $ProtocolDir 'rdx_hogp_keyboard.c'
$KeyboardHeaderPath = Join-Path $ProtocolDir 'rdx_hogp_keyboard.h'
$SubscriptionStorePath = Join-Path $ProtocolDir 'rdx_hogp_subscription_store.c'
$KeymapPath = Join-Path $ProtocolDir 'rdx_hogp_keymap_config.c'
$AppPath = Join-Path $ProtocolDir 'rdx_app.c'
$DutPath = Join-Path $ProtocolDir 'rdx_dut.c'
$HogpConfigPath = Join-Path $ProtocolDir 'rdx_hogp_config.h'
$ProjectConfigPath = Join-Path $RepoRoot 'SDK/apps/earphone/include/t2620_project_config.h'
$MakefilePath = Join-Path $RepoRoot 'SDK/Makefile'
$ModeCPath = Join-Path $ProtocolDir 'rdx_ble_mode_controller.c'
$ModeHPath = Join-Path $ProtocolDir 'rdx_ble_mode_controller.h'

$ServerText = Get-Content -Raw $ServerPath
$ServerHeaderText = Get-Content -Raw $ServerHeaderPath
$SessionText = Get-Content -Raw $SessionPath
$SessionHeaderText = Get-Content -Raw $SessionHeaderPath
$KeyboardText = Get-Content -Raw $KeyboardPath
$KeyboardHeaderText = Get-Content -Raw $KeyboardHeaderPath
$SubscriptionStoreText = Get-Content -Raw $SubscriptionStorePath
$KeymapText = Get-Content -Raw $KeymapPath
$AppText = Get-Content -Raw $AppPath
$DutText = Get-Content -Raw $DutPath
$HogpConfigText = Get-Content -Raw $HogpConfigPath
$ProjectConfigText = Get-Content -Raw $ProjectConfigPath
$MakefileText = Get-Content -Raw $MakefilePath

$RuntimeText = $ServerText + "`n" + $ServerHeaderText + "`n" + $SessionText + "`n" +
               $SessionHeaderText + "`n" + $KeyboardText + "`n" + $KeyboardHeaderText +
               "`n" + $SubscriptionStoreText + "`n" + $KeymapText + "`n" +
               $AppText + "`n" + $DutText

Test-Contract 'PHASE2B_MODE_CONTROLLER_DELETED' `
    (-not (Test-Path $ModeCPath) -and -not (Test-Path $ModeHPath) -and
     $MakefileText -notmatch 'rdx_ble_mode_controller\.c') `
    'mode controller source/header and build entry must be deleted'

Test-Contract 'PHASE2B_NO_MODE_OWNER_RUNTIME' `
    ($RuntimeText -notmatch 'RDX_BLE_OWNER|RDX_BLE_MODE|connection_owner|rdx_ble_mode_request|rdx_ble_mode_controller') `
    'no CONFIG/HOGP mode or owner symbol may remain in runtime code'

Test-Contract 'PHASE2B_AUTH_SCAFFOLD_REMOVED' `
    ($RuntimeText -notmatch 'TCFG_RDX_SESSION_AUTH_GATE_ENABLE|rdx_protocol_session_|RDX_SESSION_UNAUTHORIZED|RDX_SESSION_IDENTIFIED|RDX_SESSION_AUTHORIZED' -and
     $HogpConfigText -notmatch 'TCFG_RDX_SESSION_AUTH_GATE_ENABLE' -and
     $ProjectConfigText -notmatch 'TCFG_RDX_SESSION_AUTH_GATE_ENABLE') `
    'unused session authorization must not masquerade as a product security boundary'

Test-Contract 'PHASE2B_UNIFIED_ENTRY_ENABLED' `
    ($ProjectConfigText -match '#define\s+TCFG_RDX_HOGP_UNIFIED_ENTRY_ENABLE\s+1' -and
     $HogpConfigText -match '#if\s+TCFG_RDX_HOGP_UNIFIED_ENTRY_ENABLE\s*&&\s*!TCFG_RDX_HOGP_ENABLE') `
    'T2620 must enable the unified entry and require the HOGP profile'

Test-Contract 'PHASE2B_SESSION_IS_LINK_STATE_ONLY' `
    ($SessionHeaderText -match 'u16\s+con_handle' -and
     $SessionHeaderText -match 'u16\s+mtu_size' -and
     $SessionHeaderText -match 'u8\s+connected' -and
     $SessionHeaderText -match 'u8\s+encrypted' -and
     $SessionHeaderText -match 'u8\s+active' -and
     $SessionHeaderText -match 'u8\s+ccc_configured' -and
     $SessionHeaderText -match 'u8\s+stream_tx_ready' -and
     $SessionHeaderText -notmatch 'auth') `
    'shared session must separate link state from RDX capability state without remote identity authorization'

$ConnectedBody = Get-FunctionBody $ServerText 'static\s+u8\s+rdx_ble_server_unified_link_connected\s*\([^)]*\)'
Test-Contract 'PHASE2B_CONNECTION_COMPLETE_IS_LINK_ONLY' `
    ($ConnectedBody -match 'status\s*!=\s*0' -and
     $ConnectedBody -match 'rdx_ble_session_on_connected\s*\(' -and
     $ConnectedBody -match 'rdx_ble_server_set_conn_handle\s*\(' -and
     $ConnectedBody -match 'rdx_ble_server_auto_shut_down_enable\s*\(\s*0\s*\)' -and
     $ConnectedBody -notmatch 'rdx_ble_server_rdx_connected_handle|rdx_record_on_ble_conn_changed|rdx_app_emmc_poweron' -and
     $ConnectedBody -notmatch 'rdx_hogp_on_connected') `
    'connection complete must initialize only link policy; RDX/HID business capabilities attach independently'

Test-Contract 'PHASE2B_BOTH_CONNECTION_EVENTS_SHARE_HELPER' `
    (([regex]::Matches($ServerText,
       'rdx_ble_server_unified_link_connected\s*\(\s*packet\s*,\s*size\s*,\s*[01]\s*\)')).Count -eq 2) `
    'normal and enhanced connection complete must share one lifecycle helper'

$AttachBody = Get-FunctionBody $ServerText 'static\s+u8\s+rdx_ble_server_hogp_attach\s*\([^)]*\)'
Test-Contract 'PHASE2B_HID_ATTACH_IS_CURRENT_LINK_CAPABILITY' `
    ($AttachBody -match 'rdx_ble_session_is_current\s*\(' -and
     $AttachBody -match 'rdx_ble_session_get_link_state\s*\(' -and
     $AttachBody -match 'rdx_hogp_on_connected\s*\(\s*con_handle\s*,\s*[\s\S]*?encrypted' -and
     ([regex]::Matches($ServerText,
       'rdx_ble_server_hogp_attach\s*\(\s*connection_handle\s*\)')).Count -ge 2) `
    'HID read/write must lazily attach to the current link and inherit earlier encryption'

$RdxAttachBody = Get-FunctionBody $ServerText 'static\s+u8\s+rdx_ble_server_rdx_attach\s*\([^)]*\)'
Test-Contract 'PHASE2B_RDX_ATTACH_IS_ACCESS_DRIVEN' `
    ($RdxAttachBody -match 'rdx_ble_session_is_current\s*\(' -and
     $RdxAttachBody -match 'rdx_ble_session_is_rdx_active\s*\(' -and
     $RdxAttachBody -match 'rdx_ble_session_activate_rdx\s*\(' -and
     $RdxAttachBody -match 'rdx_ble_server_rdx_connected_handle\s*\(' -and
     ([regex]::Matches($ServerText,
       'rdx_ble_server_rdx_attach\s*\(\s*connection_handle\s*\)')).Count -ge 2) `
    'any center may activate RDX by accessing an RDX handle, without affecting pure HID links'

$ReadBody = Get-FunctionBody $ServerText 'static\s+uint16_t\s+rdx_ble_server_att_read_callback\s*\([^)]*\)'
$WriteBody = Get-FunctionBody $ServerText 'static\s+int\s+rdx_ble_server_att_write_callback\s*\([^)]*\)'
Test-Contract 'PHASE2B_ATT_USES_CURRENT_HANDLE_NOT_OWNER' `
    ($ReadBody -match 'rdx_ble_session_is_current\s*\(\s*connection_handle\s*\)' -and
     $WriteBody -match 'rdx_ble_session_is_current\s*\(\s*connection_handle\s*\)' -and
     $ReadBody -match 'rdx_ble_server_rdx_attach\s*\(\s*connection_handle\s*\)' -and
     $WriteBody -match 'rdx_ble_server_rdx_attach\s*\(\s*connection_handle\s*\)' -and
     ($ReadBody + $WriteBody) -notmatch 'owner|authorized') `
    'ATT access must reject stale handles without CONFIG/HOGP identity gates'

$ReadyBody = Get-FunctionBody $KeyboardText 'u8\s+rdx_hogp_keyboard_is_ready\s*\([^)]*\)'
Test-Contract 'PHASE2B_HID_READY_ONLINE_BOUNDARY' `
    ($ReadyBody -match 's_hogp_connected' -and
     $ReadyBody -match 's_hid_notify_enabled' -and
     $ReadyBody -match 's_hogp_suspended' -and
     $ReadyBody -match 's_hogp_encrypted' -and
     $ReadyBody -notmatch 'owner|mode') `
    'online HID requires attached link, CCC, encryption and non-suspend'

$ReadyDropBody = Get-FunctionBody $KeyboardText 'static\s+void\s+rdx_hogp_ready_drop_cleanup\s*\([^)]*\)'
Test-Contract 'PHASE2B_HID_READY_DROP_RELEASES_BEFORE_OFFLINE' `
    ($ReadyDropBody -match 'rdx_hogp_keyboard_is_ready\s*\(\s*\)' -and
     $ReadyDropBody -match 'rdx_hogp_key_action_reset\s*\(\s*\)' -and
     $ReadyDropBody -match 'rdx_hogp_current_report_clear\s*\(\s*\)' -and
     ([regex]::Matches($KeyboardText, 'rdx_hogp_ready_drop_cleanup\s*\(\s*\)')).Count -ge 5) `
    'CCC disable, suspend, encryption loss, disconnect and cleanup must release HID before returning to offline keys'

$KeyRouteBody = Get-FunctionBody $AppText 'void\s+rdx_app_earphone_key_remap\s*\([^)]*\)'
Test-Contract 'PHASE2B_OFFLINE_KEYS_WHEN_HID_NOT_READY' `
    ($KeyRouteBody -match 'rdx_hogp_keyboard_is_ready\s*\(\s*\)' -and
     $KeyRouteBody -notmatch 'rdx_hogp_keyboard_is_connected\s*\(\s*\)' -and
     $KeyRouteBody -notmatch 'rdx_ble_mode_request_toggle') `
    'BLE/RDX connection alone must not consume offline product keys'

$DisconnectBody = Get-FunctionBody $ServerText 'static\s+void\s+rdx_ble_server_unified_link_disconnected\s*\([^)]*\)'
Test-Contract 'PHASE2B_DISCONNECT_CLEANS_BOTH_CAPABILITIES' `
    ($DisconnectBody -match 'disconnect\s+hdl=0x%04x\s+status=0x%02x\s+reason=0x%02x\s+rdx=%u\s+hid_ready=' -and
     $DisconnectBody -match 'rdx_hogp_on_disconnected\s*\(\s*disconnected_handle\s*\)' -and
     $DisconnectBody -match 'if\s*\(\s*rdx_active\s*\)[\s\S]*?rdx_ble_server_rdx_disconnected_cleanup_internal\s*\(' -and
     $DisconnectBody -match 'rdx_ble_server_link_disconnected_cleanup_internal\s*\(' -and
     $DisconnectBody -match 'rdx_ble_session_on_disconnected\s*\(\s*disconnected_handle\s*\)' -and
     $DisconnectBody -match 'rdx_ble_server_disconnected_adv_restart_schedule\s*\(' -and
     $DisconnectBody -notmatch 'rdx_ble_server_disconnected_adv_restart\s*\(' -and
     $DisconnectBody -match 'rdx_ble_server_disconnected_idle_policy_resume\s*\(') `
    'disconnect must always clear link/HID state and only run RDX business cleanup when RDX was active'

$DeferredAdvBody = Get-FunctionBody $ServerText 'static\s+void\s+rdx_ble_server_disconnected_adv_restart_deferred\s*\([^)]*\)'
$ScheduleAdvBody = Get-FunctionBody $ServerText 'static\s+void\s+rdx_ble_server_disconnected_adv_restart_schedule\s*\([^)]*\)'
$CancelAdvBody = Get-FunctionBody $ServerText 'static\s+void\s+rdx_ble_server_disconnected_adv_restart_cancel\s*\([^)]*\)'
$ExitBody = Get-FunctionBody $ServerText 'void\s+rdx_ble_server_exit\s*\([^)]*\)'
Test-Contract 'PHASE2B_DISCONNECT_ADV_RESTART_IS_DEFERRED' `
    ($ScheduleAdvBody -match 'adv_refresh_pending\s*=\s*TRUE' -and
     $ScheduleAdvBody -match 'sys_timeout_add\s*\(' -and
     $DeferredAdvBody -match 'app_ble_get_hdl_con_handle\s*\(' -and
     $DeferredAdvBody -match 'RDX_DISCONNECT_ADV_RESTART_RETRY_MAX' -and
     $DeferredAdvBody -match 'rdx_ble_server_disconnected_adv_restart\s*\(' -and
     $ConnectedBody -match 'rdx_ble_server_disconnected_adv_restart_cancel\s*\(' -and
     $CancelAdvBody -match 'sys_timeout_del\s*\(' -and
     $ExitBody -match 'rdx_ble_server_disconnected_adv_restart_cancel\s*\(') `
    'advertising restart must run after the HCI callback, retry wrapper cleanup, and be cancelled by a newer link or server exit'

Test-Contract 'PHASE2B_CCC_RUNTIME_NOT_CLEARED_ON_DISCONNECT' `
    ($DisconnectBody -notmatch 'multi_att_clear_ccc_config' -and
     $KeyboardText -match 'multi_att_get_ccc_config\s*\(\s*con_handle\s*,\s*HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE\s*\)') `
    'disconnect must not erase the current stack runtime CCC and HID attach must query only its current handle'

$SendBody = Get-FunctionBody $ServerText 'int\s+rdx_ble_server_send\s*\([^)]*\)'
$OtaSendBody = Get-FunctionBody $ServerText 'int\s+rdx_ble_server_ota_send\s*\([^)]*\)'
$InactiveSendGuard = Get-SourceSlice $SendBody `
    'if (!rdx_ble_session_is_rdx_active(' `
    '//is data none?'
Test-Contract 'PHASE2B_RDX_SEND_HAS_NO_IDENTITY_GATE' `
    (($SendBody + $OtaSendBody) -notmatch 'owner|authorized|RDX_AUTH' -and
     $SendBody -match 'ble_con_handle' -and
     $SendBody -match 'rdx_ble_session_is_rdx_active' -and
     $OtaSendBody -match 'rdx_ble_session_is_rdx_active' -and
     $SendBody -match 'app_ble_att_vaild_len_get' -and
     -not $InactiveSendGuard.Contains('g_ble_send_fail_cnt')) `
    'RDX sends must require an activated capability without treating a HID-only link as transport failure'

Test-Contract 'PHASE2B_KEYMAP_HAS_NO_IDENTITY_GATE' `
    ($KeymapText -notmatch 'RDX_BLE_OWNER|rdx_protocol_session_is_authorized' -and
     $KeymapText -match 'rdx_hogp_keymap_product_authorized\s*\(' -and
     $KeymapText -match 'get_ota_status\s*\(' -and
     $KeymapText -match 'rdx_dut_is_in_mode\s*\(') `
    'keymap must be connection-accessible while preserving product and busy-state checks'

Test-Contract 'PHASE2B_DEDICATED_HOGP_ADV_REMOVED' `
    ($KeyboardText -notmatch 'rdx_hogp_adv_start|rdx_hogp_adv_stop|rdx_hogp_fill_adv_data|hogp_adv_start_internal') `
    'HOGP must not own an independent advertising state machine'

Test-Contract 'PHASE2B_DUT_USES_UNIFIED_ADV' `
    ($DutText -notmatch 'rdx_ble_mode_request_hogp' -and
     $DutText -match 'rdx_ble_server_adv_enable\s*\(\s*0\s*\)' -and
     $DutText -match 'rdx_ble_server_adv_data_changed\s*\(\s*\)') `
    'DUT enter/exit must suppress and restore the unified advertising entry'

if ($Failed -gt 0) {
    Write-Host "FAILED: $Failed Phase 2B contract check(s) failed."
    exit 1
}

Write-Host 'PASS: RDX unified session Phase 2B contracts are satisfied.'
exit 0
