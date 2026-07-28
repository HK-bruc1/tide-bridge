#Requires -Version 5.1

[CmdletBinding()]
param(
    [string]$Name = 'Codex Micro',
    [int]$Seconds = 15,
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
if (-not $OutputPath) {
    $OutputPath = Join-Path $PSScriptRoot 'evidence\ble-advertisements.json'
}
$WatcherType = [Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementWatcher, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$Watcher = [Activator]::CreateInstance($WatcherType)
$Watcher.ScanningMode = [Windows.Devices.Bluetooth.Advertisement.BluetoothLEScanningMode]::Active
$SourceIdentifier = "CodexMicroBle-$PID"
$Subscription = Register-ObjectEvent -InputObject $Watcher -EventName Received `
    -SourceIdentifier $SourceIdentifier -Action {
    $Event = $EventArgs
    if ($using:Name -and $Event.Advertisement.LocalName -ne $using:Name) { return }
    $Sections = foreach ($Section in $Event.Advertisement.DataSections) {
        $Reader = [Windows.Storage.Streams.DataReader]::FromBuffer($Section.Data)
        $Bytes = [byte[]]::new($Reader.UnconsumedBufferLength)
        $Reader.ReadBytes($Bytes)
        [ordered]@{
            data_type = ('0x{0:X2}' -f $Section.DataType)
            data_hex = (($Bytes | ForEach-Object { $_.ToString('X2') }) -join ' ')
        }
    }
    [pscustomobject][ordered]@{
        timestamp = (Get-Date).ToString('o')
        bluetooth_address = ('{0:X12}' -f $Event.BluetoothAddress)
        rssi = $Event.RawSignalStrengthInDBm
        local_name = $Event.Advertisement.LocalName
        service_uuids = @($Event.Advertisement.ServiceUuids | ForEach-Object { $_.ToString() })
        data_sections = @($Sections)
    }
}

try {
    $Watcher.Start()
    Start-Sleep -Seconds $Seconds
} finally {
    $Watcher.Stop()
    Start-Sleep -Milliseconds 200
    $Records = @(Receive-Job -Id $Subscription.Id -ErrorAction SilentlyContinue)
    Unregister-Event -SourceIdentifier $SourceIdentifier -ErrorAction SilentlyContinue
    Remove-Job -Id $Subscription.Id -Force -ErrorAction SilentlyContinue
}

$Directory = Split-Path -Parent $OutputPath
if ($Directory) { New-Item -ItemType Directory -Force -Path $Directory | Out-Null }
$Records | Select-Object timestamp, bluetooth_address, rssi, local_name, `
    service_uuids, data_sections | ConvertTo-Json -Depth 8 |
    Set-Content -Encoding UTF8 -LiteralPath $OutputPath
Write-Host "Captured $($Records.Count) matching advertisements: $OutputPath"
