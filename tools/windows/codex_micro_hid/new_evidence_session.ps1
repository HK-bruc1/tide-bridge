#Requires -Version 5.1

[CmdletBinding()]
param(
    [ValidateSet('W0', 'C0', 'V1', 'C1')]
    [Parameter(Mandatory)]
    [string]$Variant,
    [string]$ChatGptBuild = 'unknown',
    [string]$Adapter = 'unknown'
)

$ErrorActionPreference = 'Stop'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Directory = Join-Path $PSScriptRoot "evidence\$Variant-$Stamp"
New-Item -ItemType Directory -Force -Path $Directory | Out-Null

$Os = Get-CimInstance Win32_OperatingSystem
$Driver = Get-PnpDevice -Class Bluetooth -PresentOnly -ErrorAction SilentlyContinue |
    Select-Object Status, FriendlyName, InstanceId
$Commit = (& git -C (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')) rev-parse HEAD).Trim()

[ordered]@{
    variant = $Variant
    timestamp = (Get-Date).ToString('o')
    windows = [ordered]@{
        caption = $Os.Caption
        version = $Os.Version
        build = $Os.BuildNumber
    }
    chatgpt_build = $ChatGptBuild
    bluetooth_adapter = $Adapter
    bluetooth_devices = @($Driver)
    firmware_commit = $Commit
    fresh_pairing = $false
    gate_w0 = 'unverified'
    result = 'pending'
} | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 `
    -LiteralPath (Join-Path $Directory 'matrix.json')

Get-PnpDevice -Class HIDClass -PresentOnly -ErrorAction SilentlyContinue |
    Format-List * | Out-File -Encoding UTF8 (Join-Path $Directory 'hidclass.txt')
pnputil /enum-devices /class HIDClass /properties |
    Out-File -Encoding UTF8 (Join-Path $Directory 'pnputil-hidclass.txt')

Write-Host $Directory
