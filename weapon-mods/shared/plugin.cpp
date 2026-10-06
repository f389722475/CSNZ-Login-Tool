#include "include/runtime_api.hpp"
#ifndef CSNZ_WEAPON_ID
#error Define CSNZ_WEAPON_ID for a named weapon module
#endif
// The shared core owns all hooks and family state. These small, named modules
// select IDs only; they cannot create duplicate detours for paired variants.
extern "C" DWORD WINAPI CSNZWeapon_Id(void*){return CSNZ_WEAPON_ID;}
extern "C" DWORD WINAPI CSNZWeapon_Api(void*){return WeaponRuntimeApi;}
extern "C" DWORD WINAPI CSNZWeapon_Start(void*){
    const auto core=GetModuleHandleW(L"CSNZWeaponCore.dll");if(!core)return 0;
    const auto api=reinterpret_cast<WeaponRemoteEntry>(GetProcAddress(core,"CSNZWeapons_Api"));
    const auto start=reinterpret_cast<WeaponRemoteEntry>(GetProcAddress(core,"CSNZWeapons_Register"));
    if(!api||!start||api(nullptr)!=WeaponRuntimeApi)return 0;
    return start(reinterpret_cast<void*>(CSNZ_WEAPON_ID));
}
