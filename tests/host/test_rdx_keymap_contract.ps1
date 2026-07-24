#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$ProtocolRoot = 'SDK\apps\common\third_party_profile\rdx_protocol'
$Service = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keymap_config.c"
$Store = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keymap_store.c"
$Action = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_key_action.c"
$App = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_app.c"
$Server = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_server.c"

$Commit = Get-SourceSlice $Service `
    'static int rdx_hogpkm_commit(' `
    'static void rdx_hogpkm_send_write_success'
$Init = Get-SourceSlice $Service `
    'void rdx_hogp_keymap_config_init(void)' `
    'void rdx_hogp_keymap_config_handle_custom'
$Custom = Get-SourceSlice $App `
    'void rdx_app_custom_command_parse(char* cmd, char* value)' `
    'void rdx_app_single_click_handle(void)'

Assert-Contract 'KEYMAP_CUSTOM_COMMAND_ENTRY' `
    ($Custom -match 'strcmp\s*\(\s*cmd\s*,\s*RDX_HOGP_KEYMAP_CUSTOM_CMD\s*\)' -and
     $Custom -match 'rdx_hogp_keymap_config_handle_custom\s*\(\s*value\s*\)') `
    'the immutable callback must route hogpkm into the source-controlled service'

Assert-Contract 'KEYMAP_BOOT_LOAD' `
    ($Init -match 'rdx_hogpkm_store_load\s*\(\s*&entry\s*\)' -and
     $Init -match 'rdx_hogpkm_apply_payload\s*\(\s*s_rdx_hogpkm_current_keymap\s*\)') `
    'boot must publish the last committed keymap to the HID executor'

$revalidations = ([regex]::Matches(
    $Commit,
    'rdx_hogpkm_request_is_current\s*\(\s*request\s*,\s*token\s*\)'
)).Count
$transactionOk = $revalidations -ge 4 -and
                 (Test-TokensInOrder $Commit @(
                    'rdx_hogpkm_store_prepare(',
                    'rdx_hogpkm_apply_payload(payload)',
                    'rdx_hogpkm_store_commit('
                 ))
Assert-Contract 'KEYMAP_OWNER_BOUND_TRANSACTION' $transactionOk `
    'prepare, RAM apply, VM commit and completion must retain the originating token'

$storeOk = $Store -match 'VM_RDX_HOGP_KEYMAP_SLOT_A' -and
           $Store -match 'VM_RDX_HOGP_KEYMAP_SLOT_B' -and
           $Store -match 'VM_RDX_HOGP_KEYMAP_COMMIT_A' -and
           $Store -match 'VM_RDX_HOGP_KEYMAP_COMMIT_B' -and
           $Store -match 'memcmp\s*\(\s*commit_record\s*,\s*commit_readback'
Assert-Contract 'KEYMAP_VERIFIED_AB_STORE' $storeOk `
    'persistent keymaps must use verified A/B data and commit records'

$apply = Get-SourceSlice $Action `
    'int rdx_hogp_key_action_keymap_apply(' `
    'int rdx_hogp_key_action_click('
Assert-Contract 'KEYMAP_RELEASE_BEFORE_APPLY' `
    (Test-TokensInOrder $apply @(
        'rdx_hogp_key_action_cancel_release_timer()',
        'rdx_hogp_keyboard_release_all()',
        'memset(&s_rdx_hogp_key_action_active_keymap',
        'memcpy('
    )) `
    'hot replacement must release the old HID report before publishing new keys'

Assert-Contract 'KEYMAP_RESPONSE_USES_OWNER_TOKEN' `
    ($Service -match 'rdx_ble_server_send_for_token\s*\(\s*packet\s*,\s*offset\s*,\s*token\s*\)' -and
     $Service -notmatch 'rdx_protocol_custom_msg_indicate\s*\(' -and
     $Service -notmatch 'rdx_protocol_packet_send_priority\s*\(') `
    'responses must enqueue directly on the captured RDX owner'

$disconnectHookCount = ([regex]::Matches(
    $Server,
    'rdx_hogp_keymap_config_on_disconnect\s*\(\s*\)'
)).Count
Assert-Contract 'KEYMAP_CANCELS_ON_RDX_DISCONNECT_ONLY' `
    ($disconnectHookCount -eq 1 -and
     $Server -match '(?s)static\s+void\s+rdx_ble_server_rdx_disconnected_cleanup_internal\s*\(\s*void\s*\).*?rdx_hogp_keymap_config_on_disconnect\s*\(\s*\)') `
    'an unrelated HID-only disconnect must not cancel the active App transaction'

Write-Host 'RDX keymap contracts passed.'
