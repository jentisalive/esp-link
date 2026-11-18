@echo off
setlocal
title ESP Link - Flash uPesy ESP32
cd /d "%~dp0"
if errorlevel 1 goto folder_error

set "PYTHON=%~dp0.venv\Scripts\python.exe"
set "PORT=%~1"

echo ============================================================
echo ESP Link - uPesy ESP32 Wroom DevKit
echo ============================================================
echo Connect the board using a USB DATA cable.
echo Close Arduino Serial Monitor and other programs using its port.
echo WARNING: uploading replaces the current program on the board.
echo.

if not exist "%PYTHON%" goto missing_python
if not exist "firmware\platformio.ini" goto missing_project

if defined PORT (
    echo Upload port: %PORT%
) else (
    echo Upload port: automatic detection
)
echo If stuck at Connecting..., hold BOOT until uploading starts.
echo.
pause

if defined PORT (
    "%PYTHON%" -m platformio run --project-dir firmware --environment upesy_wroom --target upload --upload-port "%PORT%"
) else (
    "%PYTHON%" -m platformio run --project-dir firmware --environment upesy_wroom --target upload
)
if errorlevel 1 goto upload_error

echo.
echo Firmware uploaded successfully.
choice /C YN /N /M "Open Serial Monitor to see the device token? [Y/N]: "
if errorlevel 2 goto done
if errorlevel 1 goto monitor
goto done

:monitor
echo.
echo Serial Monitor: 115200 baud.
echo Press RESET / EN on the board after the monitor opens.
echo Look for: Device token / Wi-Fi password
echo Keep this token private. Enter it in the ESP Link iOS app.
echo Press Ctrl+C to close the monitor.
echo.
if defined PORT (
    "%PYTHON%" -m platformio device monitor --port "%PORT%" --baud 115200
) else (
    "%PYTHON%" -m platformio device monitor --baud 115200
)
goto done

:upload_error
echo.
echo ERROR: firmware upload failed. Read the messages above.
echo Check the USB cable, close other serial monitors, and try BOOT.
echo To select a port manually, run: flash_esp32.bat COM5
echo Replace COM5 with the port shown in Windows Device Manager.
pause
exit /b 1

:missing_python
echo ERROR: project Python environment is missing: .venv\Scripts\python.exe
echo This BAT uses the environment prepared in the original project folder.
echo See README.md to install Python and PlatformIO on another computer.
pause
exit /b 1

:missing_project
echo ERROR: firmware\platformio.ini not found.
echo Keep this BAT in the project root, next to README.md and firmware.
pause
exit /b 1

:folder_error
echo ERROR: cannot open the project folder.
pause
exit /b 1

:done
echo.
echo Done. Open ESP Link on iPhone and connect using your device token.
pause
exit /b 0
