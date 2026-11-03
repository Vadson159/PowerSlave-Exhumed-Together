@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo   Running PowerSlave Exhumed Together Automated Tests
echo =========================================================

set SCRIPT_DIR=%~dp0
set BIN_DIR=%SCRIPT_DIR%..\build\Release

if not exist "%BIN_DIR%\pse_test_suite.exe" (
    echo [ERROR] Binaries not found. Please run build.bat first!
    exit /b 1
)

echo [1/2] Running 15-Scenario Validation Test Suite...
"%BIN_DIR%\pse_test_suite.exe"
if %ERRORLEVEL% neq 0 (
    echo [FAILURE] Test suite encountered errors!
    exit /b %ERRORLEVEL%
)

echo.
echo [2/2] Running Scalability & Latency Benchmarks...
"%BIN_DIR%\pse_benchmark.exe"

echo.
echo [COMPLETE] All automated tests executed successfully.
echo Detailed report generated at: %SCRIPT_DIR%..\docs\TEST_RESULTS.md
exit /b 0
