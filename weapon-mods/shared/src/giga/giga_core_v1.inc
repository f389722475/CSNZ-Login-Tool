#include "game_abi.h"
#include "family_profile.h"
#include <cstdio>

namespace giga_family {
namespace {
struct Entity { Address edict{},object{},pev{}; unsigned serial{},flags{}; Vec3 center{}; };
struct Pull { Vec3 origin; double until; float radius,force; };
struct Weapon {
    Address weapon{},player{},pev{},edict{},weaponEdict{};
    unsigned serial{},weaponSerial{};
    double lastTime{},acquiredAt{},nextCharge{},nextWing1{},nextWing2{},shotTime{},crashTime{};
    unsigned hits{},shotCount{};
    bool crashing{},forcedDive{},crashVisualHeld{},pendingShield{};
    int reportedReady{-1};
    std::vector<Pull> pulls;
};
struct ScopedField { float old,last; };
struct Status { Entity entity; double from{},until{},impulseUntil{}; float speed{},gravity{}; std::map<unsigned,ScopedField> fields; };
struct Protection { Entity entity; double from{},until{},cooldown{}; };
std::map<Address,Weapon> weapons;
std::map<Address,Status> statuses;
std::map<Address,Protection> protections;
double clockValue=std::numeric_limits<double>::quiet_NaN();
std::uint32_t damageInfo[8]={0,1,0,0,0x1010000,0x100,0,0};

double now(){
    double raw=read<float>(engine.globals());
    if(!std::isfinite(raw))throw std::runtime_error("Invalid engine time");
    clockValue=!std::isfinite(clockValue)||raw<clockValue-1?raw:std::max(raw,clockValue);
    return clockValue;
}
const FamilySpec* familySpec(Address w) noexcept {
    try {return identifyFamily(read<unsigned>(w+0xe4),pointer(w),engine.mp);}
    catch(...){return nullptr;}
}
bool isFamily(Address w) noexcept {
    try {return familySpec(w)&&pointer(w+0xd0)!=0;}
    catch(...){return false;}
}
float prop(Address w,unsigned offset){
    float v=engine.property(reinterpret_cast<void*>(pointer(w+0x204)+offset),0);
    if(!std::isfinite(v))throw std::runtime_error("Invalid weapon property");
    return v;
}
int mode(){
    if(engine.zombieMode(-2))return 1;
    Address m=reinterpret_cast<Address>(engine.manager());
    Address f=pointer(pointer(m)+0x188);
    if(!executable(f))throw std::runtime_error("Invalid game-mode interface");
    int v=reinterpret_cast<int(__thiscall*)(void*)>(f)(reinterpret_cast<void*>(m));
    return v==15||v==17||v==48||v==59?2:0;
}
float damageValue(Address w,unsigned offset){return prop(w,offset+mode()*0x38);}
Vec3 center(Address pev){return (read<Vec3>(pev+0xd4)+read<Vec3>(pev+0xe0))*.5f;}
bool validEntity(const Entity& e) noexcept {
    try{return e.edict&&read<int>(e.edict)==0&&read<unsigned>(e.edict+4)==e.serial&&
        pointer(e.edict+0x80)==e.object&&pointer(e.object+8)==e.pev;}
    catch(...){return false;}
}
bool validWeapon(const Weapon& s) noexcept {
    try{return isFamily(s.weapon)&&read<int>(s.weaponEdict)==0&&read<unsigned>(s.weaponEdict+4)==s.weaponSerial&&
        pointer(s.weaponEdict+0x80)==s.weapon&&read<int>(s.edict)==0&&read<unsigned>(s.edict+4)==s.serial&&
        pointer(s.edict+0x80)==s.player&&pointer(s.player+8)==s.pev&&pointer(s.weapon+0xd0)==s.player;}
    catch(...){return false;}
}
Weapon& state(Address w){
    Address player=pointer(w+0xd0),pev=pointer(player+8),edict=pointer(pev+0x238);
    auto serial=read<unsigned>(edict+4);double t=now();
    auto it=weapons.find(w);
    if(it==weapons.end()||!validWeapon(it->second)||it->second.serial!=serial||it->second.player!=player||t<it->second.lastTime){
        Weapon s{};s.weapon=w;s.player=player;s.pev=pev;s.edict=edict;s.serial=serial;
        s.weaponEdict=pointer(pointer(w+8)+0x238);s.weaponSerial=read<unsigned>(s.weaponEdict+4);
        s.lastTime=s.acquiredAt=s.nextWing1=s.nextWing2=t;s.nextCharge=t+1;
        it=weapons.insert_or_assign(w,std::move(s)).first;
        char line[256];sprintf_s(line,"OWNED: id=%u t=%.3f weapon=%p player=%p",read<unsigned>(w+0xe4),t,reinterpret_cast<void*>(w),reinterpret_cast<void*>(player));log(line);
        sprintf_s(line,"DEFENSE_PROPERTIES: id=%u beta=%.3f crash=%.3f fatal=%.3f damage=%.0f radiusM=%.3f side=%.3f up=%.3f knockDuration=%.3f slowDuration=%.3f speed=%.3f gravity=%.3f",read<unsigned>(w+0xe4),prop(w,0x2d8),prop(w,0x17a0),prop(w,0x2680),damageValue(w,0x26b8),prop(w,0x2760),damageValue(w,0x2798),damageValue(w,0x2840),damageValue(w,0x28e8),damageValue(w,0x2990),damageValue(w,0x2a38),damageValue(w,0x2ae0));log(line);
    }
    it->second.lastTime=t;return it->second;
}
bool candidate(Address edict,Address owner,Entity& e) noexcept {
    try{
        if(!edict||read<int>(edict)!=0)return false;
        Address o=pointer(edict+0x80);if(!o||o==owner)return false;
        Address pev=pointer(o+8);
        if(read<float>(pev+0x190)==0||read<float>(pev+0x178)<=0)return false;
        e={edict,o,pev,read<unsigned>(edict+4),read<unsigned>(pev+0x1c8),center(pev)};
        return finite(e.center);
    }catch(...){return false;}
}
bool hostile(const Entity& e,const Weapon& s){
    if(e.flags&8){auto a=read<unsigned short>(e.object+0x6c),b=read<unsigned short>(s.player+0x6c);return a&&b&&a!=b;}
    return (e.flags&0x20)!=0;
}
std::vector<Entity> inSphere(Vec3 origin,float radius,const Weapon& s){
    if(!finite(origin)||!std::isfinite(radius)||radius<0||radius>100000)throw std::runtime_error("Invalid sphere");
    std::vector<Entity> result;std::set<Address> handles,objects;
    Address cursor=0,world=reinterpret_cast<Address>(engine.entityByIndex(0));
    for(int i=0;i<512;++i){
        cursor=reinterpret_cast<Address>(engine.findSphere(reinterpret_cast<void*>(cursor),&origin,radius));
        if(!cursor||cursor==world||!handles.insert(cursor).second)break;
        Entity e{};if(candidate(cursor,s.player,e)&&objects.insert(e.object).second)result.push_back(e);
    }
    return result;
}
TraceResult trace(Vec3 from,Vec3 to,Address skip){
    if(!finite(from)||!finite(to))throw std::runtime_error("Invalid trace vector");
    TraceResult r{};engine.traceLine(&from,&to,0,reinterpret_cast<void*>(skip),&r);return r;
}
bool visible(Vec3 origin,const Entity& e,const Weapon& s){auto r=trace(origin,e.center,s.edict);return r.fraction>=.999f||r.edict==e.edict;}
Vec3 closest(Address pev,Vec3 p){Vec3 lo=read<Vec3>(pev+0xd4),hi=read<Vec3>(pev+0xe0);
    for(int i=0;i<3;++i)p[i]=std::max(lo[i],std::min(hi[i],p[i]));return p;}
float hitDamage(const Entity& e,float amount,const Weapon& s,int bits=4160){
    if(!(amount>0&&amount<200000))return 0;
    Address f=pointer(pointer(e.object)+17*4);
    if(f<engine.mp||f>=engine.mp+ServerBuild.imageSize||!executable(f))throw std::runtime_error("Unknown damage vtable");
    float before=read<float>(e.pev+0x178);
    reinterpret_cast<DamageFn>(f)(reinterpret_cast<void*>(e.object),reinterpret_cast<void*>(s.pev),
        reinterpret_cast<void*>(s.pev),amount,bits,damageInfo);
    if(!validEntity(e))return 0;
    return std::max(0.f,before-read<float>(e.pev+0x178));
}
void knock(const Entity& e,Vec3 origin,float side,float up){
    if(!(e.flags&0x28))return;
    Vec3 v=read<Vec3>(e.pev+0x20),delta=e.center-origin;delta.z=0;Vec3 dir=norm(delta);
    if(side>=0){v.x=dir.x*side;v.y=dir.y*side;}if(up>=0)v.z=up;write(e.pev+0x20,v);
}
void debuff(Address w,const Entity& e,unsigned offset,float impulseDuration=0){
    if(!(e.flags&0x28))return;
    float duration=damageValue(w,offset),speed=damageValue(w,offset+0xa8),gravity=damageValue(w,offset+0x150);
    if(duration<=0)return;
    double t=now();auto it=statuses.find(e.object);
    if(it==statuses.end()||!validEntity(it->second.entity)){
        Status x{};x.entity=e;x.from=t;x.speed=speed;x.gravity=gravity;
        it=statuses.insert_or_assign(e.object,std::move(x)).first;
    }
    auto& x=it->second;x.until=std::max(x.until,t+duration);
    // The shield's native knockback duration is separate from its slow duration.
    // Preserve the initial impulse; never reapply velocity through walls each frame.
    if(impulseDuration>0)x.impulseUntil=std::max(x.impulseUntil,t+impulseDuration);
    x.speed=x.speed<0?speed:speed<0?x.speed:std::min(x.speed,speed);x.gravity=std::max(x.gravity,gravity);
}
void setScoped(Status& s,unsigned offset,float value){
    if(!statusSet)throw std::runtime_error("Missing unified status setter");
    statusSet(s.entity.object,s.entity.edict,s.entity.pev,s.entity.serial,offset,value);
}
void restore(Status& s){
    if(statusRelease)statusRelease(s.entity.object,s.entity.edict,s.entity.pev,s.entity.serial);
}
void statusFrame(double t){
    for(auto it=statuses.begin();it!=statuses.end();){auto& s=it->second;
        if(!validEntity(s.entity)){it=statuses.erase(it);continue;}
        if(t<s.from||t>=s.until||read<float>(s.entity.pev+0x178)<=0){restore(s);it=statuses.erase(it);continue;}
        if(s.speed>=0){setScoped(s,0x240,s.speed);Vec3 v=read<Vec3>(s.entity.pev+0x20);float n=std::hypot(v.x,v.y);
            if(t>=s.impulseUntil&&n>s.speed){v.x*=s.speed/n;v.y*=s.speed/n;write(s.entity.pev+0x20,v);}}
        if(s.gravity>=0)setScoped(s,0x12c,s.gravity);++it;
    }
}
int area(Address w,Vec3 origin,float radius,float amount,float side=-9999,float up=-9999,unsigned status=0,bool actors=false,float impulseDuration=0){
    auto& s=state(w);int count=0;
    for(auto& e:inSphere(origin,radius,s)){
        if(length(closest(e.pev,origin)-origin)>radius||!visible(origin,e,s))continue;
        bool enemy=hostile(e,s);if((e.flags&8)&&!enemy)continue;if(actors&&!enemy)continue;
        if(hitDamage(e,amount,s)>0){++count;if(enemy)++s.hits;knock(e,origin,side,up);if(status)debuff(w,e,status,impulseDuration);}
    }
    return count;
}
void effect(Address w,int code,int subcode=0,float distance=0,const Vec3* suppliedOrigin=nullptr,const Vec3* direction=nullptr){
    auto& s=state(w);Vec3 origin=suppliedOrigin?*suppliedOrigin:read<Vec3>(s.pev+8);
    Vec3 angles=eventAngles(direction?directionAngles(*direction):read<Vec3>(s.pev+0x74));
    engine.playback(0,reinterpret_cast<void*>(s.edict),read<unsigned short>(w+0x218),0,&origin,&angles,distance,0,0,0,subcode,code);
}
void rayEffect(Address w,int code,int subcode,const Aim& a,Vec3 impact){
    auto& s=state(w);Vec3 delta=impact-a.source;float distance=length(delta);
    Vec3 origin=a.source-read<Vec3>(s.pev+0x198),direction=distance>.000001f?delta*(1/distance):norm(a.direction);
    effect(w,code,subcode,distance,&origin,&direction);
}
Aim autoAim(Address w,Aim a,const TraceResult& directTrace){
    auto& s=state(w);Entity direct{};
    if(candidate(directTrace.edict,s.player,direct)&&hostile(direct,s))return a;
    float cosCone=std::cos(prop(w,0xc78)*Pi/180),best=-1;
    Vec3 chosen=a.direction;
    for(auto& e:inSphere(a.source,prop(w,0xcb0)*Meter,s)){
        if(!hostile(e,s)||!visible(a.source,e,s))continue;
        Vec3 delta=e.center-a.source;float n=length(delta),dl=length(a.direction);if(n<=0||dl<=0)continue;
        float score=dot(a.direction,delta)/n;
        if(score/dl>=cosCone&&score>best){chosen=delta*(1/n);best=score;}
    }
    a.direction=chosen;return a;
}
Protection& defense(const Weapon& s){
    auto it=protections.find(s.player);
    if(it==protections.end()||it->second.entity.serial!=s.serial||it->second.entity.edict!=s.edict){
        Protection p{};p.entity={s.edict,s.player,s.pev,s.serial,0,{}};p.from=now();
        it=protections.insert_or_assign(s.player,p).first;
    }
    return it->second;
}
bool validDefense(const Protection& p){return validEntity(p.entity)&&read<float>(p.entity.pev+0x178)>0&&read<int>(p.entity.pev+0x194)==0;}
void grantGod(const Weapon& s,double t,float duration,const char* reason){
    if(!(duration>0&&duration<=10))throw std::runtime_error("Invalid defense duration");
    auto& p=defense(s);p.from=t;p.until=std::max(p.until,t+duration);
    char line[256];sprintf_s(line,"DEFENSE_WINDOW: id=%u reason=%s t=%.3f duration=%.3f until=%.3f",read<unsigned>(s.weapon+0xe4),reason,t,duration,p.until);log(line);
}
bool hasGod(Address player,double t){auto it=protections.find(player);return it!=protections.end()&&validDefense(it->second)&&t>=it->second.from&&t<it->second.until;}
bool shieldReady(const Weapon& s,double t){
    return mode()!=0&&std::max(0.,t-s.acquiredAt)>=damageValue(s.weapon,0x2530)&&
        s.hits>=damageValue(s.weapon,0x2488)&&t>=defense(s).cooldown;
}
Weapon* shieldState(Address player,double t){
    for(auto& pair:weapons){auto& s=pair.second;
        if(s.player!=player||!validWeapon(s)||read<float>(s.pev+0x178)<=0||read<int>(s.pev+0x194)!=0)continue;
        if(shieldReady(s,t))return &s;
    }
    return nullptr;
}
void defenseFrame(double t){
    for(auto it=protections.begin();it!=protections.end();)if(!validDefense(it->second)||t<it->second.from)it=protections.erase(it);else ++it;
    for(auto it=weapons.begin();it!=weapons.end();){auto& s=it->second;
        if(!validWeapon(s)||t<s.lastTime){it=weapons.erase(it);continue;}
        if(read<float>(s.pev+0x178)<=0||read<int>(s.pev+0x194)!=0){protections.erase(s.player);it=weapons.erase(it);continue;}
        int old=read<int>(s.weapon+0x168),amount=static_cast<int>(prop(s.weapon,0x1f8)),cap=static_cast<int>(prop(s.weapon,0x118));
        double interval=damageValue(s.weapon,0x150);
        if(interval<=0||amount<0||cap<0)throw std::runtime_error("Invalid charge schedule");
        // This build's predicted weapon timer is REMAINING seconds, not global time.
        // Verified from a same-run trace: +1cc counts 7 -> 0 while globals advances.
        // Do not bank seven charge ticks while it is active, including while holstered.
        if(read<int>(s.weapon+0x1c0)==2){
            double remaining=read<float>(s.weapon+0x1cc);
            if(!std::isfinite(remaining)||remaining>prop(s.weapon,0x2a0)+.1)throw std::runtime_error("Invalid full-charge remaining timer");
            if(remaining>0)s.nextCharge=t+remaining;
        }
        double ticks=t<s.nextCharge?0:std::floor((t-s.nextCharge)/interval)+1;
        int charged=static_cast<int>(std::clamp(old+ticks*amount,0.,static_cast<double>(cap)));
        s.nextCharge+=ticks*interval;s.lastTime=t;if(charged!=old)write(s.weapon+0x168,charged);
        if(s.pendingShield){s.pendingShield=false;
            int targets=area(s.weapon,read<Vec3>(s.pev+8),prop(s.weapon,0x2760)*Meter,damageValue(s.weapon,0x26b8),damageValue(s.weapon,0x2798),damageValue(s.weapon,0x2840),0x2990,true,std::max(0.f,damageValue(s.weapon,0x28e8)));
            effect(s.weapon,7); // Recovery amount remains unknown; never invent HP writes.
            char line[192];sprintf_s(line,"SHIELD_EFFECT: id=%u t=%.3f targets=%d event=7 healImplemented=false",read<unsigned>(s.weapon+0xe4),t,targets);log(line);
        }
        bool ready=shieldReady(s,t);
        if(s.reportedReady!=static_cast<int>(ready)){
            s.reportedReady=ready;char line[256];sprintf_s(line,"SHIELD_READY: id=%u ready=%d t=%.3f held=%.3f hits=%u holdRequired=%.3f hitsRequired=%.0f cooldownUntil=%.3f",read<unsigned>(s.weapon+0xe4),ready,t,t-s.acquiredAt,s.hits,damageValue(s.weapon,0x2530),damageValue(s.weapon,0x2488),defense(s).cooldown);log(line);
        }
        ++it;
    }
}
void wing(Address w){
    auto& s=state(w);double t=now();if(mode()==0||t<std::min(s.nextWing1,s.nextWing2))return;
    Vec3 origin=read<Vec3>(s.pev+8);float outer=prop(w,0x1d50)*Meter,inner=prop(w,0x1d88)*Meter;
    int maxTargets=static_cast<int>(prop(w,0x1d18));
    if(maxTargets<0||maxTargets>128)throw std::runtime_error("Invalid wing target count");
    struct Target{Entity e;float distance;};std::vector<Target> targets;
    for(auto& e:inSphere(origin,outer,s))if(hostile(e,s)&&visible(origin,e,s)){
        float d=length(e.center-origin);if(std::isfinite(d)&&d<=outer)targets.push_back({e,d});}
    std::stable_sort(targets.begin(),targets.end(),[](const Target& a,const Target& b){return a.distance<b.distance;});
    if(targets.empty())return;bool innerVolley=targets.front().distance<=inner;
    if(t<(innerVolley?s.nextWing2:s.nextWing1))return;
    targets.resize(std::min(targets.size(),static_cast<std::size_t>(innerVolley?1:maxTargets)));
    std::vector<int> fired;
    for(auto& target:targets){auto& e=target.e;for(int i=0;i<(innerVolley?maxTargets:1);++i){
        if(!validEntity(e)||read<float>(e.pev+0x178)<=0)break;
        float delta=hitDamage(e,damageValue(w,innerVolley?0x20d0:0x1df8),s,4098);
        if(delta>0){++s.hits;debuff(w,e,innerVolley?0x2178:0x1ea0);}
        fired.push_back(engine.entityIndex(reinterpret_cast<void*>(e.edict)));
    }}
    if(innerVolley)s.nextWing2=t+prop(w,0x2098);else s.nextWing1=t+prop(w,0x1dc0);
    if(!fired.empty()){
        fired.resize(std::max<std::size_t>(4,fired.size()),0);Vec3 angles=eventAngles(read<Vec3>(s.pev+0x74));
        engine.playback(0,reinterpret_cast<void*>(s.edict),read<unsigned short>(w+0x218),0,&origin,&angles,
            static_cast<float>(fired[2]),static_cast<float>(fired[3]),fired[0],fired[1],0,5);
    }
}
void pullFrame(Address w,double t){auto& s=state(w);
    s.pulls.erase(std::remove_if(s.pulls.begin(),s.pulls.end(),[t](const Pull& p){return t>=p.until;}),s.pulls.end());
    for(auto& p:s.pulls)for(auto& e:inSphere(p.origin,p.radius,s))if(hostile(e,s)&&visible(p.origin,e,s))write(e.pev+0x20,norm(p.origin-e.center)*p.force);
}
} // namespace

bool active(Address w) noexcept {
    try{if(!isFamily(w))return false;Address p=pointer(w+0xd0);return pointer(p+0x1024)==w&&read<float>(pointer(p+8)+0x178)>0;}
    catch(...){return false;}
}
void acquired(Address w){if(isFamily(w))state(w);}
Aim aim(Address w){auto& s=state(w);Vec3 a=read<Vec3>(s.pev+0x68)+read<Vec3>(s.pev+0x74);
    float p=a.x*Pi/180,y=a.y*Pi/180;return {read<Vec3>(s.pev+8)+read<Vec3>(s.pev+0x198),{std::cos(p)*std::cos(y),std::cos(p)*std::sin(y),-std::sin(p)}};
}
void shoot(Address w,Aim a){
    auto& s=state(w);int damage=static_cast<int>(damageValue(w,0x5b0));float range=prop(w,0xc08)*Meter;
    if(!(damage>0&&damage<100000&&range>0&&range<100000))throw std::runtime_error("Invalid bullet properties");
    auto direct=trace(a.source,a.source+a.direction*range,s.edict);a=autoAim(w,a,direct);
    unsigned flags=read<unsigned>(s.pev+0x1c8);Vec3 velocity=read<Vec3>(s.pev+0x20);
    unsigned offset=!(flags&0x200)?0x2d10:std::hypot(velocity.x,velocity.y)>140?0x2d80:(flags&0x4000)?0x2df0:0x2ca0;
    float spread=prop(w,offset)+prop(w,offset+0x38)*read<float>(w+0x198);
    if(!std::isfinite(spread))throw std::runtime_error("Invalid bullet spread");
    Address gp=engine.globals();std::array<Vec3,3> saved{read<Vec3>(gp+0x28),read<Vec3>(gp+0x34),read<Vec3>(gp+0x40)};
    // Scope restoration includes SEH unwind (/EHa), not only ordinary returns.
    struct RestoreVectors {Address gp;std::array<Vec3,3> values;~RestoreVectors(){for(int i=0;i<3;++i)rawWrite(gp+0x28+i*12,&values[i],12);}};
    Vec3 result{},right{},up{};
    struct Before{Entity e;float health;};std::vector<Before> before;
    {
        RestoreVectors restore{gp,saved};Vec3 angles=directionAngles(a.direction);engine.makeVectors(&angles);
        right=read<Vec3>(gp+0x40);up=read<Vec3>(gp+0x34);
        for(auto& e:inSphere(a.source,range,s))before.push_back({e,read<float>(e.pev+0x178)});
        engine.bullet(reinterpret_cast<void*>(s.player),&result,a.source.x,a.source.y,a.source.z,a.direction.x,a.direction.y,a.direction.z,
            spread,range,static_cast<int>(prop(w,0xc40)),127,damage,prop(w,0x658),reinterpret_cast<void*>(s.pev),0,read<int>(s.player+0xc54),0,0);
    }
    ++shots;
    for(auto& x:before)if(validEntity(x.e)&&x.health>read<float>(x.e.pev+0x178)&&hostile(x.e,s))++s.hits;
    double t=now();s.shotCount=s.shotCount&&t>=s.shotTime&&t-s.shotTime<=prop(w,0x690)?s.shotCount+1:1;s.shotTime=t;
    unsigned small=static_cast<unsigned>(prop(w,0x6c8)),full=static_cast<unsigned>(prop(w,0x818));
    if(!small||!full)throw std::runtime_error("Invalid explosion cadence");
    Vec3 shotDirection=a.direction+right*result.x+up*result.y;
    auto tr=trace(a.source,a.source+shotDirection*range,s.edict);
    Address begin=pointer(s.player+0x84),end=pointer(s.player+0x88),capacity=pointer(s.player+0x8c);
    if(end<begin||end-begin>28*128||(end-begin)%28||end>capacity)throw std::runtime_error("Invalid native bullet impact vector");
    Vec3 impact=begin==end?tr.end:read<Vec3>(begin+16);if(!finite(impact))throw std::runtime_error("Invalid bullet impact");
    int subcode=0;bool isFull=s.shotCount%full==0;
    if(isFull||s.shotCount%small==0){area(w,impact,prop(w,isFull?0x850:0x700)*Meter,damageValue(w,isFull?0x888:0x738),-9999,-9999,isFull?0x930:0);subcode=isFull?4:1;}
    rayEffect(w,0,subcode,{a.source,norm(shotDirection)},impact);
}
void burst(Address w,const Aim& a){auto& s=state(w);auto tr=trace(a.source,a.source+a.direction*(prop(w,0x1340)*Meter),s.edict);
    area(w,tr.end,prop(w,0x1378)*Meter,damageValue(w,0x13b0),-9999,-9999,0x1490);rayEffect(w,3,0,a,tr.end);
}
void swing(Address w,int stage){auto& s=state(w);auto a=aim(w);
    if(stage==2)area(w,read<Vec3>(s.pev+8),prop(w,0xdc8)*Meter,damageValue(w,0xe00),damageValue(w,0xea8),damageValue(w,0xf50));
    else {auto tr=trace(a.source,a.source+a.direction*(prop(w,0x10a0)*Meter),s.edict);area(w,tr.end,prop(w,0x10d8)*Meter,damageValue(w,0x1110));
        float duration=damageValue(w,0x11b8);if(duration>0)s.pulls.push_back({tr.end,now()+duration,prop(w,0x1298)*Meter,prop(w,0x12d0)});
        rayEffect(w,2,0,a,tr.end);}
}
void frame(Address w){auto& s=state(w);double t=now();int before=read<int>(w+0x1c0);engine.aux(reinterpret_cast<void*>(w));
    if(read<int>(w+0x1c0)==2&&before!=2){grantGod(s,t,prop(w,0x2d8),"beta");s.nextCharge=t+std::max(0.f,read<float>(w+0x1cc));area(w,read<Vec3>(s.pev+8),prop(w,0x310)*Meter,damageValue(w,0x348),damageValue(w,0x3f0),damageValue(w,0x498));}
    wing(w);pullFrame(w,t);
}
void crashStart(Address w,const Aim& a){auto& s=state(w);Vec3 horizontal{a.direction.x,a.direction.y,0};Vec3 v=norm(horizontal)*prop(w,0x1730);
    v.z=prop(w,0x1768);write(s.pev+0x20,v);s.crashing=true;s.forcedDive=false;s.crashVisualHeld=false;s.crashTime=now();grantGod(s,s.crashTime,prop(w,0x17a0),"crash");
    char line[192];sprintf_s(line,"CRASH_START: id=%u t=%.3f endRadiusM=%.3f diveSpeed=%.3f",read<unsigned>(w+0xe4),s.crashTime,prop(w,0x1880),prop(w,0x1848));log(line);
}
CrashDeployState captureCrashDeploy(Address w){
    auto it=weapons.find(w);if(it==weapons.end())return {};
    const auto& s=it->second;
    if(!s.crashing||!validWeapon(s)||read<float>(s.pev+0x178)<=0||read<int>(s.pev+0x194)!=0||
       (read<unsigned>(s.pev+0x1c8)&0x200)||read<int>(s.pev+0x118)==5)return {};
    int phase=read<int>(w+0x178);if(phase!=1&&phase!=2)return {};
    CrashDeployState saved{w,s.player,s.weaponSerial,s.serial,phase,read<std::array<float,4>>(w+0x1d4)};
    for(float v:saved.timers)if(!std::isfinite(v))throw std::runtime_error("Invalid native Crash timer");
    return saved;
}
void restoreCrashDeploy(Address w,const CrashDeployState& saved){
    if(!saved.phase||saved.weapon!=w)return;
    auto it=weapons.find(w);if(it==weapons.end())return;
    const auto& s=it->second;
    if(!s.crashing||!validWeapon(s)||s.player!=saved.player||s.serial!=saved.ownerSerial||s.weaponSerial!=saved.weaponSerial||
       read<float>(s.pev+0x178)<=0||read<int>(s.pev+0x194)!=0||
       (read<unsigned>(s.pev+0x1c8)&0x200)||read<int>(s.pev+0x118)==5||read<int>(w+0x178)!=0)return;
    // Native DeployTiming resets phase and both countdowns even in midair.
    // Restore only this in-progress flight, not draw animation, charge, velocity,
    // original crashTime or protection: switching back must not relaunch it.
    write(w+0x178,saved.phase);write(w+0x1d4,saved.timers);
    char line[224];sprintf_s(line,"CRASH_DEPLOY_RESUME: id=%u t=%.3f phase=%d diveRemaining=%.4f startRemaining=%.4f",read<unsigned>(w+0xe4),now(),saved.phase,saved.timers[0],saved.timers[2]);log(line);
}
void finishCrash(Weapon& s,double t,bool impact,const char* reason){
    // End exactly once, including after a switch. Never call a holstered weapon's
    // Aux/SendWeaponAnim: that would overwrite the newly equipped view model.
    s.crashing=false;write(s.weapon+0x178,0);
    if(impact){
        area(s.weapon,read<Vec3>(s.pev+8),prop(s.weapon,0x18b8)*Meter,damageValue(s.weapon,0x18f0),damageValue(s.weapon,0x1998),damageValue(s.weapon,0x1a40),0x1b20);
        effect(s.weapon,4,3);
    }else effect(s.weapon,4,2); // Native case 4/subcode 2 clears the flight latch.
    char line[192];sprintf_s(line,"CRASH_END: id=%u t=%.3f impact=%d reason=%s",read<unsigned>(s.weapon+0xe4),t,impact,reason);log(line);
}
void crashFrame(double t){
    for(auto& pair:weapons){auto& s=pair.second;if(!s.crashing||!validWeapon(s))continue;
        Address w=s.weapon;double age=t-s.crashTime;
        if(age<0||read<float>(s.pev+0x178)<=0||read<int>(s.pev+0x194)!=0){finishCrash(s,t,false,"invalid_life_or_time");continue;}
        bool held=active(w);int phase=read<int>(w+0x178);
        bool contact=(read<unsigned>(s.pev+0x1c8)&0x200)||read<int>(s.pev+0x118)==5;
        // PID9380: native deploy cleared phase at z=128.37 with flags=8.
        // A zero phase alone is not a landing or permission for an air explosion.
        if(phase==0&&age>.1&&contact){finishCrash(s,t,true,"native_landing");continue;}
        if(age>5){finishCrash(s,t,false,"timeout");continue;}
        float start=prop(w,0x17d8),end=prop(w,0x1810),dive=prop(w,0x1848),radius=prop(w,0x1880)*Meter;
        if(start<0||end<start||end>5||dive<=0||dive>10000||radius<0||radius>1000)throw std::runtime_error("Invalid Crash properties");
        float startRemaining=read<float>(w+0x1dc),diveRemaining=read<float>(w+0x1d4);
        if(!std::isfinite(startRemaining)||!std::isfinite(diveRemaining))throw std::runtime_error("Invalid Crash countdown");
        if(!held&&startRemaining<0&&contact){
            write(s.pev+0x20,Vec3{});
            write(w+0x1d4,0.f);write(w+0x1dc,0.f);
            if(read<float>(w+0x1cc)<end)write(w+0x1c0,0);
            finishCrash(s,t,true,"holstered_landing");continue;
        }
        // Candidate contact gate uses native ChargeShootBEndRadius separately
        // from explosion radius, with hostile/alive identity and LOS;
        // allies, scenery and enemies behind walls cannot force a dive.
        if(!s.forcedDive&&startRemaining<0&&diveRemaining>0&&(phase==1||phase==2)){
            Vec3 origin=read<Vec3>(s.pev+8);
            for(const auto& e:inSphere(origin,radius,s))if(hostile(e,s)&&length(closest(e.pev,origin)-origin)<=radius&&visible(origin,e,s)){
                s.forcedDive=true;write(w+0x1d4,-.001f);
                char line[224];sprintf_s(line,"CRASH_ENEMY_DIVE: id=%u t=%.3f target=%p serial=%u held=%d",read<unsigned>(w+0xe4),t,reinterpret_cast<void*>(e.object),e.serial,held);log(line);break;
            }
        }
        // The native countdown also advances while holstered. Do not replace it
        // with wall-clock age (the recording showed an early automatic dive with
        // +0.012 seconds still left), or push downward after native phase 3.
        if((phase==1||phase==2)&&(s.forcedDive||(!held&&diveRemaining<0))){auto v=read<Vec3>(s.pev+0x20);v.z=-dive;write(s.pev+0x20,v);}
    }
}
void globalFrame(){double t=now();statusFrame(t);crashFrame(t);defenseFrame(t);}
bool blockDamage(Address player,float amount){if(amount>0&&hasGod(player,now())){
    unsigned count=++blocked;if(count<=16){char line[192];sprintf_s(line,"DAMAGE_BLOCKED: phase=entry t=%.3f amount=%.3f player=%p",now(),amount,reinterpret_cast<void*>(player));log(line);}return true;}return false;}
void serverCleanup() noexcept {
    std::lock_guard<std::recursive_mutex> lock(gameMutex);
    bool ok=true;protections.clear();
    for(auto& pair:statuses)try{restore(pair.second);}catch(...){ok=false;}
    for(auto& pair:weapons)try{auto& s=pair.second;if(s.crashing&&validWeapon(s)){effect(s.weapon,4,2);write(s.weapon+0x178,0);write(s.weapon+0x1c0,0);}}catch(...){ok=false;}
    if(ok){statuses.clear();weapons.clear();serverClean=true;}
}


} // namespace giga_family
