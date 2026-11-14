#Requires -Version 5.1
[CmdletBinding()]
param(
    [string]$InputArchive = '',
    [string]$OutputArchive = '',
    [string]$ToolchainBin = 'C:\JL\pi32\bin',
    [string]$EvidenceDirectory = ''
)
$ErrorActionPreference = 'Stop'
$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$InputArchive) { $InputArchive = Join-Path $RepoRoot 'SDK/cpu/br28/liba/btctrler.a' }
if (!$OutputArchive) { $OutputArchive = Join-Path $RepoRoot 'SDK/objs/btctrler_diag.a' }
$InputArchive = (Resolve-Path -LiteralPath $InputArchive).Path
$OutputArchive = [IO.Path]::GetFullPath($OutputArchive)
if ($InputArchive -eq $OutputArchive) { throw 'Never overwrite the original controller archive.' }
$ExpectedHash = '6ECBBD876EA08E6D260FBCD1C8C7B8CD214BFDE70EDF1145E3F7D02ECCFABC50'
if ((Get-FileHash -LiteralPath $InputArchive -Algorithm SHA256).Hash -ne $ExpectedHash) {
    throw 'Unsupported btctrler.a: re-review the controller ABI and every diagnostic site.'
}
$Clang = Join-Path $ToolchainBin 'clang.exe'
$Ar = Join-Path $ToolchainBin 'llvm-ar.exe'
function Run-Tool([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Tool failed ($LASTEXITCODE): $Tool" }
}
function Replace-One([string]$Text, [string]$Old, [string]$New) {
    if (([regex]::Matches($Text, [regex]::Escape($Old))).Count -ne 1) {
        throw "Diagnostic ABI anchor is not unique: $Old"
    }
    return $Text.Replace($Old, $New)
}
function Patch-Function([string]$Ir, [string]$Name, [scriptblock]$Patch) {
    $matches = [regex]::Matches($Ir, '(?ms)^define [^\n]*@' + [regex]::Escape($Name) + '\([^\n]*\{.*?^\}')
    if ($matches.Count -ne 1) { throw "Missing unique controller function: $Name" }
    $old = $matches[0].Value
    $new = & $Patch $old
    return Replace-One $Ir $old $new
}
function With-DebugLocation([string]$Anchor, [string]$Code) {
    # LTO requires a source location for calls inserted into a debug function.
    # Reuse the adjacent reviewed instruction's location, in the same scope.
    $location = [regex]::Match($Anchor, '!dbg !\d+').Value
    if (!$location) { throw "Missing debug scope on anchor: $Anchor" }
    return [regex]::Replace($Code, '(?m)^  call void @rdx_ble_diag_[^\r\n]*',
                            { param($m) $m.Value + ', ' + $location })
}
function Before([string]$Text, [string]$Anchor, [string]$Code) {
    $Code = With-DebugLocation $Anchor $Code
    return Replace-One $Text $Anchor ($Code.TrimEnd() + "`n" + $Anchor)
}
function After([string]$Text, [string]$Anchor, [string]$Code) {
    $Code = With-DebugLocation $Anchor $Code
    return Replace-One $Text $Anchor ($Anchor + "`n" + $Code.TrimEnd())
}
function Link-Call([string]$Id, [string]$Link, [int]$Event, [string]$Reason = '61', [string]$Aux = '0') {
    # Named SSA values do not renumber the vendor's numeric values/labels.
    return "  %diag_${Id}_ptr = bitcast %struct.le_link* $Link to i8*`n" +
           "  call void @rdx_ble_diag_link(i8* %diag_${Id}_ptr, i32 $Event, i32 $Reason, i32 $Aux)"
}
function Sup-Call([string]$Id, [string]$Link, [int]$Site, [string]$Timer = '-1', [string]$Value = '0') {
    # -1 reads the current field23; explicit IDs preserve register/remove results.
    return "  %diag_sup_${Id}_ptr = bitcast %struct.le_link* $Link to i8*`n" +
           "  call void @rdx_ble_diag_sup_link(i8* %diag_sup_${Id}_ptr, i32 $Site, i32 $Timer, i32 $Value)"
}
$LinkDecl = "`ndeclare void @rdx_ble_diag_link(i8*, i32, i32, i32)`n"
$RecordDecl = "`ndeclare void @rdx_ble_diag_record(i32, i16 zeroext, i32, i32)`n"
$SupLinkDecl = "`ndeclare void @rdx_ble_diag_sup_link(i8*, i32, i32, i32)`n"

function Patch-Member([string]$Member, [string]$Ir) {
    switch ($Member) {
        'bb_le_link.c.o' {
            $Ir = Patch-Function $Ir 'le_hw_upload_data' {
                param($f)
                Before $f '  store i8 %21, i8* %18, align 1, !dbg !3176' @'
  %diag_mic_privp = getelementptr inbounds %struct.ble4_hw, %struct.ble4_hw* %4, i32 0, i32 10
  %diag_mic_priv = load i8*, i8** %diag_mic_privp, align 4
  call void @rdx_ble_diag_link(i8* %diag_mic_priv, i32 2, i32 61, i32 4)
'@
            }
            return $Ir + $LinkDecl
        }
        'll_slave.c.o' {
            $Ir = Patch-Function $Ir '__conn_slave_rx_post_handler' {
                param($f)
                $f = Before $f '  %26 = tail call zeroext i8 @ll_send_msg_id(%struct.le_link* %3, i8 zeroext 38) #3, !dbg !964' @'
  %diag_rx_raw = lshr i8 %23, 3
  %diag_rx_error = zext i8 %diag_rx_raw to i32
  call void @rdx_ble_diag_link(i8* %0, i32 3, i32 61, i32 %diag_rx_error)
'@
                Before $f '  tail call void @llcp_sm_end(%struct.le_link* nonnull %3) #3, !dbg !992' (Link-Call 'state' '%3' 6 '61' '12')
            }
            return $Ir + $LinkDecl
        }
        'll_master.c.o' {
            $Ir = Patch-Function $Ir '__conn_master_rx_post_handler' {
                param($f)
                Before $f '  tail call void @llcp_sm_end(%struct.le_link* nonnull %3) #3, !dbg !957' (Link-Call 'state' '%3' 6 '61' '12')
            }
            return $Ir + $LinkDecl
        }
        'llcp_sm_common.c.o' {
            $Ir = Patch-Function $Ir 'llcp_common_msg_sm_run' {
                param($f)
                Before $f '  tail call void @ll_disconnect_process(%struct.le_link* %0, i8 zeroext 61) #7, !dbg !1900' (Link-Call 'mic' '%0' 4)
            }
            $Ir = Patch-Function $Ir 'll_recv_terminate_ind' {
                param($f)
                Before $f '  store i8 %6, i8* %7, align 1, !dbg !3900, !tbaa !3050' (
                    "  %diag_reason = zext i8 %6 to i32`n" + (Link-Call 'remote' '%0' 7 '%diag_reason'))
            }
            $Ir = Patch-Function $Ir 'll_termination' {
                param($f)
                Before $f '  %3 = zext i16 %0 to i32, !dbg !3285' @'
  %diag_reason = zext i8 %1 to i32
  call void @rdx_ble_diag_record(i32 8, i16 zeroext %0, i32 %diag_reason, i32 0)
'@
            }
            $Ir = Patch-Function $Ir 'llcp_common_sm_run' {
                param($f)
                Before $f '  tail call void (%struct.le_link*, i32, i8*, ...) @ll_send_control_data(%struct.le_link* nonnull %0, i32 2, i8* getelementptr inbounds ([2 x i8], [2 x i8]* @.str.17, i32 0, i32 0), i32 %839) #7, !dbg !3151' (Link-Call 'tx' '%0' 9 '%839')
            }
            return $Ir + $LinkDecl + $RecordDecl
        }
        'link_layer.c.o' {
            # Site ABI: 1 ALLOC, 2 REGISTER, 3 CONTEXT, 4 PERIOD, 5 REMOVE,
            # 6 TIMEOUT_ENTER, 7 TIMEOUT_REASON, 8 COMMIT, 9 FREE.
            $Ir = Patch-Function $Ir 'll_create_conn_link_post' {
                param($f)
                After $f '  store i16 %2, i16* %3, align 2, !dbg !2721, !tbaa !2253' (Sup-Call 'alloc' '%0' 1)
            }
            $Ir = Patch-Function $Ir 'll_supervision_timeout_start' {
                param($f)
                $f = After $f '  store i32 %3, i32* %4, align 4, !dbg !3682, !tbaa !1623' (Sup-Call 'register' '%0' 2 '%3' '%1')
                After $f '  tail call void @sys_timer_set_context(i32 %19, i8* %20) #11, !dbg !3713' (Sup-Call 'context' '%0' 3 '%19' '%1')
            }
            $Ir = Patch-Function $Ir 'll_supervision_timeout_changeperiod' {
                param($f)
                After $f '  tail call void @sys_timer_change_period(i32 %19, i32 %1) #11, !dbg !3789' (Sup-Call 'period' '%0' 4 '%19' '%1')
            }
            $Ir = Patch-Function $Ir 'll_supervision_timeout_stop' {
                param($f)
                # The field is already cleared; retain the ID actually removed.
                After $f '  store i32 0, i32* %2, align 4, !dbg !3119, !tbaa !1623' (Sup-Call 'remove' '%0' 5 '%3')
            }
            $Ir = Patch-Function $Ir 'll_conn_supervision_timer_handler' {
                param($f)
                # Freeze before BLE_CLOSE_HW, vendor logging and timer deletion.
                $f = Before $f '  %5 = load %struct.ble_operation*, %struct.ble_operation** @__ble_ops, align 4, !dbg !3721, !tbaa !1881' (Sup-Call 'timeout_enter' '%2' 6)
                # Use the vendor's selected reason and decision-time wait_ack,
                # not a fresh read. C associates this with the frozen entry ID.
                Before $f '  tail call void @ll_disconnect_process(%struct.le_link* nonnull %2, i8 zeroext %33) #12, !dbg !3755' (@'
  %diag_sup_reason = zext i8 %33 to i32
  %diag_sup_wait_nonzero = icmp ne i64 %24, 0
  %diag_sup_wait = zext i1 %diag_sup_wait_nonzero to i32
  %diag_sup_wait_shifted = shl i32 %diag_sup_wait, 8
  %diag_sup_reason_value = or i32 %diag_sup_reason, %diag_sup_wait_shifted
'@ + "`n" + (Sup-Call 'timeout_reason' '%2' 7 '-1' '%diag_sup_reason_value'))
            }
            $Ir = Patch-Function $Ir 'll_disconnect_process' {
                param($f)
                Before $f '  store i8 8, i8* %3, align 4, !dbg !3660, !tbaa !1566' (
                    "  %diag_reason = zext i8 %1 to i32`n" +
                    (Sup-Call 'commit' '%0' 8 '-1' '%diag_reason') + "`n" +
                    (Link-Call 'commit' '%0' 10 '%diag_reason'))
            }
            $Ir = Patch-Function $Ir 'll_close_conn_link' {
                param($f)
                Before $f '  tail call void @free_link_handle(i16 zeroext %151) #12, !dbg !3025' (Sup-Call 'free' '%0' 9)
            }
            $Ir = Patch-Function $Ir 'conn_rx_probe_handler' {
                param($f)
                # The merge must remain PHI-first. %38 is the reset decision's
                # invalid value; %48 may contain a different, post-reset read.
                Before $f '  %49 = and i8 %48, -8, !dbg !1631' @'
  %diag_rx_reset_id = phi i32 [ %43, %45 ], [ 0, %40 ], [ 0, %35 ]
  %diag_rx_event = zext i16 %5 to i32
  %diag_rx_invalid = zext i8 %38 to i32
  %diag_rx_invalid_shifted = shl i32 %diag_rx_invalid, 16
  %diag_rx_sample = or i32 %diag_rx_event, %diag_rx_invalid_shifted
  %diag_rx_link_ptr = bitcast %struct.le_link* %3 to i8*
  call void @rdx_ble_diag_sup_activity_link(i8* %diag_rx_link_ptr, i32 1, i32 %diag_rx_sample, i32 %diag_rx_reset_id)
'@
            }
            $Ir = Patch-Function $Ir 'conn_evt_probe_handler' {
                param($f)
                # %0 is the original i8* link. %42 belongs to another branch.
                After $f '  store volatile i16 %7, i16* %13, align 4, !dbg !1727, !tbaa !1548' @'
  %diag_event_sample = zext i16 %7 to i32
  call void @rdx_ble_diag_sup_activity_link(i8* %0, i32 2, i32 %diag_event_sample, i32 0)
'@
            }
            $Ir = Patch-Function $Ir 'conn_rx_acl_u_post_handler' {
                param($f)
                $f = Before $f '  %21 = tail call zeroext i8 @ll_send_msg_id(%struct.le_link* %0, i8 zeroext 38) #11, !dbg !4083' (Link-Call 'rx' '%0' 3 '61' '4')
                $f = Before $f '  tail call void @llcp_sm_end(%struct.le_link* nonnull %0) #11, !dbg !4102' (Link-Call 'slave' '%0' 5)
                Before $f '  tail call void @llcp_sm_end(%struct.le_link* nonnull %0) #11, !dbg !4112' (Link-Call 'master' '%0' 5)
            }
            # These typed GEPs belong to the audited library, not an app-side
            # guessed struct overlay. No pointers or key material leave the hook.
            return $Ir + $RecordDecl + @'

@rdx_ble_diag_enabled = external constant i32

define i32 @rdx_ble_diag_patch_revision() {
  ret i32 2
}

define void @rdx_ble_diag_link(i8* %ptr, i32 %event, i32 %reason, i32 %aux) {
entry:
  %enabled = load i32, i32* @rdx_ble_diag_enabled, align 4
  %on = icmp ne i32 %enabled, 0
  %valid = icmp ne i8* %ptr, null
  %run = and i1 %on, %valid
  br i1 %run, label %capture, label %exit
capture:
  %link = bitcast i8* %ptr to %struct.le_link*
  %hp = getelementptr inbounds %struct.le_link, %struct.le_link* %link, i32 0, i32 13
  %handle = load i16, i16* %hp, align 2
  %ep = getelementptr inbounds %struct.le_link, %struct.le_link* %link, i32 0, i32 34
  %enc = load i8, i8* %ep, align 1
  %pp = getelementptr inbounds %struct.le_link, %struct.le_link* %link, i32 0, i32 31, i32 2
  %proc = load i8, i8* %pp, align 1
  %rp = getelementptr inbounds %struct.le_link, %struct.le_link* %link, i32 0, i32 6
  %role = load i8, i8* %rp, align 1
  %e = zext i8 %enc to i32
  %p = zext i8 %proc to i32
  %r = zext i8 %role to i32
  %es = shl i32 %e, 8
  %ps = shl i32 %p, 16
  %rs = shl i32 %r, 24
  %a = and i32 %aux, 255
  %d1 = or i32 %a, %es
  %d2 = or i32 %d1, %ps
  %detail = or i32 %d2, %rs
  call void @rdx_ble_diag_record(i32 %event, i16 zeroext %handle, i32 %reason, i32 %detail)
  br label %exit
exit:
  ret void
}

declare void @rdx_ble_diag_sup_state(i32, i32, i32, i32, i32, i32, i32, i32)
declare void @rdx_ble_diag_sup_activity(i32, i32, i32, i32)

define void @rdx_ble_diag_sup_link(i8* %ptr, i32 %site, i32 %timer, i32 %value) {
entry:
  %diag_enabled = load i32, i32* @rdx_ble_diag_enabled, align 4
  %diag_on = icmp ne i32 %diag_enabled, 0
  %diag_valid = icmp ne i8* %ptr, null
  %diag_run = and i1 %diag_on, %diag_valid
  br i1 %diag_run, label %capture, label %exit
capture:
  %diag_link = bitcast i8* %ptr to %struct.le_link*
  %diag_read_timer = icmp eq i32 %timer, -1
  br i1 %diag_read_timer, label %current_timer, label %snapshot
current_timer:
  %diag_tp = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 23
  %diag_current_timer = load i32, i32* %diag_tp, align 4
  br label %snapshot
snapshot:
  %diag_timer_id = phi i32 [ %timer, %capture ], [ %diag_current_timer, %current_timer ]
  %diag_hp = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 13
  %diag_handle16 = load i16, i16* %diag_hp, align 2
  %diag_handle = zext i16 %diag_handle16 to i32
  %diag_sp = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 5
  %diag_state8 = load i8, i8* %diag_sp, align 4
  %diag_state = zext i8 %diag_state8 to i32
  %diag_timeoutp = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 3, i32 0, i32 6
  %diag_timeout16 = load i16, i16* %diag_timeoutp, align 2
  %diag_timeout = zext i16 %diag_timeout16 to i32
  %diag_eventp = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 9
  %diag_event16 = load volatile i16, i16* %diag_eventp, align 4
  %diag_event = zext i16 %diag_event16 to i32
  %diag_pp = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 31, i32 2
  %diag_proc8 = load i8, i8* %diag_pp, align 1
  %diag_proc = zext i8 %diag_proc8 to i32
  %diag_wp = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 31, i32 6
  %diag_wait_ack = load i64, i64* %diag_wp, align 4
  %diag_wait_nonzero = icmp ne i64 %diag_wait_ack, 0
  %diag_wait = zext i1 %diag_wait_nonzero to i32
  %diag_wait_shifted = shl i32 %diag_wait, 8
  %diag_rp = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 6
  %diag_role8 = load i8, i8* %diag_rp, align 1
  %diag_role = zext i8 %diag_role8 to i32
  %diag_role_shifted = shl i32 %diag_role, 16
  %diag_ep = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 34
  %diag_enc8 = load i8, i8* %diag_ep, align 1
  %diag_enc = zext i8 %diag_enc8 to i32
  %diag_enc_shifted = shl i32 %diag_enc, 24
  %diag_flags1 = or i32 %diag_proc, %diag_wait_shifted
  %diag_flags2 = or i32 %diag_flags1, %diag_role_shifted
  %diag_flags = or i32 %diag_flags2, %diag_enc_shifted
  call void @rdx_ble_diag_sup_state(i32 %site, i32 %diag_handle, i32 %diag_timer_id, i32 %value, i32 %diag_state, i32 %diag_timeout, i32 %diag_event, i32 %diag_flags)
  br label %exit
exit:
  ret void
}

define void @rdx_ble_diag_sup_activity_link(i8* %ptr, i32 %kind, i32 %sample, i32 %reset_id) {
entry:
  %diag_enabled = load i32, i32* @rdx_ble_diag_enabled, align 4
  %diag_on = icmp ne i32 %diag_enabled, 0
  %diag_valid = icmp ne i8* %ptr, null
  %diag_run = and i1 %diag_on, %diag_valid
  br i1 %diag_run, label %capture, label %exit
capture:
  %diag_link = bitcast i8* %ptr to %struct.le_link*
  %diag_hp = getelementptr inbounds %struct.le_link, %struct.le_link* %diag_link, i32 0, i32 13
  %diag_handle16 = load i16, i16* %diag_hp, align 2
  %diag_handle = zext i16 %diag_handle16 to i32
  call void @rdx_ble_diag_sup_activity(i32 %kind, i32 %diag_handle, i32 %sample, i32 %reset_id)
  br label %exit
exit:
  ret void
}
'@
        }
        'llcp_sm_conn_update.c.o' {
            $Ir = Patch-Function $Ir 'll_connection_upadte_event_callback' {
                param($f)
                # Intent at instant-1, not proof that a timer exists/was modified.
                Before $f '  tail call void @ll_supervision_timeout_changeperiod(%struct.le_link* %0, i32 40000) #6, !dbg !3596' (Sup-Call 'update_temp' '%0' 10 '-1' '40000')
            }
            $Ir = Patch-Function $Ir 'll_connection_upadte_event_post_callback' {
                param($f)
                Before $f '  tail call void @ll_supervision_timeout_changeperiod(%struct.le_link* %0, i32 %17) #6, !dbg !3648' (Sup-Call 'update_restore' '%0' 11 '-1' '%17')
            }
            return $Ir + $SupLinkDecl
        }
        'llcp_sm_conn_subrate.c.o' {
            $Ir = Patch-Function $Ir 'll_connection_subrate_event_callback' {
                param($f)
                Before $f '  tail call void @ll_supervision_timeout_changeperiod(%struct.le_link* %0, i32 %11) #6, !dbg !2396' (Sup-Call 'subrate' '%0' 12 '-1' '%11')
            }
            return $Ir + $SupLinkDecl
        }
        'sys_timer.c.o' {
            $Ir = Patch-Function $Ir 'sys_timer_change_period' {
                param($f)
                # Observe the actual u16 OS ID and raw result. C filters IDs;
                # unrelated response/advertising/CIS timers are not tracked.
                After $f '  %4 = tail call i32 @sys_timer_modify(i16 zeroext %3, i32 %1) #3, !dbg !66' @'
  %diag_timer_id = zext i16 %3 to i32
  call void @rdx_ble_diag_sup_modify_result(i32 %diag_timer_id, i32 %1, i32 %4)
'@
            }
            return $Ir + "`ndeclare void @rdx_ble_diag_sup_modify_result(i32, i32, i32)`n"
        }
        'll_events.c.o' {
            $Ir = Patch-Function $Ir 'le_connection_complete_event_emit' {
                param($f)
                Before $f '  %2 = load i64, i64* @config_btctler_le_features, align 4, !dbg !1177, !tbaa !1179' (Link-Call 'begin' '%0' 1 '0')
            }
            $Ir = Patch-Function $Ir 'hci_event_disconnection_complete' {
                param($f)
                # Only events that will actually be emitted, not reason=0xff.
                Before $f '  %19 = tail call i32 (i32, i8*, ...) @hci_send_event(i32 5, i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str.41, i32 0, i32 0), i32 0, i32 %18, i32 %3) #4, !dbg !1473' @'
  %diag_handle = zext i8 %0 to i16
  call void @rdx_ble_diag_record(i32 11, i16 zeroext %diag_handle, i32 %3, i32 0)
'@
            }
            return $Ir + $LinkDecl + $RecordDecl
        }
        'hci_ll.c.o' {
            $Ir = Patch-Function $Ir 'll_hci_disconnect' {
                param($f)
                Before $f '  %5 = tail call i32 (i32, i32, ...) @btctrler_hci_cmd_to_task(i32 4194310, i32 3, i32 1030, i32 %3, i32 %4) #9, !dbg !1277' @'
  call void @rdx_ble_diag_record(i32 12, i16 zeroext %0, i32 %4, i32 0)
'@
            }
            return $Ir + $RecordDecl
        }
        default { throw "Unreviewed member: $Member" }
    }
}

$TempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$TempDir = [IO.Path]::GetFullPath((Join-Path $TempRoot ('ble-diag-' + [guid]::NewGuid().ToString('N'))))
if ([IO.Path]::GetDirectoryName($TempDir) -ne $TempRoot) { throw 'Unexpected temp directory.' }
New-Item -ItemType Directory -Path $TempDir | Out-Null
$Members = @('bb_le_link.c.o', 'll_slave.c.o', 'll_master.c.o', 'llcp_sm_common.c.o',
             'link_layer.c.o', 'll_events.c.o', 'hci_ll.c.o',
             'llcp_sm_conn_update.c.o', 'llcp_sm_conn_subrate.c.o', 'sys_timer.c.o')
try {
    Push-Location $TempDir
    try {
        Run-Tool $Ar (@('x', $InputArchive) + $Members)
        $counts = @{}
        foreach ($member in $Members) {
            $original = Join-Path $TempDir ($member + '.original.ll')
            $patched = Join-Path $TempDir ($member + '.patched.ll')
            Run-Tool $Clang @('-S', '-emit-llvm', '-target', 'pi32v2', '-mcpu=r3', '-x', 'ir', $member, '-o', $original)
            $ir = [IO.File]::ReadAllText($original).Replace("`r`n", "`n")
            $ir = Patch-Member $member $ir
            [IO.File]::WriteAllText($patched, $ir, (New-Object Text.UTF8Encoding($false)))
            Run-Tool $Clang @('-c', '-emit-llvm', '-target', 'pi32v2', '-mcpu=r3', '-x', 'ir', $patched, '-o', $member)
            # Count every diagnostic call, including helper-to-C calls appended
            # to link_layer. Declarations/definitions are not call sites.
            $counts[$member] = ([regex]::Matches($ir, '(?m)^  call void @rdx_ble_diag_[A-Za-z0-9_]+\(')).Count
        }
        $candidate = Join-Path $TempDir 'btctrler_diag.a'
        Copy-Item -LiteralPath $InputArchive -Destination $candidate
        Run-Tool $Ar (@('r', $candidate) + $Members)
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($OutputArchive)) | Out-Null
        Copy-Item -LiteralPath $candidate -Destination $OutputArchive -Force
        $manifest = [ordered]@{
            revision = 2
            input_sha256 = $ExpectedHash.ToLowerInvariant()
            output_sha256 = (Get-FileHash -LiteralPath $OutputArchive -Algorithm SHA256).Hash.ToLowerInvariant()
            hooks_count_scope = 'all rdx_ble_diag_* calls, including helper-to-C calls'
            hooks_per_member = $counts
        }
        $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath ($OutputArchive + '.json') -Encoding UTF8
        if ($EvidenceDirectory) {
            $EvidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
            New-Item -ItemType Directory -Force -Path $EvidenceDirectory | Out-Null
            foreach ($member in $Members) {
                Copy-Item -LiteralPath (Join-Path $TempDir ($member + '.original.ll')) -Destination $EvidenceDirectory
                Copy-Item -LiteralPath (Join-Path $TempDir ($member + '.patched.ll')) -Destination $EvidenceDirectory
            }
        }
        Write-Host "BLE diagnostic controller ready: $OutputArchive (revision 2)"
    } finally { Pop-Location }
} finally {
    # One shell, verified absolute path under the allocated temp root.
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($TempDir)) -ne $TempRoot) {
        throw 'Refusing to remove an unexpected temp directory.'
    }
    Remove-Item -LiteralPath $TempDir -Recurse -Force
}
