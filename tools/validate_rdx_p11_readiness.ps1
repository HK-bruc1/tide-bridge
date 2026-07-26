Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$powerShell = (Get-Process -Id $PID).Path
$script:Failures = @()

function Invoke-Gate([string]$Name, [string]$ScriptPath) {
    Write-Host ''
    Write-Host "=== $Name ==="
    & $powerShell -NoProfile -File $ScriptPath
    if ($LASTEXITCODE -ne 0) {
        $script:Failures += $Name
        Write-Host "BLOCKED GATE: $Name"
    }
}

Invoke-Gate 'P11 source-only static validation' `
    (Join-Path $repo 'tools/validate_rdx_p11_static.ps1')
Invoke-Gate 'P11 Windows/JL ABI linkage evidence' `
    (Join-Path $repo 'tests/host/test_rdx_p11_linkage_evidence.ps1')
Invoke-Gate 'P10 evidence linkage' `
    (Join-Path $repo 'tests/host/test_rdx_p10_evidence.ps1')
Invoke-Gate 'P11 caller-context and executable golden trace evidence' `
    (Join-Path $repo 'tests/host/test_rdx_p11_trace_evidence.ps1')

Write-Host ''
if ($script:Failures.Count -gt 0) {
    Write-Host "P11.0 stage acceptance is blocked by $($script:Failures.Count) gate(s):"
    foreach ($failure in $script:Failures) {
        Write-Host "- $failure"
    }
    Write-Host 'P11.1 remains unauthorized.'
    exit 1
}

Write-Host 'P11.0 stage acceptance passed; P11.1 may be considered for authorization.'
