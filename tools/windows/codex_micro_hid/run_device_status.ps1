#Requires -Version 5.1

[CmdletBinding()]
param(
    [string]$PathFilter,
    [string]$EvidenceDirectory,
    [int]$TimeoutMs = 2000,
    [switch]$TestSetOutputReport
)

$ErrorActionPreference = 'Stop'
if (-not $EvidenceDirectory) {
    $EvidenceDirectory = Join-Path $PSScriptRoot 'evidence'
}
$Exe = Join-Path $PSScriptRoot 'bin\codex_micro_hid.exe'
if (-not (Test-Path -LiteralPath $Exe)) {
    & (Join-Path $PSScriptRoot 'build_hid_tool.ps1') | Out-Host
}

New-Item -ItemType Directory -Force -Path $EvidenceDirectory | Out-Null
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Trace = Join-Path $EvidenceDirectory "hid-$Stamp.json"
$Arguments = @('--timeout-ms', $TimeoutMs, '--trace', $Trace)
if ($PathFilter) { $Arguments += @('--path', $PathFilter) }
if ($TestSetOutputReport) { $Arguments += '--test-set-output-report' }

& $Exe @Arguments
if ($LASTEXITCODE -ne 0) {
    throw "Codex Micro HID round trip failed with exit code $LASTEXITCODE. Trace: $Trace"
}
Write-Host "PASS: $Trace"
