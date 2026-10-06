#include "families.hpp"
#include "hooks.hpp"
#include "brokers.hpp"
#include "world.hpp"
#include "combat.hpp"
#include <optional>

namespace csnz {
namespace soul {
struct Orb {Vec3 position,direction;std::optional<CombatTarget> target;double last{},until{};};
struct State : Entity {
    Address weapon{};Entity we;unsigned id{},order{};Properties props;int mode{};
    double held{},cooldown{},nextBeam{},nextOrb{},nextMode{},lastFrame{},nextConsume{},bEnergy{};
    std::optional<double> nextCharge;unsigned hits{};bool helix=false,pendingArmor=false;std::optional<bool> wasActive;
    std::vector<Orb> orbs;int energyOriginal{};std::optional<int> energyLast;
};
struct Defense : Entity {double cooldown{},until{};};
struct Status {Entity e;double until{},speed{},gravity{};};
struct Pull {Entity e;Vec3 original,start,end;std::optional<Vec3> last;double until{},duration{};};
struct Queued {unsigned id;std::string type;Vec3 position;double radius;};
struct Before {std::shared_ptr<State> state;Aim aim;std::map<Address,std::pair<CombatTarget,double>> health;int clip;};
struct ArmorToken {std::shared_ptr<State> state;std::shared_ptr<Defense> defense;};
std::map<Address,std::shared_ptr<State>> states;std::map<Address,std::shared_ptr<Defense>> defenses;
std::map<Address,Status> statuses;std::map<Address,Pull> pulls;std::map<Address,Properties> properties;
std::vector<Queued> queue;EffectBank visuals{"soul",128};double lastTime=-1;bool cleaned=false;unsigned stateOrder=0,visualId=0;
void(__thiscall* primaryNative)(Address){};void(__thiscall* tail)(Address,int){};
void(__thiscall* velocity)(Address,float,float,float,char,int){};
double prop(const State& s,const std::string& name){return property(s.props,name);}
double value(const State& s,const std::string& name){return prop(s,name+(s.mode==1?"Zombie":s.mode==2?"Scenario":""));}
bool isSoul(Address w){return weaponIs(w,"soul")&&ptr(w+0xd0)!=0;}
bool validState(const State& s){return isSoul(s.weapon)&&valid(s)&&valid(s.we)&&ptr(s.weapon+0xd0)==s.object;}
bool isActive(Address w){if(!isSoul(w))return false;const auto o=ptr(w+0xd0),p=ptr(o+8);return ptr(o+0x1024)==w&&read<float>(p+0x178)>0&&read<int>(p+0x194)==0;}
std::shared_ptr<Defense> defense(const State& s){auto i=defenses.find(s.object);if(i!=defenses.end()&&valid(*i->second))return i->second;auto d=std::make_shared<Defense>();static_cast<Entity&>(*d)=s;defenses[s.object]=d;return d;}
std::shared_ptr<State> state(Address w){
    auto i=states.find(w);if(i!=states.end()&&validState(*i->second))return i->second;auto s=std::make_shared<State>();static_cast<Entity&>(*s)=entity(ptr(w+0xd0));s->weapon=w;s->we=entity(w);s->id=read<unsigned>(w+0xe4);s->order=++stateOrder;
    const auto base=ptr(w+0x204);auto p=properties.find(base);if(p==properties.end()){
        auto props=readProperties(w,0x1960);for(const auto& [name,v]:profiles()["soul"]["property_contract"][std::to_string(s->id)].object())if(property(props,name)!=v.number())throw std::runtime_error("Soul property contract: "+name);p=properties.emplace(base,std::move(props)).first;
    }s->props=p->second;s->mode=mode();s->lastFrame=now();s->nextCharge=s->lastFrame;s->energyOriginal=read<int>(w+0x168);states[w]=s;defense(*s);return s;
}
int energy(const State& s){const auto n=read<int>(s.weapon+0x168);if(n<0||n>prop(s,"CModeChargeMax"))throw std::runtime_error("Soul energy range");return n;}
void setEnergy(State& s,double n){const auto v=static_cast<int>(std::floor(clamp(n,0,prop(s,"CModeChargeMax"))));write(s.weapon+0x168,v);s.energyLast=v;}
void event(const State& s,int code,double f1=0,double f2=0,int i1=0,int b1=0){if(!validState(s))return;auto a=vec(s.pev+0x74);a.x/=-3;const auto origin=vec(s.pev+8);engine.playback(0,s.edict,read<unsigned short>(s.weapon+0x218),0,&origin,&a,static_cast<float>(f1),static_cast<float>(f2),i1,0,b1,code);}
void animate(const State& s,int sequence,double duration){if(!isActive(s.weapon))return;engine.animation(s.weapon,sequence,0);idleTimer(s.weapon,duration);}
int normalAppearance(const State& s){return energy(s)>=prop(s,"CModeCanChangeGauge")?2:0;}
void syncAppearance(State& s){if(s.helix||!isActive(s.weapon))return;const auto desired=normalAppearance(s);if(read<int>(ptr(s.weapon+8)+0x120)==desired&&read<int>(s.weapon+0x1c0)==0)return;tail(s.weapon,desired);}
bool hit(State& s,const CombatTarget& e,double amount){if(!valid(e.e)||!(amount>0&&amount<200000))return false;const auto result=damage(e.e,s,amount);if(result.delta<=0)return false;if(hostile(e.e,s)&&result.killed)setEnergy(s,energy(s)+value(s,"CModeChargeAmountWhenKill"));return true;}
void slow(State& s,const CombatTarget& e,const std::string& prefix){
    if(!hostile(e.e,s))return;const auto duration=prop(s,prefix+"FixedMove"+(prefix=="KillEvasion"?"Duration":"Time")),speed=prop(s,prefix+"FixedMoveSpeed"),gravity=prop(s,prefix+"FixedMoveGravity");if(duration<=0)return;
    auto i=statuses.find(e.e.object);if(i==statuses.end()||!valid(i->second.e))i=statuses.insert_or_assign(e.e.object,Status{e.e,0,speed,gravity}).first;auto& x=i->second;x.until=std::max(x.until,now()+duration);x.speed=std::min(x.speed,speed);x.gravity=gravity;
}
void knock(const State& s,const CombatTarget& e,double side,double up,Vec3 origin){if(!hostile(e.e,s))return;const auto d=unit(Vec3{e.center.x-origin.x,e.center.y-origin.y,0},{1,0,0});putVec(e.e.pev+0x20,{static_cast<float>(d.x*side),static_cast<float>(d.y*side),static_cast<float>(up)});}
int area(State& s,Vec3 origin,double radius,double amount,const std::string& knockPrefix={},const std::string& slowPrefix={}){
    int n=0;for(const auto& e:combatSphere(s,origin,radius))if(length(closest(e.e,origin)-origin)<=radius&&visible(s,origin,e)&&hit(s,e,amount)){if(hostile(e.e,s))++n;if(!knockPrefix.empty())knock(s,e,value(s,knockPrefix+"KnockbackSide"),value(s,knockPrefix+"KnockbackUp"),origin);if(!slowPrefix.empty())slow(s,e,slowPrefix);}return n;
}
void startPull(const State& s,const CombatTarget& e,Vec3 start,Vec3 end){const auto duration=value(s,"CModePullingDuration");if(!(duration>0)||!hostile(e.e,s)||!valid(e.e))return;auto i=pulls.find(e.e.object);if(i==pulls.end()||!valid(i->second.e)){Pull p;p.e=e.e;p.original=vec(e.e.pev+0x20);i=pulls.insert_or_assign(e.e.object,p).first;}auto& x=i->second;x.until=now()+duration;x.duration=duration;x.start=start;x.end=end;}
void restorePull(const Pull& x){if(!x.last||!valid(x.e))return;for(unsigned i=0;i<3;i++)if(read<float>(x.e.pev+0x20+i*4)==(*x.last)[i])write(x.e.pev+0x20+i*4,x.original[i]);}
void updatePulls(double t){for(auto i=pulls.begin();i!=pulls.end();){auto& x=i->second;if(!valid(x.e)){i=pulls.erase(i);continue;}if(t>=x.until||read<float>(x.e.pev+0x178)<=0){restorePull(x);i=pulls.erase(i);continue;}const auto axis=unit(x.end-x.start,{1,0,0}),position=center(x.e),v=axis*dot(vec(x.e.pev+0x20),axis)+(segmentPoint(position,x.start,x.end)-position)*(1/x.duration);if(!finite(v))throw std::runtime_error("Soul pull velocity");velocity(x.e.object,v.x,v.y,v.z,1,1);x.last=vec(x.e.pev+0x20);++i;}}
Before normalBefore(Address w){Before x;x.state=state(w);x.aim=combatAim(*x.state);x.clip=read<int>(w+0x160);for(const auto& e:combatSphere(*x.state,x.aim.source,prop(*x.state,"AModeDistance")))x.health[e.e.object]={e,read<float>(e.e.pev+0x178)};return x;}
void normalAfter(Before& x){
    auto& s=*x.state;if(read<int>(s.weapon+0x160)!=x.clip-1)return;const auto old=s.bEnergy;s.bEnergy=std::min(prop(s,"BModeMaxEnergy"),s.bEnergy+prop(s,"BModeChargeEnergy"));if(old!=s.bEnergy)event(s,7,0,0,static_cast<int>(s.bEnergy));
    const auto begin=ptr(s.object+0x84),end=ptr(s.object+0x88);if(end<begin||end-begin>28*128||(end-begin)%28)throw std::runtime_error("Soul native impact vector");std::set<Address> seen;std::vector<CombatTarget> hits;
    for(auto q=begin;q<end;q+=28){const auto key=ptr(q);const auto i=x.health.find(key);if(i!=x.health.end()&&valid(i->second.first.e)&&read<float>(i->second.first.e.pev+0x178)<i->second.second&&seen.insert(key).second)hits.push_back(i->second.first);}
    for(const auto& e:hits)if(hostile(e.e,s)&&read<float>(e.e.pev+0x178)<=0)setEnergy(s,energy(s)+value(s,"CModeChargeAmountWhenKill"));
    const auto radius=prop(s,"AModeFanAttackMeter")*Meter,angle=prop(s,"AModeFanAttackAngle");bool fanEnemy=false;
    for(const auto& e:combatSphere(s,x.aim.source,radius))if(inFan(e.center,x.aim.source,x.aim.direction,radius,angle)&&visible(s,x.aim.source,e)&&hit(s,e,value(s,"AModeFanAttackDamage"))&&hostile(e.e,s))fanEnemy=true;
    if(fanEnemy||std::any_of(hits.begin(),hits.end(),[&](const auto& e){return hostile(e.e,s);}))++s.hits;
}
void armorHeal(State& s,int targets){if(s.id!=692||targets<=0)return;const auto amount=std::min(targets*value(s,"KillEvasionAttackHeal"),value(s,"KillEvasionAttackHealMax"));if(!(amount>0))return;if(ptr(ptr(s.object)+80)!=at(0x14dd550))throw std::runtime_error("Soul TakeHealth vslot");engine.heal(s.object,s.pev,static_cast<float>(amount),0,reinterpret_cast<Address>(damageInfo.data()));}
bool launchOrb(State& s,std::optional<CombatTarget> target={}){
    const auto t=now(),cost=prop(s,"BModeMakeProjEnergy");if(s.helix||t<s.nextOrb||s.bEnergy<cost)return false;const auto a=combatAim(s);const auto d=target?unit(target->center-a.source,{1,0,0}):a.direction;
    s.bEnergy-=cost;s.nextOrb=t+prop(s,"BModeFireProjCycleTime");s.nextMode=t+prop(s,"BModeToCModeDelay");attackTimers(s.weapon,prop(s,"BModeToAModeDelay"),prop(s,"BModeToBModeDelay"));event(s,2,s.bEnergy,0,0,1);if(!target)animate(s,4,1);
    s.orbs.push_back({a.source,d,target,t,t+prop(s,"AModeDistance")/prop(s,"BModeProjSpeed")});return true;
}
void fireBeam(State& s){
    const auto t=now();if(!s.helix||t<s.nextBeam||energy(s)<=0)return;const auto a=combatAim(s);const auto range=prop(s,"CModeDistance")*Meter,radius=prop(s,"CModeRadius")*Meter;
    const auto end=trace(a.source,a.source+a.direction*range,s.edict,1).end,mid=(a.source+end)*.5;
    for(const auto& e:combatSphere(s,mid,length(end-a.source)*.5+radius)){const auto q=segmentPoint(e.center,a.source,end);if(length(closest(e.e,q)-q)<=radius&&trace(a.source,e.center,s.edict,1).fraction>=.999&&hit(s,e,value(s,"CModeDamage"))){slow(s,e,"CMode");startPull(s,e,a.source,end);}}
    idleTimer(s.weapon,1);s.nextBeam=t+prop(s,"CModeCycleTime");attackTimers(s.weapon,prop(s,"CModeCycleTime"),prop(s,"CModeCycleTime"));event(s,3,radius,length(end-a.source));
}
void startHelix(State& s){
    const auto t=now();const auto buttons=read<unsigned>(s.pev+0x1a4);if(s.helix||(buttons&0x801)!=0x801||energy(s)<prop(s,"CModeCanChangeGauge")||t<s.nextMode)return;
    setEnergy(s,energy(s)-prop(s,"CModeChangeUseGauge"));s.helix=true;s.nextConsume=t+prop(s,"CModeConsumeInterval");s.nextBeam=t;s.nextCharge.reset();tail(s.weapon,1);event(s,0,0,0,0,1);event(s,4);
    area(s,vec(s.pev+8),prop(s,"CModeActivatedAttackRadius")*Meter,value(s,"CModeActivatedAttackDamage"),"CModeActivatedAttack");fireBeam(s);
}
void endHelix(State& s,bool energyEmpty=false){if(!s.helix)return;s.helix=false;s.nextCharge=now()+prop(s,"CModeChargeDelay");if(validState(s)&&read<int>(s.weapon+0x1c0)==1)tail(s.weapon,isActive(s.weapon)?normalAppearance(s):0);if(energyEmpty&&isActive(s.weapon)){animate(s,9,20./30);attackTimers(s.weapon,20./30,20./30);}event(s,5,isActive(s.weapon)?s.bEnergy:0);}
void input(Address w){if(!isActive(w))return;auto s=state(w);const auto buttons=read<unsigned>(s->pev+0x1a4);if(s->helix){if(buttons&1)fireBeam(*s);return;}if((buttons&0x801)==0x801){startHelix(*s);return;}if(buttons&0x800)launchOrb(*s);}
void cleanup(){visuals.clear();queue.clear();for(const auto& [o,p]:pulls)restorePull(p);pulls.clear();for(const auto& [o,s]:statuses)StatusBroker::release("soul",s.e);statuses.clear();defenses.clear();for(auto& [w,s]:states)if(validState(*s)){endHelix(*s);event(*s,6);event(*s,0);s->orbs.clear();s->pendingArmor=false;if(s->energyLast&&energy(*s)==*s->energyLast)write(w+0x168,s->energyOriginal);}cleaned=stopping||!active;}
void tick(){
    const auto t=now();if(lastTime>=0&&t<lastTime){cleanup();states.clear();properties.clear();}lastTime=t;
    for(auto i=defenses.begin();i!=defenses.end();)if(!alive(*i->second))i=defenses.erase(i);else ++i;
    for(auto i=states.begin();i!=states.end();){auto s=i->second;if(!validState(*s)){i=states.erase(i);continue;}if(!alive(*s)){endHelix(*s);event(*s,6);i=states.erase(i);continue;}
        const auto dt=clamp(t-s->lastFrame,0,.1);s->lastFrame=t;const bool held=isActive(s->weapon);s->held+=dt;if(!held&&s->helix)endHelix(*s);
        if(s->helix){if(t>=s->nextConsume){const auto interval=prop(*s,"CModeConsumeInterval");const auto n=std::min(100.,1+std::floor((t-s->nextConsume)/interval));setEnergy(*s,energy(*s)-n);s->nextConsume+=n*interval;if(energy(*s)<=0)endHelix(*s,true);}}
        else if(held&&s->nextCharge&&t>=*s->nextCharge){setEnergy(*s,energy(*s)+value(*s,"CModeChargeAmount"));s->nextCharge=t+value(*s,"CModeChargeInterval");}
        if(held&&!s->helix)syncAppearance(*s);if(!s->wasActive||held!=*s->wasActive){if(held){event(*s,5,s->bEnergy);event(*s,7,0,0,static_cast<int>(s->bEnergy));}else event(*s,6);s->wasActive=held;}
        if(held&&!s->helix&&s->bEnergy>=prop(*s,"BModeMakeProjEnergy")&&t>=s->nextOrb){const auto origin=combatAim(*s).source;auto nearbyTargets=combatSphere(*s,origin,prop(*s,"BModeFindTargetRadius")*Meter);nearbyTargets.erase(std::remove_if(nearbyTargets.begin(),nearbyTargets.end(),[&](const auto& e){return !hostile(e.e,*s)||!visible(*s,origin,e);}),nearbyTargets.end());std::stable_sort(nearbyTargets.begin(),nearbyTargets.end(),[&](const auto& a,const auto& b){return length(a.center-origin)<length(b.center-origin);});if(!nearbyTargets.empty())launchOrb(*s,nearbyTargets.front());}
        for(auto o=s->orbs.begin();o!=s->orbs.end();){if(t<o->last){if(o->last-t>1)o=s->orbs.erase(o);else ++o;continue;}if(t>=o->until){o=s->orbs.erase(o);continue;}const auto step=clamp(t-o->last,0,.1);
            const bool tracking=o->target&&valid(o->target->e)&&read<float>(o->target->e.pev+0x178)>0&&hostile(o->target->e,*s);if(tracking){o->target->center=center(o->target->e);const auto toward=o->target->center-o->position;if(length(toward)>1e-5)o->direction=unit(toward,{1,0,0});}
            const auto next=o->position+o->direction*(prop(*s,"BModeProjSpeed")*step);const auto tr=trace(o->position,next,s->edict);o->last=t;o->position=tr.end;
            const bool proximity=tracking&&length(closest(o->target->e,o->position)-o->position)<=prop(*s,"BModeProjExpDist")*Meter;
            if(tr.fraction<.999||proximity){const auto position=o->position-o->direction;area(*s,position,prop(*s,"BModeProjExploRadius")*Meter,value(*s,"BModeProjExploDamage"),"BModeProjExplo");queue.push_back({s->id,"impact",position,prop(*s,"BModeProjExploRadius")*Meter});World::ambient("soul","impact",position);o=s->orbs.erase(o);}else ++o;
        }
        if(s->pendingArmor){s->pendingArmor=false;const auto origin=vec(s->pev+8);const auto hits=area(*s,origin,prop(*s,"KillEvasionAttackRadius")*Meter,value(*s,"KillEvasionDamage"),"KillEvasion","KillEvasion");armorHeal(*s,hits);queue.push_back({s->id,"armor",origin,prop(*s,"KillEvasionAttackRadius")*Meter});}++i;
    }
    for(auto i=statuses.begin();i!=statuses.end();){const auto& x=i->second;if(!valid(x.e)){i=statuses.erase(i);continue;}if(t>=x.until||read<float>(x.e.pev+0x178)<=0){StatusBroker::release("soul",x.e);i=statuses.erase(i);continue;}if(x.speed>=0){StatusBroker::set("soul",x.e,0x240,x.speed);auto v=vec(x.e.pev+0x20);const auto n=std::hypot(v.x,v.y);if(n>x.speed){v.x=static_cast<float>(v.x*x.speed/n);v.y=static_cast<float>(v.y*x.speed/n);putVec(x.e.pev+0x20,v);}}if(std::isfinite(x.gravity)&&x.gravity>=0)StatusBroker::set("soul",x.e,0x12c,x.gravity);++i;}updatePulls(t);
}
void network(){if(!World::ready()){queue.clear();return;}const auto t=now();const auto count=std::min<std::size_t>(128,queue.size());for(std::size_t i=0;i<count;i++){const auto& f=queue[i];const auto& meta=profiles()["soul"]["visual_assets"][std::to_string(f.id)+"/"+f.type];const auto scale=meta["type"].text()=="sprite"?2*f.radius/meta["width"].number():1;visuals.ensure("fx/"+std::to_string(++visualId),f.id,f.type,f.position,scale,t);}queue.erase(queue.begin(),queue.begin()+count);visuals.tick(t);}
bool armorReady(const State& s,const Defense& d,double t){return s.mode!=0&&t>=d.cooldown&&s.held>=value(s,"KillEvasionWeaponCooltime")&&s.hits>=value(s,"KillEvasionShotCount");}
Address armorWeapon(Address victim){
    std::vector<std::shared_ptr<State>> owned,ready;const auto t=now();for(const auto& [w,s]:states)if(s->object==victim&&validState(*s)){owned.push_back(s);const auto d=defenses.find(victim);if(d!=defenses.end()&&valid(*d->second)&&armorReady(*s,*d->second,t))ready.push_back(s);}if(owned.empty())return 0;
    auto& list=ready.empty()?owned:ready;const auto held=ptr(victim+0x1024);for(const auto& s:list)if(s->weapon==held)return s->weapon;std::stable_sort(list.begin(),list.end(),[](const auto& a,const auto& b){return a->held!=b->held?a->held>b->held:a->order<b->order;});return list.front()->weapon;
}
void __fastcall primary(Address w,void*){std::lock_guard<std::recursive_mutex> lock(gameplayMutex);if(!active||stopping||!isActive(w)){primaryNative(w);return;}guarded("soul.primary",[&]{auto s=state(w);if(s->helix){fireBeam(*s);return;}auto before=normalBefore(w);primaryNative(w);normalAfter(before);});}
using Assists=std::vector<std::pair<std::shared_ptr<State>,int>>;
}
bool soulClean(){return soul::cleaned;}
void installSoul(){
    using namespace soul;tail=native<decltype(tail)>(at(0xf6bd60));velocity=native<decltype(velocity)>(at(0x13e73c0));
    World::onMapReset([]{visuals.forget();queue.clear();states.clear();statuses.clear();defenses.clear();pulls.clear();properties.clear();lastTime=-1;});
    Hooks::replace(at(0xf6ba10),reinterpret_cast<void*>(&primary),reinterpret_cast<void**>(&primaryNative),"soul");
    Hooks::attach(at(0x14b3be0),"soul",[](Invocation& v){auto& w=v.createState<Address>(0);const auto a=v.argument<Address>(0);if(active&&!stopping&&v.returnAddress==at(0x1528e67)&&isActive(a)&&ptr(a+0xd0)==v.registers->ecx)w=a;},[](Invocation& v){const auto w=v.state<Address>();if(w&&active&&!stopping)input(w);});
    Hooks::attach(at(0x15225f0),"soul",[](Invocation& v){v.createState<Address>(v.registers->ecx);},[](Invocation& v){const auto w=v.state<Address>();if(active&&!stopping&&v.registers->eax&&isSoul(w))state(w);});
    Hooks::attach(at(0x13f8a80),"soul",[](Invocation&){if(!active||stopping){if(!cleaned)cleanup();return;}tick();network();});
    Hooks::attach(at(0x14c28e0),"soul",[](Invocation& v){auto& a=v.createState<Assists>();const auto id=v.argument<unsigned>(2),p=v.registers->ecx;if(!active||stopping||(id!=568&&id!=692)||!p||ptr(ptr(p)+0x218)!=at(0x14c28e0))return;for(const auto& [w,s]:states)if(s->id==id&&s->object==p&&validState(*s))a.emplace_back(s,energy(*s));},[](Invocation& v){if(!active||stopping)return;for(const auto& [s,before]:v.state<Assists>())if(validState(*s)&&energy(*s)==before)setEnergy(*s,before+value(*s,"CModeChargeAmountWhenAssist"));});
    DamageBroker::add("soul",[](const DamageContext& c){const auto d=defenses.find(c.victim);return active&&!stopping&&d!=defenses.end()&&valid(*d->second)&&read<float>(d->second->pev+0x178)>0&&now()<d->second->until;});
    ArmorGate::add({"soul",[](Address victim){return active&&!stopping?armorWeapon(victim):0;},[](const ArmorContext& c)->std::any{
        if(!active||stopping)return {};const auto i=states.find(c.weapon);if(i==states.end())return {};auto s=i->second;
        if(!validState(*s)||s->object!=c.object||s->serial!=c.serial||s->we.serial!=c.weaponSerial||s->mode!=1)return {};const auto d=defenses.find(c.object);
        if(d==defenses.end()||!valid(*d->second)||c.time<d->second->cooldown||c.time<d->second->until||!armorReady(*s,*d->second,c.time))return {};return ArmorToken{s,d->second};
    },[](const ArmorContext& c,const std::any& any){const auto token=std::any_cast<ArmorToken>(any);auto& s=*token.state;auto& d=*token.defense;if(!active||stopping||!validState(s)||!valid(d))return;d.cooldown=s.cooldown=c.time+value(s,"KillEvasionPlayerCooltime");d.until=c.time+prop(s,"KillEvasionGodTime");s.held=0;s.hits=0;s.pendingArmor=true;event(s,8);}});
}
}
