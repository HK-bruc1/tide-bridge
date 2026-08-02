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
$PeerCapture = Get-SourceSlice $Session `
    'static void rdx_ble_session_rebind_peer_capture(' `
    'static rdx_ble_claim_result_t rdx_ble_session_rebind_peer_check('
$PeerCheck = Get-SourceSlice $Session `
    'static rdx_ble_claim_result_t rdx_ble_session_rebind_peer_check(' `
    'void rdx_ble_session_transport_init('
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
$PacketHandler = Get-SourceSlice $Server `
    'static void rdx_ble_server_phase0a_packet_handler(' `
    'static void rdx_ble_server_cbk_packet_handler('
$Disconnect = Get-SourceSlice $Server `
    'static void rdx_ble_server_phase0a_link_disconnected(' `
    'static void rdx_ble_server_phase0a_packet_handler('
$GetInfo = Get-SourceSlice $Server `
    'rdx_ble_server_info_t * rdx_ble_server_get_info(void)' `
    '/**************************************************************************'
$Idle = Get-SourceSlice $App `
    'u8 rdx_app_rdx_rebind_is_idle(void)' `
    '/**************************************************************************'

$abiOk = $ArchiveHash -eq 'C540D70540DC4D61E15D1CA13579CD2342D4EA972FF0A74A1AFCCC04B1EF4ACA' -and
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
          $Idle -match 'rdx_pc_storage_is_busy\s*\(' -and
          $Idle -match 'send_data->send_pending' -and
          $Idle -match 'send_data->bulk_sending' -and
          $Idle -match 'bulk_data->busy' -and
          $Idle -match 'bulk_data->bulk_flag'
Assert-Contract 'REARM_REQUIRES_WORKER_IDLE' $idleOk `
    'READY must follow observable file, record, OTA, send and bulk worker quiescence'

$peerOk = $PeerCapture -match 'link->peer_identity_valid' -and
          $PeerCapture -match 'link->peer_addr_type' -and
          $PeerCheck -match 'memcmp\s*\(\s*s_rdx_rebind_peer_addr\s*,\s*link->peer_identity' -and
          $Claim -match '(?s)s_rdx_runtime_consumed_this_boot.*?rdx_ble_session_rebind_peer_check\s*\(' -and
          $PacketHandler -match '(?s)if\s*\(\s*encrypted\s*\).*?get_sm_peer_address_for_hci_handle\s*\(\s*con_handle\s*,\s*peer_identity\s*\).*?rdx_ble_session_link_set_peer_identity'
Assert-Contract 'REBIND_REQUIRES_SAME_SM_PEER' $peerOk `
    'a consumed singleton may only rebind to the same resolved SM identity'

$closedOk = $Detach -match '(?s)rdx_protocol_packet_recv\s*\(.*?!=\s*barrier_packet_len.*?rdx_ble_session_rdx_runtime_fail_closed\s*\(' -and
            $Rearm -match '(?s)!s_rdx_rebind_peer_valid.*?RDX_BLE_RUNTIME_FAILED' -and
            $Server -notmatch 'RDX_LIFECYCLE_BARRIER_TIMEOUT' -and
            $BarrierArrive -notmatch 'RDX_BLE_RUNTIME_READY'
Assert-Contract 'LIFECYCLE_FAILURES_STAY_CLOSED' $closedOk `
    'identity or barrier failure must never time out or fall through to READY'

$isolationOk = ($Detach + $BarrierComplete + $TryRearm) -notmatch 'rdx_protocol_task_(free|create)|rdx_uxfile_task_free|rdx_uxfile_init|os_task_(create|del)' -and
               ($Detach + $BarrierComplete + $TryRearm) -notmatch 'rdx_hid_service_(on_disconnected|deinit)' -and
               $Disconnect -match '(?s)rdx_ble_session_link_is_hid.*?rdx_hid_service_on_disconnected' -and
               $Disconnect -match '(?s)rdx_ble_session_link_is_rdx.*?rdx_ble_server_phase2_rdx_detach'
Assert-Contract 'RUNTIME_REUSE_PRESERVES_HID' $isolationOk `
    'reconnect must reuse the audited singleton without resetting an independent HID owner'

Write-Host 'RDX lifecycle contracts passed.'
