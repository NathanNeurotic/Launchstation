@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Cloud Relay.ps1" %*
exit /b %ERRORLEVEL%
