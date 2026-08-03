param(
    [string]$RepoRoot = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

$rdxRoot = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$failures = New-Object System.Collections.Generic.List[string]

function Get-RelativePath([string]$Path) {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    return $fullPath.Substring($RepoRoot.Length + 1).Replace('\', '/')
}

function Get-RdxFiles {
    param([string[]]$Extensions = @('.c', '.h'))

    return @(Get-ChildItem -Path $rdxRoot -Recurse -File | Where-Object {
        $Extensions -contains $_.Extension
    })
}

function Get-CodeText {
    param(
        [string]$Path,
        [bool]$MaskLiterals = $false
    )

    $text = [System.IO.File]::ReadAllText($Path)
    $tokenPattern = '(?s)(?<literal>"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*'')|(?<comment>/\*.*?\*/|//[^\r\n]*)'
    return [regex]::Replace($text, $tokenPattern, {
        param($match)
        if ($match.Groups['comment'].Success -or $MaskLiterals) {
            return [regex]::Replace($match.Value, '[^\r\n]', ' ')
        }
        return $match.Value
    })
}

function Get-Matches {
    param(
        [System.IO.FileInfo[]]$Files,
        [string]$Pattern,
        [string[]]$AllowedFiles = @(),
        [bool]$MaskLiterals = $false
    )

    $matches = @()
    foreach ($file in $Files) {
        $relativePath = Get-RelativePath $file.FullName
        if ($AllowedFiles -contains $relativePath) {
            continue
        }
        $text = Get-CodeText $file.FullName $MaskLiterals
        foreach ($match in [regex]::Matches($text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
            $lineNumber = 1 + ([regex]::Matches($text.Substring(0, $match.Index), "`n")).Count
            $matches += [pscustomobject]@{
                Path = $file.FullName
                LineNumber = $lineNumber
                Line = $match.Value
            }
        }
    }
    return $matches
}

function Assert-NoMatches {
    param(
        [string]$Name,
        [object[]]$Items
    )

    $matchList = @($Items | Where-Object { $null -ne $_ })
    if ($matchList.Count -eq 0) {
        Write-Host "PASS: $Name"
        return
    }

    $script:failures.Add($Name)
    Write-Host "FAIL: $Name"
    foreach ($match in $matchList) {
        Write-Host ("  {0}:{1}: {2}" -f `
            (Get-RelativePath $match.Path), $match.LineNumber, $match.Line.Trim())
    }
}

if (-not (Test-Path -LiteralPath $rdxRoot)) {
    throw "RDX source root not found: $rdxRoot"
}

$allFiles = Get-RdxFiles
$businessFiles = @($allFiles | Where-Object {
    $relativePath = Get-RelativePath $_.FullName
    $relativePath -notmatch '/(?:board|port)/'
})
$serviceSources = @($allFiles | Where-Object {
    (Get-RelativePath $_.FullName) -match '/service/.*\.c$'
})
$publicHeaders = @($allFiles | Where-Object {
    $relativePath = Get-RelativePath $_.FullName
    $_.Extension -eq '.h' -and $relativePath -match '/service/'
})

Write-Host 'RDX architecture constraints'

Assert-NoMatches 'business code does not include board or JL platform-private headers' `
    (Get-Matches $businessFiles `
        '^\s*#\s*include\s*[<"](?:board/|port/jl/(?!include/))')

Assert-NoMatches 'service public headers do not expose JL or board primitives' `
    (Get-Matches $publicHeaders `
        '\b(?:OS_MUTEX|OS_SEM|cbuffer_t|IO_PORT[A-Z0-9_]*|APP_MSG_[A-Z0-9_]+)\b' @() $true)

Assert-NoMatches 'service code uses OS abstraction ports for tasks and timers' `
    (Get-Matches $serviceSources `
        '\b(?:os_taskq_post_type|sys_timeout_(?:add|del)|sys_timer_(?:add|del|modify|re_run)|sys_timeout_add_2_task)\s*\(' @() $true)

Assert-NoMatches 'business code keeps persistence behind the JL storage port' `
    (Get-Matches $businessFiles `
        '\b(?:syscfg_(?:read|write|read_string)|VM_RDX_[A-Z0-9_]*|CFG_BT_NAME|CFG_BT_MAC_ADDR)\b' @() $true)

$recordOwnerFiles = @(
    'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.h',
    'SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.h',
    'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_record_domain.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_record_protocol_adapter.c'
)
Assert-NoMatches 'legacy record state is confined to owner and ABI compatibility modules' `
    (Get-Matches $allFiles `
        '\b(?:RecordStatus|rdx_record_get_status\s*\(|rdx_record_process\s*\()' `
        $recordOwnerFiles $true)

$uxfileControlOwnerFiles = @(
    'SDK/apps/common/third_party_profile/rdx_protocol/rdx_app.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/rdx_record.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/rdx_uxfile.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/rdx_uxfile.h',
    'SDK/apps/common/third_party_profile/rdx_protocol/internal/rdx_storage_domain.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_device_service.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_storage_service.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_wifi_service.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_file_transfer_cleanup_compat.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_storage_format_compat.c'
)
Assert-NoMatches 'legacy uxfile access is confined to storage owners and compatibility modules' `
    (Get-Matches $allFiles '\brdx_uxfile_[A-Za-z0-9_]*\s*\(' $uxfileControlOwnerFiles $true)

Assert-NoMatches 'service public headers do not expose legacy record or uxfile types' `
    (Get-Matches $publicHeaders '\b(?:RecordStatus|ReqFileInfo|uxfile_[A-Za-z0-9_]*_t)\b' @() $true)

# P12.1: WiFi TX-done/send-stop/retry still own legacy state until P12.3.
$fileTransferLegacyOwnerFiles = @(
    'SDK/apps/common/third_party_profile/rdx_protocol/rdx_uxfile.h',
    'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_file_transfer_query.c',
    'SDK/apps/common/third_party_profile/rdx_protocol/service/rdx_wifi_service.c'
)
Assert-NoMatches 'file transfer legacy state is confined to query and compatibility owners' `
    (Get-Matches $allFiles `
        '\b(?:rdx_protocol_get_uploadfileInfo\s*\(|ReqFileInfo\b)|->\s*file_send_busy\b' `
        $fileTransferLegacyOwnerFiles $true)

Assert-NoMatches 'production sources do not include Host-only test support' `
    (Get-Matches $allFiles '(?:tests[/\\]host|rdx_p11_trace_(?:schema|spy)\.h)' @() $true)

if ($failures.Count -ne 0) {
    Write-Host ''
    Write-Host "RDX architecture constraints failed: $($failures.Count)"
    exit 1
}

Write-Host 'RDX architecture constraints passed.'
exit 0
