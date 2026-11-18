@echo off
setlocal
title ESP Link - Change device token over USB
cd /d "%~dp0"
if errorlevel 1 exit /b 1
set "PYTHON=%~dp0.venv\Scripts\python.exe"
if not exist "%PYTHON%" goto missing_python
echo Close Arduino Serial Monitor and get_token.bat before continuing.
echo This replaces the device token only. No flashing or Wi-Fi password change.
echo.
if "%~1"=="" (
    "%PYTHON%" scripts\change_device_token.py
) else (
    "%PYTHON%" scripts\change_device_token.py --port "%~1"
)
set "RESULT=%ERRORLEVEL%"
pause
exit /b %RESULT%

:missing_python
echo ERROR: .venv\Scripts\python.exe not found. See README.md for setup.
pause
exit /b 1
