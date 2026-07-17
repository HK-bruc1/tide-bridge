#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$ServerPath = Join-Path $ProtocolDir 'rdx_ble_server.c'
$ServerHeaderPath = Join-Path $ProtocolDir 'rdx_ble_server.h'
$HogpConfigPath = Join-Path $ProtocolDir 'rdx_hogp_config.h'
$ProjectConfigPath = Join-Path $RepoRoot 'SDK/apps/earphone/include/t2620_project_config.h'
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

$ServerText = Get-Content -Raw $ServerPath
$ServerHeaderText = Get-Content -Raw $ServerHeaderPath
$HogpConfigText = Get-Content -Raw $HogpConfigPath
$ProjectConfigText = Get-Content -Raw $ProjectConfigPath

Test-Contract 'PRODUCTION_UNIFIED_ENTRY_SWITCH_REMOVED' `
    (($ServerText + $HogpConfigText + $ProjectConfigText) -notmatch 'TCFG_RDX_HOGP_UNIFIED_ENTRY_ENABLE') `
    'the validated unified entry must be the only production path, without a compatibility switch'

Test-Contract 'OBSOLETE_HOGP_ADVERTISING_CONFIG_REMOVED' `
    ($HogpConfigText -notmatch 'RDX_HOGP_APPEARANCE|RDX_HOGP_NAME_SOURCE|RDX_HOGP_CUSTOM_NAME') `
    'standalone HOGP appearance and name settings must not survive after advertising is unified'

$FillMatch = [regex]::Match($ServerText,
    '(?sm)static\s+u8\s+rdx_ble_server_fill_adv_data\s*\([^)]*\)\s*\{(.*?)^\}')
$UnifiedBody = if ($FillMatch.Success) { $FillMatch.Groups[1].Value } else { '' }
$fieldTokens = @(
    'HCI_EIR_DATATYPE_FLAGS',
    'name_type, name_p, name_len'
)
$lastPosition = -1
$fieldOrderOk = $true
foreach ($token in $fieldTokens) {
    $position = $UnifiedBody.IndexOf($token, $lastPosition + 1)
    if ($position -lt 0 -or $position -le $lastPosition) {
        $fieldOrderOk = $false
        break
    }
    $lastPosition = $position
}
Test-Contract 'UNIFIED_PRIMARY_ADV_FIELD_ORDER' $fieldOrderOk `
    'primary ADV must preserve the legacy CONFIG order: Flags followed by Name'

$unifiedConstantsOk = $UnifiedBody -match 'const\s+u8\s+flags\[\]\s*=\s*\{\s*0x0A\s*\}' -and
                      $UnifiedBody -match 'name_capacity\s*=\s*ADV_RSP_PACKET_MAX\s*-\s*offset\s*-\s*2' -and
                      $UnifiedBody -match 'name_type\s*=\s*HCI_EIR_DATATYPE_SHORTENED_LOCAL_NAME'
Test-Contract 'UNIFIED_PRIMARY_ADV_VALUES_AND_BOUND' $unifiedConstantsOk `
    'primary ADV must preserve flags 0x0A and bound an overlong legacy name with AD type 0x08'

$RspMatch = [regex]::Match($ServerText,
    '(?sm)static\s+u8\s+rdx_ble_server_fill_rsp_data\s*\([^)]*\)\s*\{(.*?)^\}')
$RspBody = if ($RspMatch.Success) { $RspMatch.Groups[1].Value } else { '' }
$manufacturerPreserved = $RspBody -match 'PRODUCT_CODE' -and
                         $RspBody -match 'le_controller_get_mac' -and
                         $RspBody -match 'FACTORY_CODE' -and
                         $RspBody -match 'RDX_DEVICE_ABILITY' -and
                         $RspBody -match 'PRODUCT_TYPE' -and
                         $RspBody -match 'RDX_SELF_MARK' -and
                         $RspBody -match 'HCI_EIR_DATATYPE_MANUFACTURER_SPECIFIC_DATA'
Test-Contract 'RDX_MANUFACTURER_BYTES_PRESERVED' $manufacturerPreserved `
    'RDX Manufacturer Data fields and byte order must remain unchanged'

$UnifiedRspBody = $RspBody
$rspFieldTokens = @(
    'HCI_EIR_DATATYPE_MANUFACTURER_SPECIFIC_DATA',
    'HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS'
)
$lastPosition = -1
$rspFieldOrderOk = $true
foreach ($token in $rspFieldTokens) {
    $position = $UnifiedRspBody.IndexOf($token, $lastPosition + 1)
    if ($position -lt 0 -or $position -le $lastPosition) {
        $rspFieldOrderOk = $false
        break
    }
    $lastPosition = $position
}
Test-Contract 'UNIFIED_SCAN_RESPONSE_FIELD_ORDER' $rspFieldOrderOk `
    'Scan Response must preserve RDX Manufacturer Data first and append HID UUID second'

$rspValuesOk = $UnifiedRspBody -match 'const\s+u8\s+hid_uuid\[\]\s*=\s*\{\s*0x12\s*,\s*0x18\s*\}' -and
               $UnifiedRspBody -notmatch 'HCI_EIR_DATATYPE_APPEARANCE_DATA' -and
               $UnifiedRspBody -notmatch 'LOCAL_NAME'
Test-Contract 'UNIFIED_SCAN_RESPONSE_HID_UUID_ONLY' $rspValuesOk `
    'candidate C must append HID UUID 0x1812 without Appearance or Name'

Test-Contract 'HID_UUID_FOLLOWS_HOGP_MASTER_SWITCH' `
    ($UnifiedRspBody -match '#if\s+TCFG_RDX_HOGP_ENABLE[\s\S]*?HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS[\s\S]*?#endif') `
    'a build with HOGP disabled must keep RDX advertising but must not advertise HID capability'

$capacityOk = $ServerText -match '\(u16\)\(\*offset\)\s*\+\s*2\s*\+\s*data_len\s*>\s*ADV_RSP_PACKET_MAX' -and
              $UnifiedRspBody -match 'offset\s*>\s*ADV_RSP_PACKET_MAX' -and
              $UnifiedRspBody -match 'rdx_ble_server_adv_append_data'
Test-Contract 'UNIFIED_SCAN_RESPONSE_CAPACITY' $capacityOk `
    'the RDX prefix and appended HID UUID must both be bounded by 31 bytes'

$currentNameLength = 'Beanstalk RKB 0002'.Length
$primaryAdvLength = 3 + 2 + $currentNameLength
$unifiedRspLength = 27 + 4
$maxCompleteNameLength = 31 - 3 - 2
Test-Contract 'UNIFIED_PACKET_BUDGETS' `
    ($currentNameLength -eq 18 -and
     $primaryAdvLength -eq 23 -and
     $unifiedRspLength -eq 31 -and
     $maxCompleteNameLength -eq 26) `
    'candidate C must use a 23-byte current ADV, a full 31-byte Scan Response and allow 26 complete-name bytes'

$AdvEnableMatch = [regex]::Match($ServerText,
    '(?sm)int\s+rdx_ble_server_adv_enable\s*\([^)]*\)\s*\{(.*?)^\}')
$AdvEnableBody = if ($AdvEnableMatch.Success) { $AdvEnableMatch.Groups[1].Value } else { '' }
Test-Contract 'UNIFIED_PACKET_BUILDERS_SELECTED' `
    ($AdvEnableBody -match 'len\s*=\s*rdx_ble_server_fill_rsp_data\s*\(\s*rspData\s*\)' -and
     $AdvEnableBody -notmatch 'fill_unified_rsp_data|TCFG_RDX_HOGP_UNIFIED_ENTRY_ENABLE') `
    'advertising must use one production Scan Response builder without a legacy fallback branch'

Test-Contract 'PHASE2B_CONNECTION_HAS_NO_ADVERTISED_IDENTITY' `
    ($ServerText -notmatch 'RDX_BLE_OWNER|RDX_BLE_MODE|connection_owner|rdx_ble_mode_get_advertised') `
    'connection lifecycle must not infer CONFIG/HOGP identity from unified advertising'

Test-Contract 'PHASE2B_SERVICE_ACCESS_HAS_NO_OWNER_CLAIM' `
    ($ServerText -notmatch 'rdx_ble_server_connection_owner_claim|rdx_ble_connection_owner_set') `
    'RDX and HID access must coexist on the current link without first-service owner claiming'

Test-Contract 'PHASE2B_HID_CAPABILITY_ATTACH' `
    ($ServerText -match 'static\s+u8\s+rdx_ble_server_hogp_attach\s*\(' -and
     ([regex]::Matches($ServerText, 'rdx_ble_server_hogp_attach\s*\(\s*connection_handle\s*\)')).Count -ge 2) `
    'HID read/write must attach the HID capability independently of RDX access'

Test-Contract 'PHASE2B_UNIFIED_ADV_SINGLE_SERVER_ENTRY' `
    ($ServerText -notmatch 'rdx_hogp_adv_start|rdx_hogp_adv_stop|rdx_ble_mode_start_hogp_advertising' -and
     $AdvEnableBody -match 'rdx_ble_server_fill_rsp_data\s*\(\s*rspData\s*\)') `
    'all advertising must use the RDX server unified ADV/RSP builder'

$nameBufferOk = $ServerHeaderText -match 'char\s+ble_local_name\s*\[\s*BLE_LOCAL_NAME_MAX_LEN\s*\+\s*1\s*\]' -and
                $ServerText -match 'rdx_ble_server_local_name_copy' -and
                $ServerText -match "dst\[len\]\s*=\s*'\\0'" -and
                $ServerText -match 'syscfg_write\s*\(\s*VM_RDX_BLE_NAME\s*,\s*g_rdx_ble_server_info\.ble_local_name\s*,\s*len\s*\)'
Test-Contract 'LOCAL_NAME_BOUNDED_AND_ACTUAL_VM_LENGTH' $nameBufferOk `
    'local-name RAM needs NUL space and VM writes must use the bounded actual length'

$SetterMatch = [regex]::Match($ServerText,
    '(?sm)int\s+rdx_ble_server_set_local_name\s*\([^)]*\)\s*\{(.*?)^\}')
$SetterBody = if ($SetterMatch.Success) { $SetterMatch.Groups[1].Value } else { '' }
Test-Contract 'LOCAL_NAME_SETTER_NO_UNBOUNDED_COPY' `
    ($SetterBody -match 'rdx_ble_server_local_name_store' -and $SetterBody -notmatch 'sprintf\s*\(') `
    'the BLE name setter must use the shared bounded storage helper'

$refreshMatch = [regex]::Match($ServerText,
    '(?sm)void\s+rdx_ble_server_adv_data_changed\s*\([^)]*\)\s*\{(.*?)^\}')
$refreshBody = if ($refreshMatch.Success) { $refreshMatch.Groups[1].Value } else { '' }
$refreshPolicyOk = $ServerHeaderText -match 'u8\s+adv_refresh_pending' -and
                   $refreshBody -match 'g_rdx_ble_server_info\.ble_conn' -and
                   $refreshBody -match 'app_ble_get_hdl_con_handle' -and
                   $refreshBody -match 'adv_refresh_pending\s*=\s*TRUE' -and
                   $refreshBody -match 'adv_refresh_pending\s*=\s*FALSE' -and
                   $ServerText -match '(?s)rdx_ble_server_local_name_store.*?if\s*\(\s*refresh_adv\s*\).*?rdx_ble_server_adv_data_changed'
Test-Contract 'LOCAL_NAME_REFRESH_POLICY' $refreshPolicyOk `
    'name changes must refresh immediately while idle and defer while connected'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All production RDX unified advertising checks passed.'
    exit 0
}

Write-Host "$Failed production RDX unified advertising checks failed."
exit 1
