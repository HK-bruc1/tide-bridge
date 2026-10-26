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
$DevManager = Read-RepoFile $RepoRoot 'SDK\apps\common\dev_manager\dev_manager.c'
$RdxApp = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_app.c'
$RdxRecord = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_record.c'
$RdxPlayback = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_playback.c'
$RdxLedCtrl = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_led_ctrl.c'
$RdxServer = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_ble_server.c'
$RdxServerH = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_ble_server.h'
$RdxAppConfig = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_app_config.h'
$PeripheralPower = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_peripheral_power.c'
$PeripheralPowerH = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_peripheral_power.h'
$AdvPolicy = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_adv_policy.c'
$AppMain = Read-RepoFile $RepoRoot 'SDK\apps\earphone\app_main.c'
$RdxLibraryPatch = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\patch_librdxApp.ps1'
$Makefile = Read-RepoFile $RepoRoot 'SDK\Makefile'
$PatchedRdxArchivePath = Join-Path $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\librdxApp_patched.a'
$PatchedRdxArchiveHash = if (Test-Path -LiteralPath $PatchedRdxArchivePath) {
    (Get-FileHash -Algorithm SHA256 -LiteralPath $PatchedRdxArchivePath).Hash
} else {
    ''
}
$StorageLifecycle = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_storage_lifecycle.c'
$KeyCancelVm = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_vm.c'
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

$BeanstalkAppConfig = Get-SourceSlice $RdxAppConfig `
    '#elif (RDX_AI_SEL_APP & APP_BEANSTALK_EN)' `
    '#elif (RDX_AI_SEL_APP & APP_CUSTOM_TEST_EN)'
$BeanstalkT2620Config = Get-SourceSlice $BeanstalkAppConfig `
    '#elif (RDX_SEL_DEVICE == DEVICE_BEANSTALK_RKB_T2620)' `
    '#endif'
$CustomTestAppConfig = Get-SourceSlice $RdxAppConfig `
    '#elif (RDX_AI_SEL_APP & APP_CUSTOM_TEST_EN)' `
    '#elif (RDX_AI_SEL_APP & APP_ZENCHORD_EN)'
$SharedT2620ProductDefines = @{
    RDX_FREQUENCY_OFFSET = '(15)'
    PRODUCT_TYPE = '"K1"'
    RDX_AI_TRANSLATE_SUPPORT = '(0)'
    FACTORY_CODE = '"GSPKHW"'
    FACTORY_CODE_SIZE = 'strlen(FACTORY_CODE)'
    PRODUCT_CODE = '"607"'
    PRODUCT_CODE_SIZE = 'strlen(PRODUCT_CODE)'
    WIFI_AP_PASSWORD = '"88888888"'
    HARDWARE_VERSION = '"0.0.1"'
    HARDWARE_VERSION_HEX = '0x00000001'
}
foreach ($profile in @(
    @{ Name = 'BEANSTALK'; Source = $BeanstalkT2620Config; DeviceName = '"VoicePad"'; NameSuffix = '(1)'; Version = '"1.1.4"'; VersionHex = '0x00010104' },
    @{ Name = 'CUSTOM_TEST'; Source = $CustomTestAppConfig; DeviceName = '"GensparkVoicePad"'; NameSuffix = '(0)'; Version = '"1.1.2"'; VersionHex = '0x00010102' }
)) {
    $ExpectedProductDefines = $SharedT2620ProductDefines.Clone()
    foreach ($macro in @('BT_NAME', 'BLE_LOCAL_NAME', 'WIFI_AP_SSID')) {
        $ExpectedProductDefines[$macro] = $profile.DeviceName
    }
    $ExpectedProductDefines['RDX_BLE_NAME_AUTH_SUFFIX_ENABLE'] = $profile.NameSuffix
    $ExpectedProductDefines['FIRMWARE_VERSION'] = $profile.Version
    $ExpectedProductDefines['FIRMWARE_VERSION_HEX'] = $profile.VersionHex
    foreach ($macro in $ExpectedProductDefines.Keys) {
        $pattern = '(?m)^[ \t]*#define[ \t]+' + [regex]::Escape($macro) + '[ \t]+([^\r\n]+)'
        $definitions = [regex]::Matches($profile.Source, $pattern)
        Assert-Contract "T2620_$($profile.Name)_$macro" `
            ($definitions.Count -eq 1 -and
             $definitions[0].Groups[1].Value.Trim() -ceq $ExpectedProductDefines[$macro]) `
            "$($profile.Name) must define $macro exactly once as $($ExpectedProductDefines[$macro])"
    }
}

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
                     $ampImplementation -match 'rdx_peripheral_power_amp_set\(g_rdx_amp_analog_ready\s*&&\s*g_rdx_amp_output_running\)' -and
                     $ampImplementation -match '(?s)void\s+audio_dac_power_state\s*\(u8\s+state\)\s*\{\s*rdx_amp_apply\(RDX_AMP_SRC_ANALOG,\s*state\);' -and
                     $ampImplementation -match '(?s)void\s+_audio_dac_status_hook\s*\(u8\s+status\)\s*\{\s*rdx_amp_apply\(RDX_AMP_SRC_DIGITAL,\s*status\);' -and
                     $ampImplementation -match 'gpio_set_mode\(IO_PORT_SPILT\(TCFG_T2620_AMP_ENABLE_IO\)' -and
                     $ampImplementation -notmatch 'sys_timeout_add|sys_timer_add|os_time_dly'
Assert-Contract 'T2620_AMP_FOLLOWS_DAC_LIFECYCLE' $ampDacLifecycleOk `
    'PE5 must follow DAC analog readiness AND digital output running, without timers'

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

$rdxPowerSources = Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol') -Recurse -File |
    Where-Object { $_.Extension -in '.c', '.h' } |
    ForEach-Object { Get-Content -Raw -Encoding UTF8 -LiteralPath $_.FullName }
$micNativeOwnershipOk = ($rdxPowerSources -join "`n") -notmatch '\bVDD_POWER_(PORT_IO|PORT|PIN)\b|\bIO_PORTA_00\b|\brdx_app_do_emmc_reset\b' -and
                        $SdkConfigH -match '(?m)^#define\s+TCFG_ADC0_BIAS_SEL\s+16\b' -and
                        $SdkConfigH -match '(?m)^#define\s+TCFG_ADC3_BIAS_SEL\s+16\b'
Assert-Contract 'T2620_MIC_PA0_NATIVE_OWNERSHIP' $micNativeOwnershipOk `
    'PA0 must stay with JL MIC_LDO (ADC0/ADC3), never the legacy RDX VDD alias or GPIO paths'

$legacyStoragePower = Get-SourceSlice $RdxApp `
    'void rdx_app_emmc_poweron(u8 check_en)' `
    '#if RDX_WIFI_ENABLE' -Last
$legacyStorageDelegatesOk = $legacyStoragePower -match 'rdx_peripheral_power_vdd_ensure_on\(RDX_SHARED_VDD_WAKE_BUSINESS\)' -and
                           $legacyStoragePower -match 'rdx_peripheral_power_vdd_business_changed_notify\(\)' -and
                           $legacyStoragePower -notmatch 'gpio_set_mode|sd_set_power|sys_(timeout|timer)_(add|del)' -and
                           $RdxApp -notmatch '\bemmc_poweroff_flag\b|\bemmc_poweroff_check_timer\b|\bEMMC_LDO_POWER_OFF_CHECK_TIMEOUT\b|\brdx_app_emmc_poweroff_check_timer_(start|rerun|cb)\b'
Assert-Contract 'T2620_LEGACY_STORAGE_POWER_DELEGATES' $legacyStorageDelegatesOk `
    'legacy library entry points must delegate to the shared rail manager without a second power state or timer'

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
                      $RdxApp -match '(?s)int\s+rdx_led_hardware_deinit.*?rdx_led_ctrl_deinit\(\).*?led_pt0807_deinit' -and
                      $Pc -match '(?s)pc_storage_prepare.*?rdx_peripheral_power_vdd_usb_prepare\(\).*?dev_manager_takeover\("sd0"\).*?rdx_peripheral_power_vdd_usb_takeover_complete\(1\)' -and
                      $Pc -match '(?s)pc_storage_restore.*?dev_manager_restore\("sd0"\).*?rdx_peripheral_power_vdd_usb_restore_complete\(1\)'
Assert-Contract 'T2620_SHARED_VDD_QUIESCE_AND_RESTORE' $sharedVddQuiesceOk `
    'shared VDD must order SD/RGB quiesce, physical cut, stale-event rejection, recovery and USB ownership handoff'

$sharedVddTransitionSerializationOk = $PeripheralPower -match 'OS_MUTEX\s+transition_mutex' -and
                                      $PeripheralPower -match 'os_mutex_create\(&g_rdx_shared_vdd\.transition_mutex\)' -and
                                      $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_ensure_on.*?os_mutex_pend\(&g_rdx_shared_vdd\.transition_mutex.*?rdx_peripheral_power_vdd_restore_idle_domain.*?os_mutex_post\(&g_rdx_shared_vdd\.transition_mutex' -and
                                      $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_idle_stop\(u32\s+idle_epoch,\s*u8\s+source\).*?os_mutex_pend\(&g_rdx_shared_vdd\.transition_mutex.*?g_rdx_shared_vdd\.wake_epoch\s*!=\s*stop_epoch.*?os_mutex_post\(&g_rdx_shared_vdd\.transition_mutex' -and
                                      $PeripheralPower -match 'g_rdx_shared_vdd\.slow_adv_epoch\s*=\s*\(u32\)epoch'
Assert-Contract 'T2620_SHARED_VDD_TRANSITIONS_SERIALIZED' $sharedVddTransitionSerializationOk `
    'stop/restore transitions must be mutex-serialized and revalidate the exact slow-advertising epoch'

$sharedVddIrqSafeFinalRecheckOk = $PeripheralPower -match '(?s)local_irq_disable\(\);\s*canceled\s*=\s*g_rdx_shared_vdd\.wake_requested\s*\|\|\s*g_rdx_shared_vdd\.wake_epoch\s*!=\s*stop_epoch.*?g_rdx_shared_vdd\.busy_mask.*?!g_rdx_shared_vdd\.slow_adv\s*\|\|\s*!rdx_adv_policy_idle_valid\(\)\)\);\s*if\s*\(!canceled\)' -and
                                  $AdvPolicy -match '(?s)u8 rdx_adv_policy_idle_valid\(void\)\s*\{\s*return adv.enabled && adv.qualified && !adv.pending &&\s*adv.consumed == adv.version;\s*\}' -and
                                  $PeripheralPower -notmatch '(?s)local_irq_disable\(\);(?:(?!local_irq_enable\(\);).)*rdx_ble_server_get_connected_count\(\)'
Assert-Contract 'T2620_SHARED_VDD_FINAL_RECHECK_IS_IRQ_SAFE' $sharedVddIrqSafeFinalRecheckOk `
    'the IRQ-disabled final commit must use atomic wake facts and never call the mutex-backed JL BLE wrapper API'

$sharedVddDualAclOk = $RdxServerH -match 'u8\s+rdx_ble_server_get_connected_count\s*\(void\)' -and
                       $RdxServer -match '(?s)static u8\s+rdx_ble_server_phase0a_connected_count\s*\(void\).*?RDX_BLE_PHASE0A_WRAPPER_MAX.*?app_ble_get_hdl_con_handle' -and
                       $RdxServer -match '(?s)u8\s+rdx_ble_server_get_connected_count\s*\(void\).*?return\s+rdx_ble_server_phase0a_connected_count\(\)' -and
                       $RdxServer -match '(?s)rdx_ble_server_phase0a_link_connected.*?rdx_peripheral_power_vdd_ble_links_changed_notify\(\)' -and
                       $RdxServer -match '(?s)rdx_ble_server_phase0a_link_disconnected.*?rdx_ble_session_link_release.*?rdx_peripheral_power_vdd_ble_links_changed_notify\(\)' -and
                       $RdxServer -match '(?s)rdx_adv_policy_slow_committed.*?rdx_led_ctrl_set_scene\(RDX_LED_SCENE_OFF\).*?rdx_peripheral_power_vdd_slow_adv_notify\(\)' -and
                       $RdxServer -match '(?s)void rdx_ble_server_fast_adv_restart\(void\).*?rdx_adv_policy_activity\(\)' -and
                       $RdxServer -match '(?s)int rdx_adv_policy_radio_set\(u8 slow\).*?if \(!slow\).*?rdx_peripheral_power_vdd_fast_adv_notify\(\)' -and
                       $RdxServer -match '(?s)void rdx_adv_policy_slow_committed\(void\).*?!rdx_adv_policy_idle_valid\(\).*?rdx_peripheral_power_vdd_slow_adv_notify\(\)'
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
                                  $PeripheralPower -match '(?s)rdx_peripheral_power_vdd_restore_idle_domain.*?last_would_off_generation\s*=\s*\(u32\)-1' -and
                                  $PeripheralPower -match '(?s)case\s+RDX_SHARED_VDD_EVENT_BUSINESS_CHANGED:.*?rdx_peripheral_power_vdd_idle_evaluate' -and
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

# C 行为测试桩覆盖按键值分发、周期归属、固定路由及 STOP 重试。
# 此处仅保留这些测试桩未执行的调用入口衔接检查。
$legacyProductKeys = Get-SourceSlice $RdxApp `
    'if (key->value >= KEY_IO_NUM0 && key->value <= KEY_IO_NUM4)' `
    '// ---- KEY_POWER'
Assert-Contract 'RDX_LEGACY_IO_USAGE_DISPATCH' (
    $legacyProductKeys -match 'rdx_app_usage_key_remap' -and
    $legacyProductKeys -notmatch 'rdx_hogp_keyboard_is_ready|rdx_ble_server_has_active_link|rdx_app_key5_remap'
) 'legacy product-key entry must use the same usage dispatch without a BLE-presence veto'
Assert-Contract 'RDX_LOCAL_SYSTEM_CANCEL_CALLERS' (
    $KeyCancelVm -match '(?s)rdx_hogp_input_cancel\(\);\s*rdx_uxfile_sd_format\(rdx_vm_unbound_cb\)' -and
    $KeyCancelVm -match '(?s)rdx_hogp_input_cancel\(\);\s*rdx_uxfile_sd_format\(rdx_vm_choose_to_unbound_cb\)' -and
    $StorageLifecycle -match '(?s)void rdx_storage_lifecycle_pc_returned\(void\)\s*\{\s*rdx_hogp_input_cancel\(\)'
) 'format and PC return cancel both input paths; factory-reset and native-poweroff callers run in C harnesses'
Assert-Contract 'RDX_STREAM_ONLY_LIFECYCLE_WIRING' (
    $RdxRecord -match '(?s)if\(!rdx_record_stream_only_session_is_active\(\)\).*?rdx_uxfile_finish_record\(\);' -and
    $RdxServer -match '(?s)if\(!rdx_record_stream_only_session_is_active\(\)\).*?rp->orig_mode\s*=\s*RECORD_MODE_OFFLINE;' -and
    $RdxApp -match '\(u32\)priv != record_state_upload_generation'
) 'stream-only STOP/disconnect must not fall back to storage; stale upload timers cannot restart a request'

$uxfileStartupRecoveryOk = $Makefile -match '(?m)^\s*apps/common/third_party_profile/rdx_protocol/librdxApp_patched\.a\s*\\\s*$' -and
                           $Makefile -notmatch '(?m)^\s*apps/common/third_party_profile/rdx_protocol/librdxApp\.a\s*\\\s*$' -and
                           $PatchedRdxArchiveHash -eq 'CF87FA617E007EDBCAF57790EB5A3DCE9D31C7424DF4F08CB710BE7261F39766' -and
                           $RdxLibraryPatch -match '4289EC0F6D923EC9337A5DBE57F8D720BCC4946601B7D8393F9E2878B16F5C7D' -and
                           $RdxLibraryPatch -match '(?s)rdx_patch_scan_succeeded.*?syscfg_write\(i16 zeroext 159.*?rdx_patch_vm_written' -and
                           $RdxLibraryPatch -match '(?s)define zeroext i8 @rdx_uxfile_is_scan_active.*?@g_sync_state.*?@g_dat_need_upgrade_rebuild.*?rdx_patch_scan_active_u8' -and
                           $RdxRecord -match '(?s)static u8 rdx_record_uxfile_is_busy.*?rdx_uxfile_sync_is_in_progress.*?rdx_uxfile_is_scan_active.*?rdx_uxfile_is_formatting' -and
                           $RdxRecord -match '(?s)r_info->cmd == \(RECORD_STATE_START \+ 0x30\).*?uxfile_busy.*?g_record_cmd_delay_timer = sys_timeout_add' -and
                           $RdxRecord -match '(?s)r_info->cmd == \(RECORD_STATE_STOP \+ 0x30\).*?g_record_cmd_delay_timer.*?pending start cancelled by stop' -and
                           $RdxRecord -match '(?s)#if !TCFG_SD_ALWAY_ONLINE_ENABLE\s*int err = dev_manager_add\("sd0"\);.*?#endif' -and
                           $RdxApp -match '(?s)route == RECORD_MODE_ONLINE.*?else if \(rdx_uxfile_sync_is_in_progress\(\) \|\| rdx_uxfile_is_scan_active\(\) \|\|.*?rdx_uxfile_is_formatting\(\)\) return -1;'
Assert-Contract 'RDX_UXFILE_STARTUP_RECOVERY' $uxfileStartupRecoveryOk `
    'the hash-pinned library must serialize recording against the worker-owned management index recovery'

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
$recordProcessBranches = [regex]::Matches($RdxRecord, '(?s)static void rdx_record_process_impl\(void\)\s*\{.*?(?=\r?\n\})')
$recordToneOrderingOk = $recordProcessBranches.Count -eq 2 -and
    $RdxRecord -match '(?s)void rdx_record_process\(void\)\s*\{\s*if \(rdx_adv_policy_business_enter\(\)\) return;\s*rdx_record_process_impl\(\);\s*rdx_adv_policy_business_exit\(\);'
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
     $recordToneValid -match 'rdx_record_start_tone_admitted\(\)' -and
     $recordToneWait -match '(?s)record_status.run != RECORD_STATE_START.*?rdx_record_start_tone_cancel' -and
     $recordUi -notmatch 'tone_.*(?:post|play)\(') `
    'both recording branches must wait for natural tone completion and reject stale, cancelled, or power-off starts'

$recordTask = Get-SourceSlice $RdxRecord 'static void rdx_record_task(' 'int rdx_record_task_create('
$recordStopTone = Get-SourceSlice $RdxRecord `
    'static int rdx_record_stop_tone_callback(' 'static void rdx_record_stop_tone_play('
Assert-Contract 'RDX_RECORD_STOP_TONE_AFTER_CAPTURE_CLOSE' `
    ((Test-TokensInOrder $recordTask @('rdx_record_worker_close();', 'rdx_record_stop_session_cleanup();', 'rdx_record_stop_tone_post(tone_epoch);')) -and
     $recordStopTone -match 'STREAM_EVENT_INIT' -and
     $recordStopTone -match 'record_start_tone_epoch' -and
     $recordStopTone -match 'record_status.run != RECORD_STATE_STOP') `
    'the stop prompt must follow closure of both capture paths and be rejected if a new recording has started'

$recordingEncoderPathOk = $Config -notmatch 'TCFG_STENC_OPUS_ENABLE' -and
                          $SdkUsedList -match '(?s)#if TCFG_STENC_OPUS_ENABLE\s+opus_stenc_plug\s+#endif' -and
                          $EffectDev2 -match 'get_opus_stenc_ops\s*\(\s*\)'
Assert-Contract 'RDX_RECORDING_USES_EFFECT_DEV2_ENCODER' $recordingEncoderPathOk `
    'RDX recording must use effect_dev2; the native legacy registration stays behind its disabled gate'

$usbProfileOk = $Config -match '(?m)^\s*#define\s+TCFG_T2620_PC_STORAGE_ENABLE\s+0\s*$' -and
                $Config -match '(?s)#if\s+TCFG_T2620_PC_STORAGE_ENABLE.*?#if\s+!TCFG_SD0_ENABLE.*?#if\s+!TCFG_USB_SLAVE_MSD_ENABLE.*?#define\s+TCFG_APP_PC_EN\s+1' -and
                $SdkConfigH -match '(?m)^\s*#define\s+TCFG_CHARGE_POWERON_ENABLE\s+0\b' -and
                $SdkConfigH -match '(?m)^\s*#define\s+TCFG_USB_SLAVE_MSD_ENABLE\s+1\b' -and
                $Config -match '(?s)#if\s+TCFG_SD0_ENABLE\s+#undef\s+TCFG_SD_ALWAY_ONLINE_ENABLE\s*#define\s+TCFG_SD_ALWAY_ONLINE_ENABLE\s+1' -and
                $BoardConfig -notmatch '(?m)^\s*#\s*(?:define|undef)\s+TCFG_SD_ALWAY_ONLINE_ENABLE\b'
Assert-Contract 'USB_MSC_PRODUCT_PROFILE' $usbProfileOk `
    'USB export defaults off; optional export is MSC-only and fixed SD registration is independent'

Assert-Contract 'USB_EXPORT_AND_CHARGING_ARE_INDEPENDENT' (
    $Config -match '(?m)^\s*#define\s+TCFG_T2620_CHARGE_COEXIST_ENABLE\s+1\s*$' -and
    $Config -match '(?s)#else\s*(?:/\*.*?\*/\s*)?#undef TCFG_APP_PC_EN\s+#define TCFG_APP_PC_EN\s+0\s+#endif' -and
    $Config -match '#define TCFG_PC_ENABLE\s+TCFG_APP_PC_EN' -and
    $Dip -match '(?s)int rdx_dip_switch_pc_allowed.*?#if TCFG_T2620_PC_STORAGE_ENABLE.*?#else\s+return false;' -and
    $StorageLifecycle -notmatch 'TCFG_T2620_PC_STORAGE_ENABLE' -and
    $Dip -match 'rdx_storage_lifecycle_service\(on, vbus\)' -and
    $Makefile -match 'rdx_storage_lifecycle\.c'
) 'Disabling export must close PC admission without removing charging, SD or native shutdown'

$bootPreserve = Get-SourceSlice $DevManager `
    '#if (TCFG_SD0_ENABLE && TCFG_T2620_STORAGE_PRESERVE_ON_BOOT)' `
    '#elif (TCFG_SD0_ENABLE && TCFG_SD0_FORMAT_ON_BOOT)'
$formatSafetyOk = $Config -match '(?m)^\s*#define\s+TCFG_T2620_STORAGE_PRESERVE_ON_BOOT\s+1\s*$' -and
                  $bootPreserve -notmatch 'f_format\(|syscfg_write\(|dev->fmnt\s*=' -and
                  $DevManager -match 'dev->valid = \(dev->fmnt \? 1 : 0\)'
Assert-Contract 'STORAGE_BOOT_PRESERVES_DATA_ON_MOUNT_FAILURE' $formatSafetyOk `
    'T2620 must bypass all automatic/forced boot formatting, preserve mount failure and never infer blank media from VM'

$entryGatesOk = $Pc -match '(?s)static int pc_mode_try_enter.*?rdx_dip_switch_pc_allowed\(\).*?rdx_pc_storage_is_busy\(\)' -and
                $Dip -match '(?s)int rdx_dip_switch_pc_allowed.*?!get_power_on_status\(\).*?usb_otg_online\(0\) == SLAVE_MODE.*?rdx_dip_switch_cold_service\(\)' -and
                $RdxApp -match '(?s)u8 rdx_pc_storage_is_busy.*?rdx_app_storage_activity_is_busy\s*\(\s*"PC-STORAGE"\s*,\s*0\s*,\s*0\s*\)' -and
                $RdxApp -match '(?s)static u8 rdx_app_storage_activity_check.*?RECORD_STATE_STOP.*?rdx_record_process_is_busy_check.*?rdx_is_file_transfer_active.*?rdx_is_file_sync_busy'
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
Assert-Contract 'POWER_RESET_EVIDENCE_IS_NOT_SUPPLY_READY' `
    ($AppMain -match '(?s)BOOT-POWER.*?is_reset_source\(P33_VDDIO_LVD_RST\).*?is_reset_source\(P33_VDDIO_POR_RST\).*?is_reset_source\(P33_SOFT_RST\)' -and
     $StorageLifecycle -match 'storage reconciliation complete; enabling business, supply stability unverified' -and
     $Charge -match '(?s)if \(app_var.goto_poweroff_flag\) \{\s*log_info\("\[CHARGE\] controlled reset: USB inserted during pending poweroff"\);\s*cpu_reset\(\);') `
    'Keep historical reset evidence separate from supply readiness and identify the actual charge-triggered reset branch'
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
    $Dip -match '(?s)on != s_last_on \|\| usb != s_last_usb \|\| vbus != s_last_vbus.*?s_attempts = 0;.*?s_retry_wait = 0;' -and
    $Dip -match '(?s)rdx_dip_switch_pc_allowed\(\).*?target = APP_MODE_PC;.*?if \(s_attempts >= DIP_SERVICE_MAX_ATTEMPTS\) return 0;' -and
    $Dip -match '(?s)if \(target >= 0\).*?\+\+s_attempts;.*?s_retry_due = now \+ DIP_SERVICE_RETRY_MS;.*?app_send_message\(APP_MSG_GOTO_MODE, target\)' -and
    $AppMain -match '(?s)if \(err != 0\).*?#if TCFG_DIP_SWITCH_POWER_ENABLE.*?next_mode->name == APP_MODE_PC \|\| app_in_mode\(APP_MODE_PC\).*?next_mode->name == APP_MODE_BT && app_in_mode\(APP_MODE_IDLE\).*?rdx_dip_switch_wake\(\);.*?return NULL;.*?#endif.*?sys_timeout_add'
) 'Rejected or failed PC entry must be reconsidered with bounded requests; native timers must not replay stale PC targets'

Assert-Contract 'PC_GENERIC_FALLBACK_IS_PRESERVED' (
    $AppDefault -match '(?s)else if \(ret == 2\).*?#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE.*?IDLE_MODE_CHARGE.*?#else\s+app_send_message\(APP_MSG_GOTO_NEXT_MODE' -and
    $PcDevice -match '(?s)#if TCFG_T2620_PC_STORAGE_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE\s+if \(!rdx_dip_switch_pc_allowed\(\)\)'
) 'Product USB policy must be shared and generic USB removal fallback preserved'

$chargePrepare = Get-SourceSlice $RdxCharge 'void rdx_app_charge_prepare(void)' 'int rdx_app_battery_msg_handler(int *msg)'
$productPrepare = Get-SourceSlice $chargePrepare '#if TCFG_T2620_CHARGE_COEXIST_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE' '#else'
$chargeEvents = Get-SourceSlice $RdxCharge 'int rdx_app_battery_msg_handler(int *msg)' 'APP_MSG_PROB_HANDLER(rdx_app_battery_msg_entry)'
$productChargeEvents = Get-SourceSlice $chargeEvents `
    '#if TCFG_T2620_CHARGE_COEXIST_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE' '#elif TCFG_DIP_SWITCH_POWER_ENABLE'
Assert-Contract 'ON_USB_PRESERVES_RECORDING_AND_STORAGE' (
    $productPrepare -match 'return;' -and
    $productPrepare -notmatch 'rdx_record_process|rdx_ota_stop|rdx_app_wifi_handle|rdx_app_emmc_power' -and
    $productChargeEvents -match '(?s)case CHARGE_EVENT_LDO5V_IN:.*?case CHARGE_EVENT_LDO5V_KEEP:.*?rdx_app_charge_start\(\)' -and
    $productChargeEvents -notmatch 'rdx_record_process\(|rdx_app_emmc_poweroff\(|rdx_app_normal_poweroff\(' -and
    $RdxCharge -match '(?s)#if \(TCFG_CHARGE_POWERON_ENABLE == 0\) &&\s*\\\s*!\(TCFG_T2620_CHARGE_COEXIST_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE\).*?rdx_app_emmc_poweron\(0\)'
) 'Product USB insertion must maintain charge state without stopping recording, transfers or storage power'

$Poweroff = Read-RepoFile $RepoRoot 'SDK\apps\earphone\mode\bt\poweroff.c'
$autoShutdown = Get-SourceSlice $Poweroff 'void sys_auto_shut_down_enable(void)' 'static void sys_auto_shut_down_deal(void *priv)'
Assert-Contract 'CHARGING_WITHOUT_USB_SLAVE' (
    $Charge -match '(?s)case CHARGE_EVENT_LDO5V_KEEP:\s+rdx_dip_switch_wake\(\);\s+#if TCFG_T2620_CHARGE_COEXIST_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE.*?set_charge_poweron_en\(1\);\s+#endif\s+#if \(\(TCFG_OTG_MODE' -and
    $Charge -match '(?s)case CHARGE_EVENT_LDO5V_IN:\s+rdx_dip_switch_wake\(\);\s+#if TCFG_T2620_CHARGE_COEXIST_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE.*?set_charge_poweron_en\(1\);\s+#endif\s+#if \(\(TCFG_OTG_MODE' -and
    $RdxCharge -notmatch 'TCFG_T2620_PC_STORAGE_ENABLE' -and
    $RdxLedCtrl -notmatch 'TCFG_T2620_PC_STORAGE_ENABLE' -and
    $autoShutdown -notmatch 'TCFG_T2620_PC_STORAGE_ENABLE'
) 'Charge-only builds must keep CPU service and ON business alive without an OTG slave event'

Assert-Contract 'ON_USB_CHARGE_LIFECYCLE' (
    $productChargeEvents -match '(?s)rdx_app_charge_start\(\).*?rdx_app_business_started\(\).*?rdx_ble_server_auto_shut_down_enable\(0\)' -and
    $productChargeEvents -match '(?s)case CHARGE_EVENT_LDO5V_OFF:.*?rdx_app_charge_stop\(\).*?rdx_app_business_started\(\) && get_power_on_status\(\) &&.*?!app_var.goto_poweroff_flag.*?rdx_ble_server_auto_shut_down_enable\(1\)' -and
    $autoShutdown -match '(?s)#if TCFG_T2620_CHARGE_COEXIST_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE.*?if \(get_charge_online_flag\(\)\).*?sys_auto_shut_down_disable\(\);.*?return;' -and
    $RdxApp -match '(?s)#if \(TCFG_CHARGE_POWERON_ENABLE == 1\) \|\|\s*\\\s*\(TCFG_T2620_CHARGE_COEXIST_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE\).*?get_charge_online_flag\(\).*?rdx_app_charge_start\(\)' -and
    $Charge -match '(?s)case CHARGE_EVENT_LDO5V_OFF:.*?if \(!get_power_on_status\(\)\).*?APP_MSG_REQUEST_POWEROFF.*?charge_ldo5v_off_deal\(\)'
) 'USB power including FULL must inhibit inactivity shutdown; ON removal restores the normal business-aware timer policy'

$usbLedPolicy = Get-SourceSlice $RdxLedCtrl 'static bool _rdx_led_on_usb_charge(void)' 'static void _rdx_led_restore_system_state(void)'
$ledSceneEntry = Get-SourceSlice $RdxLedCtrl 'void rdx_led_ctrl_set_scene(' 'rdx_led_scene_e rdx_led_ctrl_get_scene('
$ledChargeEntry = Get-SourceSlice $RdxLedCtrl 'void rdx_led_ctrl_set_charge_state_by_battery(' 'void rdx_led_ctrl_restore_system_state('
Assert-Contract 'ON_CHARGE_LED_BUSINESS_PRIORITY' (
    $usbLedPolicy -match '(?s)#if TCFG_T2620_CHARGE_COEXIST_ENABLE && TCFG_DIP_SWITCH_POWER_ENABLE.*?get_power_on_status\(\) && rdx_app_business_started\(\).*?get_charge_online_flag\(\).*?RDX_CHARGE_OUT.*?!app_var.goto_poweroff_flag && !get_vbat_need_shutdown\(\)' -and
    (Test-TokensInOrder $usbLedPolicy @('RECORD_STATE_START', 'RDX_LED_SCENE_RECORD_START', 'RDX_LED_SCENE_DUT_ENTER', 'RDX_LED_SCENE_OTA_START', 'RDX_LED_SCENE_WIFI_START', 'RDX_LED_SCENE_CHARGE_FULL')) -and
    $ledSceneEntry -match '(?s)if \(on_usb_charge\).*?_rdx_led_resolve_on_usb_charge\(scene\).*?scene == g_current_scene && g_active_effect && !new_mark.*?return;' -and
    $ledChargeEntry -match '(?s)if \(_rdx_led_on_usb_charge\(\) \|\| rdx_app_get_dut_status\(\)\).*?rdx_led_ctrl_set_scene\(RDX_LED_SCENE_CHARGE_PLUG_IN\);.*?return;.*?_rdx_led_set_charge_effect_by_battery'
) 'Only normal ON USB charging arbitrates business above charge/FULL; charge updates share scene entry and preserve effect phase'
Assert-Contract 'DUT_CHARGE_EVENTS_PRESERVE_TEST_INDICATION' (
    $ledSceneEntry -match '(?s)if \(!transition && !app_var.goto_poweroff_flag && !get_vbat_need_shutdown\(\) &&\s+rdx_app_get_dut_status\(\) &&\s+\(scene == RDX_LED_SCENE_CHARGE_PLUG_IN \|\|\s+scene == RDX_LED_SCENE_CHARGE_FULL \|\|\s+scene == RDX_LED_SCENE_CHARGE_PLUG_OUT\)\) \{\s+return;' -and
    $RdxLedCtrl -match '(?s)void rdx_led_ctrl_restore_system_state\(void\).*?rdx_app_get_dut_status\(\) &&\s+g_current_scene == RDX_LED_SCENE_DUT_LED_TEST &&.*?!rdx_storage_lifecycle_transition_led\(\)\) \{\s+return;'
) 'Charge notifications preserve the current BLE/CDC factory test, including its off phase; shutdown and storage transition retain priority'
Assert-Contract 'ON_CHARGE_LED_RECOVERS_FROM_LIVE_STATE' (
    $usbLedPolicy -match 'static bool _rdx_led_key_feedback_should_yield\(void\)' -and
    $RdxLedCtrl -match 'if \(_rdx_led_is_key_feedback_scene\(scene\) && _rdx_led_key_feedback_should_yield\(\)\)' -and
    $usbLedPolicy -match 'requested == RDX_LED_SCENE_RECORD_MARK' -and
    $RdxLedCtrl -match '(?s)void rdx_led_ctrl_update\(void\).*?_rdx_led_on_usb_charge\(\).*?_rdx_led_resolve_on_usb_charge\(RDX_LED_SCENE_OFF\) != g_current_scene.*?rdx_led_ctrl_set_scene\(RDX_LED_SCENE_OFF\)' -and
    $ledSceneEntry -match '!on_usb_charge && g_current_scene == RDX_LED_SCENE_LOW_BATTERY'
) 'The existing LED tick reconciles silent OTA exits and expired marks without restarting unchanged effects or retaining a stale warning lock'

$StoragePatch = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\patch_librdxApp_storage.ps1'
$StorageIr = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_storage_patch.ll'
$DipSwitch = $StorageLifecycle
$RecoveryPatch = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\patch_librdxApp_recovery.ps1'
Assert-Contract 'NATIVE_OFF_NOT_STORAGE_FENCED' (
    $DipSwitch -notmatch 'USB_SWITCH_DRAIN|USB_SWITCH_FAILED|rdx_record_usb_quiesce_request|rdx_uxfile_fence_request' -and
    $Poweroff -notmatch 'rdx_storage_lifecycle_shutdown_deferred\(' -and
    $Poweroff -notmatch 'rdx_dip_switch_shutdown_begin\(' -and
    $Dip -match '(?s)rdx_dip_switch_shutdown_begin\(\);.*?app_send_message\(APP_MSG_REQUEST_POWEROFF' -and
    $Poweroff -match 'app_send_message\(APP_MSG_RECORD_OFF, 0\)' -and
    $Dip -match 'usr_timer_add\(NULL, rdx_dip_switch_guard, 100, 0\)' -and
    $Dip -match '\(u32\)\(now - s_shutdown_since\) >= 3000' -and
    $Dip -match 'rdx_dip_switch_cold_service\(\) && !app_var.goto_poweroff_flag'
) 'OFF must retain native cleanup and an independent deadline, without storage/record/BLE save vetoes'

$dipGuard = Get-SourceSlice $Dip 'static void rdx_dip_switch_guard(' 'void rdx_dip_switch_shutdown_begin('
$dipP33 = Get-SourceSlice $Dip 'void rdx_dip_switch_p33_irq(' 'void rdx_dip_switch_init('
Assert-Contract 'DIP_EVENT_SCHEDULING_AND_RECOVERY' (
    $Dip -notmatch 'sys_timer_add\(' -and
    $Dip -match 'usr_timer_add\(NULL, rdx_dip_switch_recovery_tick, 100, 0\)' -and
    $Dip -match 's_work_failed = !s_work_timer' -and
    $Dip -match '\(u32\)priv != s_work_serial' -and
    $dipGuard -match '\(u32\)\(now - s_sample_since\) >= 100' -and
    ($dipGuard + $dipP33) -notmatch 'os_taskq_|app_send_message|rdx_storage_lifecycle_service|usb_factory_service' -and
    $UsbTask -match '(?s)case USBSTACK_OTG_MSG:\s+rdx_dip_switch_wake\(\)' -and
    $Charge -match '(?s)case CHARGE_EVENT_LDO5V_OFF:\s+rdx_dip_switch_wake\(\)' -and
    $PcDevice -match 'FACTORY_CDC_MAX_ATTEMPTS 3' -and
    $PcDevice -match 'factory_cdc_retry_at = sys_timer_get_ms\(\) \+ 300' -and
    $PcDevice -match '(?s)factory_usb_state = FACTORY_USB_CDC;\s+rdx_dip_switch_wake\(\)' -and
    $PcDevice -match '(?s)factory_usb_state = FACTORY_USB_NONE;\s+rdx_dip_switch_wake\(\)'
) 'DIP must have no idle system poll; preserve isolated protection, event producers, failed-post recovery and bounded USB retries'
Assert-Contract 'WORKER_OWNS_INDEX_BEFORE_CACHE' (
    $RdxApp -notmatch 'if \(!rdx_record_format_boot\(\)\)' -and
    $RecoveryPatch -match 'call i32 @rdx_record_format_boot' -and
    $RecoveryPatch -match 'call i32 @rdx_uxfile_dat_init_impl' -and
    $RecoveryPatch -match 'rdx_record_format_service_status' -and
    $RecoveryPatch -match 'rdx_uxfile_delete_checked' -and
    $RdxApp -match 'rdx_app_file_delete_worker' -and
    $RdxApp -match 'rdx_uxfile_delete_submit'
) 'File task must recover before cache handoff; APP deletion acknowledgement follows actual worker completion'
Assert-Contract 'USB_PC_RETURN_GATES_BUSINESS_AND_INDEX' (
    (Test-TokensInOrder $Pc @('dev_manager_restore("sd0")', 'rdx_storage_lifecycle_pc_returned()')) -and
    $DipSwitch -match '(?s)rdx_storage_lifecycle_business_blocked\(void\).*?rdx_uxfile_pc_refresh_status\(\) != 0' -and
    $RdxServer -notmatch 'rdx_storage_lifecycle_business_blocked\('  -and
    ([regex]::Matches($RdxRecord, 'rdx_record_session_storage_blocked\(\) && record_status.run != RECORD_STATE_STOP').Count -eq 2) -and
    $RdxPlayback -match '(?s)bool rdx_playback_can_start\(void\).*?rdx_storage_lifecycle_business_blocked' -and
    $StoragePatch -match 'rdx_storage_boot_refresh_post' -and
    $StorageIr -match '(?s)rdx_storage_boot_reconcile.*?rdx_uxfile_sync_files_with_dat.*?store volatile i32 %state, i32\* @rdx_storage_refresh'
) 'PC return must stop USB/remount before reconciliation; storage may gate files but never BLE/HID availability'
Assert-Contract 'UXFILE_PATCH_QUIET_AND_ERROR_BOUNDARY' (
    $Makefile -match '(?m)^\$\(OUT_ELF\): apps/common/third_party_profile/rdx_protocol/librdxApp_patched\.a Makefile \| pre_build' -and
    $RdxLibraryPatch -match 'Update-RdxStorageIr' -and
    $StorageIr -match '(?s)rdx_storage_fwrite.*?mul i32 %size, %count.*?icmp eq i32 %written, %expected' -and
    (Test-TokensInOrder $StorageIr @('define internal void @rdx_storage_fence_arrive', 'rdx_uxfile_process_delete_queue', 'rdx_uxfile_close_read_file_handle', 'rdx_uxfile_flush_cache', 'rdx_storage_f_flush_wbuf', 'store volatile i32 %quiet', 'store volatile i32 %ticket, i32* @rdx_storage_fence_done'))
) 'The pinned patch must retain I/O errors, stop library producers, and publish the matching completion only after flush and worker freeze'

$BindingVm = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_vm.c'
$BindingDut = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_dut.c'
$BindingRecorder = Read-RepoFile $RepoRoot 'SDK\audio\interface\recoder\translation_ear_recoder.c'
$BindingInit = Get-SourceSlice $BindingVm 'void rdx_vm_init(void)' 'u8 rdx_vm_get_bound_status(void)'
$BindingRead = Get-SourceSlice $BindingVm 'u8 rdx_vm_get_bound_status(void)' 'int rdx_vm_set_bound_status('
Assert-Contract 'RECORD_BINDING_FAIL_CLOSED_VM' (
    $BindingInit -match '(?s)bound_state = 0;.*?syscfg_read\(VM_RDX_NOTTA_BOUND_STATUS.*?== sizeof\(bound\).*?bound == RDX_BOUND_STATE_BOUND' -and
    $BindingRead -notmatch 'syscfg_read' -and
    $RdxRecord -match 'return rdx_record_binding_token_capture\(\) != 0;' -and
    $BindingVm -match 'bound_token = d \? bound_epoch : 0;'
) 'Ordinary recording must require a validated persistent product binding, with a cached fail-closed runtime gate independent of BLE pairing'

$BindingCommand = Get-SourceSlice $RdxApp 'static void rdx_app_record_cmd_on_app_core(' 'typedef struct {'
$BindingDevice = Get-SourceSlice $RdxApp 'static int rdx_app_device_record_set(u8 scene, u8 run, u8 route, u8 kind)' 'void rdx_app_device_record_handle('
Assert-Contract 'RECORD_BINDING_ENTRY_AND_ASYNC_GATES' (
    (Test-TokensInOrder $BindingCommand @('rdx_ble_session_rdx_token_resolve', 'info.cmd != (RECORD_STATE_STOP + 0x30)', 'rdx_record_binding_token_is_current(request->binding_token)', 'rdx_record_cmd_handle_from_rdx')) -and
    $BindingDevice -match 'run != RECORD_STATE_START \|\| !rdx_record_binding_allowed\(\)' -and
    $RdxApp -match 'key_record_pressed && !rdx_record_binding_allowed\(\)' -and
    $RdxApp -match '(?s)case APP_MSG_RECORD_SWITCH:.*?!rdx_record_binding_allowed\(\).*?key_press_record_ready_flag = 0;' -and
    $RdxRecord -match 'rdx_record_binding_token_is_current\(pending_binding_token\)' -and
    $RdxRecord -match 'rdx_record_session_token_is_current\(tone_binding_token\)' -and
    $RdxRecord -match 'sys_timeout_add\(\(void \*\)rdx_record_binding_token_capture\(\), rdx_record_restart_if_bound, 1000\)'
) 'Keys and App commands must be gated before side effects; STOP remains available and delayed starts cannot survive unbind/rebind'

Assert-Contract 'RECORD_BINDING_LOW_LEVEL_COVERAGE' (
    $BindingRecorder -match '#include "app_config.h"' -and
    ([regex]::Matches($RdxRecord, 'static void rdx_record_process_impl\(void\)\s*\{\s*if \(!rdx_record_session_allowed\(\)').Count -eq 2) -and
    ([regex]::Matches($RdxRecord, 'int rdx_record_run_init\(void\)\s*\{\s*if \(!rdx_record_session_allowed\(\)').Count -eq 2) -and
    ($BindingDut.Contains('rdx_record_dut_authorize_usb(dut_owner.generation)') -and $BindingDut.Contains('!rdx_record_dut_authorize()')) -and
    ([regex]::Matches($BindingRecorder, 'int translation_ear_recoder_open(?:_all)?_impl\([^\n]+\)\s*\{\s*#if[^\n]+\s*if \(!rdx_record_session_allowed\(\)').Count -eq 2) -and
    $RdxRecord -match 'rdx_record_session_token_is_current\(\(u32\)msg\[3\]\)'
) 'Both recorder variants and queued worker starts must enforce recording authorization; DUT uses an explicit session grant'

Assert-Contract 'RECORD_DUT_ENTRY_GATES' (
    $BindingDut -match '(?s)void rdx_dut_test_session_revoke\(void\)\s*\{\s*rdx_record_dut_ble_revoke\(\)' -and
    $RdxRecord -match '(?s)int rdx_record_dut_stop_request\(u32 ticket\)\s*\{\s*rdx_record_dut_authorization_revoke\(\)' -and
    ([regex]::Matches($RdxRecord, 'int rdx_record_run_data_handle\([^\n]+\)\s*\{\s*if \(!rdx_record_session_allowed\(\)').Count -eq 2) -and
    $BindingRecorder -match 'translation_ear_recoder_open_all_bound\(ch_mode, rdx_record_binding_token_capture\(\)\)'
) 'DUT revoke/stop and both data paths must retain authorization gates; SDK opens remain bound-only'

$BindingFence = Get-SourceSlice $RdxRecord 'if (msg[1] == RDX_RECORD_BIND_FENCE)' 'if (msg[1] == RDX_RECORD_USB_FENCE)'
$BindingRevoke = Get-SourceSlice $RdxRecord 'static void rdx_record_binding_revoke_on_app_core(void)' 'int rdx_record_usb_quiesce_request('
Assert-Contract 'RECORD_UNBIND_DRAINS_BEFORE_REARM' (
    (Test-TokensInOrder $BindingFence @('int saved = rdx_record_worker_close()', 'rdx_record_format_status() < 0', 'rdx_record_stop_session_cleanup()', 'rdx_record_set_process_state_ready()', 'if (saved < 0)', 'record_binding_revoke_pending = 0')) -and
    (Test-TokensInOrder $BindingRevoke @('rdx_app_record_binding_reset()', 'sys_timeout_del(g_record_cmd_delay_timer)', '++record_start_tone_epoch', 'record_status.rerun = false', 'RDX_RECORD_BIND_FENCE')) -and
    $BindingVm -match '(?s)\+\+bound_epoch;.*?rdx_bound_info.bound_state = d;.*?rdx_record_binding_revoke\(\)'
) 'Unbinding must cancel producers and close/save through the recording FIFO before rearming; storage failure keeps the gate closed'

Assert-Contract 'RECORD_BINDING_SERIALIZED_AUDIO_START' (
    (Test-TokensInOrder $BindingVm @('int rdx_vm_set_bound_status(', 'rdx_vm_bound_transition_lock()', 'syscfg_write(VM_RDX_NOTTA_BOUND_STATUS', 'bound_token = d ? bound_epoch : 0;', 'rdx_record_binding_revoke()', 'rdx_vm_bound_transition_unlock()')) -and
    (Test-TokensInOrder $BindingRecorder @('int translation_ear_recoder_open_all_bound(', 'rdx_record_audio_start_enter(token)', 'translation_ear_recoder_open_all_impl(ch_mode)', 'rdx_record_audio_start_exit(ret)')) -and
    $RdxRecord -match 'translation_ear_recoder_open_all_bound\(msg\[2\], \(u32\)msg\[3\]\)' -and
    (Test-TokensInOrder $RdxRecord @('int rdx_record_audio_start_enter(', 'rdx_vm_bound_transition_lock()', 'rdx_record_session_token_is_current(token)', 'void rdx_record_audio_start_exit(', 'rdx_record_binding_revoke()', 'rdx_vm_bound_transition_unlock()')) -and
    $BindingRecorder -notmatch 'rdx_vm_'
) 'Binding publication and actual audio open must serialize, preserving the original queued token'
Assert-Contract 'RECORD_START_FAILURE_ROLLBACK' (
    (Test-TokensInOrder $BindingRecorder @('if (!esco_player_runing())', 'ret = translation_ear_recoder_open_impl(MIC', 'goto fail;', 'ret = translation_ear_recoder_open_impl(DAC', 'fail:', 'translation_ear_recoder_close_all()', 'return ret;')) -and
    $RdxRecord -match '(?s)void rdx_record_audio_start_exit\(int result\).*?if \(result\).*?rdx_record_binding_revoke\(\);' -and
    $RdxRecord -match '\+\+record_binding_generation' -and
    $RdxRecord -match 'record_binding_cleanup_error = -1;' -and
    $RdxRecord -match 'record_binding_cleanup_error = -2;'
) 'Partial opens must unwind, call recording may skip MIC, and worker failure must drain before rearm'

$RecordSink = Read-RepoFile $RepoRoot 'SDK/audio/framework/nodes/sink_dev1_node.c'
$RdxStorage = Read-RepoFile $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record_storage.c'
Assert-Contract 'RECORD_SESSION_INIT_SINGLE_OWNER' (
    ([regex]::Matches($RdxRecord, 'rdx_record_run_init\(\);').Count -eq 0) -and
    ([regex]::Matches($RecordSink, 'rdx_record_run_init\(\);').Count -eq 1) -and
    (Test-TokensInOrder $RecordSink @('static int sink_dev1_init(', 'int err = rdx_record_run_init();', 'if (err)', 'hdl->live = 0;', 'rdx_record_run_init_failed(err);', 'return err;', 'hdl->live = 1;', 'rdx_record_mic_gain_check()')) -and
    (Test-TokensInOrder $RecordSink @('static int sink_dev1_ioc_start', 'if (hdl->started)', 'sink_dev1_init(hdl);', 'hdl->started = 1;', 'return 0;', 'static int sink_dev1_ioc_stop')) -and
    $RecordSink -notmatch 'int err = sink_dev1_init\(hdl\);' -and
    (Test-TokensInOrder $RecordSink @('static void sink_dev1_run(', 'if (!hdl->live)', 'return;', 'rdx_record_run_data_handle(data, data_len);')) -and
    $RecordSink -match '(?s)case NODE_IOC_START:\s*return sink_dev1_ioc_start\(hdl\);' -and
    (Test-TokensInOrder $RdxRecord @('void rdx_record_run_init_failed(int err)', 'if (!rdx_record_session_allowed())', 'return;', 'if (err == -2)', 'translation_ear_recoder_storage_full();', 'translation_ear_recoder_storage_fault();')) -and
    ([regex]::Matches($RdxRecord, 'int space = rdx_record_storage_check_start\(\);\s*if \(space\) return space;').Count -eq 2)
) 'Only the sink initializes each recording session; failed init never fails jlstream START, drops frames and latches an orderly STOP'

$AdmitRecordCmd = Get-SourceSlice $RdxRecord 'static void rdx_record_cmd_handle_internal(' "if(r_info->cmd == '0'){" -Last
$AdmitRecordGate = Get-SourceSlice $RdxRecord 'rdx_record_local_storage_admit("RECORD_COMMAND")' "if(r_info->cmd == '0'){"
$AdmitKey = Get-SourceSlice $RdxApp 'static int rdx_app_device_record_set(u8 scene, u8 run, u8 route, u8 kind)' 'void rdx_app_device_record_handle('
$AdmitDut = Get-SourceSlice $BindingDut 'static const char *dut_accept(' 'dut_action_since = '
Assert-Contract 'RECORD_STORAGE_ADMISSION_BEFORE_MUTATION' (
    $RdxStorage -match '(?s)int rdx_record_storage_admit\(void\)\s*\{\s*if \(!rs\.created \|\| rs\.active \|\| rs\.error \|\| rdx_record_format_status\(\) < 0\)\s*return -1;\s*return rdx_record_storage_check_start\(\);' -and
    (Test-TokensInOrder $RdxRecord @('int rdx_record_local_storage_admit(const char *who)', 'rdx_record_storage_admit()')) -and
    (Test-TokensInOrder $AdmitRecordCmd @('rdx_record_process_is_busy_check()', 'rdx_record_start_uses_local_storage(token)', 'RECORD_STATE_RESUME + 0x30', 'record_status.run == RECORD_STATE_PAUSE', '!rdx_record_stream_only_session_is_active()', 'rdx_record_local_storage_admit("RECORD_COMMAND")', 'rdx_protocol_record_state_indicate();', 'return;')) -and
    $AdmitRecordGate -notmatch 'record_session_generation|rdx_record_online_session_bind\(|rdx_record_stream_only_start_consume\(|record_key_context' -and
    (Test-TokensInOrder $AdmitKey @('if (scene != RECORD_SCENE_CHAT', '!(stream_only && route == RECORD_MODE_ONLINE)', 'rdx_record_local_storage_admit("KEY_START")) return -1;', 'if (fixed_route)', 'rdx_record_key_start_prepare(', 'rdx_playback_stop()', 'rdx_app_record_state_upload_timer_start()', 'rdx_app_record_trigger_post(')) -and
    (Test-TokensInOrder $AdmitDut @('if (dut_next_id == 0xffffffffu) return "STATE";', 'request->command == DUT_CMD_REC || request->command == DUT_CMD_REC_CALL', 'request->value && target != active', 'rdx_record_local_storage_admit("DUT_START")) return "STATE";', 'local_irq_disable();', 'request->id = ++dut_next_id;', 'dut_last = *request;', 'dut_last_state = 1;'))
) 'Local-storage START/RESUME, key starts and DUT recordings must pass storage admission before any session, key, playback or DUT ID/LAST mutation'

Assert-Contract 'RECORD_SESSION_LIFETIME' (
    $RdxRecord -match 'if \(\+\+record_session_generation == 0\)' -and
    ($RdxRecord -match 'record_initialized_generation = record_session_generation;') -and
    ($RdxRecord -match 'record_initialized_generation != record_session_generation') -and
    $RdxRecord -match 'record_initialized_generation == record_session_generation' -and
    (Test-TokensInOrder $RecordSink @('static int sink_dev1_ioc_stop', 'if (hdl->started)', 'hdl->started = 0;', 'return sink_dev1_exit(hdl);')) -and
    $RecordSink -match 'sink_dev1_ioc_stop\(\(struct sink_dev1_hdl \*\)node->private_data\);'
) 'Session identity survives audio restarts; every started node exits once, including failed-init and release paths'
Write-Host 'T2620 product contracts passed.'
