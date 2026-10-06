@echo off
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
if not exist build\tests mkdir build\tests
pushd build\tests
set "MH=..\..\shared\third_party\minhook"
cl /nologo /c /O2 /MT /W3 /DWIN32 /D_WINDOWS /I"%MH%\include" "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde32.c"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHa /W4 /arch:SSE2 /DCSNZ_HOOK_TEST /I"..\..\shared\include" /I"%MH%\include" ..\..\tests\hook_abi.cpp ..\..\shared\src\platform.cpp ..\..\shared\src\hooks.cpp buffer.obj hook.obj trampoline.obj hde32.obj /link /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /OPT:NOICF /OUT:hook_abi.exe
if errorlevel 1 exit /b 1
hook_abi.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHa /W4 /arch:SSE2 /I"..\..\shared\include" ..\..\tests\contracts.cpp ..\..\shared\src\frost_policy.cpp ..\..\shared\src\profile_data.cpp /link /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /OUT:contracts.exe
if errorlevel 1 exit /b 1
contracts.exe
if errorlevel 1 exit /b 1
if not exist ..\..\shared\CSNZWeaponCore.dll goto done
cl /nologo /std:c++17 /utf-8 /O2 /MT /EHsc /W4 /I"..\..\shared\include" ..\..\tests\dll_contract.cpp /link /MACHINE:X86 /DYNAMICBASE /NXCOMPAT /SAFESEH /OUT:dll_contract.exe
if errorlevel 1 exit /b 1
dll_contract.exe "%~dp0."
:done
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
