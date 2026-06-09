@echo off
rem Enable Monado's simulated HMD and controller (Qwerty) driver
set QWERTY_ENABLE=1

rem Enable Monado's debug GUI (required to view the compositor window and control the HMD/controllers)
set XRT_DEBUG_GUI=1

rem Monado is not registered as the default system OpenXR runtime, so point to it directly:
set XR_RUNTIME_JSON=C:\git\monado\build\openxr_monado-dev.json

rem Check if monado-service is running
tasklist /FI "IMAGENAME eq monado-service.exe" 2>NUL | find /I /N "monado-service.exe" >NUL
if "%ERRORLEVEL%"=="0" (
    echo Monado service is already running.
) else (
    echo Starting Monado service...
    start "" "C:\git\monado\build\src\xrt\targets\service\monado-service.exe"
    rem Give the service a moment to initialize the IPC pipe (using ping as timeout fails on redirected shells)
    ping -n 3 127.0.0.1 >nul
)

rem Run the compiled application, capturing all output to monado_run.log
build\Main.exe > monado_run.log 2>&1
echo Exit code: %ERRORLEVEL%
echo Log written to monado_run.log
type monado_run.log
