#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$SessionText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.c')
$SessionHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_session.h')
$ServerText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.c')
$ServerHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_ble_server.h')
$ProtocolHeaderText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_protocol.h')
$AppText = Get-Content -Raw (Join-Path $ProtocolDir 'rdx_app.c')
$ArchiveHash = (Get-FileHash -Algorithm SHA256 `
    (Join-Path $ProtocolDir 'librdxApp.a')).Hash
$Failed = 0

function Get-SourceSlice {
    param([string]$Text, [string]$StartMarker, [string]$EndMarker)

    $start = $Text.IndexOf($StartMarker)
    if ($start -lt 0) { return '' }
    $end = $Text.IndexOf($EndMarker, $start + $StartMarker.Length)
    if ($end -lt 0) { return '' }
    return $Text.Substring($start, $end - $start)
}

function Get-LastSourceSlice {
    param([string]$Text, [string]$StartMarker, [string]$EndMarker)

    $start = $Text.LastIndexOf($StartMarker)
    if ($start -lt 0) { return '' }
    $end = $Text.IndexOf($EndMarker, $start + $StartMarker.Length)
    if ($end -lt 0) { return '' }
    return $Text.Substring($start, $end - $start)
}

function Test-Contract {
    param([string]$Name, [bool]$Passed, [string]$Message)

    if ($Passed) {
        Write-Host "PASS: $Name"
        return
    }
    Write-Host "FAIL: ${Name}: $Message"
    $script:Failed++
}

$ClaimBody = Get-SourceSlice $SessionText `
    'rdx_ble_claim_result_t rdx_ble_session_claim_rdx(' `
    'rdx_ble_claim_result_t rdx_ble_session_claim_hid('
$QuiesceBody = Get-SourceSlice $SessionText `
    'u8 rdx_ble_session_rdx_runtime_begin_quiesce(' `
    'u8 rdx_ble_session_rdx_runtime_barrier_arrive('
$BarrierArriveBody = Get-SourceSlice $SessionText `
    'u8 rdx_ble_session_rdx_runtime_barrier_arrive(' `
    'u8 rdx_ble_session_rdx_runtime_rearm('
$RearmBody = Get-SourceSlice $SessionText `
    'u8 rdx_ble_session_rdx_runtime_rearm(' `
    'void rdx_ble_session_rdx_runtime_fail_closed('
$PeerCaptureBody = Get-SourceSlice $SessionText `
    'static void rdx_ble_session_rebind_peer_capture(' `
    'static rdx_ble_claim_result_t rdx_ble_session_rebind_peer_check('
$PeerCheckBody = Get-SourceSlice $SessionText `
    'static rdx_ble_claim_result_t rdx_ble_session_rebind_peer_check(' `
    'void rdx_ble_session_transport_init('
$DetachStart = $ServerText.LastIndexOf(
    'static void rdx_ble_server_phase2_rdx_detach(')
$DetachEnd = if ($DetachStart -ge 0) {
    $ServerText.IndexOf('#if TCFG_RDX_HOGP_ENABLE', $DetachStart)
} else { -1 }
$DetachBody = if ($DetachEnd -gt $DetachStart) {
    $ServerText.Substring($DetachStart, $DetachEnd - $DetachStart)
} else { '' }
$AbortBody = Get-LastSourceSlice $ServerText `
    'static void rdx_ble_server_rdx_session_abort(' `
    'static void rdx_ble_server_rdx_send_worker_quiesce('
$SendQuiesceBody = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_rdx_send_worker_quiesce(' `
    'static void rdx_ble_server_rdx_session_reset_finalize('
$FinalizeBody = Get-LastSourceSlice $ServerText `
    'static void rdx_ble_server_rdx_session_reset_finalize(' `
    'static u8 rdx_ble_server_rdx_runtime_try_rearm('
$TryRearmBody = Get-LastSourceSlice $ServerText `
    'static u8 rdx_ble_server_rdx_runtime_try_rearm(' `
    'u8 rdx_ble_server_rdx_lifecycle_barrier_match('
$BarrierCompleteBody = Get-SourceSlice $ServerText `
    'void rdx_ble_server_rdx_lifecycle_barrier_complete(' `
    'static void rdx_ble_server_link_disconnected_cleanup_internal('
$AttachBody = Get-SourceSlice $ServerText `
    'static u8 rdx_ble_server_phase2_rdx_attach(' `
    'static void rdx_ble_server_phase2_rdx_detach('
$DisconnectBody = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_link_disconnected(' `
    'static void rdx_ble_server_phase0a_packet_handler('
$PacketHandlerBody = Get-SourceSlice $ServerText `
    'static void rdx_ble_server_phase0a_packet_handler(' `
    'static void rdx_ble_server_cbk_packet_handler('
$CustomBody = Get-SourceSlice $AppText `
    'void rdx_app_custom_command_parse(char* cmd, char* value)' `
    'void rdx_app_single_click_handle(void)'
$ProtocolEventBody = Get-SourceSlice $AppText `
    'static void rdx_app_protocol_handle(' `
    'void rdx_app_tasks_init(void)'
$IdleBody = Get-SourceSlice $AppText `
    'u8 rdx_app_rdx_rebind_is_idle(void)' `
    '/**************************************************************************'
$GetInfoBody = Get-SourceSlice $ServerText `
    'rdx_ble_server_info_t * rdx_ble_server_get_info(void)' `
    '/**************************************************************************'

Test-Contract 'PHASE3_RDX_ARCHIVE_AND_ABI_ARE_PINNED' `
    ($ArchiveHash -eq 'C540D70540DC4D61E15D1CA13579CD2342D4EA972FF0A74A1AFCCC04B1EF4ACA' -and
     $ProtocolHeaderText -match '(?s)typedef\s+struct\s*\{\s*int\s+app_select;\s*int\s+device_select;\s*char\s*\*fw_version;\s*char\s*\*hw_version;\s*void\s*\(\*rdx_protocol_cb\)\(ProtocolEvents event, void\* data, u32 len\);\s*\}\s*RdxProtocolCallbacks;' -and
     $AppText -match 'void\s+rdx_app_custom_command_parse\s*\(\s*char\s*\*\s*cmd\s*,\s*char\s*\*\s*value\s*\)' -and
     $ServerHeaderText -match 'ble_state_e\s+ble_work_state;' -and
     $ServerHeaderText -match 'u16\s+ble_con_handle;' -and
     $ServerHeaderText -match 'void\s*\*rdx_ble_server_hdl;' -and
     $ServerHeaderText -match 'char\s+ble_local_name\[BLE_LOCAL_NAME_MAX_LEN\s*\+\s*1\];' -and
     $ServerHeaderText -match '\}\s*rdx_ble_server_info_t;') `
    'the immutable archive and its callback/server ABI must not change'

Test-Contract 'PHASE3_RUNTIME_HAS_EVENT_DRIVEN_REARM_STATES' `
    ($SessionHeaderText -match '(?s)RDX_BLE_RUNTIME_ACTIVE,.*?RDX_BLE_RUNTIME_QUIESCING,.*?RDX_BLE_RUNTIME_RESETTING,.*?RDX_BLE_RUNTIME_FAILED' -and
     $QuiesceBody -match 's_rdx_runtime_state\s*=\s*RDX_BLE_RUNTIME_QUIESCING' -and
     $BarrierArriveBody -match 's_rdx_runtime_state\s*=\s*RDX_BLE_RUNTIME_RESETTING' -and
     $RearmBody -match 's_rdx_runtime_state\s*=\s*RDX_BLE_RUNTIME_READY') `
    'normal reconnect must follow ACTIVE -> QUIESCING -> RESETTING -> READY'

Test-Contract 'PHASE3_DISCONNECT_INVALIDATES_BEFORE_BARRIER' `
    ($QuiesceBody -match 'link->rdx_runtime_active\s*=\s*0' -and
     $QuiesceBody -match 'rdx_ble_session_runtime_epoch_advance\s*\(' -and
     $DetachBody.IndexOf('rdx_ble_session_rdx_runtime_begin_quiesce(link)') -ge 0 -and
     $DetachBody.IndexOf('rdx_ble_session_rdx_runtime_begin_quiesce(link)') -lt
        $DetachBody.IndexOf('rdx_ble_server_rdx_disconnected_cleanup_internal()') -and
     $DetachBody.IndexOf('rdx_ble_server_rdx_disconnected_cleanup_internal()') -lt
        $DetachBody.IndexOf('rdx_protocol_packet_recv(barrier_packet')) `
    'transport/runtime tokens and ATT input must close before old receive work drains'

Test-Contract 'PHASE3_RECEIVE_FIFO_BARRIER_USES_PRIVATE_NONCE' `
    ($DetachBody -match 'sprintf\s*\(\s*g_rdx_lifecycle_barrier_value\s*,\s*"%08x%08x"\s*,\s*\(unsigned int\)rand32\(\)\s*,\s*\(unsigned int\)rand32\(\)\s*\)' -and
     $DetachBody -match 'RDX_LIFECYCLE_CUSTOM_CMD' -and
     $DetachBody -match 'rdx_protocol_packet_recv\s*\(\s*barrier_packet\s*,\s*barrier_packet_len\s*\)' -and
     $ServerText -match 'g_rdx_lifecycle_barrier_armed\s*&&\s*value' -and
     $ServerText -match 'strcmp\s*\(\s*value\s*,\s*g_rdx_lifecycle_barrier_value\s*\)') `
    'the receive FIFO marker must be locally generated and nonce-authenticated'

Test-Contract 'PHASE3_RECEIVE_DRAIN_ADMISSION_KEEPS_SEND_CLOSED' `
    ($GetInfoBody -match '(?s)g_rdx_lifecycle_barrier_armed\s*&&.*?RDX_BLE_RUNTIME_QUIESCING.*?g_rdx_ble_server_info\.ble_conn\s*=\s*TRUE' -and
     $GetInfoBody.IndexOf('g_rdx_ble_server_info.ble_con_handle = 0') -ge 0 -and
     $GetInfoBody.IndexOf('g_rdx_ble_server_info.ble_con_handle = 0') -lt
        $GetInfoBody.IndexOf('g_rdx_lifecycle_barrier_armed') -and
     $GetInfoBody -notmatch '(?s)g_rdx_lifecycle_barrier_armed.*?g_rdx_ble_server_info\.ble_con_handle\s*=\s*[^0]' -and
     $BarrierCompleteBody -match '(?s)g_rdx_lifecycle_barrier_armed\s*=\s*0.*?g_rdx_ble_server_info\.ble_conn\s*=\s*FALSE.*?rdx_ble_server_rdx_session_abort') `
    'the immutable parser may drain to the nonce marker while all legacy send paths still observe handle zero'

Test-Contract 'PHASE3_BARRIER_COMPLETION_REABORTS_OLD_WORK' `
    ($BarrierCompleteBody.IndexOf('rdx_ble_session_rdx_runtime_barrier_arrive()') -ge 0 -and
     $BarrierCompleteBody.IndexOf('rdx_ble_session_rdx_runtime_barrier_arrive()') -lt
        $BarrierCompleteBody.IndexOf('rdx_ble_server_rdx_session_abort()') -and
     $BarrierCompleteBody.IndexOf('rdx_ble_server_rdx_session_abort()') -lt
        $BarrierCompleteBody.IndexOf('rdx_ble_server_rdx_runtime_try_rearm()') -and
     $AbortBody -match 'rdx_protocol_bleFileUpload_cancel' -and
     $AbortBody -match 'rdx_protocol_stop_loop_fileTransfer' -and
     $AbortBody -match 'rdx_protocol_recordFileData_sendFail_pending_stop' -and
     $AbortBody -match 'rdx_protocol_bulk_send_timer_stop') `
    'old receive packets may start work, so cancellation must repeat after FIFO arrival'

Test-Contract 'PHASE3_SEND_WORKER_IS_EXPLICITLY_QUIESCED' `
    ($DetachBody.IndexOf('rdx_ble_server_set_conn_handle(0)') -ge 0 -and
     $DetachBody.IndexOf('rdx_ble_server_set_conn_handle(0)') -lt
        $DetachBody.IndexOf('rdx_ble_server_rdx_send_worker_quiesce()') -and
     $SendQuiesceBody -match 'rdx_protocol_set_ble_sent\s*\(\s*0\s*\)' -and
     $SendQuiesceBody -match 'bulk_data->bulk_flag\s*=\s*false' -and
     $SendQuiesceBody -match 'os_sem_post\s*\(\s*&send_data->send_sem\s*\)') `
    'the legacy send task must wake against a cleared connection instead of awaiting stale CAN_SEND_NOW'

Test-Contract 'PHASE3_REARM_WAITS_FOR_ALL_OBSERVABLE_WORKERS' `
    ($TryRearmBody.IndexOf('rdx_app_rdx_rebind_is_idle()') -ge 0 -and
     $TryRearmBody.IndexOf('rdx_app_rdx_rebind_is_idle()') -lt
        $TryRearmBody.IndexOf('rdx_ble_server_rdx_session_reset_finalize()') -and
     $TryRearmBody.IndexOf('rdx_ble_server_rdx_session_reset_finalize()') -lt
        $TryRearmBody.IndexOf('rdx_ble_session_rdx_runtime_rearm()') -and
     $IdleBody -match 'rdx_pc_storage_is_busy\s*\(' -and
     $IdleBody -match 'send_data->send_pending' -and
     $IdleBody -match 'send_data->bulk_sending' -and
     $IdleBody -match 'bulk_data->busy' -and
     $IdleBody -match 'bulk_data->bulk_flag') `
    'READY must depend on observed receive/file/record/OTA/send/bulk quiescence'

Test-Contract 'PHASE3_FINAL_CLEANUP_PRECEDES_READY' `
    ($FinalizeBody -match 'rdx_protocol_bulk_data_send_para_reset' -and
     $FinalizeBody -match 'rdx_protocol_prepared_data_clean' -and
     $FinalizeBody -match 'rdx_protocol_uploadFileInfo_clean' -and
     $FinalizeBody -match 'rdx_protocol_send_buffer_reinit' -and
     $TryRearmBody.IndexOf('rdx_ble_server_rdx_session_reset_finalize()') -lt
        $TryRearmBody.IndexOf('rdx_ble_session_rdx_runtime_rearm()')) `
    'destructive singleton cleanup must run only after workers are idle and before READY'

Test-Contract 'PHASE3_REBIND_IS_RESTRICTED_TO_SAME_PEER' `
    ($PeerCaptureBody -match 'link->peer_identity_valid' -and
     $PeerCaptureBody -match 'link->peer_addr_type' -and
     $PeerCheckBody -match 'return\s+RDX_BLE_CLAIM_NOT_READY' -and
     $PeerCheckBody -match 'memcmp\s*\(\s*s_rdx_rebind_peer_addr\s*,\s*link->peer_identity' -and
     $PeerCheckBody -match 'link->peer_addr_type\s*==\s*s_rdx_rebind_peer_addr_type' -and
     $ClaimBody -match '(?s)s_rdx_runtime_consumed_this_boot.*?rdx_ble_session_rebind_peer_check\s*\(\s*link\s*\)') `
    'the immutable singleton may only rebind to the disconnected peer'

Test-Contract 'PHASE3_ENCRYPTED_PEER_USES_SM_IDENTITY' `
    ($PacketHandlerBody -match '(?s)if\s*\(\s*encrypted\s*\).*?get_sm_peer_address\s*\(\s*peer_identity\s*\).*?rdx_ble_session_link_set_peer_identity' -and
     $SessionHeaderText -match 'u8\s+peer_identity_valid;' -and
     $SessionHeaderText -match 'u8\s+peer_identity\[6\];') `
    'bonded reconnects must compare the resolved identity rather than a rotating RPA'

Test-Contract 'PHASE3_FAILURE_PATHS_REMAIN_CLOSED' `
    ($DetachBody -match '(?s)rdx_protocol_packet_recv\s*\(.*?!=\s*barrier_packet_len.*?rdx_ble_session_rdx_runtime_fail_closed\s*\(' -and
     $RearmBody -match '(?s)!s_rdx_rebind_peer_valid.*?RDX_BLE_RUNTIME_FAILED' -and
     $ServerText -notmatch 'RDX_LIFECYCLE_BARRIER_TIMEOUT' -and
     $BarrierArriveBody -notmatch 'RDX_BLE_RUNTIME_READY') `
    'enqueue/identity failure must fail closed, and a missing barrier must never time into READY'

Test-Contract 'PHASE3_NORMAL_CALLBACKS_REQUIRE_ACTIVE_RUNTIME' `
    ($CustomBody.IndexOf('rdx_ble_server_rdx_lifecycle_barrier_match(value)') -ge 0 -and
     $CustomBody.IndexOf('rdx_ble_server_rdx_lifecycle_barrier_match(value)') -lt
        $CustomBody.IndexOf('RDX_BLE_RUNTIME_ACTIVE') -and
     $CustomBody -match '(?s)RDX_BLE_RUNTIME_ACTIVE.*?stale custom command dropped' -and
     $ProtocolEventBody -match '(?s)RDX_BLE_RUNTIME_ACTIVE.*?stale protocol event dropped') `
    'only the authenticated barrier may bypass the ACTIVE callback gate'

Test-Contract 'PHASE3_ATTACH_RETRIES_CONDITION_BASED_REARM' `
    ($AttachBody -match '(?s)RDX_BLE_RUNTIME_RESETTING.*?rdx_ble_server_rdx_runtime_try_rearm\s*\(\s*\).*?rdx_ble_session_claim_rdx' -and
     $DetachBody -notmatch 'sys_timeout_add' -and
     $ServerText -notmatch 'sys_timeout_add\s*\(\s*NULL\s*,\s*rdx_ble_server_disconnected_delay_handle\s*,\s*500\s*\)') `
    'later ATT access may retry observed-idle rearm without treating elapsed time as a barrier'

Test-Contract 'PHASE3_DOES_NOT_RECREATE_LIBRARY_TASKS' `
    (($DetachBody + $BarrierCompleteBody + $TryRearmBody) -notmatch 'rdx_protocol_task_(free|create)|rdx_uxfile_task_free|rdx_uxfile_init|os_task_(create|del)') `
    'the audited unsafe destroy/recreate path must remain unused'

Test-Contract 'PHASE3_HID_LIFECYCLE_REMAINS_INDEPENDENT' `
    ($DisconnectBody -match '(?s)if\s*\(\s*rdx_ble_session_link_is_hid\s*\(\s*link\s*\)\s*\).*?rdx_hogp_on_disconnected' -and
     $DisconnectBody -match '(?s)if\s*\(\s*rdx_ble_session_link_is_rdx\s*\(\s*link\s*\)\s*\).*?rdx_ble_server_phase2_rdx_detach' -and
     ($DetachBody + $BarrierCompleteBody + $TryRearmBody) -notmatch 'rdx_hogp_(on_disconnected|deinit)') `
    'RDX quiesce/rearm must not reset the independent HID owner or report runtime'

Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host 'All RDX dual-link Phase 3 reconnect lifecycle contracts passed.'
    exit 0
}

Write-Host "$Failed RDX dual-link Phase 3 reconnect lifecycle contract check(s) failed."
exit 1
