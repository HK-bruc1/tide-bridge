param(
    [string]$Compiler = 'C:\JL\pi32\bin\clang.exe',
    [string]$MakeCommand = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    throw 'P10 production static validation must run in the Windows/JL environment.'
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($MakeCommand)) {
    $MakeCommand = Join-Path $repo 'SDK\tools\utils\make.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($MakeCommand)) {
    $MakeCommand = Join-Path $repo $MakeCommand
}

function Invoke-RequiredScript {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string[]]$Arguments = @()
    )

    Write-Host ""
    Write-Host "=== $Name ==="
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Invoke-RequiredScript `
    -Name 'RDX boundary checks' `
    -ScriptPath (Join-Path $repo 'tools\check_rdx_boundaries.ps1')

Invoke-RequiredScript `
    -Name 'P10 storage contract' `
    -ScriptPath (Join-Path $repo 'tests\host\test_rdx_storage_contract.ps1')

Invoke-RequiredScript `
    -Name 'Static-library ABI' `
    -ScriptPath (Join-Path $repo 'tests\host\test_rdx_static_library_abi.ps1') `
    -Arguments @('-BaselineRef', '5d0284f17006bd333de992ed22d5a1c7484c37a0')

Invoke-RequiredScript `
    -Name 'Record recovery contract' `
    -ScriptPath (Join-Path $repo 'tests\host\test_rdx_record_recovery.ps1')

Invoke-RequiredScript `
    -Name 'RDX configuration matrix' `
    -ScriptPath (Join-Path $repo 'tests\host\test_rdx_config_matrix.ps1') `
    -Arguments @('-Compiler', $Compiler, '-MakeCommand', $MakeCommand)

Write-Host ""
Write-Host 'All required RDX P10 production static checks passed.'
