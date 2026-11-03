@echo off
setlocal enabledelayedexpansion

echo ================================================================================
echo   Building M1.2b Input Cadence and Jitter QA Tool Suite (Release x64)
echo ================================================================================

cd /d "%~dp0"

if not exist build mkdir build

cmake -B build -S .
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

cmake --build build --config Release --parallel
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo.
echo ================================================================================
echo   Build Successful!
echo   Binaries located in: %~dp0build\Release\
echo     - scheduler_bench.exe
echo     - cadence_qa_host.exe
echo     - precision_client.exe
echo     - net_impairment_proxy.exe
echo ================================================================================
endlocal
