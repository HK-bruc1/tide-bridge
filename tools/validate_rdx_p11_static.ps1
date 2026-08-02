param(
    [ValidateSet('Baseline', 'Progress', 'Final')]
    [string]$OwnershipMode = 'Final',
    [string]$OwnershipBaselineRef = '128eb8d',
    [string]$AbiBaselineRef = '5d0284f17006bd333de992ed22d5a1c7484c37a0',
    [string]$NmTool = ''
)

Set-StrictMode -Version 2.0

& (Join-Path $PSScriptRoot 'verify_rdx.ps1') -Mode P11Static `
    -OwnershipMode $OwnershipMode `
    -OwnershipBaselineRef $OwnershipBaselineRef `
    -AbiBaselineRef $AbiBaselineRef `
    -NmTool $NmTool
exit $LASTEXITCODE
