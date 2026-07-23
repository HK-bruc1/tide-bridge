#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$ServiceText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_keymap_config.c')
$StoreText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_keymap_store.c')
$ActionText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_hogp_key_action.c')
$AppText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_app.c')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$SessionText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.c')
$SessionHeaderText = Get-Content -Raw `
    (Join-Path $ProtocolDir 'rdx_ble_session.h')
$ProjectConfigText = Get-Content -Raw `
    (Join-Path $RepoRoot 'SDK/apps/earphone/include/t2620_project_config.h')
$ArchiveHash = (Get-FileHash -Algorithm SHA256 `
    (Join-Path $ProtocolDir 'librdxApp.a')).Hash
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

function Get-SourceSlice {
    param([string]$Text, [string]$StartToken, [string]$EndToken)
    $start = $Text.IndexOf($StartToken)
    if ($start -lt 0) { return '' }
    $end = $Text.IndexOf($EndToken, $start + $StartToken.Length)
    if ($end -lt 0) { return $Text.Substring($start) }
    return $Text.Substring($start, $end - $start)
}

$CommitBody = Get-SourceSlice $ServiceText `
    'static int rdx_hogpkm_commit(' `
    'static void rdx_hogpkm_send_write_success'
$InitBody = Get-SourceSlice $ServiceText `
    'void rdx_hogp_keymap_config_init(void)' `
    'void rdx_hogp_keymap_config_handle_custom'
$CustomCallbackBody = Get-SourceSlice $AppText `
    'void rdx_app_custom_command_parse(char* cmd, char* value)' `
    'void rdx_app_single_click_handle(void)'
$ClaimBody = Get-SourceSlice $SessionText `
    'static rdx_ble_claim_result_t rdx_ble_session_claim(' `
    'rdx_ble_claim_result_t rdx_ble_session_claim_rdx('
$OwnerGetBody = Get-SourceSlice $SessionText `
    'static rdx_ble_link_state_t *rdx_ble_session_owner_get(' `
    'rdx_ble_link_state_t *rdx_ble_session_get_rdx_link(void)'
$ReleaseBody = Get-SourceSlice $SessionText `
    'rdx_ble_link_state_t *rdx_ble_session_link_release(' `
    'u8 rdx_ble_session_active_count(void)'
$HidAttachBody = Get-SourceSlice $ServerText `
    'static u8 rdx_ble_server_phase2_hid_attach(' `
    'static int rdx_ble_server_phase2_rdx_write('
$RdxAttachBody = Get-SourceSlice $ServerText `
    'static u8 rdx_ble_server_phase2_rdx_attach(' `
    'static void rdx_ble_server_phase2_rdx_detach('
$AttWriteBody = Get-SourceSlice $ServerText `
    'static int rdx_ble_server_att_write_callback(' `
    'void rdx_ble_server_gatt_receive_data('
$DisconnectBody = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_link_disconnected(' `
    'static void rdx_ble_server_phase0a_packet_handler('
$SmBody = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_sm_event_callback(' `
    'static void rdx_ble_server_cbk_packet_handler('

Test-Contract 'RDX_ARCHIVE_AND_CALLBACK_ABI_FROZEN' `
    ($ArchiveHash -eq 'C540D70540DC4D61E15D1CA13579CD2342D4EA972FF0A74A1AFCCC04B1EF4ACA' -and
     $AppText -match 'void\s+rdx_app_custom_command_parse\s*\(\s*char\s*\*\s*cmd\s*,\s*char\s*\*\s*value\s*\)') `
    'Phase 3 must not replace librdxApp.a or change its custom callback ABI'

Test-Contract 'SAME_LINK_CAPABILITIES_ARE_COMPOSABLE' `
    ($SessionHeaderText -match 'RDX_BLE_CAPABILITY_RDX\s*=\s*1\s*<<\s*0' -and
     $SessionHeaderText -match 'RDX_BLE_CAPABILITY_HID\s*=\s*1\s*<<\s*1' -and
     $SessionHeaderText -match 'RDX_BLE_CAPABILITY_RDX_HID' -and
     $ClaimBody -match 'link->capability\s*\|=\s*capability' -and
     $ClaimBody -notmatch 'other_owner_index|link->capability\s*!=\s*RDX_BLE_CAPABILITY_NONE') `
    'one physical link must be able to add RDX and HID ownership in either order'

Test-Contract 'COMPOSITE_OWNER_RETAINS_GLOBAL_UNIQUENESS' `
    ($ClaimBody -match 'if\s*\(\s*\*owner_index\s*<\s*RDX_BLE_LINK_MAX\s*\)' -and
     $ClaimBody -match 'return\s+RDX_BLE_CLAIM_BUSY' -and
     $OwnerGetBody -match 'link->capability\s*&\s*capability' -and
     $ReleaseBody -match 's_rdx_rdx_link_index\s*=\s*RDX_BLE_LINK_INVALID_INDEX' -and
     $ReleaseBody -match 's_rdx_hid_link_index\s*=\s*RDX_BLE_LINK_INVALID_INDEX') `
    'RDX and HID indexes may match each other but each capability must still have only one owner'

Test-Contract 'RDX_FIRST_DOES_NOT_BLOCK_HID_ATTACH' `
    ($HidAttachBody -match 'had_rdx_owner\s*=\s*rdx_ble_session_link_is_rdx\s*\(\s*link\s*\)' -and
     $HidAttachBody -match 'rdx_ble_session_claim_hid\s*\(\s*link' -and
     $HidAttachBody -match '\[BLE_PHASE3\] composite owner' -and
     $AttWriteBody -notmatch 'link->capability\s*!=\s*RDX_BLE_CAPABILITY_NONE') `
    'an RDX-owned PC ACL must still be able to pair, enable HID CCC, and attach HOGP'

Test-Contract 'HID_FIRST_DOES_NOT_BLOCK_RDX_ACTIVATION' `
    ($RdxAttachBody -match 'had_hid_owner\s*=\s*rdx_ble_session_link_is_hid\s*\(\s*link\s*\)' -and
     $RdxAttachBody -match 'rdx_ble_session_claim_rdx\s*\(\s*link' -and
     $RdxAttachBody -match '\[BLE_PHASE3\] composite owner' -and
     $RdxAttachBody -match 'order=HID\+RDX') `
    'a HID-ready PC ACL must still be able to activate the one allowed RDX runtime'

Test-Contract 'FRESH_PC_PAIRING_CAN_PRECEDE_HID_CCC' `
    ($SmBody -match '!rdx_ble_session_get_hid_link\s*\(\s*\)' -and
     $SmBody -match 'rdx_ble_session_link_set_hid_pairing_pending\s*\(\s*link\s*,\s*1\s*\)' -and
     $SmBody -match '\[BLE_PHASE3\] Just Works confirmed for provisional HID candidate' -and
     $SmBody -match 'sm_just_works_confirm\s*\(\s*con_handle\s*\)' -and
     $SmBody -notmatch 'rdx_ble_session_claim_hid') `
    'with no HID owner, Just Works may confirm a provisional PC link without claiming HID before encrypted CCC'

Test-Contract 'COMPOSITE_DISCONNECT_CLEANS_BOTH_OWNERS' `
    ($DisconnectBody -match 'if\s*\(\s*rdx_ble_session_link_is_hid\s*\(\s*link\s*\)\s*\)' -and
     $DisconnectBody -match 'if\s*\(\s*rdx_ble_session_link_is_rdx\s*\(\s*link\s*\)\s*\)' -and
     $DisconnectBody -notmatch 'else\s+if\s*\(\s*rdx_ble_session_link_is_rdx' -and
     $DisconnectBody.IndexOf('rdx_hogp_on_disconnected(con_handle)') -lt
        $DisconnectBody.IndexOf('rdx_ble_session_link_release(hdl, con_handle)') -and
     $DisconnectBody.IndexOf('rdx_ble_server_phase2_rdx_detach(link)') -lt
        $DisconnectBody.IndexOf('rdx_ble_session_link_release(hdl, con_handle)')) `
    'a physical disconnect of a composite ACL must clean HID and RDX before releasing the shared slot'

Test-Contract 'PHASE3_QUALIFICATION_TRACE_ENABLED' `
    ($ProjectConfigText -match '#define\s+RDX_HOGPKM_TRACE_ENABLE\s+1' -and
     $ServiceText -match '\[BLE_PHASE3\] keymap ready' -and
     $ServiceText -match '\[BLE_PHASE3\] keymap committed' -and
     $ServiceText -match '\[BLE_PHASE3\] RDX response route') `
    'the qualification image must expose init, commit, and owner-route evidence'

Test-Contract 'CUSTOM_COMMAND_ENTERS_FORMAL_KEYMAP_SERVICE' `
    ($CustomCallbackBody -match 'strcmp\s*\(\s*cmd\s*,\s*RDX_HOGP_KEYMAP_CUSTOM_CMD\s*\)' -and
     $CustomCallbackBody -match 'rdx_hogp_keymap_config_handle_custom\s*\(\s*value\s*\)') `
    'hogpkm must enter the source-controlled service without changing the callback ABI'

Test-Contract 'BOOT_LOAD_PUBLISHES_PERSISTED_KEYMAP' `
    ($InitBody -match 'rdx_hogpkm_store_load\s*\(\s*&entry\s*\)' -and
     $InitBody -match 'rdx_hogpkm_apply_payload\s*\(\s*s_rdx_hogpkm_current_keymap\s*\)') `
    'boot must load the last committed VM entry and publish it to the HID executor'

$Revalidations = ([regex]::Matches(
    $CommitBody,
    'rdx_hogpkm_request_is_current\s*\(\s*request\s*,\s*token\s*\)'
)).Count
Test-Contract 'COMMIT_IS_OWNER_BOUND_TWO_PHASE_TRANSACTION' `
    ($Revalidations -ge 4 -and
     $CommitBody.IndexOf('rdx_hogpkm_store_prepare(') -ge 0 -and
     $CommitBody.IndexOf('rdx_hogpkm_apply_payload(payload)') -gt
        $CommitBody.IndexOf('rdx_hogpkm_store_prepare(') -and
     $CommitBody.IndexOf('rdx_hogpkm_store_commit(') -gt
        $CommitBody.IndexOf('rdx_hogpkm_apply_payload(payload)')) `
    'prepare, RAM apply, VM commit, and completion must retain the originating RDX token'

Test-Contract 'VM_STORE_USES_VERIFIED_AB_SLOTS' `
    ($StoreText -match 'VM_RDX_HOGP_KEYMAP_SLOT_A' -and
     $StoreText -match 'VM_RDX_HOGP_KEYMAP_SLOT_B' -and
     $StoreText -match 'VM_RDX_HOGP_KEYMAP_COMMIT_A' -and
     $StoreText -match 'VM_RDX_HOGP_KEYMAP_COMMIT_B' -and
     $StoreText -match 'memcmp\s*\(\s*commit_record\s*,\s*commit_readback') `
    'Phase 3 persistence must retain A/B data and commit readback verification'

Test-Contract 'HOT_APPLY_RELEASES_OLD_HID_REPORT' `
    ($ActionText -match '(?s)rdx_hogp_key_action_keymap_apply.*?rdx_hogp_key_action_cancel_release_timer\s*\(\s*\).*?rdx_hogp_keyboard_release_all\s*\(\s*\).*?memcpy\s*\(\s*s_rdx_hogp_key_action_active_keymap\.keys') `
    'a keymap replacement must cancel the old timer and release before publishing new keys'

Test-Contract 'KEYMAP_RESPONSE_STAYS_ON_RDX_OWNER' `
    ($ServiceText -match 'rdx_ble_server_send_for_token\s*\(\s*packet\s*,\s*offset\s*,\s*token\s*\)' -and
     $ServiceText -notmatch 'rdx_protocol_custom_msg_indicate\s*\(' -and
     $ServiceText -notmatch 'rdx_protocol_packet_send_priority\s*\(') `
    'responses must bypass tokenless prebuilt queues and enqueue on the captured RDX wrapper'

$DisconnectHookCount = ([regex]::Matches(
    $ServerText,
    'rdx_hogp_keymap_config_on_disconnect\s*\(\s*\)'
)).Count
Test-Contract 'ONLY_RDX_DISCONNECT_CANCELS_KEYMAP_TRANSACTION' `
    ($DisconnectHookCount -eq 1 -and
     $ServerText -match '(?s)static\s+void\s+rdx_ble_server_rdx_disconnected_cleanup_internal\s*\(\s*void\s*\)\s*\{.*?RecordStatus\s*\*\s*rp.*?rdx_hogp_keymap_config_on_disconnect\s*\(\s*\)\s*;\s*\}') `
    'HID-only disconnect cleanup must not invalidate the App keymap transaction'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 3 online keymap contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 3 online keymap contract checks failed."
exit 1
