param(
    [ValidateSet('Baseline', 'Progress', 'Final')]
    [string]$Mode = 'Baseline',
    [string]$BaselineRef = '128eb8d'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$rdxRel = 'SDK/apps/common/third_party_profile/rdx_protocol'
$rdxRoot = Join-Path $repo $rdxRel
$allowlistPath = Join-Path $PSScriptRoot 'rdx_p11_allowlist.psd1'
$script:Errors = @()

function Add-Pass([string]$Message) {
    Write-Host "PASS: $Message"
}

function Add-Failure([string]$Message) {
    $script:Errors += $Message
    Write-Host "FAIL: $Message"
}

function Mask-CCommentsAndStrings([string]$Text) {
    $pattern = '(?s)/\*.*?\*/|//[^\r\n]*|"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*'''
    return [regex]::Replace($Text, $pattern, {
        param($match)
        return [regex]::Replace($match.Value, '[^\r\n]', ' ')
    })
}

function Mask-CComments([string]$Text) {
    return [regex]::Replace($Text, '(?s)/\*.*?\*/|//[^\r\n]*', {
        param($match)
        return [regex]::Replace($match.Value, '[^\r\n]', ' ')
    })
}

function Get-FunctionRanges([string]$Text) {
    $masked = Mask-CCommentsAndStrings $Text
    $signaturePattern = '(?m)^\s*(?:static\s+)?(?:[A-Za-z_]\w*[\s\*]+)+(?<name>[A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{'
    $ranges = @()
    foreach ($match in [regex]::Matches($masked, $signaturePattern)) {
        $braceStart = $masked.IndexOf('{', $match.Index)
        $depth = 0
        $end = -1
        for ($i = $braceStart; $i -lt $masked.Length; $i++) {
            if ($masked[$i] -eq '{') {
                $depth++
            } elseif ($masked[$i] -eq '}') {
                $depth--
                if ($depth -eq 0) {
                    $end = $i + 1
                    break
                }
            }
        }
        if ($end -lt 0) {
            throw "Unbalanced function body while parsing $($match.Groups['name'].Value)"
        }
        $ranges += [pscustomobject]@{
            Name = $match.Groups['name'].Value
            Start = $match.Index
            End = $end
        }
    }
    return $ranges
}

function Get-ContainingFunction([object[]]$Ranges, [int]$Index) {
    foreach ($range in $Ranges) {
        if ($Index -ge $range.Start -and $Index -lt $range.End) {
            return $range.Name
        }
    }
    return '<file-scope>'
}

function Read-Baseline([string]$RelativePath) {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = 'git'
    $startInfo.Arguments = "-C `"$repo`" show `"${BaselineRef}:$RelativePath`""
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = [System.Text.Encoding]::UTF8

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $content = $process.StandardOutput.ReadToEnd()
    $process.StandardError.ReadToEnd() | Out-Null
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        return ''
    }
    return $content.Replace("`r`n", "`n").Replace("`r", "`n")
}

function Get-ScopedHits([string]$RelativePath, [string]$Text) {
    if ([string]::IsNullOrEmpty($Text)) {
        return @()
    }
    $masked = Mask-CCommentsAndStrings $Text
    $ranges = @(Get-FunctionRanges $Text)
    $hits = @()
    foreach ($definition in @(
        @{ Pattern = '\b(?<symbol>rdx_uxfile_[A-Za-z0-9_]*)\s*\('; Fixed = '' },
        @{ Pattern = '\bRecordStatus\b'; Fixed = 'RecordStatus' },
        @{ Pattern = '\brdx_record_get_status\s*\('; Fixed = 'rdx_record_get_status' },
        @{ Pattern = '\brdx_record_process\s*\('; Fixed = 'rdx_record_process' }
    )) {
        foreach ($match in [regex]::Matches($masked, $definition.Pattern)) {
            $symbol = if ([string]::IsNullOrEmpty($definition.Fixed)) {
                $match.Groups['symbol'].Value
            } else {
                $definition.Fixed
            }
            $hits += [pscustomobject]@{
                File = $RelativePath
                Function = Get-ContainingFunction $ranges $match.Index
                Symbol = $symbol
            }
        }
    }
    return $hits
}

function Get-HitCounts([object[]]$Hits) {
    $counts = @{}
    foreach ($hit in $Hits) {
        $key = "$($hit.File)::$($hit.Function)::$($hit.Symbol)"
        if (-not $counts.ContainsKey($key)) {
            $counts[$key] = 0
        }
        $counts[$key]++
    }
    return $counts
}

if (-not (Test-Path -LiteralPath $allowlistPath)) {
    throw "P11 allowlist not found: $allowlistPath"
}

$allowlist = Import-PowerShellDataFile -LiteralPath $allowlistPath
if ($allowlist.Version -ne 1 -or $null -eq $allowlist.Entries) {
    throw 'Unsupported or invalid P11 allowlist manifest'
}

$entryKeys = @{}
foreach ($entry in $allowlist.Entries) {
    foreach ($field in @('File', 'Function', 'Symbol', 'Purpose', 'Count')) {
        if ([string]::IsNullOrWhiteSpace([string]$entry[$field])) {
            Add-Failure "allowlist entry has an empty $field"
        }
    }
    if ([int]$entry.Count -lt 1) {
        Add-Failure "allowlist entry has an invalid Count: $($entry.File)::$($entry.Function)::$($entry.Symbol)"
    }
    if ($entry.File -match '[*?]' -or $entry.Function -match '[*?]' -or $entry.Symbol -match '[*?]') {
        Add-Failure "allowlist entry uses a wildcard: $($entry.File)::$($entry.Function)::$($entry.Symbol)"
    }
    $key = "$($entry.File)::$($entry.Function)::$($entry.Symbol)"
    if ($entryKeys.ContainsKey($key)) {
        Add-Failure "duplicate allowlist entry: $key"
    } else {
        $entryKeys[$key] = $true
    }
}
if ($script:Errors.Count -eq 0) {
    Add-Pass 'allowlist entries are exact file + function + symbol + purpose records'
}

$serviceHeaders = @(Get-ChildItem -LiteralPath (Join-Path $rdxRoot 'service') -File -Filter '*.h')
$forbiddenHeaderPattern = '#\s*include\s*[<"]rdx_record\.h[>"]|\b(?:RecordStatus|ReqFileInfo|uxfile_data_t)\b'
$headerScanProbe = Mask-CComments "/* RecordStatus */`n#include `"rdx_record.h`""
if (-not [regex]::IsMatch($headerScanProbe, $forbiddenHeaderPattern) -or
    [regex]::IsMatch($headerScanProbe, '\bRecordStatus\b')) {
    throw 'P11 public-header scanner self-test failed'
}
foreach ($header in $serviceHeaders) {
    $text = [System.IO.File]::ReadAllText($header.FullName)
    $masked = Mask-CComments $text
    if ([regex]::IsMatch($masked, $forbiddenHeaderPattern)) {
        Add-Failure "service public header exposes a P11 legacy type/include: $($header.Name)"
    }
}
if (-not ($script:Errors | Where-Object { $_ -like 'service public header*' })) {
    Add-Pass 'service public headers exclude rdx_record.h, RecordStatus, ReqFileInfo and uxfile_data_t'
}

foreach ($privateHeaderRel in @(
    "$rdxRel/internal/rdx_record_domain.h",
    "$rdxRel/compat/rdx_record_protocol_adapter.h"
)) {
    $privateHeader = Join-Path $repo $privateHeaderRel
    if (-not (Test-Path -LiteralPath $privateHeader)) {
        continue
    }
    $masked = Mask-CComments ([System.IO.File]::ReadAllText($privateHeader))
    if ([regex]::IsMatch($masked, '\bRecordStatus\b')) {
        Add-Failure "clean private header exposes RecordStatus: $privateHeaderRel"
    } else {
        Add-Pass "clean private header excludes RecordStatus: $privateHeaderRel"
    }
}

$baselineFiles = @(& git -C $repo ls-tree -r --name-only $BaselineRef -- $rdxRel) |
    Where-Object { $_ -match '\.c$' }
$workingFiles = @(Get-ChildItem -LiteralPath $rdxRoot -Recurse -File -Filter '*.c' |
    ForEach-Object { $_.FullName.Substring($repo.Length + 1).Replace('\', '/') })
$allProductionCFiles = @(($baselineFiles + $workingFiles) | Sort-Object -Unique)
$baselineHits = @()
$workingHits = @()
foreach ($relativeFile in $allProductionCFiles) {
    $baselineHits += @(Get-ScopedHits $relativeFile (Read-Baseline $relativeFile))
    $workingPath = Join-Path $repo $relativeFile
    if (Test-Path -LiteralPath $workingPath) {
        $workingText = [System.IO.File]::ReadAllText($workingPath)
        $workingHits += @(Get-ScopedHits $relativeFile $workingText)
    }
}

$baselineCounts = Get-HitCounts $baselineHits
$workingCounts = Get-HitCounts $workingHits
$allHitKeys = @(($baselineCounts.Keys + $workingCounts.Keys) | Sort-Object -Unique)
foreach ($key in $allHitKeys) {
    $baselineCount = if ($baselineCounts.ContainsKey($key)) { $baselineCounts[$key] } else { 0 }
    $workingCount = if ($workingCounts.ContainsKey($key)) { $workingCounts[$key] } else { 0 }
    if ($Mode -eq 'Baseline' -and $workingCount -ne $baselineCount) {
        Add-Failure "full-tree baseline drift: $key baseline=$baselineCount working=$workingCount"
    } elseif ($Mode -eq 'Progress' -and $workingCount -gt $baselineCount) {
        $allowed = $entryKeys.ContainsKey($key)
        if (-not $allowed) {
            Add-Failure "full-tree unlisted increase: $key baseline=$baselineCount working=$workingCount"
        }
    } elseif ($Mode -eq 'Final' -and $workingCount -gt 0) {
        $ownerLegacy = $key.StartsWith("$rdxRel/rdx_record.c::") -and
            ($key.EndsWith('::RecordStatus') -or
             $key.EndsWith('::rdx_record_get_status') -or
             $key.EndsWith('::rdx_record_process'))
        if (-not $ownerLegacy -and -not $entryKeys.ContainsKey($key)) {
            Add-Failure "full-tree final access is not allowlisted: $key"
        }
    }
}
if (-not ($script:Errors | Where-Object { $_ -like 'full-tree*' })) {
    Add-Pass "full RDX production .c tree matches $Mode discovery policy"
}

foreach ($entry in $allowlist.Entries) {
    $key = "$($entry.File)::$($entry.Function)::$($entry.Symbol)"
    $actualCount = if ($workingCounts.ContainsKey($key)) { $workingCounts[$key] } else { 0 }
    if ($actualCount -ne [int]$entry.Count) {
        Add-Failure "stale or mismatched allowlist entry: $key expected=$($entry.Count) actual=$actualCount"
    }
}
if (-not ($script:Errors | Where-Object { $_ -like 'stale or mismatched allowlist entry*' })) {
    Add-Pass 'every allowlist entry resolves to the exact declared working-tree call count'
}

if ($Mode -eq 'Final') {
    $recordServicePath = Join-Path $rdxRoot 'service/rdx_record_service.c'
    $recordService = Mask-CCommentsAndStrings ([System.IO.File]::ReadAllText($recordServicePath))
    if ([regex]::IsMatch($recordService, '\b(?:RecordStatus|rdx_record_get_status|rdx_record_process)\b')) {
        Add-Failure 'Final record service still exposes legacy record state/process symbols'
    } else {
        Add-Pass 'Final record service has zero legacy record state/process symbols'
    }
}

Write-Host ''
if ($script:Errors.Count -gt 0) {
    Write-Host "P11 exact boundary checks failed: $($script:Errors.Count) error(s)"
    exit 1
}

Write-Host 'All RDX P11 exact boundary checks passed.'
