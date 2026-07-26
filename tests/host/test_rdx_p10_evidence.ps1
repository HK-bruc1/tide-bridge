Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$manifestPath = Join-Path $PSScriptRoot 'rdx_p10_evidence.psd1'
$manifest = Import-PowerShellDataFile -LiteralPath $manifestPath
$errors = @()
$seenPaths = @{}
$allowedKinds = @(
    'Commit', 'Archive', 'Firmware', 'Elf', 'Map', 'BuildLog',
    'PersistenceReport', 'BoardReport', 'IssueReport'
)

function Resolve-EvidencePath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
    return Join-Path $repo $Path
}

function Has-Property([object]$Object, [string]$Name) {
    return $null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name
}

function Read-JsonReport([string]$Path, [string]$Label, [string]$ReportType) {
    try {
        $report = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    } catch {
        $script:errors += "${Label}: report is not valid JSON"
        return $null
    }
    foreach ($property in @('schema_version', 'report_type', 'source_commit', 'build_identity')) {
        if (-not (Has-Property $report $property)) {
            $script:errors += "${Label}: missing JSON property '$property'"
            return $null
        }
    }
    if ([int]$report.schema_version -ne 1 -or
        [string]$report.report_type -cne $ReportType -or
        [string]$report.source_commit -cne [string]$manifest.SourceCommit -or
        [string]$report.build_identity -cne [string]$manifest.BuildIdentity) {
        $script:errors += "${Label}: report identity does not match the manifest"
        return $null
    }
    return $report
}

if ($manifest.Version -ne 2) {
    throw 'Unsupported P10 evidence manifest version'
}
if ([string]::IsNullOrWhiteSpace([string]$manifest.BuildIdentity)) {
    $errors += 'P10 production BuildIdentity is not set'
}

foreach ($item in $manifest.Items) {
    if (-not $item.Required) { continue }
    $label = [string]$item.Name
    if ($allowedKinds -notcontains [string]$item.Kind) {
        $errors += "${label}: unsupported evidence kind"
        continue
    }
    if ($item.Kind -ceq 'Commit') {
        $resolved = (& git -C $repo rev-parse --verify "$($manifest.SourceCommit)^{commit}" 2>$null).Trim()
        if ($LASTEXITCODE -ne 0 -or $resolved -cne $manifest.SourceCommit) {
            $errors += "${label}: commit reference does not resolve exactly"
        } else {
            Write-Host "PASS: $label -> $resolved"
        }
        continue
    }
    if ([string]::IsNullOrWhiteSpace([string]$item.Path)) {
        $errors += "${label}: evidence path is not linked"
        continue
    }
    if ([string]::IsNullOrWhiteSpace([string]$item.Sha256) -or
        [string]$item.Sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        $errors += "${label}: SHA256 is required"
        continue
    }
    $path = Resolve-EvidencePath ([string]$item.Path)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $errors += "${label}: evidence must be a regular file: $($item.Path)"
        continue
    }
    $file = Get-Item -LiteralPath $path
    if ($file.Length -le 0) {
        $errors += "${label}: evidence file is empty"
        continue
    }
    $canonicalPath = (Resolve-Path -LiteralPath $path).Path
    if ($seenPaths.ContainsKey($canonicalPath)) {
        $errors += "${label}: evidence file is reused by '$($seenPaths[$canonicalPath])'"
        continue
    }
    $seenPaths[$canonicalPath] = $label
    $actualSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
    if ($actualSha -cne ([string]$item.Sha256).ToLowerInvariant()) {
        $errors += "${label}: SHA256 mismatch"
        continue
    }

    switch ([string]$item.Kind) {
        'Archive' {
            if ($actualSha -cne 'c540d70540dc4d61e15d1ca13579cd2342d4ea972ff0a74a1afccc04b1ef4aca') {
                $errors += "${label}: frozen archive fingerprint mismatch"
            }
        }
        'Firmware' {
            if ($file.Extension.ToLowerInvariant() -notin @('.bin', '.ufw', '.fw') -or $file.Length -lt 1024) {
                $errors += "${label}: firmware extension/size is invalid"
            }
        }
        'Elf' {
            $stream = [System.IO.File]::OpenRead($path)
            try {
                $magic = New-Object byte[] 4
                $read = $stream.Read($magic, 0, 4)
            } finally {
                $stream.Dispose()
            }
            if ($read -ne 4 -or $magic[0] -ne 0x7f -or $magic[1] -ne 0x45 -or
                $magic[2] -ne 0x4c -or $magic[3] -ne 0x46) {
                $errors += "${label}: file does not have an ELF header"
            }
        }
        'Map' {
            $map = Get-Content -Raw -LiteralPath $path
            foreach ($token in @('rdx_record_get_status', 'rdx_record_process', [string]$manifest.BuildIdentity)) {
                if ([string]::IsNullOrWhiteSpace($token) -or -not $map.Contains($token)) {
                    $errors += "${label}: map misses required token '$token'"
                }
            }
        }
        'BuildLog' {
            $log = Get-Content -Raw -LiteralPath $path
            if (-not $log.Contains([string]$manifest.BuildIdentity) -or
                $log -notmatch '(?i)(build\s+(?:success|succeeded)|0\s+errors?)') {
                $errors += "${label}: build identity/success marker is missing"
            }
        }
        'PersistenceReport' {
            $report = Read-JsonReport $path $label 'RDX_P10_PERSISTENCE_VALIDATION'
            if ($null -ne $report -and
                (-not (Has-Property $report 'passed') -or $report.passed -ne $true -or
                 -not (Has-Property $report 'cases') -or @($report.cases).Count -eq 0)) {
                $errors += "${label}: passed=true and non-empty cases are required"
            }
        }
        'BoardReport' {
            $report = Read-JsonReport $path $label 'RDX_P10_BOARD_REGRESSION'
            if ($null -ne $report -and
                (-not (Has-Property $report 'passed') -or $report.passed -ne $true -or
                 -not (Has-Property $report 'cases') -or @($report.cases).Count -eq 0)) {
                $errors += "${label}: passed=true and non-empty cases are required"
            }
        }
        'IssueReport' {
            $report = Read-JsonReport $path $label 'RDX_P10_RESIDUAL_ISSUES'
            if ($null -ne $report) {
                if (-not (Has-Property $report 'issues')) {
                    $errors += "${label}: issues array is required"
                } else {
                    foreach ($issue in @($report.issues)) {
                        if (-not (Has-Property $issue 'classification') -or
                            -not (Has-Property $issue 'status') -or
                            @('BLOCKER', 'NON_BLOCKER', 'RESOLVED') -notcontains [string]$issue.classification -or
                            ([string]$issue.classification -ceq 'BLOCKER' -and [string]$issue.status -ne 'RESOLVED')) {
                            $errors += "${label}: issue classification/status is invalid"
                        }
                    }
                }
            }
        }
    }
    Write-Host "PASS: $label -> $($item.Path)"
}

if ($errors.Count -gt 0) {
    foreach ($errorMessage in $errors) { Write-Host "BLOCKED: $errorMessage" }
    Write-Host "P10 evidence linkage is incomplete or invalid: $($errors.Count) required issue(s)."
    exit 1
}

Write-Host 'All required P10 production evidence is valid and build-linked.'
