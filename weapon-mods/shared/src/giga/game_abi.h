#pragma once
#include "platform.h"
#include <array>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <limits>
#include <cstddef>

// These are the measured CSNZ0930 GoldSrc extensions, NOT the stock HLSDK's
// CBasePlayer/TEMPENTITY layouts. Do not substitute an SDK class definition.
namespace giga_family {
struct Vec3 {
    float x{},y{},z{};
    float& operator[](int i){return i==0?x:i==1?y:z;}
    float operator[](int i)const{return i==0?x:i==1?y:z;}
};
static_assert(sizeof(Vec3)==12);
inline Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 operator*(Vec3 a,float b){return {a.x*b,a.y*b,a.z*b};}
inline float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline float length(Vec3 v){return std::sqrt(dot(v,v));}
inline Vec3 norm(Vec3 v){float n=length(v);return n>.001f?v*(1/n):Vec3{0,0,1};}
inline bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
constexpr float Pi=3.14159265358979323846f, Meter=39.369998931884766f;
inline Vec3 directionAngles(Vec3 v){v=norm(v);return {-std::asin(std::clamp(v.z,-1.f,1.f))*180/Pi,std::atan2(v.y,v.x)*180/Pi,0};}
inline Vec3 eventAngles(Vec3 v){if(!finite(v))throw std::runtime_error("Invalid event angle");return {-v.x/3,v.y,v.z};}

bool rawRead(Address p,void* out,std::size_t n) noexcept;
bool rawWrite(Address p,const void* in,std::size_t n) noexcept;
template<class T> T read(Address p){T v{};if(!rawRead(p,&v,sizeof(v)))throw std::runtime_error("Invalid game read");return v;}
template<class T> void write(Address p,const T& v){if(!rawWrite(p,&v,sizeof(v)))throw std::runtime_error("Invalid game write");}
inline Address pointer(Address p){return read<Address>(p);}
bool executable(Address p) noexcept;
void log(const char* text) noexcept;

struct TraceResult {
    int allSolid,startSolid,inOpen,inWater;
    float fraction;
    Vec3 end;
    float planeDistance;
    Vec3 planeNormal;
    Address edict;
    int hitgroup;
    std::uint8_t extension[128-56];
};
static_assert(offsetof(TraceResult,fraction)==0x10 && offsetof(TraceResult,end)==0x14);
static_assert(offsetof(TraceResult,edict)==0x30 && offsetof(TraceResult,hitgroup)==0x34);
static_assert(sizeof(TraceResult)==128);
using WeaponFn=void(__thiscall*)(void*);
using DamageFn=int(__thiscall*)(void*,void*,void*,float,int,void*);
using PropertyFn=float(__thiscall*)(void*,unsigned);
using BulletFn=void(__thiscall*)(void*,Vec3*,float,float,float,float,float,float,float,float,int,int,int,float,void*,int,int,int,int);
using PlaybackFn=void(__cdecl*)(int,void*,int,float,const Vec3*,const Vec3*,float,float,int,int,int,int);
using TempUpdateFn=void(__cdecl*)(double,double,double,void*,void*,void*,void*);
struct Engine {
    Address mp{},client{};
    PropertyFn property{};
    bool(__cdecl* zombieMode)(int){};
    void*(__cdecl* manager)(){};
    void(__cdecl* makeVectors)(const Vec3*){};
    BulletFn bullet{};
    PlaybackFn playback{};
    void(__cdecl* traceLine)(const Vec3*,const Vec3*,int,void*,TraceResult*){};
    void*(__cdecl* findSphere)(void*,const Vec3*,float){};
    void*(__cdecl* entityByIndex)(int){};
    int(__cdecl* entityIndex)(void*){};
    WeaponFn aux{};
    float(__cdecl* clientTime)(){};
    bool(__cdecl* isLocal)(int){};
    void*(__cdecl* tempModel)(const Vec3*,const Vec3*,const Vec3*,float,int,int,int){};
    int(__cdecl* findModel)(const char*){};
    void*(__cdecl* clientEntity)(int){};
    Address globals()const{return pointer(mp+0x21d3dac);}
};
extern Engine engine;
extern std::recursive_mutex gameMutex;
extern std::atomic<bool> enabled,stopRequested,serverClean,clientClean;
extern std::atomic<unsigned> shots,shields,blocked,errors;
void fail(const char* reason) noexcept;
void serverCleanup() noexcept;
void clientCleanup() noexcept;

// Every mod callback runs on its invoking engine thread. /EHa lets the guard
// unwind C++ locks/buffers if a native API raises SEH; originals are not wrapped.
template<class F> bool guarded(F&& action) noexcept {
    std::lock_guard<std::recursive_mutex> lock(gameMutex);
    if(!enabled.load(std::memory_order_acquire))return false;
    try { action();return true; }
    catch(const std::exception& e){fail(e.what());}
    catch(...){fail("Native exception in mod callback");}
    return false;
}
struct Aim { Vec3 source,direction; };
struct CrashDeployState {
    Address weapon{},player{};
    unsigned weaponSerial{},ownerSerial{};
    int phase{};
    std::array<float,4> timers{}; // native remaining timers and their delta fields
};
bool active(Address weapon) noexcept;
void acquired(Address weapon);
Aim aim(Address weapon);
void shoot(Address weapon,Aim a);
void burst(Address weapon,const Aim& a);
void swing(Address weapon,int stage);
void frame(Address weapon);
void crashStart(Address weapon,const Aim& a);
CrashDeployState captureCrashDeploy(Address weapon);
void restoreCrashDeploy(Address weapon,const CrashDeployState& saved);
bool keepCrashVisual(Address tempEntity);
void globalFrame();
bool blockDamage(Address player,float amount);
void finalDamage(Address player,Address amount);
void wingVisualFrame();
bool ourWing(Address renderer) noexcept;
void balanceWing(Address renderer);
}
