@echo off
setlocal
chcp 65001 >nul
title T2620 CDC Test
pushd "%~dp0"
py -3 -c "import sys" >nul 2>&1
if not errorlevel 1 goto use_py
python -c "import sys; assert sys.version_info.major == 3" >nul 2>&1
if not errorlevel 1 goto use_python
echo Python 3 was not found. Install Python 3 and enable Add Python to PATH.
goto finish
:use_py
py -3 -X utf8 tests\board\factory_cdc_launcher.py
goto finish
:use_python
python -X utf8 tests\board\factory_cdc_launcher.py
:finish
echo.
pause
popd
endlocal
