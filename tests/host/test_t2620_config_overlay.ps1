$ErrorActionPreference = 'Stop'

$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')

function Read-RepoFile($relativePath) {
    Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $repo $relativePath)
}

$errors = @()

$sdkConfigHPath = 'SDK\apps\earphone\board\br28\sdk_config.h'
$sdkConfigCPath = 'SDK\apps\earphone\board\br28\sdk_config.c'
$overlayPath = 'SDK\apps\earphone\include\t2620_project_config.h'
$appConfigPath = 'SDK\apps\earphone\include\app_config.h'
$iokeyConfigPath = 'SDK\apps\earphone\board\iokey_config.c'

$sdkConfigH = Read-RepoFile $sdkConfigHPath
$sdkConfigC = Read-RepoFile $sdkConfigCPath
$appConfig = Read-RepoFile $appConfigPath
$iokeyConfig = Read-RepoFile $iokeyConfigPath

if ($sdkConfigH -match 'TCFG_DIP_SWITCH_POWER') {
    $errors += "$sdkConfigHPath must stay tool-generated and must not define T2620 DIP switch macros"
}

if ($sdkConfigC -match 'TCFG_DIP_SWITCH_POWER') {
    $errors += "$sdkConfigCPath must stay tool-generated and must not contain T2620 DIP switch guards"
}

if (!(Test-Path -LiteralPath (Join-Path $repo $overlayPath))) {
    $errors += "$overlayPath is missing"
} else {
    $overlay = Read-RepoFile $overlayPath
    if ($overlay -notmatch '#define\s+TCFG_DIP_SWITCH_POWER_ENABLE\s+1') {
        $errors += "$overlayPath must define TCFG_DIP_SWITCH_POWER_ENABLE"
    }
    if ($overlay -notmatch '#define\s+TCFG_DIP_SWITCH_POWER_IO\s+IO_PORTB_01') {
        $errors += "$overlayPath must define TCFG_DIP_SWITCH_POWER_IO"
    }
}

if ($appConfig -notmatch '#include\s+"sdk_config\.h"\s*\r?\n#include\s+"t2620_project_config\.h"') {
    $errors += "$appConfigPath must include t2620_project_config.h immediately after sdk_config.h"
}

if ($iokeyConfig -match 'TCFG_DIP_SWITCH_POWER') {
    $errors += "$iokeyConfigPath must NOT reference TCFG_DIP_SWITCH_POWER -- PB1 excluded via tool (not in g_iokey_info[]), no runtime filter needed"
}

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output 'T2620 config overlay structure is valid.'
