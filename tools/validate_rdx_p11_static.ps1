param(
    [ValidateSet('Baseline', 'Progress', 'Final')]
    [string]$OwnershipMode = 'Baseline',
    [string]$OwnershipBaselineRef = '128eb8d',
    [string]$AbiBaselineRef = '5d0284f17006bd333de992ed22d5a1c7484c37a0',
    [string]$NmTool = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$powerShell = (Get-Process -Id $PID).Path

function Invoke-StaticCheck {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$Arguments = @()
    )

    Write-Host ''
    Write-Host "=== $Name ==="
    & $powerShell -NoProfile -File $ScriptPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Write-Host 'RDX P11 source-only static validation'
Write-Host "Host OS: $([System.Runtime.InteropServices.RuntimeInformation]::OSDescription)"
Write-Host 'This entry does not compile firmware, run the JL configuration matrix, inspect a linker map, or perform board tests.'

Invoke-StaticCheck `
    -Name 'RDX boundary checks' `
    -ScriptPath (Join-Path $repo 'tools/check_rdx_boundaries.ps1')

Invoke-StaticCheck `
    -Name 'P10 storage contract' `
    -ScriptPath (Join-Path $repo 'tests/host/test_rdx_storage_contract.ps1')

Invoke-StaticCheck `
    -Name 'Record recovery contract' `
    -ScriptPath (Join-Path $repo 'tests/host/test_rdx_record_recovery.ps1')

$abiArguments = @('-BaselineRef', $AbiBaselineRef)
if (-not [string]::IsNullOrWhiteSpace($NmTool)) {
    $abiArguments += @('-NmTool', $NmTool)
}
Invoke-StaticCheck `
    -Name 'Static-library ABI and archive dependencies' `
    -ScriptPath (Join-Path $repo 'tests/host/test_rdx_static_library_abi.ps1') `
    -Arguments $abiArguments

Invoke-StaticCheck `
    -Name "P11 ownership ($OwnershipMode)" `
    -ScriptPath (Join-Path $repo 'tests/host/test_rdx_p11_ownership.ps1') `
    -Arguments @('-Mode', $OwnershipMode, '-BaselineRef', $OwnershipBaselineRef)

Invoke-StaticCheck `
    -Name "P11 exact allowlist and public-header boundaries ($OwnershipMode)" `
    -ScriptPath (Join-Path $repo 'tests/host/test_rdx_p11_boundaries.ps1') `
    -Arguments @('-Mode', $OwnershipMode)

Invoke-StaticCheck `
    -Name 'P11 Host trace scaffold static contract' `
    -ScriptPath (Join-Path $repo 'tests/host/test_rdx_p11_trace_scaffold.ps1')

Write-Host ''
Write-Host 'All RDX P11 source-only static checks passed.'
Write-Host 'This is not P11.0 stage acceptance. Run validate_rdx_p11_readiness.ps1 only after linking P10 production evidence and executable caller traces.'
