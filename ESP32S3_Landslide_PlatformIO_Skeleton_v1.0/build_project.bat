@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "PIO_ENV=esp32-s3-mock"
set "ACTION=%~1"
if not defined ACTION set "ACTION=all"

if exist "C:\msys64\ucrt64\bin\g++.exe" (
    set "PATH=C:\msys64\ucrt64\bin;%PATH%"
)

where pio >nul 2>nul
if not errorlevel 1 (
    set "PIO_CMD=pio"
) else if exist "%USERPROFILE%\.platformio\penv\Scripts\platformio.exe" (
    set "PIO_CMD=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
) else if exist "%USERPROFILE%\.platformio\penv\Scripts\pio.exe" (
    set "PIO_CMD=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"
) else (
    where python >nul 2>nul
    if errorlevel 1 (
        echo ERROR: PlatformIO Core and Python were not found.
        echo Install the PlatformIO IDE extension or PlatformIO Core, then try again.
        pause
        exit /b 1
    )
    set "PIO_CMD=python"
    set "PIO_PREFIX=-m platformio"
)

pushd "%PROJECT_DIR%"
if errorlevel 1 (
    echo ERROR: Cannot open project directory "%PROJECT_DIR%".
    pause
    exit /b 1
)

if /i "%ACTION%"=="help" goto usage
if /i "%ACTION%"=="tests" goto tests
if /i "%ACTION%"=="build" goto build
if /i "%ACTION%"=="upload" goto upload
if /i "%ACTION%"=="production-build" goto production_build
if /i "%ACTION%"=="all" goto all

echo ERROR: Unknown action "%ACTION%".
goto usage_error

:all
call :run_tests
if errorlevel 1 goto failed
call :upload_mock
if errorlevel 1 goto failed
call :monitor_mock
goto finished

:tests
call :run_tests
if errorlevel 1 goto failed
echo.
echo Native unit and integration tests passed.
goto finished

:build
echo Building mock firmware for %PIO_ENV%...
call :run_pio run -e %PIO_ENV%
if errorlevel 1 goto failed
echo.
echo Mock firmware build succeeded.
goto finished

:upload
call :upload_mock
if errorlevel 1 goto failed
call :monitor_mock
goto finished

:production_build
echo Building the fail-closed production environment...
call :run_pio run -e esp32-s3-devkitc-1
if errorlevel 1 goto failed
echo.
echo Production target build succeeded. Runtime startup remains gated by approved configuration.
goto finished

:run_tests
echo Running native unit tests...
call :run_pio test -e native-unit-tests
if errorlevel 1 exit /b 1
echo.
echo Running native integration tests...
call :run_pio test -e native-integration-tests
if errorlevel 1 exit /b 1
exit /b 0

:upload_mock
echo Building and uploading synthetic mock firmware to %PIO_ENV%...
if defined PIO_UPLOAD_PORT (
    call :run_pio run -e %PIO_ENV% -t upload --upload-port "%PIO_UPLOAD_PORT%"
) else (
    call :run_pio run -e %PIO_ENV% -t upload
)
if errorlevel 1 exit /b 1
exit /b 0

:monitor_mock
echo.
echo Opening mock CSV serial stream at 115200 baud. Press Ctrl+C to stop.
if defined PIO_MONITOR_PORT (
    call :run_pio device monitor -e %PIO_ENV% -b 115200 -p "%PIO_MONITOR_PORT%"
) else (
    call :run_pio device monitor -e %PIO_ENV% -b 115200
)
exit /b %ERRORLEVEL%

:run_pio
if defined PIO_PREFIX (
    call "%PIO_CMD%" %PIO_PREFIX% %*
) else (
    call "%PIO_CMD%" %*
)
exit /b %ERRORLEVEL%

:usage
echo Usage: build_project.bat [all^|tests^|build^|upload^|production-build]
echo.
echo   all              Run native tests, build/upload mock firmware, open serial monitor.
echo                    This is the default when no action is specified.
echo   tests            Run native unit and integration tests.
echo   build            Build the synthetic ESP32-S3 mock firmware only.
echo   upload           Build/upload mock firmware and open its serial monitor.
echo   production-build Build the fail-closed non-mock ESP32-S3 target only.
echo.
echo Optional port selection: set PIO_UPLOAD_PORT=COMx and/or PIO_MONITOR_PORT=COMx.
popd
exit /b 0

:usage_error
echo.
echo Use "build_project.bat help" for available actions.
popd
pause
exit /b 2

:failed
echo.
echo FAILED. Review the error above. No success status was reported.
popd
pause
exit /b 1

:finished
set "FINAL_EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %FINAL_EXIT_CODE%
