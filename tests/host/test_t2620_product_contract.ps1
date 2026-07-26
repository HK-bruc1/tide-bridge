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
$Dip = Read-RepoFile $RepoRoot 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_dip_switch.c'
$AppDefault = Read-RepoFile $RepoRoot 'SDK\apps\earphone\mode\common\app_default_msg_handler.c'

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
    'TCFG_DEC_OGG_OPUS_ENABLE',
    'RDX_HOGP_KEY_ACTION_TEST_ENABLE'
)) {
    $configOwnershipOk = $configOwnershipOk -and
        $Config -notmatch "(?m)^\s*#\s*(?:define|undef)\s+$externallyOwnedMacro\b"
}
Assert-Contract 'PROJECT_CONFIG_DOES_NOT_REPEAT_EXISTING_DEFAULTS' $configOwnershipOk `
    'tool-owned and subsystem-default settings must not be repeated in the T2620 project config'

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
                $RdxApp -match '(?s)u8 rdx_pc_storage_is_busy.*?RECORD_STATE_STOP.*?rdx_record_process_is_busy_check.*?rdx_is_file_transfer_active.*?rdx_is_file_sync_busy'
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
