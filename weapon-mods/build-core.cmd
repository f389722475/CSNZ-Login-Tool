@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
if not exist build\core mkdir build\core
pushd build\core
set "MH=..\..\shared\third_party\minhook"
cl /nologo /c /O2 /MT /W3 /DWIN32 /D_WINDOWS /I"%MH%\include" "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde32.c"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /utf-8 /O2 /MT /EHa /W4 /arch:SSE2 /DUNICODE /D_UNICODE /I"..\..\shared\include" /I"%MH%\include" ..\..\shared\src\*.cpp ..\..\shared\src\giga\giga_gameplay.cpp
if errorlevel 1 exit /b 1
rem An explicit object list prevents stale named-plugin objects from entering
rem the shared core archive during an incremental build.
lib /nologo /OUT:CSNZWeaponCore.lib buffer.obj hook.obj trampoline.obj hde32.obj combat.obj defense_brokers.obj divine.obj effects.obj frost.obj frost_policy.obj game.obj giga_adapter.obj halo.obj hooks.obj leapstrike.obj platform.obj profile.obj profile_data.obj runtime.obj soul.obj status_broker.obj wolf.obj world.obj giga_gameplay.obj
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
