@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "OUTPUT_DIR=%PROJECT_DIR%build_output"
set "PIO_CMD=pio"

where pio >nul 2>nul
if errorlevel 1 (
    if exist "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" (
        set "PIO_CMD=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
    ) else if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" (
        set "PIO_CMD=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
    ) else (
        echo ERROR: PlatformIO Core was not found.
        echo Install the PlatformIO IDE extension or PlatformIO Core, then try again.
        pause
        exit /b 1
    )
)

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

echo Building all PlatformIO environments in "%PROJECT_DIR%"...
pushd "%PROJECT_DIR%"
"%PIO_CMD%" run --project-dir "%PROJECT_DIR%"
set "BUILD_EXIT_CODE=%ERRORLEVEL%"
popd

if not "%BUILD_EXIT_CODE%"=="0" (
    echo.
    echo BUILD FAILED. See the messages above.
    pause
    exit /b %BUILD_EXIT_CODE%
)

echo.
echo BUILD SUCCEEDED. Output is in "%OUTPUT_DIR%".
pause
exit /b 0