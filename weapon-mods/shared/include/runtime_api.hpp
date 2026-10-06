#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// All remote entry points use LPTHREAD_START_ROUTINE's Win32 ABI. Registration
// must finish before Start. Changes require a fresh server; never hot-unload.
enum class WeaponRuntimeStatus:DWORD {Loaded=0,Registered=1,WaitingForMap=2,Active=3,Stopping=4,CleanResident=5,Failed=100};
constexpr DWORD WeaponRuntimeApi=0x00010000;
using WeaponRemoteEntry=DWORD(WINAPI*)(void*);
