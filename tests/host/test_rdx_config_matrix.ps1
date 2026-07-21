param(
    [string]$Compiler = "",
    [string]$MakeCommand = ""
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$runningOnWindows = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$sdk = Join-Path $repo "SDK"
$protocol = Join-Path $repo "SDK/apps/common/third_party_profile/rdx_protocol"

if ([string]::IsNullOrWhiteSpace($Compiler)) {
    if (-not $runningOnWindows) {
        throw "-Compiler is required outside the Windows/JL production environment; no compiler is discovered from PATH"
    }
    $Compiler = "C:\JL\pi32\bin\clang.exe"
}
$source = Join-Path ([System.IO.Path]::GetTempPath()) "rdx_config_$([guid]::NewGuid().ToString('N')).c"
$probeSource = Join-Path ([System.IO.Path]::GetTempPath()) "rdx_config_probe_$([guid]::NewGuid().ToString('N')).c"
$failures = 0

if ([string]::IsNullOrWhiteSpace($MakeCommand)) {
    if (-not $runningOnWindows) {
        throw "-MakeCommand is required outside the Windows/JL production environment; no make is discovered from PATH"
    }
    $MakeCommand = Join-Path $sdk "tools/utils/make.exe"
}

function Resolve-Executable {
    param(
        [string]$Command,
        [string]$Label
    )

    if (-not (Test-Path -LiteralPath $Command -PathType Leaf)) {
        throw "$Label path does not exist: $Command"
    }
    return (Resolve-Path -LiteralPath $Command).Path
}

function Invoke-NativeCommand {
    param(
        [string]$Command,
        [string[]]$Arguments
    )

    $previousErrorActionPreference = $ErrorActionPreference
    $started = $false
    try {
        # Windows PowerShell 5 promotes native stderr to NativeCommandError
        # when ErrorActionPreference is Stop. Native non-zero exits are test
        # inputs here, so capture them without terminating the script.
        $ErrorActionPreference = "Continue"
        $output = & $Command @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        $started = $true
    } catch {
        $output = @($_)
        $exitCode = $null
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    return @{
        Output = @($output)
        ExitCode = $exitCode
        Started = $started
    }
}

function Assert-NativeStarted {
    param(
        [hashtable]$Result,
        [string]$Label
    )

    if (-not $Result.Started) {
        $details = @($Result.Output) -join [Environment]::NewLine
        throw "$Label could not be started. $details"
    }
}

function Test-Config {
    param(
        [string]$Name,
        [bool]$ShouldPass,
        [string[]]$Defines,
        [string]$ExpectedDiagnostic = ""
    )

    if (-not $ShouldPass -and [string]::IsNullOrWhiteSpace($ExpectedDiagnostic)) {
        throw "Negative configuration case '$Name' must declare an expected diagnostic"
    }

    $arguments = @("-std=c11", "-fsyntax-only", "-I$protocol") + $Defines + @($source)
    $result = Invoke-NativeCommand $Compiler $arguments
    Assert-NativeStarted $result "Configuration compiler"
    $output = $result.Output
    $outputText = @($output) -join "`n"
    $passed = ($result.ExitCode -eq 0)
    $diagnosticMatched = $ShouldPass -or ($outputText.IndexOf(
        $ExpectedDiagnostic, [System.StringComparison]::Ordinal) -ge 0)

    if (($passed -eq $ShouldPass) -and $diagnosticMatched) {
        Write-Host "[PASS] $Name"
        return
    }

    $script:failures++
    Write-Host "[FAIL] $Name"
    if (-not $ShouldPass -and -not $diagnosticMatched) {
        Write-Host "       Missing expected diagnostic: $ExpectedDiagnostic"
    }
    $output | ForEach-Object { Write-Host "       $_" }
}

function Test-MakeConfig {
    param(
        [string]$Name,
        [bool]$ShouldPass,
        [string[]]$Variables,
        [string]$ExpectedDiagnostic = ""
    )

    if (-not $ShouldPass -and [string]::IsNullOrWhiteSpace($ExpectedDiagnostic)) {
        throw "Negative Make case '$Name' must declare an expected diagnostic"
    }

    $arguments = @("-C", $sdk) + $Variables + @("rdx_config_check")
    $result = Invoke-NativeCommand $MakeCommand $arguments
    Assert-NativeStarted $result "Repository make"
    $output = $result.Output
    $outputText = @($output) -join "`n"
    $passed = ($result.ExitCode -eq 0)
    $diagnosticMatched = $ShouldPass -or ($outputText.IndexOf(
        $ExpectedDiagnostic, [System.StringComparison]::Ordinal) -ge 0)

    if (($passed -eq $ShouldPass) -and $diagnosticMatched) {
        Write-Host "[PASS] $Name"
        return
    }

    $script:failures++
    Write-Host "[FAIL] $Name"
    if (-not $ShouldPass -and -not $diagnosticMatched) {
        Write-Host "       Missing expected diagnostic: $ExpectedDiagnostic"
    }
    $output | ForEach-Object { Write-Host "       $_" }
}

function Test-MakeCompileFlags {
    param(
        [string]$Name,
        [string[]]$Variables,
        [string[]]$ExpectedFlags
    )

    $arguments = @("-C", $sdk, "-n") + $Variables + @("pre_build")
    $result = Invoke-NativeCommand $MakeCommand $arguments
    Assert-NativeStarted $result "Repository make"
    $output = $result.Output
    $expandedCommands = @($output) -join "`n"
    $missingFlags = @($ExpectedFlags | Where-Object { -not $expandedCommands.Contains($_) })

    if (($result.ExitCode -eq 0) -and ($missingFlags.Count -eq 0)) {
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
    $Compiler = Resolve-Executable $Compiler "Configuration compiler"
    $MakeCommand = Resolve-Executable $MakeCommand "Repository make"

    $compilerVersion = Invoke-NativeCommand $Compiler @("--version")
    Assert-NativeStarted $compilerVersion "Configuration compiler"
    if ($compilerVersion.ExitCode -ne 0) {
        $details = @($compilerVersion.Output) -join [Environment]::NewLine
        throw "Configuration compiler version preflight failed. $details"
    }
    Write-Host "Configuration compiler: $Compiler"
    $compilerVersion.Output | ForEach-Object { Write-Host "       $_" }

    [System.IO.File]::WriteAllText($probeSource, @"
_Static_assert(1, "C11 static assert is required");
int main(void) { return 0; }
"@)
    $compilerProbe = Invoke-NativeCommand $Compiler @("-std=c11", "-fsyntax-only", $probeSource)
    Assert-NativeStarted $compilerProbe "Configuration compiler"
    if ($compilerProbe.ExitCode -ne 0) {
        $details = @($compilerProbe.Output) -join [Environment]::NewLine
        throw "Configuration compiler does not support the required C11 syntax. $details"
    }

    $makeProbe = Invoke-NativeCommand $MakeCommand @("--version")
    Assert-NativeStarted $makeProbe "Repository make"
    if ($makeProbe.ExitCode -ne 0) {
        $details = @($makeProbe.Output) -join [Environment]::NewLine
        throw "Repository make preflight failed. $details"
    }
    Write-Host "Repository make: $MakeCommand"
    $makeProbe.Output | ForEach-Object { Write-Host "       $_" }

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
    ) "RDX_AI_SEL_APP has no product config"
    Test-Config "unknown DEVICE" $false @(
        "-DRDX_AI_SEL_APP=APP_ZENCHORD_EN",
        "-DRDX_SEL_DEVICE=0xdead"
    ) "RDX_SEL_DEVICE must be one of DEVICE_*"
    Test-Config "invalid APP/DEVICE pair" $false @(
        "-DRDX_AI_SEL_APP=APP_ZENCHORD_EN",
        "-DRDX_SEL_DEVICE=DEVICE_RDX_BJ_T2403"
    ) "APP_ZENCHORD_EN must pair with DEVICE_ZENCORD_*_T2616"
    Test-Config "multiple APP bits" $false @(
        "-DRDX_AI_SEL_APP=(APP_ZENCHORD_EN|APP_NEVIEW_EN)",
        "-DRDX_SEL_DEVICE=DEVICE_ZENCORD_CC_T2616"
    ) "RDX_AI_SEL_APP has no product config"

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
    ) "requires RDX_BOARD=t2616_cc"
    Test-MakeConfig "Make product profile path override" $false @(
        "RDX_PRODUCT=zenchord_cc",
        "RDX_PRODUCT_MK=apps/common/third_party_profile/rdx_protocol/config/build/zenchord_ep.mk"
    ) "RDX_PRODUCT_MK is internal"
    Test-MakeConfig "Make expected and selected value override" $false @(
        "RDX_PRODUCT=zenchord_cc",
        "RDX_EXPECTED_DEVICE=DEVICE_ZENCORD_EP_T2616",
        "RDX_SEL_DEVICE=DEVICE_ZENCORD_EP_T2616",
        "RDX_EXPECTED_BOARD=t2616_ep",
        "RDX_BOARD=t2616_ep"
    ) "RDX_EXPECTED_DEVICE is internal to RDX_PRODUCT"
    Test-MakeConfig "Make recursive selection expression" $false @(
        "RDX_PRODUCT=zenchord_cc",
        'RDX_SEL_DEVICE=$(eval override RDX_EXPECTED_DEVICE := DEVICE_ZENCORD_EP_T2616)DEVICE_ZENCORD_EP_T2616'
    ) "requires RDX_SEL_DEVICE="
    Test-MakeConfig "Make unknown chip family" $false @(
        "RDX_PRODUCT=zenchord_cc",
        "RDX_CHIP_FAMILY=does_not_exist"
    ) "RDX_CHIP_FAMILY must be the literal jl7018 or jl7018_shadow"
} finally {
    Remove-Item -Force $source -ErrorAction SilentlyContinue
    Remove-Item -Force $probeSource -ErrorAction SilentlyContinue
}

if ($failures -gt 0) {
    Write-Host "RDX config matrix failed: $failures case(s)"
    exit 1
}

Write-Host "RDX config matrix passed."
