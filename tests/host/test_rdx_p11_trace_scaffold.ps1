Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$schema = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'rdx_p11_trace_schema.h')
$spy = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'rdx_p11_trace_spy.c')

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Label) {
    if (-not [regex]::IsMatch($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)) {
        throw "FAIL: $Label"
    }
    Write-Host "PASS: $Label"
}

Assert-Contains $schema 'execution_context[\s\S]*?operation[\s\S]*?result[\s\S]*?arg0[\s\S]*?arg1' `
    'trace schema records context, operation, result and key arguments'
Assert-Contains $schema 'rdx_p11_trace_owner_state_t' `
    'trace sample schema captures owner state at spy boundaries'
Assert-Contains $spy 'expected_count\s*!=\s*actual_count' `
    'trace comparator rejects missing or duplicate operations'
foreach ($field in @('execution_context', 'operation', 'result', 'arg0', 'arg1', 'owner_state')) {
    Assert-Contains $spy ([regex]::Escape($field)) "trace comparator checks $field"
}
Assert-Contains $schema '#define\s+RDX_P11_TRACE_ENTRY_SIZE\s+16u' `
    'trace entry size remains frozen at 16 bytes'

$productionReferences = @(Get-ChildItem -LiteralPath (Join-Path $repo 'SDK') -Recurse -File -Include '*.c', '*.h' |
    Select-String -Pattern 'rdx_p11_trace_spy|rdx_p11_trace_schema')
if ($productionReferences.Count -ne 0) {
    throw 'FAIL: Host-only trace scaffold is included by production SDK sources'
}
Write-Host 'PASS: diagnostic trace source is not connected to production SDK sources'

Write-Host 'P11 trace source contract passed; caller wiring and execution evidence are checked separately.'
