#include "families.hpp"
#include "hooks.hpp"
#include "brokers.hpp"
#include "world.hpp"

namespace csnz {
extern "C" {
int __cdecl GigaInitialize(Address,void(__cdecl*)(Address,Address,Address,unsigned,unsigned,float),void(__cdecl*)(Address,Address,Address,unsigned),void(__cdecl*)(const char*));
int __cdecl GigaActive(Address);void __cdecl GigaAcquire(Address);void __cdecl GigaAim(Address,Address);
void __cdecl GigaShoot(Address,Address);void __cdecl GigaBurst(Address,Address);void __cdecl GigaSwing(Address,int);
void __cdecl GigaFrame(Address);void __cdecl GigaGlobalFrame();void __cdecl GigaCrashStart(Address,Address);
void __cdecl GigaCaptureDeploy(Address,Address);void __cdecl GigaRestoreDeploy(Address,Address);
int __cdecl GigaBlockDamage(Address,float);Address __cdecl GigaArmorWeapon(Address);
int __cdecl GigaArmorReady(Address,Address,unsigned,unsigned);void __cdecl GigaArmorCommit(Address,Address,unsigned,unsigned);
unsigned __cdecl GigaSnapshot(Address,unsigned);void __cdecl GigaStop();unsigned __cdecl GigaStats(Address);
}
namespace {
void __cdecl setStatus(Address object,Address edict,Address pev,unsigned serial,unsigned offset,float value){StatusBroker::set("giga",Entity{object,pev,edict,serial},offset,value);}
void __cdecl releaseStatus(Address object,Address edict,Address pev,unsigned serial){StatusBroker::release("giga",Entity{object,pev,edict,serial});}
void __cdecl logSink(const char* text){log(std::string("giga: ")+text);}
struct State {Address weapon{};int before{};std::array<float,6> aim{};std::array<unsigned,10> deploy{};};
bool gigaActive(Address w){return active&&!stopping&&GigaActive(w)!=0;}
void getAim(State& s){GigaAim(s.weapon,reinterpret_cast<Address>(s.aim.data()));}
void frame(){
    if(stopping||!active){GigaStop();GigaGlobalFrame();World::clear("giga");return;}
    GigaGlobalFrame();std::array<unsigned,7> stats{};GigaStats(reinterpret_cast<Address>(stats.data()));if(stats[6])throw std::runtime_error("Giga gameplay failure");
    std::array<std::array<unsigned,10>,64> rows{};const auto n=GigaSnapshot(reinterpret_cast<Address>(rows.data()),64);if(n>rows.size())throw std::runtime_error("Giga snapshot capacity");
    std::set<std::string> wanted;for(unsigned i=0;i<n;i++){
        const auto& s=rows[i];if(!s[8]||(!s[7]&&!s[9]))continue;const auto pev=ptr(s[1]+8);if(read<float>(pev+0x178)<=0||read<int>(pev+0x194)!=0)continue;
        const auto key="wing/"+std::to_string(s[0])+"/"+std::to_string(s[4]);wanted.insert(key);VisualOptions o;o.angles={0,read<float>(pev+0x54),0};
        World::set("giga",key,s[5]==725?"models/ef_beamgun_wingman.mdl":"models/ef_beamgunle_wingman.mdl",vec(pev+8),o);
    }World::retain("giga",wanted);
}
}
bool gigaClean(){std::array<unsigned,7> stats{};GigaStats(reinterpret_cast<Address>(stats.data()));return stats[2]!=0;}
extern "C" void __cdecl GigaMapReset() noexcept;
void installGiga(){
    if(!GigaInitialize(mp,setStatus,releaseStatus,logSink))throw std::runtime_error("Giga initialization");
    World::onMapReset([]{GigaMapReset();});
    for(const Address rva:{0xba1bc0u,0xbb10c0u})Hooks::attach(at(rva),"giga",[](Invocation& v){v.createState<State>().weapon=v.registers->ecx;},[](Invocation& v){if(active&&!stopping&&v.registers->eax)GigaAcquire(v.state<State>().weapon);});
    Hooks::attach(at(0xbab520),"giga",[](Invocation& v){auto& s=v.createState<State>();const auto w=v.registers->ecx;if(gigaActive(w)){s.weapon=w;s.before=read<int>(w+0x160);getAim(s);}},[](Invocation& v){auto& s=v.state<State>();if(s.weapon&&active&&!stopping&&read<int>(s.weapon+0x160)==s.before-1)GigaShoot(s.weapon,reinterpret_cast<Address>(s.aim.data()));});
    Hooks::attach(at(0xba0a40),"giga",[](Invocation& v){auto& s=v.createState<State>();const auto w=v.registers->ecx;if(gigaActive(w)){const auto m=read<int>(w+0x1c0);if(m==1||m==2){s.weapon=w;getAim(s);}}},[](Invocation& v){auto& s=v.state<State>();if(s.weapon&&active&&!stopping)GigaBurst(s.weapon,reinterpret_cast<Address>(s.aim.data()));});
    Hooks::attach(at(0xba0ff0),"giga",[](Invocation& v){auto& s=v.createState<State>();const auto w=v.registers->ecx;if(gigaActive(w)){s.weapon=w;s.before=read<int>(w+0x178);getAim(s);}},[](Invocation& v){auto& s=v.state<State>();if(s.weapon&&active&&!stopping&&s.before==0&&read<int>(s.weapon+0x178)==1)GigaCrashStart(s.weapon,reinterpret_cast<Address>(s.aim.data()));});
    Hooks::attach(at(0xba1570),"giga",[](Invocation& v){auto& s=v.createState<State>();const auto w=v.registers->ecx;if(gigaActive(w)){GigaFrame(w);s.weapon=w;s.before=read<int>(w+0x64);}},[](Invocation& v){auto& s=v.state<State>();if(s.weapon&&active&&!stopping&&(s.before==2||s.before==3)&&read<int>(s.weapon+0x64)==0)GigaSwing(s.weapon,s.before);});
    Hooks::attach(at(0xba0f10),"giga",[](Invocation& v){auto& s=v.createState<State>();if(active&&!stopping){s.weapon=v.registers->ecx;GigaCaptureDeploy(s.weapon,reinterpret_cast<Address>(s.deploy.data()));}},[](Invocation& v){auto& s=v.state<State>();if(s.weapon&&active&&!stopping&&v.registers->eax)GigaRestoreDeploy(s.weapon,reinterpret_cast<Address>(s.deploy.data()));});
    Hooks::attach(at(0x13f8a80),"giga",[](Invocation&){frame();});
    DamageBroker::add("giga",[](const DamageContext& c){return active&&!stopping&&GigaBlockDamage(c.victim,c.damage)!=0;});
    ArmorGate::add({"giga",[](Address victim){return active&&!stopping?GigaArmorWeapon(victim):0;},[](const ArmorContext& c)->std::any{if(active&&!stopping&&GigaArmorReady(c.weapon,c.object,c.serial,c.weaponSerial))return true;return {};},[](const ArmorContext& c,const std::any&){
        if(!active||stopping)return;GigaArmorCommit(c.weapon,c.object,c.serial,c.weaponSerial);const auto current=read<float>(c.pev+0x178);const auto amount=c.before-current;
        if(std::isfinite(amount)&&amount>0&&ptr(ptr(c.object)+80)==at(0x14dd550))engine.heal(c.object,c.pev,static_cast<float>(amount),0,reinterpret_cast<Address>(damageInfo.data()));
    }});
}
}
