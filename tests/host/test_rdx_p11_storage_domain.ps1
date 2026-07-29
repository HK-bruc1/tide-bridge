param(
    [string]$Compiler = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$source = Join-Path $PSScriptRoot 'test_rdx_p11_storage_domain.c'
$domainStubs = Join-Path $PSScriptRoot 'p11_storage_domain_stubs'
$commonStubs = Join-Path $PSScriptRoot 'p11_protocol_adapter_stubs'
$output = Join-Path ([System.IO.Path]::GetTempPath()) (
    'rdx_p11_storage_domain_' + [guid]::NewGuid().ToString('N'))

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
        "-I$domainStubs" "-I$commonStubs" $source -o $output
    if ($LASTEXITCODE -ne 0) {
        throw "P11 storage domain Host compile failed: $LASTEXITCODE"
    }
    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "P11 storage domain Host test failed: $LASTEXITCODE"
    }
} finally {
    if (Test-Path -LiteralPath $output) {
        Remove-Item -LiteralPath $output -Force
    }
}
