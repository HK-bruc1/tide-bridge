param(
    [string]$Compiler = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$source = Join-Path $PSScriptRoot 'test_rdx_p11_protocol_adapter.c'
$stubs = Join-Path $PSScriptRoot 'p11_protocol_adapter_stubs'
$output = Join-Path ([System.IO.Path]::GetTempPath()) (
    'rdx_p11_protocol_adapter_' + [guid]::NewGuid().ToString('N'))

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
    & $Compiler -std=c11 -Wall -Wextra -Werror "-I$stubs" $source -o $output
    if ($LASTEXITCODE -ne 0) {
        throw "P11 protocol adapter Host compile failed: $LASTEXITCODE"
    }
    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "P11 protocol adapter Host test failed: $LASTEXITCODE"
    }
} finally {
    if (Test-Path -LiteralPath $output) {
        Remove-Item -LiteralPath $output -Force
    }
}
