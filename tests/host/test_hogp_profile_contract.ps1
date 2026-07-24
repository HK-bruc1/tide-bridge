#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$ProtocolRoot = 'SDK\apps\common\third_party_profile\rdx_protocol'
$Header = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_profile.h"
$Profile = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_profile.c"
$Keyboard = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keyboard.c"
$KeyboardHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keyboard.h"
$Config = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_config.h"
$Server = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_server.c"
$Store = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_subscription_store.c"
$Vm = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_vm.c"
$Syscfg = Read-RepoFile $RepoRoot 'SDK\interface\utils\syscfg_id.h'
$Makefile = Read-RepoFile $RepoRoot 'SDK\Makefile'
$MultiProtocol = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\multi_protocol_main.c'
$NormalizedHeader = (($Header -replace '\\', '') -replace '\s+', '')
$NormalizedServer = (($Server -replace '\\', '') -replace '\s+', '')

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
        $Header,
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
    '(?s)const\s+u8\s+rdx_hogp_report_map\[\]\s*=\s*\{(.*?)\};'
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
    (Test-TokensInOrder $NormalizedServer $ProfileTokens) `
    'HID attributes must retain their externally visible order'

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

$WriteBody = Get-SourceSlice $Keyboard `
    'int rdx_hogp_att_write(' `
    '/******************************************************************************'
$ReadyBody = Get-SourceSlice $Keyboard `
    'u8 rdx_hogp_keyboard_is_ready(' `
    'int rdx_hogp_keyboard_report_send('
$ReadyDropBody = Get-SourceSlice $Keyboard `
    'static void rdx_hogp_ready_drop_cleanup(' `
    'static void rdx_hogp_peer_identity_reset('
Assert-Contract 'HID_CCC_AND_READY_BOUNDARY' `
    ($WriteBody -match 'offset\s*!=\s*0' -and
     $WriteBody -match 'buffer_size\s*!=\s*2' -and
     $WriteBody -match 'cfg\s*!=\s*0x0000\s*&&\s*cfg\s*!=\s*0x0001' -and
     $WriteBody -match '!s_hogp_encrypted' -and
     $ReadyBody -match 's_hogp_connected' -and
     $ReadyBody -match 's_hid_notify_enabled' -and
     $ReadyBody -match 's_hogp_encrypted' -and
     $ReadyBody -match 's_hogp_suspended' -and
     $ReadyDropBody -match 'rdx_hogp_key_action_reset\s*\(\s*\)' -and
     $ReadyDropBody -match 'rdx_hogp_current_report_clear\s*\(\s*\)') `
    'HID is ready only for encrypted CCC 0x0001 while not suspended, and every ready drop clears key state'

$storeOk = $Makefile.Contains('rdx_hogp_subscription_store.c') -and
           $Syscfg -match '#define\s+VM_RDX_HOGP_SUBSCRIPTION_A\s+165' -and
           $Syscfg -match '#define\s+VM_RDX_HOGP_SUBSCRIPTION_B\s+166' -and
           $Store.Contains('rdx_hogp_subscription_crc32') -and
           $Store.Contains('memcmp(record, readback, sizeof(record))') -and
           $Store.Contains('baseline_revision') -and
           $Keyboard -match 'get_sm_peer_address\s*\(' -and
           $Keyboard -match 'rdx_hogp_subscription_store_(?:set|contains)\s*\(' -and
           $Vm -match '(?s)rdx_vm_ble_pairing_state_reset.*?rdx_hogp_subscription_store_reset\s*\('
Assert-Contract 'BONDED_CCC_IS_PEER_SCOPED' $storeOk `
    'bonded CCC intent must use verified A/B records keyed by SM identity and clear with bond reset'

Write-Host 'HOGP profile contracts passed.'
