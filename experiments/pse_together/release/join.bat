@echo off
cd /d "%~dp0"
rem Direct internet join: join.bat 1.2.3.4  (or 1.2.3.4:28745, or a host name) puts the address into the F7 form
if not "%~1"=="" set "PSE_JOIN_ADDRESS=%~1"
rem pe-play.exe has no console window of its own; this window closes at once. Problems are shown in a message box, logs are in the profile folder (logs subfolder).
start "" "%~dp0pe-play.exe" --role client -stdout
