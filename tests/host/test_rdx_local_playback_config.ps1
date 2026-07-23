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
$AppMsgPath = Join-Path $RepoRoot 'SDK/apps/earphone/include/app_msg.h'
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
$AppMsgText = Get-Content -Raw -Path $AppMsgPath
$SourceHeaderText = Get-Content -Raw -Path $SourceHeaderPath
$SourceText = Get-Content -Raw -Path $SourcePath
$SinkText = Get-Content -Raw -Path $SinkPath
$EffectText = Get-Content -Raw -Path $EffectPath
$RecordSourceText = Get-Content -Raw -Path $RecordSourcePath
$MakefileText = Get-Content -Raw -Path $MakefilePath

Test-Pattern -Name 'VALIDATION_PROFILE_PLAYBACK_DISABLED' -Text $ProjectConfigText `
    -Pattern '^[ \t]*#define[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t]+0[ \t]*$'

Test-Pattern -Name 'VALIDATION_PROFILE_OGG_DECODER_DISABLED' -Text $ProjectConfigText `
    -Pattern '^[ \t]*#define[ \t]+TCFG_DEC_OGG_OPUS_ENABLE[ \t]+0[ \t]*$'

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
    -Pattern '(?s)#include[ \t]+"rdx_playback\.h"[ \t\r\n]+#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE.*int[ \t]+rdx_playback_next[ \t]*\('

Test-Pattern -Name 'APP_LOADS_FALLBACK_CONFIG' -Text $AppText `
    -Pattern '#include[ \t]+"rdx_playback_config\.h"'

Test-Pattern -Name 'APP_HEADER_GUARDED' -Text $AppText `
    -Pattern '(?s)#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+#include[ \t]+"rdx_playback\.h"[ \t\r\n]+#endif'

Test-Pattern -Name 'APP_PREV_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)case[ \t]+APP_MSG_REC_PREV:.*?#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_prev\(\);[ \t\r\n]+#endif'

Test-Pattern -Name 'APP_NEXT_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)case[ \t]+APP_MSG_REC_NEXT:.*?#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_next\(\);[ \t\r\n]+#endif'

Test-Pattern -Name 'APP_FR_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)case[ \t]+APP_MSG_REC_FR:.*?#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_fr\(\);[ \t\r\n]+#endif'

Test-Pattern -Name 'APP_FF_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)case[ \t]+APP_MSG_REC_FF:.*?#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_ff\(\);[ \t\r\n]+#endif'

Test-Pattern -Name 'PLAY_PAUSE_MESSAGES_DECLARED' -Text $AppMsgText `
    -Pattern '(?s)APP_MSG_REC_PREV,.*?APP_MSG_REC_NEXT,.*?APP_MSG_REC_FR,.*?APP_MSG_REC_FF,.*?APP_MSG_REC_PLAY,.*?APP_MSG_REC_PAUSE,'

Test-Pattern -Name 'APP_PLAY_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)case[ \t]+APP_MSG_REC_PLAY:.*?#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_play\(\);[ \t\r\n]+#endif'

Test-Pattern -Name 'APP_PAUSE_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)case[ \t]+APP_MSG_REC_PAUSE:.*?#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_pause\(\);[ \t\r\n]+#endif'

Add-CheckResult -Name 'RECORD_START_PREEMPTS_PLAYBACK_ON_APP_CORE' -Passed (
    $AppText -match '(?s)static\s+void\s+rdx_app_record_cmd_on_app_core\s*\([^)]*\).*?rdx_ble_session_rdx_token_resolve\s*\(&request->token,\s*1\).*?RECORD_STATE_START.*?RECORD_STATE_RESUME.*?rdx_playback_stop\(\);.*?rdx_record_cmd_handle_from_rdx\(&info,\s*&request->token\);' -and
    $AppText -match '(?s)case\s+PROTOCOL_EVENT_CMD_RECORD:.*?os_taskq_post_type\("app_core",\s*Q_CALLBACK,\s*3,\s*msg\).*?break;' -and
    $AppText -notmatch '(?s)case\s+PROTOCOL_EVENT_CMD_RECORD:.*?rdx_record_cmd_handle\(\(Record_info\s*\*\)data\)'
) -Message 'protocol record start/resume must stop playback and execute record control serially on app_core'

Add-CheckResult -Name 'OFFLINE_RECORD_START_PREEMPTS_PLAYBACK' -Passed (
    $AppText -match '(?s)void\s+rdx_app_device_record_handle\s*\([^)]*\).*?else\s*\{\s*if\s*\(rp->run\s*==\s*RECORD_STATE_STOP\).*?rdx_playback_stop\(\);.*?rp->run\s*=\s*RECORD_STATE_START;'
) -Message 'offline record start must clear an existing local playback pause session first'

Test-Pattern -Name 'APP_INIT_CALL_GUARDED' -Text $AppText `
    -Pattern '(?s)#if[ \t]+TCFG_RDX_LOCAL_PLAYBACK_ENABLE[ \t\r\n]+[ \t]*rdx_playback_init\(\);[ \t\r\n]+#endif'

Add-CheckResult -Name 'KEY_TABLE_DIRECT_MESSAGES' -Passed (
    $KeyText -notmatch 'RDX_LOCAL_PLAYBACK_' -and
    $KeyText -notmatch '#include[ \t]+"rdx_playback_config\.h"'
) -Message 'key table should send APP_MSG_REC_* directly; app handler owns playback feature gating'

$Num0KeyItems = Get-CArrayItems -Text $KeyText -ArrayName 'key_table_io_num0_normal'
$Num1KeyItems = Get-CArrayItems -Text $KeyText -ArrayName 'key_table_io_num1_normal'

$Num0MappingValid = $Num0KeyItems.Count -ge 5 -and
    $Num0KeyItems[0] -eq 'APP_MSG_REC_NEXT' -and
    $Num0KeyItems[1] -eq 'APP_MSG_REC_FF' -and
    $Num0KeyItems[4] -eq 'APP_MSG_REC_PLAY'
Add-CheckResult -Name 'KEY0_ACTION_INDEX_MAPPING' -Passed $Num0MappingValid `
    -Message 'expected KEY1 CLICK=NEXT, LONG=FF, and DOUBLE_CLICK=PLAY app messages'

$Num1MappingValid = $Num1KeyItems.Count -ge 5 -and
    $Num1KeyItems[0] -eq 'APP_MSG_REC_PREV' -and
    $Num1KeyItems[1] -eq 'APP_MSG_REC_FR' -and
    $Num1KeyItems[4] -eq 'APP_MSG_REC_PAUSE'
Add-CheckResult -Name 'KEY1_ACTION_INDEX_MAPPING' -Passed $Num1MappingValid `
    -Message 'expected KEY2 CLICK=PREV, LONG=FR, and DOUBLE_CLICK=PAUSE app messages'

Test-Pattern -Name 'SOURCE_PUBLIC_API_DECLARED' -Text $SourceHeaderText `
    -Pattern '(?s)source_dev0_input_write\s*\(.*source_dev0_get_free_space\s*\(.*source_dev0_is_empty\s*\(.*source_dev0_get_consumed_bytes\s*\(.*source_dev0_reset_consumed_bytes\s*\('

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
