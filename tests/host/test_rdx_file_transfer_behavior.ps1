param(
    [string]$RepoRoot = '',
    [string]$Compiler = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $compilerCommand = Get-Command tcc -ErrorAction SilentlyContinue
    if (-not $compilerCommand) {
        throw 'Tiny C Compiler was not found. Install tcc or pass -Compiler explicitly.'
    }
    $Compiler = $compilerCommand.Source
} else {
    $Compiler = (Resolve-Path $Compiler).Path
}

$source = Join-Path $RepoRoot 'tests/host/test_rdx_file_transfer_behavior.c'
$stubRoot = Join-Path $RepoRoot 'tests/host/stubs'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Behavior harness source not found: $source"
}
if (-not (Test-Path -LiteralPath $stubRoot -PathType Container)) {
    throw "Behavior harness stub directory not found: $stubRoot"
}

Write-Host 'RDX file-transfer compat behavior'
Write-Host "Compiler: $Compiler"
& $Compiler -Wall -Werror "-I$stubRoot" -run $source
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    Write-Host "RDX file-transfer compat behavior failed (exit code $exitCode)"
    exit $exitCode
}

Write-Host 'RDX file-transfer compat behavior passed.'
exit 0
