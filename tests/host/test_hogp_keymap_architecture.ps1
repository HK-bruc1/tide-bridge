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

$Files = @{
    Service  = Join-Path $ProtocolDir 'rdx_hogp_keymap_config.c'
    Protocol = Join-Path $ProtocolDir 'rdx_hogp_keymap_protocol.c'
    Store    = Join-Path $ProtocolDir 'rdx_hogp_keymap_store.c'
    Internal = Join-Path $ProtocolDir 'rdx_hogp_keymap_internal.h'
    Usage    = Join-Path $RepoRoot 'SDK/apps/common/device/hid/hid_keyboard_usage.h'
    UsbKeys  = Join-Path $RepoRoot 'SDK/apps/common/device/usb/host/usb_hid_keys.h'
    Action   = Join-Path $ProtocolDir 'rdx_hogp_key_action.c'
    HogpCfg  = Join-Path $ProtocolDir 'rdx_hogp_config.h'
    Makefile = Join-Path $RepoRoot 'SDK/Makefile'
}

foreach ($entry in $Files.GetEnumerator()) {
    Test-Contract "FILE_$($entry.Key.ToUpper())" (Test-Path $entry.Value) "missing $($entry.Value)"
}

if ($Failed -eq 0) {
    $ServiceText = Get-Content -Raw $Files.Service
    $ProtocolText = Get-Content -Raw $Files.Protocol
    $StoreText = Get-Content -Raw $Files.Store
    $UsageText = Get-Content -Raw $Files.Usage
    $UsbText = Get-Content -Raw $Files.UsbKeys
    $ActionText = Get-Content -Raw $Files.Action
    $HogpCfgText = Get-Content -Raw $Files.HogpCfg
    $MakeText = Get-Content -Raw $Files.Makefile
    $ProtocolHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_protocol.h')

    Test-Contract 'SERVICE_NO_VM_IO' `
        ($ServiceText -notmatch 'syscfg_(read|write)|syscfg_id\.h') `
        'service layer must not access syscfg directly'
    Test-Contract 'PROTOCOL_NO_TRANSPORT_OR_VM' `
        ($ProtocolText -notmatch 'rdx_protocol_custom_msg_indicate|syscfg_(read|write)|rdx_hogp_key_action') `
        'protocol layer must only encode, decode and validate'
    Test-Contract 'STORE_NO_BLE_OR_EXECUTOR' `
        ($StoreText -notmatch 'rdx_protocol_custom_msg_indicate|rdx_hogp_key_action|rdx_ble_') `
        'store layer must not depend on BLE routing or the executor'

    Test-Contract 'USB_USES_SHARED_USAGE' `
        ($UsbText -match '#include\s+"\.\./\.\./hid/hid_keyboard_usage\.h"' -and
         $UsbText -match '_KEY_MOD_LCTRL\s+HID_KEYBOARD_MOD_LCTRL') `
        'USB Host compatibility header must consume shared HID definitions'
    Test-Contract 'HOGP_USES_SHARED_USAGE' `
        ($ProtocolText -match '#include\s+"device/hid/hid_keyboard_usage\.h"' -and
         $ActionText -match '#include\s+"device/hid/hid_keyboard_usage\.h"') `
        'HOGP protocol and test vectors must consume shared HID definitions'
    Test-Contract 'USAGE_RANGES_ARE_NAMED' `
        ($UsageText -match 'HID_KEYBOARD_USAGE_STANDARD_MIN\s+0x04' -and
         $UsageText -match 'HID_KEYBOARD_USAGE_STANDARD_MAX\s+0xA4' -and
         $UsageText -match 'HID_KEYBOARD_USAGE_KEYPAD_EXT_MIN\s+0xB0' -and
         $UsageText -match 'HID_KEYBOARD_USAGE_KEYPAD_EXT_MAX\s+0xDD') `
        'shared header must define the A1 Keyboard/Keypad whitelist boundaries'

    Test-Contract 'LONG_FRAMES_OFF_TASK_STACK' `
        ($ServiceText -match 'static\s+char\s+hex\s*\[\s*RDX_HOGPKM_MAX_RESPONSE_HEX_LEN\s*\+\s*1\s*\]' -and
         $ServiceText -match 'static\s+u8\s+payload\s*\[\s*1\s*\+\s*RDX_HOGPKM_KEYMAP_LEN\s*\]' -and
         $ProtocolText -match 'static\s+u8\s+frame\s*\[\s*RDX_HOGPKM_MAX_RESPONSE_FRAME_LEN\s*\]') `
        'A1 GET response workspaces must not be nested local arrays on app_core'

    Test-Contract 'LONG_UPLINK_BYPASSES_LEGACY_CUSTOM_WRAPPER' `
        ($ServiceText -notmatch 'rdx_protocol_custom_msg_indicate\s*\(' -and
         $ProtocolHeaderText -match '#define\s+CUSTOM_VALUE_MAX_LENGTH\s+\(100\)' -and
         $ServiceText -match 'RDX_HOGPKM_UPLINK_LEN\s*==\s*120' -and
         $ServiceText -match 'rdx_protocol_packet_send_priority\s*\(\s*packet\s*,\s*offset\s*\)') `
        'preserve the prebuilt custom ABI and bypass it for 100-character GET values'

    Test-Contract 'RDX_PRIORITY_SEND_RETURNS_POSITIVE_ON_SUCCESS' `
        ($ServiceText -match 'sent_len\s*=\s*rdx_protocol_packet_send_priority\s*\(\s*packet\s*,\s*offset\s*\)' -and
         $ServiceText -match 'return\s*\(\s*sent_len\s*>\s*0\s*\)\s*\?\s*0\s*:\s*-1') `
        'rdx_protocol_packet_send_priority returns positive length on success, not zero'

    Test-Contract 'HOGPKM_TRACE_DEFAULT_OFF' `
        ($HogpCfgText -match '#define\s+RDX_HOGPKM_TRACE_ENABLE\s+0' -and
         $ServiceText -match '#if\s+RDX_HOGPKM_TRACE_ENABLE' -and
         $ServiceText -match '#define\s+HOGPKM_TRACE\(\.\.\.\)\s+y_printf\(__VA_ARGS__\)' -and
         $ServiceText -notmatch 'y_printf\s*\(\s*"\[HOGPKM\]') `
        'HOGPKM diagnostics must stay behind RDX_HOGPKM_TRACE_ENABLE and default off'

    $preparePos = $ServiceText.IndexOf('rdx_hogpkm_store_prepare(')
    $applyPos = $ServiceText.IndexOf('rdx_hogpkm_apply_payload(payload)', $preparePos)
    $commitPos = $ServiceText.IndexOf('rdx_hogpkm_store_commit(', $applyPos)
    Test-Contract 'TRANSACTION_PREPARE_APPLY_COMMIT_ORDER' `
        ($preparePos -ge 0 -and $applyPos -gt $preparePos -and $commitPos -gt $applyPos) `
        'transaction must prepare/read back VM before RAM apply and commit metadata last'
    Test-Contract 'STORE_SPLITS_PREPARE_AND_COMMIT' `
        ($StoreText -match 'int\s+rdx_hogpkm_store_prepare\s*\(' -and
         $StoreText -match 'int\s+rdx_hogpkm_store_commit\s*\(\s*rdx_hogpkm_store_transaction_t\s*\*') `
        'store must expose an explicit two-phase transaction'
    Test-Contract 'APPLY_RELEASES_OLD_REPORT' `
        ($ActionText -match '(?s)rdx_hogp_key_action_keymap_apply.*?rdx_hogp_key_action_cancel_release_timer\s*\(\s*\).*?rdx_hogp_keyboard_release_all\s*\(\s*\).*?memset\s*\(\s*&s_rdx_hogp_key_action_active_keymap') `
        'executor must release the old report before replacing its active map'
    Test-Contract 'CONFIG_ACCESS_POLICY' `
        ($ServiceText -match '__attribute__\s*\(\s*\(weak\)\s*\)\s*int\s+rdx_hogp_keymap_product_authorized\s*\(' -and
         $ServiceText -match '(?s)rdx_hogp_keymap_product_authorized\s*\(void\).*?return\s+1\s*;' -and
         $ServiceText -notmatch 'RDX_BLE_OWNER|rdx_protocol_session_is_authorized' -and
         $ServiceText -notmatch 'rdx_vm_get_bound_status\s*\(' -and
         $ServiceText -match 'get_ota_status\s*\(' -and
         $ServiceText -match 'rdx_app_get_poweroff_flag\s*\(' -and
         $ServiceText -match 'rdx_dut_is_in_mode\s*\(') `
        'keymap must be open to the current BLE link without identity gates while rejecting conflicting product states'
    Test-Contract 'RESPONSES_QUEUED_TO_APP_CORE' `
        ($ServiceText -match 'rdx_hogpkm_queue_status' -and
         $ServiceText -match 'os_taskq_post_type\s*\(\s*"app_core"\s*,\s*Q_CALLBACK') `
        'receive-context status responses must be serialized on app_core'
    Test-Contract 'DISCONNECT_GENERATION_GUARDS_IN_FLIGHT_REQUEST' `
        ($ServiceText -match 'static\s+volatile\s+u32\s+s_rdx_hogpkm_generation' -and
         $ServiceText -match 'rdx_hogpkm_commit\s*\(\s*u32\s+generation' -and
         $ServiceText -match 'generation\s*!=\s*s_rdx_hogpkm_generation' -and
         $ServiceText -match 'rdx_hogpkm_send_response[\s\S]*?u32\s+generation') `
        'generation must guard both in-flight commit boundaries and response send'
    Test-Contract 'STALE_CALLBACK_CANNOT_CLEAR_NEW_PENDING' `
        ($ServiceText -match 'rdx_hogpkm_clear_pending_if_match' -and
         $ServiceText -match 's_rdx_hogpkm_pending\.generation\s*==\s*request->generation' -and
         $ServiceText -match 's_rdx_hogpkm_pending\.request_frame_crc32\s*==\s*request->request_frame_crc32') `
        'an old app_core callback must only clear its own pending request'

    $ModuleLineLimits = @{ Service = 700; Protocol = 600; Store = 600 }
    foreach ($name in @('Service', 'Protocol', 'Store')) {
        $lineCount = (Get-Content $Files[$name]).Count
        Test-Contract "${name}_MODULE_SIZE" ($lineCount -lt $ModuleLineLimits[$name]) `
            "$name module has $lineCount lines"
    }

    foreach ($source in @('rdx_hogp_keymap_config.c', 'rdx_hogp_keymap_protocol.c', 'rdx_hogp_keymap_store.c')) {
        Test-Contract "MAKEFILE_$($source.ToUpper())" ($MakeText -match [regex]::Escape($source)) "Makefile missing $source"
    }
}

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All HOGP keymap architecture checks passed.'
    exit 0
}

Write-Host "$Failed HOGP keymap architecture checks failed."
exit 1
