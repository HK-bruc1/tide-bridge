#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$Config = Read-RepoFile $RepoRoot 'SDK\apps\earphone\include\t2620_project_config.h'
$BoardJsonPath = Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'src') -Filter '*.json' |
    Where-Object {
        Select-String -LiteralPath $_.FullName -SimpleMatch 'TCFG_IO_CFG_AT_POWER_ON' -Quiet
    } |
    Select-Object -First 1
if (-not $BoardJsonPath) {
    throw 'Board JSON containing TCFG_IO_CFG_AT_POWER_ON was not found'
}
$BoardJson = Get-Content -Raw -Encoding UTF8 -LiteralPath $BoardJsonPath.FullName
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
$RdxPlayback = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_playback.c'
$RdxLedCtrl = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_led_ctrl.c'
$RdxLedCtrlH = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_led_ctrl.h'
$RdxLedCfg = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_led_cfg.h'
$RdxServer = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_ble_server.c'
$RdxServerH = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_ble_server.h'
$RdxAppConfig = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_app_config.h'
$PeripheralPower = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_peripheral_power.c'
$PeripheralPowerH = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_peripheral_power.h'
$AppMain = Read-RepoFile $RepoRoot 'SDK\apps\earphone\app_main.c'
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

$ampPowerOnJsonOk = $BoardJson -match '(?s)"defaultValue"\s*:\s*"IO_PORTE_05".*?"defaultValue"\s*:\s*"PORT_OUTPUT_LOW".*?"enableUuid"\s*:\s*"TCFG_IO_CFG_AT_POWER_ON"'
$ampGeneratedPowerOnOk = $SdkConfigC -match '(?s)g_io_cfg_at_poweron\s*\[\].*?\.gpio\s*=\s*IO_PORTE_05.*?\.mode\s*=\s*PORT_OUTPUT_LOW'
$ampGeneratedPowerOffOk = $SdkConfigC -match '(?s)g_io_cfg_at_poweroff\s*\[\].*?\.gpio\s*=\s*IO_PORTE_05.*?\.mode\s*=\s*PORT_OUTPUT_LOW'
Assert-Contract 'T2620_AMP_SAFE_BOOT_AND_POWEROFF' `
    ($ampPowerOnJsonOk -and $ampGeneratedPowerOnOk -and $ampGeneratedPowerOffOk) `
    'PE5 must be driven low by both the board source configuration and generated boot/power-off tables'

$ampConfigOk = $Config -match '(?m)^\s*#define\s+TCFG_T2620_AMP_POWER_ENABLE\s+1\s*$' -and
               $Config -match '(?m)^\s*#define\s+TCFG_T2620_AMP_ENABLE_IO\s+IO_PORTE_05\s*$' -and
               $Makefile -match '(?m)^\s*apps/common/third_party_profile/rdx_protocol/rdx_peripheral_power\.c\s*\\\s*$'
Assert-Contract 'T2620_AMP_PRODUCT_OWNERSHIP' $ampConfigOk `
    'the PE5 amplifier policy must be enabled by the T2620 overlay and implemented in the RDX product module'

$ampImplementation = Get-SourceSlice $PeripheralPower `
    'static u8 g_rdx_amp_enabled;' `
    '#else'
$ampDacLifecycleOk = $PeripheralPowerH -match 'void\s+rdx_peripheral_power_amp_set\s*\(u8\s+enable\)' -and
                     $ampImplementation -match '(?s)void\s+audio_dac_power_state\s*\(u8\s+state\).*?case\s+DAC_ANALOG_OPEN_FINISH\s*:.*?rdx_peripheral_power_amp_set\(1\).*?case\s+DAC_ANALOG_OPEN_PREPARE\s*:.*?case\s+DAC_ANALOG_CLOSE_PREPARE\s*:.*?case\s+DAC_ANALOG_CLOSE_FINISH\s*:.*?default\s*:.*?rdx_peripheral_power_amp_set\(0\)' -and
                     $ampImplementation -match 'gpio_set_mode\(IO_PORT_SPILT\(TCFG_T2620_AMP_ENABLE_IO\)' -and
                     $ampImplementation -notmatch 'sys_timeout_add|sys_timer_add|os_time_dly'
Assert-Contract 'T2620_AMP_FOLLOWS_DAC_LIFECYCLE' $ampDacLifecycleOk `
    'PE5 must rise only after DAC analog open finishes and fall synchronously before/after DAC analog close'

$ampPinConflictOk = $RdxAppConfig -match '(?m)^\s*#define\s+RDX_WIFI_ENABLE\s+\(0\)' -and
                    $PeripheralPower -match '(?s)#if\s+TCFG_T2620_AMP_POWER_ENABLE\s*&&\s*RDX_WIFI_ENABLE.*?#error\s+"T2620 PE5 amplifier enable conflicts with the legacy RDX WiFi SPI CS assignment"'
Assert-Contract 'T2620_AMP_PE5_CONFLICT_FAILS_CLOSED' $ampPinConflictOk `
    'the current no-WiFi product may own PE5, and enabling the legacy PE5 SPI CS path must fail at compile time'

$sharedVddPowerCutConfigOk = $Config -match '(?m)^\s*#define\s+TCFG_T2620_SHARED_VDD_ENABLE\s+1\s*$' -and
                             $Config -match '(?m)^\s*#define\s+TCFG_T2620_SHARED_VDD_IO\s+IO_PORTA_04\s*$' -and
                             $Config -match '(?m)^\s*#define\s+TCFG_T2620_SHARED_VDD_MODE\s+T2620_SHARED_VDD_MODE_POWER_CUT\s*$' -and
                             $Config -match '(?m)^\s*#define\s+TCFG_T2620_SHARED_VDD_POWER_STABLE_TICKS\s+1\s*$'
Assert-Contract 'T2620_SHARED_VDD_POWER_CUT_CONFIG' $sharedVddPowerCutConfigOk `
    'PA4 shared SD/RGB rail must use the POWER_CUT path with a nonzero power-stable window'

$sdPowerCallback = Get-SourceSlice $AppMain `
    'void sd_set_power_user(u8 en)' `
    'static struct app_mode *app_task_init()'
$sharedVddEarlyOwnershipOk = $sdPowerCallback -match 'rdx_peripheral_power_vdd_ensure_on\(RDX_SHARED_VDD_WAKE_SD_DRIVER\)' -and
                             $sdPowerCallback -notmatch 'gpio_set_mode|IO_PORTA_04' -and
                             $AppMain -match '(?s)static struct app_mode \*app_task_init\(\).*?rdx_peripheral_power_vdd_early_init\(\).*?app_var_init\(\).*?sdfile_init\(\)' -and
                             $PeripheralPower -match 'static void\s+rdx_peripheral_power_vdd_hw_set\s*\(u8\s+enable\)' -and
                             $PeripheralPower -match 'gpio_set_mode\(IO_PORT_SPILT\(TCFG_T2620_SHARED_VDD_IO\)' -and
                             $RdxApp -notmatch 'gpio_set_mode\(IO_PORT_SPILT\(IO_PORTA_04\)'
Assert-Contract 'T2620_SHARED_VDD_EARLY_SINGLE_WRITER' $sharedVddEarlyOwnershipOk `
    'PA4 must be high before SD initialization, and the SD callback must delegate to the product power manager'

$sharedVddQuiesceOk = $PeripheralPower -match 'T2620 PA4 shared VDD conflicts with the legacy RDX WiFi power assignment' -and
                      $PeripheralPower -match 'T2620 PA4 shared VDD conflicts with the DLOG SPI CS assignment' -and
                      $PeripheralPower -match 'T2620 PA4 shared VDD conflicts with the current RDEC0 assignment' -and
                      $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_idle_stop.*?dev_manager_takeover\("sd0"\).*?rdx_led_hardware_deinit\(\).*?FINAL_RECHECK' -and
                      $PeripheralPower -match '(?s)T2620_SHARED_VDD_MODE_POWER_CUT.*?rdx_peripheral_power_vdd_storage_io_safe\(\).*?rdx_peripheral_power_vdd_hw_set\(0\).*?RDX_SHARED_VDD_STATE_OFF' -and
                      $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_restore_idle_domain.*?dev_manager_restore\("sd0"\).*?rdx_led_hardware_resume\(\).*?RDX_SHARED_VDD_STATE_ON_READY' -and
                      $PeripheralPower -match 'stale_idle_ignored' -and
                      $RdxApp -match '(?s)int\s+rdx_led_hardware_deinit.*?rdx_led_ctrl_deinit\(\).*?led_pt0807_deinit' -and
                      $Pc -match '(?s)pc_storage_prepare.*?rdx_peripheral_power_vdd_usb_prepare\(\).*?dev_manager_takeover\("sd0"\).*?rdx_peripheral_power_vdd_usb_takeover_complete\(1\)' -and
                      $Pc -match '(?s)pc_storage_restore.*?dev_manager_restore\("sd0"\).*?rdx_peripheral_power_vdd_usb_restore_complete\(1\)'
Assert-Contract 'T2620_SHARED_VDD_QUIESCE_AND_RESTORE' $sharedVddQuiesceOk `
    'shared VDD must order SD/RGB quiesce, physical cut, stale-event rejection, recovery and USB ownership handoff'

$sharedVddTransitionSerializationOk = $PeripheralPower -match 'OS_MUTEX\s+transition_mutex' -and
                                      $PeripheralPower -match 'os_mutex_create\(&g_rdx_shared_vdd\.transition_mutex\)' -and
                                      $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_ensure_on.*?os_mutex_pend\(&g_rdx_shared_vdd\.transition_mutex.*?rdx_peripheral_power_vdd_restore_idle_domain.*?os_mutex_post\(&g_rdx_shared_vdd\.transition_mutex' -and
                                      $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_idle_stop\(u32\s+idle_epoch\).*?os_mutex_pend\(&g_rdx_shared_vdd\.transition_mutex.*?g_rdx_shared_vdd\.wake_epoch\s*!=\s*stop_epoch.*?os_mutex_post\(&g_rdx_shared_vdd\.transition_mutex' -and
                                      $PeripheralPower -match 'g_rdx_shared_vdd\.slow_adv_epoch\s*=\s*\(u32\)epoch'
Assert-Contract 'T2620_SHARED_VDD_TRANSITIONS_SERIALIZED' $sharedVddTransitionSerializationOk `
    'stop/restore transitions must be mutex-serialized and revalidate the exact slow-advertising epoch'

$sharedVddIrqSafeFinalRecheckOk = $PeripheralPower -match '(?s)A BLE topology change.*?raises wake_requested/wake_epoch.*?local_irq_disable\(\);\s*canceled\s*=\s*g_rdx_shared_vdd\.wake_requested\s*\|\|\s*g_rdx_shared_vdd\.wake_epoch\s*!=\s*stop_epoch.*?g_rdx_shared_vdd\.busy_mask.*?!g_rdx_shared_vdd\.slow_adv;\s*if\s*\(!canceled\)' -and
                                  $PeripheralPower -notmatch '(?s)local_irq_disable\(\);(?:(?!local_irq_enable\(\);).)*rdx_ble_server_get_connected_count\(\)'
Assert-Contract 'T2620_SHARED_VDD_FINAL_RECHECK_IS_IRQ_SAFE' $sharedVddIrqSafeFinalRecheckOk `
    'the IRQ-disabled final commit must use atomic wake facts and never call the mutex-backed JL BLE wrapper API'

$sharedVddDualAclOk = $RdxServerH -match 'u8\s+rdx_ble_server_get_connected_count\s*\(void\)' -and
                       $RdxServer -match '(?s)static u8\s+rdx_ble_server_phase0a_connected_count\s*\(void\).*?RDX_BLE_PHASE0A_WRAPPER_MAX.*?app_ble_get_hdl_con_handle' -and
                       $RdxServer -match '(?s)u8\s+rdx_ble_server_get_connected_count\s*\(void\).*?return\s+rdx_ble_server_phase0a_connected_count\(\)' -and
                       $RdxServer -match '(?s)rdx_ble_server_phase0a_link_connected.*?rdx_peripheral_power_vdd_ble_links_changed_notify\(\)' -and
                       $RdxServer -match '(?s)rdx_ble_server_phase0a_link_disconnected.*?rdx_ble_session_link_release.*?rdx_peripheral_power_vdd_ble_links_changed_notify\(\)' -and
                       $RdxServer -match '(?s)rdx_ble_server_adv_interval_change_timer_cb.*?rdx_led_ctrl_set_scene\(RDX_LED_SCENE_OFF\).*?rdx_peripheral_power_vdd_slow_adv_notify\(\)' -and
                       $RdxServer -match '(?s)rdx_ble_server_fast_adv_restart.*?rdx_peripheral_power_vdd_fast_adv_notify\(\).*?rdx_led_ctrl_set_scene\(RDX_LED_SCENE_BLE_ADV_START\)'
Assert-Contract 'T2620_SHARED_VDD_USES_DUAL_ACL_AND_ADV_EVENTS' $sharedVddDualAclOk `
    'shared VDD decisions must use both physical wrappers and receive slow/fast advertising lifecycle events'

$sharedVddBusySnapshotOk = $PeripheralPowerH -match 'RDX_SHARED_VDD_BUSY_BLE_LINK' -and
                           $PeripheralPowerH -match 'RDX_SHARED_VDD_BUSY_RECORD' -and
                           $PeripheralPowerH -match 'RDX_SHARED_VDD_BUSY_PLAYBACK' -and
                           $PeripheralPowerH -match 'RDX_SHARED_VDD_BUSY_FILE_OP' -and
                           $PeripheralPowerH -match 'RDX_SHARED_VDD_BUSY_USB_MSC' -and
                           $PeripheralPowerH -match 'RDX_SHARED_VDD_BUSY_FORMAT_RECOVERY' -and
                           $PeripheralPowerH -match 'RDX_SHARED_VDD_BUSY_RGB_REQUIRED' -and
                           $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_busy_snapshot.*?rdx_ble_server_get_connected_count.*?record->run.*?playback.state.*?rdx_is_file_transfer_active.*?app_in_mode\(APP_MODE_PC\).*?rdx_uxfile_is_formatting.*?rdx_led_ctrl_get_scene' -and
                           $PeripheralPower -match '(?s)os_taskq_post_type\("app_core",\s*Q_CALLBACK.*?event_post_failed' -and
                           $PeripheralPower -match '(?s)RDX_SHARED_VDD_EVENT_SLOW_ADV.*?idle_request.*?rdx_peripheral_power_vdd_idle_evaluate'
Assert-Contract 'T2620_SHARED_VDD_BUSY_SNAPSHOT' $sharedVddBusySnapshotOk `
    'shared VDD must aggregate link, storage, PC, format and RGB facts and serialize decisions on app_core'

$sharedVddBusinessCompletionOk = $PeripheralPower -match 'RDX_SHARED_VDD_IDLE_RECHECK_MS\s+\(1000u\)' -and
                                  $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_idle_recheck_schedule.*?sys_timeout_add\(.*?rdx_peripheral_power_vdd_idle_recheck_cb' -and
                                  $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_idle_recheck_cb.*?RDX_SHARED_VDD_EVENT_BUSINESS_CHANGED.*?epoch' -and
                                  $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_restore_idle_domain.*?last_would_off_generation\s*=\s*\(u32\)-1.*?restored reason=' -and
                                  $PeripheralPower -match '(?s)case\s+RDX_SHARED_VDD_EVENT_BUSINESS_CHANGED:.*?stale_business_ignored.*?rdx_peripheral_power_vdd_idle_evaluate' -and
                                  $PeripheralPower -match '(?s)case\s+RDX_SHARED_VDD_EVENT_FAST_ADV:.*?rdx_peripheral_power_vdd_idle_recheck_cancel' -and
                                  $RdxRecord -match '(?s)rdx_record_set_process_state_ready.*?record_status\.run\s*==\s*RECORD_STATE_STOP.*?rdx_peripheral_power_vdd_business_changed_notify' -and
                                  $RdxPlayback -match '(?s)static void\s+pb_finish_stop.*?pb\.state\s*=.*?rdx_peripheral_power_vdd_business_changed_notify' -and
                                  $RdxLedCtrl -match '(?s)void\s+rdx_led_ctrl_set_scene.*?scene\s*==\s*RDX_LED_SCENE_OFF.*?rdx_peripheral_power_vdd_business_changed_notify' -and
                                  $RdxLedCtrl -match '(?s)static void\s+_rdx_led_restore_system_state.*?rdx_ble_server_has_active_link\(\)\s*\|\|\s*rdx_peripheral_power_vdd_is_slow_adv\(\).*?RDX_LED_SCENE_OFF'
Assert-Contract 'T2620_SHARED_VDD_REEVALUATES_AFTER_BUSINESS' $sharedVddBusinessCompletionOk `
    'slow advertising must retry after record/playback/RGB completion, with an epoch-bound fallback for opaque library activity'

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

$recordPlayToggleRoute = Get-SourceSlice $RdxApp `
    'case APP_MSG_REC_PLAY_TOGGLE:' `
    'case APP_MSG_TWS_START_PAIR:'
$recordPlayToggleOk = $RdxKey -match '(?s)u8\s+key_table_io_num4_normal\s*\[KEY_ACTION_MAX\]\s*=\s*\{\s*APP_MSG_REC_PLAY_TOGGLE,' -and
                      (Test-TokensInOrder $recordPlayToggleRoute @(
                          'RECORD_STATE_START',
                          'RECORD_STATE_RESUME',
                          'rdx_record_add_mark(RDX_MARK_SOURCE_KEY)',
                          'rdx_playback_toggle()'
                      ))
Assert-Contract 'RDX_RECORD_PLAY_KEY_STATE_DISPATCH' $recordPlayToggleOk `
    'the offline record/play key must add a device mark while recording and otherwise retain playback toggle behavior'

$markSuccessPath = Get-SourceSlice $RdxRecord 'static int rdx_record_add_mark_internal(' 'int rdx_record_add_mark(u8 source)'
$recordMarkFeedbackOk = $markSuccessPath -match '(?s)return RDX_RECMARK_RESULT_BUSY;.*?s_cur_marks\[s_cur_mark_count\+\+\] = offset_ms;\s*/\*.*?\*/\s*rdx_led_ctrl_set_scene\(RDX_LED_SCENE_RECORD_MARK\)' -and
                        $recordPlayToggleRoute -notmatch 'rdx_led_ctrl_set_scene\(RDX_LED_SCENE_RECORD_MARK\)' -and
                        $RdxLedCtrlH -match 'RDX_LED_SCENE_RECORD_MARK' -and
                        $RdxLedCfg -match '(?s)\[RDX_LED_SCENE_RECORD_MARK\]\s*=\s*RDX_LED_EFFECT_RECORD_MARK_YELLOW.*?\[RDX_LED_EFFECT_RECORD_MARK_YELLOW\]\s*=\s*\{.*?RDX_LED_MODE_SOLID_TIMEOUT.*?\.r\s*=\s*255\s*,\s*\.g\s*=\s*160\s*,\s*\.b\s*=\s*0.*?\.timeout_ms\s*=\s*2000' -and
                        $RdxLedCtrl -match '(?s)g_current_scene\s*==\s*RDX_LED_SCENE_RECORD_MARK.*?_rdx_led_restore_system_state\(\)'
Assert-Contract 'RDX_RECORD_MARK_LED_FEEDBACK' $recordMarkFeedbackOk `
    'successful APP and key marks must share two-second yellow feedback after validation, without duplicate key feedback, then restore the system scene'

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
                       $RdxRecord -match '(?s)if\(!rdx_record_stream_only_session_is_active\(\)\).*?rdx_uxfile_finish_record\(\);' -and
                       $RdxServer -match '(?s)if\(!rdx_record_stream_only_session_is_active\(\)\).*?rp->orig_mode\s*=\s*RECORD_MODE_OFFLINE;'
Assert-Contract 'RDX_HOLD_RECORDING_IS_STREAM_ONLY' $holdRecordStorageOk `
    'only hold-triggered online recording may report empty identity and skip local persistence'

$uxfileStartupRecoveryOk = $Makefile -match '(?m)^\s*apps/common/third_party_profile/rdx_protocol/librdxApp_patched\.a\s*\\\s*$' -and
                           $Makefile -notmatch '(?m)^\s*apps/common/third_party_profile/rdx_protocol/librdxApp\.a\s*\\\s*$' -and
                           $PatchedRdxArchiveHash -eq '2D844D806F0F26B03BE03C8ACE7495A456CB8A4D19E381E4423C50FDA7518C64' -and
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

$recordMarkPersistenceOk = $RdxLibraryPatch.Contains("'  %113 = sub i32 4, %105, !dbg !1146'") -and
                           $RdxLibraryPatch.Contains("'  %113 = sub i32 248, %105, !dbg !1146'") -and
                           $RdxLibraryPatch.Contains("'  %122 = icmp ugt i32 %121, 247, !dbg !1157'") -and
                           $RdxLibraryPatch.Contains("'  %126 = icmp ult i32 %125, 248, !dbg !1165'") -and
                           $RdxLibraryPatch.Contains("'  store i8 93, i8* getelementptr inbounds ([248 x i8], [248 x i8]* @s_marks_str, i32 0, i32 246), align 1, !dbg !1173, !tbaa !710'") -and
                           $RdxLibraryPatch.Contains("'  %133 = phi i8* [ getelementptr inbounds ([248 x i8], [248 x i8]* @s_marks_str, i32 0, i32 247), %131 ], [ %130, %127 ]'") -and
                           $RdxLibraryPatch -match 'record marks persistence buffer capacity'
Assert-Contract 'RDX_RECORD_MARKS_PERSIST_TO_DAT' $recordMarkPersistenceOk `
    'the UXFILE serializer must use the full 248-byte marks buffer for every append and closing boundary'

$recordToneFinish = Get-SourceSlice $RdxRecord `
    'static void rdx_record_start_tone_finish(' 'static void rdx_record_start_tone_complete('
$recordToneValid = Get-SourceSlice $RdxRecord `
    'static bool rdx_record_start_tone_is_current(' 'static void rdx_record_start_tone_finish('
$recordToneCallback = Get-SourceSlice $RdxRecord `
    'static int rdx_record_start_tone_callback(' 'static void rdx_record_start_tone_play('
$recordToneWait = Get-SourceSlice $RdxRecord `
    'static bool rdx_record_start_tone_wait(' 'static int rdx_record_stop_tone_callback('
$recordUi = Get-SourceSlice $RdxRecord `
    'void rdx_record_ui_notify(void)' 'void rdx_record_auto_run(' -Last
$recordProcessBranches = [regex]::Matches($RdxRecord, '(?s)void rdx_record_process\(void\)\s*\{.*?(?=\r?\n\})')
$recordToneOrderingOk = $recordProcessBranches.Count -eq 2
foreach ($branch in $recordProcessBranches) {
    $recordToneOrderingOk = $recordToneOrderingOk -and (Test-TokensInOrder $branch.Value @(
        'rdx_record_start_tone_wait()', 'rdx_record_set_process_state_busy()', 'case RECORD_STATE_START:'
    ))
}
Assert-Contract 'RDX_RECORD_START_WAITS_FOR_TONE' `
    ($recordToneOrderingOk -and
     $RdxRecord -match 'play_tone_file_with_completion\(get_tone_files\(\)->ding' -and
     $recordToneFinish -match 'record_start_tone_completed != epoch' -and
     $recordToneFinish -match 'rdx_record_start_tone_cancel\(\)' -and
     $recordToneCallback -match 'os_taskq_post_type\("app_core"' -and
     $recordToneCallback -notmatch 'rdx_record_process\(' -and
     $recordToneValid -match 'epoch == record_start_tone_epoch' -and
     $recordToneValid -match 'goto_poweroff_flag' -and
     $recordToneValid -match 'rdx_app_get_poweroff_flag' -and
     $recordToneValid -match 'rdx_record_online_session_is_current' -and
     $recordToneWait -match '(?s)record_status.run != RECORD_STATE_START.*?rdx_record_start_tone_cancel' -and
     $recordUi -notmatch 'tone_.*(?:post|play)\(') `
    'both recording branches must wait for natural tone completion and reject stale, cancelled, or power-off starts'

$recordTask = Get-SourceSlice $RdxRecord 'static void rdx_record_task(' 'int rdx_record_task_create('
$recordStopTone = Get-SourceSlice $RdxRecord `
    'static int rdx_record_stop_tone_callback(' 'static void rdx_record_stop_tone_play('
Assert-Contract 'RDX_RECORD_STOP_TONE_AFTER_CAPTURE_CLOSE' `
    ((Test-TokensInOrder $recordTask @('translation_ear_recoder_close_all();', 'rdx_record_stop_tone_post(tone_epoch);')) -and
     $recordStopTone -match 'STREAM_EVENT_INIT' -and
     $recordStopTone -match 'record_start_tone_epoch' -and
     $recordStopTone -match 'record_status.run != RECORD_STATE_STOP') `
    'the stop prompt must follow closure of both capture paths and be rejected if a new recording has started'

$recordingEncoderPathOk = $Config -notmatch 'TCFG_STENC_OPUS_ENABLE' -and
                          $SdkUsedList -match '(?s)#if TCFG_STENC_OPUS_ENABLE\s+opus_stenc_plug\s+#endif' -and
                          $EffectDev2 -match 'get_opus_stenc_ops\s*\(\s*\)'
Assert-Contract 'RDX_RECORDING_USES_EFFECT_DEV2_ENCODER' $recordingEncoderPathOk `
    'RDX recording must use effect_dev2; the native legacy registration stays behind its disabled gate'

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

$entryGatesOk = $Pc -match '(?s)static int pc_mode_try_enter.*?rdx_dip_switch_pc_allowed\(\).*?rdx_pc_storage_is_busy\(\)' -and
                $Dip -match '(?s)int rdx_dip_switch_pc_allowed.*?!get_power_on_status\(\).*?usb_otg_online\(0\) == SLAVE_MODE.*?rdx_dip_switch_cold_service\(\)' -and
                $RdxApp -match '(?s)u8 rdx_pc_storage_is_busy.*?rdx_app_storage_activity_is_busy\s*\(\s*"PC-STORAGE"\s*,\s*0\s*\)' -and
                $RdxApp -match '(?s)static u8 rdx_app_storage_activity_is_busy.*?RECORD_STATE_STOP.*?rdx_record_process_is_busy_check.*?rdx_is_file_transfer_active.*?rdx_is_file_sync_busy'
Assert-Contract 'PC_ENTRY_REQUIRES_OFF_HOST_AND_IDLE_STORAGE' $entryGatesOk `
    'Only OFF with a confirmed host and a fresh RDX runtime may export; storage activity still blocks takeover'

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
              $Pc -match '(?s)case APP_MSG_REQUEST_POWEROFF:.*?poweroff blocked.*?return 1;' -and
              $Pc -match '(?s)if \(pc_app_msg_handler\(msg \+ 1\)\).*?continue;' -and
              $AppDefault -match '(?s)case APP_MSG_REQUEST_POWEROFF:.*?sys_enter_soft_poweroff'
Assert-Contract 'POWEROFF_USES_MODE_CLEANUP' $poweroffOk `
    'DIP power-off must request normal application shutdown so PC storage is released first'

$Charge = Read-RepoFile $RepoRoot 'SDK\apps\earphone\battery\charge.c'
$RdxCharge = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_charge.c'
$Charger = Read-RepoFile $RepoRoot 'SDK\apps\common\device\charge\sk4558.c'
$AppMain = Read-RepoFile $RepoRoot 'SDK\apps\earphone\app_main.c'
$PcDevice = Read-RepoFile $RepoRoot 'SDK\apps\common\device\usb\device\task_pc.c'

Assert-Contract 'PC_REVALIDATES_BEFORE_USB_START' (
    $Pc -match '(?s)static int pc_task_start.*?rdx_dip_switch_pc_allowed\(\).*?USBSTACK_START' -and
    $Pc -match '(?s)#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE\s+if \(r == SLAVE_MODE\)' -and
    $PcDevice -match '(?s)if \(!rdx_dip_switch_pc_allowed\(\)\).*?MSC disabled.*?return false' -and
    $AppDefault -match '(?s)if \(ret == 1\).*?!rdx_dip_switch_pc_allowed\(\).*?break;'
) 'Both OTG entry and delayed MSC start must enforce the exclusive OFF/host policy'

Assert-Contract 'COLD_PC_HAS_NO_RDX_BUSINESS' (
    $AppMain -match '(?s)dev_manager_init\(\);.*?rdx_dip_switch_init\(\);.*?struct app_mode \*mode' -and
    $RdxApp -match '(?s)void rdx_app_all_init.*?rdx_business_started = true;' -and
    $RdxApp -notmatch 'rdx_business_started = (?:false|0)' -and
    $Dip -match 'return !s_business_mode_entered && !rdx_app_business_started\(\)' -and
    $Dip -notmatch 's_business_mode_entered = (?:false|0)' -and
    $AppMain -match '(?s)app_goto_mode.*?APP_MODE_BT.*?APP_MODE_POWERON.*?rdx_dip_switch_note_business_mode\(\)' -and
    $RdxApp -match '(?s)int rdx_app_msg_handler.*?!rdx_app_business_started\(\).*?return false;' -and
    $RdxApp -match '(?s)int rdx_app_key_msg_handler.*?!rdx_app_business_started\(\).*?return true;'
) 'Independent DIP startup and sticky business latch must prevent cold PC business access and warm takeover'

Assert-Contract 'CHARGE_SERVICE_IS_NONBLOCKING_AND_USB_SAFE' (
    $Charge -match '(?s)app_charge_wait_otg_role.*?#if TCFG_DIP_SWITCH_POWER_ENABLE.*?return usb_otg_online\(0\);.*?#else' -and
    $Charger -match '(?s)#if \(RDX_BJ_VERSION != BJ_BOARD_VERSION_03\)\s+usb_iomode\(1\);\s+#endif' -and
    $RdxCharge -match '(?s)if \(rdx_app_business_started\(\)\).*?rdx_protocol_update_dev_battery_level' -and
    $RdxCharge -match '(?s)void rdx_app_charge_stop.*?sys_timer_del\(incharge_full_check_timer\);.*?incharge_full_check_timer = 0;' -and
    $RdxCharge -match 'cur_charge_state == RDX_CHARGE_IN && incharge_full_check_timer == 0'
) 'OTG must not block app_core; cold charging must avoid protocol access, USB pin takeover and stale timers'

Assert-Contract 'PC_FAILURE_CANNOT_FALL_BACK_TO_BLE_WHILE_OFF' (
    $Pc -match '(?s)static void pc_start_failed.*?#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE\s+app_send_message\(APP_MSG_GOTO_MODE, APP_MODE_IDLE \| \(IDLE_MODE_CHARGE << 8\)\);\s+#else\s+app_send_message\(APP_MSG_GOTO_NEXT_MODE' -and
    $AppMain -match '(?s)!get_power_on_status\(\).*?next_mode->name != APP_MODE_PC.*?next_mode->name != APP_MODE_IDLE.*?app_get_mode_by_name\(APP_MODE_IDLE\)' -and
    $AppDefault -match '(?s)case APP_MSG_REQUEST_POWEROFF:.*?rdx_dip_switch_cold_service\(\).*?IDLE_MODE_POWEROFF'
) 'OFF fallback and cold poweroff must stay in native idle after PC cleanup, without BT initialization'

Assert-Contract 'PC_RETRY_IS_BOUNDED_AND_REVALIDATED' (
    $Dip -match '#define DIP_SERVICE_MAX_ATTEMPTS 3' -and
    $Dip -match '(?s)on != s_last_on \|\| usb != s_last_usb \|\| vbus != s_last_vbus.*?s_attempts = 0;.*?s_retry_ticks = 0;' -and
    $Dip -match '(?s)if \(s_attempts >= DIP_SERVICE_MAX_ATTEMPTS\).*?return;.*?rdx_dip_switch_pc_allowed\(\).*?target = APP_MODE_PC;' -and
    $Dip -match '(?s)if \(target >= 0\).*?\+\+s_attempts;.*?s_retry_ticks = DIP_SERVICE_RETRY_TICKS;.*?app_send_message\(APP_MSG_GOTO_MODE, target\)' -and
    $AppMain -match '(?s)if \(err != 0\).*?#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE.*?next_mode->name == APP_MODE_PC \|\| app_in_mode\(APP_MODE_PC\).*?return NULL;.*?#endif.*?sys_timeout_add'
) 'Rejected or failed PC entry must be reconsidered with bounded requests; native timers must not replay stale PC targets'

Assert-Contract 'PC_GENERIC_FALLBACK_IS_PRESERVED' (
    $AppDefault -match '(?s)else if \(ret == 2\).*?#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE.*?IDLE_MODE_CHARGE.*?#else\s+app_send_message\(APP_MSG_GOTO_NEXT_MODE' -and
    $PcDevice -match '(?s)#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE\s+if \(!rdx_dip_switch_pc_allowed\(\)\)'
) 'Product USB policy must be shared and generic USB removal fallback preserved'

$chargePrepare = Get-SourceSlice $RdxCharge 'void rdx_app_charge_prepare(void)' 'int rdx_app_battery_msg_handler(int *msg)'
$productPrepare = Get-SourceSlice $chargePrepare '#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE' '#else'
$chargeEvents = Get-SourceSlice $RdxCharge 'int rdx_app_battery_msg_handler(int *msg)' 'APP_MSG_PROB_HANDLER(rdx_app_battery_msg_entry)'
$productChargeEvents = Get-SourceSlice $chargeEvents `
    '#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE' '#elif TCFG_DIP_SWITCH_POWER_ENABLE'
Assert-Contract 'ON_USB_PRESERVES_RECORDING_AND_STORAGE' (
    $productPrepare -match 'return;' -and
    $productPrepare -notmatch 'rdx_record_process|rdx_ota_stop|rdx_app_wifi_handle|rdx_app_emmc_power' -and
    $productChargeEvents -match '(?s)case CHARGE_EVENT_LDO5V_IN:.*?case CHARGE_EVENT_LDO5V_KEEP:.*?rdx_app_charge_start\(\)' -and
    $productChargeEvents -notmatch 'rdx_record_process\(|rdx_app_emmc_poweroff\(|rdx_app_normal_poweroff\(' -and
    $RdxCharge -match '(?s)#if \(TCFG_CHARGE_POWERON_ENABLE == 0\) &&\s*\\\s*!\(TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE\).*?rdx_app_emmc_poweron\(0\)'
) 'Product USB insertion must maintain charge state without stopping recording, transfers or storage power'

$Poweroff = Read-RepoFile $RepoRoot 'SDK\apps\earphone\mode\bt\poweroff.c'
$autoShutdown = Get-SourceSlice $Poweroff 'void sys_auto_shut_down_enable(void)' 'static void sys_auto_shut_down_deal(void *priv)'
Assert-Contract 'ON_USB_CHARGE_LIFECYCLE' (
    $productChargeEvents -match '(?s)rdx_app_charge_start\(\).*?rdx_app_business_started\(\).*?rdx_ble_server_auto_shut_down_enable\(0\)' -and
    $productChargeEvents -match '(?s)case CHARGE_EVENT_LDO5V_OFF:.*?rdx_app_charge_stop\(\).*?rdx_app_business_started\(\) && get_power_on_status\(\) &&.*?!app_var.goto_poweroff_flag.*?rdx_ble_server_auto_shut_down_enable\(1\)' -and
    $autoShutdown -match '(?s)#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE.*?if \(get_charge_online_flag\(\)\).*?sys_auto_shut_down_disable\(\);.*?return;' -and
    $RdxApp -match '(?s)#if \(TCFG_CHARGE_POWERON_ENABLE == 1\) \|\|\s*\\\s*\(TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE\).*?get_charge_online_flag\(\).*?rdx_app_charge_start\(\)' -and
    $Charge -match '(?s)case CHARGE_EVENT_LDO5V_OFF:.*?if \(!get_power_on_status\(\)\).*?APP_MSG_REQUEST_POWEROFF.*?charge_ldo5v_off_deal\(\)'
) 'USB power including FULL must inhibit inactivity shutdown; ON removal restores the normal business-aware timer policy'

$usbLedPolicy = Get-SourceSlice $RdxLedCtrl 'static bool _rdx_led_on_usb_charge(void)' 'static void _rdx_led_restore_system_state(void)'
$ledSceneEntry = Get-SourceSlice $RdxLedCtrl 'void rdx_led_ctrl_set_scene(' 'rdx_led_scene_e rdx_led_ctrl_get_scene('
$ledChargeEntry = Get-SourceSlice $RdxLedCtrl 'void rdx_led_ctrl_set_charge_state_by_battery(' 'void rdx_led_ctrl_restore_system_state('
Assert-Contract 'ON_CHARGE_LED_BUSINESS_PRIORITY' (
    $usbLedPolicy -match '(?s)#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE.*?get_power_on_status\(\) && rdx_app_business_started\(\).*?get_charge_online_flag\(\).*?RDX_CHARGE_OUT.*?!app_var.goto_poweroff_flag && !get_vbat_need_shutdown\(\)' -and
    (Test-TokensInOrder $usbLedPolicy @('RECORD_STATE_START', 'RDX_LED_SCENE_RECORD_MARK', 'RDX_LED_SCENE_RECORD_START', 'RDX_LED_SCENE_DUT_ENTER', 'RDX_LED_SCENE_OTA_START', 'RDX_LED_SCENE_WIFI_START', 'RDX_LED_SCENE_CHARGE_FULL')) -and
    $ledSceneEntry -match '(?s)if \(on_usb_charge\).*?_rdx_led_resolve_on_usb_charge\(scene\).*?scene == g_current_scene && g_active_effect && !new_mark.*?return;' -and
    $ledChargeEntry -match '(?s)if \(_rdx_led_on_usb_charge\(\)\).*?rdx_led_ctrl_set_scene\(RDX_LED_SCENE_CHARGE_PLUG_IN\);.*?return;.*?_rdx_led_set_charge_effect_by_battery'
) 'Only normal ON USB charging arbitrates business above charge/FULL; charge updates share scene entry and preserve effect phase'
Assert-Contract 'ON_CHARGE_LED_RECOVERS_FROM_LIVE_STATE' (
    $usbLedPolicy -match 'g_effect_elapsed_ms < g_active_effect->timeout_ms' -and
    $RdxLedCtrl -match '(?s)void rdx_led_ctrl_update\(void\).*?_rdx_led_on_usb_charge\(\).*?_rdx_led_resolve_on_usb_charge\(RDX_LED_SCENE_OFF\) != g_current_scene.*?rdx_led_ctrl_set_scene\(RDX_LED_SCENE_OFF\)' -and
    $ledSceneEntry -match '!on_usb_charge && g_current_scene == RDX_LED_SCENE_LOW_BATTERY'
) 'The existing LED tick reconciles silent OTA exits and expired marks without restarting unchanged effects or retaining a stale warning lock'

$StoragePatch = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\patch_librdxApp_storage.ps1'
$StorageIr = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_storage_patch.ll'
$DipSwitch = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_dip_switch.c'
Assert-Contract 'USB_HOT_SWITCH_REQUIRES_REAL_COMPLETION' (
    $DipSwitch -match '(?s)s_usb_switch = USB_SWITCH_DRAIN.*?rdx_record_usb_quiesce_request' -and
    (Test-TokensInOrder $DipSwitch @('rdx_record_usb_quiesce_poll', 'rdx_ble_server_usb_quiesce', 'saved == 0 && ble_idle', 'rdx_uxfile_fence_request', 'rdx_uxfile_fence_poll', 'else if (!result)', 'rdx_cpu_reset()')) -and
    $DipSwitch -match '(?s)rdx_usb_switch_fail\(.*?USB_SWITCH_FAILED.*?no export/reset' -and
    $DipSwitch -match 'rdx_dip_switch_cold_service\(\) && !app_var.goto_poweroff_flag' -and
    $Poweroff -match '(?s)void sys_enter_soft_poweroff\(enum poweroff_reason reason\).*?reason == POWEROFF_NORMAL && rdx_dip_switch_shutdown_deferred\(\) &&.*?!get_vbat_need_shutdown\(\).*?return;' -and
    $RdxRecord -match '(?s)msg\[1\] == RDX_RECORD_USB_FENCE.*?translation_ear_recoder_close_all\(\).*?rdx_uxfile_finish_record\(\).*?usb_record_done = ' -and
    $RdxServer -match '(?s)int rdx_ble_server_usb_quiesce\(void\).*?app_ble_disconnect.*?rdx_ble_server_rdx_runtime_try_rearm\(\)'
) 'Hot switch must close recording, drain real BLE lifecycle and freeze UXFILE before reset; cold export and failure protection remain intact'
Assert-Contract 'USB_PC_RETURN_GATES_BUSINESS_AND_INDEX' (
    (Test-TokensInOrder $Pc @('dev_manager_restore("sd0")', 'HOST_OWNED -> DEVICE_OWNED', 'rdx_dip_switch_pc_returned()')) -and
    $DipSwitch -match '(?s)rdx_dip_switch_business_blocked\(void\).*?rdx_uxfile_pc_refresh_status\(\) != 0' -and
    $RdxServer -match '(?s)int rdx_ble_server_adv_enable\(u8 enable\).*?rdx_dip_switch_business_blocked\(\).*?enable = 0;' -and
    $RdxRecord -match '(?s)void rdx_record_process\(void\).*?rdx_dip_switch_business_blocked\(\) && record_status.run != RECORD_STATE_STOP' -and
    $RdxPlayback -match '(?s)bool rdx_playback_can_start\(void\).*?rdx_dip_switch_business_blocked' -and
    $StoragePatch -match 'rdx_storage_boot_refresh_post' -and
    $StorageIr -match '(?s)rdx_storage_boot_reconcile.*?rdx_uxfile_sync_files_with_dat.*?store volatile i32 %state, i32\* @rdx_storage_refresh'
) 'PC return must stop USB/remount before reconciliation and gate BLE, recording and playback until completion'
Assert-Contract 'UXFILE_PATCH_QUIET_AND_ERROR_BOUNDARY' (
    $Makefile -match '(?m)^\$\(OUT_ELF\): apps/common/third_party_profile/rdx_protocol/librdxApp_patched\.a Makefile \| pre_build' -and
    $RdxLibraryPatch -match 'Update-RdxStorageIr' -and
    $StoragePatch -match 'retain dirty on storage I/O failure' -and
    $StoragePatch -match 'raw open failure cannot bypass safe shutdown' -and
    $StoragePatch -match 'freeze file worker after successful fence' -and
    $StorageIr -match '(?s)rdx_storage_fwrite.*?mul i32 %size, %count.*?icmp eq i32 %written, %expected' -and
    (Test-TokensInOrder $StorageIr @('define internal void @rdx_storage_fence_arrive', 'rdx_uxfile_process_delete_queue', 'rdx_uxfile_close_read_file_handle', 'rdx_uxfile_flush_cache', 'rdx_storage_f_flush_wbuf', 'store volatile i32 %quiet', 'store volatile i32 %ticket, i32* @rdx_storage_fence_done'))
) 'The pinned patch must retain I/O errors, stop library producers, and publish the matching completion only after flush and worker freeze'

Write-Host 'T2620 product contracts passed.'
