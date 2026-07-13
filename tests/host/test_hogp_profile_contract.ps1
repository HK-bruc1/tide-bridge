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
$ModeControllerPath = Join-Path $ProtocolDir 'rdx_ble_mode_controller.c'
$DutPath = Join-Path $ProtocolDir 'rdx_dut.c'
$KeyActionPath = Join-Path $ProtocolDir 'rdx_hogp_key_action.c'
$KeyActionHeaderPath = Join-Path $ProtocolDir 'rdx_hogp_key_action.h'
$KeyPath = Join-Path $ProtocolDir 'rdx_key.c'
$KeyHeaderPath = Join-Path $ProtocolDir 'rdx_key.h'
$AppConfigPath = Join-Path $ProtocolDir 'rdx_app_config.h'
$HogpConfigPath = Join-Path $ProtocolDir 'rdx_hogp_config.h'
$ProjectConfigPath = Join-Path $RepoRoot 'SDK/apps/earphone/include/t2620_project_config.h'

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

$ProfileConstantExprMap = @{}
$ProfileConstantValueCache = @{}
[regex]::Matches($HeaderText, '(?m)^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+)$') | ForEach-Object {
    $name = $_.Groups[1].Value
    $expr = $_.Groups[2].Value
    $expr = [regex]::Replace($expr, '/\*.*?\*/', '')
    $expr = $expr -replace '//.*$', ''
    $expr = $expr.Trim()
    if ($expr -and ($expr -notmatch '\\$')) {
        $ProfileConstantExprMap[$name] = $expr
    }
}

function Resolve-ProfileConstant {
    param(
        [Parameter(Mandatory)]
        [string]$Token
    )

    $normalized = $Token.Trim()
    if ($normalized.StartsWith('(') -and $normalized.EndsWith(')')) {
        $normalized = $normalized.Substring(1, $normalized.Length - 2).Trim()
    }

    if ($normalized -match '\|') {
        $value = 0
        foreach ($part in ($normalized -split '\|')) {
            $value = $value -bor (Resolve-ProfileConstant -Token $part)
        }
        return $value
    }

    if ($normalized -match '^0x[0-9A-Fa-f]+$') {
        return ConvertFrom-HexString -Value $normalized
    }
    if ($normalized -match '^\d+$') {
        return [int]$normalized
    }
    if ($ProfileConstantValueCache.ContainsKey($normalized)) {
        return $ProfileConstantValueCache[$normalized]
    }
    if ($ProfileConstantExprMap.ContainsKey($normalized)) {
        $resolved = Resolve-ProfileConstant -Token $ProfileConstantExprMap[$normalized]
        $ProfileConstantValueCache[$normalized] = $resolved
        return $resolved
    }

    throw "Unknown HOGP profile constant '$normalized'"
}

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
# CHECK: Input Report payload inside rdx_hogp_keyboard_report_send()
# -----------------------------------------------------------------------------
$KeyboardText = Get-Content -Raw -Path $KeyboardPath

$sendFunctionMatch = [regex]::Match($KeyboardText,
    '(?sm)int\s+rdx_hogp_keyboard_report_send\s*\([^)]*\)\s*\{(.*?)^\}')

if ($sendFunctionMatch.Success) {
    $sendFunctionBody = $sendFunctionMatch.Groups[1].Value

    $payloadPattern = 'u8\s+payload\s*\[\s*RDX_HOGP_KEYBOARD_REPORT_LEN\s*\]'
    $hasPayload = $sendFunctionBody -match $payloadPattern
    Add-CheckResult -Name 'INPUT_REPORT_LENGTH' -Passed $hasPayload `
        -Message $(if ($hasPayload) { '' } else { 'local payload array is not u8 payload[RDX_HOGP_KEYBOARD_REPORT_LEN]' })

    $sendCallPattern = 'app_ble_att_send_data\s*\([^,]+,\s*HID_INPUT_REPORT_VALUE_HANDLE\s*,\s*payload\s*,\s*sizeof\s*\(\s*payload\s*\)'
    $hasSendCall = $sendFunctionBody -match $sendCallPattern
    Add-CheckResult -Name 'NO_REPORT_ID_PREFIX' -Passed $hasSendCall `
        -Message $(if ($hasSendCall) { '' } else { 'notify call does not use (payload, sizeof(payload)) exactly' })
} else {
    Add-CheckResult -Name 'INPUT_REPORT_LENGTH' -Passed $false -Message 'rdx_hogp_keyboard_report_send() not found'
    Add-CheckResult -Name 'NO_REPORT_ID_PREFIX' -Passed $false -Message 'rdx_hogp_keyboard_report_send() not found'
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

function ConvertFrom-HogpAttributeMacro {
    param(
        [Parameter(Mandatory)]
        [string]$MacroName,

        [Parameter(Mandatory)]
        [string]$ArgumentText
    )

    $args = $ArgumentText -split ',' | ForEach-Object { $_.Trim() }
    switch ($MacroName) {
        'RDX_HOGP_ATT_PRIMARY_SERVICE_16' {
            if ($args.Count -ne 2) { throw "$MacroName expects 2 args" }
            return @{
                Handle      = Resolve-ProfileConstant $args[0]
                CommentType = 'PRIMARY_SERVICE'
                AttUuid     = Resolve-ProfileConstant 'RDX_HOGP_UUID_PRIMARY_SERVICE'
                ServiceUuid = Resolve-ProfileConstant $args[1]
            }
        }
        'RDX_HOGP_ATT_CHARACTERISTIC_16' {
            if ($args.Count -ne 4) { throw "$MacroName expects 4 args" }
            return @{
                Handle      = Resolve-ProfileConstant $args[0]
                CommentType = 'CHARACTERISTIC'
                AttUuid     = Resolve-ProfileConstant 'RDX_HOGP_UUID_CHARACTERISTIC'
                Properties  = Resolve-ProfileConstant $args[1]
                ValueHandle = Resolve-ProfileConstant $args[2]
                CharUuid    = Resolve-ProfileConstant $args[3]
            }
        }
        'RDX_HOGP_ATT_VALUE_16' {
            if ($args.Count -ne 3) { throw "$MacroName expects 3 args" }
            return @{
                Handle      = Resolve-ProfileConstant $args[0]
                CommentType = 'VALUE'
                AttUuid     = Resolve-ProfileConstant $args[2]
            }
        }
        'RDX_HOGP_ATT_VALUE_16_U8' {
            if ($args.Count -ne 4) { throw "$MacroName expects 4 args" }
            return @{
                Handle      = Resolve-ProfileConstant $args[0]
                CommentType = 'VALUE'
                AttUuid     = Resolve-ProfileConstant $args[2]
                Value       = Resolve-ProfileConstant $args[3]
            }
        }
        'RDX_HOGP_ATT_CCC' {
            if ($args.Count -ne 2) { throw "$MacroName expects 2 args" }
            return @{
                Handle      = Resolve-ProfileConstant $args[0]
                CommentType = 'CLIENT_CHARACTERISTIC_CONFIGURATION'
                AttUuid     = Resolve-ProfileConstant 'RDX_HOGP_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION'
                Value       = Resolve-ProfileConstant $args[1]
            }
        }
        'RDX_HOGP_ATT_REPORT_REFERENCE' {
            if ($args.Count -ne 3) { throw "$MacroName expects 3 args" }
            return @{
                Handle      = Resolve-ProfileConstant $args[0]
                CommentType = 'REPORT_REFERENCE'
                AttUuid     = Resolve-ProfileConstant 'RDX_HOGP_UUID_REPORT_REFERENCE'
                ReportId    = Resolve-ProfileConstant $args[1]
                ReportType  = Resolve-ProfileConstant $args[2]
            }
        }
        default {
            throw "Unsupported HOGP attribute macro '$MacroName'"
        }
    }
}

function Test-ProfileAttributeOrder {
    # Extract HID Service block from rdx_profile_data[]
    $blockMatch = [regex]::Match($ServerText,
        '(?s)//\s*0x0016\s+PRIMARY_SERVICE.*?(?=\r?\n\s*#endif\s*/\*\s*TCFG_RDX_HOGP_ENABLE\s*\*/)')

    if (-not $blockMatch.Success) {
        Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false -Message 'HID Service block not found in rdx_profile_data[]'
        return
    }

    $block = $blockMatch.Value
    $macroMatches = [regex]::Matches($block, '(?s)(RDX_HOGP_ATT_[A-Z0-9_]+)\s*\((.*?)\)')
    if ($macroMatches.Count -eq 0) {
        Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
            -Message 'HID Service block must use RDX_HOGP_ATT_* profile macros'
        return
    }

    $parsedAttributes = [System.Collections.Generic.List[hashtable]]::new()
    try {
        foreach ($match in $macroMatches) {
            $parsedAttributes.Add((ConvertFrom-HogpAttributeMacro `
                -MacroName $match.Groups[1].Value `
                -ArgumentText $match.Groups[2].Value))
        }
    } catch {
        Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false -Message $_.Exception.Message
        return
    }

    if ($parsedAttributes.Count -ne $ExpectedAttributes.Count) {
        Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
            -Message "expected $($ExpectedAttributes.Count) attributes, found $($parsedAttributes.Count)"
        return
    }

    for ($i = 0; $i -lt $ExpectedAttributes.Count; $i++) {
        $exp = $ExpectedAttributes[$i]
        $act = $parsedAttributes[$i]

        if ($act['Handle'] -ne $exp.Handle) {
            Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                -Message "attribute at index $i expected handle 0x$($exp.Handle.ToString('X4')) but found 0x$($act['Handle'].ToString('X4'))"
            return
        }
        if ($act['CommentType'] -ne $exp.Type) {
            Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                -Message "handle 0x$($exp.Handle.ToString('X4')) expected type $($exp.Type) but found $($act['CommentType'])"
            return
        }
        if ($act['AttUuid'] -ne $exp.AttUuid) {
            Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                -Message "handle 0x$($exp.Handle.ToString('X4')) expected att_uuid 0x$($exp.AttUuid.ToString('X4')) but found 0x$($act['AttUuid'].ToString('X4'))"
            return
        }

        switch ($exp.Type) {
            'PRIMARY_SERVICE' {
                if ($act['ServiceUuid'] -ne $exp.ServiceUuid) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) PRIMARY_SERVICE expected service_uuid 0x$($exp.ServiceUuid.ToString('X4')) but found 0x$($act['ServiceUuid'].ToString('X4'))"
                    return
                }
            }
            'CHARACTERISTIC' {
                if ($act['Properties'] -ne $exp.Properties) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) CHARACTERISTIC expected properties 0x$($exp.Properties.ToString('X2')) but found 0x$($act['Properties'].ToString('X2'))"
                    return
                }
                if ($act['ValueHandle'] -ne $exp.ValueHandle) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) CHARACTERISTIC expected value_handle 0x$($exp.ValueHandle.ToString('X4')) but found 0x$($act['ValueHandle'].ToString('X4'))"
                    return
                }
                if ($act['CharUuid'] -ne $exp.CharUuid) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) CHARACTERISTIC expected characteristic_uuid 0x$($exp.CharUuid.ToString('X4')) but found 0x$($act['CharUuid'].ToString('X4'))"
                    return
                }
            }
            'VALUE' {
                if ($exp.ContainsKey('Value') -and ($act['Value'] -ne $exp.Value)) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) VALUE expected value 0x$($exp.Value.ToString('X2')) but found 0x$($act['Value'].ToString('X2'))"
                    return
                }
            }
            'CLIENT_CHARACTERISTIC_CONFIGURATION' {
                if ($act['Value'] -ne $exp.Value) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) CCC expected value 0x$($exp.Value.ToString('X4')) but found 0x$($act['Value'].ToString('X4'))"
                    return
                }
            }
            'REPORT_REFERENCE' {
                if ($act['ReportId'] -ne $exp.ReportId -or $act['ReportType'] -ne $exp.ReportType) {
                    Add-CheckResult -Name 'PROFILE_ATTRIBUTE_ORDER' -Passed $false `
                        -Message "handle 0x$($exp.Handle.ToString('X4')) REPORT_REFERENCE expected (id=0x$($exp.ReportId.ToString('X2')), type=0x$($exp.ReportType.ToString('X2'))) but found (id=0x$($act['ReportId'].ToString('X2')), type=0x$($act['ReportType'].ToString('X2')))"
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
$outputReportGatedPattern = '(?s)#if\s+TCFG_RDX_HOGP_ENABLE\s*\r?\n\s*//\s*0x0028\s+CHARACTERISTIC\s+0x2A4D.*?RDX_HOGP_ATT_CHARACTERISTIC_16\s*\(\s*HID_OUTPUT_REPORT_CHARACTERISTIC_HANDLE.*?RDX_HOGP_ATT_VALUE_16_U8\s*\(\s*HID_OUTPUT_REPORT_VALUE_HANDLE.*?RDX_HOGP_ATT_REPORT_REFERENCE\s*\(\s*HID_OUTPUT_REPORT_REFERENCE_HANDLE\s*,\s*RDX_HOGP_OUTPUT_REPORT_ID\s*,\s*RDX_HOGP_OUTPUT_REPORT_TYPE\s*\).*?#endif\s*/\*\s*TCFG_RDX_HOGP_ENABLE\s*\*/'
$isOutputReportGated = $ServerText -match $outputReportGatedPattern
Add-CheckResult -Name 'OUTPUT_REPORT_GATED' -Passed $isOutputReportGated `
    -Message $(if ($isOutputReportGated) { '' } else { 'Output Report block (0x0028-0x002a) is not wrapped in #if TCFG_RDX_HOGP_ENABLE / #endif' })

$outputHandleMacrosOk = $HeaderText -match '#define\s+HID_OUTPUT_REPORT_CHARACTERISTIC_HANDLE\s+0x0028' -and
                        $HeaderText -match '#define\s+HID_OUTPUT_REPORT_VALUE_HANDLE\s+0x0029' -and
                        $HeaderText -match '#define\s+HID_OUTPUT_REPORT_REFERENCE_HANDLE\s+0x002a'
Add-CheckResult -Name 'OUTPUT_REPORT_HANDLE_MACROS' -Passed $outputHandleMacrosOk `
    -Message $(if ($outputHandleMacrosOk) { '' } else { 'Output Report handles 0x0028-0x002a must be defined in rdx_hogp_profile.h' })

# -----------------------------------------------------------------------------
# Phase 6 C1 checks: mode controller and owner authorization
# -----------------------------------------------------------------------------
$ServerHPath = Join-Path $ProtocolDir 'rdx_ble_server.h'
$ServerHText = Get-Content -Raw -Path $ServerHPath

# Build a comment-stripped view of server.c so static checks do not treat
# commented-out type definitions as valid code.
$serverCodeOnly = [regex]::Replace($ServerText, '/\*.*?\*/', '', [System.Text.RegularExpressions.RegexOptions]::Singleline)
$ModeControllerText = Get-Content -Raw -Path $ModeControllerPath
$modeControllerCodeOnly = [regex]::Replace($ModeControllerText, '/\*.*?\*/', '', [System.Text.RegularExpressions.RegexOptions]::Singleline)

# 1) Private mode controller lives in its own file, not in server.c or public header
$hasPrivateController = $modeControllerCodeOnly -match 's_ble_mode'
$serverHasController = $serverCodeOnly -match 'static\s+rdx_ble_mode_controller_t\s+s_ble_mode'
Add-CheckResult -Name 'C1_PRIVATE_MODE_CONTROLLER' -Passed ($hasPrivateController -and -not $serverHasController) `
    -Message $(if ($hasPrivateController -and -not $serverHasController) { '' } else { 'mode controller state must live in rdx_ble_mode_controller.c, not rdx_ble_server.c' })

$ModeControllerHPath = Join-Path $ProtocolDir 'rdx_ble_mode_controller.h'
$ModeControllerHText = Get-Content -Raw -Path $ModeControllerHPath
$modeControllerHCodeOnly = [regex]::Replace($ModeControllerHText, '/\*.*?\*/', '', [System.Text.RegularExpressions.RegexOptions]::Singleline)
$modeTypeDefinedInCode = ($modeControllerHCodeOnly -match 'typedef\s+enum\s*\{\s*RDX_BLE_MODE_CONFIG\s*=\s*0,\s*RDX_BLE_MODE_HOGP,\s*\}\s*rdx_ble_mode_t') -and
                         ($modeControllerHCodeOnly -match 'typedef\s+enum\s*\{\s*RDX_BLE_OWNER_NONE\s*=\s*0,\s*RDX_BLE_OWNER_CONFIG,\s*RDX_BLE_OWNER_HOGP,\s*\}\s*rdx_ble_connection_owner_t')
Add-CheckResult -Name 'C1_MODE_TYPES_DEFINED_IN_CODE' -Passed $modeTypeDefinedInCode `
    -Message $(if ($modeTypeDefinedInCode) { '' } else { 'rdx_ble_mode_t / rdx_ble_connection_owner_t not defined in rdx_ble_mode_controller.h' })

$hasPublicModeField = $ServerHText -match 'rdx_ble_mode_t|rdx_ble_connection_owner_t'
Add-CheckResult -Name 'C1_MODE_TYPES_NOT_PUBLIC' -Passed (-not $hasPublicModeField) `
    -Message $(if (-not $hasPublicModeField) { '' } else { 'rdx_ble_mode_t or rdx_ble_connection_owner_t found in public header rdx_ble_server.h' })

# 2) Public wrappers declared in header and implemented in server.c
$hasModeRequestDecl = $ServerHText -match 'void\s+rdx_ble_mode_request_hogp\s*\(\s*u8\s+enable\s*\)'
$hasOwnerCheckDecl = $ServerHText -match 'u8\s+rdx_ble_connection_owner_is_hogp\s*\(\s*void\s*\)'
Add-CheckResult -Name 'C1_PUBLIC_WRAPPERS_DECLARED' -Passed ($hasModeRequestDecl -and $hasOwnerCheckDecl) `
    -Message $(if ($hasModeRequestDecl -and $hasOwnerCheckDecl) { '' } else { 'rdx_ble_mode_request_hogp or rdx_ble_connection_owner_is_hogp not declared in rdx_ble_server.h' })

$hasModeRequestImpl = $serverCodeOnly -match 'void\s+rdx_ble_mode_request_hogp\s*\(\s*u8\s+enable\s*\)'
$hasOwnerCheckImpl = $modeControllerCodeOnly -match 'u8\s+rdx_ble_connection_owner_is_hogp\s*\(\s*void\s*\)'
Add-CheckResult -Name 'C1_PUBLIC_WRAPPERS_IMPLEMENTED' -Passed ($hasModeRequestImpl -and $hasOwnerCheckImpl) `
    -Message $(if ($hasModeRequestImpl -and $hasOwnerCheckImpl) { '' } else { 'rdx_ble_mode_request_hogp must be implemented in rdx_ble_server.c; rdx_ble_connection_owner_is_hogp must be implemented in rdx_ble_mode_controller.c' })

# 2b) Public request facades must trigger server-side disconnect/apply logic
$requestHogpBodyMatch = [regex]::Match($serverCodeOnly,
    '(?s)void\s+rdx_ble_mode_request_hogp\s*\(\s*u8\s+enable\s*\)\s*\{(.*?)\}')
$requestToggleBodyMatch = [regex]::Match($serverCodeOnly,
    '(?s)void\s+rdx_ble_mode_request_toggle\s*\(\s*void\s*\)\s*\{(.*?)\}')
$requestFacadesDriveAction = $requestHogpBodyMatch.Success -and
                             $requestHogpBodyMatch.Groups[1].Value -match 'rdx_ble_mode_request\s*\(' -and
                             $requestToggleBodyMatch.Success -and
                             $requestToggleBodyMatch.Groups[1].Value -match 'rdx_ble_mode_request\s*\('
Add-CheckResult -Name 'C1_PUBLIC_REQUEST_TRIGGERS_SERVER_ACTION' -Passed $requestFacadesDriveAction `
    -Message $(if ($requestFacadesDriveAction) { '' } else { 'rdx_ble_mode_request_hogp() and rdx_ble_mode_request_toggle() in rdx_ble_server.c must call the server-side rdx_ble_mode_request() helper' })

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
    $hasPrevOwner = $disconnBlock -match 'prev_owner\s*=\s*rdx_ble_connection_owner_get\s*\('
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
    $hasPendingClear = $applyInternalBody -match 'rdx_ble_mode_clear_pending\s*\('
    $hasAdvUpdate = $applyInternalBody -match 'rdx_ble_mode_set_advertised\s*\(\s*rdx_ble_mode_get_requested\s*\(\s*\)\s*\)'
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
$advertisingApiPattern = 'rdx_ble_server_adv_enable|rdx_ble_mode_start_config_advertising|rdx_ble_mode_start_hogp_advertising|rdx_ble_mode_restart_hogp_advertising|rdx_hogp_adv_start|rdx_hogp_adv_stop|hogp_adv_start_internal|hogp_adv_stop_internal|rdx_hogp_mode_set|app_ble_adv_enable|app_ble_adv_data_set'

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
    $hasConfigCheck = $hogpClearBody -match 'rdx_ble_mode_get_advertised\s*\(\s*\)\s*==\s*RDX_BLE_MODE_CONFIG'
    $hasHogpGet = $hogpClearBody -match 'rdx_hogp_mode_get\s*\('
    $hasRuntimeCleanup = $hogpClearBody -match 'rdx_hogp_runtime_cleanup\s*\('
    $syncHasNoAdvertising = $hogpClearBody -notmatch $advertisingApiPattern
    $cleanupHasNoAdvertising = $runtimeCleanupInKeyboardMatch.Groups[1].Value -notmatch $advertisingApiPattern
    $hogpClearOk = $hasConfigCheck -and $hasHogpGet -and $hasRuntimeCleanup -and $applyInternalCallsSync -and $syncHasNoAdvertising -and $cleanupHasNoAdvertising
    $parts = @()
    if (-not $hasConfigCheck) { $parts += 'CONFIG branch' }
    if (-not $hasHogpGet) { $parts += 'rdx_hogp_mode_get()' }
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

# 4c.1) HOGP keyboard module must not own BLE Server lifecycle decisions
$KeyboardHeaderTextEarly = Get-Content -Raw -Path (Join-Path $ProtocolDir 'rdx_hogp_keyboard.h')
$hogpNoServerLifecycle = ($keyboardCodeOnly -notmatch 'rdx_ble_server_app_disconnect') -and
                         ($keyboardCodeOnly -notmatch 'rdx_ble_server_adv_enable') -and
                         ($keyboardCodeOnly -notmatch 'rdx_ble_server_get_local_name') -and
                         ($keyboardCodeOnly -notmatch 'rdx_ble_server_get_info\s*\(\)\s*->\s*ble_conn') -and
                         ($keyboardCodeOnly -notmatch 'rdx_ble_server_get_info\s*\(\)\s*->\s*adv_interval_min') -and
                         ($KeyboardText -notmatch '#include\s+"rdx_ble_server\.h"')
Add-CheckResult -Name 'C1_HOGP_NO_SERVER_LIFECYCLE_CALLS' -Passed $hogpNoServerLifecycle `
    -Message $(if ($hogpNoServerLifecycle) { '' } else { 'rdx_hogp_keyboard.c must not include rdx_ble_server.h or call Server disconnect/adv_enable/local_name or read ble_conn/adv_interval_min' })

# 4c.2) Mode controller must not depend on BLE Server internals
$modeControllerBoundaryOk = ($modeControllerCodeOnly -notmatch 'rdx_ble_server_app_disconnect') -and
                           ($modeControllerCodeOnly -notmatch 'rdx_ble_server_adv_enable') -and
                           ($modeControllerCodeOnly -notmatch 'g_rdx_ble_server_info') -and
                           ($ModeControllerText -notmatch '#include\s+"rdx_ble_server\.h"')
Add-CheckResult -Name 'C1_MODE_CONTROLLER_NO_SERVER_DEP' -Passed $modeControllerBoundaryOk `
    -Message $(if ($modeControllerBoundaryOk) { '' } else { 'rdx_ble_mode_controller.c must not include rdx_ble_server.h or depend on Server internals' })

$hogpAdvStartTakesContext = $KeyboardHeaderTextEarly -match 'void\s+rdx_hogp_adv_start\s*\(\s*u16\s+adv_interval_min\s*,\s*const\s+char\s+\*\s*local_name\s*\)'
$serverPassesAdvContext = $ServerText -match 'rdx_hogp_adv_start\s*\(\s*g_rdx_ble_server_info\.adv_interval_min\s*,\s*rdx_ble_server_get_local_name\s*\(\s*\)\s*\)'
Add-CheckResult -Name 'C1_HOGP_ADV_CONTEXT_OWNED_BY_SERVER' -Passed ($hogpAdvStartTakesContext -and $serverPassesAdvContext) `
    -Message $(if ($hogpAdvStartTakesContext -and $serverPassesAdvContext) { '' } else { 'Server must pass adv_interval_min and local name into rdx_hogp_adv_start()' })

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
    $hasNoHogpModeSet = $configStartBody -notmatch '\brdx_hogp_mode_set\s*\('
    $configStartOk = $hasAdvDisable -and $hasAdvEnable -and $hasNoHogpModeSet
    $parts = @()
    if (-not $hasAdvDisable) { $parts += 'disable advertising' }
    if (-not $hasAdvEnable) { $parts += 'enable RDX advertising' }
    if (-not $hasNoHogpModeSet) { $parts += 'must not call rdx_hogp_mode_set()' }
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
    $hasHogpBranch = ($beforeAdvOff -match 'rdx_ble_mode_get_advertised\s*\(\s*\)\s*==\s*RDX_BLE_MODE_HOGP') -and
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
    $sendOwnerCheck = $sendBody -match 'rdx_ble_connection_owner_get\s*\(\s*\)\s*!=\s*RDX_BLE_OWNER_CONFIG'
}
Add-CheckResult -Name 'C1_SERVER_SEND_OWNER_CHECK' -Passed $sendOwnerCheck `
    -Message $(if ($sendOwnerCheck) { '' } else { 'rdx_ble_server_send() does not reject non-CONFIG owner' })

$otaSendFunctionMatch = [regex]::Match($ServerText,
    '(?sm)int\s+rdx_ble_server_ota_send\s*\([^)]*\)\s*\{(.*?)^\}')
$otaSendOwnerCheck = $false
if ($otaSendFunctionMatch.Success) {
    $otaSendBody = $otaSendFunctionMatch.Groups[1].Value
    $otaSendOwnerCheck = $otaSendBody -match 'rdx_ble_connection_owner_get\s*\(\s*\)\s*!=\s*RDX_BLE_OWNER_CONFIG'
}
Add-CheckResult -Name 'C1_OTA_SEND_OWNER_CHECK' -Passed $otaSendOwnerCheck `
    -Message $(if ($otaSendOwnerCheck) { '' } else { 'rdx_ble_server_ota_send() does not reject non-CONFIG owner' })

# 8) RDX App write/CCC checks reject OWNER_NONE and OWNER_HOGP by using != CONFIG
$rdxWriteCheckPattern = 'if\s*\(\s*rdx_ble_connection_owner_get\s*\(\s*\)\s*!=\s*RDX_BLE_OWNER_CONFIG\s*\)'
$rdxWriteChecks = [regex]::Matches($ServerText, $rdxWriteCheckPattern).Count
Add-CheckResult -Name 'C1_RDX_APP_OWNER_REJECTION' -Passed ($rdxWriteChecks -ge 2) `
    -Message $(if ($rdxWriteChecks -ge 2) { '' } else { "expected at least 2 '!= RDX_BLE_OWNER_CONFIG' owner checks for RDX App handles, found $rdxWriteChecks" })

# -----------------------------------------------------------------------------
# C3 CHECKS: module boundary and Report API
# -----------------------------------------------------------------------------
$ServerHeaderText = Get-Content -Raw -Path (Join-Path $ProtocolDir 'rdx_ble_server.h')
$KeyboardHeaderText = Get-Content -Raw -Path (Join-Path $ProtocolDir 'rdx_hogp_keyboard.h')
$KeyboardCText = Get-Content -Raw -Path (Join-Path $ProtocolDir 'rdx_hogp_keyboard.c')
$AppCText = Get-Content -Raw -Path (Join-Path $ProtocolDir 'rdx_app.c')

# C3.1 Include boundaries
$serverHeaderIncludesHogp = $ServerHeaderText -match '#include\s+"rdx_hogp_keyboard\.h"'
Add-CheckResult -Name 'C3_SERVER_HEADER_NO_TRANSITIVE_HOGP' -Passed (-not $serverHeaderIncludesHogp) `
    -Message 'rdx_ble_server.h must not include rdx_hogp_keyboard.h'

$keyboardHeaderIncludesConfig = $KeyboardHeaderText -match '#include\s+"rdx_hogp_config\.h"'
Add-CheckResult -Name 'C3_KEYBOARD_HEADER_NO_CONFIG' -Passed (-not $keyboardHeaderIncludesConfig) `
    -Message 'rdx_hogp_keyboard.h must not include rdx_hogp_config.h'

$keyboardCIncludesConfig = $KeyboardCText -match '#include\s+"rdx_hogp_config\.h"'
Add-CheckResult -Name 'C3_KEYBOARD_C_INCLUDES_CONFIG' -Passed $keyboardCIncludesConfig `
    -Message 'rdx_hogp_keyboard.c must explicitly include rdx_hogp_config.h'

# C3.2 Report API surface
$hasReportLen = $KeyboardHeaderText -match '#define\s+RDX_HOGP_KEYBOARD_REPORT_LEN\s+8'
Add-CheckResult -Name 'C3_REPORT_LEN_8' -Passed $hasReportLen `
    -Message 'RDX_HOGP_KEYBOARD_REPORT_LEN must be 8'

$hasReportType = $KeyboardHeaderText -match 'typedef\s+struct\s*\{\s*u8\s+modifiers;\s*u8\s+reserved;\s*u8\s+usages\[6\];\s*\}\s*rdx_hogp_keyboard_report_t'
Add-CheckResult -Name 'C3_REPORT_TYPE' -Passed $hasReportType `
    -Message 'rdx_hogp_keyboard_report_t must have modifiers/reserved/usages[6]'

$hasReportSend = $KeyboardHeaderText -match 'int\s+rdx_hogp_keyboard_report_send\s*\(\s*const\s+rdx_hogp_keyboard_report_t\s*\*\s*report\s*\)'
Add-CheckResult -Name 'C3_REPORT_SEND_DECL' -Passed $hasReportSend `
    -Message 'rdx_hogp_keyboard_report_send() declaration missing or wrong signature'

$hasReleaseAll = $KeyboardHeaderText -match 'int\s+rdx_hogp_keyboard_release_all\s*\(\s*void\s*\)'
Add-CheckResult -Name 'C3_RELEASE_ALL_DECL' -Passed $hasReleaseAll `
    -Message 'rdx_hogp_keyboard_release_all() declaration missing or wrong signature'

$hasIsConnected = $KeyboardHeaderText -match 'u8\s+rdx_hogp_keyboard_is_connected\s*\(\s*void\s*\)'
Add-CheckResult -Name 'C3_IS_CONNECTED_DECL' -Passed $hasIsConnected `
    -Message 'rdx_hogp_keyboard_is_connected() declaration missing or wrong signature'

$hasIsReady = $KeyboardHeaderText -match 'u8\s+rdx_hogp_keyboard_is_ready\s*\(\s*void\s*\)'
Add-CheckResult -Name 'C3_IS_READY_DECL' -Passed $hasIsReady `
    -Message 'rdx_hogp_keyboard_is_ready() declaration missing or wrong signature'

$releaseAllBodyMatch = [regex]::Match($KeyboardCText,
    '(?sm)int\s+rdx_hogp_keyboard_release_all\s*\(\s*void\s*\)\s*\{(.*?)^\}')
if ($releaseAllBodyMatch.Success) {
    $releaseAllBody = $releaseAllBodyMatch.Groups[1].Value
    $releaseAllZero = $releaseAllBody -match 'rdx_hogp_keyboard_report_t\s+report\s*=\s*\{0\}'
    Add-CheckResult -Name 'C3_RELEASE_ALL_ZERO' -Passed $releaseAllZero `
        -Message 'release_all must build and send a zero-initialized report'
} else {
    Add-CheckResult -Name 'C3_RELEASE_ALL_ZERO' -Passed $false -Message 'rdx_hogp_keyboard_release_all() not found'
}

# C3.3 HOGP module no longer knows physical keys or legacy wrappers
$forbiddenPatterns = @(
    'KEY_IO_NUM',
    'KEY_ACTION_',
    'key_to_hid_usage',
    'RDX_HOGP_KEYMAP_',
    'rdx_hogp_on_io_num_key',
    'rdx_hogp_key_click_index',
    'hogp_key_send',
    'hogp_key_click_send'
)

foreach ($pattern in $forbiddenPatterns) {
    $found = $KeyboardCText -match [regex]::Escape($pattern)
    Add-CheckResult -Name "C3_KEYBOARD_NO_$pattern" -Passed (-not $found) `
        -Message "forbidden pattern '$pattern' found in rdx_hogp_keyboard.c"
}

# C3.4 BLE Server uses only formal rdx_hogp_mode_get/set
$legacyModeGet = [regex]::Match($ServerText, '(?<!rdx_)\bhogp_mode_get\b')
$legacyModeSet = [regex]::Match($ServerText, '(?<!rdx_)\bhogp_mode_set\b')
Add-CheckResult -Name 'C3_SERVER_NO_LEGACY_HOGP_MODE_GET' -Passed (-not $legacyModeGet.Success) `
    -Message 'rdx_ble_server.c must not call legacy hogp_mode_get()'
Add-CheckResult -Name 'C3_SERVER_NO_LEGACY_HOGP_MODE_SET' -Passed (-not $legacyModeSet.Success) `
    -Message 'rdx_ble_server.c must not call legacy hogp_mode_set()'

$formalModeGet = $ServerText -match '\brdx_hogp_mode_get\b'
$formalModeSet = $ServerText -match '\brdx_hogp_mode_set\b'
Add-CheckResult -Name 'C3_SERVER_USES_FORMAL_MODE_GET' -Passed $formalModeGet `
    -Message 'rdx_ble_server.c must call rdx_hogp_mode_get()'
Add-CheckResult -Name 'C3_SERVER_USES_FORMAL_MODE_SET' -Passed $formalModeSet `
    -Message 'rdx_ble_server.c must call rdx_hogp_mode_set()'

# C3.5 rdx_app.c hosts the temporary NUM debug adapter
$appIncludesHogp = $AppCText -match '#include\s+"rdx_hogp_keyboard\.h"'
Add-CheckResult -Name 'C3_APP_INCLUDES_HOGP_HEADER' -Passed $appIncludesHogp `
    -Message 'rdx_app.c must explicitly include rdx_hogp_keyboard.h'

$appUsesOldEntry = $AppCText -match 'rdx_hogp_on_io_num_key'
Add-CheckResult -Name 'C3_APP_NO_OLD_HOGP_ENTRY' -Passed (-not $appUsesOldEntry) `
    -Message 'rdx_app.c must not call rdx_hogp_on_io_num_key()'

$appNoDebugAdapter = $AppCText -notmatch 'rdx_app_hogp_debug_num_key'
Add-CheckResult -Name 'C3_APP_DEBUG_ADAPTER_REMOVED' -Passed $appNoDebugAdapter `
    -Message $(if ($appNoDebugAdapter) { '' } else { 'rdx_app.c must remove the temporary rdx_app_hogp_debug_num_key() adapter' })

# C3.6 Disabled stubs cover all public declarations
$disabledBranchMatch = [regex]::Match($KeyboardCText,
    '(?sm)#else[^\r\n]*TCFG_RDX_HOGP_ENABLE[^\r\n]*stubs[^\r\n]*\r?\n(.*?)#endif[^\r\n]*TCFG_RDX_HOGP_ENABLE')

if ($disabledBranchMatch.Success) {
    $disabledBranch = $disabledBranchMatch.Groups[1].Value
    $requiredStubs = @(
        'rdx_hogp_init',
        'rdx_hogp_deinit',
        'rdx_hogp_runtime_cleanup',
        'rdx_hogp_mode_get',
        'rdx_hogp_mode_set',
        'rdx_hogp_is_handle',
        'rdx_hogp_att_read',
        'rdx_hogp_att_write',
        'rdx_hogp_on_connected',
        'rdx_hogp_on_disconnected',
        'rdx_hogp_on_encryption_change',
        'rdx_hogp_on_sm_event',
        'rdx_hogp_fill_adv_data',
        'rdx_hogp_adv_start',
        'rdx_hogp_adv_stop',
        'rdx_hogp_dump_state',
        'rdx_hogp_keyboard_report_send',
        'rdx_hogp_keyboard_release_all',
        'rdx_hogp_keyboard_is_connected',
        'rdx_hogp_keyboard_is_ready'
    )

    foreach ($stub in $requiredStubs) {
        $found = $disabledBranch -match [regex]::Escape($stub)
        Add-CheckResult -Name "C3_DISABLED_STUB_$stub" -Passed $found `
            -Message "disabled branch missing stub for $stub"
    }
} else {
    Add-CheckResult -Name 'C3_DISABLED_STUB_BRANCH' -Passed $false `
        -Message 'could not locate disabled stub branch in rdx_hogp_keyboard.c'
}

# -----------------------------------------------------------------------------
# C4 CHECKS: protocol state and Profile data convergence
# -----------------------------------------------------------------------------

# C4.1 Input Report current value is synchronized only on notify success
$reportSendBodyMatch = [regex]::Match($KeyboardCText,
    '(?sm)int\s+rdx_hogp_keyboard_report_send\s*\([^)]*\)\s*\{(.*?)^\}')
$currentReportSyncOk = $false
if ($reportSendBodyMatch.Success) {
    $sendBody = $reportSendBodyMatch.Groups[1].Value
    $currentReportSyncOk = $sendBody -match 'if\s*\(\s*ret\s*==\s*APP_BLE_NO_ERROR\s*\)' -and
                           $sendBody -match 'rdx_hogp_current_report_set\s*\(\s*payload\s*,\s*sizeof\s*\(\s*payload\s*\)\s*\)'
}
Add-CheckResult -Name 'C4_CURRENT_REPORT_SYNC' -Passed $currentReportSyncOk `
    -Message $(if ($currentReportSyncOk) { '' } else { 'rdx_hogp_keyboard_report_send() does not update s_hid_input_report on success only' })

# C4.2 release_all still builds a zero report and sends through the unified API
$releaseAllBodyMatch = [regex]::Match($KeyboardCText,
    '(?sm)int\s+rdx_hogp_keyboard_release_all\s*\(\s*void\s*\)\s*\{(.*?)^\}')
$releaseAllUnifiedOk = $false
if ($releaseAllBodyMatch.Success) {
    $releaseAllBody = $releaseAllBodyMatch.Groups[1].Value
    $releaseAllUnifiedOk = $releaseAllBody -match 'rdx_hogp_keyboard_report_t\s+report\s*=\s*\{0\}' -and
                           $releaseAllBody -match 'rdx_hogp_keyboard_report_send\s*\(\s*&report\s*\)'
}
Add-CheckResult -Name 'C4_RELEASE_ZERO_REPORT' -Passed $releaseAllUnifiedOk `
    -Message $(if ($releaseAllUnifiedOk) { '' } else { 'release_all must zero-initialize a report and send via rdx_hogp_keyboard_report_send()' })

# C4.3 Connection query and is_ready() expose the correct protocol state
$isConnectedBodyMatch = [regex]::Match($KeyboardCText,
    '(?sm)u8\s+rdx_hogp_keyboard_is_connected\s*\(\s*void\s*\)\s*\{(.*?)^\}')
$connectedChecksState = $false
if ($isConnectedBodyMatch.Success) {
    $connectedBody = $isConnectedBodyMatch.Groups[1].Value
    $connectedChecksState = $connectedBody -match 's_hogp_connected' -and
                            $connectedBody -match 'rdx_ble_connection_owner_is_hogp\s*\('
}
Add-CheckResult -Name 'C4_CONNECTED_CHECKS_STATE_AND_OWNER' -Passed $connectedChecksState `
    -Message $(if ($connectedChecksState) { '' } else { 'rdx_hogp_keyboard_is_connected() must check HOGP connection state and owner' })

$isReadyBodyMatch = [regex]::Match($KeyboardCText,
    '(?sm)u8\s+rdx_hogp_keyboard_is_ready\s*\(\s*void\s*\)\s*\{(.*?)^\}')
$readyChecksSuspend = $false
if ($isReadyBodyMatch.Success) {
    $readyBody = $isReadyBodyMatch.Groups[1].Value
    $readyChecksSuspend = $readyBody -match 'if\s*\(\s*s_hogp_suspended\s*\)'
}
Add-CheckResult -Name 'C4_READY_CHECKS_SUSPEND' -Passed $readyChecksSuspend `
    -Message $(if ($readyChecksSuspend) { '' } else { 'rdx_hogp_keyboard_is_ready() does not check s_hogp_suspended' })

# C4.4 Protocol Mode write is validated and restricted to 0/1
$attWriteBodyMatch = [regex]::Match($KeyboardCText,
    '(?sm)int\s+rdx_hogp_att_write\s*\([^)]*\)\s*\{(.*?)^\}')
$protocolModeWriteOk = $false
if ($attWriteBodyMatch.Success) {
    $attWriteBody = $attWriteBodyMatch.Groups[1].Value
    $hasCase = $attWriteBody -match 'case\s+HID_PROTOCOL_MODE_VALUE_HANDLE\s*:'
    $hasOffsetCheck = $attWriteBody -match 'offset\s*!=\s*0'
    $hasLengthCheck = $attWriteBody -match 'buffer_size\s*!=\s*1'
    $hasValueCheck = $attWriteBody -match 'RDX_HOGP_PROTOCOL_MODE_BOOT' -and
                     $attWriteBody -match 'RDX_HOGP_PROTOCOL_MODE_REPORT'
    $hasErrorReturn = $attWriteBody -match 'RDX_HOGP_ATT_ERR_INVALID_OFFSET' -and
                      $attWriteBody -match 'RDX_HOGP_ATT_ERR_INVALID_ATTRIBUTE_VALUE_LEN' -and
                      $attWriteBody -match 'RDX_HOGP_ATT_ERR_VALUE_NOT_ALLOWED'
    $protocolModeWriteOk = $hasCase -and $hasOffsetCheck -and $hasLengthCheck -and $hasValueCheck -and $hasErrorReturn
}
Add-CheckResult -Name 'C4_PROTOCOL_MODE_WRITE' -Passed $protocolModeWriteOk `
    -Message $(if ($protocolModeWriteOk) { '' } else { 'rdx_hogp_att_write() does not fully validate HID_PROTOCOL_MODE_VALUE_HANDLE writes' })

# C4.5 HID Control Point updates suspend state for 0/1
$controlPointSuspendOk = $false
if ($attWriteBodyMatch.Success) {
    $attWriteBody = $attWriteBodyMatch.Groups[1].Value
    $hasCase = $attWriteBody -match 'case\s+HID_CONTROL_POINT_VALUE_HANDLE\s*:'
    $hasSuspendSet = $attWriteBody -match 's_hogp_suspended\s*=\s*1'
    $hasExitSuspendSet = $attWriteBody -match 's_hogp_suspended\s*=\s*0'
    $hasSuspendConst = $attWriteBody -match 'RDX_HOGP_CONTROL_POINT_SUSPEND' -and
                       $attWriteBody -match 'RDX_HOGP_CONTROL_POINT_EXIT_SUSPEND'
    $controlPointSuspendOk = $hasCase -and $hasSuspendSet -and $hasExitSuspendSet -and $hasSuspendConst
}
Add-CheckResult -Name 'C4_CONTROL_POINT_SUSPEND' -Passed $controlPointSuspendOk `
    -Message $(if ($controlPointSuspendOk) { '' } else { 'rdx_hogp_att_write() does not update s_hogp_suspended on HID_CONTROL_POINT_VALUE_HANDLE writes' })

# C4.6 Encryption change assigns encrypted fully and clears on failure/disable
$encChangeBodyMatch = [regex]::Match($KeyboardCText,
    '(?sm)void\s+rdx_hogp_on_encryption_change\s*\([^)]*\)\s*\{(.*?)^\}')
$encryptionAssignmentOk = $false
if ($encChangeBodyMatch.Success) {
    $encChangeBody = $encChangeBodyMatch.Groups[1].Value
    $hasStaleHandleGuard = $encChangeBody -match 'con_handle\s*!=\s*s_hid_con_handle'
    $hasFullAssignment = $encChangeBody -match 's_hogp_encrypted\s*=\s*\(\s*enabled\s*&&\s*status\s*==\s*0\s*\)\s*\?\s*1\s*:\s*0'
    $hasClearOnFail = $encChangeBody -match 'if\s*\(\s*!\s*s_hogp_encrypted\s*\)' -and
                      $encChangeBody -match 'rdx_hogp_current_report_clear\s*\('
    $encryptionAssignmentOk = $hasStaleHandleGuard -and $hasFullAssignment -and $hasClearOnFail
}
Add-CheckResult -Name 'C4_ENCRYPTION_ASSIGNMENT' -Passed $encryptionAssignmentOk `
    -Message $(if ($encryptionAssignmentOk) { '' } else { 'rdx_hogp_on_encryption_change() does not fully assign encrypted or clear report on failure' })

# C4.7 Advertising data uses capacity-checking append helpers
$advFillBodyMatch = [regex]::Match($KeyboardCText,
    '(?sm)int\s+rdx_hogp_fill_adv_data\s*\([^)]*\)\s*\{(.*?)^\}')
$advCapacityOk = $false
if ($advFillBodyMatch.Success) {
    $advFillBody = $advFillBodyMatch.Groups[1].Value
    $hasAppendData = $advFillBody -match 'rdx_hogp_adv_append_data\s*\('
    $hasAppendVal = $advFillBody -match 'rdx_hogp_adv_append_val\s*\('
    $noUnderflow = $advFillBody -notmatch 'max_len\s*-\s*offset\s*-\s*2'
    $advCapacityOk = $hasAppendData -and $hasAppendVal -and $noUnderflow
}
Add-CheckResult -Name 'C4_ADV_CAPACITY_CHECK' -Passed $advCapacityOk `
    -Message $(if ($advCapacityOk) { '' } else { 'rdx_hogp_fill_adv_data() must use append helpers and avoid max_len - offset - 2' })

# C4.8 Module uses local ATT error constants, not undefined SDK macros
$noUndefinedAttError = ($KeyboardCText -notmatch 'ATT_ERROR_INVALID_HANDLE_VALUE') -and
                       ($KeyboardCText -notmatch 'ATT_ERROR_INVALID_OFFSET') -and
                       ($KeyboardCText -notmatch 'ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LEN') -and
                       ($KeyboardCText -notmatch 'ATT_ERROR_VALUE_NOT_ALLOWED')
Add-CheckResult -Name 'C4_NO_UNDEFINED_ATT_ERROR' -Passed $noUndefinedAttError `
    -Message $(if ($noUndefinedAttError) { '' } else { 'rdx_hogp_keyboard.c must not reference undefined ATT_ERROR_* macros' })

# C4.9 Profile header exposes HID UUIDs and default Protocol Mode
$headerHasUuids = $HeaderText -match '#define\s+RDX_HOGP_UUID_HID_SERVICE\s+0x1812' -and
                  $HeaderText -match '#define\s+RDX_HOGP_UUID_PROTOCOL_MODE\s+0x2A4E' -and
                  $HeaderText -match '#define\s+RDX_HOGP_UUID_REPORT\s+0x2A4D' -and
                  $HeaderText -match '#define\s+RDX_HOGP_UUID_REPORT_MAP\s+0x2A4B' -and
                  $HeaderText -match '#define\s+RDX_HOGP_UUID_HID_INFORMATION\s+0x2A4A' -and
                  $HeaderText -match '#define\s+RDX_HOGP_UUID_HID_CONTROL_POINT\s+0x2A4C' -and
                  $HeaderText -match '#define\s+RDX_HOGP_UUID_REPORT_REFERENCE\s+0x2908' -and
                  $HeaderText -match '#define\s+RDX_HOGP_UUID_CLIENT_CHARACTERISTIC_CONFIGURATION\s+0x2902'
$headerHasDefaultProtocolMode = $HeaderText -match '#define\s+RDX_HOGP_PROTOCOL_MODE_DEFAULT\s+0x01'
Add-CheckResult -Name 'C4_PROFILE_CONSTANTS' -Passed ($headerHasUuids -and $headerHasDefaultProtocolMode) `
    -Message $(if ($headerHasUuids -and $headerHasDefaultProtocolMode) { '' } else { 'rdx_hogp_profile.h must define RDX_HOGP_UUID_* and RDX_HOGP_PROTOCOL_MODE_DEFAULT' })

# -----------------------------------------------------------------------------
# C5 CHECKS: default HOGP boot and Key Action test skeleton
# -----------------------------------------------------------------------------
$KeyActionText = Get-Content -Raw -Path $KeyActionPath
$KeyActionHeaderText = Get-Content -Raw -Path $KeyActionHeaderPath
$KeyText = Get-Content -Raw -Path $KeyPath
$KeyHeaderText = Get-Content -Raw -Path $KeyHeaderPath
$AppConfigText = Get-Content -Raw -Path $AppConfigPath
$HogpConfigText = Get-Content -Raw -Path $HogpConfigPath
$ProjectConfigText = Get-Content -Raw -Path $ProjectConfigPath

# C5.1 Project config enables default HOGP and test keymap
$projectDefaultModeOk = $ProjectConfigText -match '#define\s+RDX_BLE_DEFAULT_MODE\s+RDX_BLE_DEFAULT_MODE_HOGP'
Add-CheckResult -Name 'C5_PROJECT_DEFAULT_MODE_HOGP' -Passed $projectDefaultModeOk `
    -Message $(if ($projectDefaultModeOk) { '' } else { 't2620_project_config.h must define RDX_BLE_DEFAULT_MODE as RDX_BLE_DEFAULT_MODE_HOGP' })

$projectTestEnableOk = $ProjectConfigText -match '#define\s+RDX_HOGP_KEY_ACTION_TEST_ENABLE\s+1'
Add-CheckResult -Name 'C5_PROJECT_TEST_ENABLE' -Passed $projectTestEnableOk `
    -Message $(if ($projectTestEnableOk) { '' } else { 't2620_project_config.h must define RDX_HOGP_KEY_ACTION_TEST_ENABLE as 1' })

# C5.2 Public fallback defaults are conservative and old debug macro is gone
$appConfigTestFallbackOk = $AppConfigText -match '#ifndef\s+RDX_HOGP_KEY_ACTION_TEST_ENABLE\s*\r?\n\s*#define\s+RDX_HOGP_KEY_ACTION_TEST_ENABLE\s+0'
Add-CheckResult -Name 'C5_APP_CONFIG_TEST_FALLBACK' -Passed $appConfigTestFallbackOk `
    -Message $(if ($appConfigTestFallbackOk) { '' } else { 'rdx_app_config.h must provide RDX_HOGP_KEY_ACTION_TEST_ENABLE fallback defaulting to 0' })

$hogpConfigIncludesAppConfig = $HogpConfigText -match '#include\s+"app_config\.h"'
Add-CheckResult -Name 'C5_HOGP_CONFIG_INCLUDES_APP_CONFIG' -Passed $hogpConfigIncludesAppConfig `
    -Message $(if ($hogpConfigIncludesAppConfig) { '' } else { 'rdx_hogp_config.h must include app_config.h before applying fallback defaults' })

$oldDebugMacroGone = ($HogpConfigText -notmatch 'RDX_BLE_DEBUG_MODE_SWITCH_KEY') -and
                     ($AppCText -notmatch 'RDX_BLE_DEBUG_MODE_SWITCH_KEY')
Add-CheckResult -Name 'C5_OLD_DEBUG_MACRO_REMOVED' -Passed $oldDebugMacroGone `
    -Message $(if ($oldDebugMacroGone) { '' } else { 'RDX_BLE_DEBUG_MODE_SWITCH_KEY must be removed from rdx_hogp_config.h and rdx_app.c' })

$keyUpDelayConfigOk = $HogpConfigText -match '#ifndef\s+TCFG_RDX_HOGP_KEY_UP_DELAY_MS\s*\r?\n\s*#define\s+TCFG_RDX_HOGP_KEY_UP_DELAY_MS\s+20'
Add-CheckResult -Name 'C5_KEY_UP_DELAY_CONFIG' -Passed $keyUpDelayConfigOk `
    -Message $(if ($keyUpDelayConfigOk) { '' } else { 'rdx_hogp_config.h must define TCFG_RDX_HOGP_KEY_UP_DELAY_MS with default 20' })

$keyActionUsesConfigDelay = ($KeyActionText -match 'sys_timeout_add\s*\(\s*NULL\s*,\s*rdx_hogp_key_action_release_timer_cb\s*,\s*TCFG_RDX_HOGP_KEY_UP_DELAY_MS\s*\)') -and
                            ($KeyActionText -notmatch 'RDX_HOGP_KEY_ACTION_RELEASE_DELAY_MS')
Add-CheckResult -Name 'C5_KEY_ACTION_USES_CONFIG_DELAY' -Passed $keyActionUsesConfigDelay `
    -Message $(if ($keyActionUsesConfigDelay) { '' } else { 'rdx_hogp_key_action.c must use TCFG_RDX_HOGP_KEY_UP_DELAY_MS and not define a private release delay' })

# C5.3 HOGP advertising local name still comes from Server local name
$hogpNameSourceOk = $HogpConfigText -match '#define\s+RDX_HOGP_NAME_SOURCE\s+0'
Add-CheckResult -Name 'C5_HOGP_NAME_SOURCE_SERVER' -Passed $hogpNameSourceOk `
    -Message $(if ($hogpNameSourceOk) { '' } else { 'RDX_HOGP_NAME_SOURCE must remain 0 (server local name)' })

$hogpFillTakesLocalName = $KeyboardCText -match 'int\s+rdx_hogp_fill_adv_data\s*\([^)]*const\s+char\s+\*\s*local_name'
$hogpUsesInjectedLocalName = $KeyboardCText -match 'const\s+char\s+\*\s*name\s*=\s*local_name\s*\?\s*local_name\s*:\s*""'
$serverInjectsLocalName = $ServerText -match 'rdx_hogp_adv_start\s*\([^;]*rdx_ble_server_get_local_name\s*\('
Add-CheckResult -Name 'C5_HOGP_ADV_USES_SERVER_LOCAL_NAME' -Passed ($hogpFillTakesLocalName -and $hogpUsesInjectedLocalName -and $serverInjectsLocalName) `
    -Message $(if ($hogpFillTakesLocalName -and $hogpUsesInjectedLocalName -and $serverInjectsLocalName) { '' } else { 'Server must inject local name into HOGP advertising; HOGP must consume local_name parameter' })

# C5.4 Default mode is configurable and effective default respects HOGP master switch
$defaultModeConstantsOk = $HogpConfigText -match '#define\s+RDX_BLE_DEFAULT_MODE_CONFIG\s+0' -and
                         $HogpConfigText -match '#define\s+RDX_BLE_DEFAULT_MODE_HOGP\s+1' -and
                         $HogpConfigText -match '#ifndef\s+RDX_BLE_DEFAULT_MODE\s*\r?\n\s*#define\s+RDX_BLE_DEFAULT_MODE\s+RDX_BLE_DEFAULT_MODE_CONFIG'
Add-CheckResult -Name 'C5_DEFAULT_MODE_CONSTANTS' -Passed $defaultModeConstantsOk `
    -Message $(if ($defaultModeConstantsOk) { '' } else { 'rdx_hogp_config.h must define default BLE mode constants and fallback' })

$defaultModeNoServerHeaderDependency = ($ServerHeaderText -notmatch 'RDX_BLE_DEFAULT_MODE_CONFIG') -and
                                       ($ServerHeaderText -notmatch 'RDX_BLE_DEFAULT_MODE_HOGP') -and
                                       ($HogpConfigText -notmatch '#include\s+"rdx_ble_server\.h"')
Add-CheckResult -Name 'C5_DEFAULT_MODE_NO_SERVER_HEADER_DEP' -Passed $defaultModeNoServerHeaderDependency `
    -Message $(if ($defaultModeNoServerHeaderDependency) { '' } else { 'RDX_BLE_DEFAULT_MODE_* must not require rdx_ble_server.h or make rdx_hogp_config.h include it' })

$serverNoDefaultFallback = $ServerText -notmatch '#ifndef\s+RDX_BLE_DEFAULT_MODE\s*\r?\n\s*#define\s+RDX_BLE_DEFAULT_MODE'
Add-CheckResult -Name 'C5_SERVER_NO_DEFAULT_MODE_FALLBACK' -Passed $serverNoDefaultFallback `
    -Message $(if ($serverNoDefaultFallback) { '' } else { 'RDX_BLE_DEFAULT_MODE fallback must live in rdx_hogp_config.h, not rdx_ble_server.c' })

$effectiveDefaultFunctionMatch = [regex]::Match($ModeControllerText,
    '(?sm)rdx_ble_mode_t\s+rdx_ble_mode_effective_default\s*\([^)]*\)\s*\{(.*?)^\}')
$effectiveDefaultOk = $false
$effectiveDefaultMessage = 'rdx_ble_mode_effective_default() not found'
if ($effectiveDefaultFunctionMatch.Success) {
    $effectiveBody = $effectiveDefaultFunctionMatch.Groups[1].Value
    $hasHogpEnableBranch = $effectiveBody -match '#if\s+TCFG_RDX_HOGP_ENABLE'
    $hasConfigReturn = $effectiveBody -match 'return\s+\(rdx_ble_mode_t\)RDX_BLE_DEFAULT_MODE;'
    $hasElseBranch = $effectiveBody -match '#else'
    $hasDisabledFallback = $effectiveBody -match 'return\s+RDX_BLE_MODE_CONFIG;'
    $effectiveDefaultOk = $hasHogpEnableBranch -and $hasConfigReturn -and $hasElseBranch -and $hasDisabledFallback
    $parts = @()
    if (-not $hasHogpEnableBranch) { $parts += 'HOGP enable branch' }
    if (-not $hasConfigReturn) { $parts += 'return RDX_BLE_DEFAULT_MODE' }
    if (-not $hasElseBranch) { $parts += '#else branch' }
    if (-not $hasDisabledFallback) { $parts += 'disabled fallback to CONFIG' }
    if ($parts.Count -gt 0) {
        $effectiveDefaultMessage = 'effective_default contract missing: ' + ($parts -join ', ')
    } else {
        $effectiveDefaultMessage = ''
    }
}
Add-CheckResult -Name 'C5_EFFECTIVE_DEFAULT_RESPECTS_SWITCH' -Passed $effectiveDefaultOk -Message $effectiveDefaultMessage

$controllerInitUsesEffective = $ModeControllerText -match 'rdx_ble_mode_t\s+default_mode\s*=\s*rdx_ble_mode_effective_default\s*\(\)'
Add-CheckResult -Name 'C5_CONTROLLER_INIT_USES_EFFECTIVE_DEFAULT' -Passed $controllerInitUsesEffective `
    -Message $(if ($controllerInitUsesEffective) { '' } else { 'rdx_ble_mode_controller_init() must use rdx_ble_mode_effective_default()' })

# C5.5 Server init routes default mode to the correct advertising helper
$serverInitFunctionMatch = [regex]::Match($ServerText,
    '(?sm)void\s+rdx_ble_server_init\s*\([^)]*\)\s*\{(.*?)^\}')
$serverInitNoUnconditionalAdv = $false
$serverInitNoUnconditionalAdvMessage = 'rdx_ble_server_init() not found'
if ($serverInitFunctionMatch.Success) {
    $initBody = $serverInitFunctionMatch.Groups[1].Value
    $serverInitNoUnconditionalAdv = $initBody -notmatch 'rdx_ble_server_adv_enable\s*\(\s*1\s*\)\s*;'
    if (-not $serverInitNoUnconditionalAdv) {
        $serverInitNoUnconditionalAdvMessage = 'rdx_ble_server_init() must not unconditionally call rdx_ble_server_adv_enable(1)'
    } else {
        $serverInitNoUnconditionalAdvMessage = ''
    }
}
Add-CheckResult -Name 'C5_SERVER_INIT_NO_UNCONDITIONAL_ADV' -Passed $serverInitNoUnconditionalAdv -Message $serverInitNoUnconditionalAdvMessage

$serverInitRoutesDefault = $ServerText -match 'if\s*\(\s*rdx_ble_mode_get_advertised\s*\(\s*\)\s*==\s*RDX_BLE_MODE_HOGP\s*\)' -and
                          $ServerText -match 'rdx_ble_mode_start_hogp_advertising\s*\(' -and
                          $ServerText -match 'rdx_ble_mode_start_config_advertising\s*\('
Add-CheckResult -Name 'C5_SERVER_INIT_ROUTES_DEFAULT_MODE' -Passed $serverInitRoutesDefault `
    -Message $(if ($serverInitRoutesDefault) { '' } else { 'rdx_ble_server_init() must branch on advertised_mode to start HOGP or CONFIG advertising' })

$serverInitChecksSuppression = $serverInitFunctionMatch.Success -and
                               ($serverInitFunctionMatch.Groups[1].Value -match 'rdx_ble_mode_broadcast_suppressed\s*\(')
Add-CheckResult -Name 'C5_SERVER_INIT_SUPPRESSES_BROADCAST' -Passed $serverInitChecksSuppression `
    -Message $(if ($serverInitChecksSuppression) { '' } else { 'rdx_ble_server_init() must check rdx_ble_mode_broadcast_suppressed() before starting the default broadcast' })

# C5.6 Narrow mode query/toggle API is present and does not bypass controller
$hasIsRequested = $ServerHeaderText -match 'u8\s+rdx_ble_mode_is_hogp_requested\s*\(\s*void\s*\)'
$hasToggle = $ServerHeaderText -match 'void\s+rdx_ble_mode_request_toggle\s*\(\s*void\s*\)'
Add-CheckResult -Name 'C5_MODE_QUERY_TOGGLE_API' -Passed ($hasIsRequested -and $hasToggle) `
    -Message $(if ($hasIsRequested -and $hasToggle) { '' } else { 'rdx_ble_server.h must declare rdx_ble_mode_is_hogp_requested() and rdx_ble_mode_request_toggle()' })

$toggleUsesRequest = $serverCodeOnly -match '(?s)void\s+rdx_ble_mode_request_toggle\s*\(\s*void\s*\)\s*\{[^}]*rdx_ble_mode_request\s*\('
Add-CheckResult -Name 'C5_TOGGLE_USES_REQUEST_HOGP' -Passed $toggleUsesRequest `
    -Message $(if ($toggleUsesRequest) { '' } else { 'rdx_ble_mode_request_toggle() in rdx_ble_server.c must call the server-side rdx_ble_mode_request() helper' })

# C5.7 KEY1 triple-click toggles mode through the narrow API, not private HOGP APIs
$appTripleClickToggle = $AppCText -match 'KEY_ACTION_TRIPLE_CLICK' -and
                        $AppCText -match 'rdx_ble_mode_request_toggle\s*\('
$appNoPrivateHogpMode = $AppCText -notmatch '(?<!rdx_)\bhogp_mode_set\b' -and
                        $AppCText -notmatch '(?<!rdx_)\bhogp_adv_start\b' -and
                        $AppCText -notmatch '(?<!rdx_)\bhogp_adv_stop\b'
Add-CheckResult -Name 'C5_KEY1_TRIPLE_CLICK_TOGGLE' -Passed ($appTripleClickToggle -and $appNoPrivateHogpMode) `
    -Message 'KEY1 triple-click must call rdx_ble_mode_request_toggle() and rdx_app.c must not call private hogp_* mode/adv APIs'

# C5.8 KEY1 triple-click is a formal HOGP path independent of the test keymap;
#      CLICK routes exclusively by HOGP connection state under the same master switch;
#      LONG/HOLD/UP are not consumed by either path.
$tripleClickBlockMatch = [regex]::Match($AppCText,
    '(?sm)#if\s+TCFG_RDX_HOGP_ENABLE\s*\r?\n(?:(?!#if|#endif).)*?KEY_ACTION_TRIPLE_CLICK(?:(?!#endif).)*?#\s*endif')
$clickBlockMatch = [regex]::Match($AppCText,
    '(?sm)#if\s+TCFG_RDX_HOGP_ENABLE\s*\r?\n(?:(?!#if|#endif).)*?if\s*\(\s*index\s*==\s*KEY_ACTION_CLICK\s*\)(?:(?!#endif).)*?#\s*endif')

$keyActionRoutingOk = $false
$parts = @()

$hasTripleClick = $false
if ($tripleClickBlockMatch.Success) {
    $tripleBlock = $tripleClickBlockMatch.Groups[0].Value
    $hasTripleClick = $tripleBlock -match 'KEY_ACTION_TRIPLE_CLICK' -and
                       $tripleBlock -match 'rdx_ble_mode_request_toggle\s*\(' -and
                       $tripleBlock -notmatch 'RDX_HOGP_KEY_ACTION_TEST_ENABLE'
} else {
    $parts += 'TCFG_RDX_HOGP_ENABLE formal block with KEY_ACTION_TRIPLE_CLICK not found'
}

$hasClickExecutor = $false
if ($clickBlockMatch.Success) {
    $clickBlock = $clickBlockMatch.Groups[0].Value
    $hasClickExecutor = $clickBlock -match 'rdx_hogp_key_action_click\s*\('
} else {
    $parts += 'TCFG_RDX_HOGP_ENABLE block with KEY_ACTION_CLICK not found'
}

$combinedBlock = ($tripleClickBlockMatch.Groups[0].Value + "`n" + $clickBlockMatch.Groups[0].Value)
$noLongHoldUp = $combinedBlock -notmatch 'KEY_ACTION_LONG' -and
                $combinedBlock -notmatch 'KEY_ACTION_HOLD\b' -and
                $combinedBlock -notmatch 'KEY_ACTION_HOLDUP'

$keyActionRoutingOk = $hasTripleClick -and $hasClickExecutor -and $noLongHoldUp
if (-not $hasTripleClick) { $parts += 'KEY1 TRIPLE_CLICK -> rdx_ble_mode_request_toggle()' }
if (-not $hasClickExecutor) { $parts += 'CLICK -> rdx_hogp_key_action_click()' }
if (-not $noLongHoldUp) { $parts += 'LONG/HOLD/UP must not appear in executor block' }
$keyActionRoutingMessage = if ($parts.Count -gt 0) { 'C5 key action routing contract missing: ' + ($parts -join ', ') } else { '' }
Add-CheckResult -Name 'C5_KEY_ACTION_ROUTING' -Passed $keyActionRoutingOk -Message $keyActionRoutingMessage

$hidConnectionRoutingOk = $clickBlockMatch.Success -and
    ($clickBlockMatch.Groups[0].Value -match '(?s)if\s*\(\s*index\s*==\s*KEY_ACTION_CLICK\s*\)\s*\{\s*if\s*\(\s*rdx_hogp_keyboard_is_connected\s*\(\s*\)\s*\)\s*\{.*?rdx_hogp_key_action_click\s*\(.*?\*value\s*=\s*APP_MSG_NULL\s*;\s*return\s*;')
Add-CheckResult -Name 'C5_HID_CONNECTION_EXCLUSIVE_ROUTING' -Passed $hidConnectionRoutingOk `
    -Message $(if ($hidConnectionRoutingOk) { '' } else { 'CLICK must execute and consume HOGP actions only inside rdx_hogp_keyboard_is_connected(), otherwise fall through to the offline IO table' })

# C5.9 HOGP executor is the sole owner of the built-in test keymap
$defaultActionsInExecutor = $KeyActionText -match 's_rdx_hogp_test_keymap\s*\[\s*RDX_HOGP_KEY_ACTION_PHYSICAL_KEY_COUNT\s*\]' -and
                            $KeyActionText -match '\{\s*0x01,\s*\{\s*0x06,\s*0x00,\s*0x00,\s*0x00,\s*0x00,\s*0x00\s*\}\s*\},\s*/\*\s*KEY1:\s*Ctrl\+C\s*\*/' -and
                            $KeyActionText -match '\{\s*0x01,\s*\{\s*0x19,\s*0x00,\s*0x00,\s*0x00,\s*0x00,\s*0x00\s*\}\s*\},\s*/\*\s*KEY2:\s*Ctrl\+V\s*\*/' -and
                            $KeyActionText -match '\{\s*0x01,\s*\{\s*0x1b,\s*0x00,\s*0x00,\s*0x00,\s*0x00,\s*0x00\s*\}\s*\},\s*/\*\s*KEY3:\s*Ctrl\+X\s*\*/' -and
                            $KeyActionText -match '\{\s*0x00,\s*\{\s*0x2a,\s*0x00,\s*0x00,\s*0x00,\s*0x00,\s*0x00\s*\}\s*\},\s*/\*\s*KEY4:\s*Backspace\s*\*/' -and
                            $KeyActionText -match '\{\s*0x00,\s*\{\s*0x28,\s*0x00,\s*0x00,\s*0x00,\s*0x00,\s*0x00\s*\}\s*\},\s*/\*\s*KEY5:\s*Enter\s*\*/'
Add-CheckResult -Name 'C5_DEFAULT_ACTIONS_IN_HOGP_EXECUTOR' -Passed $defaultActionsInExecutor `
    -Message $(if ($defaultActionsInExecutor) { '' } else { 'rdx_hogp_key_action.c must hold the five default HID actions (Ctrl+C, Ctrl+V, Ctrl+X, Backspace, Enter)' })

$legacyKeyHasNoHogp = ($KeyText -notmatch 'RDX_HOGP_KEY_ACTION_TEST_ENABLE|rdx_hogp_key_action|rdx_key_get_hogp|s_rdx_key_hogp_default_actions') -and
                      ($KeyHeaderText -notmatch 'RDX_HOGP_KEY_ACTION_TEST_ENABLE|rdx_hogp_key_action|rdx_key_get_hogp')
Add-CheckResult -Name 'C5_LEGACY_KEY_MODULE_NO_HOGP' -Passed $legacyKeyHasNoHogp `
    -Message $(if ($legacyKeyHasNoHogp) { '' } else { 'rdx_key.c/.h must not contain HOGP key-action types, test maps, macros, or getters' })

$executorNoLegacyKeyDependency = $KeyActionText -notmatch '#include\s+"rdx_key\.h"|rdx_key_get_hogp'
Add-CheckResult -Name 'C5_EXECUTOR_NO_LEGACY_KEY_DEPENDENCY' -Passed $executorNoLegacyKeyDependency `
    -Message $(if ($executorNoLegacyKeyDependency) { '' } else { 'rdx_hogp_key_action.c must own its test map and not depend on rdx_key.c/.h' })

# C5.10 Executor owns release timer lifecycle correctly
$executorStartsTimerOnSuccess = $KeyActionText -match 'sys_timeout_add\s*\(\s*NULL\s*,\s*rdx_hogp_key_action_release_timer_cb' -and
                               $KeyActionText -match 'rdx_hogp_key_action_cancel_release_timer\s*\(\)'
$executorResetCancelsTimer = $KeyActionText -match 'void\s+rdx_hogp_key_action_reset\s*\([^)]*\)\s*\{[^}]*rdx_hogp_key_action_cancel_release_timer' -and
                            $KeyActionText -match 'void\s+rdx_hogp_key_action_reset\s*\([^)]*\)\s*\{[^}]*rdx_hogp_keyboard_release_all'
$executorDeinitCancelsTimer = $KeyActionText -match 'void\s+rdx_hogp_key_action_deinit\s*\([^)]*\)\s*\{[^}]*rdx_hogp_key_action_cancel_release_timer'
Add-CheckResult -Name 'C5_EXECUTOR_TIMER_LIFECYCLE' -Passed ($executorStartsTimerOnSuccess -and $executorResetCancelsTimer -and $executorDeinitCancelsTimer) `
    -Message 'rdx_hogp_key_action.c must start release timer only on successful send, and cancel it in reset()/deinit()'

# C5.10a Executor click() enforces key_count and handles timer creation failure
$clickFunctionMatch = [regex]::Match($KeyActionText,
    '(?sm)int\s+rdx_hogp_key_action_click\s*\([^)]*\)\s*\{(.*?)^\}')
$keyCountCheckOk = $false
$timerFailureOk = $false
if ($clickFunctionMatch.Success) {
    $clickBody = $clickFunctionMatch.Groups[1].Value
    $keyCountCheckOk = $clickBody -match 'key_id\s*>=\s*s_rdx_hogp_key_action_active_keymap\.key_count'
    $timerFailureOk = $clickBody -match 'if\s*\(\s*s_rdx_hogp_key_action_release_timer\s*==\s*0\s*\)' -and
                      $clickBody -match 'rdx_hogp_keyboard_release_all\s*\(' -and
                      $clickBody -match 'return\s+1\s*;'
}
Add-CheckResult -Name 'C5_EXECUTOR_KEY_COUNT_CHECK' -Passed $keyCountCheckOk `
    -Message $(if ($keyCountCheckOk) { '' } else { 'rdx_hogp_key_action_click() must reject key_id >= active_keymap.key_count' })
Add-CheckResult -Name 'C5_EXECUTOR_TIMER_FAILURE_RELEASE' -Passed $timerFailureOk `
    -Message $(if ($timerFailureOk) { '' } else { 'rdx_hogp_key_action_click() must send immediate release if release timer cannot be created' })

# C5.10b R5-A keeps report conversion and keymap loading explicit but internal
$conversionFunctionMatch = [regex]::Match($KeyActionText,
    '(?sm)static\s+void\s+rdx_hogp_key_action_to_keyboard_report\s*\([^)]*rdx_hogp_key_action_keyboard_t\s+\*action[^)]*rdx_hogp_keyboard_report_t\s+\*report[^)]*\)\s*\{(.*?)^\}')
$conversionOk = $false
if ($conversionFunctionMatch.Success) {
    $conversionBody = $conversionFunctionMatch.Groups[1].Value
    $conversionOk = $conversionBody -match 'report->modifiers\s*=\s*action->modifiers' -and
                    $conversionBody -match 'report->reserved\s*=\s*0' -and
                    $conversionBody -match 'memcpy\s*\(\s*report->usages\s*,\s*action->usages\s*,\s*sizeof\s*\(\s*report->usages\s*\)\s*\)'
}
$clickUsesConversion = $clickFunctionMatch.Success -and
                       ($clickFunctionMatch.Groups[1].Value -match 'rdx_hogp_key_action_to_keyboard_report\s*\(\s*action\s*,\s*&report\s*\)') -and
                       ($clickFunctionMatch.Groups[1].Value -notmatch 'report\.reserved\s*=\s*0')
Add-CheckResult -Name 'C5_R5A_REPORT_CONVERSION' -Passed ($conversionOk -and $clickUsesConversion) `
    -Message $(if ($conversionOk -and $clickUsesConversion) { '' } else { 'R5-A requires a private action-to-keyboard-report helper and click() must use it' })

$defaultLoaderMatch = [regex]::Match($KeyActionText,
    '(?sm)static\s+void\s+rdx_hogp_key_action_load_default_keymap\s*\([^)]*\)\s*\{(.*?)^\}')
$testLoaderMatch = [regex]::Match($KeyActionText,
    '(?sm)static\s+void\s+rdx_hogp_key_action_load_test_keymap\s*\([^)]*\)\s*\{(.*?)^\}')
$initFunctionMatch = [regex]::Match($KeyActionText,
    '(?sm)void\s+rdx_hogp_key_action_init\s*\([^)]*\)\s*\{(.*?)^\}')
$defaultLoaderOk = $defaultLoaderMatch.Success -and
                   ($defaultLoaderMatch.Groups[1].Value -match 'rdx_hogp_key_action_clear_active_keymap\s*\(') -and
                   ($defaultLoaderMatch.Groups[1].Value -notmatch 'VM_RDX_|rdx_vm_|memcpy\s*\(')
$testLoaderOk = $testLoaderMatch.Success -and
                ($testLoaderMatch.Groups[1].Value -match 's_rdx_hogp_test_keymap') -and
                ($testLoaderMatch.Groups[1].Value -match 's_rdx_hogp_key_action_active\s*=\s*1')
$initLoadsByGate = $initFunctionMatch.Success -and
                   ($initFunctionMatch.Groups[1].Value -match '#if\s*\(\s*RDX_HOGP_KEY_ACTION_TEST_ENABLE\s*&&\s*TCFG_RDX_HOGP_ENABLE\s*\)') -and
                   ($initFunctionMatch.Groups[1].Value -match 'rdx_hogp_key_action_load_test_keymap\s*\(') -and
                   ($initFunctionMatch.Groups[1].Value -match '#else') -and
                   ($initFunctionMatch.Groups[1].Value -match 'rdx_hogp_key_action_load_default_keymap\s*\(')
Add-CheckResult -Name 'C5_R5A_KEYMAP_LOADERS' -Passed ($defaultLoaderOk -and $testLoaderOk -and $initLoadsByGate) `
    -Message $(if ($defaultLoaderOk -and $testLoaderOk -and $initLoadsByGate) { '' } else { 'R5-A requires gated test/default keymap loaders; default loader must stay empty/no-VM' })

$noPrivatePressReleaseApi = ($KeyActionText -notmatch 'rdx_hogp_key_action_(press|release)\s*\(') -and
                            ($KeyActionHeaderText -notmatch 'rdx_hogp_key_action_(press|release)\s*\(')
Add-CheckResult -Name 'C5_R5A_NO_PRIVATE_PRESS_RELEASE_API' -Passed $noPrivatePressReleaseApi `
    -Message $(if ($noPrivatePressReleaseApi) { '' } else { 'R5-A must not add private HOGP press/release APIs' })

$applyFunctionMatch = [regex]::Match($KeyActionText,
    '(?sm)int\s+rdx_hogp_key_action_keymap_apply\s*\([^)]*\)\s*\{(.*?)^\}')
$applyClearsTail = $false
if ($applyFunctionMatch.Success) {
    $applyBody = $applyFunctionMatch.Groups[1].Value
    $applyClearsTail = $applyBody -match 'memset\s*\(\s*&s_rdx_hogp_key_action_active_keymap\s*,\s*0' -and
                       $applyBody -match 'keymap->key_count\s*\*\s*sizeof\s*\(\s*keymap->keys\[0\]\s*\)'
}
Add-CheckResult -Name 'C5_KEYMAP_APPLY_CLEARS_TAIL' -Passed $applyClearsTail `
    -Message $(if ($applyClearsTail) { '' } else { 'rdx_hogp_key_action_keymap_apply() must clear the active map and copy only key_count entries' })

# C5.11 rdx_app.c no longer owns HOGP report construction or release timer
$appNoReleaseTimer = $AppCText -notmatch 's_rdx_app_hogp_release_timer'
$appNoFixedUsages = $AppCText -notmatch 's_rdx_app_hogp_debug_usages'
$appNoReportConstruct = $AppCText -notmatch 'rdx_hogp_keyboard_report_t\s+report\s*=\s*\{0\}'
Add-CheckResult -Name 'C5_APP_NO_HOGP_INTERNALS' -Passed ($appNoReleaseTimer -and $appNoFixedUsages -and $appNoReportConstruct) `
    -Message 'rdx_app.c must not own release timer, fixed usage table, or report construction'

# C5.12 Executor boundary: no broadcast/VM/HFP/private mode calls
$actionBoundaryOk = ($KeyActionText -notmatch 'rdx_ble_server_adv_enable') -and
                   ($KeyActionText -notmatch 'rdx_hogp_mode_set') -and
                   ($KeyActionText -notmatch 'rdx_hogp_adv_start') -and
                   ($KeyActionText -notmatch 'rdx_hogp_adv_stop') -and
                   ($KeyActionText -notmatch 'rdx_ble_server_app_disconnect') -and
                   ($KeyActionText -notmatch 'VM_RDX_') -and
                   ($KeyActionText -notmatch 'rdx_vm_') -and
                   ($KeyActionText -notmatch 'hfp_') -and
                   ($KeyActionText -notmatch 'rdx_hogp_keyboard_is_connected')
Add-CheckResult -Name 'C5_EXECUTOR_BOUNDARY' -Passed $actionBoundaryOk `
    -Message $(if ($actionBoundaryOk) { '' } else { 'rdx_hogp_key_action.c must not call advertising, disconnect, VM, HFP, HOGP mode, or connection-routing APIs' })

# C5.13 Executor lifecycle is wired into BLE Server disconnect, mode switch, and exit
$disconnectCleanupMatch = [regex]::Match($ServerText,
    '(?sm)static\s+void\s+rdx_ble_server_disconnected_cleanup_internal\s*\([^)]*\)\s*\{(.*?)^\}')
$modeSyncMatch = [regex]::Match($ServerText,
    '(?sm)static\s+void\s+rdx_ble_mode_sync_hogp_runtime\s*\([^)]*\)\s*\{(.*?)^\}')
$exitMatch = [regex]::Match($ServerText,
    '(?sm)void\s+rdx_ble_server_exit\s*\([^)]*\)\s*\{(.*?)^\}')
$resetOnDisconnect = $disconnectCleanupMatch.Success -and
                     ($disconnectCleanupMatch.Groups[1].Value -match 'rdx_hogp_key_action_reset\s*\(')
$resetOnModeSwitch = $modeSyncMatch.Success -and
                     ($modeSyncMatch.Groups[1].Value -match 'rdx_hogp_key_action_reset\s*\(')
$deinitOnExit = $exitMatch.Success -and
                ($exitMatch.Groups[1].Value -match 'rdx_hogp_key_action_deinit\s*\(')
$deinitBeforeHogp = $false
if ($exitMatch.Success) {
    $exitBody = $exitMatch.Groups[1].Value
    $deinitPos = $exitBody.IndexOf('rdx_hogp_key_action_deinit')
    $hogpDeinitPos = $exitBody.IndexOf('rdx_hogp_deinit')
    $deinitBeforeHogp = ($deinitPos -ge 0) -and ($hogpDeinitPos -ge 0) -and ($deinitPos -lt $hogpDeinitPos)
}
Add-CheckResult -Name 'C5_EXECUTOR_LIFECYCLE_WIRED' -Passed ($resetOnDisconnect -and $resetOnModeSwitch -and $deinitOnExit) `
    -Message 'rdx_hogp_key_action_reset() must be called on disconnect and HOGP->Config switch; deinit() on BLE Server exit'
Add-CheckResult -Name 'C5_EXECUTOR_DEINIT_BEFORE_HOGP' -Passed $deinitBeforeHogp `
    -Message 'rdx_ble_server_exit() must call rdx_hogp_key_action_deinit() before rdx_hogp_deinit()'

# C5.14 Existing IO NUM legacy key tables are preserved verbatim
$legacyTablesPreserved = $KeyText -match 'key_table_io_num0_normal\[KEY_ACTION_MAX\]' -and
                         $KeyText -match 'key_table_io_num1_normal\[KEY_ACTION_MAX\]' -and
                         $KeyText -match 'key_table_io_num2_normal\[KEY_ACTION_MAX\]' -and
                         $KeyText -match 'key_table_io_num3_normal\[KEY_ACTION_MAX\]' -and
                         $KeyText -match 'key_table_io_num4_normal\[KEY_ACTION_MAX\]'
Add-CheckResult -Name 'C5_LEGACY_KEY_TABLES_PRESERVED' -Passed $legacyTablesPreserved `
    -Message $(if ($legacyTablesPreserved) { '' } else { 'rdx_key.c must preserve the five legacy key_table_io_num*_normal[] arrays' })

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
