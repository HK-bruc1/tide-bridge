#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$ServiceText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_keymap_config.c')
$InternalText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_keymap_internal.h')
$AppText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_app.c')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$ArchivePath = Join-Path $ProtocolDir 'librdxApp.a'
$ArchiveHash = (Get-FileHash -Algorithm SHA256 $ArchivePath).Hash
$RdxWriteStart = $ServerText.IndexOf('static int rdx_ble_server_phase2_rdx_write(')
$RdxWriteEnd = $ServerText.IndexOf('/* Phase 0A still exposes', $RdxWriteStart)
$RdxWriteBody = if ($RdxWriteStart -ge 0 -and $RdxWriteEnd -gt $RdxWriteStart) {
    $ServerText.Substring($RdxWriteStart, $RdxWriteEnd - $RdxWriteStart)
} else { '' }
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

Test-Contract 'PREBUILT_CUSTOM_CALLBACK_ABI_UNCHANGED' `
    ($ArchiveHash -eq 'C540D70540DC4D61E15D1CA13579CD2342D4EA972FF0A74A1AFCCC04B1EF4ACA' -and
     $AppText -match 'void\s+rdx_app_custom_command_parse\s*\(\s*char\s*\*\s*cmd\s*,\s*char\s*\*\s*value\s*\)') `
    'the pinned librdxApp.a and its legacy two-pointer callback definition must remain unchanged'

Test-Contract 'KEYMAP_REQUEST_OWNS_RDX_TOKEN' `
    ($ServiceText -match '(?s)typedef\s+struct\s*\{\s*rdx_hogpkm_request_t\s+frame\s*;\s*rdx_ble_async_token_t\s+rdx_token\s*;\s*\}\s*rdx_hogpkm_owned_request_t' -and
     $InternalText -notmatch 'rdx_ble_(session|async_token)') `
    'the service envelope must own the token without coupling the wire codec to BLE'

Test-Contract 'KEYMAP_ENTRY_CAPTURES_OWNER_TOKEN' `
    ($ServiceText -match '(?s)rdx_hogp_keymap_config_handle_custom\s*\([^)]*\).*?rdx_hogpkm_token_capture\s*\(\s*&rdx_token\s*\).*?s_rdx_hogpkm_pending\.rdx_token\s*=\s*rdx_token') `
    'the ABI callback must capture the active RDX owner before posting keymap work'

Test-Contract 'KEYMAP_APP_CORE_REVALIDATES_TOKEN' `
    ($ServiceText -match '(?s)rdx_hogpkm_process_pending\s*\(void\).*?rdx_hogpkm_request_is_current\s*\(\s*request\s*,\s*&owned_request\.rdx_token\s*\)') `
    'app_core must reject a request after owner disconnect, slot reuse, or transport reinit'

$CommitStart = $ServiceText.IndexOf('static int rdx_hogpkm_commit(')
$CommitEnd = $ServiceText.IndexOf('static void rdx_hogpkm_send_write_success', $CommitStart)
$CommitBody = if ($CommitStart -ge 0 -and $CommitEnd -gt $CommitStart) {
    $ServiceText.Substring($CommitStart, $CommitEnd - $CommitStart)
} else { '' }
Test-Contract 'KEYMAP_VM_TRANSACTION_REVALIDATES_TOKEN' `
    (([regex]::Matches($CommitBody, 'rdx_hogpkm_request_is_current\s*\(\s*request\s*,\s*token\s*\)')).Count -ge 4) `
    'prepare, RAM apply, VM commit publication, and completion must stay bound to the originating owner'

Test-Contract 'KEYMAP_QUEUED_STATUS_OWNS_TOKEN' `
    ($ServiceText -match 'rdx_hogpkm_status_request_t' -and
     $ServiceText -match 'request->rdx_token\s*=\s*\*token' -and
     $ServiceText -match '(?s)rdx_hogpkm_send_queued_status.*?rdx_hogpkm_token_is_current\s*\(\s*&request->rdx_token\s*\).*?free\s*\(\s*request\s*\)') `
    'decode-error and busy responses must carry and release their own token-bearing queue object'

Test-Contract 'KEYMAP_UPLINK_BYPASSES_TOKENLESS_PREBUILT_QUEUE' `
    ($ServiceText -notmatch 'rdx_protocol_packet_send_priority\s*\(' -and
     $ServiceText -match 'rdx_ble_server_send_for_token\s*\(\s*packet\s*,\s*offset\s*,\s*token\s*\)') `
    'keymap responses must not enter the prebuilt queue that cannot retain a link token'

Test-Contract 'KEYMAP_SOURCE_MIGRATION_USES_SINGLE_SESSION_GATE' `
    ($RdxWriteBody -match 'rdx_ble_server_phase2_rdx_attach\s*\(' -and
     $RdxWriteBody -match 'rdx_ble_server_gatt_receive_data\s*\(' -and
     $ServerText -match 'one-session-per-boot') `
    'the tokenless prebuilt receive queue is safe only while owner replacement is forbidden'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 2B keymap async contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 2B keymap async contract checks failed."
exit 1
