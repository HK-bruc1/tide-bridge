# Codex Micro Windows HID diagnostic tool

Build from a PowerShell prompt with Visual Studio Build Tools installed:

```powershell
.\tools\windows\codex_micro_hid\build_hid_tool.ps1
```

List all HID top-level collections:

```powershell
.\tools\windows\codex_micro_hid\bin\codex_micro_hid.exe --list
```

Run the Report ID 6 `device.status` round trip and save a trace:

```powershell
.\tools\windows\codex_micro_hid\run_device_status.ps1
```

Create a fixed test-matrix evidence directory and capture active BLE advertisements:

```powershell
$dir = .\tools\windows\codex_micro_hid\new_evidence_session.ps1 `
    -Variant V1 -ChatGptBuild '<build>' -Adapter '<adapter/driver>'
.\tools\windows\codex_micro_hid\capture_ble_advertisements.ps1 `
    -OutputPath (Join-Path $dir 'ble-advertisements.json')
.\tools\windows\codex_micro_hid\run_device_status.ps1 `
    -EvidenceDirectory $dir -TestSetOutputReport
```

`WriteFile` is the required Output path. `HidD_SetOutputReport` is tested and
logged separately only when `-TestSetOutputReport` is supplied. Input uses
overlapped `ReadFile`; the tool does not substitute `HidD_GetInputReport` for
notifications.

Build the firmware variants from `SDK` with the repository wrapper. Both
experimental targets clean first so objects from another Report Map cannot be
reused:

```powershell
.\.vscode\winmk.bat codex-v1
.\.vscode\winmk.bat codex-c1
```
