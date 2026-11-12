@echo off
setlocal
cd /d "%~dp0"
echo ============================================================
echo  Exhumed Together - installer (uses YOUR own PowerSlave Exhumed)
echo ============================================================
set "SRC=%~1"
if defined SRC goto check
echo Looking for the game in your Steam libraries...
for /f "usebackq delims=" %%G in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0find_game.ps1"`) do set "SRC=%%G"
if defined SRC echo Found: %SRC%
:check
if defined SRC if exist "%SRC%\anubis_Shipping_Steam_x64.exe" goto found
echo.
echo The game folder was not found automatically.
echo Open Steam - right click PowerSlave Exhumed - Manage - Browse local files,
echo copy the folder path from the address bar and paste it here.
set "SRC="
set /p "SRC=Game folder: "
if not defined SRC goto fail
set "SRC=%SRC:"=%"
goto check
:found
echo Game folder: %SRC%
set "WANT=b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d"
set "GOT="
for /f "skip=1 tokens=* delims=" %%H in ('certutil -hashfile "%SRC%\anubis_Shipping_Steam_x64.exe" SHA256') do if not defined GOT set "GOT=%%H"
set "GOT=%GOT: =%"
if /i not "%GOT%"=="%WANT%" goto badversion
echo Game version OK.
if exist "%~dp0game" goto update
echo Copying the game (about 500 MB, one minute)...
robocopy "%SRC%" "%~dp0game" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto fail
goto overlay
:update
echo A previous install exists: updating the mod files only.
:overlay
rem The mod replaces SDL2.dll and forwards to the ORIGINAL one, kept as SDL2_pse_original.dll. It is always taken from the Steam folder of the game
rem (not from the copy, which may already hold the mod's SDL2.dll after an earlier or interrupted install).
if exist "%~dp0game\SDL2_pse_original.dll" goto haveorig
copy /y "%SRC%\SDL2.dll" "%~dp0game\SDL2_pse_original.dll" >nul
if errorlevel 1 goto fail
:haveorig
xcopy /e /y /q "%~dp0overlay\game\*" "%~dp0game\" >nul
if errorlevel 1 goto fail
echo.
echo DONE. Run host.bat to host or join.bat to join (Radmin VPN: see README.txt).
pause
exit /b 0
:badversion
echo.
echo Your game build differs from the one this mod supports.
echo Expected anubis_Shipping_Steam_x64.exe SHA-256:
echo   %WANT%
echo Found:
echo   %GOT%
echo Update the game in Steam (Properties - Updates / verify files) and run again.
pause
exit /b 2
:fail
echo.
echo Install failed.
pause
exit /b 1
