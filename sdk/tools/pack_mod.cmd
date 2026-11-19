@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0pack_mod.ps1" %*
exit /b %ERRORLEVEL%
