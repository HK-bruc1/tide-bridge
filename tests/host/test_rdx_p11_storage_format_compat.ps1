param(
    [string]$Compiler = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$source = Join-Path $PSScriptRoot 'test_rdx_p11_storage_format_compat.c'
$productionSource = Join-Path $PSScriptRoot `
    '../../SDK/apps/common/third_party_profile/rdx_protocol/compat/rdx_storage_format_compat.c'
$compatInclude = Join-Path $PSScriptRoot `
    '../../SDK/apps/common/third_party_profile/rdx_protocol/compat'
$serviceStubs = Join-Path $PSScriptRoot 'p11_storage_service_stubs'
$commonStubs = Join-Path $PSScriptRoot 'p11_protocol_adapter_stubs'
$output = Join-Path ([System.IO.Path]::GetTempPath()) (
    'rdx_p11_storage_format_compat_' + [guid]::NewGuid().ToString('N'))

if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $command = Get-Command clang -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        $command = Get-Command gcc -ErrorAction SilentlyContinue
    }
    if ($null -eq $command) {
        throw 'No Host C compiler found; pass -Compiler explicitly.'
    }
    $Compiler = $command.Source
}

try {
    & $Compiler -std=c11 -Wall -Wextra -Werror `
        "-I$compatInclude" "-I$serviceStubs" "-I$commonStubs" `
        $source $productionSource -o $output
    if ($LASTEXITCODE -ne 0) {
        throw "P11 storage format compatibility Host compile failed: $LASTEXITCODE"
    }
    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "P11 storage format compatibility Host test failed: $LASTEXITCODE"
    }
} finally {
    if (Test-Path -LiteralPath $output) {
        Remove-Item -LiteralPath $output -Force
    }
}
