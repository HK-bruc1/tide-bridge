#Requires -Version 5.1
<#
.SYNOPSIS
    Host-side HOGP profile contract regression test.

.DESCRIPTION
    Verifies that the frozen HOGP external contract has not regressed:
      - HID handle macros in rdx_hogp_profile.h
      - Report Map length and byte sequence in rdx_hogp_profile.c
      - Input Report payload is 8 bytes with no Report ID prefix in rdx_hogp_keyboard.c
      - HID Service attribute order and byte-level values in rdx_ble_server.c

    The test reads C source/header files and does not build or flash firmware.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'

$HeaderPath = Join-Path $ProtocolDir 'rdx_hogp_profile.h'
$ProfileCPath = Join-Path $ProtocolDir 'rdx_hogp_profile.c'
$KeyboardPath = Join-Path $ProtocolDir 'rdx_hogp_keyboard.c'
$ServerPath = Join-Path $ProtocolDir 'rdx_ble_server.c'
$DutPath = Join-Path $ProtocolDir 'rdx_dut.c'

$CheckResults = [System.Collections.Generic.List[object]]::new()
$Failed = 0

function Add-CheckResult {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [bool]$Passed,

        [string]$Message = ''
    )

    $script:CheckResults.Add([PSCustomObject]@{
        Name    = $Name
        Passed  = $Passed
        Message = $Message
    })

    if (-not $Passed) {
        $script:Failed++
    }

    $status = if ($Passed) { 'PASS' } else { 'FAIL' }
    if ($Message) {
        Write-Host "${status}: ${Name}: ${Message}"
    } else {
        Write-Host "${status}: ${Name}"
    }
}

function ConvertFrom-HexString {
    param([string]$Value)
    return [convert]::ToInt32($Value, 16)
}

function Normalize-Uuid {
    param([string]$Uuid)
    return $Uuid.Trim().ToLower() -replace '^0x', ''
}

# -----------------------------------------------------------------------------
# CHECK: all 13 HID handle macros
# -----------------------------------------------------------------------------
$HandleSnapshot = [ordered]@{
    HID_SERVICE_HANDLE                           = 0x0016
    HID_PROTOCOL_MODE_CHARACTERISTIC_HANDLE      = 0x0017
    HID_PROTOCOL_MODE_VALUE_HANDLE               = 0x0018
    HID_INPUT_REPORT_CHARACTERISTIC_HANDLE       = 0x0019
    HID_INPUT_REPORT_VALUE_HANDLE                = 0x001a
    HID_INPUT_REPORT_CLIENT_CONFIGURATION_HANDLE = 0x001b
    HID_INPUT_REPORT_REFERENCE_HANDLE            = 0x001c
    HID_REPORT_MAP_CHARACTERISTIC_HANDLE         = 0x001d
    HID_REPORT_MAP_VALUE_HANDLE                  = 0x001e
    HID_INFORMATION_CHARACTERISTIC_HANDLE        = 0x001f
    HID_INFORMATION_VALUE_HANDLE                 = 0x0020
    HID_CONTROL_POINT_CHARACTERISTIC_HANDLE      = 0x0021
    HID_CONTROL_POINT_VALUE_HANDLE               = 0x0022
}

$HeaderText = Get-Content -Raw -Path $HeaderPath

foreach ($entry in $HandleSnapshot.GetEnumerator()) {
    $name = $entry.Key
    $expected = $entry.Value
    $checkName = 'HANDLE_' + ($name -replace '^HID_', '' -replace '_HANDLE$', '')

    $escapedName = [regex]::Escape($name)
    if ($HeaderText -match "#define\s+$escapedName\s+(0x[0-9A-Fa-f]+)") {
        $actual = ConvertFrom-HexString -Value $Matches[1]
        Add-CheckResult -Name $checkName -Passed ($actual -eq $expected) `
            -Message "expected 0x$($expected.ToString('X4')), found 0x$($actual.ToString('X4'))"
    } else {
        Add-CheckResult -Name $checkName -Passed $false -Message "macro $name not found"
    }
}

# -----------------------------------------------------------------------------
# CHECK: Report Map length
# -----------------------------------------------------------------------------
if ($HeaderText -match '#define\s+RDX_HOGP_REPORT_MAP_LEN\s+\(?(\d+)\)?') {
    $actualLen = [int]$Matches[1]
    Add-CheckResult -Name 'REPORT_MAP_LENGTH' -Passed ($actualLen -eq 70) `
        -Message "expected 70, found $actualLen"
} else {
    Add-CheckResult -Name 'REPORT_MAP_LENGTH' -Passed $false -Message 'macro not found'
}

# -----------------------------------------------------------------------------
# CHECK: Report Map byte sequence
# -----------------------------------------------------------------------------
$ReportMapSnapshot = @(
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
    0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05,
    0x07, 0x19, 0x00, 0x29, 0xFF, 0x81, 0x00, 0x05, 0x08, 0x19,
    0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75,
    0x01, 0x91, 0x02, 0x95, 0x01, 0x75, 0x05, 0x91, 0x01, 0xC0
)

$ProfileCText = Get-Content -Raw -Path $ProfileCPath

if ($ProfileCText -match 'const\s+u8\s+rdx_hogp_report_map\[\]\s*=\s*\{([^}]+)\}') {
    $arrayBody = $Matches[1]

    # Strip C comments
    $arrayBody = [regex]::Replace($arrayBody, '/\*.*?\*/', '', [System.Text.RegularExpressions.RegexOptions]::Singleline)
    $arrayBody = ($arrayBody -split "`r?`n" | ForEach-Object { $_ -replace '//.*$', '' }) -join "`n"

    # Extract hex bytes
    $bytes = [regex]::Matches($arrayBody, '0x([0-9A-Fa-f]{2})') | ForEach-Object {
        ConvertFrom-HexString -Value ("0x" + $_.Groups[1].Value)
    }

    if ($bytes.Count -ne $ReportMapSnapshot.Count) {
        Add-CheckResult -Name 'REPORT_MAP_BYTES' -Passed $false `
            -Message "expected $($ReportMapSnapshot.Count) bytes, found $($bytes.Count)"
    } else {
        $mismatchIndex = -1
        for ($i = 0; $i -lt $ReportMapSnapshot.Count; $i++) {
            if ($bytes[$i] -ne $ReportMapSnapshot[$i]) {
                $mismatchIndex = $i
                break
            }
        }

        if ($mismatchIndex -ge 0) {
            $exp = $ReportMapSnapshot[$mismatchIndex].ToString('X2')
            $act = $bytes[$mismatchIndex].ToString('X2')
            Add-CheckResult -Name 'REPORT_MAP_BYTES' -Passed $false `
                -Message "byte at index $mismatchIndex expected 0x$exp but found 0x$act"
        } else {
            Add-CheckResult -Name 'REPORT_MAP_BYTES' -Passed $true
        }
    }
} else {
    Add-CheckResult -Name 'REPORT_MAP_BYTES' -Passed $false -Message 'array rdx_hogp_report_map not found'
}

# -----------------------------------------------------------------------------
# CHECK: Input Report payload inside rdx_hogp_key_send_usage()
# -----------------------------------------------------------------------------
$KeyboardText = Get-Content -Raw -Path $KeyboardPath

$sendFunctionMatch = [regex]::Match($KeyboardText,
    '(?sm)int\s+rdx_hogp_key_send_usage\s*\([^)]*\)\s*\{(.*?)^\}')

if ($sendFunctionMatch.Success) {
    $sendFunctionBody = $sendFunctionMatch.Groups[1].Value

    $hasReport8 = $sendFunctionBody -match 'u8\s+report\s*\[\s*8\s*\]'
    Add-CheckResult -Name 'INPUT_REPORT_LENGTH' -Passed $hasReport8 `
        -Message $(if ($hasReport8) { '' } else { 'local report array is not u8 report[8]' })

    $sendCallPattern = 'app_ble_att_send_data\s*\([^,]+,\s*HID_INPUT_REPORT_VALUE_HANDLE\s*,\s*report\s*,\s*sizeof\s*\(\s*report\s*\)'
    $hasSendCall = $sendFunctionBody -match $sendCallPattern
    Add-CheckResult -Name 'NO_REPORT_ID_PREFIX' -Passed $hasSendCall `
        -Message $(if ($hasSendCall) { '' } else { 'notify call does not use (report, sizeof(report)) exactly' })
} else {
    Add-CheckResult -Name 'INPUT_REPORT_LENGTH' -Passed $false -Message 'rdx_hogp_key_send_usage() not found'
    Add-CheckResult -Name 'NO_REPORT_ID_PREFIX' -Passed $false -Message 'rdx_hogp_key_send_usage() not found'
}

# -----------------------------------------------------------------------------
# CHECK: profile data attribute order and byte-level values
# -----------------------------------------------------------------------------
$ServerText = Get-Content -Raw -Path $ServerPath

# Expected contract records for the HID Service block (0x0016-0x0022)
$ExpectedAttributes = @(
    @{ Handle = 0x0016; Type = 'PRIMARY_SERVICE'; AttUuid = 0x2800; ServiceUuid = 0x1812 }
    @{ Handle = 0x0017; Type = 'CHARACTERISTIC'; AttUuid = 0x2803; Properties = 0x06; ValueHandle = 0x0018; CharUuid = 0x2A4E }
    @{ Handle = 0x0018; Type = 'VALUE'; AttUuid = 0x2A4E; Value = 0x01 }
    @{ Handle = 0x0019; Type = 'CHARACTERISTIC'; AttUuid = 0x2803; Properties = 0x1A; ValueHandle = 0x001A; CharUuid = 0x2A4D }
    @{ Handle = 0x001A; Type = 'VALUE'; AttUuid = 0x2A4D }
    @{ Handle = 0x001B; Type = 'CLIENT_CHARACTERISTIC_CONFIGURATION'; AttUuid = 0x2902; Value = 0x0000 }
    @{ Handle = 0x001C; Type = 'REPORT_REFERENCE'; AttUuid = 0x2908; ReportId = 0x01; ReportType = 0x01 }
    @{ Handle = 0x001D; Type = 'CHARACTERISTIC'; AttUuid = 0x2803; Properties = 0x02; ValueHandle = 0x001E; CharUuid = 0x2A4B }
    @{ Handle = 0x001E; Type = 'VALUE'; AttUuid = 0x2A4B }
    @{ Handle = 0x001F; Type = 'CHARACTERISTIC'; AttUuid = 0x2803; Properties = 0x02; ValueHandle = 0x0020; CharUuid = 0x2A4A }
    @{ Handle = 0x0020; Type = 'VALUE'; AttUuid = 0x2A4A }
    @{ Handle = 0x0021; Type = 'CHARACTERISTIC'; AttUuid = 0x2803; Properties = 0x04; ValueHandle = 0x0022; CharUuid = 0x2A4C }
    @{ Handle = 0x0022; Type = 'VALUE'; AttUuid = 0x2A4C }
)

function Test-ProfileAttributeOrder {
    # Extract HID Service block from rdx_profile_data[]
    $blockMatch = [regex]::Match($ServerText,
        '(?s)//\s*0x0016\s+PRIMARY_SERVICE.*?(?=\r?\n\s*#endif\s*/\*\s*TCFG_RDX_HOGP_ENABLE\s*\*/)')

    if (-not $blockMatch.Success) {
        Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false -Message 'HID Service block not found in rdx_profile_data[]'
        return
    }

    $block = $blockMatch.Value
    $lines = $block -split "`r?`n"

    $commentPattern = '^\s*//\s*0x(?<handle>[0-9A-Fa-f]{4})\s+(?<type>\S+)(?:\s+(?<uuid>0x[0-9A-Fa-f]+|[0-9A-Fa-f]+))?.*$'

    $parsedAttributes = [System.Collections.Generic.List[hashtable]]::new()
    $currentEntry = $null
    $script:parseFailed = $false

    function Complete-CurrentEntry {
        if ($null -eq $currentEntry) { return }

        $bytes = [regex]::Matches(($currentEntry.ByteLines -join ' '), '0x([0-9A-Fa-f]{2})') | ForEach-Object {
            ConvertFrom-HexString -Value ("0x" + $_.Groups[1].Value)
        }

        if ($bytes.Count -lt 8) {
            Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                -Message "handle 0x$($currentEntry.Handle.ToString('X4')) has insufficient byte line data"
            $script:parseFailed = $true
            return
        }

        $size = $bytes[0] -bor ($bytes[1] -shl 8)
        $flags = $bytes[2] -bor ($bytes[3] -shl 8)
        $handle = $bytes[4] -bor ($bytes[5] -shl 8)
        $attUuid = $bytes[6] -bor ($bytes[7] -shl 8)

        $record = [ordered]@{
            Handle      = $handle
            CommentType = $currentEntry.Type
            AttUuid     = $attUuid
        }

        switch ($currentEntry.Type) {
            'PRIMARY_SERVICE' {
                if ($bytes.Count -lt 10) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($handle.ToString('X4')) PRIMARY_SERVICE has no service UUID"
                    $script:parseFailed = $true
                    return
                }
                $record.ServiceUuid = $bytes[8] -bor ($bytes[9] -shl 8)
            }
            'CHARACTERISTIC' {
                if ($bytes.Count -lt 13) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($handle.ToString('X4')) CHARACTERISTIC has insufficient bytes"
                    $script:parseFailed = $true
                    return
                }
                $record.Properties = $bytes[8]
                $record.ValueHandle = $bytes[9] -bor ($bytes[10] -shl 8)
                $record.CharUuid = $bytes[11] -bor ($bytes[12] -shl 8)
            }
            'VALUE' {
                if ($bytes.Count -gt 8) {
                    $record.Value = $bytes[8]
                }
            }
            'CLIENT_CHARACTERISTIC_CONFIGURATION' {
                if ($bytes.Count -lt 10) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($handle.ToString('X4')) CCC has no value"
                    $script:parseFailed = $true
                    return
                }
                $record.Value = $bytes[8] -bor ($bytes[9] -shl 8)
            }
            'REPORT_REFERENCE' {
                if ($bytes.Count -lt 10) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($handle.ToString('X4')) REPORT_REFERENCE has no report id/type"
                    $script:parseFailed = $true
                    return
                }
                $record.ReportId = $bytes[8]
                $record.ReportType = $bytes[9]
            }
            default {
                Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                    -Message "handle 0x$($handle.ToString('X4')) has unknown attribute type '$($currentEntry.Type)'"
                $script:parseFailed = $true
                return
            }
        }

        $parsedAttributes.Add($record)
        $script:currentEntry = $null
    }

    foreach ($line in $lines) {
        $cm = [regex]::Match($line, $commentPattern)
        if ($cm.Success) {
            Complete-CurrentEntry
            if ($script:parseFailed) { return }

            $currentEntry = @{
                Handle    = ConvertFrom-HexString -Value ("0x" + $cm.Groups['handle'].Value)
                Type      = $cm.Groups['type'].Value.Trim().TrimEnd(',').ToUpper()
                ByteLines = [System.Collections.Generic.List[string]]::new()
            }
            continue
        }

        if (($line -match '0x[0-9A-Fa-f]{2}') -and ($null -ne $currentEntry)) {
            $currentEntry.ByteLines.Add($line)
        }
    }
    Complete-CurrentEntry
    if ($script:parseFailed) { return }

    # Compare parsed attributes against expected snapshot
    if ($parsedAttributes.Count -ne $ExpectedAttributes.Count) {
        Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
            -Message "expected $($ExpectedAttributes.Count) attributes, found $($parsedAttributes.Count)"
        return
    }

    for ($i = 0; $i -lt $ExpectedAttributes.Count; $i++) {
        $exp = $ExpectedAttributes[$i]
        $act = $parsedAttributes[$i]

        if ($act.Handle -ne $exp.Handle) {
            Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                -Message "attribute at index $i expected handle 0x$($exp.Handle.ToString('X4')) but found 0x$($act.Handle.ToString('X4'))"
            return
        }
        if ($act.CommentType -ne $exp.Type) {
            Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                -Message "handle 0x$($exp.Handle.ToString('X4')) expected type $($exp.Type) but found $($act.CommentType)"
            return
        }
        if ($act.AttUuid -ne $exp.AttUuid) {
            Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                -Message "handle 0x$($exp.Handle.ToString('X4')) expected att_uuid 0x$($exp.AttUuid.ToString('X4')) but found 0x$($act.AttUuid.ToString('X4'))"
            return
        }

        switch ($exp.Type) {
            'PRIMARY_SERVICE' {
                if ($act.ServiceUuid -ne $exp.ServiceUuid) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) PRIMARY_SERVICE expected service_uuid 0x$($exp.ServiceUuid.ToString('X4')) but found 0x$($act.ServiceUuid.ToString('X4'))"
                    return
                }
            }
            'CHARACTERISTIC' {
                if ($act.Properties -ne $exp.Properties) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) CHARACTERISTIC expected properties 0x$($exp.Properties.ToString('X2')) but found 0x$($act.Properties.ToString('X2'))"
                    return
                }
                if ($act.ValueHandle -ne $exp.ValueHandle) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) CHARACTERISTIC expected value_handle 0x$($exp.ValueHandle.ToString('X4')) but found 0x$($act.ValueHandle.ToString('X4'))"
                    return
                }
                if ($act.CharUuid -ne $exp.CharUuid) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) CHARACTERISTIC expected characteristic_uuid 0x$($exp.CharUuid.ToString('X4')) but found 0x$($act.CharUuid.ToString('X4'))"
                    return
                }
            }
            'VALUE' {
                if ($exp.ContainsKey('Value') -and ($act.Value -ne $exp.Value)) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) VALUE expected value 0x$($exp.Value.ToString('X2')) but found 0x$($act.Value.ToString('X2'))"
                    return
                }
            }
            'CLIENT_CHARACTERISTIC_CONFIGURATION' {
                if ($act.Value -ne $exp.Value) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) CCC expected value 0x$($exp.Value.ToString('X4')) but found 0x$($act.Value.ToString('X4'))"
                    return
                }
            }
            'REPORT_REFERENCE' {
                if ($act.ReportId -ne $exp.ReportId -or $act.ReportType -ne $exp.ReportType) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) REPORT_REFERENCE expected (id=0x$($exp.ReportId.ToString('X2')), type=0x$($exp.ReportType.ToString('X2'))) but found (id=0x$($act.ReportId.ToString('X2')), type=0x$($act.ReportType.ToString('X2')))"
                    return
                }
            }
        }
    }

    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $true
}

Test-ProfileAttributeOrder

# -----------------------------------------------------------------------------
# CHECK: Output Report block (0x0028-0x002a) is gated by TCFG_RDX_HOGP_ENABLE
# -----------------------------------------------------------------------------
$outputReportGatedPattern = '(?s)#if\s+TCFG_RDX_HOGP_ENABLE\s*\r?\n\s*//\s*0x0028\s+CHARACTERISTIC\s+0x2A4D.*?0x01,\s*0x02,\s*\r?\n\s*#endif\s*/\*\s*TCFG_RDX_HOGP_ENABLE\s*\*/'
$isOutputReportGated = $ServerText -match $outputReportGatedPattern
Add-CheckResult -Name 'OUTPUT_REPORT_GATED' -Passed $isOutputReportGated `
    -Message $(if ($isOutputReportGated) { '' } else { 'Output Report block (0x0028-0x002a) is not wrapped in #if TCFG_RDX_HOGP_ENABLE / #endif' })

# -----------------------------------------------------------------------------
# Phase 6 C1 checks: mode controller and owner authorization
# -----------------------------------------------------------------------------
$ServerHPath = Join-Path $ProtocolDir 'rdx_ble_server.h'
$ServerHText = Get-Content -Raw -Path $ServerHPath

# Build a comment-stripped view of server.c so static checks do not treat
# commented-out type definitions as valid code.
$serverCodeOnly = [regex]::Replace($ServerText, '/\*.*?\*/', '', [System.Text.RegularExpressions.RegexOptions]::Singleline)

# 1) Private mode controller lives in server.c, not in public header
$hasPrivateController = $serverCodeOnly -match 'static\s+rdx_ble_mode_controller_t\s+s_ble_mode'
Add-CheckResult -Name 'C1_PRIVATE_MODE_CONTROLLER' -Passed $hasPrivateController `
    -Message $(if ($hasPrivateController) { '' } else { 'static rdx_ble_mode_controller_t s_ble_mode not found in rdx_ble_server.c (may be commented out)' })

$modeTypeDefinedInCode = ($serverCodeOnly -match 'typedef\s+enum\s*\{\s*RDX_BLE_MODE_CONFIG\s*=\s*0,\s*RDX_BLE_MODE_HOGP,\s*\}\s*rdx_ble_mode_t') -and
                         ($serverCodeOnly -match 'typedef\s+struct\s*\{[\s\S]*?rdx_ble_mode_t\s+requested_mode;[\s\S]*?\}\s*rdx_ble_mode_controller_t\s*;')
Add-CheckResult -Name 'C1_MODE_TYPES_DEFINED_IN_CODE' -Passed $modeTypeDefinedInCode `
    -Message $(if ($modeTypeDefinedInCode) { '' } else { 'rdx_ble_mode_t / rdx_ble_mode_controller_t not defined outside comments in rdx_ble_server.c' })

$hasPublicModeField = $ServerHText -match 'rdx_ble_mode_t|rdx_ble_connection_owner_t'
Add-CheckResult -Name 'C1_MODE_TYPES_NOT_PUBLIC' -Passed (-not $hasPublicModeField) `
    -Message $(if (-not $hasPublicModeField) { '' } else { 'rdx_ble_mode_t or rdx_ble_connection_owner_t found in public header rdx_ble_server.h' })

# 2) Public wrappers declared in header and implemented in server.c
$hasModeRequestDecl = $ServerHText -match 'void\s+rdx_ble_mode_request_hogp\s*\(\s*u8\s+enable\s*\)'
$hasOwnerCheckDecl = $ServerHText -match 'u8\s+rdx_ble_connection_owner_is_hogp\s*\(\s*void\s*\)'
Add-CheckResult -Name 'C1_PUBLIC_WRAPPERS_DECLARED' -Passed ($hasModeRequestDecl -and $hasOwnerCheckDecl) `
    -Message $(if ($hasModeRequestDecl -and $hasOwnerCheckDecl) { '' } else { 'rdx_ble_mode_request_hogp or rdx_ble_connection_owner_is_hogp not declared in rdx_ble_server.h' })

$hasModeRequestImpl = $serverCodeOnly -match 'void\s+rdx_ble_mode_request_hogp\s*\(\s*u8\s+enable\s*\)'
$hasOwnerCheckImpl = $serverCodeOnly -match 'u8\s+rdx_ble_connection_owner_is_hogp\s*\(\s*void\s*\)'
Add-CheckResult -Name 'C1_PUBLIC_WRAPPERS_IMPLEMENTED' -Passed ($hasModeRequestImpl -and $hasOwnerCheckImpl) `
    -Message $(if ($hasModeRequestImpl -and $hasOwnerCheckImpl) { '' } else { 'rdx_ble_mode_request_hogp or rdx_ble_connection_owner_is_hogp not implemented outside comments in rdx_ble_server.c' })

# 3) Public info struct must not carry the new mode/owner fields
$infoStructMatch = [regex]::Match($ServerHText, '(?s)typedef\s+struct\s*\{\s*(.*?)\s*\}\s*rdx_ble_server_info_t\s*;')
$infoStructCarriesMode = $false
if ($infoStructMatch.Success) {
    $infoStructBody = $infoStructMatch.Groups[1].Value
    $infoStructCarriesMode = ($infoStructBody -match 'rdx_ble_mode_t') -or ($infoStructBody -match 'rdx_ble_connection_owner_t')
}
Add-CheckResult -Name 'C1_INFO_STRUCT_NO_MODE_FIELDS' -Passed (-not $infoStructCarriesMode) `
    -Message $(if (-not $infoStructCarriesMode) { '' } else { 'rdx_ble_server_info_t contains rdx_ble_mode_t/rdx_ble_connection_owner_t fields' })

# 4) Disconnection complete dispatches by previous owner, force-applies pending mode, restarts HOGP advertising
$disconnBlockMatch = [regex]::Match($serverCodeOnly,
    '(?s)case\s+HCI_EVENT_DISCONNECTION_COMPLETE:\s*\{(.*?)\}\s*break;')
$disconnBlockOk = $false
$disconnMessage = 'HCI_EVENT_DISCONNECTION_COMPLETE block not found'
if ($disconnBlockMatch.Success) {
    $disconnBlock = $disconnBlockMatch.Groups[1].Value
    $hasPrevOwner = $disconnBlock -match 'prev_owner\s*=\s*s_ble_mode\.connection_owner'
    $hasHogpDisconnect = $disconnBlock -match 'rdx_hogp_on_disconnected\s*\('
    $hasConfigCleanup = $disconnBlock -match 'rdx_ble_server_disconnected_cleanup_internal\s*\('
    $hasForceApply = $disconnBlock -match 'rdx_ble_mode_apply_requested_force\s*\('
    $hasHogpRestart = $disconnBlock -match 'rdx_ble_mode_restart_hogp_advertising\s*\('
    $disconnBlockOk = $hasPrevOwner -and $hasHogpDisconnect -and $hasConfigCleanup -and $hasForceApply -and $hasHogpRestart
    $parts = @()
    if (-not $hasPrevOwner) { $parts += 'prev_owner capture' }
    if (-not $hasHogpDisconnect) { $parts += 'hogp disconnect' }
    if (-not $hasConfigCleanup) { $parts += 'config cleanup' }
    if (-not $hasForceApply) { $parts += 'force mode apply' }
    if (-not $hasHogpRestart) { $parts += 'hogp restart' }
    if ($parts.Count -gt 0) {
        $disconnMessage = 'missing: ' + ($parts -join ', ')
    } else {
        $disconnMessage = ''
    }
}
Add-CheckResult -Name 'C1_DISCONNECTION_DISPATCH' -Passed $disconnBlockOk -Message $disconnMessage

# 4b) Force-apply helper actually transitions advertised_mode and clears pending
$applyForceFunctionMatch = [regex]::Match($serverCodeOnly,
    '(?sm)static\s+void\s+rdx_ble_mode_apply_requested_force\s*\([^)]*\)\s*\{(.*?)^\}')
$forceCallsInternal = $false
if ($applyForceFunctionMatch.Success) {
    $forceCallsInternal = $applyForceFunctionMatch.Groups[1].Value -match 'rdx_ble_mode_apply_requested_internal\s*\('
}

$applyInternalFunctionMatch = [regex]::Match($serverCodeOnly,
    '(?sm)static\s+void\s+rdx_ble_mode_apply_requested_internal\s*\([^)]*\)\s*\{(.*?)^\}')
$applyForceOk = $false
$applyForceMessage = 'rdx_ble_mode_apply_requested_force() / rdx_ble_mode_apply_requested_internal() not found'
if ($applyForceFunctionMatch.Success -and $applyInternalFunctionMatch.Success) {
    $applyInternalBody = $applyInternalFunctionMatch.Groups[1].Value
    $hasPendingClear = $applyInternalBody -match 'switch_pending\s*=\s*0'
    $hasAdvUpdate = $applyInternalBody -match 'advertised_mode\s*=\s*s_ble_mode\.requested_mode'
    $applyForceOk = $forceCallsInternal -and $hasPendingClear -and $hasAdvUpdate
    $parts = @()
    if (-not $forceCallsInternal) { $parts += 'force calls internal' }
    if (-not $hasPendingClear) { $parts += 'pending clear' }
    if (-not $hasAdvUpdate) { $parts += 'advertised_mode update' }
    if ($parts.Count -gt 0) {
        $applyForceMessage = 'rdx_ble_mode_apply_requested_force() missing: ' + ($parts -join ', ')
    } else {
        $applyForceMessage = ''
    }
}
Add-CheckResult -Name 'C1_DISCONNECTION_FORCE_APPLY_TRANSITION' -Passed $applyForceOk -Message $applyForceMessage

# 4c) Switching to CONFIG clears HOGP runtime mode without touching advertising
$KeyboardText = Get-Content -Raw -Path $KeyboardPath
$keyboardCodeOnly = [regex]::Replace($KeyboardText, '/\*.*?\*/', '', [System.Text.RegularExpressions.RegexOptions]::Singleline)
$advertisingApiPattern = 'rdx_ble_server_adv_enable|rdx_ble_mode_start_config_advertising|rdx_ble_mode_start_hogp_advertising|rdx_ble_mode_restart_hogp_advertising|rdx_hogp_adv_start|rdx_hogp_adv_stop|hogp_adv_start_internal|hogp_adv_stop_internal|hogp_mode_set|app_ble_adv_enable|app_ble_adv_data_set'

$hogpClearMatch = [regex]::Match($serverCodeOnly,
    '(?sm)static\s+void\s+rdx_ble_mode_sync_hogp_runtime\s*\([^)]*\)\s*\{(.*?)^\}')
$hogpClearOk = $false
$hogpClearMessage = 'rdx_ble_mode_sync_hogp_runtime() not found'
$applyInternalCallsSync = $false
if ($applyInternalFunctionMatch.Success) {
    $applyInternalCallsSync = $applyInternalFunctionMatch.Groups[1].Value -match 'rdx_ble_mode_sync_hogp_runtime\s*\('
}
$runtimeCleanupInKeyboardMatch = [regex]::Match($keyboardCodeOnly,
    '(?sm)void\s+rdx_hogp_runtime_cleanup\s*\([^)]*\)\s*\{(.*?)^\}')
if ($hogpClearMatch.Success -and $runtimeCleanupInKeyboardMatch.Success) {
    $hogpClearBody = $hogpClearMatch.Groups[1].Value
    $hasConfigCheck = $hogpClearBody -match 'advertised_mode\s*==\s*RDX_BLE_MODE_CONFIG'
    $hasHogpGet = $hogpClearBody -match 'hogp_mode_get\s*\('
    $hasRuntimeCleanup = $hogpClearBody -match 'rdx_hogp_runtime_cleanup\s*\('
    $syncHasNoAdvertising = $hogpClearBody -notmatch $advertisingApiPattern
    $cleanupHasNoAdvertising = $runtimeCleanupInKeyboardMatch.Groups[1].Value -notmatch $advertisingApiPattern
    $hogpClearOk = $hasConfigCheck -and $hasHogpGet -and $hasRuntimeCleanup -and $applyInternalCallsSync -and $syncHasNoAdvertising -and $cleanupHasNoAdvertising
    $parts = @()
    if (-not $hasConfigCheck) { $parts += 'CONFIG branch' }
    if (-not $hasHogpGet) { $parts += 'hogp_mode_get()' }
    if (-not $hasRuntimeCleanup) { $parts += 'rdx_hogp_runtime_cleanup()' }
    if (-not $applyInternalCallsSync) { $parts += 'called from apply_internal' }
    if (-not $syncHasNoAdvertising) { $parts += 'sync helper must not call advertising APIs' }
    if (-not $cleanupHasNoAdvertising) { $parts += 'rdx_hogp_runtime_cleanup() must not call advertising APIs' }
    if ($parts.Count -gt 0) {
        $hogpClearMessage = 'HOGP runtime clear contract missing: ' + ($parts -join ', ')
    } else {
        $hogpClearMessage = ''
    }
}
Add-CheckResult -Name 'C1_HOGP_RUNTIME_CLEARED_ON_CONFIG' -Passed $hogpClearOk -Message $hogpClearMessage

# 4d) Force-apply path (start_adv == 0) must not trigger any advertising API
$forceApplyNoAdvOk = $false
$forceApplyNoAdvMessage = 'rdx_ble_mode_apply_requested_force() not found'
if ($applyForceFunctionMatch.Success -and $applyInternalFunctionMatch.Success) {
    $forceBody = $applyForceFunctionMatch.Groups[1].Value
    $internalBody = $applyInternalFunctionMatch.Groups[1].Value
    $forceBodyNoAdv = $forceBody -notmatch $advertisingApiPattern
    # Remove the "if (start_adv) { ... }" block from internal body, then verify
    # no advertising calls remain outside that conditional.  Match uses the same
    # indentation for the closing brace as the if line.
    $startAdvBlockPattern = '(?m)^(\s*)if\s*\(\s*start_adv\s*\)\s*\{[\s\S]*?^\1\}'
    $internalBodyWithoutStartAdv = [regex]::Replace($internalBody, $startAdvBlockPattern, '')
    $internalNoAdvOutsideStart = $internalBodyWithoutStartAdv -notmatch $advertisingApiPattern
    $forceApplyNoAdvOk = $forceBodyNoAdv -and $internalNoAdvOutsideStart
    $parts = @()
    if (-not $forceBodyNoAdv) { $parts += 'force wrapper calls advertising API' }
    if (-not $internalNoAdvOutsideStart) { $parts += 'apply_internal calls advertising API outside if (start_adv)' }
    if ($parts.Count -gt 0) {
        $forceApplyNoAdvMessage = 'Force-apply advertising leak: ' + ($parts -join ', ')
    } else {
        $forceApplyNoAdvMessage = ''
    }
}
Add-CheckResult -Name 'C1_FORCE_APPLY_NO_ADVERTISING' -Passed $forceApplyNoAdvOk -Message $forceApplyNoAdvMessage

# 4e+) apply_internal() checks broadcast suppression inside the start_adv branch
$applySuppressOk = $false
$applySuppressMessage = 'rdx_ble_mode_apply_requested_internal() not found'
if ($applyInternalFunctionMatch.Success) {
    $applyBody = $applyInternalFunctionMatch.Groups[1].Value
    $startAdvSegments = $applyBody -split 'if\s*\(\s*start_adv\s*\)'
    if ($startAdvSegments.Count -ge 2) {
        $insideStartAdv = $startAdvSegments[1]
        $hasSuppressedCheck = $insideStartAdv -match 'rdx_ble_mode_broadcast_suppressed\s*\('
        $hasAdvDisable = $insideStartAdv -match 'rdx_ble_server_adv_enable\s*\(\s*0\s*\)'
        $hasReturn = $insideStartAdv -match 'return\s*;'
        $applySuppressOk = $hasSuppressedCheck -and $hasAdvDisable -and $hasReturn
        $parts = @()
        if (-not $hasSuppressedCheck) { $parts += 'broadcast_suppressed() check' }
        if (-not $hasAdvDisable) { $parts += 'disable advertising when suppressed' }
        if (-not $hasReturn) { $parts += 'early return when suppressed' }
        if ($parts.Count -gt 0) {
            $applySuppressMessage = 'apply_internal suppression contract missing: ' + ($parts -join ', ')
        } else {
            $applySuppressMessage = ''
        }
    } else {
        $applySuppressMessage = 'start_adv branch not found in apply_internal()'
    }
}
Add-CheckResult -Name 'C1_APPLY_INTERNAL_SUPPRESSES_BROADCAST' -Passed $applySuppressOk -Message $applySuppressMessage

# 4e) CONFIG advertising start must stop current broadcast and refresh RDX data
$configStartMatch = [regex]::Match($serverCodeOnly,
    '(?sm)static\s+void\s+rdx_ble_mode_start_config_advertising\s*\([^)]*\)\s*\{(.*?)^\}')
$configStartOk = $false
$configStartMessage = 'rdx_ble_mode_start_config_advertising() not found'
if ($configStartMatch.Success) {
    $configStartBody = $configStartMatch.Groups[1].Value
    $hasAdvDisable = $configStartBody -match 'rdx_ble_server_adv_enable\s*\(\s*0\s*\)'
    $hasAdvEnable = $configStartBody -match 'rdx_ble_server_adv_enable\s*\(\s*1\s*\)'
    $hasNoHogpModeSet = $configStartBody -notmatch '\bhogp_mode_set\s*\('
    $configStartOk = $hasAdvDisable -and $hasAdvEnable -and $hasNoHogpModeSet
    $parts = @()
    if (-not $hasAdvDisable) { $parts += 'disable advertising' }
    if (-not $hasAdvEnable) { $parts += 'enable RDX advertising' }
    if (-not $hasNoHogpModeSet) { $parts += 'must not call hogp_mode_set()' }
    if ($parts.Count -gt 0) {
        $configStartMessage = 'CONFIG advertising start contract missing: ' + ($parts -join ', ')
    } else {
        $configStartMessage = ''
    }
}
Add-CheckResult -Name 'C1_CONFIG_START_REFRESHES_ADV' -Passed $configStartOk -Message $configStartMessage

$DutText = Get-Content -Raw -Path $DutPath

# 4f) DUT enter/exit request CONFIG mode through the narrow public wrapper
$enterBlockMatch = [regex]::Match($DutText,
    '(?s)rdx_dut_info\.dut_mode = TRUE;.*?rdx_ble_mode_request_hogp\s*\(\s*0\s*\).*?rdx_ble_server_app_disconnect\s*\(')
Add-CheckResult -Name 'C1_DUT_ENTER_REQUESTS_CONFIG' -Passed $enterBlockMatch.Success `
    -Message $(if ($enterBlockMatch.Success) { '' } else { 'DUT enter does not request CONFIG mode before disconnecting' })

$exitBlockMatch = [regex]::Match($DutText,
    '(?s)rdx_dut_info\.dut_mode = FALSE;.*?rdx_ble_mode_request_hogp\s*\(\s*0\s*\).*?rdx_ble_server_adv_data_changed\s*\(')
Add-CheckResult -Name 'C1_DUT_EXIT_REQUESTS_CONFIG' -Passed $exitBlockMatch.Success `
    -Message $(if ($exitBlockMatch.Success) { '' } else { 'DUT exit does not request CONFIG mode before refreshing broadcast' })

# 4g) HOGP advertising restart is suppressed under the same conditions as RDX restart
$restartFunctionMatch = [regex]::Match($serverCodeOnly,
    '(?sm)static\s+void\s+rdx_ble_mode_restart_hogp_advertising\s*\([^)]*\)\s*\{(.*?)^\}')
$broadcastSuppressedMatch = [regex]::Match($serverCodeOnly,
    '(?sm)static\s+u8\s+rdx_ble_mode_broadcast_suppressed\s*\([^)]*\)\s*\{(.*?)^\}')
$hogpRestartSuppressedOk = $false
$hogpRestartSuppressedMessage = 'rdx_ble_mode_restart_hogp_advertising() not found'
if ($restartFunctionMatch.Success -and $broadcastSuppressedMatch.Success) {
    $restartBody = $restartFunctionMatch.Groups[1].Value
    $suppressedBody = $broadcastSuppressedMatch.Groups[1].Value
    $hasSuppressedCall = $restartBody -match 'rdx_ble_mode_broadcast_suppressed\s*\('
    $hasAdvDisable = $restartBody -match 'rdx_ble_server_adv_enable\s*\(\s*0\s*\)'
    $hasDutCheck = $suppressedBody -match 'rdx_app_get_dut_status\s*\('
    $hogpRestartSuppressedOk = $hasSuppressedCall -and $hasAdvDisable -and $hasDutCheck
    $parts = @()
    if (-not $hasSuppressedCall) { $parts += 'calls broadcast_suppressed()' }
    if (-not $hasAdvDisable) { $parts += 'disables advertising when suppressed' }
    if (-not $hasDutCheck) { $parts += 'broadcast_suppressed() checks DUT status' }
    if ($parts.Count -gt 0) {
        $hogpRestartSuppressedMessage = 'HOGP restart suppression contract missing: ' + ($parts -join ', ')
    } else {
        $hogpRestartSuppressedMessage = ''
    }
}
Add-CheckResult -Name 'C1_HOGP_RESTART_SUPPRESSES_DUT' -Passed $hogpRestartSuppressedOk -Message $hogpRestartSuppressedMessage

# 4h) adv_data_changed() routes to the current advertised identity
$advDataChangedMatch = [regex]::Match($serverCodeOnly,
    '(?sm)void\s+rdx_ble_server_adv_data_changed\s*\([^)]*\)\s*\{(.*?)^\}')
$advDataIdentityOk = $false
$advDataIdentityMessage = 'rdx_ble_server_adv_data_changed() not found'
if ($advDataChangedMatch.Success) {
    $advBody = $advDataChangedMatch.Groups[1].Value
    $beforeAdvOff = ($advBody -split 'rdx_ble_server_adv_enable\s*\(\s*0\s*\)')[0]
    $hasHogpBranch = ($beforeAdvOff -match 's_ble_mode\.advertised_mode\s*==\s*RDX_BLE_MODE_HOGP') -and
                     ($beforeAdvOff -match 'rdx_ble_mode_restart_hogp_advertising\s*\(')
    $hasReturn = $beforeAdvOff -match 'return\s*;'
    $advDataIdentityOk = $hasHogpBranch -and $hasReturn
    $parts = @()
    if (-not $hasHogpBranch) { $parts += 'HOGP branch before RDX adv off' }
    if (-not $hasReturn) { $parts += 'HOGP branch returns early' }
    if ($parts.Count -gt 0) {
        $advDataIdentityMessage = 'adv_data_changed identity routing missing: ' + ($parts -join ', ')
    } else {
        $advDataIdentityMessage = ''
    }
}
Add-CheckResult -Name 'C1_ADV_DATA_CHANGED_IDENTITY_ROUTES' -Passed $advDataIdentityOk -Message $advDataIdentityMessage

# 5) Server exit resets mode controller
$exitFunctionMatch = [regex]::Match($ServerText,
    '(?sm)void\s+rdx_ble_server_exit\s*\([^)]*\)\s*\{(.*?)^\}')
$exitResetsMode = $false
if ($exitFunctionMatch.Success) {
    $exitBody = $exitFunctionMatch.Groups[1].Value
    $exitResetsMode = $exitBody -match 'rdx_ble_mode_controller_reset\s*\('
}
Add-CheckResult -Name 'C1_EXIT_RESETS_MODE_CONTROLLER' -Passed $exitResetsMode `
    -Message $(if ($exitResetsMode) { '' } else { 'rdx_ble_mode_controller_reset() not called in rdx_ble_server_exit()' })

# 6) Output Report write has owner authorization
$writeFunctionMatch = [regex]::Match($ServerText,
    '(?sm)static\s+int\s+rdx_ble_server_att_write_callback\s*\([^)]*\)\s*\{(.*?)^\}')
$outputReportOwnerCheck = $false
if ($writeFunctionMatch.Success) {
    $writeBody = $writeFunctionMatch.Groups[1].Value
    $outputReportOwnerCheck = ($writeBody -match 'HID_OUTPUT_REPORT_VALUE_HANDLE') -and
                              ($writeBody -match 'rdx_ble_connection_owner_is_hogp\s*\(')
}
Add-CheckResult -Name 'C1_OUTPUT_REPORT_OWNER_CHECK' -Passed $outputReportOwnerCheck `
    -Message $(if ($outputReportOwnerCheck) { '' } else { 'Output Report write does not check HOGP owner' })

# 7) RDX notify send paths reject non-CONFIG owner
$sendFunctionMatch = [regex]::Match($ServerText,
    '(?sm)int\s+rdx_ble_server_send\s*\([^)]*\)\s*\{(.*?)^\}')
$sendOwnerCheck = $false
if ($sendFunctionMatch.Success) {
    $sendBody = $sendFunctionMatch.Groups[1].Value
    $sendOwnerCheck = $sendBody -match 'connection_owner\s*!=\s*RDX_BLE_OWNER_CONFIG'
}
Add-CheckResult -Name 'C1_SERVER_SEND_OWNER_CHECK' -Passed $sendOwnerCheck `
    -Message $(if ($sendOwnerCheck) { '' } else { 'rdx_ble_server_send() does not reject non-CONFIG owner' })

$otaSendFunctionMatch = [regex]::Match($ServerText,
    '(?sm)int\s+rdx_ble_server_ota_send\s*\([^)]*\)\s*\{(.*?)^\}')
$otaSendOwnerCheck = $false
if ($otaSendFunctionMatch.Success) {
    $otaSendBody = $otaSendFunctionMatch.Groups[1].Value
    $otaSendOwnerCheck = $otaSendBody -match 'connection_owner\s*!=\s*RDX_BLE_OWNER_CONFIG'
}
Add-CheckResult -Name 'C1_OTA_SEND_OWNER_CHECK' -Passed $otaSendOwnerCheck `
    -Message $(if ($otaSendOwnerCheck) { '' } else { 'rdx_ble_server_ota_send() does not reject non-CONFIG owner' })

# 8) RDX App write/CCC checks reject OWNER_NONE and OWNER_HOGP by using != CONFIG
$rdxWriteCheckPattern = 'if\s*\(\s*s_ble_mode\.connection_owner\s*!=\s*RDX_BLE_OWNER_CONFIG\s*\)'
$rdxWriteChecks = [regex]::Matches($ServerText, $rdxWriteCheckPattern).Count
Add-CheckResult -Name 'C1_RDX_APP_OWNER_REJECTION' -Passed ($rdxWriteChecks -ge 2) `
    -Message $(if ($rdxWriteChecks -ge 2) { '' } else { "expected at least 2 '!= RDX_BLE_OWNER_CONFIG' owner checks for RDX App handles, found $rdxWriteChecks" })

# -----------------------------------------------------------------------------
# Summary
# -----------------------------------------------------------------------------
Write-Host '---------------------------'
if ($Failed -eq 0) {
    Write-Host "All $($CheckResults.Count) HOGP profile contract checks passed."
    exit 0
} else {
    Write-Host "$Failed of $($CheckResults.Count) HOGP profile contract checks failed."
    exit 1
}
