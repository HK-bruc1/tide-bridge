#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$ProtocolRoot = 'SDK\apps\common\third_party_profile\rdx_protocol'
$Header = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_profile.h"
$Profile = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_profile.c"
$GattProfile = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_gatt_profile.c"
$GattProfileHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_gatt_profile.h"
$GattPrivateFragment = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_gatt_private_profile.inc"
$HidFragment = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hid_profile.inc"
$DisFragment = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_dis_profile.inc"
$HidService = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hid_service.c"
$HidServiceHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hid_service.h"
$Keyboard = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keyboard.c"
$KeyboardHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keyboard.h"
$Config = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_config.h"
$Server = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_server.c"
$Store = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_subscription_store.c"
$StoreHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_subscription_store.h"
$Vm = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_vm.c"
$Syscfg = Read-RepoFile $RepoRoot 'SDK\interface\utils\syscfg_id.h'
$LeUser = Read-RepoFile $RepoRoot 'SDK\interface\btstack\le\le_user.h'
$Makefile = Read-RepoFile $RepoRoot 'SDK\Makefile'
$MultiProtocol = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\multi_protocol_main.c'
$NormalizedHeader = (($Header -replace '\\', '') -replace '\s+', '')
$NormalizedGattProfile = (($GattProfile -replace '\\', '') -replace '\s+', '')
$NormalizedGattFragments = ((
    $GattPrivateFragment + $HidFragment + $DisFragment
) -replace '\\', '') -replace '\s+', ''

$Handles = [ordered]@{
    HID_SERVICE_HANDLE                           = 0x0016
    HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE      = 0x0017
    HID_PROTOCOL_MODE_VALUE_HANDLE               = 0x0018
    HID_INPUT_REPORT_CHARACTERISTIC_HANDLE       = 0x0019
    HID_INPUT_REPORT_VALUE_HANDLE                = 0x001a
    HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE = 0x001b
    HID_INPUT_REPORT_REFERENCE_HANDLE            = 0x001c
    HID_REPORT_MAP_CHARACTERISTIC_HANDLE          = 0x001d
    HID_REPORT_MAP_VALUE_HANDLE                   = 0x001e
    HID_INFORMATION_CHARACTERISTIC_HANDLE         = 0x001f
    HID_INFORMATION_VALUE_HANDLE                  = 0x0020
    HID_CONTROL_POINT_CHARACTERISTIC_HANDLE       = 0x0021
    HID_CONTROL_POINT_VALUE_HANDLE                = 0x0022
    HID_OUTPUT_REPORT_CHARACTERISTIC_HANDLE       = 0x0023
    HID_OUTPUT_REPORT_VALUE_HANDLE                = 0x0024
    HID_OUTPUT_REPORT_REFERENCE_HANDLE            = 0x0025
}
foreach ($entry in $Handles.GetEnumerator()) {
    $match = [regex]::Match(
        $GattProfileHeader,
        '#define\s+' + [regex]::Escape($entry.Key) + '\s+(0x[0-9A-Fa-f]+)'
    )
    $actual = if ($match.Success) {
        [convert]::ToInt32($match.Groups[1].Value, 16)
    } else { -1 }
    Assert-Contract "HANDLE_$($entry.Key)" ($actual -eq $entry.Value) `
        "expected 0x$($entry.Value.ToString('X4')), found 0x$($actual.ToString('X4'))"
}

$ExpectedMap = @(
    0x05,0x01,0x09,0x06,0xA1,0x01,0x85,0x01,0x05,0x07,
    0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,0x75,0x01,
    0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x01,
    0x95,0x06,0x75,0x08,0x15,0x00,0x26,0xFF,0x00,0x05,
    0x07,0x19,0x00,0x29,0xFF,0x81,0x00,0x05,0x08,0x19,
    0x01,0x29,0x03,0x15,0x00,0x25,0x01,0x95,0x03,0x75,
    0x01,0x91,0x02,0x95,0x01,0x75,0x05,0x91,0x01,0xC0
)
$mapMatch = [regex]::Match(
    $Profile,
    '(?s)const\s+u8\s+rdx_hogp_report_map\[\]\s*=\s*\{\s*#if\s+TCFG_RDX_CODEX_MICRO_MODE\s*!=\s*RDX_CODEX_MICRO_MODE_VENDOR_ONLY(.*?)#endif'
)
$ActualMap = if ($mapMatch.Success) {
    @([regex]::Matches($mapMatch.Groups[1].Value, '0x([0-9A-Fa-f]{2})') |
        ForEach-Object { [convert]::ToInt32($_.Groups[1].Value, 16) })
} else { @() }
$mapOk = $ActualMap.Count -eq $ExpectedMap.Count
for ($index = 0; $mapOk -and $index -lt $ExpectedMap.Count; $index++) {
    $mapOk = $ActualMap[$index] -eq $ExpectedMap[$index]
}
Assert-Contract 'REPORT_MAP_BYTES' `
    ($mapOk -and $Header -match '#define\s+RDX_HOGP_REPORT_MAP_LEN\s+\(?70\)?') `
    'the 70-byte keyboard Report Map is a host-visible protocol contract'

$ProfileTokens = @(
    'RDX_HOGP_ATT_PRIMARY_SERVICE_16(HID_SERVICE_HANDLE,RDX_HOGP_UUID_HID_SERVICE)',
    'RDX_HOGP_ATT_CHARACTERISTIC_16(HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE,RDX_HOGP_CHAR_PROP_PROTOCOL_MODE,HID_PROTOCOL_MODE_VALUE_HANDLE,RDX_HOGP_UUID_PROTOCOL_MODE)',
    'RDX_HOGP_ATT_VALUE_16(HID_PROTOCOL_MODE_VALUE_HANDLE,RDX_HOGP_ATT_FLAGS_PROTOCOL_MODE_VALUE,RDX_HOGP_UUID_PROTOCOL_MODE)',
    'RDX_HOGP_ATT_CHARACTERISTIC_16(HID_INPUT_REPORT_CHARACTERISTIC_HANDLE,RDX_HOGP_CHAR_PROP_INPUT_REPORT,HID_INPUT_REPORT_VALUE_HANDLE,RDX_HOGP_UUID_REPORT)',
    'RDX_HOGP_ATT_VALUE_16(HID_INPUT_REPORT_VALUE_HANDLE,RDX_HOGP_ATT_FLAGS_INPUT_REPORT_VALUE,RDX_HOGP_UUID_REPORT)',
    'RDX_HOGP_ATT_CCC(HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE,RDX_HOGP_CCC_DEFAULT_VALUE)',
    'RDX_HOGP_ATT_REPORT_REFERENCE(HID_INPUT_REPORT_REFERENCE_HANDLE,RDX_HOGP_INPUT_REPORT_ID,RDX_HOGP_INPUT_REPORT_TYPE)',
    'RDX_HOGP_ATT_CHARACTERISTIC_16(HID_REPORT_MAP_CHARACTERISTIC_HANDLE,RDX_HOGP_CHAR_PROP_REPORT_MAP,HID_REPORT_MAP_VALUE_HANDLE,RDX_HOGP_UUID_REPORT_MAP)',
    'RDX_HOGP_ATT_VALUE_16(HID_REPORT_MAP_VALUE_HANDLE,RDX_HOGP_ATT_FLAGS_REPORT_MAP_VALUE,RDX_HOGP_UUID_REPORT_MAP)',
    'RDX_HOGP_ATT_CHARACTERISTIC_16(HID_INFORMATION_CHARACTERISTIC_HANDLE,RDX_HOGP_CHAR_PROP_HID_INFORMATION,HID_INFORMATION_VALUE_HANDLE,RDX_HOGP_UUID_HID_INFORMATION)',
    'RDX_HOGP_ATT_VALUE_16(HID_INFORMATION_VALUE_HANDLE,RDX_HOGP_ATT_FLAGS_HID_INFORMATION_VALUE,RDX_HOGP_UUID_HID_INFORMATION)',
    'RDX_HOGP_ATT_CHARACTERISTIC_16(HID_CONTROL_POINT_CHARACTERISTIC_HANDLE,RDX_HOGP_CHAR_PROP_CONTROL_POINT,HID_CONTROL_POINT_VALUE_HANDLE,RDX_HOGP_UUID_HID_CONTROL_POINT)',
    'RDX_HOGP_ATT_VALUE_16(HID_CONTROL_POINT_VALUE_HANDLE,RDX_HOGP_ATT_FLAGS_CONTROL_POINT_VALUE,RDX_HOGP_UUID_HID_CONTROL_POINT)',
    'RDX_HOGP_ATT_CHARACTERISTIC_16(HID_OUTPUT_REPORT_CHARACTERISTIC_HANDLE,RDX_HOGP_CHAR_PROP_OUTPUT_REPORT,HID_OUTPUT_REPORT_VALUE_HANDLE,RDX_HOGP_UUID_REPORT)',
    'RDX_HOGP_ATT_VALUE_16(HID_OUTPUT_REPORT_VALUE_HANDLE,RDX_HOGP_ATT_FLAGS_OUTPUT_REPORT_VALUE,RDX_HOGP_UUID_REPORT)',
    'RDX_HOGP_ATT_REPORT_REFERENCE(HID_OUTPUT_REPORT_REFERENCE_HANDLE,RDX_HOGP_OUTPUT_REPORT_ID,RDX_HOGP_OUTPUT_REPORT_TYPE)'
)
Assert-Contract 'PROFILE_ATTRIBUTE_ORDER' `
    (Test-TokensInOrder $NormalizedGattFragments $ProfileTokens) `
    'HID attributes must retain their externally visible order'

$ReadCallback = Get-SourceSlice $Server `
    'static uint16_t rdx_ble_server_att_read_callback(' `
    '/**************************************************************************'
$WriteCallback = Get-SourceSlice $Server `
    'static int rdx_ble_server_att_write_callback(' `
    'static u8 rdx_ble_server_adv_append_data('
Assert-Contract 'GATT_PROFILE_OWNS_AGGREGATE_AND_ROUTES' `
    ($GattProfile -match 'const\s+u8\s+rdx_profile_data\[\]' -and
     $NormalizedGattProfile.Contains('#include"rdx_gatt_private_profile.inc"#include"rdx_hid_profile.inc"#include"rdx_dis_profile.inc"') -and
     $GattProfileHeader -match '#define\s+RDX_GATT_GAP_NAME_VALUE_HANDLE\s+0x0003' -and
     $GattProfileHeader -match '#define\s+RDX_GATT_COMMAND_VALUE_HANDLE\s+0x0006' -and
     $GattProfileHeader -match '#define\s+RDX_GATT_NOTIFY_CCC_HANDLE\s+0x0009' -and
     $GattProfileHeader -match '#define\s+RDX_GATT_BATTERY_CCC_HANDLE\s+0x000f' -and
     $GattProfileHeader -match '#define\s+RDX_GATT_OTA_NOTIFY_CCC_HANDLE\s+0x0015' -and
     $GattProfileHeader -match '#define\s+HID_SERVICE_HANDLE\s+0x0016' -and
     $GattProfileHeader -match '#define\s+HID_CODEX_OUTPUT_REPORT_REFERENCE_HANDLE\s+0x002c' -and
     $GattProfileHeader -match '#define\s+DIS_SERVICE_HANDLE\s+0x002d' -and
     $Header -notmatch '(?m)^\s*#define\s+(?:HID_|DIS_).*HANDLE' -and
     $GattProfileHeader -match 'rdx_gatt_profile_dispatch_read' -and
     $GattProfileHeader -match 'rdx_gatt_profile_dispatch_write' -and
     $GattProfile -match '(?s)case\s+RDX_GATT_COMMAND_VALUE_HANDLE:.*?case\s+RDX_GATT_NOTIFY_CCC_HANDLE:.*?case\s+RDX_GATT_OTA_COMMAND_VALUE_HANDLE:.*?case\s+RDX_GATT_OTA_NOTIFY_CCC_HANDLE:.*?provider\s*=\s*ops->write_private' -and
     $Server -match '(?s)static\s+const\s+rdx_gatt_profile_ops_t\s+g_rdx_gatt_profile_ops\s*=.*?\.read_gap_name.*?\.read_hid.*?\.write_battery_ccc.*?\.write_hid' -and
     $ReadCallback -match 'rdx_gatt_profile_dispatch_read\s*\(' -and
     $WriteCallback -match 'rdx_gatt_profile_dispatch_write\s*\(' -and
     $ReadCallback -notmatch '\bswitch\s*\(|RDX_GATT_(?:READ|WRITE)_ROUTE|(?:HID|DIS|RDX_GATT_).*HANDLE' -and
     $WriteCallback -notmatch '\bswitch\s*\(|RDX_GATT_(?:READ|WRITE)_ROUTE|(?:HID|DIS|RDX_GATT_).*HANDLE' -and
     $Server -notmatch 'const\s+(?:uint8_t|u8)\s+rdx_profile_data\[\]' -and
     $Server -notmatch '(?m)^\s*#define\s+(?:ATT_CHARACTERISTIC_|DIS_)' -and
     $Makefile.Contains('rdx_gatt_profile.c')) `
    'the profile module must own the ATT layout/fragments and dispatch SDK callbacks through narrow providers'

$ByteContracts = @(
    '#defineRDX_HOGP_UUID_HID_SERVICE0x1812',
    '#defineRDX_HOGP_UUID_PROTOCOL_MODE0x2A4E',
    '#defineRDX_HOGP_UUID_REPORT0x2A4D',
    '#defineRDX_HOGP_UUID_REPORT_MAP0x2A4B',
    '#defineRDX_HOGP_UUID_HID_INFORMATION0x2A4A',
    '#defineRDX_HOGP_UUID_HID_CONTROL_POINT0x2A4C',
    '#defineRDX_HOGP_CCC_DEFAULT_VALUE0x0000',
    '#defineRDX_HOGP_INPUT_REPORT_ID0x01',
    '#defineRDX_HOGP_INPUT_REPORT_TYPE0x01',
    '#defineRDX_HOGP_OUTPUT_REPORT_ID0x01',
    '#defineRDX_HOGP_OUTPUT_REPORT_TYPE0x02'
)
$profileBytesOk = $true
foreach ($contract in $ByteContracts) {
    $profileBytesOk = $profileBytesOk -and $NormalizedHeader.Contains($contract)
}
Assert-Contract 'PROFILE_BYTE_VALUES' $profileBytesOk `
    'HID UUIDs, CCC defaults and Report Reference values must remain frozen'

$SendBody = Get-SourceSlice $Keyboard `
    'int rdx_hogp_keyboard_report_send(' `
    'int rdx_hogp_keyboard_release_all('
Assert-Contract 'INPUT_REPORT_PAYLOAD' `
    ($KeyboardHeader -match '#define\s+RDX_HOGP_KEYBOARD_REPORT_LEN\s+8' -and
     $SendBody -match 'u8\s+payload\s*\[\s*RDX_HOGP_KEYBOARD_REPORT_LEN\s*\]' -and
     $SendBody -match 'HID_INPUT_REPORT_VALUE_HANDLE\s*,\s*payload\s*,\s*sizeof\s*\(\s*payload\s*\)' -and
     $SendBody -notmatch 'REPORT_ID') `
    'Input Report must remain an 8-byte payload without a Report ID prefix'

$SmBranch = Get-SourceSlice $MultiProtocol `
    '#elif (THIRD_PARTY_PROTOCOLS_SEL & RDX_EN) && TCFG_RDX_HOGP_ENABLE' `
    '#elif (TCFG_LE_AUDIO_APP_CONFIG'
$encryptedFlagsOk = $Config -match '#define\s+RDX_HOGP_ENCRYPTION_REQUIRED\s+1' -and
                    $NormalizedHeader -match 'RDX_HOGP_ATT_FLAGS_PROTOCOL_MODE_VALUE.*ENCRYPTED_READ.*ENCRYPTED_WRITE' -and
                    $NormalizedHeader -match 'RDX_HOGP_ATT_FLAGS_INPUT_REPORT_CCC.*ENCRYPTED_READ.*ENCRYPTED_WRITE'
Assert-Contract 'HID_SECURITY_POLICY' `
    ($Config -match '#define\s+RDX_HOGP_PAIRING_MODE\s+0' -and
     $SmBranch -match 'IO_CAPABILITY_NO_INPUT_NO_OUTPUT' -and
     $SmBranch -match 'SM_AUTHREQ_BONDING\s*\|\s*SM_AUTHREQ_SECURE_CONNECTION' -and
     $SmBranch -notmatch 'SM_AUTHREQ_MITM_PROTECTION' -and
     $encryptedFlagsOk) `
    'HOGP must use bonded Just Works and encrypted dynamic attributes without a false MITM claim'

$WriteBody = Get-SourceSlice $HidService `
    'int rdx_hid_service_att_write(' `
    'u8 rdx_hid_service_is_connected('
$ReadyBody = Get-SourceSlice $HidService `
    'u8 rdx_hid_report_is_ready(' `
    'int rdx_hid_report_notify('
$ReadyDropBody = Get-SourceSlice $HidService `
    'static void rdx_hid_report_ready_drop(' `
    'void rdx_hid_service_runtime_cleanup('
$KeyboardDropBody = Get-SourceSlice $Keyboard `
    'void rdx_hogp_keyboard_ready_drop_cleanup(' `
    'u16 rdx_hogp_keyboard_att_read('
$HidStateMatch = [regex]::Match(
    $HidService,
    '(?s)typedef\s+struct\s*\{(.*?)\}\s*rdx_hid_service_state_t\s*;'
)
$HidStateBody = if ($HidStateMatch.Success) {
    $HidStateMatch.Groups[1].Value
} else { '' }
$VolatileRuntimeFields = @(
    'connected',
    'encrypted',
    'suspended',
    'keyboard_notify_enabled',
    'codex_notify_enabled'
)
$volatileRuntimeStateOk = $HidStateMatch.Success
foreach ($field in $VolatileRuntimeFields) {
    $volatileRuntimeStateOk = $volatileRuntimeStateOk -and
        ($HidStateBody -match ('volatile\s+u8\s+' + [regex]::Escape($field) + '\s*;'))
}
$volatileRuntimeStateOk = $volatileRuntimeStateOk -and
    ([regex]::Matches($HidStateBody, 'volatile\s+u8\s+\w+\s*;').Count -eq
        $VolatileRuntimeFields.Count)
Assert-Contract 'HID_RUNTIME_FLAGS_ARE_VOLATILE' `
    $volatileRuntimeStateOk `
    'the five callback-shared HID runtime flags must preserve their volatile baseline semantics under JL LTO'

Assert-Contract 'HID_CCC_AND_READY_BOUNDARY' `
    ($WriteBody -match 'offset\s*!=\s*0' -and
     $WriteBody -match 'buffer_size\s*!=\s*2' -and
     $WriteBody -match 'cfg\s*!=\s*0x0000\s*&&\s*cfg\s*!=\s*0x0001' -and
     $WriteBody -match '!s_hid\.encrypted' -and
     $ReadyBody -match 's_hid\.connected' -and
     $ReadyBody -match 's_hid\.keyboard_notify_enabled' -and
     $ReadyBody -match 's_hid\.codex_notify_enabled' -and
     $ReadyBody -match 's_hid\.encrypted' -and
     $ReadyBody -match 's_hid\.suspended' -and
     $ReadyDropBody -match 'rdx_hogp_keyboard_ready_drop_cleanup\s*\(\s*\)' -and
     $ReadyDropBody -match 'rdx_codex_micro_ready_drop_cleanup\s*\(\s*\)' -and
     $KeyboardDropBody -match 'rdx_input_router_keyboard_ready_drop_cleanup\s*\(\s*\)') `
    'HID core must gate each report by its own CCC plus shared encrypted/non-suspended state and clean providers before ready drops'

Assert-Contract 'HID_CORE_OWNS_SHARED_STATE' `
    ($Makefile.Contains('rdx_hid_service.c') -and
     $HidServiceHeader -match 'rdx_hid_report_is_ready' -and
     $HidServiceHeader -match 'rdx_hid_report_notify' -and
     $HidService -match 'app_ble_att_send_data\s*\(' -and
     $Keyboard -notmatch 'app_ble_att_send_data|s_hogp_connected|s_hogp_encrypted|s_hogp_suspended|s_codex_notify_enabled|get_sm_peer_address|rdx_hogp_subscription_store|rdx_codex_micro' -and
     $KeyboardHeader -notmatch 'rdx_hogp_(?:on_connected|on_disconnected|on_encryption_change|on_sm_event|codex_is_ready|route_is_active)' -and
     $HidService -match 'rdx_hogp_keyboard_att_(?:read|write)' -and
     $HidService -match 'rdx_codex_micro_(?:att_read|output_write)' -and
     $Server -match 'rdx_hid_service_att_(?:read|write)' -and
     $Server -notmatch 'rdx_hogp_att_(?:read|write)') `
    'HID core must exclusively own shared runtime/transport while keyboard remains an ID 1 report provider'

$StoreParseBody = Get-SourceSlice $Store `
    'static int rdx_hogp_subscription_parse_record(' `
    'static int rdx_hogp_subscription_read_vm_slot('
$StoreBuildBody = Get-SourceSlice $Store `
    'static void rdx_hogp_subscription_build_record(' `
    'static int rdx_hogp_subscription_parse_record('
$StoreUpdateBody = Get-SourceSlice $Store `
    'int rdx_hogp_subscription_store_update(' `
    'int rdx_hogp_subscription_store_reset('
$RestoreBody = Get-SourceSlice $HidService `
    'static void rdx_hid_subscription_restore_if_available(' `
    'static void rdx_hid_report_ready_drop('
$StoreLoadBody = Get-SourceSlice $Store `
    'static void rdx_hogp_subscription_load(' `
    'static int rdx_hogp_subscription_publish('
$StoreInitBody = Get-SourceSlice $Store `
    'int rdx_hogp_subscription_store_init(' `
    'u8 rdx_hogp_subscription_store_get('
$NotifyBody = Get-SourceSlice $HidService `
    'int rdx_hid_report_notify(' `
    'u8 rdx_hid_service_peer_has_persisted_subscription('
$ConnectedBody = Get-SourceSlice $HidService `
    'void rdx_hid_service_on_connected_with_hdl(' `
    'void rdx_hid_service_on_disconnected('
$PersistedSubscriptionBody = Get-SourceSlice $HidService `
    'u8 rdx_hid_service_peer_has_persisted_subscription(' `
    'int rdx_hid_service_peer_subscription_update('
$ServerHidWriteBody = Get-SourceSlice $Server `
    'static int rdx_ble_server_gatt_write_hid(' `
    'static int rdx_ble_server_gatt_write_rejected(' -Last
$LegacyMigrationMatch = [regex]::Match(
    $StoreParseBody,
    '(?s)if\s*\(schema\s*==\s*RDX_HOGP_SUB_VM_LEGACY_SCHEMA\)\s*\{(.*?)\}\s*else'
)
$LegacyMigrationBody = if ($LegacyMigrationMatch.Success) {
    $LegacyMigrationMatch.Groups[1].Value
} else { '' }

Assert-Contract 'BONDED_CCC_HCS2_SCHEMA' `
    ($Store -match '#define\s+RDX_HOGP_SUB_VM_MAGIC\s+"HCS2"' -and
     $Store -match '#define\s+RDX_HOGP_SUB_VM_SCHEMA\s+0x02' -and
     $Store -match '#define\s+RDX_HOGP_SUB_VM_RECORD_LEN\s+48' -and
     $Store -match '#define\s+RDX_HOGP_SUB_VM_ENTRY_LEN\s+8' -and
     $StoreHeader -match '#define\s+RDX_HOGP_SUBSCRIPTION_KEYBOARD\s+0x01' -and
     $StoreHeader -match '#define\s+RDX_HOGP_SUBSCRIPTION_CODEX\s+0x02' -and
     $StoreBuildBody -match 'record\[offset\s*\+\s*1\]\s*=\s*cache->entries\[i\]\.subscription_bits' -and
     $StoreParseBody -match 'subscription_bits\s*=\s*record\[offset\s*\+\s*1\]') `
    'HCS2 must preserve the 48-byte A/B record while assigning independent ID 1 and ID 6 bits to entry byte 1'

Assert-Contract 'BONDED_CCC_HCS1_MIGRATION' `
    ($Store -match '#define\s+RDX_HOGP_SUB_VM_LEGACY_MAGIC\s+"HCS1"' -and
     $Store -match '#define\s+RDX_HOGP_SUB_VM_LEGACY_SCHEMA\s+0x01' -and
     $LegacyMigrationMatch.Success -and
     $LegacyMigrationBody -match 'RDX_HOGP_SUBSCRIPTION_KEYBOARD' -and
     $LegacyMigrationBody -notmatch 'RDX_HOGP_SUBSCRIPTION_CODEX' -and
     $StoreInitBody -match 'HCS1 migrated to HCS2' -and
     $StoreInitBody -match 'rdx_hogp_subscription_publish\s*\(' -and
     $StoreLoadBody -notmatch 'rdx_hogp_subscription_publish\s*\(' -and
     $HidService -match '(?s)rdx_hid_service_init.*?rdx_hogp_subscription_store_init\s*\(') `
    'HCS1 entries must migrate to keyboard-only HCS2 state without inferring a Codex subscription'

Assert-Contract 'BONDED_CCC_REPORT_BITS_ARE_INDEPENDENT' `
    ($StoreUpdateBody -match 'updated_bits\s*\|=\s*subscription_bit' -and
     $StoreUpdateBody -match 'updated_bits\s*&=\s*~subscription_bit' -and
     $WriteBody -match '(?s)HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE.*?rdx_hid_subscription_update_queue\s*\(\s*RDX_HOGP_SUBSCRIPTION_KEYBOARD' -and
     $WriteBody -match '(?s)HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE.*?rdx_hid_subscription_update_queue\s*\(\s*RDX_HOGP_SUBSCRIPTION_CODEX' -and
     $RestoreBody -match '(?s)RDX_HOGP_SUBSCRIPTION_KEYBOARD.*?HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE' -and
     $RestoreBody -match '(?s)RDX_HOGP_SUBSCRIPTION_CODEX.*?HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE' -and
     ([regex]::Matches($WriteBody, 'rdx_hid_subscription_update_cancel\s*\(').Count -ge 2)) `
    'each CCC must update, restore, disable, and roll back only its own persisted report bit'

Assert-Contract 'HID_IDENTITY_COMES_FROM_LINK_REGISTRY' `
    ($LeUser -match 'get_sm_peer_address_for_hci_handle\s*\(\s*u16\s+hci_handle\s*,\s*u8\s*\*\s*addr\s*\)' -and
     $Server -match '(?s)get_sm_peer_address_for_hci_handle\s*\(\s*con_handle\s*,\s*peer_identity\s*\).*?rdx_ble_session_link_set_peer_identity\s*\(\s*link\s*,\s*peer_identity\s*\)' -and
     $Server -match '(?s)rdx_hid_service_peer_has_persisted_subscription\s*\(\s*link->peer_identity\s*\)' -and
     $Server -match '(?s)rdx_hid_service_on_connected_with_hdl\s*\(.*?link->peer_identity\s*\)' -and
     $ConnectedBody -match 'rdx_hid_peer_identity_set\s*\(\s*peer_identity\s*\)' -and
     $HidService -notmatch 'get_sm_peer_address\s*\(' -and
     $Server -notmatch '(?<!for_hci_handle)get_sm_peer_address\s*\(') `
    'the dual-link registry must resolve SM identity by connection handle and pass it into the owner-scoped HID runtime'

Assert-Contract 'PERSISTED_CCC_AUTO_ATTACH_MATCHES_PERSONA' `
    ($HidService -match '(?s)static\s+u8\s+rdx_hid_supported_subscription_bits.*?TCFG_RDX_CODEX_MICRO_MODE\s*!=\s*RDX_CODEX_MICRO_MODE_VENDOR_ONLY.*?RDX_HOGP_SUBSCRIPTION_KEYBOARD.*?#if\s+TCFG_RDX_CODEX_MICRO_MODE.*?RDX_HOGP_SUBSCRIPTION_CODEX' -and
     $PersistedSubscriptionBody -match '(?s)rdx_hogp_subscription_store_get\s*\(\s*peer_identity\s*\).*?subscription_bits\s*&\s*rdx_hid_supported_subscription_bits\s*\(\s*\)') `
    'bonded reconnect must claim the HID owner only when a persisted CCC bit is supported by the active C0, V1, or C1 persona'

Assert-Contract 'HID_CCC_REJECTS_QUEUED_WRITES' `
    ($Server -match '#define\s+RDX_BLE_PHASE0A_ATT_ERR_REQUEST_NOT_SUPPORTED\s+0x06' -and
     $ServerHidWriteBody -match '(?s)u8\s+input_ccc_write\s*=.*?HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE.*?#if\s+TCFG_RDX_CODEX_MICRO_MODE.*?HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE' -and
     $ServerHidWriteBody -match '(?s)input_ccc_write\s*&&\s*context->transaction_mode\s*!=\s*ATT_TRANSACTION_MODE_NONE.*?RDX_BLE_PHASE0A_ATT_ERR_REQUEST_NOT_SUPPORTED' -and
     $ServerHidWriteBody -match '(?s)input_ccc_write\s*&&\s*context->offset\s*!=\s*0.*?RDX_BLE_PHASE0A_ATT_ERR_INVALID_OFFSET.*?input_ccc_write\s*&&\s*cfg\s*==\s*0x0000') `
    'Input CCC writes must reject queued transactions and nonzero offsets before owner routing or HCS2 persistence'

Assert-Contract 'NON_OWNER_CCC_DISABLE_PERSISTS_OWN_BIT' `
    ($ServerHidWriteBody -match '(?s)cfg\s*==\s*0x0000.*?rdx_ble_session_link_is_hid.*?rdx_hid_service_peer_subscription_update\s*\(\s*link->peer_identity' -and
     $ServerHidWriteBody -match 'link->peer_identity_valid' -and
     $ServerHidWriteBody -match 'RDX_HID_REPORT_KEYBOARD\s*:\s*RDX_HID_REPORT_CODEX' -and
     $HidService -match '(?s)rdx_hid_service_peer_subscription_update.*?rdx_hogp_subscription_store_update\s*\(') `
    'an encrypted non-owner peer must be able to clear only its own persisted report bit without claiming HID'

Assert-Contract 'REPORT_NOTIFY_DOES_NOT_TOUCH_VM' `
    ($NotifyBody -notmatch 'subscription_update_flush|subscription_store|syscfg_' -and
     $NotifyBody -match 'app_ble_att_send_data\s*\(') `
    'report sending must remain a transport-only path and never flush subscription VM state from app_core'

$storeOk = $Makefile.Contains('rdx_hogp_subscription_store.c') -and
           $Syscfg -match '#define\s+VM_RDX_HOGP_SUBSCRIPTION_A\s+165' -and
           $Syscfg -match '#define\s+VM_RDX_HOGP_SUBSCRIPTION_B\s+166' -and
           $Store.Contains('rdx_hogp_subscription_crc32') -and
           $Store.Contains('memcmp(record, readback, sizeof(record))') -and
           $Store.Contains('baseline_revision') -and
           $HidService -match 'rdx_hogp_subscription_store_(?:get|update)\s*\(' -and
           $Vm -match '(?s)rdx_vm_ble_pairing_state_reset.*?rdx_hogp_subscription_store_reset\s*\('
Assert-Contract 'BONDED_CCC_IS_PEER_SCOPED' $storeOk `
    'bonded dual-report CCC intent must use verified A/B records keyed by SM identity and clear with bond reset'

Write-Host 'HOGP profile contracts passed.'
