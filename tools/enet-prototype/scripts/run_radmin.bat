@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo   PowerSlave Exhumed Together - Radmin VPN Launcher
echo =========================================================

set SCRIPT_DIR=%~dp0
set BIN_DIR=%SCRIPT_DIR%..\build\Release

if not exist "%BIN_DIR%\pse_host.exe" (
    echo [ERROR] Binaries not found. Please run build.bat first!
    exit /b 1
)

rem Query Radmin VPN IPv4 safely
for /f "usebackq tokens=*" %%a in (`powershell -NoProfile -Command "(Get-NetIPAddress -InterfaceAlias 'Radmin VPN' -AddressFamily IPv4 -ErrorAction SilentlyContinue).IPAddress"`) do (
    set RADMIN_IP=%%a
)

if "%RADMIN_IP%"=="" (
    echo [WARNING] Radmin VPN network adapter not found or inactive.
    echo Please make sure Radmin VPN is installed and switched ON.
    set RADMIN_IP=0.0.0.0
) else (
    echo [INFO] Detected Radmin VPN IPv4: %RADMIN_IP%
)

set DEFAULT_PORT=27015

echo.
echo Select Mode:
echo   [1] Start Host (Bind on %RADMIN_IP%:%DEFAULT_PORT%)
echo   [2] Start Client (Connect to a remote Host via Radmin IP)
echo   [3] Add Windows Firewall Rule for UDP Port %DEFAULT_PORT% (Run as Admin)
echo   [4] Exit
echo.

set /p CHOICE="Enter choice (1-4): "

if "%CHOICE%"=="1" (
    echo Starting Host on %RADMIN_IP%:%DEFAULT_PORT%...
    "%BIN_DIR%\pse_host.exe" --bind %RADMIN_IP% --port %DEFAULT_PORT% --max-clients 7 --tick-rate 60 --snapshot-rate 30
    goto end
)

if "%CHOICE%"=="2" (
    set /p REMOTE_IP="Enter Host Radmin IP address: "
    echo Connecting to !REMOTE_IP!:%DEFAULT_PORT%...
    "%BIN_DIR%\pse_client.exe" --connect !REMOTE_IP!:%DEFAULT_PORT%
    goto end
)

if "%CHOICE%"=="3" (
    echo Creating inbound Windows Firewall UDP rule...
    netsh advfirewall firewall add rule name="PowerSlave Exhumed Together UDP" dir=in action=allow protocol=UDP localport=%DEFAULT_PORT%
    echo Done.
    pause
    goto end
)

:end
exit /b 0
