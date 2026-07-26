Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$schema = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'rdx_p11_trace_schema.h')
$spy = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'rdx_p11_trace_spy.c')
$test = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'test_rdx_p11_golden_trace.c')
$makefile = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'Makefile')

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
foreach ($testName in @(
    'test_wrong_context_is_detected',
    'test_wrong_parameter_and_result_are_detected',
    'test_duplicate_ack_or_process_is_detected',
    'test_pool_failure_sequence_has_no_followup',
    'test_task_post_failure_has_no_process',
    'test_timer_failure_has_no_dependent_post',
    'test_schema_size_is_frozen'
)) {
    Assert-Contains $test ([regex]::Escape($testName)) "Host trace contract includes $testName"
}
Assert-Contains $makefile 'test_rdx_p11_golden_trace[^\r\n]*:[^\r\n]*test_rdx_p11_golden_trace\.c[^\r\n]*rdx_p11_trace_spy\.c' `
    'optional Host Makefile has an executable trace-contract target'

$productionReferences = @(Get-ChildItem -LiteralPath (Join-Path $repo 'SDK') -Recurse -File -Include '*.c', '*.h' |
    Select-String -Pattern 'rdx_p11_trace_spy|rdx_p11_trace_schema')
if ($productionReferences.Count -ne 0) {
    throw 'FAIL: Host-only trace scaffold is included by production SDK sources'
}
Write-Host 'PASS: Host trace scaffold is not connected to production SDK sources'

Write-Host 'P11 trace scaffold static checks passed; caller wiring and execution evidence are checked separately.'
