param(
    [string]$RepoRoot = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

$failures = New-Object System.Collections.Generic.List[string]

function Read-Source([string]$RelativePath) {
    $path = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path)) {
        $script:failures.Add("missing source: $RelativePath")
        return ''
    }
    return [System.IO.File]::ReadAllText($path)
}

function Get-StructFields {
    param(
        [string]$Text,
        [string]$TypeName
    )

    $structMatch = [regex]::Match(
        $Text,
        "typedef\s+struct\s*\{(?<body>[^{}]*)\}\s*$TypeName\s*;",
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $structMatch.Success) {
        return @()
    }

    $body = [regex]::Replace(
        $structMatch.Groups['body'].Value,
        '/\*.*?\*/',
        ' ',
        [System.Text.RegularExpressions.RegexOptions]::Singleline)
    $body = [regex]::Replace($body, '(?m)//.*$', ' ')

    return @([regex]::Matches($body, '\b[A-Za-z_]\w*\s+(?<name>[A-Za-z_]\w*)\s*;') |
        ForEach-Object { $_.Groups['name'].Value })
}

function Assert-FieldOrder {
    param(
        [string]$Name,
        [string]$Text,
        [string]$TypeName,
        [string[]]$ExpectedFields
    )

    $actualFields = @(Get-StructFields $Text $TypeName)
    if (($actualFields -join ',') -eq ($ExpectedFields -join ',')) {
        Write-Host "PASS: $Name"
        return
    }

    $script:failures.Add($Name)
    Write-Host "FAIL: $Name"
}

function Assert-Match {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern
    )

    if ([regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Write-Host "PASS: $Name"
        return
    }

    $script:failures.Add($Name)
    Write-Host "FAIL: $Name"
}

function Assert-NoMatch {
    param(
        [string]$Name,
        [string]$Text,
        [string]$Pattern
    )

    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        Write-Host "PASS: $Name"
        return
    }

    $script:failures.Add($Name)
    Write-Host "FAIL: $Name"
}

$recordHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.h'
$appHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.h'
$uxfileHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_uxfile.h'
$storagePortHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/port/jl/include/rdx_jl_storage.h'
$bleHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_ble_server.h'
$recordServiceHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_record_service.h'
$fileTransferServiceHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_file_transfer_service.h'
$wifiServiceHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_wifi_service.h'
$storageServiceHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.h'
$storageFormatHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_storage_format_compat.h'
$protocolAdapterHeader = Read-Source 'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.h'

Write-Host 'RDX public compatibility contract'

$recordStatusFields = @(
    'run',
    'formate',
    'scene',
    'orig_scene',
    'is_switch',
    'switch_orig_scene',
    'noshow',
    'process_state',
    'mode',
    'orig_mode',
    'key_trigger',
    'ui_notify',
    'begin_time',
    'rerun',
    'stream_discont',
    'pause_start_ms',
    'paused_accumulated_ms',
    'pause_timeout_timer'
)
Assert-FieldOrder 'RecordStatus field order remains compatible' `
    $recordHeader 'RecordStatus' $recordStatusFields

$reqFileInfoFields = @(
    'ack',
    'file_num',
    'is_first_pack',
    'pack_num',
    'orig_pack_num',
    'sent_size',
    'auto_del',
    'file_offset',
    'total_pack',
    'chunk',
    'block_cnt',
    'file_send_busy',
    'loop',
    'interrupt',
    'send_stop',
    'ble_upload_cancel'
)
Assert-FieldOrder 'ReqFileInfo field order remains compatible' `
    $uxfileHeader 'ReqFileInfo' $reqFileInfoFields

$contracts = @(
    @{ Name = 'legacy record process signature remains available'; Text = $recordHeader; Pattern = 'void\s+rdx_record_process\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'legacy record status getter signature remains available'; Text = $recordHeader; Pattern = 'RecordStatus\s*\*\s*rdx_record_get_status\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'record recovery writer keeps positive-result ABI'; Text = $recordHeader; Pattern = 'int\s+rdx_record_err_reboot_flag_write_into_vm\s*\(\s*u8\s+err_reboot_flag\s*\)\s*;' },
    @{ Name = 'BLE name reset keeps result ABI'; Text = $bleHeader; Pattern = 'int\s+rdx_ble_server_reset_local_name\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'storage port exposes logical key type'; Text = $storagePortHeader; Pattern = 'typedef\s+enum\s*\{[^}]*RDX_STORAGE_KEY_BOUND_STATUS[^}]*RDX_STORAGE_KEY_BLE_MAC[^}]*\}\s*rdx_storage_key_t\s*;' },
    @{ Name = 'fixed storage read uses logical key'; Text = $storagePortHeader; Pattern = 'rdx_err_t\s+rdx_storage_read\s*\(\s*rdx_storage_key_t\s+key\s*,\s*u8\s*\*\s*buf\s*,\s*u16\s+len\s*\)\s*;' },
    @{ Name = 'fixed storage write uses logical key'; Text = $storagePortHeader; Pattern = 'rdx_err_t\s+rdx_storage_write\s*\(\s*rdx_storage_key_t\s+key\s*,\s*const\s+u8\s*\*\s*buf\s*,\s*u16\s+len\s*\)\s*;' },
    @{ Name = 'BLE blob read reports actual length'; Text = $storagePortHeader; Pattern = 'rdx_err_t\s+rdx_storage_read_blob\s*\([^;]*u16\s*\*\s*actual_len\s*\)\s*;' },
    @{ Name = 'factory BT name accessor remains semantic'; Text = $storagePortHeader; Pattern = 'rdx_err_t\s+rdx_storage_read_factory_bt_name\s*\(\s*void\s*\*\s*buf\s*,\s*u16\s+len\s*\)\s*;' },
    @{ Name = 'factory BT MAC accessor remains semantic'; Text = $storagePortHeader; Pattern = 'rdx_err_t\s+rdx_storage_read_factory_bt_mac\s*\(\s*u8\s+mac\s*\[\s*6\s*\]\s*\)\s*;' },
    @{ Name = 'TWS record payload keeps RecordStatus'; Text = $appHeader; Pattern = 'typedef\s+struct\s*\{\s*RecordStatus\s+record_status\s*;\s*\}\s*rdx_tws_sync_record_t\s*;' },
    @{ Name = 'record service state query remains available'; Text = $recordServiceHeader; Pattern = 'u8\s+rdx_record_service_get_state\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'record service stop command remains available'; Text = $recordServiceHeader; Pattern = 'void\s+rdx_record_service_stop\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'storage format command remains available'; Text = $storageServiceHeader; Pattern = 'rdx_err_t\s+rdx_storage_service_format_for_app\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'file transfer state query exposes unavailable idle busy'; Text = $fileTransferServiceHeader; Pattern = 'typedef\s+enum\s*\{\s*RDX_FILE_TRANSFER_STATE_UNAVAILABLE\s*=\s*0\s*,\s*RDX_FILE_TRANSFER_STATE_IDLE\s*,\s*RDX_FILE_TRANSFER_STATE_BUSY\s*,?\s*\}\s*rdx_file_transfer_state_t\s*;' },
    @{ Name = 'file transfer state query remains available'; Text = $fileTransferServiceHeader; Pattern = 'rdx_err_t\s+rdx_file_transfer_get_state\s*\(\s*rdx_file_transfer_state_t\s*\*\s*out\s*\)\s*;' },
    @{ Name = 'file transfer stopped query remains available'; Text = $fileTransferServiceHeader; Pattern = 'rdx_err_t\s+rdx_file_transfer_get_stopped\s*\(\s*int\s*\*\s*out\s*\)\s*;' },
    @{ Name = 'record disconnect cleanup command remains available'; Text = $fileTransferServiceHeader; Pattern = 'rdx_err_t\s+rdx_file_transfer_cleanup_record_disconnect\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'BLE delayed cleanup command remains available'; Text = $fileTransferServiceHeader; Pattern = 'rdx_err_t\s+rdx_file_transfer_cleanup_ble_delayed\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'WiFi file busy compatibility query remains available'; Text = $wifiServiceHeader; Pattern = 'int\s+rdx_wifi_service_is_file_send_busy\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'legacy upload info getter signature remains available'; Text = $uxfileHeader; Pattern = 'ReqFileInfo\s*\*\s*rdx_protocol_get_uploadfileInfo\s*\(\s*void\s*\)\s*;' },
    @{ Name = 'legacy record file send finish signature remains available'; Text = $uxfileHeader; Pattern = 'void\s+rdx_uxfile_recordFileData_send_finish\s*\(\s*ReqFileInfo\s*\*\s*rf_info\s*\)\s*;' },
    @{ Name = 'legacy format callback keeps u8 result ABI'; Text = $storageFormatHeader; Pattern = 'typedef\s+void\s*\(\s*\*\s*rdx_storage_legacy_format_cb_t\s*\)\s*\(\s*u8\s+legacy_result\s*\)\s*;' },
    @{ Name = 'format result mapping keeps u8 ABI'; Text = $storageFormatHeader; Pattern = 'u8\s+rdx_storage_format_compat_result_is_ok\s*\(\s*u8\s+legacy_result\s*\)\s*;' },
    @{ Name = 'record protocol adapter accepts semantic payload'; Text = $protocolAdapterHeader; Pattern = 'rdx_err_t\s+rdx_record_protocol_post_trigger\s*\(\s*const\s+rdx_record_trigger_payload_t\s*\*\s*payload\s*\)\s*;' }
)

foreach ($contract in $contracts) {
    Assert-Match $contract.Name $contract.Text $contract.Pattern
}

Assert-NoMatch 'storage port public API does not expose raw JL ids' `
    $storagePortHeader '\b(?:VM_RDX_[A-Z0-9_]*|CFG_BT_NAME|CFG_BT_MAC_ADDR)\b'
Assert-NoMatch 'record service public API does not expose RecordStatus' `
    $recordServiceHeader '\bRecordStatus\b'
Assert-NoMatch 'storage service public API does not expose uxfile types' `
    $storageServiceHeader '\b(?:ReqFileInfo|uxfile_[A-Za-z0-9_]*_t)\b'
Assert-NoMatch 'file transfer public API does not expose legacy types' `
    $fileTransferServiceHeader '\b(?:ReqFileInfo|rdx_protocol_get_uploadfileInfo|uxfile_[A-Za-z0-9_]*_t|OS_[A-Za-z0-9_]+)\b'

if ($failures.Count -ne 0) {
    Write-Host ''
    Write-Host "RDX public compatibility contract failed: $($failures.Count)"
    foreach ($failure in $failures) {
        Write-Host "- $failure"
    }
    exit 1
}

Write-Host 'RDX public compatibility contract passed.'
exit 0
