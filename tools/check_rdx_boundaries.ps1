param(
    [string]$RepoRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = Split-Path -Parent $PSScriptRoot
}

$RepoRoot = (Resolve-Path $RepoRoot).Path
$RdxRoot = Join-Path $RepoRoot "SDK/apps/common/third_party_profile/rdx_protocol"

$script:Errors = @()
$script:Warnings = @()

function Convert-ToRelativePath {
    param([string]$Path)
    $full = (Resolve-Path $Path).Path
    if ($full.StartsWith($RepoRoot)) {
        return $full.Substring($RepoRoot.Length + 1).Replace("\", "/")
    }
    return $full.Replace("\", "/")
}

function Add-Failure {
    param([string]$Message)
    $script:Errors += $Message
    Write-Host "[FAIL] $Message"
}

function Add-Warning {
    param([string]$Message)
    $script:Warnings += $Message
    Write-Host "[WARN] $Message"
}

function Add-Pass {
    param([string]$Message)
    Write-Host "[PASS] $Message"
}

function Get-SourceFiles {
    param(
        [string]$Root,
        [string[]]$Include = @("*.c", "*.h")
    )

    $files = @()
    foreach ($pattern in $Include) {
        $files += Get-ChildItem -Path $Root -Recurse -File -Filter $pattern
    }
    return $files
}

function Find-Pattern {
    param(
        [System.IO.FileInfo[]]$Files,
        [string]$Pattern,
        [scriptblock]$Exclude = { param($match) return $false }
    )

    $hits = @()
    foreach ($file in $Files) {
        $matches = Select-String -Path $file.FullName -Pattern $Pattern
        foreach ($match in $matches) {
            if (-not (& $Exclude $match)) {
                $hits += $match
            }
        }
    }
    return $hits
}

function Assert-NoHits {
    param(
        [string]$Name,
        [object[]]$Hits
    )

    if ($Hits.Count -eq 0) {
        Add-Pass $Name
        return
    }

    Add-Failure "$Name ($($Hits.Count) hit(s))"
    foreach ($hit in $Hits) {
        $path = Convert-ToRelativePath $hit.Path
        Write-Host "       ${path}:$($hit.LineNumber): $($hit.Line.Trim())"
    }
}

if (-not (Test-Path $RdxRoot)) {
    Add-Failure "RDX root not found: $RdxRoot"
    exit 1
}

Write-Host "RDX boundary checks"
Write-Host "Repo: $RepoRoot"
Write-Host ""

$appFile = Join-Path $RdxRoot "rdx_app.c"
$appRegisterHits = @()
if (Test-Path $appFile) {
    $appRegisterHits = Select-String -Path $appFile -Pattern "rdx_cmd_register\s*\("
}
Assert-NoHits "rdx_app.c has no rdx_cmd_register()" $appRegisterHits

$serviceHeaderRoot = Join-Path $RdxRoot "service"
$serviceHeaders = @()
if (Test-Path $serviceHeaderRoot) {
    $serviceHeaders = Get-ChildItem -Path $serviceHeaderRoot -File -Filter "*.h"
}
$servicePrimitiveHits = Find-Pattern -Files $serviceHeaders `
    -Pattern "\b(OS_MUTEX|OS_SEM|cbuffer_t|IO_PORT[A-Z0-9_]*|APP_MSG_[A-Z0-9_]+)\b"
Assert-NoHits "service public headers do not expose JL/board primitives" $servicePrimitiveHits

$portHeaderRoot = Join-Path $RdxRoot "port/jl/include"
$portHeaders = @()
if (Test-Path $portHeaderRoot) {
    $portHeaders = Get-ChildItem -Path $portHeaderRoot -File -Filter "*.h" |
        Where-Object {
            $_.Name -notin @("rdx_jl_osal.h", "rdx_jl_gpio.h")
        }
}
$portPrimitiveHits = Find-Pattern -Files $portHeaders `
    -Pattern "\b(OS_MUTEX|OS_SEM|cbuffer_t|IO_PORT[A-Z0-9_]*|APP_MSG_[A-Z0-9_]+)\b"
Assert-NoHits "port public headers avoid unintended platform primitive leaks" $portPrimitiveHits

$allRdxFiles = Get-SourceFiles -Root $RdxRoot
$businessFiles = $allRdxFiles | Where-Object {
    $rel = Convert-ToRelativePath $_.FullName
    return ($rel -notmatch "/board/" -and
            $rel -notmatch "/port/" -and
            $rel -notmatch "/rdx_led_ctrl\.(c|h)$" -and
            $rel -notmatch "/rdx_led_cfg\.h$")
}

$privateIncludeHits = Find-Pattern -Files $businessFiles `
    -Pattern '^\s*#\s*include\s*[<"](?:board/[^/]+/|port/jl/(?:jl7018|jl7018_shadow)/)'
Assert-NoHits "business code does not include board/chip private headers" $privateIncludeHits

$xxpHits = Find-Pattern -Files $allRdxFiles `
    -Pattern "\bxxp_esp32[A-Za-z0-9_]*\b" `
    -Exclude {
        param($match)
        $rel = Convert-ToRelativePath $match.Path
        return ($rel -match "/port/jl/jl7018/rdx_ops\.c$")
    }
Assert-NoHits "xxp_esp32 symbols are isolated to port ops" $xxpHits

$ledHits = Find-Pattern -Files $businessFiles `
    -Pattern "\brdx_led_ctrl_(set_scene|restore_system_state|set_charge_state_by_battery)\s*\(" `
    -Exclude {
        param($match)
        $rel = Convert-ToRelativePath $match.Path
        return ($rel -match "/service/rdx_default_hooks\.c$")
    }
Assert-NoHits "business code uses rdx_hook_led_* instead of rdx_led_ctrl_*" $ledHits

$directBoardIncludeHits = Find-Pattern -Files $businessFiles `
    -Pattern '^\s*#\s*include\s*["<]board/'
Assert-NoHits "business code has no direct board include path" $directBoardIncludeHits

# P0: business .c must not ref old board macros
$oldMacroTargets = $allRdxFiles | Where-Object {
    $n = $_.Name
    return ($n -in @("rdx_app.c", "rdx_charge.c", "rdx_dut.c") -or
            ($_.FullName -match "[/\\]service[/\\]" -and $n -like "*.c"))
}
$oldMacroHits = Find-Pattern -Files $oldMacroTargets `
    -Pattern "\b(WIFI_POWER_PORT_IO|VDD_POWER_PORT_IO|LED_PT0807_DATA_PORT_IO)\b"
Assert-NoHits "business .c has no old board macros (WIFI/VDD/LED)" $oldMacroHits

# P0: business .c must not directly operate IO_PORTC_* pins
$ioPortcHits = Find-Pattern -Files $oldMacroTargets `
    -Pattern "\bIO_PORTC_0[1-5]\b"
Assert-NoHits "business .c has no bare IO_PORTC_* operations" $ioPortcHits

# P1: rdx_spi.c must not reference ESP8684 old macros
$spiFile = Join-Path $RdxRoot "rdx_spi.c"
$spi8684Hits = @()
if (Test-Path $spiFile) {
    $spi8684Hits = Select-String -Path $spiFile -Pattern "\bESP8684_[A-Z]" -CaseSensitive
}
Assert-NoHits "rdx_spi.c has no ESP8684_ hardcoded macros" $spi8684Hits

# P1: rdx_spi.c must not have CHIP_TYPE conditionals
$spiChipTypeHits = @()
if (Test-Path $spiFile) {
    $spiChipTypeHits = Select-String -Path $spiFile -Pattern "CHIP_TYPE"
}
Assert-NoHits "rdx_spi.c has no CHIP_TYPE conditionals" $spiChipTypeHits

# P2: service .c must not call os_taskq_post_type directly
$serviceCFiles = $allRdxFiles | Where-Object {
    $_.FullName -match "[/\\]service[/\\]" -and $_.Name -like "*.c"
}
$taskqHits = Find-Pattern -Files $serviceCFiles `
    -Pattern "\bos_taskq_post_type\b"
Assert-NoHits "service .c has no direct os_taskq_post_type calls" $taskqHits

# P2: service .c must not call sys_timeout_* directly
$sysTimeoutHits = Find-Pattern -Files $serviceCFiles `
    -Pattern "\bsys_timeout_(add|del)\b"
Assert-NoHits "service .c has no direct sys_timeout_add/del calls" $sysTimeoutHits

# P2: service .c must not call sys_timer_re_run directly
$sysTimerHits = Find-Pattern -Files $serviceCFiles `
    -Pattern "\bsys_timer_re_run\b"
Assert-NoHits "service .c has no direct sys_timer_re_run calls" $sysTimerHits

# P3: RDX code must not hand-roll Q_CALLBACK msg[] arrays — use callback0/1/2
$rdxBusinessCFiles = $allRdxFiles | Where-Object {
    $_.FullName -match "[/\\]third_party_profile[/\\]rdx_protocol[/\\]" `
        -and $_.Name -like "*.c" `
        -and $_.FullName -notmatch "[/\\]port[/\\]"
}

$directQCallbackHits = Find-Pattern -Files $rdxBusinessCFiles `
    -Pattern "\bos_taskq_post_type\s*\([^)]*Q_CALLBACK" `
    -Exclude {
        param($match)
        $line = $match.Line.Trim()
        # skip commented-out code
        return ($line -match '^\s*//')
    }
Assert-NoHits "business .c has no hand-rolled os_taskq_post_type(Q_CALLBACK...)" $directQCallbackHits

$msgArrayQCallbackHits = Find-Pattern -Files $rdxBusinessCFiles `
    -Pattern "\brdx_os_task_post_msg_array\s*\([^)]*Q_CALLBACK"
Assert-NoHits "business .c has no rdx_os_task_post_msg_array(Q_CALLBACK...)" $msgArrayQCallbackHits

# P4: rdx_dut.c must have no direct sys_timeout_add/del and no Q_CALLBACK
$dutFile = $rdxBusinessCFiles | Where-Object { $_.Name -eq "rdx_dut.c" }
if ($dutFile) {
    $dutSysTimeoutHits = Find-Pattern -Files @($dutFile) `
        -Pattern "\bsys_timeout_(add|del)\b"
    Assert-NoHits "rdx_dut.c has no direct sys_timeout_add/del calls" $dutSysTimeoutHits

    $dutSysTimerHits = Find-Pattern -Files @($dutFile) `
        -Pattern "\bsys_timer_re_run\b"
    Assert-NoHits "rdx_dut.c has no direct sys_timer_re_run calls" $dutSysTimerHits

    $dutQCallbackHits = Find-Pattern -Files @($dutFile) `
        -Pattern "\bQ_CALLBACK\b"
    Assert-NoHits "rdx_dut.c has no hand-rolled Q_CALLBACK arrays" $dutQCallbackHits
}

# P3a: 4 specified business .c files must not call sys_timeout_* / sys_timer_re_run directly
$p3aTargetNames = @("rdx_app.c", "rdx_charge.c", "rdx_ble_server.c", "rdx_battery.c")
$p3aTargetFiles = $allRdxFiles | Where-Object {
    $_.Name -in $p3aTargetNames -and
    $_.FullName -match "[/\\]rdx_protocol[/\\]"
}

$p3aSysTimeoutHits = Find-Pattern -Files $p3aTargetFiles `
    -Pattern "\bsys_timeout_(add|del)\b"
Assert-NoHits "P3a files have no direct sys_timeout_add/del calls" $p3aSysTimeoutHits

$p3aSysTimerHits = Find-Pattern -Files $p3aTargetFiles `
    -Pattern "\bsys_timer_re_run\b"
Assert-NoHits "P3a files have no direct sys_timer_re_run calls" $p3aSysTimerHits

# P5: rdx_record.c / rdx_ota.c must have no direct sys_timeout_* / sys_timer_* / sys_timeout_add_2_task
$p5TargetNames = @("rdx_record.c", "rdx_ota.c")
$p5TargetFiles = $allRdxFiles | Where-Object {
    $_.Name -in $p5TargetNames -and
    $_.FullName -match "[/\\]rdx_protocol[/\\]"
}

$p5SysTimeoutHits = Find-Pattern -Files $p5TargetFiles `
    -Pattern "\bsys_timeout_(add|del)\b"
Assert-NoHits "P5 files have no direct sys_timeout_add/del calls" $p5SysTimeoutHits

$p5SysTimerHits = Find-Pattern -Files $p5TargetFiles `
    -Pattern "\bsys_timer_(add|del|re_run)\b"
Assert-NoHits "P5 files have no direct sys_timer_add/del/re_run calls" $p5SysTimerHits

$p5Add2TaskHits = Find-Pattern -Files $p5TargetFiles `
    -Pattern "\bsys_timeout_add_2_task\b"
Assert-NoHits "P5 files have no direct sys_timeout_add_2_task calls" $p5Add2TaskHits

# P6: remaining business periodic timers must go through rdx_os_timer_periodic_*
$p6TargetNames = @("rdx_charge.c", "rdx_dut.c", "rdx_led_ctrl.c", "rdx_rtc.c")
$p6TargetFiles = $allRdxFiles | Where-Object {
    $_.Name -in $p6TargetNames -and
    $_.FullName -match "[/\\]rdx_protocol[/\\]"
}

$p6SysTimerHits = Find-Pattern -Files $p6TargetFiles `
    -Pattern "\bsys_timer_(add|del|modify)\b"
Assert-NoHits "P6 files have no direct sys_timer_add/del/modify calls" $p6SysTimerHits

# P7: old BLE server must not directly access record/protocol state internals
$bleServerFile = Join-Path $RdxRoot "rdx_ble_server.c"
$p7BleServerStateHits = @()
if (Test-Path $bleServerFile) {
    $p7BleServerStateHits = Select-String -Path $bleServerFile `
        -Pattern 'RecordStatus|rdx_record_get_status\s*\(|rdx_record_process\s*\(|rdx_record_mode_active_check\s*\(|rdx_protocol_record_(state|trigger)_indicate\s*\(|#\s*include\s*["<]rdx_record\.h[">]'
}
Assert-NoHits "P7 rdx_ble_server.c has no direct record/protocol state access" $p7BleServerStateHits

if ($script:Warnings.Count -gt 0) {
    Write-Host ""
    Write-Host "Warnings: $($script:Warnings.Count)"
}

Write-Host ""
if ($script:Errors.Count -gt 0) {
    Write-Host "RDX boundary checks failed: $($script:Errors.Count) error(s)"
    exit 1
}

Write-Host "RDX boundary checks passed."
exit 0
