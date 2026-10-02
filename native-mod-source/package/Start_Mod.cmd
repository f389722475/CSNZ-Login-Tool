@echo off
setlocal EnableExtensions DisableDelayedExpansion
if not exist "%~dp0payload\CSNZ_GigaBreakLE.exe" (
  echo Missing payload\CSNZ_GigaBreakLE.exe. Extract the complete deployment ZIP first.
  pause
  exit /b 1
)
if not exist "%~dp0payload\native.ini" (
  echo Not configured. Run Install.cmd in this package first.
  pause
  exit /b 1
)
"%~dp0payload\CSNZ_GigaBreakLE.exe" --start
set "result=%errorlevel%"
if not "%result%"=="0" pause
exit /b %result%
