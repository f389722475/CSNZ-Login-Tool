#include "families.hpp"
#include "hooks.hpp"
#include "brokers.hpp"
#include "world.hpp"
#include <optional>

namespace csnz {
namespace leap {
enum class Phase {Idle,Leap,Glide,Dive};
struct State {
    Address weapon{},property{},module{};unsigned id{};Entity owner,we;Properties props;int mode{};
    double created{},last{},nextCharge{},phaseUntil{},godUntil{},specialCharged{},diveBorn{};Vec3 diveOrigin;
    unsigned previousButtons{};Phase phase=Phase::Idle;int gaugeOld{},stateOld{};std::optional<int> gaugeLast,stateLast;
};
struct Projectile {unsigned id{};std::shared_ptr<State> state;Vec3 position,direction;double speed{},born{},last{},expires{};bool boosted{};};
struct Status {Entity e;double from{},until{},speed{},gravity{};};
struct Queued {std::string key,kind;unsigned id{};Vec3 position;double scale;};
struct Shot {std::shared_ptr<State> state;int clip{};bool nativeAttack=false;Vec3 source,direction;};
struct Move {std::shared_ptr<State> state;Address pm{};};
std::map<Address,std::shared_ptr<State>> states;
std::vector<Projectile> projectiles;std::map<Address,Status> statuses;std::vector<Queued> queue;
thread_local std::vector<std::shared_ptr<Shot>> shots;
EffectBank visuals{"leapstrike",192};bool cleaned=false;double lastTick=std::numeric_limits<double>::quiet_NaN();unsigned projectileId=0,visualId=0;
Address(__thiscall* moduleGet)(Address,unsigned){};void(__thiscall* moduleDeactivate)(Address,unsigned){};
double value(const State& s,const std::string& name,bool variant=false){return property(s.props,name+(variant?(s.mode==1?"Zombie":s.mode==2?"Scenario":""):""));}
bool validState(const State& s){return weaponIs(s.weapon,"leapstrike")&&read<unsigned>(s.weapon+0xe4)==s.id&&valid(s.we)&&valid(s.owner)&&ptr(s.weapon+0xd0)==s.owner.object;}
bool isActive(const State& s){return validState(s)&&read<float>(s.owner.pev+0x178)>0&&ptr(s.owner.object+0x1024)==s.weapon;}
bool ownedModule(const State& s){return s.module&&ptr(s.module)==at(profiles()["leapstrike"]["module_vtable"].u32())&&ptr(s.module+8)==s.owner.object&&read<unsigned>(s.module+4)==25;}
void setGauge(State& s,double n){const auto v=static_cast<int>(clamp(std::floor(n),0,100));write(s.weapon+0x168,v);s.gaugeLast=v;}
void setState(State& s,int n){write(s.weapon+0x1c0,n);s.stateLast=n;}
void gain(State& s,double n,bool special=false){if(n<=0)return;if(special){n=std::min(n,std::max(0.,value(s,"MaxSpecialCharge")-s.specialCharged));s.specialCharged+=n;}setGauge(s,read<int>(s.weapon+0x168)+n);}
void finish(State& s,bool landed=false){
    if(!s.module){s.phase=Phase::Idle;if(!landed)s.godUntil=0;return;}
    if(valid(s.owner)&&ownedModule(s)){
        write(s.module+0x10,0);write(s.module+0x14,0);moduleDeactivate(s.owner.object,25);write(s.module+0x14,0);write(s.module+0x18,Address(0));
    }s.module=0;s.phase=Phase::Idle;if(!landed)s.godUntil=0;if(validState(s))setState(s,0);
}
std::shared_ptr<State> acquire(Address w){
    if(!weaponIs(w,"leapstrike"))return {};auto i=states.find(w);if(i!=states.end()&&validState(*i->second))return i->second;if(i!=states.end()&&i->second->module)finish(*i->second);
    auto s=std::make_shared<State>();s->weapon=w;s->id=read<unsigned>(w+0xe4);s->owner=entity(ptr(w+0xd0));s->we=entity(w);s->property=ptr(w+0x204);s->props=readProperties(w,0x11f0,4096,true);
    for(const auto& [key,v]:s->props)if(v<0||v>20000||key.size()>128)throw std::runtime_error("Leap property bounds");
    for(const char* key:{"ProjSpeed","ProjDuration","ProjExploRadius","AModeExploRadius","GaugeChargeInterval","LeapTime","GlidingTime","DiveTime"})if(!(value(*s,key)>0))throw std::runtime_error("Leap required property");
    const auto t=now();s->mode=mode();s->created=s->last=t;s->nextCharge=t+value(*s,"GaugeChargeInterval",true);s->gaugeOld=read<int>(w+0x168);s->stateOld=read<int>(w+0x1c0);states[w]=s;return s;
}
std::vector<Entity> areaTargets(const State& s,Vec3 origin,double radius){
    if(!(radius>=0&&radius<=20000))throw std::runtime_error("Leap target radius");std::vector<Entity> result;std::set<Address> seen;Address cursor=0;const auto world=engine.byIndex(0);
    for(unsigned i=0;i<512;i++){
        cursor=engine.findSphere(cursor,&origin,static_cast<float>(radius));if(!cursor||cursor==world||!seen.insert(cursor).second)break;if(read<int>(cursor))continue;const auto o=ptr(cursor+0x80);if(!o||o==s.owner.object)continue;const auto e=entity(o);
        if(read<float>(e.pev+0x178)<=0||read<float>(e.pev+0x190)==0)continue;if((read<unsigned>(e.pev+0x1c8)&8)&&!hostile(e,s.owner))continue;
        const auto lo=vec(e.pev+0xd4),hi=vec(e.pev+0xe0);Vec3 closest=origin;for(unsigned j=0;j<3;j++)closest[j]=std::max(lo[j],std::min(hi[j],origin[j]));if(length(closest-origin)>radius)continue;
        const auto line=trace(origin,center(e),s.owner.edict);if(line.fraction<.999&&line.edict!=e.edict)continue;result.push_back(e);
    }return result;
}
void knock(const Entity& e,Vec3 origin,double side,double up){if(!valid(e)||!(read<unsigned>(e.pev+0x1c8)&0x28))return;auto d=center(e)-origin;d.z=0;d=unit(d);auto v=vec(e.pev+0x20);if(side>=0){v.x=static_cast<float>(d.x*side);v.y=static_cast<float>(d.y*side);}if(up>=0)v.z=static_cast<float>(up);putVec(e.pev+0x20,v);}
void slow(const Entity& e,const State& s,double t){const auto duration=value(s,"DiveExploFixedMoveTime",true);if(duration<=0||!hostile(e,s.owner))return;auto i=statuses.find(e.object);if(i==statuses.end()||!valid(i->second.e))i=statuses.insert_or_assign(e.object,Status{e,t,t,0,0}).first;auto& x=i->second;x.until=std::max(x.until,t+duration);x.speed=value(s,"DiveExploFixedMoveSpeed",true);x.gravity=value(s,"DiveExploFixedMoveGravity",true);}
int area(State& s,Vec3 origin,double radius,double amount,double side=-1,double up=-1,bool withSlow=false){int hit=0;for(const auto& e:areaTargets(s,origin,radius))if(validState(s)&&damage(e,s.owner,amount).delta>0){++hit;if(hostile(e,s.owner)){knock(e,origin,side,up);if(withSlow)slow(e,s,now());}}return hit;}
void enqueue(const State& s,const std::string& kind,Vec3 origin,double scale){if(kind=="pulse"||kind=="jumpfx"||kind=="landing"){const auto line=trace(origin+Vec3{0,0,4},origin+Vec3{0,0,-100},s.owner.edict);if(line.fraction<.999)origin=line.end+Vec3{0,0,2};}queue.push_back({"fx/"+std::to_string(++visualId),kind,s.id,origin,scale});}
void sound(const State& s,const std::string& kind,Vec3 origin){if(!active||stopping)return;if(!World::ready())throw std::runtime_error("Leap sound before precache");const auto key=kind=="landing"?kind:std::to_string(s.id)+"/"+kind;if(kind=="fire"){const auto& p=profiles()["leapstrike"]["sound_assets"][key]["path"].text();engine.emitSound(s.owner.edict,1,p.c_str(),1,.8f,0,100);}else World::ambient("leapstrike",key,origin);}
void spawn(const std::shared_ptr<State>& s,const Shot& shot,double t){
    const bool boosted=s->phase==Phase::Glide;projectiles.push_back({++projectileId,s,shot.source,shot.direction,value(*s,"ProjSpeed"),t,t,t+value(*s,"ProjDuration"),boosted});
    area(*s,vec(s->owner.pev+8),value(*s,"AModeExploRadius")*Meter,value(*s,"AModeExploDamage",true)*(boosted?value(*s,"AModeExploDamageBModeRatio"):1));enqueue(*s,"pulse",vec(s->owner.pev+8),value(*s,"AModeExploEffectScale"));
}
void projectileFrame(double t){
    for(std::size_t i=projectiles.size();i-->0;){auto& p=projectiles[i];const auto s=p.state;
        if(!validState(*s)){projectiles.erase(projectiles.begin()+i);continue;}if(t<p.last){if(p.last-t>1)projectiles.erase(projectiles.begin()+i);continue;}
        const auto endTime=std::min(t,p.expires),dt=endTime-p.last;if(dt<=0){if(t>=p.expires)projectiles.erase(projectiles.begin()+i);continue;}p.last=endTime;
        const auto end=p.position+p.direction*(p.speed*dt);const auto line=trace(p.position,end,s->owner.edict);bool collided=false;
        if(line.fraction<.999||line.startSolid){const auto origin=line.end-p.direction;area(*s,origin,value(*s,"ProjExploRadius")*Meter,value(*s,"ProjExploDamage",true)*(p.boosted?value(*s,"ProjExploDamageBModeRatio"):1));enqueue(*s,"hit",origin,1);sound(*s,"impact",origin);collided=true;}else p.position=end;
        if(collided||t>=p.expires)projectiles.erase(projectiles.begin()+i);
    }
}
void launch(State& s,double t){
    if(read<int>(s.weapon+0x168)<100||s.phase!=Phase::Idle||(read<unsigned>(s.owner.object+0x3408)&0x2000000))return;
    s.module=moduleGet(s.owner.object,25);if(!ownedModule(s)||read<int>(s.module+0x10)!=0)throw std::runtime_error("Leap module ownership/state");
    const auto m=s.module;write(m+0xc,static_cast<unsigned char>(1));write(m+0x18,s.property);write(m+0x2c,static_cast<float>(value(s,"GlidingGravity")));write(m+0x30,static_cast<float>(value(s,"GlidingSpeed")));write(m+0x38,static_cast<float>(value(s,"DiveSpeed")));
    write(m+0x10,1);write(m+0x14,1);write(m+0x1c,static_cast<float>(t));auto a=vec(s.owner.pev+0x74);a.x=static_cast<float>(std::min(double(a.x),-value(s,"LeapMinPitch")));putVec(s.owner.pev+0x20,direction(a)*value(s,"LeapSpeed",true));write(s.owner.pev+0x1c8,read<unsigned>(s.owner.pev+0x1c8)&~0x200u);
    setGauge(s,0);setState(s,1);s.specialCharged=0;s.phase=Phase::Leap;s.phaseUntil=t+value(s,"LeapTime",true);const auto hit=area(s,vec(s.owner.pev+8),value(s,"LeapExploRadius")*Meter,value(s,"LeapExploDamage",true),value(s,"LeapExploKnockbackSide",true),value(s,"LeapExploKnockbackUp",true));gain(s,hit*value(s,"LeapAttackChargeRatio"),true);enqueue(s,"jumpfx",vec(s.owner.pev+8),1);
}
void glide(State& s,double t){if(!ownedModule(s))throw std::runtime_error("Lost leap module");write(s.module+0x10,2);write(s.module+0x14,3);write(s.module+0x1c,static_cast<float>(t));s.phase=Phase::Glide;s.phaseUntil=t+value(s,"GlidingTime",true);setState(s,2);}
void dive(State& s,double t){if(!ownedModule(s))throw std::runtime_error("Lost leap module");write(s.module+0x10,3);write(s.module+0x14,5);write(s.module+0x1c,static_cast<float>(t));putVec(s.owner.pev+0x20,{0,0,static_cast<float>(-value(s,"DiveSpeed"))});s.diveBorn=t;s.diveOrigin=vec(s.owner.pev+8);s.phase=Phase::Dive;s.phaseUntil=t+value(s,"DiveTime");s.godUntil=t+value(s,"DiveGodTime",true);setState(s,3);}
void landing(State& s,double t){const auto o=vec(s.owner.pev+8);const auto hit=area(s,o,value(s,"DiveExploDamageRadius")*Meter,value(s,"DiveExploDamage",true));for(const auto& e:areaTargets(s,o,value(s,"DiveExploKnockbackRadius")*Meter))if(hostile(e,s.owner)){knock(e,o,value(s,"DiveExploKnockbackSide",true),value(s,"DiveExploKnockbackUp",true));slow(e,s,t);}gain(s,hit*value(s,"DiveAttackChargeRatio"),true);enqueue(s,"landing",o,1);sound(s,"landing",o);finish(s,true);}
void weaponFrame(State& s,double t){
    if(!validState(s)){if(s.module)finish(s);return;}const bool held=isActive(s);const auto buttons=read<unsigned>(s.owner.object+0xec4);const bool pressed=held&&(buttons&0x800)&&!(s.previousButtons&0x800);s.previousButtons=buttons;
    if(t<s.last){if(s.last-t<=1)return;finish(s);s.nextCharge=t+value(s,"GaugeChargeInterval",true);}s.last=t;
    if(read<float>(s.owner.pev+0x178)<=0||read<int>(s.owner.pev+0x194)!=0){if(s.module)finish(s);return;}
    if(!held&&s.phase==Phase::Idle){s.nextCharge=t+value(s,"GaugeChargeInterval",true);return;}
    if(t>=s.nextCharge){const auto interval=value(s,"GaugeChargeInterval",true);if(!(interval>0))throw std::runtime_error("Leap charge interval");const auto due=std::floor((t-s.nextCharge)/interval+1e-6)+1;gain(s,std::min(10.,due));s.nextCharge+=due*interval;}
    if(s.phase==Phase::Idle){if(pressed)launch(s,t);return;}if(!ownedModule(s))throw std::runtime_error("Lost active leap module");const auto nativeState=read<int>(s.module+0x10),nativePhase=read<int>(s.module+0x14);
    if(s.phase==Phase::Leap&&(t>=s.phaseUntil||nativeState==2))glide(s,t);else if(s.phase==Phase::Glide&&(pressed||t>=s.phaseUntil))dive(s,t);else if(s.phase==Phase::Dive){if(nativePhase==6||(read<unsigned>(s.owner.pev+0x1c8)&0x200))landing(s,t);else if(t>=s.phaseUntil)finish(s);}
}
void suppressFall(){const auto pm=ptr(at(0x21d5f70));if(!pm)return;const auto index=read<int>(pm);if(index<0||index>=32)return;const auto fall=read<float>(pm+0xac);if(!std::isfinite(fall)||fall<=0)return;for(const auto& [w,s]:states)if(s->phase==Phase::Dive&&validState(*s)&&ownedModule(*s)&&engine.byIndex(index+1)==s->owner.edict){write(pm+0xac,0.f);return;}}
void network(double t){
    if(!World::ready()){queue.clear();return;}std::set<std::string> wanted;
    for(const auto& p:projectiles){const auto key="projectile/"+std::to_string(p.id);wanted.insert(key);auto v=visuals.ensure(key,p.state->id,"projectile",p.position,1,t);if(v){v->until=t+.25;v->sticky=v->loop=v->manualFrame=true;const auto f=std::floor(std::max(0.,t-p.born)*30),first=std::floor(value(*p.state,"ProjRenderStartFrame"));v->options.frame=static_cast<float>(std::fmod(first+f,(*v->metadata)["frames"].number()));v->options.alpha=static_cast<float>(std::min(value(*p.state,"ProjMaxRenderAmount"),f*value(*p.state,"ProjAddedRenderAmountPerFrame")));}}
    for(const auto& [w,s]:states)if(validState(*s)&&read<float>(s->owner.pev+0x178)>0&&s->module&&s->phase!=Phase::Idle){const std::string kind=s->phase==Phase::Leap?"jumpdash":s->phase==Phase::Glide?"flying":"fall",key="flight/"+std::to_string(w)+"/"+kind;wanted.insert(key);auto v=visuals.ensure(key,s->id,kind,vec(s->owner.pev+8),1,t);if(v){v->until=t+.25;v->sticky=v->loop=true;}}
    for(const auto& f:queue)visuals.ensure(f.key,f.id,f.kind,f.position,f.scale,t);queue.clear();visuals.tick(t,wanted);
}
void cleanup(){projectiles.clear();for(auto& [w,s]:states){finish(*s);if(validState(*s)){if(s->gaugeLast&&read<int>(w+0x168)==*s->gaugeLast)write(w+0x168,s->gaugeOld);if(s->stateLast&&read<int>(w+0x1c0)==*s->stateLast)write(w+0x1c0,s->stateOld);}}for(const auto& [o,x]:statuses)StatusBroker::release("leapstrike",x.e);statuses.clear();visuals.clear();queue.clear();cleaned=true;}
void tick(){if(cleaned)return;if(!active||stopping){cleanup();return;}const auto t=now();if(t==lastTick)return;lastTick=t;for(auto& [w,s]:states)weaponFrame(*s,t);projectileFrame(t);network(t);for(auto i=statuses.begin();i!=statuses.end();){const auto& s=i->second;if(!valid(s.e)||t<s.from||t>=s.until||read<float>(s.e.pev+0x178)<=0){StatusBroker::release("leapstrike",s.e);i=statuses.erase(i);}else{StatusBroker::set("leapstrike",s.e,0x240,s.speed);StatusBroker::set("leapstrike",s.e,0x12c,s.gravity);++i;}}}
}
bool leapstrikeClean(){return leap::cleaned;}
void installLeapstrike(){
    using namespace leap;moduleGet=native<decltype(moduleGet)>(at(0x87e6e0));moduleDeactivate=native<decltype(moduleDeactivate)>(at(0x87f5a0));
    World::onMapReset([]{visuals.forget();queue.clear();projectiles.clear();states.clear();statuses.clear();lastTick=std::numeric_limits<double>::quiet_NaN();});
    Hooks::attach(at(0xe660c0),"leapstrike",[](Invocation& v){
        auto& shot=v.createState<std::shared_ptr<Shot>>();if(!active||stopping||cleaned)return;const auto s=acquire(v.registers->ecx);if(!s||!isActive(*s))return;
        shot=std::make_shared<Shot>();shot->state=s;shot->clip=read<int>(s->weapon+0x160);shot->source=vec(s->owner.pev+8)+vec(s->owner.pev+0x198);shot->direction=direction(vec(s->owner.pev+0x74)+vec(s->owner.pev+0x68));shots.push_back(shot);
    },[](Invocation& v){const auto shot=v.state<std::shared_ptr<Shot>>();if(!shot)return;if(shots.empty()||shots.back()!=shot)throw std::runtime_error("Leap shot stack");shots.pop_back();if(active&&!stopping&&validState(*shot->state)&&shot->clip-read<int>(shot->state->weapon+0x160)==1&&!shot->nativeAttack){spawn(shot->state,*shot,now());sound(*shot->state,"fire",shot->source);}});
    Hooks::attach(at(0x1528830),"leapstrike",[](Invocation& v){if(active&&!stopping&&!cleaned)acquire(v.registers->ecx);});
    for(const Address rva:{0x13fc970u,0x13e6830u})Hooks::attach(at(rva),"leapstrike",[](Invocation&){if(!shots.empty())shots.back()->nativeAttack=true;});
    Hooks::attach(at(0x7d8b90),"leapstrike",[](Invocation& v){auto& m=v.createState<Move>();if(cleaned)return;for(const auto& [w,s]:states)if(s->module==v.registers->ecx&&s->module){m.state=s;m.pm=v.argument<Address>(0);break;}},[](Invocation& v){const auto m=v.state<Move>();if(m.state&&active&&!stopping&&validState(*m.state)&&m.state->phase==Phase::Dive&&ownedModule(*m.state)&&read<int>(m.state->module+0x10)==3)putVec(m.pm+0x5c,{0,0,static_cast<float>(-value(*m.state,"DiveSpeed"))});});
    Hooks::attach(at(0x14eff20),"leapstrike",{},[](Invocation&){if(active&&!stopping&&!cleaned)suppressFall();});
    Hooks::attach(at(0x1524130),"leapstrike",[](Invocation& v){if(!active||stopping||v.returnAddress!=at(0xe65f0d)||!weaponIs(v.registers->ecx,"leapstrike")||v.argument<int>(2)!=3)return;const auto delay=v.argument<float>(5);if(!std::isfinite(delay)||delay<0||delay>20)throw std::runtime_error("Leap draw delay");v.argument<float>(5,std::max(delay,1.f));});
    Hooks::attach(at(0x13f8a80),"leapstrike",{},[](Invocation&){tick();});
    DamageBroker::add("leapstrike",[](const DamageContext& c){if(!active||stopping)return false;for(const auto& [w,s]:states)if(s->owner.object==c.victim&&validState(*s)&&s->godUntil>now())return true;return false;});
}
}
