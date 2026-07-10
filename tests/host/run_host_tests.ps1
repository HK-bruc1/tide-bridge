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
    throw 'powershell.exe not found in PATH'
}

$Tests = @(
    [PSCustomObject]@{
        Name = 'T2620 config overlay'
        Path = Join-Path $PSScriptRoot 'test_t2620_config_overlay.ps1'
    },
    [PSCustomObject]@{
        Name = 'HOGP profile contract'
        Path = Join-Path $PSScriptRoot 'test_hogp_profile_contract.ps1'
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
