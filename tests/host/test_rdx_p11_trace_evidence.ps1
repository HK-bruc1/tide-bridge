Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$manifest = Import-PowerShellDataFile -LiteralPath (Join-Path $PSScriptRoot 'rdx_p11_trace_evidence.psd1')
$errors = @()
$seenPaths = @{}
$allowedContexts = @(
    'APP_MESSAGE', 'APP_CORE', 'TIMER', 'BLE_EVENT', 'DUT',
    'CURRENT_SYNC', 'FORMAT_CALLBACK'
)
$allowedOperations = @(
    'FIELD_TRANSITION', 'APP_MESSAGE', 'TASK_POST', 'LOCAL_PROCESS',
    'PROTOCOL_INDICATE', 'POOL_ALLOC', 'POOL_RELEASE', 'TIMER_START',
    'TIMER_STOP', 'TIMER_RESTART', 'STORAGE_CLEANUP', 'FORMAT_CALLBACK',
    'FORMAT_REQUEST'
)
$ownerFields = @(
    'run', 'format', 'scene', 'mode', 'original_mode',
    'switching', 'key_triggered', 'rerun_pending'
)

function Resolve-EvidencePath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $repo $Path
}

function Require-JsonProperty([object]$Object, [string]$Name, [string]$Label) {
    if ($null -eq $Object -or $Object.PSObject.Properties.Name -notcontains $Name) {
        $script:errors += "${Label}: missing JSON property '$Name'"
        return $false
    }
    return $true
}

if ($manifest.Version -ne 2) {
    throw 'Unsupported P11 trace evidence manifest version'
}

foreach ($scenario in $manifest.Scenarios) {
    $label = [string]$scenario.Name
    $contexts = @($scenario.Contexts)
    if ($contexts.Count -eq 0 -or @($contexts | Where-Object { $allowedContexts -notcontains $_ }).Count -gt 0) {
        $errors += "${label}: Contexts must contain only the frozen execution-context enum names"
    }
    if ([string]::IsNullOrWhiteSpace([string]$scenario.CallerSymbol) -or
        [string]$scenario.CallerSymbol -notmatch '^[A-Za-z_]\w*$') {
        $errors += "${label}: CallerSymbol is missing or invalid"
    }
    if ([string]::IsNullOrWhiteSpace([string]$scenario.BaselineTrace)) {
        $errors += "${label}: executable P10 baseline trace is not linked"
        continue
    }
    if ([string]::IsNullOrWhiteSpace([string]$scenario.BaselineTraceSha256) -or
        [string]$scenario.BaselineTraceSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        $errors += "${label}: BaselineTraceSha256 is required"
        continue
    }

    $tracePath = Resolve-EvidencePath ([string]$scenario.BaselineTrace)
    if (-not (Test-Path -LiteralPath $tracePath -PathType Leaf)) {
        $errors += "${label}: trace must be a regular file: $($scenario.BaselineTrace)"
        continue
    }
    if ((Get-Item -LiteralPath $tracePath).Length -le 0) {
        $errors += "${label}: trace file is empty"
        continue
    }
    $canonicalPath = (Resolve-Path -LiteralPath $tracePath).Path
    if ($seenPaths.ContainsKey($canonicalPath)) {
        $errors += "${label}: trace file is reused by scenario '$($seenPaths[$canonicalPath])'"
        continue
    }
    $seenPaths[$canonicalPath] = $label
    $actualSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $tracePath).Hash.ToLowerInvariant()
    if ($actualSha -cne ([string]$scenario.BaselineTraceSha256).ToLowerInvariant()) {
        $errors += "${label}: trace SHA256 mismatch"
        continue
    }

    try {
        $trace = Get-Content -Raw -LiteralPath $tracePath | ConvertFrom-Json
    } catch {
        $errors += "${label}: trace is not valid JSON"
        continue
    }
    $requiredTopLevel = @(
        'schema_version', 'scenario', 'source_commit', 'build_identity',
        'toolchain_identity', 'capture_mode', 'capture_command',
        'caller_symbol', 'executed', 'samples'
    )
    $topLevelOk = $true
    foreach ($property in $requiredTopLevel) {
        if (-not (Require-JsonProperty $trace $property $label)) {
            $topLevelOk = $false
        }
    }
    if (-not $topLevelOk) {
        continue
    }
    if ([int]$trace.schema_version -ne 1 -or
        [string]$trace.scenario -cne $label -or
        [string]$trace.source_commit -cne [string]$manifest.P10Commit -or
        [string]$trace.caller_symbol -cne [string]$scenario.CallerSymbol -or
        $trace.executed -ne $true) {
        $errors += "${label}: trace identity/execution metadata does not match the manifest"
    }
    if ([string]::IsNullOrWhiteSpace([string]$trace.build_identity) -or
        [string]::IsNullOrWhiteSpace([string]$trace.toolchain_identity) -or
        [string]::IsNullOrWhiteSpace([string]$trace.capture_command) -or
        @('HOST_SPY', 'TARGET_DIAGNOSTIC') -notcontains [string]$trace.capture_mode) {
        $errors += "${label}: trace producer metadata is incomplete"
    }

    $samples = @($trace.samples)
    if ($samples.Count -eq 0) {
        $errors += "${label}: trace has no samples"
        continue
    }
    for ($i = 0; $i -lt $samples.Count; $i++) {
        $sample = $samples[$i]
        $sampleOk = $true
        foreach ($property in @('sequence_id', 'execution_context', 'operation', 'result', 'arg0', 'arg1', 'owner_state')) {
            if (-not (Require-JsonProperty $sample $property "$label sample[$i]")) {
                $sampleOk = $false
            }
        }
        if (-not $sampleOk) {
            continue
        }
        foreach ($numericField in @('sequence_id', 'result', 'arg0', 'arg1')) {
            if ([string]$sample.$numericField -notmatch '^-?\d+$') {
                $errors += "$label sample[$i]: $numericField is not an integer"
                $sampleOk = $false
            }
        }
        if (-not $sampleOk) {
            continue
        }
        if ([int]$sample.sequence_id -ne $i -or
            $contexts -notcontains [string]$sample.execution_context -or
            $allowedOperations -notcontains [string]$sample.operation) {
            $errors += "$label sample[$i]: sequence/context/operation is invalid"
        }
        foreach ($field in $ownerFields) {
            if (Require-JsonProperty $sample.owner_state $field "$label sample[$i].owner_state") {
                if ([string]$sample.owner_state.$field -notmatch '^\d+$') {
                    $errors += "$label sample[$i].owner_state: $field is not an unsigned integer"
                }
            }
        }
    }
    Write-Host "PASS: $label -> $($scenario.BaselineTrace)"
}

if ($errors.Count -gt 0) {
    foreach ($errorMessage in $errors) {
        Write-Host "BLOCKED: $errorMessage"
    }
    Write-Host "P11 caller-context/golden-trace evidence is incomplete or invalid: $($errors.Count) gap(s)."
    exit 1
}

Write-Host 'All required P11 caller contexts and executable P10 traces are valid.'
