@echo off
cd /d "%~dp0"
"%~dp0pe-play.exe" --role host -stdout
set "pse_result=%ERRORLEVEL%"
if not "%pse_result%"=="0" echo Failed with code %pse_result%
pause
exit /b %pse_result%
