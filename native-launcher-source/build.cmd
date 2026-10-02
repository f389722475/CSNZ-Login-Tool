@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>&1
if not errorlevel 1 goto tools_ready
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto no_tools
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT goto no_tools
call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
:tools_ready
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
  echo Please use an x86 Native Tools Command Prompt or run this file outside an existing x64 build prompt.
  exit /b 1
)
if not exist build\obj mkdir build\obj
if not exist build\bin mkdir build\bin
set "MH=..\native-mod-source\third_party\minhook"
cl /nologo /c /TC /O2 /MT /W3 /DWIN32 /Fo"build\obj\\" /I"%MH%\include" "%MH%\src\hook.c" "%MH%\src\buffer.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde32.c"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /utf-8 /O2 /MT /W4 /GS /DUNICODE /D_UNICODE /Fo"build\obj\\" /I"%MH%\include" auth_bridge.cpp
if errorlevel 1 exit /b 1
link /nologo /DLL /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEF:CSNZLauncherBridge.def /OUT:build\bin\CSNZLauncherBridge.dll /IMPLIB:build\obj\CSNZLauncherBridge.lib build\obj\auth_bridge.obj build\obj\hook.obj build\obj\buffer.obj build\obj\trampoline.obj build\obj\hde32.obj kernel32.lib
exit /b %errorlevel%
:no_tools
echo MSVC x86 Build Tools required.
exit /b 1
