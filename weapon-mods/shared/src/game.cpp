#include "game.hpp"

namespace csnz {
Engine engine;
std::array<unsigned,8> damageInfo{0,1,0,0,0x1010000,0x100,0,0};
void Engine::initialize(){
    traceLine=native<decltype(traceLine)>(ptr(at(0x21d3adc)));findSphere=native<decltype(findSphere)>(ptr(at(0x21d3a9c)));
    byIndex=native<decltype(byIndex)>(ptr(at(0x21d3b8c)));index=native<decltype(index)>(ptr(at(0x21d3b88)));
    manager=native<decltype(manager)>(ptr(at(0x21d3cec)));zombie=native<decltype(zombie)>(at(0x6688c0));
    makeVectors=native<decltype(makeVectors)>(ptr(at(0x21d3aac)));emitSound=native<decltype(emitSound)>(ptr(at(0x21d3ad4)));
    playback=native<decltype(playback)>(ptr(at(0x21d3c5c)));animation=native<decltype(animation)>(at(0x152d030));
    weaponTime=native<decltype(weaponTime)>(at(0x657cf0));heal=native<decltype(heal)>(at(0x14dd550));
}
Entity entity(Address o){
    if(!o)throw std::runtime_error("Null game entity");Entity e;e.object=o;e.pev=ptr(o+8);e.edict=ptr(e.pev+0x238);
    if(!e.edict||read<int>(e.edict)!=0||ptr(e.edict+0x80)!=o)throw std::runtime_error("Entity backlink mismatch");
    e.serial=read<unsigned>(e.edict+4);return e;
}
bool valid(const Entity& e) noexcept {try{return e.edict&&read<int>(e.edict)==0&&read<unsigned>(e.edict+4)==e.serial&&ptr(e.edict+0x80)==e.object&&ptr(e.object+8)==e.pev;}catch(...){return false;}}
Vec3 center(const Entity& e){return (vec(e.pev+0xd4)+vec(e.pev+0xe0))*.5;}
bool hostile(const Entity& e,const Entity& owner){const auto flags=read<unsigned>(e.pev+0x1c8);if(flags&8){const auto a=read<unsigned short>(e.object+0x6c),b=read<unsigned short>(owner.object+0x6c);return a&&b&&a!=b;}return (flags&0x20)!=0;}
bool alive(const Entity& e){return valid(e)&&read<float>(e.pev+0x178)>0&&read<int>(e.pev+0x194)==0;}
double now(){const auto t=read<float>(ptr(at(0x21d3dac)));if(!std::isfinite(t))throw std::runtime_error("Invalid engine clock");return t;}
int gameMode(){const auto m=engine.manager();if(!m)throw std::runtime_error("No game manager");return native<int(__thiscall*)(Address)>(ptr(ptr(m)+0x188))(m);}
int mode(){if(engine.zombie(-2))return 1;const int m=gameMode();return m==15||m==17||m==48||m==59?2:0;}
Trace trace(Vec3 from,Vec3 to,Address skip,int flags){if(!finite(from)||!finite(to))throw std::runtime_error("Trace vector");Trace r{};engine.traceLine(&from,&to,flags,skip,&r);if(!std::isfinite(r.fraction)||r.fraction<0||r.fraction>1.001||!finite(r.end))throw std::runtime_error("Trace result");return r;}
std::vector<Target> targets(const Entity& owner,Vec3 origin,double radius,bool lineOfSight,unsigned limit){
    if(!(radius>0&&radius<=20000)||!finite(origin))throw std::runtime_error("Target radius/vector");
    std::vector<Target> out;std::set<Address> seen;const auto world=engine.byIndex(0);Address ed=0;
    for(unsigned i=0;i<limit;i++){
        ed=engine.findSphere(ed,&origin,static_cast<float>(radius));if(!ed||ed==world||!seen.insert(ed).second)break;
        if(read<int>(ed)!=0)continue;const auto o=ptr(ed+0x80);if(!o||o==owner.object)continue;const auto e=entity(o);
        if(read<float>(e.pev+0x178)<=0||read<float>(e.pev+0x190)==0||!hostile(e,owner))continue;
        const auto c=center(e);const auto distance=length(c-origin);if(distance>radius)continue;
        if(lineOfSight){const auto r=trace(origin,c,owner.edict);if(r.fraction<.999&&r.edict!=e.edict)continue;}
        out.push_back({e,c,distance});
    }
    std::stable_sort(out.begin(),out.end(),[](const Target& a,const Target& b){return a.distance<b.distance;});return out;
}
DamageResult damage(const Entity& e,const Entity& owner,double amount,int bits){
    DamageResult result;if(!valid(owner)||!valid(e)||!(amount>0&&amount<200000))return result;
    const auto f=ptr(ptr(e.object)+17*4);if(f<mp||f>=mp+profiles()["shared"]["modules"]["mp.dll"]["size"].u32())throw std::runtime_error("Damage target outside mp");
    result.before=read<float>(e.pev+0x178);native<DamageFn>(f)(e.object,owner.pev,owner.pev,static_cast<float>(amount),bits,reinterpret_cast<Address>(damageInfo.data()));
    result.after=valid(e)?read<float>(e.pev+0x178):result.before;result.delta=std::max(0.,result.before-result.after);result.killed=result.before>0&&result.after<=0;return result;
}
Aim aim(const Entity& e){Aim a;a.source=vec(e.pev+8)+vec(e.pev+0x108);a.angles=vec(e.pev+0x74)+vec(e.pev+0x98);a.direction=direction(a.angles);return a;}
Properties readProperties(Address weapon,unsigned lastOffset,unsigned capacityLimit,bool exactSingle){
    const auto b=ptr(weapon+0x204);Properties result;if(!b)throw std::runtime_error("Missing property table");
    for(unsigned off=0x38;off<=lastOffset;off+=0x38){
        const auto q=b+off;const auto n=read<unsigned>(q+0x10),cap=read<unsigned>(q+0x14);
        if(!n||n>160||cap<n||cap>capacityLimit)throw std::runtime_error("Property SSO layout");
        const auto name=readText(cap>15?ptr(q):q,n);if(!std::all_of(name.begin(),name.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}))throw std::runtime_error("Property name");
        const auto begin=ptr(q+0x1c),end=ptr(q+0x20);if(end<begin||end-begin<4||end-begin>256||(exactSingle&&end-begin!=4))throw std::runtime_error("Property vector");
        const double v=read<float>(begin);if(!std::isfinite(v))throw std::runtime_error("Property value");result[name]=v;
    }return result;
}
double property(const Properties& p,const std::string& key){const auto i=p.find(key);if(i==p.end()||!std::isfinite(i->second))throw std::runtime_error("Missing property: "+key);return i->second;}
bool weaponIs(Address w,const char* family) noexcept {try{if(!w)return false;const auto id=read<unsigned>(w+0xe4);if(!selected(id))return false;const auto& cfg=profiles()[family]["vtables"]["mp.dll"];if(!cfg.has(std::to_string(id)))return false;for(const auto& vt:cfg[std::to_string(id)].array())if(ptr(w)==at(vt.u32()))return true;}catch(...){}return false;}
}
