@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\tools\verify_rdx.ps1" -Mode Host %*
exit /b %ERRORLEVEL%
