@echo off
setlocal enabledelayedexpansion

echo =========================================================
echo   Building PowerSlave Exhumed Together ENet Prototype
echo =========================================================

set SCRIPT_DIR=%~dp0
set PROTOTYPE_DIR=%SCRIPT_DIR%..
set BUILD_DIR=%PROTOTYPE_DIR%\build

if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)

echo [1/2] Configuring CMake (Visual Studio 2022, x64)...
cmake -S "%PROTOTYPE_DIR%" -B "%BUILD_DIR%" -A x64
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed.
    exit /b %ERRORLEVEL%
)

echo [2/2] Compiling Release binaries...
cmake --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed.
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCCESS] Build completed!
echo Binaries located in: %BUILD_DIR%\Release\
echo   - pse_host.exe
echo   - pse_client.exe
echo   - pse_test_suite.exe
echo   - pse_benchmark.exe
exit /b 0
