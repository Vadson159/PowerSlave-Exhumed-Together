@echo off
setlocal
python "%~dp0multiclient_runner.py" %*
exit /b %ERRORLEVEL%
