#Requires -Version 5.1
<#
.SYNOPSIS
    Validates the RDX local playback compile-time module boundary.

.DESCRIPTION
    Freezes the source-level contract for enabling and disabling local
    recording playback without compiling firmware. Recording-side stereo
    Opus fixes and the encoder must remain independent from playback.
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$ProtocolDir = Join-Path $RepoRoot 'SDK/apps/common/third_party_profile/rdx_protocol'
$ProjectConfigPath = Join-Path $RepoRoot 'SDK/apps/earphone/include/t2620_project_config.h'
$PlaybackConfigPath = Join-Path $ProtocolDir 'rdx_playback_config.h'
$PlaybackHeaderPath = Join-Path $ProtocolDir 'rdx_playback.h'
$PlaybackPath = Join-Path $ProtocolDir 'rdx_playback.c'
$AppPath = Join-Path $ProtocolDir 'rdx_app.c'
$KeyPath = Join-Path $ProtocolDir 'rdx_key.c'
$SourceHeaderPath = Join-Path $RepoRoot 'SDK/audio/interface/include/source_dev0.h'
$SourcePath = Join-Path $RepoRoot 'SDK/audio/framework/plugs/source/source_dev0_file.c'
$SinkPath = Join-Path $RepoRoot 'SDK/audio/framework/nodes/sink_dev1_node.c'
$EffectPath = Join-Path $RepoRoot 'SDK/audio/framework/nodes/effect_dev2_node.c'
$RecordSourcePath = Join-Path $RepoRoot 'SDK/audio/framework/plugs/source/source_dev1_file.c'
$MakefilePath = Join-Path $RepoRoot 'SDK/Makefile'

$CheckResults = [System.Collections.Generic.List[object]]::new()
$Failed = 0

function Add-CheckResult {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [bool]$Passed,

        [string]$Message = ''
    )

    $script:CheckResults.Add([PSCustomObject]@{
        Name    = $Name
        Passed  = $Passed
        Message = $Message
    })

    if (-not $Passed) {
        $script:Failed++
    }

    if ($Passed) {
        Write-Host "PASS: ${Name}"
    } elseif ($Message) {
        Write-Host "FAIL: ${Name}: ${Message}"
    } else {
        Write-Host "FAIL: ${Name}"
    }
}

function Test-Pattern {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [string]$Text,

        [Parameter(Mandatory)]
        [string]$Pattern,

        [string]$FailureMessage = 'required source pattern not found'
    )

    Add-CheckResult -Name $Name -Passed ([regex]::IsMatch(
        $Text,
        $Pattern,
        [System.Text.RegularExpressions.RegexOptions]::Multiline
    )) -Message $FailureMessage
}

function Get-CArrayItems {
    param(
        [Parameter(Mandatory)]
        [string]$Text,

        [Parameter(Mandatory)]
        [string]$ArrayName
    )

    $escapedName = [regex]::Escape($ArrayName)
    $match = [regex]::Match(
        $Text,
        "(?s)\b$escapedName\s*\[[^\]]*\]\s*=\s*\{(?<body>.*?)\};"
    )
    if (-not $match.Success) {
        throw "C array not found: $ArrayName"
    }

    $body = [regex]::Replace($match.Groups['body'].Value, '/\*.*?\*/', '')
    $body = [regex]::Replace($body, '//[^\r\n]*', '')
    return @($body -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

$RequiredPaths = @(
    $ProjectConfigPath,
    $PlaybackConfigPath,
    $PlaybackHeaderPath,
    $PlaybackPath,
    $AppPath,
    $KeyPath,
    $SourceHeaderPath,
    $SourcePath,
    $SinkPath,
    $EffectPath,
    $RecordSourcePath,
    $MakefilePath
)

foreach ($path in $RequiredPaths) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required file not found: $path"
    }
}

$ProjectConfigText = Get-Content -Raw -Path $ProjectConfigPath
$PlaybackConfigText = Get-Content -Raw -Path $PlaybackConfigPath
$PlaybackHeaderText = Get-Content -Raw -Path $PlaybackHeaderPath
$PlaybackText = Get-Content -Raw -Path $PlaybackPath
$AppText = Get-Content -Raw -Path $AppPath
$KeyText = Get-Content -Raw -Path $KeyPath
$SourceHeaderText = Get-Content -Raw -Path $SourceHeaderPath
$SourceText = Get-Content -Raw -Path $SourcePath
$SinkText = Get-Content -Raw -Path $SinkPath
$EffectText = Get-Content -Raw -Path $EffectPath
$RecordSourceText = Get-Content -Raw -Path $RecordSourcePath
$MakefileText = Get-Content -Raw -Path $MakefilePath

Test-Pattern -Name 'PROJECT_SWITCH_ENABLED' -Text $ProjectConfigText `
    -Pattern '^[ \t]*#define[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t]+1[ \t]*$'

Test-Pattern -Name 'MODULE_FALLBACK_DISABLED' -Text $PlaybackConfigText `
    -Pattern '(?s)#ifndef[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE.*?#define[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t]+0'

$MacroDefinitionFiles = Get-ChildItem -Path (Join-Path $RepoRoot 'SDK') -Recurse -File -Include '*.c', '*.h' |
    Where-Object {
        Select-String -Path $_.FullName -Pattern '^[ \t]*#define[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE\b' -Quiet
    }
$ExpectedMacroFiles = @(
    (Resolve-Path $ProjectConfigPath).Path,
    (Resolve-Path $PlaybackConfigPath).Path
)
$UnexpectedMacroFiles = @($MacroDefinitionFiles.FullName | Where-Object { $_ -notin $ExpectedMacroFiles })
Add-CheckResult -Name 'NO_DUPLICATE_SWITCH_DEFINITIONS' `
    -Passed ($MacroDefinitionFiles.Count -eq 2 -and $UnexpectedMacroFiles.Count -eq 0) `
    -Message $(if ($UnexpectedMacroFiles.Count) { $UnexpectedMacroFiles -join ', ' } else { 'expected exactly two guarded definitions' })

Test-Pattern -Name 'DECODER_FOLLOWS_PLAYBACK' -Text $ProjectConfigText `
    -Pattern '^[ \t]*#define[ \t]+TCFG_DEC_STENC_OPUS_ENABLE[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t]*$'

Test-Pattern -Name 'ENCODER_REMAINS_ENABLED' -Text $ProjectConfigText `
    -Pattern '^[ \t]*#define[ \t]+TCFG_STENC_OPUS_ENABLE[ \t]+1[ \t]*$'

Test-Pattern -Name 'PLAYBACK_HEADER_OWNS_CONFIG' -Text $PlaybackHeaderText `
    -Pattern '#include[ \t]+"rdx_playback_config\.h"'

Test-Pattern -Name 'PLAYBACK_HEADER_OWNS_TYPES' -Text $PlaybackHeaderText `
    -Pattern '#include[ \t]+"typedef\.h"'

Test-Pattern -Name 'PLAYBACK_IMPLEMENTATION_GUARDED' -Text $PlaybackText `
    -Pattern '(?s)#include[ \t]+"rdx_playback\.h"[ \t\r\n]+#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE.*void[ \t]+rdx_playback_next[ \t]*\('

Test-Pattern -Name 'APP_LOADS_FALLBACK_CONFIG' -Text $AppText `
    -Pattern '#include[ \t]+"rdx_playback_config\.h"'

Test-Pattern -Name 'APP_HEADER_GUARDED' -Text $AppText `
    -Pattern '(?s)#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+#include[ \t]+"rdx_playback\.h"[ \t\r\n]+#endif'

Test-Pattern -Name 'APP_PREV_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)case[ \t]+APP_MSG_REC_PREV:.*?#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_prev\(\);[ \t\r\n]+#endif'

Test-Pattern -Name 'APP_NEXT_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)case[ \t]+APP_MSG_REC_NEXT:.*?#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_next\(\);[ \t\r\n]+#endif'

Test-Pattern -Name 'APP_INIT_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_init\(\);[ \t\r\n]+#endif'

Test-Pattern -Name 'KEY_DISABLED_FALLBACK' -Text $KeyText `
    -Pattern '(?s)#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE.*?#define[ \t]+RDX_LOCAL_PLAYBACK_NEXT_KEY_MSG[ \t]+APP_MSG_REC_NEXT.*?#define[ \t]+RDX_LOCAL_PLAYBACK_FR_KEY_MSG[ \t]+APP_MSG_REC_FR.*?#define[ \t]+RDX_LOCAL_PLAYBACK_FF_KEY_MSG[ \t]+APP_MSG_REC_FF.*?#else.*?#define[ \t]+RDX_LOCAL_PLAYBACK_NEXT_KEY_MSG[ \t]+APP_MSG_NULL.*?#define[ \t]+RDX_LOCAL_PLAYBACK_FR_KEY_MSG[ \t]+APP_MSG_NULL.*?#define[ \t]+RDX_LOCAL_PLAYBACK_FF_KEY_MSG[ \t]+APP_MSG_NULL.*?#endif'

$Num0KeyItems = Get-CArrayItems -Text $KeyText -ArrayName 'key_table_io_num0_normal'
$Num1KeyItems = Get-CArrayItems -Text $KeyText -ArrayName 'key_table_io_num1_normal'

$Num0MappingValid = $Num0KeyItems.Count -ge 2 -and
    $Num0KeyItems[0] -eq 'RDX_LOCAL_PLAYBACK_NEXT_KEY_MSG' -and
    $Num0KeyItems[1] -eq 'RDX_LOCAL_PLAYBACK_FR_KEY_MSG'
Add-CheckResult -Name 'KEY0_ACTION_INDEX_MAPPING' -Passed $Num0MappingValid `
    -Message 'expected CLICK=NEXT feature macro and LONG=FR feature macro'

$Num1MappingValid = $Num1KeyItems.Count -ge 2 -and
    $Num1KeyItems[0] -eq 'APP_MSG_NULL' -and
    $Num1KeyItems[1] -eq 'RDX_LOCAL_PLAYBACK_FF_KEY_MSG'
Add-CheckResult -Name 'KEY1_ACTION_INDEX_MAPPING' -Passed $Num1MappingValid `
    -Message 'expected CLICK=APP_MSG_NULL and LONG=FF feature macro'

Test-Pattern -Name 'SOURCE_PUBLIC_API_DECLARED' -Text $SourceHeaderText `
    -Pattern '(?s)source_dev0_input_write\s*\(.*source_dev0_get_free_space\s*\(.*source_dev0_is_empty\s*\('

$PrivateSourceExterns = [regex]::Matches(
    $PlaybackText,
    '^[ \t]*extern[ \t]+.*source_dev0_',
    [System.Text.RegularExpressions.RegexOptions]::Multiline
)
Add-CheckResult -Name 'NO_PLAYBACK_PRIVATE_SOURCE_EXTERNS' -Passed ($PrivateSourceExterns.Count -eq 0) `
    -Message "found $($PrivateSourceExterns.Count) private Source_Dev0 extern declaration(s)"

Test-Pattern -Name 'PLAYBACK_INCLUDES_SOURCE_API' -Text $PlaybackText `
    -Pattern '#include[ \t]+"source_dev0\.h"'

Test-Pattern -Name 'SINK_INCLUDES_SOURCE_API' -Text $SinkText `
    -Pattern '#include[ \t]+"source_dev0\.h"'

Test-Pattern -Name 'SOURCE_STEREO_BRANCH_GUARDED' -Text $SourceText `
    -Pattern '(?s)#if[ \t]+defined\(TCFG_RDX_LOCAL_PLAYBACK_ENABLE\)[ \t]+&&[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE.*AUDIO_CODING_STENC_OPUS.*?#endif'

Test-Pattern -Name 'SOURCE_MONO_PATH_RETAINED' -Text $SourceText `
    -Pattern 'fmt->coding_type[ \t]*=[ \t]*AUDIO_CODING_OPUS'

$RecordingFixUsesPlaybackSwitch = $EffectText.Contains('TCFG_RDX_LOCAL_PLAYBACK_ENABLE') -or
    $RecordSourceText.Contains('TCFG_RDX_LOCAL_PLAYBACK_ENABLE')
Add-CheckResult -Name 'RECORDING_FIXES_INDEPENDENT' -Passed (-not $RecordingFixUsesPlaybackSwitch) `
    -Message 'recording-side source contains the local playback switch'

Test-Pattern -Name 'PLAYBACK_SOURCE_REMAINS_IN_MAKEFILE' -Text $MakefileText `
    -Pattern 'apps/common/third_party_profile/rdx_protocol/rdx_playback\.c'

Write-Host ''
Write-Host '-------------------'
if ($Failed -eq 0) {
    Write-Host "All $($CheckResults.Count) RDX local playback configuration checks passed."
    exit 0
}

Write-Host "$Failed of $($CheckResults.Count) RDX local playback configuration checks failed."
exit 1
