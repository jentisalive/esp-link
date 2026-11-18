@echo off
setlocal
title ESP Link - Device token (no flashing)
cd /d "%~dp0"
if errorlevel 1 goto folder_error
set "PYTHON=%~dp0.venv\Scripts\python.exe"
set "PORT=%~1"

echo ============================================================
echo ESP Link - Read device token over USB
echo This script DOES NOT flash or erase the board.
echo ============================================================
echo Connect the ESP32 with a USB DATA cable.
echo Close Arduino Serial Monitor and other programs using its port.
echo When the monitor opens, press RESET / EN on the board.
echo Look for: Device token and Wi-Fi password (separate lines).
echo Use the 12-character device token in the app, NOT the Wi-Fi password.
echo Keep BOTH private. Press Ctrl+C to exit.
echo.
if not exist "%PYTHON%" goto missing_python
if defined PORT (
    "%PYTHON%" -m platformio device monitor --port "%PORT%" --baud 115200
) else (
    "%PYTHON%" -m platformio device monitor --baud 115200
)
if errorlevel 1 goto monitor_error
exit /b 0

:monitor_error
echo.
echo Monitor closed or could not open the port.
echo To select a port, run: get_token.bat COM5
echo Use the COM port shown in Windows Device Manager.
pause
exit /b 1

:missing_python
echo ERROR: .venv\Scripts\python.exe not found.
echo Keep this BAT in the prepared project folder, next to firmware.
echo See README.md for Python / PlatformIO setup on another computer.
pause
exit /b 1

:folder_error
echo ERROR: cannot open the project folder.
pause
exit /b 1
