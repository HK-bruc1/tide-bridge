#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$SessionHeaderPath = Join-Path $ProtocolDir 'rdx_ble_session.h'
$SessionPath = Join-Path $ProtocolDir 'rdx_ble_session.c'
$ServerPath = Join-Path $ProtocolDir 'rdx_ble_server.c'
$HogpPath = Join-Path $ProtocolDir 'rdx_hogp_keyboard.c'
$AppPath = Join-Path $ProtocolDir 'rdx_app.c'
$KeymapPath = Join-Path $ProtocolDir 'rdx_hogp_keymap_config.c'
$HogpConfigPath = Join-Path $ProtocolDir 'rdx_hogp_config.h'
$ProjectConfigPath = Join-Path $RepoRoot 'SDK/apps/earphone/include/t2620_project_config.h'
$MakefilePath = Join-Path $RepoRoot 'SDK/Makefile'
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

$SessionHeaderText = Get-Content -Raw $SessionHeaderPath
$SessionText = Get-Content -Raw $SessionPath
$ServerText = Get-Content -Raw $ServerPath
$HogpText = Get-Content -Raw $HogpPath
$AppText = Get-Content -Raw $AppPath
$KeymapText = Get-Content -Raw $KeymapPath
$HogpConfigText = Get-Content -Raw $HogpConfigPath
$ProjectConfigText = Get-Content -Raw $ProjectConfigPath
$MakefileText = Get-Content -Raw $MakefilePath

Test-Contract 'PHASE2A_SESSION_MODULE_LINKED' `
    ($MakefileText -match 'rdx_protocol/rdx_ble_session\.c') `
    'rdx_ble_session.c must be compiled with the RDX sources'

Test-Contract 'PHASE2A_SESSION_STATE_MODEL' `
    ($SessionHeaderText -match 'RDX_SESSION_UNAUTHORIZED[\s\S]*RDX_SESSION_IDENTIFIED[\s\S]*RDX_SESSION_AUTHORIZED' -and
     $SessionHeaderText -match 'typedef\s+struct\s*\{[\s\S]*u16\s+con_handle;[\s\S]*u16\s+mtu_size;[\s\S]*u8\s+connected;[\s\S]*u8\s+encrypted;[\s\S]*\}\s*rdx_ble_link_state_t' -and
     $SessionHeaderText -match 'typedef\s+struct\s*\{[\s\S]*u8\s+ccc_configured;[\s\S]*u8\s+stream_tx_ready;[\s\S]*rdx_session_auth_state_t\s+auth_state;[\s\S]*\}\s*rdx_ble_config_session_t') `
    'link state and config-session authorization must be modeled independently of owner'

Test-Contract 'PHASE2A_NEW_LINK_STARTS_UNAUTHORIZED' `
    ($SessionText -match 'rdx_ble_session_on_connected\s*\([^)]*\)[\s\S]*?rdx_ble_session_reset\s*\(\)[\s\S]*?connected\s*=\s*1' -and
     $SessionText -match 'auth_state\s*=\s*RDX_SESSION_UNAUTHORIZED') `
    'every new connection must reset session authorization before becoming connected'

Test-Contract 'PHASE2A_AUTH_TRANSITION_REQUIRES_CURRENT_IDENTIFIED_LINK' `
    ($SessionText -match 'rdx_protocol_session_authorize\s*\([^)]*\)[\s\S]*?!rdx_ble_session_is_current\s*\(\s*con_handle\s*\)[\s\S]*?auth_state\s*<\s*RDX_SESSION_IDENTIFIED[\s\S]*?RDX_SESSION_AUTHORIZED') `
    'authorization must not be inferred from an arbitrary or stale ATT write'

Test-Contract 'PHASE2A_AUTH_GATE_DEFAULTS_OFF' `
    ($HogpConfigText -match '#ifndef\s+TCFG_RDX_SESSION_AUTH_GATE_ENABLE\s*\r?\n\s*#define\s+TCFG_RDX_SESSION_AUTH_GATE_ENABLE\s+0' -and
     $ProjectConfigText -match '#define\s+TCFG_RDX_SESSION_AUTH_GATE_ENABLE\s+0') `
    'the gate must remain compatibility-off until the external protocol reports verified authentication'

$ConnectedHelper = [regex]::Match($ServerText,
    '(?sm)static\s+u8\s+rdx_ble_server_unified_link_connected\s*\([^)]*\)\s*\{(.*?)^\}')
$ConnectedBody = if ($ConnectedHelper.Success) { $ConnectedHelper.Groups[1].Value } else { '' }
Test-Contract 'PHASE2A_CONNECTION_COMPLETE_VALIDATION' `
    ($ConnectedBody -match 'size\s*<\s*min_size' -and
     $ConnectedBody -match 'connection_complete_get_status' -and
     $ConnectedBody -match 'status\s*!=\s*0' -and
     $ConnectedBody -match 'rdx_ble_session_on_connected\s*\(\s*con_handle\s*\)') `
    'normal and enhanced connection complete must share length/status validation and session initialization'

Test-Contract 'PHASE2A_LINK_CONNECT_SUSPENDS_IDLE_POLICY' `
    ($ConnectedBody -match 'g_rdx_ble_server_info\.ble_conn\s*=\s*TRUE\s*;[\s\S]*?rdx_ble_server_auto_shut_down_enable\s*\(\s*0\s*\)') `
    'a successful BLE link must suspend auto shutdown before any CONFIG/HOGP owner claim'

Test-Contract 'PHASE2A_JL_CALLBACK_SIZE_CONTRACT' `
    ($ServerText -match '#define\s+RDX_LE_CONNECTION_COMPLETE_MIN_SIZE\s+20' -and
     $ServerText -match '#define\s+RDX_LE_ENHANCED_CONNECTION_COMPLETE_MIN_SIZE\s+32' -and
     $ServerText -match '#define\s+RDX_DISCONNECTION_COMPLETE_MIN_SIZE\s+5') `
    'JL callback size excludes the HCI event-code byte, so valid lifecycle events must not be rejected as one byte short'

Test-Contract 'PHASE2A_BOTH_CONNECTION_EVENTS_USE_HELPER' `
    (([regex]::Matches($ServerText,
        'rdx_ble_server_unified_link_connected\s*\(\s*packet\s*,\s*size\s*,\s*[01]\s*\)')).Count -eq 2) `
    'both LE Connection Complete variants must converge on one helper'

Test-Contract 'PHASE2A_CONNECTION_UPDATE_USES_CURRENT_HANDLE' `
    ($ServerText -match 'HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:[\s\S]*?size\s*<\s*11[\s\S]*?get_status\s*\(\s*packet\s*\)\s*!=\s*0[\s\S]*?g_rdx_ble_server_info\.ble_con_handle\s*!=[\s\S]*?get_connection_handle\s*\(\s*packet\s*\)') `
    'connection update must compare the event handle with the stored link handle, not a zero local variable'

$DisconnectedHelper = [regex]::Match($ServerText,
    '(?sm)static\s+void\s+rdx_ble_server_unified_link_disconnected\s*\([^)]*\)\s*\{(.*?)^\}')
$DisconnectedBody = if ($DisconnectedHelper.Success) { $DisconnectedHelper.Groups[1].Value } else { '' }
$disconnectHandlePos = $DisconnectedBody.IndexOf('hci_event_disconnection_complete_get_connection_handle')
$hogpCleanupPos = $DisconnectedBody.IndexOf('rdx_hogp_on_disconnected(disconnected_handle)')
$sessionClearPos = $DisconnectedBody.IndexOf('rdx_ble_session_on_disconnected(disconnected_handle)')
$globalClearPos = $DisconnectedBody.IndexOf('rdx_ble_server_set_conn_handle(0)')
Test-Contract 'PHASE2A_DISCONNECT_PRESERVES_EVENT_HANDLE_FOR_CLEANUP' `
    ($disconnectHandlePos -ge 0 -and $hogpCleanupPos -gt $disconnectHandlePos -and
     $sessionClearPos -gt $hogpCleanupPos -and $globalClearPos -gt $sessionClearPos) `
    'disconnect cleanup must use the event handle before clearing global state'

Test-Contract 'PHASE2A_BONDED_CCC_OWNED_BY_STACK' `
    ($DisconnectedBody -notmatch 'multi_att_clear_ccc_config\s*\(' -and
     $HogpText -match 'multi_att_get_ccc_config\s*\(\s*con_handle\s*,\s*HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE\s*\)' -and
     $HogpText -match 's_hid_notify_enabled\s*=\s*\(ccc_config\s*&\s*0x0001\)') `
    'disconnect must clear local HID runtime without destroying the current bonded peer CCC managed by the stack'

$OwnerClaimHelper = [regex]::Match($ServerText,
    '(?sm)static\s+u8\s+rdx_ble_server_connection_owner_claim\s*\([^)]*\)\s*\{(.*?)^\}')
$OwnerClaimBody = if ($OwnerClaimHelper.Success) { $OwnerClaimHelper.Groups[1].Value } else { '' }
Test-Contract 'PHASE2A_HOGP_ATTACH_REPLAYS_ENCRYPTION' `
    ($OwnerClaimBody -match 'rdx_ble_session_get_link_state\s*\(\s*\)' -and
     $OwnerClaimBody -match 'encrypted\s*=\s*link_state->encrypted' -and
     $OwnerClaimBody -match 'rdx_hogp_on_connected\s*\(\s*con_handle\s*,\s*encrypted\s*\)' -and
     $HogpText -match 's_hogp_encrypted\s*=\s*encrypted\s*\?\s*1\s*:\s*0' -and
     $HogpText -match 'if\s*\(\s*!s_hogp_encrypted\s*\)\s*\{\s*sm_api_request_pairing') `
    'HOGP attach must atomically inherit an earlier link encryption event and avoid re-pairing an encrypted bond'

Test-Contract 'PHASE2A_HOGP_ENCRYPTION_EVENTS_REQUIRE_ATTACH' `
    ($ServerText -match '(?s)rdx_ble_session_set_encrypted\s*\([^;]+;\s*#if\s+TCFG_RDX_HOGP_ENABLE\s*if\s*\(rdx_hogp_keyboard_is_connected\s*\(\s*\)\)\s*\{\s*rdx_hogp_on_encryption_change') `
    'an early bonded encryption event belongs to the unified session and must not be dispatched to an unattached HOGP runtime'

Test-Contract 'PHASE2A_DISCONNECT_REASON_OBSERVABLE' `
    ($DisconnectedBody -match 'hci_event_disconnection_complete_get_status\s*\(' -and
     $DisconnectedBody -match 'hci_event_disconnection_complete_get_reason\s*\(' -and
     $DisconnectedBody -match 'disconnect hdl=.*status=.*reason=.*owner=') `
    'disconnect diagnostics must preserve handle, status, reason, and previous owner before cleanup'

$IdlePolicyHelper = [regex]::Match($ServerText,
    '(?sm)static\s+void\s+rdx_ble_server_disconnected_idle_policy_resume\s*\([^)]*\)\s*\{(.*?)^\}')
$IdlePolicyBody = if ($IdlePolicyHelper.Success) { $IdlePolicyHelper.Groups[1].Value } else { '' }
$idlePolicyCallPos = $DisconnectedBody.IndexOf('rdx_ble_server_disconnected_idle_policy_resume()')
Test-Contract 'PHASE2A_DISCONNECT_REARMS_IDLE_POLICY' `
    ($IdlePolicyBody -match 'rdx_ble_mode_broadcast_suppressed\s*\(\s*\)' -and
     $IdlePolicyBody -match 'rdx_ble_server_auto_shut_down_enable\s*\(\s*1\s*\)' -and
     $idlePolicyCallPos -gt $globalClearPos) `
    'all advertising identities must re-arm auto shutdown after link state is cleared, subject to the common suppression policy'

$HogpRestartHelper = [regex]::Match($ServerText,
    '(?sm)static\s+void\s+rdx_ble_mode_restart_hogp_advertising\s*\([^)]*\)\s*\{(.*?)^\}')
$ConfigRestartHelper = [regex]::Match($ServerText,
    '(?sm)static\s+void\s+rdx_ble_server_disconnected_adv_restart\s*\([^)]*\)\s*\{(.*?)^\}')
$HogpRestartBody = if ($HogpRestartHelper.Success) { $HogpRestartHelper.Groups[1].Value } else { '' }
$ConfigRestartBody = if ($ConfigRestartHelper.Success) { $ConfigRestartHelper.Groups[1].Value } else { '' }
Test-Contract 'PHASE2A_ADV_REFRESH_DOES_NOT_RESET_IDLE_TIMER' `
    ($HogpRestartBody -notmatch 'auto_shut_down' -and
     $ConfigRestartBody -notmatch 'auto_shut_down' -and
     $ServerText -match '(?s)void\s+rdx_ble_server_disconnected_handle\s*\([^)]*\).*?rdx_ble_server_disconnected_adv_restart\s*\(\s*\);\s*rdx_ble_server_disconnected_idle_policy_resume\s*\(\s*\);') `
    'auto shutdown belongs to disconnect finalization, not generic advertising refresh'

Test-Contract 'PHASE2A_SENSITIVE_EVENT_GATE' `
    ($AppText -match 'rdx_app_protocol_event_is_public' -and
     $AppText -match '!rdx_protocol_session_is_authorized\s*\(' -and
     $AppText -match 'event rejected') `
    'application protocol events must pass a centralized per-session gate when enabled'

Test-Contract 'PHASE2A_OTA_AND_KEYMAP_GATES' `
    ($ServerText -match 'OTA write rejected:[^\r\n]*unauthorized' -and
     $ServerText -match '!rdx_protocol_session_is_authorized\s*\(\s*connection_handle\s*\)' -and
     $ServerText -match 'server send rejected:[^\r\n]*unauthorized' -and
     $ServerText -match 'OTA send rejected:[^\r\n]*unauthorized' -and
     $KeymapText -match 'TCFG_RDX_SESSION_AUTH_GATE_ENABLE[\s\S]*?!rdx_protocol_session_is_authorized') `
    'normal sends, OTA receive/send, and keymap bypass paths must use the same session authorization state'

Test-Contract 'PHASE2A_OWNER_RETAINED_FOR_COMPATIBILITY' `
    ($ServerText -match 'rdx_ble_server_connection_owner_claim' -and
     $ServerText -match 'current\s*!=\s*RDX_BLE_OWNER_NONE') `
    'owner isolation must remain until a real authentication-complete integration enables the new gate'

if ($Failed -gt 0) {
    Write-Host "FAILED: $Failed Phase 2A contract check(s) failed."
    exit 1
}

Write-Host 'PASS: RDX unified session Phase 2A contracts are satisfied.'
exit 0
