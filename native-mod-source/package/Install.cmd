@echo off
setlocal EnableExtensions DisableDelayedExpansion
if not exist "%~dp0Install.ps1" (
  echo Missing Install.ps1. Extract the complete deployment ZIP first.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install.ps1" %*
set "result=%errorlevel%"
if not "%result%"=="0" pause
exit /b %result%
