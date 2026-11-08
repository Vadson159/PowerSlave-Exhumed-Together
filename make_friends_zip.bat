@echo off
rem ============================================================================
rem  make_friends_zip.bat - build the friends kit (P25) from CURRENT sources.
rem  Output: docs\evidence\m1\P25_PACKAGING\FRIENDS_<timestamp>\PSE-P25-DIAGNOSTIC-1.zip
rem
rem  NOTE: this is the DIAGNOSTIC kit (standalone ENet host/client + logs + crash
rem  dumps + RU/EN instructions). The in-game KEX mod (native DLLs inside the game)
rem  is NOT in the ZIP and does not run on a friend's PC yet - see README_RU.md.
rem
rem  Separate build dirs b93t/b93p: accepted b85*/b60 are not touched.
rem  Needs VS2022, CMake, Python. Run by double click or from cmd in the mod root.
rem  (Keep this file ASCII-only: cmd mangles UTF-8 text.)
rem ============================================================================
setlocal
cd /d "%~dp0"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "PYTHON=python"
set "PKG=experiments\pse_together\packaging"
set "BT=b93t"
set "BP=b93p"

for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%t"
set "OUT=docs\evidence\m1\P25_PACKAGING\FRIENDS_%STAMP%"

echo [1/4] Building ENet host/client (Release, with symbols)...
"%CMAKE%" -S tools\enet-prototype -B %BT% -G "Visual Studio 17 2022" -A x64 "-DCMAKE_SYSTEM_VERSION=10.0.26100.0" "-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi" "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG /INCREMENTAL:NO" >nul || goto :fail
"%CMAKE%" --build %BT% --config Release --target pse_host pse_client || goto :fail

echo [2/4] Building pse-diagnostics (supervisor)...
"%CMAKE%" -S %PKG% -B %BP% -G "Visual Studio 17 2022" -A x64 "-DCMAKE_SYSTEM_VERSION=10.0.26100.0" >nul || goto :fail
"%CMAKE%" --build %BP% --config Release || goto :fail

echo [3/4] Packaging into %OUT% ...
%PYTHON% -X utf8 -B %PKG%\build_package.py --transport %BT% --diagnostics %BP% --out %OUT% || goto :fail

echo [4/4] Validating the ZIP (unpack, verify, loopback host/client, crash dump)...
%PYTHON% -X utf8 -B %PKG%\validate_package.py --dist %OUT% --out %OUT%\VALIDATION --transport %BT% --diagnostics %BP% || goto :fail

echo.
echo DONE. Send to friends: %CD%\%OUT%\PSE-P25-DIAGNOSTIC-1.zip
echo Keep symbols.zip (next to it) for yourself - it is only for reading crash dumps.
echo.
pause
exit /b 0

:fail
echo.
echo *** BUILD ABORTED (code %ERRORLEVEL%). See messages above. ***
pause
exit /b 1
