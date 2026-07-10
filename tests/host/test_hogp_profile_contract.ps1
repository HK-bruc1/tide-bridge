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
