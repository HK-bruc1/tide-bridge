param(
    [ValidateSet('Baseline', 'Progress', 'Final')]
    [string]$OwnershipMode = 'Baseline'
)

Set-StrictMode -Version 2.0

& (Join-Path $PSScriptRoot 'verify_rdx.ps1') -Mode P11Readiness `
    -OwnershipMode $OwnershipMode
exit $LASTEXITCODE
