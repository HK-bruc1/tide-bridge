param(
    [ValidateSet('Host', 'P10', 'P11Static', 'P11Readiness')]
    [string]$Mode = 'Host',
    [ValidateSet('Baseline', 'Progress', 'Final')]
    [string]$OwnershipMode = 'Progress',
    [string]$OwnershipBaselineRef = '128eb8d',
    [string]$AbiBaselineRef = '5d0284f17006bd333de992ed22d5a1c7484c37a0',
    [string]$NmTool = '',
    [string]$Compiler = 'C:\JL\pi32\bin\clang.exe',
    [string]$MakeCommand = '',
    [switch]$VerboseOutput
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$powerShell = (Get-Process -Id $PID).Path
$powerShellArgs = @('-NoProfile')
if ($env:OS -eq 'Windows_NT') {
    $powerShellArgs += @('-ExecutionPolicy', 'Bypass')
}
$failures = @()
if ([string]::IsNullOrWhiteSpace($MakeCommand)) {
    $MakeCommand = Join-Path $repo 'SDK/tools/utils/make.exe'
} elseif (-not [System.IO.Path]::IsPathRooted($MakeCommand)) {
    $MakeCommand = Join-Path $repo $MakeCommand
}

function Invoke-Check {
    param(
        [string]$Name,
        [string]$Script,
        [string[]]$Arguments = @(),
        [switch]$CollectFailure
    )

    $output = @(& $powerShell @powerShellArgs -File $Script @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -or $VerboseOutput) {
        $output | ForEach-Object { Write-Host $_ }
    }
    if ($exitCode -eq 0) {
        Write-Host "PASS: $Name"
        return
    }
    if ($CollectFailure) {
        $script:failures += $Name
        return
    }
    throw "$Name failed with exit code $exitCode"
}

function Invoke-ConfigurationMatrix {
    if ($env:OS -ne 'Windows_NT') {
        throw 'The Windows/JL configuration matrix requires the production environment.'
    }
    Invoke-Check 'Windows/JL configuration matrix' `
        (Join-Path $repo 'tests/host/test_rdx_config_matrix.ps1') `
        @('-Compiler', $Compiler, '-MakeCommand', $MakeCommand)
}

function Invoke-P11Contracts {
    $abiArguments = @('-BaselineRef', $AbiBaselineRef)
    if (-not [string]::IsNullOrWhiteSpace($NmTool)) {
        $abiArguments += @('-NmTool', $NmTool)
    }

    Invoke-Check 'Storage contract' `
        (Join-Path $repo 'tests/host/test_rdx_storage_contract.ps1')
    Invoke-Check 'Record recovery contract' `
        (Join-Path $repo 'tests/host/test_rdx_record_recovery.ps1')
    Invoke-Check 'Static-library ABI' `
        (Join-Path $repo 'tests/host/test_rdx_static_library_abi.ps1') `
        $abiArguments
    Invoke-Check 'P11 ownership' `
        (Join-Path $repo 'tests/host/test_rdx_p11_ownership.ps1') `
        @('-Mode', $OwnershipMode, '-BaselineRef', $OwnershipBaselineRef)
    Invoke-Check 'P11 exact allowlist' `
        (Join-Path $repo 'tests/host/test_rdx_p11_boundaries.ps1') `
        @('-Mode', $OwnershipMode)
    Invoke-Check 'P11 trace source contract' `
        (Join-Path $repo 'tests/host/test_rdx_p11_trace_scaffold.ps1')
    Invoke-Check 'P11 protocol adapter Host contract' `
        (Join-Path $repo 'tests/host/test_rdx_p11_protocol_adapter.ps1')
    Invoke-Check 'P11 storage domain Host contract' `
        (Join-Path $repo 'tests/host/test_rdx_p11_storage_domain.ps1')
    Invoke-Check 'P11 storage service Host contract' `
        (Join-Path $repo 'tests/host/test_rdx_p11_storage_service.ps1')
    Invoke-Check 'P11 storage format compatibility Host contract' `
        (Join-Path $repo 'tests/host/test_rdx_p11_storage_format_compat.ps1')
    Invoke-Check 'P11 file-transfer cleanup Host contract' `
        (Join-Path $repo 'tests/host/test_rdx_p11_file_transfer_cleanup.ps1')
}

Write-Host "RDX verification: $Mode"

switch ($Mode) {
    'Host' {
        Invoke-ConfigurationMatrix
        Invoke-P11Contracts
    }
    'P10' {
        Invoke-Check 'Source boundaries' `
            (Join-Path $repo 'tools/check_rdx_boundaries.ps1')
        Invoke-Check 'Storage contract' `
            (Join-Path $repo 'tests/host/test_rdx_storage_contract.ps1')
        Invoke-Check 'Static-library ABI' `
            (Join-Path $repo 'tests/host/test_rdx_static_library_abi.ps1') `
            @('-BaselineRef', $AbiBaselineRef)
        Invoke-Check 'Record recovery contract' `
            (Join-Path $repo 'tests/host/test_rdx_record_recovery.ps1')
        Invoke-ConfigurationMatrix
    }
    'P11Static' {
        Invoke-Check 'Source boundaries' `
            (Join-Path $repo 'tools/check_rdx_boundaries.ps1')
        Invoke-P11Contracts
    }
    'P11Readiness' {
        Invoke-Check 'Source boundaries' `
            (Join-Path $repo 'tools/check_rdx_boundaries.ps1')
        Invoke-P11Contracts
        Invoke-Check 'Windows/JL linkage evidence' `
            (Join-Path $repo 'tests/host/test_rdx_p11_linkage_evidence.ps1') `
            -CollectFailure
        Invoke-Check 'P10 production evidence' `
            (Join-Path $repo 'tests/host/test_rdx_p10_evidence.ps1') `
            -CollectFailure
        Invoke-Check 'P11 caller trace evidence' `
            (Join-Path $repo 'tests/host/test_rdx_p11_trace_evidence.ps1') `
            -CollectFailure
    }
}

if ($failures.Count -ne 0) {
    Write-Host "RDX readiness blocked by $($failures.Count) evidence gate(s):"
    $failures | ForEach-Object { Write-Host "- $_" }
    exit 1
}
Write-Host "RDX $Mode verification passed."
