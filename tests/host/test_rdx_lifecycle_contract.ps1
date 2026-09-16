#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$ProtocolRoot = 'SDK\apps\common\third_party_profile\rdx_protocol'
$Session = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_session.c"
$SessionHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_session.h"
$Server = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_server.c"
$ServerHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ble_server.h"
$ProtocolHeader = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_protocol.h"
$App = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_app.c"
$ArchiveHash = (Get-FileHash -Algorithm SHA256 `
    (Join-Path $RepoRoot "$ProtocolRoot\librdxApp.a")).Hash

$Claim = Get-SourceSlice $Session `
    'rdx_ble_claim_result_t rdx_ble_session_claim_rdx(' `
    'rdx_ble_claim_result_t rdx_ble_session_claim_hid('
$Quiesce = Get-SourceSlice $Session `
    'u8 rdx_ble_session_rdx_runtime_begin_quiesce(' `
    'u8 rdx_ble_session_rdx_runtime_barrier_arrive('
$BarrierArrive = Get-SourceSlice $Session `
    'u8 rdx_ble_session_rdx_runtime_barrier_arrive(' `
    'u8 rdx_ble_session_rdx_runtime_rearm('
$Rearm = Get-SourceSlice $Session `
    'u8 rdx_ble_session_rdx_runtime_rearm(' `
    'void rdx_ble_session_rdx_runtime_fail_closed('
$Detach = Get-SourceSlice $Server `
    'static void rdx_ble_server_phase2_rdx_detach(' `
    '#if TCFG_RDX_HOGP_ENABLE' -Last
$Abort = Get-SourceSlice $Server `
    'static void rdx_ble_server_rdx_session_abort(' `
    'static void rdx_ble_server_rdx_send_worker_quiesce(' -Last
$TryRearm = Get-SourceSlice $Server `
    'static u8 rdx_ble_server_rdx_runtime_try_rearm(' `
    'u8 rdx_ble_server_rdx_lifecycle_barrier_match(' -Last
$BarrierComplete = Get-SourceSlice $Server `
    'void rdx_ble_server_rdx_lifecycle_barrier_complete(' `
    'static void rdx_ble_server_link_disconnected_cleanup_internal('
$Disconnect = Get-SourceSlice $Server `
    'static void rdx_ble_server_phase0a_link_disconnected(' `
    'static void rdx_ble_server_phase0a_packet_handler('
$GetInfo = Get-SourceSlice $Server `
    'rdx_ble_server_info_t * rdx_ble_server_get_info(void)' `
    '/**************************************************************************'
$Idle = Get-SourceSlice $App `
    'u8 rdx_app_rdx_rebind_is_idle(void)' `
    '/**************************************************************************'
$StorageActivity = Get-SourceSlice $App `
    'static u8 rdx_app_storage_activity_is_busy(' `
    'u8 rdx_pc_storage_is_busy(void)'

$abiOk = $ArchiveHash -eq '4289EC0F6D923EC9337A5DBE57F8D720BCC4946601B7D8393F9E2878B16F5C7D' -and
         $ProtocolHeader -match '(?s)typedef\s+struct\s*\{\s*int\s+app_select;\s*int\s+device_select;.*?rdx_protocol_cb.*?\}\s*RdxProtocolCallbacks;' -and
         $ServerHeader -match '(?s)ble_state_e\s+ble_work_state;.*?u16\s+ble_con_handle;.*?void\s*\*rdx_ble_server_hdl;.*?\}\s*rdx_ble_server_info_t;'
Assert-Contract 'IMMUTABLE_RUNTIME_ABI' $abiOk `
    'the prebuilt RDX singleton and its source-facing ABI must remain unchanged'

$stateOk = $SessionHeader -match '(?s)RDX_BLE_RUNTIME_ACTIVE,.*?RDX_BLE_RUNTIME_QUIESCING,.*?RDX_BLE_RUNTIME_RESETTING,.*?RDX_BLE_RUNTIME_FAILED' -and
           $Quiesce -match 'link->rdx_runtime_active\s*=\s*0' -and
           $Quiesce -match 'rdx_ble_session_runtime_epoch_advance\s*\(' -and
           $Quiesce -match 'RDX_BLE_RUNTIME_QUIESCING' -and
           $BarrierArrive -match 'RDX_BLE_RUNTIME_RESETTING' -and
           $Rearm -match 'RDX_BLE_RUNTIME_READY'
Assert-Contract 'RECONNECT_STATE_MACHINE' $stateOk `
    'disconnect must invalidate the old epoch before QUIESCING -> RESETTING -> READY'

$barrierOrder = @(
    'rdx_ble_session_rdx_runtime_begin_quiesce(link)',
    'rdx_ble_server_rdx_disconnected_cleanup_internal()',
    'rdx_protocol_packet_recv(barrier_packet, barrier_packet_len)'
)
$barrierOk = Test-TokensInOrder $Detach $barrierOrder
$barrierOk = $barrierOk -and
             $Detach -match '(?s)sprintf\s*\(\s*g_rdx_lifecycle_barrier_value\s*,\s*"%08x%08x".*?rand32\(\).*?rand32\(\)' -and
             $Server -match 'g_rdx_lifecycle_barrier_armed\s*&&\s*value' -and
             $Server -match 'strcmp\s*\(\s*value\s*,\s*g_rdx_lifecycle_barrier_value\s*\)'
Assert-Contract 'NONCE_FIFO_BARRIER' $barrierOk `
    'old receive work must drain behind a locally generated, authenticated FIFO marker'

$sendClosedOk = $GetInfo -match '(?s)g_rdx_ble_server_info\.ble_con_handle\s*=\s*0.*?g_rdx_lifecycle_barrier_armed.*?RDX_BLE_RUNTIME_QUIESCING.*?ble_conn\s*=\s*TRUE' -and
                $GetInfo -notmatch '(?s)g_rdx_lifecycle_barrier_armed.*?ble_con_handle\s*=\s*[^0]' -and
                $BarrierComplete -match '(?s)g_rdx_lifecycle_barrier_armed\s*=\s*0.*?ble_conn\s*=\s*FALSE'
Assert-Contract 'BARRIER_ADMISSION_SEND_CLOSED' $sendClosedOk `
    'the parser-only drain window must keep the legacy connection handle and send paths closed'

$drainOk = Test-TokensInOrder $BarrierComplete @(
    'rdx_ble_session_rdx_runtime_barrier_arrive()',
    'rdx_ble_server_rdx_session_abort()',
    'rdx_ble_server_rdx_runtime_try_rearm()'
)
$drainOk = $drainOk -and
           $Abort -match 'rdx_protocol_bleFileUpload_cancel' -and
           $Abort -match 'rdx_protocol_stop_loop_fileTransfer' -and
           $Abort -match 'rdx_protocol_recordFileData_sendFail_pending_stop' -and
           $Abort -match 'rdx_protocol_bulk_send_timer_stop'
Assert-Contract 'OLD_WORK_IS_REABORTED' $drainOk `
    'work started before FIFO arrival must be cancelled again before rearm'

$idleOk = Test-TokensInOrder $TryRearm @(
    'rdx_app_rdx_rebind_is_idle()',
    'rdx_ble_server_rdx_session_reset_finalize()',
    'rdx_ble_session_rdx_runtime_rearm()'
)
$idleOk = $idleOk -and
          $Idle -notmatch 'rdx_pc_storage_is_busy\s*\(' -and
          $Idle -match 'rdx_app_storage_activity_is_busy\s*\(\s*"RDX_BLE_SESSION"\s*,\s*1\s*\)' -and
          $StorageActivity -match 'rdx_record_get_status\s*\(' -and
          $StorageActivity -match 'rdx_record_process_is_busy_check\s*\(' -and
          $StorageActivity -match 'rdx_playback_get_info\s*\(' -and
          $StorageActivity -match 'PB_STATE_UNREADY' -and
          $StorageActivity -match 'PB_STATE_STOPPED' -and
          $StorageActivity -match 'allow_paused_playback\s*&&\s*playback\.state\s*==\s*PB_STATE_PAUSED' -and
          $StorageActivity -match 'rdx_is_file_transfer_active\s*\(' -and
          $StorageActivity -match 'rdx_is_file_sync_busy\s*\(' -and
          $Idle -match 'send_data->send_pending' -and
          $Idle -match 'send_data->bulk_sending' -and
          $Idle -match 'bulk_data->busy' -and
          $Idle -match 'bulk_data->bulk_flag'
Assert-Contract 'REARM_REQUIRES_WORKER_IDLE' $idleOk `
    'READY must follow observable file, record, OTA, send and bulk worker quiescence without treating paused playback as PC-storage ownership'

$handoffOk = $Claim -match '(?s)s_rdx_runtime_state\s*!=\s*RDX_BLE_RUNTIME_READY.*?return\s+RDX_BLE_CLAIM_NOT_READY.*?rdx_ble_session_claim\s*\(' -and
             $Claim -notmatch 'peer_(addr|identity)|rebind_peer' -and
             $Quiesce -notmatch 'rebind_peer' -and
             $Rearm -match 'RDX_BLE_RUNTIME_READY' -and
             $TryRearm -match '(?s)rdx_app_rdx_rebind_is_idle\s*\(\).*?rdx_ble_server_rdx_session_reset_finalize\s*\(\).*?rdx_ble_session_rdx_runtime_rearm\s*\('
Assert-Contract 'CROSS_PEER_HANDOFF_REQUIRES_FULL_RESET' $handoffOk `
    'a new peer may claim only after the old epoch, FIFO work, workers and buffers have fully converged to READY'

$closedOk = $Detach -match '(?s)rdx_protocol_packet_recv\s*\(.*?!=\s*barrier_packet_len.*?rdx_ble_session_rdx_runtime_fail_closed\s*\(' -and
            $Server -notmatch 'RDX_LIFECYCLE_BARRIER_TIMEOUT' -and
            $BarrierArrive -notmatch 'RDX_BLE_RUNTIME_READY' -and
            $TryRearm -match '(?s)!rdx_ble_session_rdx_runtime_rearm\s*\(\).*?rdx_ble_session_rdx_runtime_fail_closed\s*\('
Assert-Contract 'LIFECYCLE_FAILURES_STAY_CLOSED' $closedOk `
    'barrier or rearm failure must never time out or fall through to READY'

$isolationOk = ($Detach + $BarrierComplete + $TryRearm) -notmatch 'rdx_protocol_task_(free|create)|rdx_uxfile_task_free|rdx_uxfile_init|os_task_(create|del)' -and
               ($Detach + $BarrierComplete + $TryRearm) -notmatch 'rdx_hogp_(on_disconnected|deinit)' -and
               $Disconnect -match '(?s)rdx_ble_session_link_is_hid.*?rdx_hogp_on_disconnected' -and
               $Disconnect -match '(?s)rdx_ble_session_link_is_rdx.*?rdx_ble_server_phase2_rdx_detach'
Assert-Contract 'RUNTIME_REUSE_PRESERVES_HID' $isolationOk `
    'reconnect must reuse the audited singleton without resetting an independent HID owner'

$Control = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_session_control.c"
$Release = Get-SourceSlice $Session 'u8 rdx_ble_session_release_rdx(' 'u8 rdx_ble_session_rdx_runtime_begin_quiesce('
Assert-Contract 'LOGICAL_RELEASE_PRESERVES_PHYSICAL_LINK' (
    $Release -match 'capability\s*&=\s*~RDX_BLE_CAPABILITY_RDX' -and
    $Release -match 's_rdx_rdx_link_index\s*=\s*RDX_BLE_LINK_INVALID_INDEX' -and
    $Release -notmatch 'link_release|generation_advance|link_clear|s_rdx_hid_link_index\s*=' -and
    $Release -notmatch 'link->(connected|con_handle|slot_generation|encrypted|mtu_size|peer_identity)\s*=' -and
    (Test-TokensInOrder $Detach @('rdx_ble_session_rdx_runtime_begin_quiesce(link)', 'rdx_ble_session_release_rdx(link, link->slot_generation)', 'rdx_session_control_reset()', 'rdx_protocol_packet_recv(barrier_packet, barrier_packet_len)'))
) 'logical close clears only RDX and still uses the common FIFO cleanup'
Assert-Contract 'SINGLE_COMMAND_RELEASE' (
    $Control -match 'strcmp\(value, "1"\)' -and
    $Control -match '\*token = ingress' -and
    $Control -match 'rdx_ble_session_rdx_token_resolve\(token, 1\)' -and
    $Control -match 'os_taskq_post_type\("btstack", Q_CALLBACK' -and
    $App -match 'rdx_session_control_handle_custom\(value\)'
) 'one custom command carries an internal epoch to the serialized cleanup'

$Ota = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_ota.c"
$OtaMatch = Get-SourceSlice $Server 'static u8 rdx_ble_server_is_ota_stop_command(' 'static int rdx_ble_server_phase2_rdx_write('
$RdxWrite = Get-SourceSlice $Server 'static int rdx_ble_server_phase2_rdx_write(' 'static int rdx_ble_server_phase0a_hogp_control_write('
$OtaCommand = Get-SourceSlice $RdxWrite '/* A stop must never claim' 'switch (att_handle)'
$OtaStop = Get-SourceSlice $Ota 'void rdx_ota_stop(void)' 'void rdx_ota_init(void)' -Last
Assert-Contract 'OTA_CANCEL_IS_OWNER_SCOPED' (
    $OtaMatch -match 'CMD_DL_OTA_CTRL "0#"' -and
    $OtaMatch -match 'len == sizeof\(command\) - 1' -and
    $OtaMatch -match 'memcmp' -and
    $OtaCommand -match '06068D1C' -and $OtaCommand -match '00239A7F' -and
    (Test-TokensInOrder $OtaCommand @('!rdx_ble_session_link_is_rdx(link)', 'return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR', 'rdx_ota_stop()')) -and
    $OtaCommand -notmatch 'rdx_attach' -and
    $RdxWrite -match '(?s)if \(cfg == 0x0000\).*?00239A8F.*?rdx_ble_session_link_is_rdx\(link\) && link->rdx_runtime_active\).*?rdx_ota_stop\(\)'
) 'both write channels and OTA CCC cancellation must respect the current owner'
Assert-Contract 'OTA_CANCEL_PRESERVES_SESSION_AND_REJECTS_LATE_DATA' (
    (Test-TokensInOrder $OtaStop @('rdx_ota_session_clear()', 'rdx_ota_get_data_timer_stop()', 'if (!get_ota_status())', 'return;', 'set_ota_status(0)', 'dual_bank_passive_update_exit(NULL)')) -and
    $OtaStop -notmatch 'app_disconnect|rdx_detach|rdx_session_abort|rdx_hogp_' -and
    $RdxWrite -match '(?s)!get_ota_status\(\).*?CMD_DL_UPGRADE.*?return 0;.*?rdx_protocol_ota_handle' -and
    $Ota -match '(?s)int rdx_ota_get_data_handler\(.*?!get_ota_status\(\).*?return E_PROTOCOL_ECODE_FAIL;.*?_rdx_ota_split_params'
) 'cancel closes OTA before teardown, preserves BLE ownership and gates trailing data'

$OtaEnd = Get-SourceSlice $Ota 'void rdx_ota_end(void)' 'void rdx_ota_get_data_timer_stop(void)'
$OtaTimeout = Get-SourceSlice $Ota 'static void rdx_ota_get_data_timeout_cb(void *priv)' 'void rdx_ota_get_data_timer_rerun(void)'
$OtaVerify = Get-SourceSlice $Ota 'int rdx_ota_file_end_response(void *priv)' 'void rdx_ota_end(void)'
$OtaBoot = Get-SourceSlice $Ota 'int rdx_ota_boot_info_cb(int err)' 'int rdx_ota_clk_resume(int priv)'
Assert-Contract 'OTA_FINALIZATION_CLOSES_USER_CANCEL' (
    (Test-TokensInOrder $OtaEnd @('g_rdx_ota_finalizing = 1', 'dual_bank_update_write')) -and
    (Test-TokensInOrder $OtaTimeout @('if (g_rdx_ota_finalizing)', 'return;', 'rdx_ota_stop()')) -and
    (Test-TokensInOrder $OtaCommand @('rdx_ota_is_finalizing()', 'return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR', 'rdx_ota_stop()')) -and
    $RdxWrite -match '(?s)if \(cfg == 0x0000\).*?rdx_ota_is_finalizing\(\).*?return RDX_BLE_PHASE0A_ATT_ERR_UNLIKELY_ERROR;.*?multi_att_set_ccc_config' -and
    $Ota -match '(?s)if\(otaPara.cur_pack_num == otaPara.pack_total\).*?rdx_ota_get_data_timer_stop\(\);.*?rdx_ota_end\(\);'
) 'final write/verify/commit rejects user cancellation before mutating CCC and stops the transfer timer first'
Assert-Contract 'OTA_FINAL_CALLBACKS_REJECT_INVALID_SESSION' (
    (Test-TokensInOrder $OtaVerify @('!rdx_ota_session_is_current()', 'return -1;', 'dual_bank_update_verify_without_crc', '!rdx_ota_session_is_current()', 'return -1;', 'dual_bank_update_burn_boot_info')) -and
    (Test-TokensInOrder $OtaBoot @('!rdx_ota_session_is_current()', 'return -1;', 'sys_timeout_add'))
) 'invalidated OTA sessions cannot enter verification or continue from verification to commit/reset scheduling'

$Record = Read-RepoFile $RepoRoot "$ProtocolRoot\rdx_record.c"
$StopRequest = Get-SourceSlice $Record `
    'static void rdx_record_app_stop_request(const rdx_ble_async_token_t *token)' `
    '/* app_core only.' -Last
$StopPump = Get-SourceSlice $Record `
    'static void rdx_record_app_stop_pump(void *priv)' `
    'static void rdx_record_app_stop_tick(void *priv)'
$RecordCommand = Get-SourceSlice $Record `
    'static void rdx_record_cmd_handle_internal(' `
    'void rdx_record_cmd_handle(Record_info *r_info)' -Last
$RecordWorker = Get-SourceSlice $Record `
    'static void rdx_record_task(void *arg)' `
    'int rdx_record_task_create(void)'

Assert-Contract 'OFFLINE_RECORD_STOP_HAS_COMMAND_RECIPIENT' (
    (Test-TokensInOrder $RecordCommand @('!rdx_record_rdx_token_is_current(token)',
        '!rdx_record_online_session_accepts(token)', 'rdx_record_app_stop_request(token)',
        'record cmd job is same as current')) -and
    $StopRequest -match 'g_app_stop_token = \*token' -and
    $StopRequest -notmatch 'online_session_bind|token_capture' -and
    (Test-TokensInOrder $StopPump @('g_app_stop_done != ticket',
        'rdx_record_rdx_token_is_current(&g_app_stop_token)',
        'rdx_protocol_record_state_indicate()')) -and
    $StopPump -notmatch 'online_session_is_current|online_session_token_is_current'
) 'App STOP must reply to its validated command epoch even when the recording started offline'

Assert-Contract 'RECORD_STOP_COMPLETION_FOLLOWS_WORKER_CLEANUP' (
    (Test-TokensInOrder $RecordWorker @('msg[1] == RDX_RECORD_APP_STOP_FENCE',
        'msg[1] = RECORD_STATE_STOP', 'translation_ear_recoder_close_all()',
        'if (app_stop_ticket)', 'g_app_stop_done = app_stop_ticket')) -and
    $RecordWorker -match '(?s)translation_ear_recoder_close_all\(\);\s*/\* A paused session.*?rdx_uxfile_finish_record\(\).*?if \(app_stop_ticket\).*?g_app_stop_done' -and
    $Record -match '!g_app_stop_pending && rdx_record_online_session_is_current\(\)' -and
    $StopPump -match '!g_app_stop_pending \|\| ticket != g_app_stop_ticket'
) 'STOP completion must wait for both audio paths and paused-file finalization, and reject stale callbacks'

Assert-Contract 'RECORD_STOP_RETRY_IS_BOUNDED_AND_IDEMPOTENT' (
    $StopRequest -match '(?s)if \(g_app_stop_pending\).*?return;' -and
    $StopRequest -match 'record_status.run != RECORD_STATE_STOP \|\|' -and
    $RecordWorker -match '(?s)if \(!msg\[3\]\).*?g_app_stop_done = app_stop_ticket;\s*continue;' -and
    $StopPump -match '(?s)if \(!g_app_stop_posted\).*?if \(os_taskq_post_msg.*?return;.*?g_app_stop_posted = 1;' -and
    $StopRequest -match 'sys_timer_add' -and
    $StopRequest -notmatch 'malloc' -and
    (Test-TokensInOrder $StopRequest @('sys_timer_add', 'if (!g_app_stop_timer)',
        'g_app_stop_pending = 1', 'sys_timeout_del(g_record_cmd_delay_timer)',
        'rdx_record_start_tone_cancel()', 'record_status.run = RECORD_STATE_STOP')) -and
    $RecordWorker -match '(?s)RECORD_STATE_RESUME\) &&\s*\(g_app_stop_pending \|\|' -and
    $Record -match '(?s)bool rdx_record_process_is_busy_check\(void\)\s*\{.*?if \(g_app_stop_pending\).*?return TRUE;' -and
    (Test-TokensInOrder $RecordCommand @('rdx_record_app_stop_request(token)',
        'if (g_app_stop_pending)', 'command rejected: STOP pending', 'if(r_info->cmd == (RECORD_STATE_START + 0x30))')) -and
    $App -match 'run == RECORD_STATE_START && rdx_record_process_is_busy_check\(\)'
) 'coalesce retries, observe already-stopped state without closing twice, retry queue pressure and fence new starts'

Assert-Contract 'RECORD_STOP_RETAINS_POWER_AND_STORAGE_BUSY_FENCE' (
    (Test-TokensInOrder $StopRequest @('g_app_stop_pending = 1',
        'rdx_record_set_process_state_busy()', 'record_status.run = RECORD_STATE_STOP')) -and
    $Record -match '(?s)void rdx_record_set_process_state_ready\(void\)\s*\{\s*if \(g_app_stop_pending\).*?return;.*?record_status.process_state = REC_PROCESS_STATE_READY' -and
    (Test-TokensInOrder $StopPump @('g_app_stop_done != ticket',
        'g_app_stop_pending = 0', 'rdx_record_set_process_state_ready()'))
) 'shared power and storage users must continue to see BUSY until the complete worker fence has returned'

Write-Host 'RDX lifecycle contracts passed.'
