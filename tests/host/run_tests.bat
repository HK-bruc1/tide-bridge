@echo off
setlocal

set "HOST_DIR=%~dp0"
if "%HOST_DIR:~-1%"=="\" set "HOST_DIR=%HOST_DIR:~0,-1%"
set "MAKE_EXE=%HOST_DIR%\..\..\SDK\tools\utils\make.exe"

if not exist "%MAKE_EXE%" (
    echo ERROR: repository make.exe not found at %MAKE_EXE%
    exit /b 1
)

"%MAKE_EXE%" -C "%HOST_DIR%" %*
exit /b %ERRORLEVEL%
