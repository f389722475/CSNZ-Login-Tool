#include "game_abi.h"
#include "family_profile.h"
#include <cstdio>

// Preserved accepted Giga server gameplay core; entry adapters are in giga_adapter.cpp.
// No client module or client hooks. Called only by the shared native server owner.
namespace giga_family {
using SetStatusFn=void(__cdecl*)(Address,Address,Address,unsigned,unsigned,float);
using ReleaseStatusFn=void(__cdecl*)(Address,Address,Address,unsigned);
using LogFn=void(__cdecl*)(const char*);
SetStatusFn statusSet{};ReleaseStatusFn statusRelease{};LogFn logSink{};
Engine engine;
std::recursive_mutex gameMutex;
std::atomic<bool> enabled{false},stopRequested{false},serverClean{false},clientClean{true};
std::atomic<unsigned> shots{0},shields{0},blocked{0},errors{0};
__declspec(noinline) bool rawRead(Address p,void* out,std::size_t n) noexcept {
    if(p<0x10000||!n||p+n<p)return false;
    __try{std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
__declspec(noinline) bool rawWrite(Address p,const void* in,std::size_t n) noexcept {
    if(p<0x10000||!n||p+n<p)return false;
    __try{std::memcpy(reinterpret_cast<void*>(p),in,n);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool executable(Address p) noexcept {
    MEMORY_BASIC_INFORMATION mi{};
    return p>=0x10000&&VirtualQuery(reinterpret_cast<void*>(p),&mi,sizeof(mi))==sizeof(mi)&&mi.State==MEM_COMMIT&&
      !(mi.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(mi.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
void log(const char* text) noexcept {if(logSink)logSink(text);}
void fail(const char* text) noexcept {enabled=false;stopRequested=true;++errors;log(text);}
}

#include "giga_core_v1.inc"

namespace giga_family {
template<class T> bool resolve(T& fn,Address slot){auto value=pointer(slot);if(!executable(value))return false;fn=reinterpret_cast<T>(value);return true;}
}
using namespace giga_family;
#define API extern "C"
API int __cdecl GigaInitialize(Address base,SetStatusFn set,ReleaseStatusFn release,LogFn sink) noexcept {
    if(enabled||stopRequested||!set||!release||!sink)return 0;
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);const auto leaf=wcsrchr(path,L'\\');
    if(!leaf||_wcsicmp(leaf+1,L"CSOHLDS.exe")||reinterpret_cast<Address>(GetModuleHandleW(L"mp.dll"))!=base)return 0;
    engine.mp=base;statusSet=set;statusRelease=release;logSink=sink;
    try{
        auto header=read<IMAGE_NT_HEADERS32>(base+read<IMAGE_DOS_HEADER>(base).e_lfanew);
        if(!sameBuild(header,ServerBuild))throw std::runtime_error("Giga server PE mismatch");
        engine.property=reinterpret_cast<PropertyFn>(base+0x6444d0);
        engine.zombieMode=reinterpret_cast<decltype(engine.zombieMode)>(base+0x6688c0);
        engine.bullet=reinterpret_cast<BulletFn>(base+0x13fc970);
        engine.aux=reinterpret_cast<WeaponFn>(base+0xba18c0);
        if(!resolve(engine.manager,base+0x21d3cec)||!resolve(engine.makeVectors,base+0x21d3aac)||!resolve(engine.playback,base+0x21d3c5c)||
           !resolve(engine.traceLine,base+0x21d3adc)||!resolve(engine.findSphere,base+0x21d3a9c)||!resolve(engine.entityByIndex,base+0x21d3b8c)||!resolve(engine.entityIndex,base+0x21d3b88))throw std::runtime_error("Giga server engine table unavailable");
        enabled=true;log("GIGA_CORE_READY: native port of accepted server gameplay; no client module");return 1;
    }catch(const std::exception& e){fail(e.what());return 0;}catch(...){fail("Giga initialization exception");return 0;}
}
API int __cdecl GigaActive(Address w) noexcept {return enabled&&active(w);}
API void __cdecl GigaAcquire(Address w) noexcept {guarded([&]{acquired(w);});}
API void __cdecl GigaAim(Address w,Address out) noexcept {guarded([&]{write(out,aim(w));});}
API void __cdecl GigaShoot(Address w,Address a) noexcept {guarded([&]{shoot(w,read<Aim>(a));});}
API void __cdecl GigaBurst(Address w,Address a) noexcept {guarded([&]{burst(w,read<Aim>(a));});}
API void __cdecl GigaSwing(Address w,int stage) noexcept {guarded([&]{swing(w,stage);});}
API void __cdecl GigaFrame(Address w) noexcept {guarded([&]{frame(w);});}
API void __cdecl GigaGlobalFrame() noexcept {if(stopRequested){if(!serverClean)serverCleanup();return;}guarded([]{globalFrame();});}
API void __cdecl GigaCrashStart(Address w,Address a) noexcept {guarded([&]{crashStart(w,read<Aim>(a));});}
API void __cdecl GigaCaptureDeploy(Address w,Address out) noexcept {guarded([&]{write(out,captureCrashDeploy(w));});}
API void __cdecl GigaRestoreDeploy(Address w,Address saved) noexcept {guarded([&]{restoreCrashDeploy(w,read<CrashDeployState>(saved));});}
API int __cdecl GigaBlockDamage(Address victim,float damage) noexcept {bool value=false;guarded([&]{value=blockDamage(victim,damage);});return value;}
API Address __cdecl GigaArmorWeapon(Address victim) noexcept {
    Address result=0;guarded([&]{const double t=now();auto* s=shieldState(victim,t);if(s)result=s->weapon;});return result;
}
API int __cdecl GigaArmorReady(Address w,Address victim,unsigned serial,unsigned weaponSerial) noexcept {
    bool result=false;guarded([&]{auto it=weapons.find(w);if(it==weapons.end())return;const auto& s=it->second;
      result=s.player==victim&&s.serial==serial&&s.weaponSerial==weaponSerial&&validWeapon(s)&&shieldReady(s,now());});return result;
}
API void __cdecl GigaArmorCommit(Address w,Address victim,unsigned serial,unsigned weaponSerial) noexcept {
    guarded([&]{auto it=weapons.find(w);if(it==weapons.end())return;auto& s=it->second;
      if(s.player!=victim||s.serial!=serial||s.weaponSerial!=weaponSerial||!validWeapon(s)||read<float>(s.pev+0x178)<=0||!shieldReady(s,now()))return;
      const double t=now();auto& p=defense(s);p.cooldown=t+damageValue(w,0x25d8);s.acquiredAt=t;s.hits=0;s.pendingShield=true;
      grantGod(s,t,prop(w,0x2680),"native_predeath");++shields;log("SHIELD_TRIGGER: unified whole-entry native survival confirmed");});
}
// Fixed-size records are copied into owned native memory on the engine thread;
// clients never receive pointers. Snapshot is for native network visual owners.
API unsigned __cdecl GigaSnapshot(Address output,unsigned capacity) noexcept {
    unsigned count=0;guarded([&]{for(const auto& pair:weapons){const auto& s=pair.second;if(!validWeapon(s)||count>=capacity)continue;
      const unsigned row[10]={s.weapon,s.player,s.edict,s.serial,s.weaponSerial,read<unsigned>(s.weapon+0xe4),
        static_cast<unsigned>(engine.entityIndex(reinterpret_cast<void*>(s.edict))),static_cast<unsigned>(active(s.weapon)),static_cast<unsigned>(mode()),static_cast<unsigned>(s.crashing)};
      write(output+count*sizeof(row),row);++count;}});return count;
}
API void __cdecl GigaStop() noexcept {enabled=false;stopRequested=true;}
API void __cdecl GigaMapReset() noexcept {weapons.clear();statuses.clear();protections.clear();clockValue=std::numeric_limits<double>::quiet_NaN();}
API unsigned __cdecl GigaStats(Address output) noexcept {
    const unsigned values[7]={enabled?1u:0u,stopRequested?1u:0u,serverClean?1u:0u,shots.load(),shields.load(),blocked.load(),errors.load()};
    if(output)rawWrite(output,values,sizeof(values));return 7;
}
