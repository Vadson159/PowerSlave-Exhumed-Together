@echo off
cd /d "%~dp0"
rem pe-play.exe has no console window of its own; this window closes at once. Problems are shown in a message box, logs are in the profile folder (logs subfolder).
start "" "%~dp0pe-play.exe" --role host -stdout
