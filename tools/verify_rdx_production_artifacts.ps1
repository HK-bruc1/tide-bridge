param(
    [string]$RepoRoot = '',
    [ValidateSet('zenchord_cc', 'zenchord_ep')]
    [string]$Product = 'zenchord_cc',
    [ValidateSet('jl7018', 'jl7018_shadow')]
    [string]$ChipFamily = 'jl7018',
    [string]$ToolDir = 'C:\JL\pi32\bin',
    [string]$ExpectedArchiveSha256 = 'C540D70540DC4D61E15D1CA13579CD2342D4EA972FF0A74A1AFCCC04B1EF4ACA'
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

$archive = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol/librdxApp.a'
$elf = Join-Path $RepoRoot 'SDK/cpu/br28/tools/sdk.elf'
$map = Join-Path $RepoRoot 'SDK/cpu/br28/tools/sdk.map'
$llvmNm = Join-Path $ToolDir 'llvm-nm.exe'
$pi32Nm = Join-Path $ToolDir 'pi32v2-nm.exe'

foreach ($requiredFile in @($archive, $elf, $map, $llvmNm, $pi32Nm)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required production artifact or tool not found: $requiredFile"
    }
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
    Write-Host "PASS: $Message"
}

function Assert-ArchiveSymbol {
    param(
        [string[]]$Lines,
        [string]$Member,
        [string]$Type,
        [string]$Symbol,
        [string]$Description
    )

    $valuePattern = if ($Type -eq 'U') {
        '\s+U'
    } else {
        '\s+[-0-9A-Fa-f]+\s+{0}' -f [regex]::Escape($Type)
    }
    $pattern = ':{0}:{1}\s+{2}$' -f `
        [regex]::Escape($Member), $valuePattern, [regex]::Escape($Symbol)
    Assert-True ([bool]($Lines -match $pattern)) $Description
}

Write-Host 'RDX production artifact verification'
Write-Host "Product: $Product"
Write-Host "Chip family: $ChipFamily"

$actualHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
Assert-True ($actualHash -eq $ExpectedArchiveSha256) 'librdxApp.a SHA256 matches the P12 baseline'

$archiveSymbols = @(& $llvmNm -A $archive)
if ($LASTEXITCODE -ne 0) {
    throw 'llvm-nm failed while reading librdxApp.a'
}

$archiveDefinitions = @(
    @('rdx_protocol.c.o', 'T', 'rdx_is_file_sync_busy'),
    @('rdx_protocol.c.o', 'T', 'rdx_is_file_transfer_active'),
    @('rdx_protocol.c.o', 'T', 'rdx_protocol_file_sync_busy_timer_stop'),
    @('rdx_protocol.c.o', 'T', 'rdx_protocol_get_uploadfileInfo'),
    @('rdx_protocol.c.o', 'T', 'rdx_protocol_prepared_data_clean'),
    @('rdx_protocol.c.o', 'T', 'rdx_protocol_send_buffer_reinit'),
    @('rdx_protocol.c.o', 'T', 'rdx_protocol_uploadFileInfo_clean'),
    @('rdx_uxfile.c.o', 'T', 'rdx_uxfile_datFileInfo_sendBuf_free'),
    @('rdx_uxfile.c.o', 'T', 'rdx_uxfile_recordFileData_sendBuf_free'),
    @('rdx_uxfile.c.o', 'T', 'rdx_uxfile_recordFileData_send_finish')
)
foreach ($definition in $archiveDefinitions) {
    Assert-ArchiveSymbol $archiveSymbols $definition[0] $definition[1] $definition[2] `
        ("archive keeps {0} in {1}" -f $definition[2], $definition[0])
}
Assert-ArchiveSymbol $archiveSymbols 'rdx_protocol.c.o' 'U' `
    'rdx_uxfile_recordFileData_send_finish' `
    'rdx_protocol.c.o still references the legacy send-finish symbol'
Assert-ArchiveSymbol $archiveSymbols 'xxpUart.c.o' 'U' `
    'rdx_protocol_get_uploadfileInfo' `
    'xxpUart.c.o still references the stable upload-info getter'

$undefinedSymbols = @(& $pi32Nm -u $elf | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
if ($LASTEXITCODE -ne 0) {
    throw 'pi32v2-nm failed while checking unresolved ELF symbols'
}
Assert-True ($undefinedSymbols.Count -eq 0) 'sdk.elf has no unresolved symbols'

$definedSymbols = @(
    & $pi32Nm -g --defined-only $elf |
        ForEach-Object {
            if ($_ -match '^\s*[0-9A-Fa-f]+\s+\S\s+(.+)$') {
                $matches[1]
            }
        }
)
if ($LASTEXITCODE -ne 0) {
    throw 'pi32v2-nm failed while checking duplicate ELF symbols'
}
$duplicateSymbols = @($definedSymbols | Group-Object | Where-Object { $_.Count -gt 1 })
Assert-True ($duplicateSymbols.Count -eq 0) 'sdk.elf has no duplicate global definitions'

$board = if ($Product -eq 'zenchord_cc') { 't2616_cc' } else { 't2616_ep' }
$mapText = Get-Content -LiteralPath $map -Raw
$boardPattern = 'LOAD\s+objs/apps/common/third_party_profile/rdx_protocol/board/{0}/rdx_board_config\.c\.o' -f `
    [regex]::Escape($board)
$portPattern = 'LOAD\s+objs/apps/common/third_party_profile/rdx_protocol/port/jl/{0}/rdx_jl_osal\.c\.o' -f `
    [regex]::Escape($ChipFamily)
Assert-True ([bool]($mapText -match $boardPattern)) "map selects the expected board object: $board"
Assert-True ([bool]($mapText -match $portPattern)) "map selects the expected port object: $ChipFamily"

$elfInfo = Get-Item -LiteralPath $elf
$mapInfo = Get-Item -LiteralPath $map
Write-Host "ELF bytes: $($elfInfo.Length)"
Write-Host "Map bytes: $($mapInfo.Length)"
Write-Host "Archive SHA256: $actualHash"
Write-Host 'RDX production artifact verification passed.'
exit 0
