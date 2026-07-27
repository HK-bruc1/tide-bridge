param(
    [string]$Compiler = 'C:\JL\pi32\bin\clang.exe',
    [string]$MakeCommand = ''
)

& (Join-Path $PSScriptRoot 'verify_rdx.ps1') -Mode P10 `
    -Compiler $Compiler -MakeCommand $MakeCommand
exit $LASTEXITCODE
