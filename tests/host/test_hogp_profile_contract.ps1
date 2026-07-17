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

function Get-NormalizedCode {
    param([string]$Text)
    return (($Text -replace '\\', '') -replace '\s+', '')
}

$HeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_profile.h')
$ProfileText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_profile.c')
$KeyboardText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_keyboard.c')
$KeyboardHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_keyboard.h')
$ConfigText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_config.h')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$ServerHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.h')
$SessionHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.h')
$AppText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_app.c')
$MakefileText = Get-Content -Raw (Join-Path $RepoRoot 'SDK/Makefile')

$Handles = [ordered]@{
    HID_SERVICE_HANDLE                           = 0x0016
    HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE      = 0x0017
    HID_PROTOCOL_MODE_VALUE_HANDLE               = 0x0018
    HID_INPUT_REPORT_CHARACTERISTIC_HANDLE       = 0x0019
    HID_INPUT_REPORT_VALUE_HANDLE                = 0x001a
    HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE = 0x001b
    HID_INPUT_REPORT_REFERENCE_HANDLE            = 0x001c
    HID_REPORT_MAP_CHARACTERISTIC_HANDLE         = 0x001d
    HID_REPORT_MAP_VALUE_HANDLE                  = 0x001e
    HID_INFORMATION_CHARACTERISTIC_HANDLE        = 0x001f
    HID_INFORMATION_VALUE_HANDLE                 = 0x0020
    HID_CONTROL_POINT_CHARACTERISTIC_HANDLE      = 0x0021
    HID_CONTROL_POINT_VALUE_HANDLE               = 0x0022
    HID_OUTPUT_REPORT_CHARACTERISTIC_HANDLE      = 0x0023
    HID_OUTPUT_REPORT_VALUE_HANDLE               = 0x0024
    HID_OUTPUT_REPORT_REFERENCE_HANDLE            = 0x0025
}
foreach ($entry in $Handles.GetEnumerator()) {
    $escaped = [regex]::Escape($entry.Key)
    $match = [regex]::Match($HeaderText, "#define\s+$escaped\s+(0x[0-9A-Fa-f]+)")
    $actual = if ($match.Success) { [convert]::ToInt32($match.Groups[1].Value, 16) } else { -1 }
    Test-Contract ("HANDLE_" + $entry.Key) ($actual -eq $entry.Value) `
        "expected 0x$($entry.Value.ToString('X4')), found 0x$($actual.ToString('X4'))"
}

Test-Contract 'REPORT_MAP_LENGTH' `
    ($HeaderText -match '#define\s+RDX_HOGP_REPORT_MAP_LEN\s+\(?70\)?') `
    'Report Map length must remain 70 bytes'

$ExpectedMap = @(
    0x05,0x01,0x09,0x06,0xA1,0x01,0x85,0x01,0x05,0x07,
    0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,0x75,0x01,
    0x95,0x08,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x01,
    0x95,0x06,0x75,0x08,0x15,0x00,0x26,0xFF,0x00,0x05,
    0x07,0x19,0x00,0x29,0xFF,0x81,0x00,0x05,0x08,0x19,
    0x01,0x29,0x03,0x15,0x00,0x25,0x01,0x95,0x03,0x75,
    0x01,0x91,0x02,0x95,0x01,0x75,0x05,0x91,0x01,0xC0
)
$mapMatch = [regex]::Match($ProfileText,
    '(?s)const\s+u8\s+rdx_hogp_report_map\[\]\s*=\s*\{(.*?)\};')
$ActualMap = @()
if ($mapMatch.Success) {
    $ActualMap = [regex]::Matches($mapMatch.Groups[1].Value, '0x([0-9A-Fa-f]{2})') |
        ForEach-Object { [convert]::ToInt32($_.Groups[1].Value, 16) }
}
$mapOk = $ActualMap.Count -eq $ExpectedMap.Count
if ($mapOk) {
    for ($i = 0; $i -lt $ExpectedMap.Count; $i++) {
        if ($ActualMap[$i] -ne $ExpectedMap[$i]) { $mapOk = $false; break }
    }
}
Test-Contract 'REPORT_MAP_BYTES' $mapOk 'Report Map byte contract changed'

$NormalizedHeader = Get-NormalizedCode $HeaderText
$NormalizedServer = Get-NormalizedCode $ServerText
$profileTokens = @(
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
$last = -1
$orderOk = $true
foreach ($token in $profileTokens) {
    $next = $NormalizedServer.IndexOf($token, $last + 1)
    if ($next -le $last) { $orderOk = $false; break }
    $last = $next
}
Test-Contract 'PROFILE_ATTRIBUTE_ORDER' $orderOk `
    'HID attributes must retain the complete ordered macro invocation contract'

$profileByteContracts = @(
    '#defineRDX_HOGP_UUID_HID_SERVICE0x1812',
    '#defineRDX_HOGP_UUID_PROTOCOL_MODE0x2A4E',
    '#defineRDX_HOGP_UUID_REPORT0x2A4D',
    '#defineRDX_HOGP_UUID_REPORT_MAP0x2A4B',
    '#defineRDX_HOGP_UUID_HID_INFORMATION0x2A4A',
    '#defineRDX_HOGP_UUID_HID_CONTROL_POINT0x2A4C',
    '#defineRDX_HOGP_UUID_REPORT_REFERENCE0x2908',
    '#defineRDX_HOGP_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION0x2902',
    '#defineRDX_HOGP_UUID_PRIMARY_SERVICE0x2800',
    '#defineRDX_HOGP_UUID_CHARACTERISTIC0x2803',
    '#defineRDX_HOGP_CHAR_PROP_PROTOCOL_MODE(RDX_HOGP_ATT_PROP_READ|RDX_HOGP_ATT_PROP_WRITE_WITHOUT_RESPONSE)',
    '#defineRDX_HOGP_CHAR_PROP_INPUT_REPORT(RDX_HOGP_ATT_PROP_READ|RDX_HOGP_ATT_PROP_NOTIFY)',
    '#defineRDX_HOGP_CHAR_PROP_REPORT_MAPRDX_HOGP_ATT_PROP_READ',
    '#defineRDX_HOGP_CHAR_PROP_HID_INFORMATIONRDX_HOGP_ATT_PROP_READ',
    '#defineRDX_HOGP_CHAR_PROP_CONTROL_POINTRDX_HOGP_ATT_PROP_WRITE_WITHOUT_RESPONSE',
    '#defineRDX_HOGP_CHAR_PROP_OUTPUT_REPORT(RDX_HOGP_ATT_PROP_READ|RDX_HOGP_ATT_PROP_WRITE_WITHOUT_RESPONSE|RDX_HOGP_ATT_PROP_WRITE)',
    '#defineRDX_HOGP_CCC_DEFAULT_VALUE0x0000',
    '#defineRDX_HOGP_INPUT_REPORT_ID0x01',
    '#defineRDX_HOGP_INPUT_REPORT_TYPE0x01',
    '#defineRDX_HOGP_OUTPUT_REPORT_ID0x01',
    '#defineRDX_HOGP_OUTPUT_REPORT_TYPE0x02',
    '#defineRDX_HOGP_ATT_PRIMARY_SERVICE_16(handle,service_uuid)RDX_HOGP_ATT_HEADER(0x000a,0x0002,(handle),RDX_HOGP_UUID_PRIMARY_SERVICE),RDX_HOGP_ATT_U16_LE(service_uuid)',
    '#defineRDX_HOGP_ATT_CHARACTERISTIC_16(handle,properties,value_handle,char_uuid)RDX_HOGP_ATT_HEADER(0x000d,0x0002,(handle),RDX_HOGP_UUID_CHARACTERISTIC),((u8)(properties)),RDX_HOGP_ATT_U16_LE(value_handle),RDX_HOGP_ATT_U16_LE(char_uuid)',
    '#defineRDX_HOGP_ATT_VALUE_16(handle,flags,value_uuid)RDX_HOGP_ATT_HEADER(0x0008,(flags),(handle),(value_uuid))',
    '#defineRDX_HOGP_ATT_CCC(handle,value)RDX_HOGP_ATT_HEADER(0x000a,0x010a,(handle),RDX_HOGP_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION),RDX_HOGP_ATT_U16_LE(value)',
    '#defineRDX_HOGP_ATT_REPORT_REFERENCE(handle,report_id,report_type)RDX_HOGP_ATT_HEADER(0x000a,0x0002,(handle),RDX_HOGP_UUID_REPORT_REFERENCE),((u8)(report_id)),((u8)(report_type))'
)
$profileBytesOk = $true
foreach ($contract in $profileByteContracts) {
    if (-not $NormalizedHeader.Contains($contract)) {
        $profileBytesOk = $false
        break
    }
}
Test-Contract 'PROFILE_BYTE_VALUES' $profileBytesOk `
    'HID UUIDs, properties, descriptor values and ATT byte templates must remain frozen'

$SendBody = Get-FunctionBody $KeyboardText 'int\s+rdx_hogp_keyboard_report_send\s*\([^)]*\)'
Test-Contract 'INPUT_REPORT_8_BYTES_NO_PREFIX' `
    ($KeyboardHeaderText -match '#define\s+RDX_HOGP_KEYBOARD_REPORT_LEN\s+8' -and
     $SendBody -match 'u8\s+payload\s*\[\s*RDX_HOGP_KEYBOARD_REPORT_LEN\s*\]' -and
     $SendBody -match 'HID_INPUT_REPORT_VALUE_HANDLE\s*,\s*payload\s*,\s*sizeof\s*\(\s*payload\s*\)' -and
     $SendBody -notmatch 'REPORT_ID') `
    'Input Report must remain an 8-byte payload without a Report ID prefix'

$ConnectedBody = Get-FunctionBody $KeyboardText 'u8\s+rdx_hogp_keyboard_is_connected\s*\([^)]*\)'
$ReadyBody = Get-FunctionBody $KeyboardText 'u8\s+rdx_hogp_keyboard_is_ready\s*\([^)]*\)'
$KeyRouteBody = Get-FunctionBody $AppText 'void\s+rdx_app_earphone_key_remap\s*\([^)]*\)'
$Phase2bText = $ServerText + "`n" + $ServerHeaderText + "`n" + $SessionHeaderText +
               "`n" + $KeyboardText + "`n" + $KeyboardHeaderText + "`n" + $AppText
$ModeCPath = Join-Path $ProtocolDir 'rdx_ble_mode_controller.c'
$ModeHPath = Join-Path $ProtocolDir 'rdx_ble_mode_controller.h'

Test-Contract 'PHASE2B_MODE_OWNER_REMOVED' `
    (-not (Test-Path $ModeCPath) -and -not (Test-Path $ModeHPath) -and
     $MakefileText -notmatch 'rdx_ble_mode_controller\.c' -and
     $Phase2bText -notmatch 'RDX_BLE_OWNER|RDX_BLE_MODE|connection_owner|rdx_ble_mode_request') `
    'CONFIG/HOGP mode and owner runtime must be deleted'

Test-Contract 'PHASE2B_HID_CONNECTED_CAPABILITY' `
    ($ConnectedBody -match 's_hogp_connected' -and $ConnectedBody -notmatch 'owner|mode') `
    'HID connected must represent attached capability state only'

Test-Contract 'PHASE2B_HID_READY_ONLINE_BOUNDARY' `
    ($ReadyBody -match 's_hogp_connected' -and
     $ReadyBody -match 's_hid_notify_enabled' -and
     $ReadyBody -match 's_hogp_suspended' -and
     $ReadyBody -match 's_hogp_encrypted' -and
     $ReadyBody -notmatch 'owner|mode') `
    'online HID requires attached link, CCC, encryption and non-suspend'

$ReadyDropBody = Get-FunctionBody $KeyboardText 'static\s+void\s+rdx_hogp_ready_drop_cleanup\s*\([^)]*\)'
Test-Contract 'PHASE2B_HID_READY_DROP_CLEANUP' `
    ($ReadyDropBody -match 'rdx_hogp_key_action_reset\s*\(\s*\)' -and
     $ReadyDropBody -match 'rdx_hogp_current_report_clear\s*\(\s*\)' -and
     ([regex]::Matches($KeyboardText, 'rdx_hogp_ready_drop_cleanup\s*\(\s*\)')).Count -ge 5) `
    'all online-to-offline transitions must cancel delayed release and converge the report'

Test-Contract 'PHASE2B_OFFLINE_KEY_FALLBACK' `
    ($KeyRouteBody -match 'rdx_hogp_keyboard_is_ready\s*\(\s*\)' -and
     $KeyRouteBody -notmatch 'rdx_hogp_keyboard_is_connected\s*\(\s*\)' -and
     $KeyRouteBody -notmatch 'rdx_ble_mode_request_toggle') `
    'HID-not-ready links must continue through the offline key table'

Test-Contract 'PHASE2B_HID_ATTACH_BY_ACCESS' `
    ($ServerText -match 'static\s+u8\s+rdx_ble_server_hogp_attach\s*\(' -and
     ([regex]::Matches($ServerText, 'rdx_ble_server_hogp_attach\s*\(\s*connection_handle\s*\)')).Count -ge 2) `
    'HID read/write must attach capability without claiming an owner'

$WriteBody = Get-SourceSlice $KeyboardText 'int rdx_hogp_att_write(' '/******************************************************************************'
Test-Contract 'HID_CCC_STRICT_VALIDATION' `
    ($WriteBody -match 'HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE' -and
     $WriteBody -match 'offset\s*!=\s*0' -and
     $WriteBody -match 'buffer_size\s*!=\s*2' -and
     $WriteBody -match 'cfg\s*!=\s*0x0000\s*&&\s*cfg\s*!=\s*0x0001' -and
     $WriteBody -match 'RDX_HOGP_ATT_ERR_INVALID_OFFSET' -and
     $WriteBody -match 'RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN' -and
     $WriteBody -match 'RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED') `
    'HID CCC must accept only offset 0, length 2 and values 0x0000/0x0001'

$HogpConnectedBody = Get-SourceSlice $KeyboardText 'void rdx_hogp_on_connected(' 'void rdx_hogp_on_disconnected('
Test-Contract 'HID_PAIRING_DRIVEN_BY_CCC_ENABLE' `
    (-not $HogpConnectedBody.Contains('sm_api_request_pairing') -and
     $WriteBody.Contains('sm_api_request_pairing(connection_handle);')) `
    'static HID discovery must not pair; a valid CCC enable may request encryption'

Test-Contract 'HID_PROTOCOL_MODE_STRICT' `
    ($WriteBody -match 'HID_PROTOCOL_MODE_VALUE_HANDLE' -and
     $WriteBody -match 'buffer_size\s*!=\s*1' -and
     $WriteBody -match 'buffer\[0\]\s*!=\s*RDX_HOGP_PROTOCOL_MODE_REPORT' -and
     $WriteBody -match 's_hid_protocol_mode\s*=\s*buffer\[0\]') `
    'Protocol Mode must accept only the implemented Report protocol value'

Test-Contract 'HID_CONTROL_POINT_STRICT' `
    ($WriteBody -match 'HID_CONTROL_POINT_VALUE_HANDLE' -and
     $WriteBody -match 'RDX_HOGP_CONTROL_POINT_SUSPEND' -and
     $WriteBody -match 'RDX_HOGP_CONTROL_POINT_EXIT_SUSPEND' -and
     $WriteBody -match 's_hogp_suspended\s*=\s*1' -and
     $WriteBody -match 's_hogp_suspended\s*=\s*0') `
    'Control Point must strictly drive suspend/exit-suspend state'

Test-Contract 'HID_OUTPUT_REPORT_STRICT' `
    ($WriteBody -match 'HID_OUTPUT_REPORT_VALUE_HANDLE' -and
     $WriteBody -match 'offset\s*!=\s*0' -and
     $WriteBody -match 'buffer_size\s*!=\s*1' -and
     $WriteBody -match 's_hid_output_report\s*=\s*buffer\[0\]') `
    'Output Report must validate offset and one-byte payload'

$EncryptionBody = Get-SourceSlice $KeyboardText 'void rdx_hogp_on_encryption_change(' 'void rdx_hogp_on_sm_event('
Test-Contract 'HID_ENCRYPTION_TRANSITION' `
    ($EncryptionBody -match 'con_handle\s*!=\s*s_hid_con_handle' -and
     $EncryptionBody -match 'enabled\s*&&\s*status\s*==\s*0' -and
     $EncryptionBody -match 'rdx_hogp_ready_drop_cleanup\s*\(\s*\)' -and
     $EncryptionBody -match 's_hogp_encrypted\s*=\s*encrypted') `
    'Encryption events must be handle-scoped and cleanly drop ready on failure'

Test-Contract 'HID_REPORT_STATE_SYNC' `
    ($SendBody.Contains('if (ret == APP_BLE_NO_ERROR)') -and
     $SendBody.Contains('rdx_hogp_current_report_set') -and
     $KeyboardText.Contains('rdx_hogp_keyboard_report_t report = {0};') -and
     $KeyboardText.Contains('rdx_hogp_keyboard_report_send(&report)')) `
    'current report may update only after successful send and release_all must send zero'

$disabledStart = $KeyboardText.IndexOf('#else  /* !(TCFG_RDX_HOGP_ENABLE')
$disabledEnd = if ($disabledStart -ge 0) {
    $KeyboardText.IndexOf('#endif /* TCFG_RDX_HOGP_ENABLE', $disabledStart)
} else { -1 }
$disabledBranch = if ($disabledStart -ge 0 -and $disabledEnd -gt $disabledStart) {
    $KeyboardText.Substring($disabledStart, $disabledEnd - $disabledStart)
} else { '' }
$requiredStubs = @(
    'rdx_hogp_init', 'rdx_hogp_deinit', 'rdx_hogp_runtime_cleanup',
    'rdx_hogp_att_read', 'rdx_hogp_att_write', 'rdx_hogp_on_connected',
    'rdx_hogp_on_disconnected', 'rdx_hogp_on_encryption_change',
    'rdx_hogp_keyboard_report_send', 'rdx_hogp_keyboard_release_all',
    'rdx_hogp_keyboard_is_connected', 'rdx_hogp_keyboard_is_ready'
)
$stubsOk = -not [string]::IsNullOrWhiteSpace($disabledBranch)
foreach ($stub in $requiredStubs) {
    if ($disabledBranch -notmatch ([regex]::Escape($stub) + '\s*\(')) { $stubsOk = $false }
}
Test-Contract 'HOGP_DISABLED_PROFILE_STUBS' $stubsOk `
    'disabled HOGP builds must retain the complete public API stub surface'

Test-Contract 'HOGP_CONFIG_CONTRACT' `
    ($ConfigText.Contains('#include "app_config.h"') -and
     $ConfigText.Contains('#define TCFG_RDX_HOGP_KEY_UP_DELAY_MS         20') -and
     $KeyboardText.Contains('#define RDX_HOGP_ATT_ERR_INVALID_OFFSET') -and
     $KeyboardText.Contains('#define RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN') -and
     $KeyboardText.Contains('#define RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED') -and
     -not $KeyboardText.Contains('return ATT_ERROR_')) `
    'HOGP config must inherit project overrides, preserve key-up delay and use local ATT errors'

Test-Contract 'PHASE2B_AUTH_AND_DEDICATED_ADV_REMOVED' `
    ($Phase2bText -notmatch 'TCFG_RDX_SESSION_AUTH_GATE_ENABLE|rdx_protocol_session_|RDX_SESSION_AUTHORIZED' -and
     $KeyboardText -notmatch 'rdx_hogp_adv_start|rdx_hogp_adv_stop|rdx_hogp_fill_adv_data|hogp_adv_start_internal') `
    'unused auth state and standalone HOGP advertising paths must be removed'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All HOGP profile contract checks passed.'
    exit 0
}
Write-Host "$Failed HOGP profile contract checks failed."
exit 1
