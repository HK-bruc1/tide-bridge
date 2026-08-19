#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$Config = Read-RepoFile $RepoRoot 'SDK\apps\earphone\include\t2620_project_config.h'
$SdkConfigH = Read-RepoFile $RepoRoot 'SDK\apps\earphone\board\br28\sdk_config.h'
$SdkConfigC = Read-RepoFile $RepoRoot 'SDK\apps\earphone\board\br28\sdk_config.c'
$BoardConfig = Read-RepoFile $RepoRoot 'SDK\apps\earphone\board\br28\board_ac701n_demo_cfg.h'
$AppConfig = Read-RepoFile $RepoRoot 'SDK\apps\earphone\include\app_config.h'
$IoKey = Read-RepoFile $RepoRoot 'SDK\apps\earphone\board\iokey_config.c'
$Pc = Read-RepoFile $RepoRoot 'SDK\apps\earphone\mode\pc\pc.c'
$UsbTask = Read-RepoFile $RepoRoot 'SDK\apps\common\device\usb\usb_task.c'
$UsbCommon = Read-RepoFile $RepoRoot 'SDK\apps\common\device\usb\usb_common_def.h'
$DevManager = Read-RepoFile $RepoRoot 'SDK\apps\common\dev_manager\dev_manager.c'
$RdxApp = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_app.c'
$RdxKey = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_key.c'
$RdxKeyH = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_key.h'
$RdxRecord = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_record.c'
$RdxServer = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_ble_server.c'
$RdxLibraryPatch = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\patch_librdxApp.ps1'
$Makefile = Read-RepoFile $RepoRoot 'SDK\Makefile'
$PatchedRdxArchivePath = Join-Path $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\librdxApp_patched.a'
$PatchedRdxArchiveHash = if (Test-Path -LiteralPath $PatchedRdxArchivePath) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $PatchedRdxArchivePath).Hash
} else {
    ''
}
$AppMsg = Read-RepoFile $RepoRoot 'SDK\apps\earphone\include\app_msg.h'
$Dip = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_dip_switch.c'
$AppDefault = Read-RepoFile $RepoRoot 'SDK\apps\earphone\mode\common\app_default_msg_handler.c'
$SdkUsedList = Read-RepoFile $RepoRoot 'SDK\apps\earphone\sdk_used_list.c'
$EffectDev2 = Read-RepoFile $RepoRoot 'SDK\audio\framework\nodes\effect_dev2_node.c'

$overlayOk = $Config -match '#define\s+TCFG_DIP_SWITCH_POWER_ENABLE\s+1' -and
             $Config -match '#define\s+TCFG_DIP_SWITCH_POWER_IO\s+IO_PORTB_01' -and
             $AppConfig -match '#include\s+"sdk_config\.h"\s*\r?\n#include\s+"t2620_project_config\.h"' -and
             ($SdkConfigH + $SdkConfigC + $IoKey) -notmatch 'TCFG_DIP_SWITCH_POWER'
Assert-Contract 'PRODUCT_CONFIG_OVERLAY' $overlayOk `
    'DIP power ownership must stay in the project overlay and out of generated board files'

$configOwnershipOk = $true
foreach ($externallyOwnedMacro in @(
    'TCFG_ADKEY_ENABLE',
    'TCFG_LP_TOUCH_KEY_ENABLE',
    'TCFG_CHARGE_POWERON_ENABLE',
    'TCFG_USB_SLAVE_MSD_ENABLE',
    'TCFG_USB_SLAVE_CDC_ENABLE',
    'TCFG_USB_CUSTOM_HID_ENABLE',
    'TCFG_USB_SLAVE_MTP_ENABLE',
    'TCFG_USB_SLAVE_MIDI_ENABLE',
    'TCFG_USB_SLAVE_PRINTER_ENABLE',
    'TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE',
    'RDX_HOGP_KEY_ACTION_TEST_ENABLE'
)) {
    $configOwnershipOk = $configOwnershipOk -and
        $Config -notmatch "(?m)^\s*#\s*(?:define|undef)\s+$externallyOwnedMacro\b"
}
Assert-Contract 'PROJECT_CONFIG_DOES_NOT_REPEAT_EXISTING_DEFAULTS' $configOwnershipOk `
    'tool-owned and subsystem-default settings must not be repeated in the T2620 project config'

$localPlaybackDependenciesOk = $Config -match '(?m)^\s*#define\s+TCFG_RDX_LOCAL_PLAYBACK_ENABLE\s+[01]\s*$' -and
                               $Config -match '(?m)^\s*#define\s+TCFG_DEC_STENC_OPUS_ENABLE\s+TCFG_RDX_LOCAL_PLAYBACK_ENABLE\s*$' -and
                               $Config -match '(?m)^\s*#define\s+TCFG_DEC_OGG_OPUS_ENABLE\s+TCFG_RDX_LOCAL_PLAYBACK_ENABLE\s*$' -and
                               $Config -match '(?s)#if\s+TCFG_RDX_LOCAL_PLAYBACK_ENABLE\s*&&\s*\\\s*\(!TCFG_DEC_STENC_OPUS_ENABLE\s*\|\|\s*!TCFG_DEC_OGG_OPUS_ENABLE\).*?#error\s+"RDX local playback requires stereo and raw/CBR Opus decoding"'
Assert-Contract 'RDX_LOCAL_PLAYBACK_DEPENDENCIES' $localPlaybackDependenciesOk `
    'the local playback switch must own both stereo and raw/CBR Opus decoder dependencies'

$holdRecordOk = $AppMsg -match '(?s)APP_MSG_REQUEST_POWEROFF,.*?APP_MSG_RECORD_HOLD_START,.*?APP_MSG_RECORD_HOLD_STOP,' -and
                $RdxApp -match '(?s)case APP_MSG_RECORD_HOLD_START:.*?hold_record_pressed = 1.*?case APP_MSG_RECORD_HOLD_STOP:.*?hold_record_pressed = 0' -and
                $RdxApp -match '(?s)rdx_app_hold_record_pump.*?rdx_app_device_record_set\(\s*hold_record_scene,\s*RECORD_STATE_STOP,\s*1\).*?rdx_app_device_record_set\(\s*hold_record_scene,\s*RECORD_STATE_START,\s*1\)' -and
                $RdxApp -match '(?s)rdx_app_hold_record_wait_until_ready.*?REC_PROCESS_STATE_BUSY.*?rdx_record_process_is_busy_check.*?rdx_app_hold_record_retry_schedule' -and
                $RdxApp -match '(?s)static int rdx_app_device_record_set.*?rdx_ble_server_get_conn_handle.*?void rdx_app_device_record_handle.*?rdx_app_device_record_set\(scene, run, 0\)'
Assert-Contract 'RDX_HOLD_RECORD_TRIGGER_CAPABILITY' $holdRecordOk `
    'hold start/stop messages must remain reusable and share the existing online/offline recording implementation'

$holdRecordTableOk = $RdxKeyH -match 'extern\s+u8\s+key_table_record_hold\s*\[KEY_ACTION_MAX\]' -and
                     $RdxKey -match '(?s)u8\s+key_table_record_hold\s*\[KEY_ACTION_MAX\]\s*=\s*\{\s*APP_MSG_NULL,.*?APP_MSG_RECORD_HOLD_START,.*?APP_MSG_NULL,.*?APP_MSG_RECORD_HOLD_STOP,'
Assert-Contract 'RDX_HOLD_RECORD_ACTION_TABLE' $holdRecordTableOk `
    'press-to-record must use a reusable action table with LONG start and UP stop mappings'

$ioKeyRouting = Get-SourceSlice $RdxApp `
    'static u8 rdx_app_rdx_key_route_ready(void)' `
    '// ---- KEY_POWER'
$ioKeyRoutingOk = (Test-TokensInOrder $ioKeyRouting @(
                      'if (num_idx == 4)',
                      'rdx_app_key5_remap(value, index, scene)',
                      'rdx_hogp_keyboard_is_ready()',
                      'rdx_ble_server_has_active_link()',
                      'rdx_key_get_io_num_table(num_idx, scene)'
                  )) -and
                  $ioKeyRouting -match '(?s)rdx_app_rdx_key_route_ready.*?rdx_ble_session_rdx_token_capture\(&token,\s*1\)' -and
                  $ioKeyRouting -match '(?s)rdx_app_key5_remap.*?KEY_ACTION_UP\s*&&\s*key5_online_hold_routed.*?key_table_record_hold\[index\].*?rdx_app_rdx_key_route_ready\(\).*?KEY_ACTION_LONG.*?key_table_record_hold\[index\]' -and
                  $ioKeyRouting -match '(?s)rdx_ble_server_has_active_link\(\).*?APP_MSG_NULL.*?rdx_key_get_io_num_table\(4,\s*scene\)'
Assert-Contract 'RDX_IO_KEY_BLE_CAPABILITY_ROUTING' $ioKeyRoutingOk `
    'KEY1-4 must prefer HID, KEY5 must prefer RDX hold recording, and local tables require zero BLE links'

$defaultMeetingSceneOk = $RdxRecord -match '(?s)void rdx_record_set_default\(void\).*?record_status\.scene\s*=\s*RECORD_SCENE_CHAT' -and
                         $RdxApp -notmatch 'rp->scene\s*=\s*RECORD_SCENE_CALL'
Assert-Contract 'RDX_DEFAULT_RECORD_SCENE_IS_MEETING' $defaultMeetingSceneOk `
    'BLE state synchronization must preserve the default meeting scene instead of forcing call mode'

$holdRecordStorageOk = $RdxApp -match '(?s)request->stream_only\s*&&.*?request->status\.run\s*==\s*RECORD_STATE_START.*?rdx_record_stream_only_start_arm\(&request->token\)' -and
                       $RdxApp -match 'rdx_app_device_record_set\(scene, run, 0\)' -and
                       $RdxRecord -match '(?s)rdx_record_stream_only_start_consume\(token\);.*?rdx_record_online_session_bind\(token\);' -and
                       $RdxRecord -match '(?s)if\(rdx_record_stream_only_session_is_active\(\)\).*?rdx_uxfile_operate_file_init\(\);.*?else\s*\{.*?rdx_uxfile_dat_1_gen\(rp->scene\);' -and
                       $RdxRecord -match '(?s)//local save\..*?if\(!rdx_record_stream_only_session_is_active\(\)\).*?rdx_uxfile_raw_write' -and
                       $RdxRecord -match '(?s)if\(!rdx_record_stream_only_session_is_active\(\)\).*?rdx_uxfile_dat_1_save_gen\(\);' -and
                       $RdxServer -match '(?s)if\(!rdx_record_stream_only_session_is_active\(\)\).*?rp->orig_mode\s*=\s*RECORD_MODE_OFFLINE;'
Assert-Contract 'RDX_HOLD_RECORDING_IS_STREAM_ONLY' $holdRecordStorageOk `
    'only hold-triggered online recording may report empty identity and skip local persistence'

$uxfileStartupRecoveryOk = $Makefile -match '(?m)^\s*apps/common/third_party_profile/rdx_protocol/librdxApp_patched\.a\s*\\\s*$' -and
                           $Makefile -notmatch '(?m)^\s*apps/common/third_party_profile/rdx_protocol/librdxApp\.a\s*\\\s*$' -and
                           $PatchedRdxArchiveHash -eq '271319DF37566E091C99E8A92563D91A04D4533E25159B03AA82AD23078FE570' -and
                           $RdxLibraryPatch -match '4289EC0F6D923EC9337A5DBE57F8D720BCC4946601B7D8393F9E2878B16F5C7D' -and
                           $RdxLibraryPatch -match "factory-new empty index fast path" -and
                           $RdxLibraryPatch -match "br i1 %25, label %221, label %219" -and
                           $RdxLibraryPatch -match "%222 = phi i32 \[ %218, %217 \], \[ %220, %219 \], \[ 0, %15 \]" -and
                           $RdxLibraryPatch -match '(?s)rdx_patch_scan_succeeded.*?syscfg_write\(i16 zeroext 159.*?rdx_patch_vm_written' -and
                           $RdxLibraryPatch -match '(?s)define zeroext i8 @rdx_uxfile_is_scan_active.*?@g_sync_state.*?@g_dat_need_upgrade_rebuild.*?rdx_patch_scan_active_u8' -and
                           $RdxRecord -match '(?s)static u8 rdx_record_uxfile_is_busy.*?rdx_uxfile_sync_is_in_progress.*?rdx_uxfile_is_scan_active.*?rdx_uxfile_is_formatting' -and
                           $RdxRecord -match '(?s)r_info->cmd == \(RECORD_STATE_START \+ 0x30\).*?uxfile_busy.*?g_record_cmd_delay_timer = sys_timeout_add' -and
                           $RdxRecord -match '(?s)r_info->cmd == \(RECORD_STATE_STOP \+ 0x30\).*?g_record_cmd_delay_timer.*?pending start cancelled by stop' -and
                           $RdxRecord -match '(?s)#if !TCFG_SD_ALWAY_ONLINE_ENABLE\s*int err = dev_manager_add\("sd0"\);.*?#endif' -and
                           $RdxApp -match '(?s)run == RECORD_STATE_START && !stream_only.*?rdx_uxfile_sync_is_in_progress.*?local start rejected: UXFILE is busy'
Assert-Contract 'RDX_UXFILE_STARTUP_RECOVERY' $uxfileStartupRecoveryOk `
    'the patched library must commit a missing DAT as an empty index without boot fscan, persist its marker, and serialize recording against recovery'

$recordingEncoderPathOk = $Config -notmatch 'TCFG_STENC_OPUS_ENABLE' -and
                          $SdkUsedList -notmatch 'TCFG_STENC_OPUS_ENABLE|opus_stenc_plug' -and
                          $EffectDev2 -match 'get_opus_stenc_ops\s*\(\s*\)'
Assert-Contract 'RDX_RECORDING_USES_EFFECT_DEV2_ENCODER' $recordingEncoderPathOk `
    'RDX recording must use the effect_dev2 encoder path without the legacy opus_stenc_plug gate'

$usbProfileOk = $Config -match '(?m)^\s*#define\s+TCFG_T2620_PC_STORAGE_ENABLE\s+1\s*$' -and
                $Config -match '(?s)#if\s+TCFG_T2620_PC_STORAGE_ENABLE.*?#if\s+!TCFG_SD0_ENABLE.*?#if\s+!TCFG_USB_SLAVE_MSD_ENABLE.*?#define\s+TCFG_APP_PC_EN\s+1' -and
                $SdkConfigH -match '(?m)^\s*#define\s+TCFG_CHARGE_POWERON_ENABLE\s+0\b' -and
                $SdkConfigH -match '(?m)^\s*#define\s+TCFG_USB_SLAVE_MSD_ENABLE\s+1\b' -and
                $Config -match '(?s)#if\s+TCFG_T2620_PC_STORAGE_ENABLE.*?#undef\s+TCFG_SD_ALWAY_ONLINE_ENABLE\s*#define\s+TCFG_SD_ALWAY_ONLINE_ENABLE\s+1' -and
                $BoardConfig -notmatch '(?m)^\s*#\s*(?:define|undef)\s+TCFG_SD_ALWAY_ONLINE_ENABLE\b'
foreach ($disabledClass in @(
    'TCFG_USB_SLAVE_HID_ENABLE',
    'TCFG_USB_SLAVE_AUDIO_SPK_ENABLE',
    'TCFG_USB_SLAVE_AUDIO_MIC_ENABLE'
)) {
    $usbProfileOk = $usbProfileOk -and
        $Config -match "(?m)^\s*#define\s+$disabledClass\s+0\s*$"
}
foreach ($defaultDisabledClass in @(
    'TCFG_USB_SLAVE_CDC_ENABLE',
    'TCFG_USB_CUSTOM_HID_ENABLE',
    'TCFG_USB_SLAVE_MTP_ENABLE',
    'TCFG_USB_SLAVE_MIDI_ENABLE',
    'TCFG_USB_SLAVE_PRINTER_ENABLE'
)) {
    $usbProfileOk = $usbProfileOk -and
        $UsbCommon -match "(?m)^\s*#define\s+$defaultDisabledClass\s+0\s*$"
}
Assert-Contract 'USB_MSC_PRODUCT_PROFILE' $usbProfileOk `
    'the product USB profile must expose only MSC over the soldered always-online storage'

$formatSafetyOk = $BoardConfig -match '(?m)^\s*#define\s+TCFG_SD0_FORMAT_ON_BOOT\s+ENABLE_THIS_MOUDLE\s*$' -and
                  $BoardConfig -match '(?m)^\s*#define\s+TCFG_SD0_FORCE_FORMAT_ON_BOOT\s+DISABLE_THIS_MOUDLE\s*$' -and
                  $DevManager -notmatch 'TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE' -and
                  $DevManager -match '(?s)if \(dev->fmnt\).*?skip format on boot.*?else.*?if \(vm_formatted\).*?recover by format.*?else.*?first boot or vm cleared.*?f_format\('
Assert-Contract 'STORAGE_FORMATS_ON_MOUNT_FAILURE' $formatSafetyOk `
    'SD0 mount failure must format for either first-time initialization or filesystem recovery'

$entryGatesOk = $Pc -match '(?s)static int pc_mode_try_enter.*?get_power_on_status\(\).*?rdx_pc_storage_is_busy\(\)' -and
                $Dip -match '(?s)rdx_dip_switch_request_pc_if_usb_online.*?usb_otg_online\(0\).*?APP_MODE_PC' -and
                $RdxApp -match '(?s)u8 rdx_pc_storage_is_busy.*?rdx_app_storage_activity_is_busy\s*\(\s*"PC-STORAGE"\s*,\s*0\s*\)' -and
                $RdxApp -match '(?s)static u8 rdx_app_storage_activity_is_busy.*?RECORD_STATE_STOP.*?rdx_record_process_is_busy_check.*?rdx_is_file_transfer_active.*?rdx_is_file_sync_busy'
Assert-Contract 'PC_ENTRY_REQUIRES_POWER_AND_IDLE_STORAGE' $entryGatesOk `
    'DIP power must permit PC mode and recording/file activity must block storage takeover'

$takeoverOk = $DevManager -match '(?s)static int __dev_manager_mount.*?if \(dev->mount_blocked\).*?return -1;' -and
              $DevManager -match '(?s)void dev_manager_list_check_mount.*?if \(dev->mount_blocked\).*?continue;' -and
              $Pc -match '(?s)static int pc_storage_prepare.*?rdx_pc_storage_is_busy\(\).*?dev_manager_takeover\("sd0"\).*?PC_STORAGE_HOST_OWNED' -and
              $Pc -match '(?s)static int pc_task_start.*?storage_state\s*!=\s*PC_STORAGE_HOST_OWNED.*?USBSTACK_START'
Assert-Contract 'STORAGE_TAKEOVER_IS_EXCLUSIVE' $takeoverOk `
    'firmware mounts must stay blocked while USB MSC owns sd0, and USB may start only after takeover'

$usbControlOk = $UsbTask -match '(?s)static OS_MUTEX msg_mutex.*?msg_wait_seq.*?usb_stack_message_complete.*?os_taskq_post_msg\(USB_TASK_NAME, 3, msg, arg, seq\)' -and
                $UsbTask -match 'os_sem_pend\(&msg_sem, 200\) == OS_TIMEOUT'
Assert-Contract 'USB_CONTROL_COMPLETION_IS_CORRELATED' $usbControlOk `
    'start and stop completion must be serialized, sequence-correlated, and bounded by a timeout'

$restoreOk = Test-TokensInOrder $Pc @(
    'usb_message_to_stack(USBSTACK_STOP, 0, 1);',
    'pc_storage_restore();'
)
$restoreOk = $restoreOk -and
             $Pc -match '(?s)usb_message_to_stack\(USBSTACK_STOP, 0, 1\).*?if \(err\).*?return err;.*?return pc_storage_restore\(\);' -and
             $Pc -match 'PC_STORAGE_RESTORE_FAILED' -and
             $DevManager -match '(?s)int dev_manager_restore.*?__dev_manager_restore_mount\(logo\).*?__dev_manager_set_mount_blocked\(logo, 0\)' -and
             $Pc -notmatch 'USBSTACK_PAUSE|dev_manager_del\("sd0"\)'
Assert-Contract 'USB_STOP_PRECEDES_STORAGE_RESTORE' $restoreOk `
    'USB must stop successfully before sd0 is mounted and unblocked for firmware reuse'

$poweroffOk = $Dip -match 'app_send_message\(APP_MSG_REQUEST_POWEROFF, POWEROFF_NORMAL\)' -and
              $Dip -notmatch 'sys_enter_soft_poweroff\(' -and
              $Pc -match '(?s)case APP_MSG_REQUEST_POWEROFF:.*?pc_task_stop\(\).*?app_default_msg_handler\(msg\)' -and
              $AppDefault -match '(?s)case APP_MSG_REQUEST_POWEROFF:.*?sys_enter_soft_poweroff'
Assert-Contract 'POWEROFF_USES_MODE_CLEANUP' $poweroffOk `
    'DIP power-off must request normal application shutdown so PC storage is released first'

Write-Host 'T2620 product contracts passed.'
