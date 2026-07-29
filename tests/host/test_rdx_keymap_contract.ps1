#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$ProtocolRoot = 'SDK\apps\common\third_party_profile\rdx_protocol'
$Service = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keymap_config.c"
$Store = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keymap_store.c"
$Router = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_input_router.c"
$RouterHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_input_router.h"
$Internal = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_hogp_keymap_internal.h"
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

$apply = Get-SourceSlice $Router `
    'int rdx_input_router_action_map_apply(' `
    'int rdx_input_router_click('
$reset = Get-SourceSlice $Router `
    'void rdx_input_router_reset(' `
    'void rdx_input_router_deinit('
Assert-Contract 'KEYMAP_RELEASE_BEFORE_APPLY' `
    ((Test-TokensInOrder $apply @(
        'rdx_input_router_reset()',
        'memset(&s_rdx_input_active_map',
        'memcpy('
     )) -and
     $reset -match 'rdx_input_router_keyboard_ready_drop_cleanup\s*\(\s*\)' -and
     $reset -match 'rdx_codex_micro_agent_key_release_all\s*\(\s*\)') `
    'hot replacement must release both typed providers before publishing new actions'

$typedMapOk = $RouterHeader -match '#define\s+RDX_INPUT_ACTION_DATA_LEN\s+7' -and
              $RouterHeader -match '#define\s+RDX_INPUT_ACTION_NONE\s+0x00' -and
              $RouterHeader -match '#define\s+RDX_INPUT_ACTION_KEYBOARD\s+0x01' -and
              $RouterHeader -match '#define\s+RDX_INPUT_ACTION_CODEX_AGENT\s+0x02' -and
              $Router -match 'sizeof\(rdx_input_action_entry_t\)\s*==\s*8' -and
              $Service -match '(?s)memcmp\s*\(\s*entry.*?RDX_HOGPKM_ENTRY_LEN\s*\)\s*==\s*0\s*\?\s*RDX_INPUT_ACTION_NONE\s*:\s*RDX_INPUT_ACTION_KEYBOARD' -and
              $Service -match 'rdx_input_router_action_map_apply\s*\('
Assert-Contract 'KEYMAP_V1_TO_TYPED_CANONICAL_MAP' $typedMapOk `
    'legacy all-zero entries must become NONE and other entries must become KEYBOARD in the canonical 8-byte map'

$testModeOk = $Internal -match '#define\s+RDX_HOGPKM_STATUS_TEST_MODE_ACTIVE\s+0x0E' -and
              $Service -match '(?s)RDX_HOGP_KEY_ACTION_TEST_ENABLE.*?RDX_HOGPKM_OP_SET_KEYMAP.*?RDX_HOGPKM_OP_RESET_KEYMAP.*?RDX_HOGPKM_STATUS_TEST_MODE_ACTIVE' -and
              $Service -match '(?s)action_map_source=TEST; persisted V1 map not applied.*?#else\s*if \(rdx_hogpkm_apply_payload' -and
              $Router -match '(?s)RDX_HOGP_KEY_ACTION_TEST_ENABLE.*?rdx_input_router_action_map_apply.*?RDX_INPUT_ROUTER_TEST_MODE'
Assert-Contract 'KEYMAP_TEST_SOURCE_IS_IMMUTABLE' $testModeOk `
    'the built-in Gate map must reject SET/RESET and must not be overwritten by persisted V1 data'

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
