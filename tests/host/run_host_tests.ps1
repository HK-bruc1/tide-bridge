#Requires -Version 5.1

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$Tests = @(
    'test_t2620_product_contract.ps1',
    'test_hogp_profile_contract.ps1',
    'test_rdx_transport_contract.ps1',
    'test_rdx_lifecycle_contract.ps1',
    'test_rdx_keymap_contract.ps1'
)

$Failed = 0

foreach ($test in $Tests) {
    $path = Join-Path $PSScriptRoot $test
    try {
        & $path
    } catch {
        Write-Host "FAIL: ${test}: $($_.Exception.Message)"
        $Failed++
    }
}

if ($Failed -eq 0) {
    Write-Host "All $($Tests.Count) host core contracts passed."
    exit 0
}

Write-Host "$Failed of $($Tests.Count) host core contracts failed."
exit 1
