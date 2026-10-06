#include "runtime_api.hpp"
#include <cstdio>
#include <filesystem>
static void check(bool okay,const char* name){if(!okay){std::printf("FAIL %s error=%lu\n",name,GetLastError());ExitProcess(2);}}
static WeaponRemoteEntry entry(HMODULE m,const char* name){auto p=reinterpret_cast<WeaponRemoteEntry>(GetProcAddress(m,name));check(p!=nullptr,name);return p;}
int wmain(int argc,wchar_t** argv){
    check(argc==2,"native root argument");const std::filesystem::path root=argv[1];
    auto core=LoadLibraryExW((root/L"shared/CSNZWeaponCore.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);check(core!=nullptr,"core LoadLibrary");
    check(entry(core,"CSNZWeapons_Api")(nullptr)==WeaponRuntimeApi,"core API");check(entry(core,"CSNZWeapons_Status")(nullptr)==0,"core load has no hooks");
    struct Item{const wchar_t* folder;DWORD id;};const Item items[]={{L"GigaBreak",725},{L"GigaBreakLE",726},{L"Lycaon",613},{L"Brionac",4088},{L"LuminousBrionac",4128},{L"LuminousBrionacLE",4129},{L"SpaceArbalest",591},{L"GravityRepulsor",547},{L"AbyssRepulsor",609},{L"Naberius",568},{L"ArcaneNaberius",692},{L"FrostViper",597}};
    for(const auto& item:items){const auto file=root/item.folder/(std::wstring(item.folder)+L".dll");auto m=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);check(m!=nullptr,"named module load");check(entry(m,"CSNZWeapon_Api")(nullptr)==WeaponRuntimeApi,"weapon API");check(entry(m,"CSNZWeapon_Id")(nullptr)==item.id,"weapon ID");check(entry(m,"CSNZWeapon_Start")(nullptr)==0,"weapon refuses non-CSOHLDS process");FreeLibrary(m);}
    check(entry(core,"CSNZWeapons_Start")(nullptr)==0,"core refuses client/other process");check(entry(core,"CSNZWeapons_Status")(nullptr)==0,"wrong-host rejection has no side effects");FreeLibrary(core);
    std::printf("PASS 13 x86 DLL loads/exports; 12 unique IDs; wrong-host registration/start fail closed; no load-time hooks\n");
}
