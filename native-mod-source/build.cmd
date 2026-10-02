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
if not exist build mkdir build
if not exist build\obj mkdir build\obj
if not exist build\bin mkdir build\bin
cl /nologo /c /TC /O2 /MT /W3 /DWIN32 /Fo"build\obj\\" /Ithird_party\minhook\include third_party\minhook\src\hook.c third_party\minhook\src\buffer.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde32.c
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++17 /utf-8 /O2 /MT /EHa /W4 /arch:SSE2 /GS /DUNICODE /D_UNICODE /Fo"build\obj\\" /Ithird_party\minhook\include src\giga_break_le.cpp src\native_runtime.cpp src\launcher.cpp
if errorlevel 1 exit /b 1
rc /nologo /DBUILD_DLL /fobuild\obj\dll.res src\version.rc
if errorlevel 1 exit /b 1
rc /nologo /fobuild\obj\exe.res src\version.rc
if errorlevel 1 exit /b 1
link /nologo /DLL /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /OPT:REF /OPT:ICF /INCREMENTAL:NO /DEF:src\GigaBreakLE.def /OUT:build\bin\GigaBreakLE.dll /IMPLIB:build\obj\GigaBreakLE.lib build\obj\giga_break_le.obj build\obj\native_runtime.obj build\obj\hook.obj build\obj\buffer.obj build\obj\trampoline.obj build\obj\hde32.obj build\obj\dll.res kernel32.lib
if errorlevel 1 exit /b 1
link /nologo /SUBSYSTEM:CONSOLE /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /OPT:REF /OPT:ICF /INCREMENTAL:NO /OUT:build\bin\CSNZ_GigaBreakLE.exe build\obj\launcher.obj build\obj\exe.res kernel32.lib
if errorlevel 1 exit /b 1
echo Built: build\bin\GigaBreakLE.dll and build\bin\CSNZ_GigaBreakLE.exe
exit /b 0
:no_tools
echo Install Visual Studio 2022 Build Tools with Desktop development with C++ and a Windows SDK, then run build.cmd.
exit /b 1
