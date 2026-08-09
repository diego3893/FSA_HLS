@echo off
rem This bypass applies only to this child PowerShell process.
rem It does not change the permanent execution policy.
chcp 65001 >nul
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_test.ps1" %*
exit /b %ERRORLEVEL%
