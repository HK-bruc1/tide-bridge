param(
    [ValidateSet('Baseline', 'Progress', 'Final')]
    [string]$OwnershipMode = 'Baseline'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$powerShell = (Get-Process -Id $PID).Path
$script:Failures = @()

function Invoke-Gate {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$Arguments = @()
    )

    Write-Host ''
    Write-Host "=== $Name ==="
    & $powerShell -NoProfile -File $ScriptPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        $script:Failures += $Name
        Write-Host "BLOCKED GATE: $Name"
    }
}

Invoke-Gate 'P11 source-only static validation' `
    (Join-Path $repo 'tools/validate_rdx_p11_static.ps1') `
    @('-OwnershipMode', $OwnershipMode)
Invoke-Gate 'P11 Windows/JL ABI linkage evidence' `
    (Join-Path $repo 'tests/host/test_rdx_p11_linkage_evidence.ps1')
Invoke-Gate 'P10 evidence linkage' `
    (Join-Path $repo 'tests/host/test_rdx_p10_evidence.ps1')
Invoke-Gate 'P11 caller-context and executable golden trace evidence' `
    (Join-Path $repo 'tests/host/test_rdx_p11_trace_evidence.ps1')

Write-Host ''
if ($script:Failures.Count -gt 0) {
    Write-Host "P11 caller migration and production acceptance are blocked by $($script:Failures.Count) gate(s):"
    foreach ($failure in $script:Failures) {
        Write-Host "- $failure"
    }
    Write-Host 'The zero-caller query slice remains limited to source and Host validation.'
    exit 1
}

Write-Host 'P11 caller migration and production acceptance readiness passed.'
