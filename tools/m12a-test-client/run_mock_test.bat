@echo off
setlocal enabledelayedexpansion

echo ================================================================================
echo   PowerSlave Exhumed Together — M1.2a Test Client Mock Runner
echo ================================================================================

cd /d "%~dp0"

set EXE_PATH=%~dp0build\Release\m12a_test_client.exe

if not exist "%EXE_PATH%" (
    echo [INFO] Binary not found. Building now...
    call build.bat
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] Build failed.
        exit /b 1
    )
)

if "%~1"=="" (
    echo [RUN] Executing self-test against in-process mock host...
    "%EXE_PATH%" --self-test --report-file TEST_REPORT.md --json-file test_summary.json
) else (
    echo [RUN] Executing with custom arguments: %*
    "%EXE_PATH%" %*
)

set EXITCODE=%ERRORLEVEL%
echo.
echo ================================================================================
echo   Test Execution Finished with Exit Code: %EXITCODE%
echo ================================================================================
exit /b %EXITCODE%
