@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
if not exist build\x64 mkdir build\x64
if not exist build\x86 mkdir build\x86
if not exist build\bin mkdir build\bin
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /c /TC /O2 /MT /W3 /Fo"build\x64\\" /Ithird_party\minhook\include third_party\minhook\src\hook.c third_party\minhook\src\buffer.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++20 /utf-8 /O2 /MT /EHsc /W4 /DUNICODE /D_UNICODE /Fo"build\x64\\" /Ithird_party /Ithird_party\minhook\include src\engine.cpp src\server.cpp src\control.cpp
if errorlevel 1 exit /b 1
link /nologo /DLL /MACHINE:X64 /DYNAMICBASE /NXCOMPAT /OPT:REF /INCREMENTAL:NO /OUT:build\bin\ClassAwakening.Server.dll build\x64\engine.obj build\x64\server.obj build\x64\hook.obj build\x64\buffer.obj build\x64\trampoline.obj build\x64\hde64.obj kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1
link /nologo /SUBSYSTEM:CONSOLE /MACHINE:X64 /DYNAMICBASE /NXCOMPAT /OPT:REF /INCREMENTAL:NO /OUT:build\bin\AwakeningHost.exe build\x64\control.obj kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1
if exist src\test_engine.cpp (
cl /nologo /std:c++20 /utf-8 /O2 /MT /EHsc /W4 /DUNICODE /D_UNICODE /Ithird_party src\test_engine.cpp build\x64\engine.obj /Febuild\bin\AwakeningTests.exe /Fobuild\x64\test_engine.obj /link advapi32.lib
if errorlevel 1 exit /b 1
)
call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /c /TC /O2 /MT /W3 /Fo"build\x86\\" /Ithird_party\minhook\include third_party\minhook\src\hook.c third_party\minhook\src\buffer.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde32.c
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++20 /utf-8 /O2 /MT /EHsc /W4 /DUNICODE /D_UNICODE /DAW_GAME /Fo"build\x86\\" /Ithird_party /Ithird_party\minhook\include src\observer.cpp src\control.cpp
if errorlevel 1 exit /b 1
link /nologo /DLL /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /OPT:REF /INCREMENTAL:NO /OUT:build\bin\ClassAwakening.Observer.dll build\x86\observer.obj build\x86\hook.obj build\x86\buffer.obj build\x86\trampoline.obj build\x86\hde32.obj kernel32.lib advapi32.lib
if errorlevel 1 exit /b 1
link /nologo /SUBSYSTEM:CONSOLE /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /OPT:REF /INCREMENTAL:NO /OUT:build\bin\AwakeningObserverHost.exe build\x86\control.obj kernel32.lib advapi32.lib
exit /b %ERRORLEVEL%
