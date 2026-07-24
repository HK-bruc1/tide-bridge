#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'host_test_lib.ps1')

$RepoRoot = Get-HostTestRepoRoot
$Config = Read-RepoFile $RepoRoot 'SDK\apps\earphone\include\t2620_project_config.h'
$SdkConfigH = Read-RepoFile $RepoRoot 'SDK\apps\earphone\board\br28\sdk_config.h'
$SdkConfigC = Read-RepoFile $RepoRoot 'SDK\apps\earphone\board\br28\sdk_config.c'
$AppConfig = Read-RepoFile $RepoRoot 'SDK\apps\earphone\include\app_config.h'
$IoKey = Read-RepoFile $RepoRoot 'SDK\apps\earphone\board\iokey_config.c'
$Pc = Read-RepoFile $RepoRoot 'SDK\apps\earphone\mode\pc\pc.c'
$UsbTask = Read-RepoFile $RepoRoot 'SDK\apps\common\device\usb\usb_task.c'
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

$usbProfileOk = $Config -match '(?m)^\s*#define\s+TCFG_APP_PC_EN\s+1\s*$' -and
                $Config -match '(?m)^\s*#define\s+TCFG_CHARGE_POWERON_ENABLE\s+0\s*$' -and
                $Config -match '(?m)^\s*#define\s+TCFG_USB_SLAVE_MSD_ENABLE\s+1\s*$' -and
                $Config -match '(?m)^\s*#define\s+TCFG_SD_ALWAY_ONLINE_ENABLE\s+1\s*$'
foreach ($disabledClass in @(
    'TCFG_USB_SLAVE_HID_ENABLE',
    'TCFG_USB_SLAVE_AUDIO_SPK_ENABLE',
    'TCFG_USB_SLAVE_AUDIO_MIC_ENABLE',
    'TCFG_USB_SLAVE_CDC_ENABLE',
    'TCFG_USB_CUSTOM_HID_ENABLE',
    'TCFG_USB_SLAVE_MTP_ENABLE',
    'TCFG_USB_SLAVE_MIDI_ENABLE',
    'TCFG_USB_SLAVE_PRINTER_ENABLE'
)) {
    $usbProfileOk = $usbProfileOk -and
        $Config -match "(?m)^\s*#define\s+$disabledClass\s+0\s*$"
}
Assert-Contract 'USB_MSC_PRODUCT_PROFILE' $usbProfileOk `
    'the product USB profile must expose only MSC over the soldered always-online storage'

$formatSafetyOk = $Config -match '(?m)^\s*#define\s+TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE\s+0\s*$' -and
                  $DevManager -match '#if\s*\(TCFG_SD0_ENABLE\s*&&\s*TCFG_SD0_FORMAT_ON_BOOT\s*&&\s*TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE\)'
Assert-Contract 'STORAGE_FORMAT_IS_FAIL_CLOSED' $formatSafetyOk `
    'a mount failure must not format storage unless every explicit safety switch is enabled'

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
