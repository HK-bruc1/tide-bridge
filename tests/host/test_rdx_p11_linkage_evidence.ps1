Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$manifest = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot 'rdx_p11_linkage_evidence.psd1')
$errors = @()
$seenPaths = @{}
$requiredLinkageSymbols = @(
    'rdx_record_get_status', 'rdx_record_process', 'rdx_record_mode_active_check',
    'rdx_app_get_record_mode', 'rdx_app_get_wifi_info',
    'rdx_app_emmc_poweron', 'rdx_app_emmc_poweroff_check',
    'rdx_app_emmc_poweroff_check_timer_stop'
)
$requiredTypes = @(
    'RecordStatus', 'rdx_tws_sync_record_t', 'MicGainPara', 'ReqFileInfo',
    'RdxProtocolCallbacks', 'RdxProtocolIndicateOps'
)

function Resolve-EvidencePath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
    return Join-Path $repo $Path
}

function Has-Property([object]$Object, [string]$Name) {
    return $null -ne $Object -and $Object.PSObject.Properties.Name -contains $Name
}

function Assert-CommonReport([object]$Report, [string]$ExpectedType, [string]$Label) {
    foreach ($property in @(
        'schema_version', 'report_type', 'source_commit', 'archive_sha256',
        'toolchain_identity', 'build_identity', 'capture_command'
    )) {
        if (-not (Has-Property $Report $property)) {
            $script:errors += "${Label}: missing JSON property '$property'"
            return $false
        }
    }
    if ([int]$Report.schema_version -ne 1 -or
        [string]$Report.report_type -cne $ExpectedType -or
        [string]$Report.source_commit -cne [string]$manifest.SourceCommit -or
        ([string]$Report.archive_sha256).ToLowerInvariant() -cne ([string]$manifest.ArchiveSha256).ToLowerInvariant() -or
        [string]::IsNullOrWhiteSpace([string]$Report.toolchain_identity) -or
        [string]::IsNullOrWhiteSpace([string]$Report.build_identity) -or
        [string]::IsNullOrWhiteSpace([string]$Report.capture_command)) {
        $script:errors += "${Label}: report identity/toolchain metadata is invalid"
        return $false
    }
    return $true
}

if ($manifest.Version -ne 2) {
    throw 'Unsupported P11 linkage evidence manifest version'
}

foreach ($item in $manifest.Items) {
    $label = [string]$item.Name
    if (@('LinkageReport', 'LayoutReport') -notcontains [string]$item.Kind) {
        $errors += "${label}: unsupported evidence kind"
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
    if ((Get-Item -LiteralPath $path).Length -le 0) {
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
    try {
        $report = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
    } catch {
        $errors += "${label}: evidence is not valid JSON"
        continue
    }

    if ($item.Kind -ceq 'LinkageReport') {
        if (-not (Assert-CommonReport $report 'RDX_P11_JL_LINKAGE' $label)) { continue }
        $linkagePropertiesOk = $true
        foreach ($property in @('map_sha256', 'undefined_symbols', 'resolved_symbols')) {
            if (-not (Has-Property $report $property)) {
                $errors += "${label}: missing linkage property '$property'"
                $linkagePropertiesOk = $false
            }
        }
        if (-not $linkagePropertiesOk) { continue }
        if ([string]$report.map_sha256 -notmatch '^[0-9a-fA-F]{64}$') {
            $errors += "${label}: map_sha256 is invalid"
        }
        $undefined = @($report.undefined_symbols)
        $resolved = @($report.resolved_symbols)
        foreach ($symbol in $requiredLinkageSymbols) {
            if ($undefined -notcontains $symbol) {
                $errors += "${label}: undefined-symbol set misses $symbol"
            }
            $matches = @($resolved | Where-Object { [string]$_.symbol -ceq $symbol })
            if ($matches.Count -ne 1 -or
                -not (Has-Property $matches[0] 'definition') -or
                -not (Has-Property $matches[0] 'cross_references') -or
                [string]::IsNullOrWhiteSpace([string]$matches[0].definition) -or
                @($matches[0].cross_references).Count -eq 0) {
                $errors += "${label}: resolved definition/cross-reference is invalid for $symbol"
            }
        }
    } else {
        if (-not (Assert-CommonReport $report 'RDX_P11_JL_LAYOUT' $label)) { continue }
        if (-not (Has-Property $report 'compiler_flags') -or
            [string]::IsNullOrWhiteSpace([string]$report.compiler_flags) -or
            -not (Has-Property $report 'types')) {
            $errors += "${label}: layout compiler/type metadata is incomplete"
            continue
        }
        $types = @($report.types)
        foreach ($typeName in $requiredTypes) {
            $matches = @($types | Where-Object { [string]$_.name -ceq $typeName })
            if ($matches.Count -ne 1 -or
                -not (Has-Property $matches[0] 'size') -or
                [int64]$matches[0].size -le 0) {
                $errors += "${label}: missing or invalid sizeof($typeName)"
            }
        }
        $recordStatus = @($types | Where-Object { [string]$_.name -ceq 'RecordStatus' })
        if ($recordStatus.Count -eq 1) {
            foreach ($field in @('run', 'formate', 'scene', 'mode', 'orig_mode')) {
                if (-not (Has-Property $recordStatus[0] 'offsets') -or
                    -not (Has-Property $recordStatus[0].offsets $field) -or
                    [int64]$recordStatus[0].offsets.$field -lt 0) {
                    $errors += "${label}: missing RecordStatus offsetof($field)"
                }
            }
        }
        $tws = @($types | Where-Object { [string]$_.name -ceq 'rdx_tws_sync_record_t' })
        if ($tws.Count -eq 1 -and
            (-not (Has-Property $tws[0] 'offsets') -or
             -not (Has-Property $tws[0].offsets 'record_status') -or
             [int64]$tws[0].offsets.record_status -lt 0)) {
            $errors += "${label}: missing rdx_tws_sync_record_t offsetof(record_status)"
        }
    }
    Write-Host "PASS: $label -> $($item.Path)"
}

if ($errors.Count -gt 0) {
    foreach ($errorMessage in $errors) { Write-Host "BLOCKED: $errorMessage" }
    Write-Host "P11 Windows/JL ABI linkage evidence is incomplete or invalid: $($errors.Count) gap(s)."
    exit 1
}

Write-Host 'All required P11 Windows/JL ABI linkage evidence is valid.'
