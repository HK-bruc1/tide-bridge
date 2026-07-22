#Requires -Version 5.1
<#
.SYNOPSIS
    Runs all host-side software validation tests.

.DESCRIPTION
    Provides one stable entry point for VS Code tasks and developers. Each test
    is launched in a child PowerShell process so scripts that call exit cannot
    terminate this runner before the remaining tests run.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$PowerShellExe = (Get-Command powershell.exe -ErrorAction SilentlyContinue).Source
if (-not $PowerShellExe) {
    $PowerShellExe = (Get-Command pwsh -ErrorAction SilentlyContinue).Source
}
if (-not $PowerShellExe) {
    throw 'Neither powershell.exe nor pwsh was found in PATH'
}

$Tests = @(
    [PSCustomObject]@{
        Name = 'T2620 config overlay'
        Path = Join-Path $PSScriptRoot 'test_t2620_config_overlay.ps1'
    },
    [PSCustomObject]@{
        Name = 'T2620 PC storage ownership'
        Path = Join-Path $PSScriptRoot 'test_pc_mode_storage_contract.ps1'
    },
    [PSCustomObject]@{
        Name = 'HOGP profile contract'
        Path = Join-Path $PSScriptRoot 'test_hogp_profile_contract.ps1'
    },
    [PSCustomObject]@{
        Name = 'RDX unified advertising production contract'
        Path = Join-Path $PSScriptRoot 'test_rdx_unified_adv_phase1.ps1'
    },
    [PSCustomObject]@{
        Name = 'RDX unified session production contract'
        Path = Join-Path $PSScriptRoot 'test_rdx_unified_session_phase2b.ps1'
    },
    [PSCustomObject]@{
        Name = 'RDX dual-link Phase 0A contract'
        Path = Join-Path $PSScriptRoot 'test_rdx_dual_link_phase0a.ps1'
    },
    [PSCustomObject]@{
        Name = 'HOGP keymap architecture'
        Path = Join-Path $PSScriptRoot 'test_hogp_keymap_architecture.ps1'
    },
    [PSCustomObject]@{
        Name = 'HOGP keymap behavior'
        Path = Join-Path $PSScriptRoot 'test_hogp_keymap_behavior.ps1'
    },
    [PSCustomObject]@{
        Name = 'RDX local playback configuration'
        Path = Join-Path $PSScriptRoot 'test_rdx_local_playback_config.ps1'
    }
    [PSCustomObject]@{
        Name = 'RDX playback navigation'
        Path = Join-Path $PSScriptRoot 'test_rdx_playback_navigation.ps1'
    }
)

$failed = 0

Write-Host 'Host Software Tests'
Write-Host '==================='

foreach ($test in $Tests) {
    Write-Host ''
    Write-Host "Running: $($test.Name)"
    Write-Host "Script : $($test.Path)"

    & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File $test.Path
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0) {
        Write-Host "PASS: $($test.Name)"
    } else {
        Write-Host "FAIL: $($test.Name) exited with code $exitCode"
        $failed++
    }
}

Write-Host ''
Write-Host '-------------------'
if ($failed -eq 0) {
    Write-Host "All $($Tests.Count) host software tests passed."
    exit 0
}

Write-Host "$failed of $($Tests.Count) host software tests failed."
exit 1
