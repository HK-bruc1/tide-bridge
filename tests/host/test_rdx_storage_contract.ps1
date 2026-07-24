$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$rdxRoot = Join-Path $repo 'SDK/apps/common/third_party_profile/rdx_protocol'
$headerPath = Join-Path $rdxRoot 'port/jl/include/rdx_jl_storage.h'
$jlPath = Join-Path $rdxRoot 'port/jl/jl7018/rdx_jl_storage.c'
$shadowPath = Join-Path $rdxRoot 'port/jl/jl7018_shadow/rdx_jl_storage.c'

$header = Get-Content -Raw -LiteralPath $headerPath
$jl = Get-Content -Raw -LiteralPath $jlPath
$shadow = Get-Content -Raw -LiteralPath $shadowPath
$vm = Get-Content -Raw -LiteralPath (Join-Path $rdxRoot 'rdx_vm.c')
$rtc = Get-Content -Raw -LiteralPath (Join-Path $rdxRoot 'rdx_rtc.c')
$dut = Get-Content -Raw -LiteralPath (Join-Path $rdxRoot 'rdx_dut.c')
$record = Get-Content -Raw -LiteralPath (Join-Path $rdxRoot 'rdx_record.c')
$recordHeader = Get-Content -Raw -LiteralPath (Join-Path $rdxRoot 'rdx_record.h')
$ble = Get-Content -Raw -LiteralPath (Join-Path $rdxRoot 'rdx_ble_server.c')
$bleHeader = Get-Content -Raw -LiteralPath (Join-Path $rdxRoot 'rdx_ble_server.h')
$deviceService = Get-Content -Raw -LiteralPath (Join-Path $rdxRoot 'service/rdx_device_service.c')

function Assert-Match([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -notmatch $Pattern) {
        throw "FAIL: $Message"
    }
    Write-Host "PASS: $Message"
}

function Assert-NotMatch([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) {
        throw "FAIL: $Message"
    }
    Write-Host "PASS: $Message"
}

Assert-NotMatch $header 'syscfg_id\.h|\bVM_RDX_|\bCFG_BT_(?:NAME|MAC_ADDR)\b' `
    'Storage public header does not expose JL ids'
Assert-Match $header 'typedef\s+enum\s*\{[\s\S]*?RDX_STORAGE_KEY_BOUND_STATUS[\s\S]*?RDX_STORAGE_KEY_BLE_MAC[\s\S]*?\}\s*rdx_storage_key_t\s*;' `
    'Storage public header exposes logical keys only'
Assert-Match $header 'rdx_storage_read\s*\(rdx_storage_key_t\s+key' `
    'Fixed-length read uses a logical key'
Assert-Match $header 'rdx_storage_write\s*\(rdx_storage_key_t\s+key' `
    'Fixed-length write uses a logical key'
Assert-Match $header 'rdx_storage_read_blob\s*\([\s\S]*?u16\s*\*actual_len\)' `
    'Legacy blob read reports actual length'
Assert-Match $header 'rdx_storage_read_factory_bt_name\s*\(' `
    'Factory BT name has a semantic read API'
Assert-Match $header 'rdx_storage_read_factory_bt_mac\s*\(' `
    'Factory BT MAC has a semantic read API'

$mappings = @(
    @{ Key = 'BOUND_STATUS'; Id = 'VM_RDX_NOTTA_BOUND_STATUS' },
    @{ Key = 'CUSTOM_AUTH'; Id = 'VM_RDX_CUSTOM_AUTH' },
    @{ Key = 'RTC_INIT_VALUE'; Id = 'VM_RDX_RTC_INIT_VALUE' },
    @{ Key = 'DUT_DISABLED'; Id = 'VM_RDX_KEY_DUT_DISABLED' },
    @{ Key = 'MIC_GAIN'; Id = 'VM_RDX_MIC_GAIN' },
    @{ Key = 'REC_ERR_REBOOT'; Id = 'VM_RDX_REC_ERR_REBOOT' },
    @{ Key = 'BLE_NAME'; Id = 'VM_RDX_BLE_NAME' },
    @{ Key = 'BLE_MAC'; Id = 'VM_RDX_BLE_MAC' }
)

foreach ($port in @(
    @{ Name = 'jl7018'; Text = $jl },
    @{ Name = 'jl7018_shadow'; Text = $shadow }
)) {
    foreach ($mapping in $mappings) {
        $pattern = 'case\s+RDX_STORAGE_KEY_' + $mapping.Key + '\s*:\s*return\s+' + $mapping.Id + '\s*;'
        Assert-Match $port.Text $pattern "$($port.Name) maps $($mapping.Key) to the frozen JL id"
    }
    Assert-Match $port.Text 'return\s*\(ret\s*==\s*len\)\s*\?\s*RDX_OK\s*:\s*RDX_ERR_IO\s*;' `
        "$($port.Name) requires exact-length fixed transfers"
    Assert-Match $port.Text 'ret\s*<\s*0\s*\|\|\s*ret\s*>\s*capacity' `
        "$($port.Name) rejects invalid legacy blob lengths"
    Assert-Match $port.Text '\*actual_len\s*=\s*\(u16\)ret\s*;' `
        "$($port.Name) preserves legacy BLE name length"
    Assert-Match $port.Text 'RDX_STORAGE_KEY_BLE_NAME' `
        "$($port.Name) restricts the blob contract to BLE name"
    Assert-Match $port.Text 'CFG_BT_NAME' `
        "$($port.Name) owns the factory BT name id"
    Assert-Match $port.Text 'CFG_BT_MAC_ADDR' `
        "$($port.Name) owns the factory BT MAC id"
}

$businessFiles = Get-ChildItem -Path $rdxRoot -Recurse -File -Filter '*.c' |
    Where-Object { $_.FullName -notmatch '[\\/]port[\\/]' }
$businessText = ($businessFiles | ForEach-Object {
    Get-Content -Raw -LiteralPath $_.FullName
}) -join "`n"

Assert-NotMatch $businessText '\bsyscfg_(?:read|write|read_string)\s*\(' `
    'RDX business sources do not call syscfg directly'
Assert-NotMatch $businessText '\b(?:VM_RDX_[A-Z0-9_]*|CFG_BT_NAME|CFG_BT_MAC_ADDR)\b' `
    'RDX business sources do not use raw JL storage ids'
Assert-NotMatch $businessText 'extern\s+void\s+rdx_record_err_reboot_flag_write_into_vm\s*\(' `
    'Record recovery writer has no conflicting void declaration'
Assert-NotMatch $businessText 'extern\s+void\s+rdx_ble_server_reset_local_name\s*\(' `
    'BLE name reset has no conflicting void declaration'

Assert-Match $vm 'rdx_storage_read\s*\(RDX_STORAGE_KEY_BOUND_STATUS' `
    'Bound status reads through the exact storage contract'
Assert-Match $vm 'rdx_storage_(?:read|write)\s*\(\s*RDX_STORAGE_KEY_CUSTOM_AUTH' `
    'Charge-case custom auth uses the logical storage key'
Assert-Match $rtc 'rdx_storage_read\s*\(RDX_STORAGE_KEY_RTC_INIT_VALUE' `
    'RTC restore reads through the storage port'
Assert-Match $dut 'ret\s*==\s*RDX_OK\s*&&\s*vm_value\s*==\s*KEY_DUT_DISABLED_FLAG' `
    'DUT disabled state still requires a successful exact read and the 0xAA marker'
Assert-Match $record 'rdx_storage_write\s*\(RDX_STORAGE_KEY_REC_ERR_REBOOT' `
    'Record recovery flag writes through the exact storage contract'
Assert-Match $record 'return\s+sizeof\(err_reboot_flag\)\s*;' `
    'Record recovery writer preserves its positive success return'
Assert-Match $recordHeader 'int\s+rdx_record_err_reboot_flag_write_into_vm\s*\(u8\s+err_reboot_flag\)\s*;' `
    'Record recovery writer has one correct public declaration'
Assert-Match $ble 'rdx_storage_read_blob\s*\(\s*RDX_STORAGE_KEY_BLE_NAME[\s\S]*?&stored_name_len' `
    'BLE name reads preserve the legacy stored length'
Assert-Match $ble 'rdx_storage_write_blob\s*\(\s*RDX_STORAGE_KEY_BLE_NAME[\s\S]*?\(u16\)local_name_len' `
    'BLE default-name path preserves its historical variable write length'
Assert-Match $ble 'rdx_storage_read_factory_bt_mac\s*\(bt_mac\)' `
    'BLE AI mode reads the JL factory BT MAC through the semantic accessor'
Assert-Match $ble 'rdx_storage_read\s*\(RDX_STORAGE_KEY_BLE_MAC[\s\S]*?ret\s*!=\s*RDX_OK' `
    'BLE MAC falls back when the exact VM read fails'
Assert-Match $bleHeader 'int\s+rdx_ble_server_reset_local_name\s*\(void\)\s*;' `
    'BLE name reset has one correct public declaration'
Assert-Match $deviceService 'rdx_storage_read_factory_bt_name\s*\(' `
    'Factory reset reads the JL BT name through the semantic accessor'
Assert-Match $deviceService 'rdx_storage_write_factory_bt_name\s*\(' `
    'Factory reset writes the JL BT name through the semantic accessor'

$storageService = Get-Content -Raw -LiteralPath (Join-Path $rdxRoot 'service/rdx_storage_service.c')
Assert-NotMatch $storageService '#\s*include\s*["<]rdx_jl_storage\.h[">]' `
    'File storage service does not own VM persistence'

Write-Host 'All RDX P10 storage contract checks passed.'
