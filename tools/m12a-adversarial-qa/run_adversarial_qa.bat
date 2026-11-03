@echo off
setlocal enabledelayedexpansion

echo ================================================================================
echo   Running PowerSlave Exhumed Together - M1.2a Adversarial QA Suite
echo ================================================================================

cd /d "%~dp0"

if not exist "build\Release\m12a_adversarial_qa.exe" (
    echo [ERROR] m12a_adversarial_qa.exe not found! Running build first...
    call build.bat
    if errorlevel 1 (
        echo [FAIL] Build failed!
        exit /b 1
    )
)

build\Release\m12a_adversarial_qa.exe
set QA_EXIT=!ERRORLEVEL!

echo.
echo Audit finished with code !QA_EXIT!. Report generated at:
echo   %~dp0HARDENING_REPORT.md
echo (Original audit baseline preserved at %~dp0REPORT.md)
echo ================================================================================

exit /b !QA_EXIT!
