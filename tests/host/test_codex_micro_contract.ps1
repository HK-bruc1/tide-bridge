#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$ProtocolRoot = 'SDK\apps\common\third_party_profile\rdx_protocol'
$Overlay = Read-RepoFile $RepoRoot 'SDK\apps\earphone\include\t2620_project_config.h'
$Config = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_config.h"
$Header = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_profile.h"
$Profile = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_profile.c"
$GattProfile = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_gatt_profile.c"
$GattProfileHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_gatt_profile.h"
$HidFragment = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hid_profile.inc"
$DisFragment = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_dis_profile.inc"
$HidService = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hid_service.c"
$HidServiceHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hid_service.h"
$Keyboard = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keyboard.c"
$Server = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_server.c"
$App = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_app.c"
$Codex = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_codex_micro.c"
$CodexHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_codex_micro.h"
$Router = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_input_router.c"
$RouterHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_input_router.h"
$AppConfig = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_app_config.h"
$Makefile = Read-RepoFile $RepoRoot 'SDK\Makefile'
$Tasks = Read-RepoFile $RepoRoot 'SDK\.vscode\tasks.json'
$Runner = Read-RepoFile $RepoRoot 'tests\host\run_host_tests.ps1'
$Tool = Read-RepoFile $RepoRoot 'tools\windows\codex_micro_hid\codex_micro_hid.cpp'
$BuildTool = Read-RepoFile $RepoRoot 'tools\windows\codex_micro_hid\build_hid_tool.ps1'
$CaptureTool = Read-RepoFile $RepoRoot 'tools\windows\codex_micro_hid\capture_ble_advertisements.ps1'
$EvidenceTool = Read-RepoFile $RepoRoot 'tools\windows\codex_micro_hid\new_evidence_session.ps1'
$NormalizedHeader = (($Header -replace '\\', '') -replace '\s+', '')
$NormalizedServer = (($Server -replace '\\', '') -replace '\s+', '')
$NormalizedHidFragment = (($HidFragment -replace '\\', '') -replace '\s+', '')
$DefaultNameBody = [regex]::Match(
    $Server,
    '(?s)static u8 rdx_ble_server_default_local_name_build\(char \*name\).*?\n}'
).Value
$GetNameBody = [regex]::Match(
    $Server,
    '(?s)char\* rdx_ble_server_get_local_name\(void\).*?\n}'
).Value

Assert-Contract 'PRODUCT_DEFAULTS_AND_IDENTITY_GUARD' `
    ($Overlay -match '#define\s+TCFG_RDX_CODEX_MICRO_MODE\s+2' -and
     $Overlay -match '#define\s+TCFG_RDX_CODEX_MICRO_TEST_IDENTITY_ENABLE\s+1' -and
     $Overlay -match '#define\s+TCFG_RDX_CODEX_MICRO_EXPERIMENTAL_BUILD_ENABLE\s+1' -and
     $Overlay -notmatch '(?m)^\s*#\s*(?:define|undef)\s+RDX_HOGP_KEY_ACTION_TEST_ENABLE\b' -and
     $AppConfig -match '#define\s+RDX_HOGP_KEY_ACTION_TEST_ENABLE\s+1' -and
     $AppConfig -match '1 selects the built-in five-key map; 0 restores APP/VM keymap ownership' -and
     $Overlay -match 'Codex Micro reference identity requires an explicit experimental build' -and
     $Config -match '#define\s+RDX_CODEX_MICRO_MODE_VENDOR_ONLY\s+1' -and
     $Config -match '#define\s+RDX_CODEX_MICRO_MODE_COMPOSITE\s+2') `
    'all must be the controlled composite laboratory image with the hardcoded Fast map and guarded reference identity'

Assert-Contract 'PRODUCT_NAME_IS_UNIFIED' `
    ($DefaultNameBody -match 'BLE_LOCAL_NAME' -and
     $DefaultNameBody -match 'p->auth\s*\+\s*20' -and
     $DefaultNameBody -notmatch 'TCFG_RDX_CODEX_MICRO' -and
     $GetNameBody -match 'syscfg_read\(VM_RDX_BLE_NAME' -and
     $GetNameBody -match 'rdx_ble_server_default_local_name_build\(tmp\)' -and
     $GetNameBody -notmatch 'TCFG_RDX_CODEX_MICRO' -and
     $AppConfig -match '#define\s+BLE_LOCAL_NAME\s+"Beanstalk RKB"' -and
     $Server -notmatch '"Codex Micro"' -and
     $Overlay -notmatch 'TCFG_RDX_CODEX_MICRO_REFERENCE_NAME_ENABLE' -and
     $Makefile -notmatch 'codex-c1-name-test' -and
     $Tasks -notmatch 'codex-c1-name-test') `
    'all personas must use the product BLE name, authentication suffix and VM configuration path'

$VendorBytes = @(
    0x06,0x00,0xFF,0x09,0x01,0xA1,0x01,0x85,0x06,0x15,
    0x00,0x26,0xFF,0x00,0x75,0x08,0x95,0x3F,0x09,0x01,
    0x81,0x02,0x95,0x3F,0x09,0x02,0x91,0x02,0xC0
)
$VendorBlock = [regex]::Match(
    $Profile,
    '(?s)#if\s+TCFG_RDX_CODEX_MICRO_MODE\s*!=\s*RDX_CODEX_MICRO_MODE_DISABLED(.*?)#endif'
)
$ActualVendor = if ($VendorBlock.Success) {
    $VendorSource = [regex]::Replace(
        $VendorBlock.Groups[1].Value, '(?m)//.*$', '')
    @([regex]::Matches($VendorSource, '0x([0-9A-Fa-f]{2})') |
        ForEach-Object { [convert]::ToInt32($_.Groups[1].Value, 16) })
} else { @() }
$vendorOk = $ActualVendor.Count -eq $VendorBytes.Count
for ($i = 0; $vendorOk -and $i -lt $VendorBytes.Count; $i++) {
    $vendorOk = $ActualVendor[$i] -eq $VendorBytes[$i]
}
Assert-Contract 'REPORT_MAP_VARIANTS' `
    ($vendorOk -and
     $Header -match '(?s)VENDOR_ONLY.*?RDX_HOGP_REPORT_MAP_LEN\s+\(29\).*?COMPOSITE.*?RDX_HOGP_REPORT_MAP_LEN\s+\(99\).*?RDX_HOGP_REPORT_MAP_LEN\s+\(70\)') `
    'V1 must be the exact 29-byte ID 6 map and C1 must append it to the frozen 70-byte keyboard map'

$Handles = [ordered]@{
    HID_CODEX_INPUT_REPORT_CHARACTERISTIC_HANDLE = 0x0026
    HID_CODEX_INPUT_REPORT_VALUE_HANDLE = 0x0027
    HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE = 0x0028
    HID_CODEX_INPUT_REPORT_REFERENCE_HANDLE = 0x0029
    HID_CODEX_OUTPUT_REPORT_CHARACTERISTIC_HANDLE = 0x002a
    HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE = 0x002b
    HID_CODEX_OUTPUT_REPORT_REFERENCE_HANDLE = 0x002c
}
$handlesOk = $true
foreach ($entry in $Handles.GetEnumerator()) {
    $match = [regex]::Match($GattProfileHeader,
        '#define\s+' + [regex]::Escape($entry.Key) + '\s+(0x[0-9A-Fa-f]+)')
    $handlesOk = $handlesOk -and $match.Success -and
        ([convert]::ToInt32($match.Groups[1].Value, 16) -eq $entry.Value)
}
$CodexTokens = @(
    'RDX_HOGP_ATT_CHARACTERISTIC_16(HID_CODEX_INPUT_REPORT_CHARACTERISTIC_HANDLE,RDX_HOGP_CHAR_PROP_INPUT_REPORT,HID_CODEX_INPUT_REPORT_VALUE_HANDLE,RDX_HOGP_UUID_REPORT)',
    'RDX_HOGP_ATT_VALUE_16(HID_CODEX_INPUT_REPORT_VALUE_HANDLE,RDX_HOGP_ATT_FLAGS_INPUT_REPORT_VALUE,RDX_HOGP_UUID_REPORT)',
    'RDX_HOGP_ATT_CCC(HID_CODEX_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE,RDX_HOGP_CCC_DEFAULT_VALUE)',
    'RDX_HOGP_ATT_REPORT_REFERENCE(HID_CODEX_INPUT_REPORT_REFERENCE_HANDLE,RDX_CODEX_MICRO_REPORT_ID,RDX_CODEX_MICRO_INPUT_REPORT_TYPE)',
    'RDX_HOGP_ATT_CHARACTERISTIC_16(HID_CODEX_OUTPUT_REPORT_CHARACTERISTIC_HANDLE,RDX_HOGP_CHAR_PROP_OUTPUT_REPORT,HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE,RDX_HOGP_UUID_REPORT)',
    'RDX_HOGP_ATT_VALUE_16(HID_CODEX_OUTPUT_REPORT_VALUE_HANDLE,RDX_HOGP_ATT_FLAGS_OUTPUT_REPORT_VALUE,RDX_HOGP_UUID_REPORT)',
    'RDX_HOGP_ATT_REPORT_REFERENCE(HID_CODEX_OUTPUT_REPORT_REFERENCE_HANDLE,RDX_CODEX_MICRO_REPORT_ID,RDX_CODEX_MICRO_OUTPUT_REPORT_TYPE)'
)
Assert-Contract 'CODEX_ATTRIBUTE_LAYOUT' `
    ($handlesOk -and (Test-TokensInOrder $NormalizedHidFragment $CodexTokens) -and
     $NormalizedHeader.Contains('#defineRDX_CODEX_MICRO_REPORT_ID0x06') -and
     $NormalizedHeader.Contains('#defineRDX_CODEX_MICRO_REPORT_BODY_LEN63') -and
     $NormalizedHeader.Contains('#defineRDX_CODEX_MICRO_REPORT_DATA_LEN61')) `
    'ID 6 Input/CCC/Output attributes and Report References must retain the MVP handle layout'

Assert-Contract 'DIS_AND_IDENTITY_POLICY' `
    ($GattProfileHeader -match '(?s)#if\s+TCFG_RDX_CODEX_MICRO_MODE.*?DIS_SERVICE_HANDLE\s+0x002d.*?DIS_MANUFACTURER_NAME_VALUE_HANDLE\s+0x0031' -and
     $DisFragment -match '0x02,\s*0x3a,\s*0x30,\s*0x60,\s*0x83,\s*0x01,\s*0x01' -and
     $DisFragment -match "'W',\s*'o',\s*'r',\s*'k',\s*' ',\s*'L',\s*'o',\s*'u',\s*'d',\s*'e',\s*'r'" -and
     $DisFragment -match '0x02,\s*0x34,\s*0x12,\s*0x01,\s*0x00,\s*0x01,\s*0x00' -and
     $DisFragment -match "'J',\s*'i',\s*'e',\s*'L',\s*'i'") `
    'DIS identity must remain policy-gated independently of the product advertising layout'

$WriteBody = Get-SourceSlice $Codex `
    'int rdx_codex_micro_output_write(' `
    'static u8 rdx_codex_id_copy('
$TxBody = Get-SourceSlice $Codex `
    'static void rdx_codex_tx_pump(' `
    'static int rdx_codex_tx_enqueue('
Assert-Contract 'FRAMING_AND_BOUNDS' `
    ($Codex -match '#define\s+RDX_CODEX_RX_MAX\s+1024' -and
     $Codex -match '#define\s+RDX_CODEX_TX_DEPTH\s+4' -and
     $Codex -match '#define\s+RDX_CODEX_TX_ITEM_MAX\s+512' -and
     $WriteBody -match 'buffer_size\s*!=\s*RDX_CODEX_MICRO_REPORT_BODY_LEN' -and
     $WriteBody -match 'buffer\[0\]\s*!=\s*0x02' -and
     $WriteBody -match 'buffer\[1\]\s*>\s*RDX_CODEX_MICRO_REPORT_DATA_LEN' -and
     $WriteBody -match 'rdx_codex_json_feed' -and
     $Codex -match '#define\s+RDX_CODEX_JSON_DEPTH_MAX\s+8' -and
     $Codex -match 's_codex_rx.depth\s*>\s*RDX_CODEX_JSON_DEPTH_MAX' -and
     $Codex -match 'rdx_codex_rx_timeout,\s*2000' -and
     $Codex -match "item->data\[len\+\+\]\s*=\s*'\\n'" -and
     $TxBody -match 'report\[0\]\s*=\s*0x02' -and
     $TxBody -match 'report\[1\]\s*=\s*chunk') `
    'ATT bodies must use [02][N], bounded complete-JSON reassembly, LF TX and 63-byte chunks'

Assert-Contract 'OWNER_GENERATION_AND_BACKPRESSURE' `
    ($Codex -match 'rdx_ble_session_get_hid_link' -and
     $Codex -match 'rdx_ble_session_token_capture' -and
     $Codex -match 'rdx_ble_session_link_token_resolve' -and
     $Codex -match 'rdx_ble_session_link_is_hid' -and
     $WriteBody -match 'link->con_handle\s*!=\s*connection_handle' -and
     $TxBody -match 'APP_BLE_BUFF_FULL' -and
     $TxBody -match 'att_server_request_can_send_now_event' -and
     $Server -match '(?s)case\s+ATT_EVENT_CAN_SEND_NOW:.*?rdx_codex_micro_on_can_send_now' -and
     $Codex -notmatch 'delay\s*\(') `
    'wrong-owner/stale work must fail closed and buffer-full must retry asynchronously without blocking delay'

Assert-Contract 'RPC_ALLOWLIST' `
    ($Codex -match '"device.status"' -and
     $Codex -match '"sys.version"' -and
     $Codex -match '"v.oai.thstatus"' -and
     $Codex -match '"v.oai.rgbcfg"' -and
     $Codex -match '"lights.preview"' -and
     $Codex -match '"host.focused_app"' -and
     $Codex -match '"layer_index",\s*1' -and
     $Codex -match '-32601,\s*"Method not found"' -and
     $Codex -match '(?s)!strcmp\(method,\s*"v\.oai\.thstatus"\).*?return\s+cJSON_IsArray\(params\)' -and
     $Codex -match '(?s)!strcmp\(method,\s*"v\.oai\.rgbcfg"\).*?return\s+cJSON_IsObject\(params\)' -and
     $Codex -match '-32602,\s*"Invalid params"' -and
     $Codex -notmatch 'rdx_codex_light_valid' -and
     $Codex -notmatch '"fs\.') `
    'only the MVP RPC methods are accepted, with reference-compatible lighting payload shapes'

$routeBody = Get-SourceSlice $App `
    '// User-visible keys 1..5 map directly to physical_key_id 0..4.' `
    'pk_r = rdx_key_get_io_num_table'
$routerClick = Get-SourceSlice $Router `
    'int rdx_input_router_click(' `
    'u8 rdx_input_router_test_mode_active('
$testMapOk = Test-TokensInOrder $Router @(
    'HID_KEYBOARD_USAGE_C',
    'HID_KEYBOARD_USAGE_V',
    'HID_KEYBOARD_USAGE_BACKSPACE',
    'HID_KEYBOARD_USAGE_ENTER',
    '{ RDX_INPUT_ACTION_CODEX_FAST'
)
Assert-Contract 'FAST_EXCLUSIVE_ROUTING' `
    ($routeBody -match 'rdx_hid_service_route_is_active\s*\(\s*\)' -and
     $routeBody -match 'rdx_input_router_click\s*\(\s*\(u8\)num_idx\s*\)' -and
     $routeBody -notmatch 'rdx_codex_micro|rdx_hogp_keyboard_report_send' -and
     $App -notmatch 'num_idx\s*==\s*0' -and
     $routerClick -match 'switch\s*\(\s*entry->kind\s*\)' -and
     $routerClick -match '(?s)case\s+RDX_INPUT_ACTION_KEYBOARD:.*?rdx_input_router_keyboard_click\s*\(\s*entry\s*\).*?case\s+RDX_INPUT_ACTION_CODEX_FAST:.*?rdx_hid_report_is_ready\s*\(\s*RDX_HID_REPORT_CODEX\s*\).*?rdx_codex_micro_fast_key_click\s*\(\s*\)' -and
     $testMapOk -and
     $RouterHeader -match '#define\s+RDX_INPUT_ROUTER_PHYSICAL_KEY_COUNT\s+5' -and
     $Codex -match '\\"k\\":\\"ACT06\\",\\"act\\":%u\}\}' -and
     $Codex -notmatch '\\"k\\":\\"AG00\\"' -and
     $Codex -match 'rdx_codex_micro_send_fast_key\(1\)' -and
     $Codex -match 'rdx_codex_micro_send_fast_key\(0\)') `
    'keys 1..5 must enter one typed router, with key_id 4 exclusively emitting Fast ACT06 down/up'

Assert-Contract 'BUILD_AND_LIFECYCLE_WIRING' `
    ($Makefile.Contains('rdx_gatt_profile.c') -and
     $Makefile.Contains('rdx_hid_service.c') -and
     $Makefile.Contains('rdx_codex_micro.c') -and
     $Makefile.Contains('rdx_input_router.c') -and
     $Server -match 'rdx_codex_micro_init\s*\(' -and
     $Server -match 'rdx_codex_micro_deinit\s*\(' -and
     $App -match 'rdx_input_router_init\s*\(' -and
     $Server -match 'rdx_input_router_deinit\s*\(' -and
     $HidService -match 'rdx_codex_micro_runtime_reset\s*\(' -and
     $HidService -match 'rdx_codex_micro_ready_drop_cleanup\s*\(' -and
     $HidServiceHeader -match 'rdx_hid_report_notify' -and
     $Codex -match 'rdx_hid_report_notify\s*\(\s*RDX_HID_REPORT_CODEX' -and
     $Codex -notmatch 'app_ble_att_send_data|rdx_hogp_codex_is_ready' -and
     $CodexHeader -match 'rdx_codex_micro_fast_key_release_all' -and
     $CodexHeader -match 'rdx_codex_micro_output_write' -and
     $Runner.Contains('test_codex_micro_contract.ps1') -and
     $Makefile -match '(?m)^\.PHONY:\s+all\s+clean\s+pre_build\s*$' -and
     $Makefile -notmatch '(?m)^codex-(?:v1|c1)' -and
     $Tasks -match '"label"\s*:\s*"all"' -and
     $Tasks -match 'winmk\.bat all' -and
     $Tasks -notmatch 'codex-(?:v1|c1)') `
    'the router and Codex module must be lifecycle-wired and all must be the only product build entry'

Assert-Contract 'WINDOWS_HID_TOOL' `
    ($Tool -match 'SetupDiGetClassDevsW' -and
     $Tool -match 'HidD_GetPreparsedData' -and
     $Tool -match 'HidP_GetCaps' -and
     $Tool -match 'InputReportByteLength' -and
     $Tool -match 'OutputReportByteLength' -and
     $Tool -match 'WriteFile\(' -and
     $Tool -match 'FILE_FLAG_OVERLAPPED' -and
     $Tool -match 'ReadFile\(' -and
     $Tool -match 'HidD_SetOutputReport' -and
     $Tool -notmatch 'HidD_GetInputReport' -and
     $Tool -match '\\"method\\":\\"device.status\\",\\"id\\":1' -and
     $BuildTool -match 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' -and
     $CaptureTool -match 'BluetoothLEAdvertisementWatcher' -and
     $CaptureTool -match 'DataSections' -and
     $EvidenceTool -match 'Get-PnpDevice\s+-Class\s+HIDClass' -and
     $EvidenceTool -match 'pnputil\s+/enum-devices\s+/class\s+HIDClass\s+/properties') `
    'Windows evidence must use native HID caps, WriteFile/overlapped ReadFile and active BLE advertisement capture'

Write-Host 'Codex Micro contracts passed.'
