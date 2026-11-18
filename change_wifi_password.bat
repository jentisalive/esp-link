@echo off
setlocal
title ESP Link - Change Wi-Fi password over USB
cd /d "%~dp0"
if errorlevel 1 goto folder_error
set "PYTHON=%~dp0.venv\Scripts\python.exe"
if not exist "%PYTHON%" goto missing_python
echo Close Arduino Serial Monitor and get_token.bat before continuing.
echo This changes the Wi-Fi password only. It does NOT flash or erase the board.
echo The device token will NOT change.
echo.
if "%~1"=="" (
    "%PYTHON%" scripts\change_wifi_password.py
) else (
    "%PYTHON%" scripts\change_wifi_password.py --port "%~1"
)
if errorlevel 1 goto change_error
echo.
pause
exit /b 0
:change_error
echo.
echo Password change was not confirmed. Read the message above.
pause
exit /b 1
:missing_python
echo ERROR: .venv\Scripts\python.exe not found. See README.md for setup.
pause
exit /b 1
:folder_error
echo ERROR: cannot open the project folder.
pause
exit /b 1
