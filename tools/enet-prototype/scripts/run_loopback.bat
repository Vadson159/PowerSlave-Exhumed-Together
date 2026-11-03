@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo   Launching PowerSlave Exhumed Together Loopback Session
echo   (1 Host [P1] + 7 Autonomous Clients [P2..P8])
echo =========================================================

set SCRIPT_DIR=%~dp0
set BIN_DIR=%SCRIPT_DIR%..\build\Release

if not exist "%BIN_DIR%\pse_host.exe" (
    echo [ERROR] Binaries not found. Please run build.bat first!
    exit /b 1
)

set BIND_PORT=27015

echo [Host] Starting Host on 127.0.0.1:%BIND_PORT%...
start "PSE Host (P1)" "%BIN_DIR%\pse_host.exe" --bind 127.0.0.1 --port %BIND_PORT% --max-clients 7 --tick-rate 60 --snapshot-rate 30

rem Wait for host to bind socket
timeout /t 2 /nobreak >nul

echo [Clients] Launching 7 automated test clients...
for /L %%i in (2,1,8) do (
    echo Starting Client P%%i...
    start "PSE Client P%%i" "%BIN_DIR%\pse_client.exe" --connect 127.0.0.1:%BIND_PORT% --id client_id_%%i --name Player_P%%i --auto-input
    timeout /t 1 /nobreak >nul
)

echo.
echo All 7 clients launched! Check open console windows to observe inputs,
echo snapshots, and live position replication.
echo To close, quit the Host window or close individual client windows.
exit /b 0
