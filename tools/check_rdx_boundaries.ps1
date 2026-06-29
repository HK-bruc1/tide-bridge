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
