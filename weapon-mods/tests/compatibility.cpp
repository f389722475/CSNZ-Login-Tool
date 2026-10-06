#include "config.hpp"
#include "../shared/src/giga/platform.h"
#include <cstdio>
#include <filesystem>

static void check(bool okay,const char* label){if(!okay){std::printf("FAIL %s\n",label);ExitProcess(2);}std::printf("PASS %s\n",label);}
template<class T> static void replace(csnz::Address address,T value){DWORD old{};check(VirtualProtect(reinterpret_cast<void*>(address),sizeof(T),PAGE_EXECUTE_READWRITE,&old)!=0,"fixture memory protection");csnz::write(address,value);DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(address),sizeof(T),old,&ignored);}
template<class F> static void denied(F&& fn,const char* reason){try{fn();}catch(const std::exception& e){check(std::strstr(e.what(),reason)!=nullptr,reason);return;}check(false,reason);}

// This newly compiled test host is intentionally named CSOHLDS.exe. It is NOT
// the game. DONT_RESOLVE_DLL_REFERENCES maps fixture DLLs without DllMain or
// resolving imports. No engine code, hooks, game, lobby or map is started.
int wmain(){
    using namespace csnz;
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);const auto root=std::filesystem::path(exe).parent_path();
    check(LoadLibraryExW((root/L"mp.dll").c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES)!=nullptr,"map fixture mp without initialization");
    check(LoadLibraryExW((root/L"hw.dll").c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES)!=nullptr,"map fixture hw without initialization");
    validateBuild();check(true,"different host EXE and module file hashes accepted by real native validator");
    const auto nt=mp+read<IMAGE_DOS_HEADER>(mp).e_lfanew;const auto timestamp=nt+offsetof(IMAGE_NT_HEADERS32,FileHeader)+offsetof(IMAGE_FILE_HEADER,TimeDateStamp);
    const auto originalTimestamp=read<DWORD>(timestamp);replace(timestamp,originalTimestamp+1);validateBuild();
    check(giga_family::sameBuild(read<IMAGE_NT_HEADERS32>(nt),giga_family::ServerBuild),"Giga no longer pins metadata timestamps");replace(timestamp,originalTimestamp);
    const auto& guard=profiles()["shared"]["hooks"][0];const auto code=(guard["module"].text()=="mp.dll"?mp:hw)+guard["rva"].u32();const auto first=read<BYTE>(code);
    replace(code,static_cast<BYTE>(first^1));denied([]{validateBuild();},"Live code guard");replace(code,first);
    const auto& tables=profiles()["frost"]["modules"]["mp.dll"]["slots"].object();check(!tables.empty(),"fixture vtable contract exists");
    const auto& table=*tables.begin();const auto& slot=*table.second.object().begin();const auto location=mp+std::stoul(table.first)+std::stoul(slot.first)*4;const auto originalSlot=ptr(location);
    replace(location,originalSlot+1);denied([]{validateBuild();},"Vtable slot mismatch");replace(location,originalSlot);
    const auto size=nt+offsetof(IMAGE_NT_HEADERS32,OptionalHeader)+offsetof(IMAGE_OPTIONAL_HEADER32,SizeOfImage);const auto originalSize=read<DWORD>(size);
    replace(size,originalSize+4096);denied([]{validateBuild();},"Server PE layout mismatch");replace(size,originalSize);
    validateBuild();check(!active&&!failed,"native compatibility validation installs no hooks and starts no gameplay");
    std::printf("PASS_OFFLINE_ONLY native PE/code/vtable guards preserved; metadata differences accepted\n");
}
