#Requires -Version 5.1
<#
.SYNOPSIS
    Validates the T2620 USB Mass Storage ownership contract.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$Failures = [System.Collections.Generic.List[string]]::new()

function Read-RepoFile {
    param([Parameter(Mandatory)][string]$RelativePath)
    Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $RepoRoot $RelativePath)
}

function Assert-Pattern {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string]$Pattern
    )

    if ([regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        Write-Host "PASS: $Name"
    } else {
        Write-Host "FAIL: $Name"
        $script:Failures.Add($Name)
    }
}

function Assert-NotPattern {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string]$Pattern
    )

    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
        Write-Host "PASS: $Name"
    } else {
        Write-Host "FAIL: $Name"
        $script:Failures.Add($Name)
    }
}

function Assert-Order {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Text,
        [Parameter(Mandatory)][string]$First,
        [Parameter(Mandatory)][string]$Second
    )

    $FirstIndex = $Text.IndexOf($First, [System.StringComparison]::Ordinal)
    $SecondIndex = $Text.IndexOf($Second, [System.StringComparison]::Ordinal)
    if ($FirstIndex -ge 0 -and $SecondIndex -gt $FirstIndex) {
        Write-Host "PASS: $Name"
    } else {
        Write-Host "FAIL: $Name"
        $script:Failures.Add($Name)
    }
}

$Config = Read-RepoFile 'SDK\apps\earphone\include\t2620_project_config.h'
$Pc = Read-RepoFile 'SDK\apps\earphone\mode\pc\pc.c'
$UsbTask = Read-RepoFile 'SDK\apps\common\device\usb\usb_task.c'
$AppDefault = Read-RepoFile 'SDK\apps\earphone\mode\common\app_default_msg_handler.c'
$Poweroff = Read-RepoFile 'SDK\apps\earphone\mode\bt\poweroff.c'
$UsbDevice = Read-RepoFile 'SDK\apps\common\device\usb\device\task_pc.c'
$Charge = Read-RepoFile 'SDK\apps\earphone\battery\charge.c'
$DevManager = Read-RepoFile 'SDK\apps\common\dev_manager\dev_manager.c'
$DevManagerHeader = Read-RepoFile 'SDK\apps\common\dev_manager\dev_manager.h'
$RdxApp = Read-RepoFile 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_app.c'
$RdxCharge = Read-RepoFile 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_charge.c'
$Dip = Read-RepoFile 'SDK\apps\common\third_party_profile\rdx_protocol\rdx_dip_switch.c'

Assert-Pattern 'PC_MODE_ENABLED' $Config '^\s*#define\s+TCFG_APP_PC_EN\s+1\s*$'
Assert-Pattern 'CHARGE_POWERON_DISABLED' $Config '^\s*#define\s+TCFG_CHARGE_POWERON_ENABLE\s+0\s*$'
Assert-Pattern 'MSC_ENABLED' $Config '^\s*#define\s+TCFG_USB_SLAVE_MSD_ENABLE\s+1\s*$'
Assert-Pattern 'SOLDERED_SD_NAND_ALWAYS_ONLINE' $Config '^\s*#define\s+TCFG_SD_ALWAY_ONLINE_ENABLE\s+1\s*$'

@(
    'TCFG_USB_SLAVE_HID_ENABLE',
    'TCFG_USB_SLAVE_AUDIO_SPK_ENABLE',
    'TCFG_USB_SLAVE_AUDIO_MIC_ENABLE',
    'TCFG_USB_SLAVE_CDC_ENABLE',
    'TCFG_USB_CUSTOM_HID_ENABLE',
    'TCFG_USB_SLAVE_MTP_ENABLE',
    'TCFG_USB_SLAVE_MIDI_ENABLE',
    'TCFG_USB_SLAVE_PRINTER_ENABLE'
) | ForEach-Object {
    Assert-Pattern "MSC_ONLY_$($_)" $Config "^\s*#define\s+$($_)\s+0\s*$"
}

Assert-Pattern 'AUTO_FORMAT_ON_MOUNT_FAIL_DISABLED' $Config '^\s*#define\s+TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE\s+0\s*$'
Assert-Pattern 'FORMAT_PATH_REQUIRES_SAFETY_SWITCH' $DevManager '#if\s*\(TCFG_SD0_ENABLE\s*&&\s*TCFG_SD0_FORMAT_ON_BOOT\s*&&\s*TCFG_SD0_AUTO_FORMAT_ON_MOUNT_FAIL_ENABLE\)'

Assert-Pattern 'DIP_GATES_OTG_EVENT' $UsbDevice '(?s)DEVICE_EVENT_IN.*?get_power_on_status\(\).*?charge only.*?switch_app_case\s*=\s*1'
Assert-Pattern 'DIP_GATES_PC_TRY_ENTER' $Pc '(?s)static int pc_mode_try_enter.*?get_power_on_status\(\).*?rdx_pc_storage_is_busy\(\)'
Assert-Pattern 'DIP_ON_RETRIES_CONNECTED_USB' $Dip '(?s)rdx_dip_switch_request_pc_if_usb_online.*?usb_otg_online\(0\).*?APP_MSG_GOTO_MODE.*?APP_MODE_PC'
Assert-Pattern 'LDO5V_KEEP_WAITS_FOR_OTG_ROLE' $Charge '(?s)case\s+CHARGE_EVENT_LDO5V_KEEP:.*?app_charge_wait_otg_role\("LDO5V_KEEP"\).*?ldo5v_keep_deal\(\)'
Assert-Pattern 'LDO5V_IN_WAITS_FOR_OTG_ROLE' $Charge '(?s)case\s+CHARGE_EVENT_LDO5V_IN:.*?app_charge_wait_otg_role\("LDO5V_IN"\).*?charge_ldo5v_in_deal\(\)'
Assert-Pattern 'USB_PC_POWERON_REQUIRES_DIP_ON' $Charge '(?s)static u8 app_charge_allow_usb_pc_poweron.*?get_power_on_status\(\).*?return false;.*?set_charge_poweron_en\(1\)'
Assert-Pattern 'DIP_ON_USB_REMOVAL_STAYS_POWERED' $RdxCharge '(?s)case\s+CHARGE_EVENT_LDO5V_OFF:.*?get_power_on_status\(\).*?USB removed with DIP ON: stay powered.*?break;.*?rdx_app_normal_poweroff\(\)'

Assert-Pattern 'RDX_BUSY_CHECKS_RECORD' $RdxApp '(?s)u8 rdx_pc_storage_is_busy.*?RECORD_STATE_STOP.*?rdx_record_process_is_busy_check'
Assert-Pattern 'RDX_BUSY_CHECKS_PLAYBACK' $RdxApp '(?s)rdx_pc_storage_is_busy.*?PB_STATE_UNREADY.*?PB_STATE_STOPPED'
Assert-Pattern 'RDX_BUSY_CHECKS_FILE_ACTIVITY' $RdxApp '(?s)rdx_pc_storage_is_busy.*?rdx_is_file_transfer_active.*?rdx_is_file_sync_busy.*?rdx_uxfile_is_scan_active.*?rdx_uxfile_is_formatting'

Assert-Pattern 'DEVICE_MANAGER_HAS_TAKEOVER_API' $DevManagerHeader '(?s)int dev_manager_takeover\(char \*logo\);.*?int dev_manager_restore\(char \*logo\);'
Assert-Pattern 'BLOCKED_STORAGE_CANNOT_REMOUNT' $DevManager '(?s)static int __dev_manager_mount.*?if \(dev->mount_blocked\).*?return -1;'
Assert-Pattern 'BACKGROUND_REMOUNT_SKIPS_HOST_STORAGE' $DevManager '(?s)void dev_manager_list_check_mount.*?if \(dev->mount_blocked\).*?continue;'
Assert-Pattern 'UNMOUNT_ACCEPTS_REGISTERED_UNMOUNTED_NODE' $DevManager '(?s)static int __dev_manager_unmount.*?list_for_each_entry\(item, &__this->list, entry\).*?if\(dev->fmnt\)'
Assert-Pattern 'ALWAYS_ONLINE_SD_STOPS_DETECT_TIMER' $DevManager '(?s)#if TCFG_SD_ALWAY_ONLINE_ENABLE.*?force_set_sd_online\("sd0"\).*?dev_manager_add\("sd0"\).*?sdx_dev_detect_timer_del\(\)'

Assert-Pattern 'PC_PREPARES_SD0_TAKEOVER' $Pc '(?s)static int pc_storage_prepare.*?rdx_pc_storage_is_busy\(\).*?dev_manager_takeover\("sd0"\).*?storage_state\s*=\s*PC_STORAGE_HOST_OWNED'
Assert-Pattern 'USB_START_REQUIRES_HOST_OWNERSHIP' $Pc '(?s)static int pc_task_start.*?storage_state\s*!=\s*PC_STORAGE_HOST_OWNED.*?USBSTACK_START'
Assert-Pattern 'USB_CONTROL_COMPLETION_IS_CORRELATED' $UsbTask '(?s)static OS_MUTEX msg_mutex.*?msg_wait_seq.*?usb_stack_message_complete.*?os_taskq_post_msg\(USB_TASK_NAME, 3, msg, arg, seq\)'
Assert-Pattern 'USB_CONTROL_TIMEOUT_IS_RETURNED' $UsbTask 'os_sem_pend\(&msg_sem, 200\) == OS_TIMEOUT'
Assert-Pattern 'USB_STOP_FAILURE_BLOCKS_RESTORE' $Pc '(?s)usb_message_to_stack\(USBSTACK_STOP, 0, 1\).*?if \(err\).*?return err;.*?return pc_storage_restore\(\);'
Assert-Pattern 'RESTORE_FAILURE_HAS_EXPLICIT_STATE' $Pc '(?s)PC_STORAGE_RESTORE_FAILED.*?storage_state\s*=\s*PC_STORAGE_RESTORE_FAILED.*?sd0 remains blocked'
Assert-Pattern 'RESTORE_MOUNTS_BEFORE_UNBLOCK' $DevManager '(?s)int dev_manager_restore.*?__dev_manager_restore_mount\(logo\).*?__dev_manager_set_mount_blocked\(logo, 0\)'
Assert-Pattern 'DIP_POWEROFF_IS_APP_REQUEST' $Dip 'app_send_message\(APP_MSG_REQUEST_POWEROFF, POWEROFF_NORMAL\)'
Assert-NotPattern 'DIP_DOES_NOT_BYPASS_MODE_CLEANUP' $Dip 'sys_enter_soft_poweroff\('
Assert-Pattern 'PC_HANDLES_POWEROFF_BEFORE_COMMON_HANDLER' $Pc '(?s)case APP_MSG_REQUEST_POWEROFF:.*?pc_task_stop\(\).*?app_default_msg_handler\(msg\)'
Assert-Pattern 'COMMON_HANDLER_EXECUTES_POWEROFF_REQUEST' $AppDefault '(?s)case APP_MSG_REQUEST_POWEROFF:.*?sys_enter_soft_poweroff'
Assert-Pattern 'DIP_OFF_OVERRIDES_USB_POWER_KEEP' $Poweroff 'otg_status == SLAVE_MODE && get_power_on_status\(\)'
Assert-Pattern 'HID_HELPERS_FOLLOW_HID_CLASS_SWITCH' $Pc '(?s)#if TCFG_USB_SLAVE_HID_ENABLE\s+static void pc_hid_hold_release.*?#endif\s+static void pc_app_msg_handler'
Assert-Order 'PC_INIT_TAKES_STORAGE_BEFORE_BECOMING_ACTIVE' $Pc 'if (pc_storage_prepare())' '__this->pc_is_active = 1'
Assert-Order 'USB_STOPS_BEFORE_SD0_RESTORE' $Pc 'usb_message_to_stack(USBSTACK_STOP, 0, 1);' 'pc_storage_restore();'
Assert-NotPattern 'PC_EXIT_NEVER_PAUSES_MSC' $Pc 'USBSTACK_PAUSE'
Assert-NotPattern 'PC_MODE_NEVER_DELETES_SD0_NODE' $Pc 'dev_manager_del\("sd0"\)'

if ($Failures.Count) {
    Write-Error "$($Failures.Count) PC storage contract check(s) failed: $($Failures -join ', ')"
    exit 1
}

Write-Output 'T2620 PC storage static contract checks passed.'
