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
$checks = @(
    @{
        Name = 'architecture constraints'
        RelativePath = 'tests/host/test_rdx_architecture.ps1'
    },
    @{
        Name = 'public compatibility contract'
        RelativePath = 'tests/host/test_rdx_public_contract.ps1'
    }
)

function Invoke-RepositoryCheck {
    param(
        [string]$Name,
        [string]$RelativePath
    )

    $scriptPath = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        throw "RDX verification script not found: $RelativePath"
    }

    Write-Host "Running: $Name"
    & $powerShell -NoProfile -File $scriptPath -RepoRoot $RepoRoot
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Write-Host ''
        Write-Host "RDX repository verification failed: $Name (exit code $exitCode)"
        exit $exitCode
    }
}

Write-Host 'RDX repository verification'
Write-Host "Repo: $RepoRoot"

foreach ($check in $checks) {
    Write-Host ''
    Invoke-RepositoryCheck $check.Name $check.RelativePath
}

Write-Host ''
Write-Host 'RDX repository verification passed.'
exit 0
