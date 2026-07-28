#Requires -Version 5.1

[CmdletBinding()]
param(
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot 'bin'
}
$Source = Join-Path $PSScriptRoot 'codex_micro_hid.cpp'
$VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

if (-not (Test-Path -LiteralPath $VsWhere)) {
    throw 'Visual Studio Build Tools not found. Install the Desktop development with C++ workload.'
}

$InstallPath = & $VsWhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $InstallPath) {
    throw 'MSVC C++ tools not found. Install the Desktop development with C++ workload.'
}

$DevShell = Join-Path $InstallPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Import-Module $DevShell
Enter-VsDevShell -VsInstallPath $InstallPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$Output = Join-Path $OutputDirectory 'codex_micro_hid.exe'
& cl.exe /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE `
    $Source /Fe:$Output setupapi.lib hid.lib
if ($LASTEXITCODE -ne 0) {
    throw "cl.exe failed with exit code $LASTEXITCODE"
}
Write-Host $Output
