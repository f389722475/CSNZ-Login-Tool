#pragma once
#include "config.hpp"

namespace csnz {
struct Vec3 {float x{},y{},z{};float& operator[](unsigned i){return i==0?x:i==1?y:z;}float operator[](unsigned i)const{return i==0?x:i==1?y:z;}};
static_assert(sizeof(Vec3)==12);
inline Vec3 operator+(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 operator*(Vec3 v,double n){return {float(v.x*n),float(v.y*n),float(v.z*n)};}
inline double dot(Vec3 a,Vec3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
inline double length(Vec3 v){return std::sqrt(dot(v,v));}
inline bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline Vec3 unit(Vec3 v,Vec3 fallback={0,0,0}){const auto n=length(v);return n>1e-6?v*(1/n):fallback;}
inline Vec3 vec(Address p){auto v=read<Vec3>(p);if(!finite(v))throw std::runtime_error("Nonfinite game vector");return v;}
inline void putVec(Address p,Vec3 v){if(!finite(v))throw std::runtime_error("Nonfinite vector write");write(p,v);}
constexpr double Pi=3.14159265358979323846,Meter=39.369998931884766;
inline Vec3 angles(Vec3 v){v=unit(v,{1,0,0});return {float(-std::asin(std::clamp(double(v.z),-1.,1.))*180/Pi),float(std::atan2(v.y,v.x)*180/Pi),0};}
inline Vec3 direction(Vec3 a){const double pitch=a.x*Pi/180,yaw=a.y*Pi/180;return {float(std::cos(pitch)*std::cos(yaw)),float(std::cos(pitch)*std::sin(yaw)),float(-std::sin(pitch))};}
inline double clamp(double v,double low,double high){return std::clamp(v,low,high);}
struct Entity {Address object{},pev{},edict{};unsigned serial{};};
Entity entity(Address);
bool valid(const Entity&) noexcept;
Vec3 center(const Entity&);
bool hostile(const Entity&,const Entity& owner);
bool alive(const Entity&);
struct Trace {int allSolid{},startSolid{},inOpen{},inWater{};float fraction{};Vec3 end{};float planeDistance{};Vec3 planeNormal{};Address edict{};int hitgroup{};unsigned char extension[72]{};};
static_assert(sizeof(Trace)==128);
struct Target {Entity entity;Vec3 center;double distance;};
struct DamageResult {double before{},after{},delta{};bool killed{};};
struct Aim {Vec3 source,direction,angles;};
using DamageFn=int(__thiscall*)(Address,Address,Address,float,int,Address);
struct Engine {
    void(__cdecl* traceLine)(const Vec3*,const Vec3*,int,Address,Trace*){};
    Address(__cdecl* findSphere)(Address,const Vec3*,float){};
    Address(__cdecl* byIndex)(int){};
    int(__cdecl* index)(Address){};
    Address(__cdecl* manager)(){};
    bool(__cdecl* zombie)(int){};
    void(__cdecl* makeVectors)(const Vec3*){};
    void(__cdecl* emitSound)(Address,int,const char*,float,float,int,int){};
    void(__cdecl* playback)(int,Address,int,float,const Vec3*,const Vec3*,float,float,int,int,int,int){};
    void(__thiscall* animation)(Address,int,int){};
    float(__cdecl* weaponTime)(){};
    void(__thiscall* heal)(Address,Address,float,int,Address){};
    void initialize();
};
extern Engine engine;
extern std::array<unsigned,8> damageInfo;
double now();int mode();int gameMode();
Trace trace(Vec3,Vec3,Address skip,int flags=0);
std::vector<Target> targets(const Entity& owner,Vec3,double radius,bool lineOfSight=true,unsigned limit=512);
DamageResult damage(const Entity&,const Entity& owner,double amount,int bits=4160);
Aim aim(const Entity&);
using Properties=std::map<std::string,double>;
Properties readProperties(Address weapon,unsigned lastOffset,unsigned capacityLimit=1048576,bool exactSingle=false);
double property(const Properties&,const std::string&);
bool selected(unsigned);
bool familySelected(const std::string&);
bool weaponIs(Address,const char* family) noexcept;
void initializeShared();
}
