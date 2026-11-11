@echo off
setlocal
rem ============================================================================
rem  Rebuilds the CURRENT mod build and packs PSE_Together_friend.zip for friends.
rem    make_friend_package.bat            build (incremental) + pack
rem    make_friend_package.bat /nobuild   only pack what is already built
rem  Result: PSE_Together_friend.zip in this folder (the friend runs install.bat from it).
rem ============================================================================
cd /d "%~dp0"
set "ROOT=%CD%"
set "BUILD=%ROOT%\b99"
set "NOPAUSE="
set "NOBUILD="
:args
if "%~1"=="" goto argsdone
if /i "%~1"=="/nobuild" set "NOBUILD=1"
if /i "%~1"=="/nopause" set "NOPAUSE=1"
shift
goto args
:argsdone

if defined NOBUILD goto pack

set "CMAKE=cmake"
where cmake >nul 2>&1
if errorlevel 1 set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if not exist "%BUILD%\CMakeCache.txt" (
  echo [0/3] First run: configuring %BUILD% ...
  "%CMAKE%" -S "%ROOT%\experiments\pse_together" -B "%BUILD%" -G "Visual Studio 17 2022" -A x64
  if errorlevel 1 goto fail
)
echo [1/3] Building Release (incremental)...
"%CMAKE%" --build "%BUILD%" --config Release --target pse_all --parallel 8
if errorlevel 1 goto fail

:pack
echo [2/3] Packing...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\experiments\pse_together\tools\make_friend_package.ps1" -Root "%ROOT%" -Build "%BUILD%"
if errorlevel 1 goto fail
echo [3/3] Done. Send PSE_Together_friend.zip to your friends.
if not defined NOPAUSE pause
exit /b 0

:fail
echo.
echo FAILED. See the messages above.
if not defined NOPAUSE pause
exit /b 1
