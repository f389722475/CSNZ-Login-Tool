#include "runtime_api.hpp"
#include "families.hpp"
#include "hooks.hpp"
#include "brokers.hpp"
#include "world.hpp"
#include <filesystem>

namespace csnz {
namespace {
struct Descriptor {unsigned id;const char* family;};
constexpr Descriptor catalog[]={{725,"giga"},{726,"giga"},{613,"wolf"},{4088,"divinespear"},{4128,"divinespear"},{4129,"divinespear"},{591,"halo"},{537,"halo"},{547,"leapstrike"},{609,"leapstrike"},{568,"soul"},{692,"soul"},{597,"frost"}};
std::set<unsigned> enabledIds;bool started=false,hooksInstalled=false,allClean=false;
std::atomic<DWORD> phase{DWORD(WeaponRuntimeStatus::Loaded)};
bool server(){wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return false;const auto leaf=wcsrchr(path,L'\\');return leaf&&_wcsicmp(leaf+1,L"CSOHLDS.exe")==0;}
void openLog(){HMODULE module{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&openLog),&module))throw std::runtime_error("Core module identity");wchar_t path[32768]{};if(!GetModuleFileNameW(module,path,32768))throw std::runtime_error("Core path");const auto dir=std::filesystem::path(path).parent_path()/L"logs";std::filesystem::create_directories(dir);setLogFile((dir/(L"native-weapons-"+std::to_wstring(GetCurrentProcessId())+L".log")).wstring());}
bool familiesClean(){return (!familySelected("giga")||gigaClean())&&(!familySelected("wolf")||wolfClean())&&(!familySelected("divinespear")||divineClean())&&(!familySelected("leapstrike")||leapstrikeClean())&&(!familySelected("soul")||soulClean())&&(!familySelected("frost")||frostClean());}
}
bool selected(unsigned id){return enabledIds.count(id)!=0;}
bool familySelected(const std::string& family){for(const auto& c:catalog)if(c.family==family&&selected(c.id))return true;return false;}
void initializeShared(){
    engine.initialize();World::initialize();
    World::onMapReset([]{StatusBroker::forget();});
    Hooks::attach(hat(0x740f00),"shared",{},[](Invocation&){if(active&&!stopping)World::precache();});
    // Registered first, so reverse-order leave executes AFTER every family's
    // final engine-thread cleanup, including deferred Frost HUD removal.
    Hooks::attach(at(0x13f8a80),"shared",{},[](Invocation&){
        if(stopping){if(!allClean&&familiesClean()){for(const auto& [family,p]:profiles().object())StatusBroker::releaseAll(family);World::stop();allClean=true;phase=DWORD(WeaponRuntimeStatus::CleanResident);log("CLEAN_RESIDENT: hooks remain pass-through until process exit");}return;}
        if(!active)return;StatusBroker::maintain(now());World::precache();phase=DWORD(World::ready()?WeaponRuntimeStatus::Active:WeaponRuntimeStatus::WaitingForMap);
    });
}
}
extern "C" DWORD WINAPI CSNZWeapons_Api(void*){return WeaponRuntimeApi;}
extern "C" DWORD WINAPI CSNZWeapons_Register(void* argument){using namespace csnz;std::lock_guard<std::recursive_mutex> lock(gameplayMutex);if(!server()||started||failed)return 0;const auto id=reinterpret_cast<unsigned>(argument);for(const auto& c:catalog)if(c.id==id){enabledIds.insert(id);phase=DWORD(WeaponRuntimeStatus::Registered);return id;}return 0;}
extern "C" DWORD WINAPI CSNZWeapons_Start(void*){using namespace csnz;std::lock_guard<std::recursive_mutex> lock(gameplayMutex);if(!server())return 0;if(started)return active&&!stopping?1:0;started=true;
    try{
        openLog();if(enabledIds.empty())throw std::runtime_error("No weapon modules registered");validateBuild();
        // LoadLibrary(mp.dll) can become visible before the engine supplies its
        // import table. Wait only for verified data bindings, without calling
        // engine APIs from this remote initialization thread.
        bool tableReady=false;for(unsigned attempt=0;attempt<200;attempt++){
            Address precache{},load{},globals{};
            tableReady=rawRead(at(0x21d3a5c),&precache,4)&&precache==hat(0x740f00)&&rawRead(at(0x21d3be8),&load,4)&&load==hat(0x6dcbf0)&&rawRead(at(0x21d3dac),&globals,4)&&globals;
            if(tableReady)break;Sleep(100);
        }if(!tableReady)throw std::runtime_error("Engine import table readiness timeout");
        // Attaching to a running map misses engine precache. Never claim ready
        // on that path: the launcher must own a fresh, waiting dedicated server.
        if(read<int>(hat(0x256dd4c))==2)throw std::runtime_error("Map already active; restart dedicated server before loading native weapons");
        initializeShared();if(familySelected("giga"))installGiga();if(familySelected("wolf"))installWolf();if(familySelected("divinespear"))installDivine();if(familySelected("halo"))installHalo();if(familySelected("leapstrike"))installLeapstrike();if(familySelected("soul"))installSoul();if(familySelected("frost"))installFrost();
        DamageBroker::install();ArmorGate::install();
        HMODULE pinned{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&CSNZWeapons_Start),&pinned))throw std::runtime_error("Cannot pin resident core");
        // Hooks see pass-through during the enable transaction. Publish active
        // only after every detour/trampoline has been installed successfully.
        Hooks::install();hooksInstalled=true;active=true;phase=DWORD(WeaponRuntimeStatus::WaitingForMap);
        log("HOOKS_READY: native x86, server-only, weapons="+std::to_string(enabledIds.size())+" entries="+std::to_string(Hooks::size())+"; gameplay acceptance pending");return 1;
    }catch(const std::exception& e){fail(e.what());}catch(...){fail("Native initialization exception");}if(!hooksInstalled)allClean=true;return 0;
}
extern "C" DWORD WINAPI CSNZWeapons_Stop(void*){using namespace csnz;std::lock_guard<std::recursive_mutex> lock(gameplayMutex);active=false;stopping=true;if(!hooksInstalled)allClean=true;phase=DWORD(allClean?WeaponRuntimeStatus::CleanResident:WeaponRuntimeStatus::Stopping);return 1;}
extern "C" DWORD WINAPI CSNZWeapons_Status(void*){using namespace csnz;return failed?DWORD(WeaponRuntimeStatus::Failed):phase.load();}
extern "C" DWORD WINAPI CSNZWeapons_IsClean(void*){using namespace csnz;std::lock_guard<std::recursive_mutex> lock(gameplayMutex);return allClean?1:0;}
