param(
    [string]$RepoRoot = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

$powerShell = (Get-Process -Id $PID).Path
$arguments = @(
    '-NoProfile',
    '-File', (Join-Path $RepoRoot 'tests/host/test_rdx_architecture.ps1'),
    '-RepoRoot', $RepoRoot
)

Write-Host 'RDX repository verification'
Write-Host "Repo: $RepoRoot"
Write-Host ''

& $powerShell @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host ''
& $powerShell -NoProfile -File `
    (Join-Path $RepoRoot 'tests/host/test_rdx_public_contract.ps1') `
    -RepoRoot $RepoRoot
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host ''
Write-Host 'RDX repository verification passed.'
exit 0
