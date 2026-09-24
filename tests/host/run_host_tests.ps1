#Requires -Version 5.1

[CmdletBinding()]
param(
    [ValidateSet('all', 'contracts', 'behavior')]
    [string]$Suite = 'all'
)

$ErrorActionPreference = 'Stop'

$Contracts = @(
    'test_t2620_product_contract.ps1',
    'test_hogp_profile_contract.ps1',
    'test_rdx_transport_contract.ps1',
    'test_rdx_lifecycle_contract.ps1',
    'test_rdx_keymap_contract.ps1'
)
$Behavior = @(
    'test_adv_policy.py',
    'test_factory_usb.py',
    'test_finalpack.py',
    'test_dut_keys_speaker.py',
    'test_record_storage.py',
    'test_meeting_mono.py',
    'test_record_format.py',
    'test_hogp_hold.py'
    'test_keymap_factory_reset.py'
)
$Tests = @()
if ($Suite -ne 'behavior') { $Tests += $Contracts }
if ($Suite -ne 'contracts') { $Tests += $Behavior }

$Failed = 0

foreach ($test in $Tests) {
    $path = Join-Path $PSScriptRoot $test
    try {
        if ($test.EndsWith('.py')) {
            & python -X utf8 $path
        } else {
            & powershell -NoProfile -ExecutionPolicy Bypass -File $path
        }
        if ($LASTEXITCODE -ne 0) { throw "exit code $LASTEXITCODE" }
    } catch {
        Write-Host "FAIL: ${test}: $($_.Exception.Message)"
        $Failed++
    }
}

if ($Failed -eq 0) {
    Write-Host "All $($Tests.Count) host suites passed."
    exit 0
}

Write-Host "$Failed of $($Tests.Count) host suites failed."
exit 1
