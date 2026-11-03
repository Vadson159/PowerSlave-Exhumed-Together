@echo off
setlocal enabledelayedexpansion

echo ================================================================================
echo   Building PowerSlave Exhumed Together - M1.2a Adversarial QA (Release x64)
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
echo   Build Successful! Binary located at:
echo   %~dp0build\Release\m12a_adversarial_qa.exe
echo ================================================================================
endlocal
