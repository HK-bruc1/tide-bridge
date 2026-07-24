#Requires -Version 5.1
<#
.SYNOPSIS
    Runs all host-side software validation tests.

.DESCRIPTION
    Provides one stable entry point for the small set of core source contracts.
    Each test is launched in a child PowerShell process because the individual
    scripts use exit codes for CI compatibility.
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
        Name = 'RDX unified advertising contract'
        Path = Join-Path $PSScriptRoot 'test_rdx_unified_adv_phase1.ps1'
    },
    [PSCustomObject]@{
        Name = 'RDX unified session contract'
        Path = Join-Path $PSScriptRoot 'test_rdx_unified_session_phase2b.ps1'
    },
    [PSCustomObject]@{
        Name = 'RDX dual-link Phase 3 online keymap contract'
        Path = Join-Path $PSScriptRoot 'test_rdx_dual_link_phase3_keymap.ps1'
    },
    [PSCustomObject]@{
        Name = 'RDX dual-link Phase 3 reconnect lifecycle contract'
        Path = Join-Path $PSScriptRoot 'test_rdx_dual_link_phase3_reconnect_lifecycle.ps1'
    },
    [PSCustomObject]@{
        Name = 'RDX local playback configuration'
        Path = Join-Path $PSScriptRoot 'test_rdx_local_playback_config.ps1'
    },
    [PSCustomObject]@{
        Name = 'RDX playback navigation'
        Path = Join-Path $PSScriptRoot 'test_rdx_playback_navigation.ps1'
    }
)

$failed = 0

Write-Host 'Host Core Contracts'
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
