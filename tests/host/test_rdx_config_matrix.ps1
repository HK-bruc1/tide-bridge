param(
    [string]$Compiler = "",
    [string]$MakeCommand = ""
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($Compiler)) {
    $Compiler = if (Get-Command cc -ErrorAction SilentlyContinue) { "cc" } else { "gcc" }
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$sdk = Join-Path $repo "SDK"
$protocol = Join-Path $repo "SDK/apps/common/third_party_profile/rdx_protocol"
$source = Join-Path ([System.IO.Path]::GetTempPath()) "rdx_config_$([guid]::NewGuid().ToString('N')).c"
$failures = 0

if ([string]::IsNullOrWhiteSpace($MakeCommand)) {
    $bundledMake = Join-Path $sdk "tools/utils/make.exe"
    $runningOnWindows = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
    $MakeCommand = if ($runningOnWindows -and (Test-Path $bundledMake)) { $bundledMake } else { "make" }
}

function Test-Config {
    param(
        [string]$Name,
        [bool]$ShouldPass,
        [string[]]$Defines
    )

    $arguments = @("-std=c11", "-fsyntax-only", "-I$protocol") + $Defines + @($source)
    $output = & $Compiler @arguments 2>&1
    $passed = ($LASTEXITCODE -eq 0)

    if ($passed -eq $ShouldPass) {
        Write-Host "[PASS] $Name"
        return
    }

    $script:failures++
    Write-Host "[FAIL] $Name"
    $output | ForEach-Object { Write-Host "       $_" }
}

function Test-MakeConfig {
    param(
        [string]$Name,
        [bool]$ShouldPass,
        [string[]]$Variables
    )

    $arguments = @("-C", $sdk) + $Variables + @("rdx_config_check")
    $output = & $MakeCommand @arguments 2>&1
    $passed = ($LASTEXITCODE -eq 0)

    if ($passed -eq $ShouldPass) {
        Write-Host "[PASS] $Name"
        return
    }

    $script:failures++
    Write-Host "[FAIL] $Name"
    $output | ForEach-Object { Write-Host "       $_" }
}

function Test-MakeCompileFlags {
    param(
        [string]$Name,
        [string[]]$Variables,
        [string[]]$ExpectedFlags
    )

    $arguments = @("-C", $sdk, "-n") + $Variables + @("pre_build")
    $output = & $MakeCommand @arguments 2>&1
    $expandedCommands = @($output) -join "`n"
    $missingFlags = @($ExpectedFlags | Where-Object { -not $expandedCommands.Contains($_) })

    if (($LASTEXITCODE -eq 0) -and ($missingFlags.Count -eq 0)) {
        Write-Host "[PASS] $Name"
        return
    }

    $script:failures++
    Write-Host "[FAIL] $Name"
    if ($missingFlags.Count -gt 0) {
        Write-Host "       Missing: $($missingFlags -join ', ')"
    }
    $output | ForEach-Object { Write-Host "       $_" }
}

try {
    [System.IO.File]::WriteAllText($source, @"
#include "rdx_app_config.h"
#ifdef RDX_TEST_EXPECT_LOCAL
_Static_assert(RDX_RECORD_USE_LOCAL_PIPELINE == RDX_TEST_EXPECT_LOCAL, "local pipeline mismatch");
_Static_assert(RDX_RECORD_DISCONNECT_TO_OFFLINE == RDX_TEST_EXPECT_OFFLINE, "disconnect behavior mismatch");
_Static_assert(RDX_RECORD_DISCONNECT_RERUN == RDX_TEST_EXPECT_RERUN, "rerun behavior mismatch");
_Static_assert(RDX_RECORD_SINK_AUTO_INIT == RDX_TEST_EXPECT_SINK, "sink init mismatch");
_Static_assert(RDX_IDLE_WAKE_ON_LONG_PRESS == RDX_TEST_EXPECT_IDLE_KEY, "idle key mismatch");
_Static_assert(RDX_HAS_SK4558_CHARGER == RDX_TEST_EXPECT_CHARGER, "charger mismatch");
_Static_assert(RDX_SUPPORT_KEY_DUT_ENTRY == RDX_TEST_EXPECT_KEY_DUT, "key DUT mismatch");
_Static_assert(RDX_NEEDS_POWER_ACTIVITY_GUARD == RDX_TEST_EXPECT_POWER_GUARD, "power guard mismatch");
#endif
int main(void) { return 0; }
"@)

    $recordCardApps = @(
        "APP_NEVIEW_EN", "APP_NINGQU_EN", "APP_NOTTA_EN", "APP_TINGNAO_EN",
        "APP_JMEASY_EN", "APP_SHENGLANG_EN", "APP_AITIR_EN", "APP_YYS_EN",
        "APP_LYNSE_EN", "APP_TURING_EN", "APP_RAYCON_EN", "APP_CDJY_EN",
        "APP_BRANDWORKS_EN", "APP_FINDAI_EN", "APP_BEANSTALK_EN", "APP_DEEPMINER_EN"
    )

    $localPipelineApps = @(
        "APP_NEVIEW_EN", "APP_NINGQU_EN", "APP_JMEASY_EN", "APP_SHENGLANG_EN",
        "APP_YYS_EN", "APP_LYNSE_EN", "APP_RAYCON_EN", "APP_CDJY_EN",
        "APP_BRANDWORKS_EN", "APP_FINDAI_EN", "APP_BEANSTALK_EN", "APP_DEEPMINER_EN"
    )
    $sinkInitApps = @("APP_TINGNAO_EN", "APP_AITIR_EN", "APP_TURING_EN")

    foreach ($app in $recordCardApps) {
        $local = [int]($localPipelineApps -contains $app)
        $sink = [int]($sinkInitApps -contains $app)
        $rerun = [int]($app -ne "APP_TURING_EN")
        $idleKey = [int]($app -eq "APP_TINGNAO_EN")
        Test-Config $app $true @(
            "-DRDX_AI_SEL_APP=$app",
            "-DRDX_SEL_DEVICE=DEVICE_RDX_BJ_T2403",
            "-DRDX_TEST_EXPECT_LOCAL=$local",
            "-DRDX_TEST_EXPECT_OFFLINE=$local",
            "-DRDX_TEST_EXPECT_RERUN=$rerun",
            "-DRDX_TEST_EXPECT_SINK=$sink",
            "-DRDX_TEST_EXPECT_IDLE_KEY=$idleKey",
            "-DRDX_TEST_EXPECT_CHARGER=1",
            "-DRDX_TEST_EXPECT_KEY_DUT=1",
            "-DRDX_TEST_EXPECT_POWER_GUARD=1"
        )
    }

    Test-Config "Zenchord CC" $true @(
        "-DRDX_AI_SEL_APP=APP_ZENCHORD_EN",
        "-DRDX_SEL_DEVICE=DEVICE_ZENCORD_CC_T2616",
        "-DRDX_TEST_EXPECT_LOCAL=1",
        "-DRDX_TEST_EXPECT_OFFLINE=1",
        "-DRDX_TEST_EXPECT_RERUN=1",
        "-DRDX_TEST_EXPECT_SINK=1",
        "-DRDX_TEST_EXPECT_IDLE_KEY=0",
        "-DRDX_TEST_EXPECT_CHARGER=0",
        "-DRDX_TEST_EXPECT_KEY_DUT=1",
        "-DRDX_TEST_EXPECT_POWER_GUARD=1"
    )
    Test-Config "Zenchord EP" $true @(
        "-DRDX_AI_SEL_APP=APP_ZENCHORD_EN",
        "-DRDX_SEL_DEVICE=DEVICE_ZENCORD_EP_T2616",
        "-DRDX_TEST_EXPECT_LOCAL=1",
        "-DRDX_TEST_EXPECT_OFFLINE=1",
        "-DRDX_TEST_EXPECT_RERUN=1",
        "-DRDX_TEST_EXPECT_SINK=1",
        "-DRDX_TEST_EXPECT_IDLE_KEY=0",
        "-DRDX_TEST_EXPECT_CHARGER=0",
        "-DRDX_TEST_EXPECT_KEY_DUT=0",
        "-DRDX_TEST_EXPECT_POWER_GUARD=0"
    )

    Test-Config "unconfigured APP" $false @(
        "-DRDX_AI_SEL_APP=APP_XLSW_EN",
        "-DRDX_SEL_DEVICE=DEVICE_RDX_BJ_T2403"
    )
    Test-Config "unknown DEVICE" $false @(
        "-DRDX_AI_SEL_APP=APP_ZENCHORD_EN",
        "-DRDX_SEL_DEVICE=0xdead"
    )
    Test-Config "invalid APP/DEVICE pair" $false @(
        "-DRDX_AI_SEL_APP=APP_ZENCHORD_EN",
        "-DRDX_SEL_DEVICE=DEVICE_RDX_BJ_T2403"
    )
    Test-Config "multiple APP bits" $false @(
        "-DRDX_AI_SEL_APP=(APP_ZENCHORD_EN|APP_NEVIEW_EN)",
        "-DRDX_SEL_DEVICE=DEVICE_ZENCORD_CC_T2616"
    )

    Test-MakeConfig "Make Zenchord CC profile" $true @(
        "RDX_PRODUCT=zenchord_cc"
    )
    Test-MakeConfig "Make Zenchord EP profile" $true @(
        "RDX_PRODUCT=zenchord_ep"
    )
    Test-MakeConfig "Make chip family override" $true @(
        "RDX_PRODUCT=zenchord_cc",
        "RDX_CHIP_FAMILY=jl7018_shadow"
    )
    Test-MakeConfig "Make DEFINES override keeps RDX selections" $true @(
        "RDX_PRODUCT=zenchord_cc",
        "DEFINES=USER_SUPPLIED"
    )
    Test-MakeCompileFlags "Make pre_build expands RDX selections" @(
        "RDX_PRODUCT=zenchord_cc",
        "DEFINES=USER_SUPPLIED"
    ) @(
        "-DRDX_AI_SEL_APP=APP_ZENCHORD_EN",
        "-DRDX_SEL_DEVICE=DEVICE_ZENCORD_CC_T2616"
    )
    Test-MakeConfig "Make CC device with EP board" $false @(
        "RDX_PRODUCT=zenchord_cc",
        "RDX_SEL_DEVICE=DEVICE_ZENCORD_CC_T2616",
        "RDX_BOARD=t2616_ep"
    )
    Test-MakeConfig "Make product profile path override" $false @(
        "RDX_PRODUCT=zenchord_cc",
        "RDX_PRODUCT_MK=apps/common/third_party_profile/rdx_protocol/config/build/zenchord_ep.mk"
    )
    Test-MakeConfig "Make expected and selected value override" $false @(
        "RDX_PRODUCT=zenchord_cc",
        "RDX_EXPECTED_DEVICE=DEVICE_ZENCORD_EP_T2616",
        "RDX_SEL_DEVICE=DEVICE_ZENCORD_EP_T2616",
        "RDX_EXPECTED_BOARD=t2616_ep",
        "RDX_BOARD=t2616_ep"
    )
    Test-MakeConfig "Make recursive selection expression" $false @(
        "RDX_PRODUCT=zenchord_cc",
        'RDX_SEL_DEVICE=$(eval override RDX_EXPECTED_DEVICE := DEVICE_ZENCORD_EP_T2616)DEVICE_ZENCORD_EP_T2616'
    )
    Test-MakeConfig "Make unknown chip family" $false @(
        "RDX_PRODUCT=zenchord_cc",
        "RDX_CHIP_FAMILY=does_not_exist"
    )
} finally {
    Remove-Item -Force $source -ErrorAction SilentlyContinue
}

if ($failures -gt 0) {
    Write-Host "RDX config matrix failed: $failures case(s)"
    exit 1
}

Write-Host "RDX config matrix passed."
