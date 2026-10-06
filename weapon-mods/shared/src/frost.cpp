#include "families.hpp"
#include "hooks.hpp"
#include "brokers.hpp"
#include "world.hpp"
#include "combat.hpp"
#include "frost_policy.hpp"
#include <random>

namespace csnz {
namespace frost {
struct Orb {unsigned id{};Vec3 position,direction;double until{},last{};std::set<std::pair<Address,unsigned>> hit;};
struct State : Entity {Address weapon{};Entity we;Properties props;unsigned order{},hits{};int mode{};double held{},cooldown{},godUntil{},nextBeam{},nextShield{},nextShieldScan{},lastFrame{};bool pendingArmor=false;std::vector<Orb> orbs;};
struct Defense : Entity {double cooldown{},until{};};
struct Status {Entity e;double until{},speed{};std::optional<double> gravity;};
struct Spike {CombatTarget target;std::shared_ptr<State> state;double until{};};
struct Hud {std::shared_ptr<State> state;double expires;};
struct Queued {enum class Type {Visual,Spike,Hud} type=Type::Visual;std::string kind;Vec3 position;std::optional<Vec3> direction;double radius{};int entity{};bool on=false,clear=false,start=false;double expires{};std::shared_ptr<State> state;};
struct Before {std::shared_ptr<State> state;Aim aim;std::map<Address,std::pair<CombatTarget,double>> health;int clip;};
struct ArmorToken {std::shared_ptr<State> state;std::shared_ptr<Defense> defense;};
std::map<Address,std::shared_ptr<State>> states;std::map<Address,std::shared_ptr<Defense>> defenses;
std::map<Address,Status> statuses;std::map<std::pair<Address,Address>,Spike> spikes;std::map<Address,Properties> properties;std::map<int,Hud> hud;
std::vector<Queued> queue;EffectBank visuals{"frost",128};double lastTime=-1;bool cleaned=false;unsigned stateOrder=0,orbId=0,visualId=0;
std::mt19937 random;
void(__thiscall* primaryNative)(Address){};void(__thiscall* secondaryNative)(Address){};void(__thiscall* shieldNative)(Address){};bool(__cdecl* skipNativeSecondary)(){};
void(__cdecl* msgBegin)(int,int,Address,Address){};void(__cdecl* msgByte)(int){};void(__cdecl* msgEnd)(){};
double prop(const State& s,const std::string& name){return property(s.props,name);}
double value(const State& s,const std::string& name){return prop(s,name+(s.mode==1?"Zombie":s.mode==2?"Scenario":""));}
bool isFrost(Address w){return weaponIs(w,"frost")&&ptr(w+0xd0)!=0;}
bool validState(const State& s){return isFrost(s.weapon)&&valid(s)&&valid(s.we)&&ptr(s.weapon+0xd0)==s.object;}
bool isActive(Address w){if(!isFrost(w))return false;const auto p=ptr(w+0xd0),v=ptr(p+8);return ptr(p+0x1024)==w&&read<float>(v+0x178)>0&&read<int>(v+0x194)==0;}
std::shared_ptr<Defense> defense(const State& s){auto i=defenses.find(s.object);if(i!=defenses.end()&&valid(*i->second))return i->second;auto d=std::make_shared<Defense>();static_cast<Entity&>(*d)=s;defenses[s.object]=d;return d;}
std::shared_ptr<State> state(Address w){auto i=states.find(w);if(i!=states.end()&&validState(*i->second))return i->second;auto s=std::make_shared<State>();static_cast<Entity&>(*s)=entity(ptr(w+0xd0));s->weapon=w;s->we=entity(w);s->order=++stateOrder;const auto base=ptr(w+0x204);auto p=properties.find(base);if(p==properties.end()){auto out=readProperties(w,0x2290);for(const auto& [name,v]:profiles()["frost"]["property_contract"].object())if(property(out,name)!=v.number())throw std::runtime_error("Frost property contract: "+name);p=properties.emplace(base,std::move(out)).first;}s->props=p->second;s->mode=mode();s->lastFrame=now();states[w]=s;defense(*s);return s;}
void effect(const State& s,int code,double radius=0,double distance=0){auto a=vec(s.pev+0x74);a.x/=-3;const auto origin=vec(s.pev+8);engine.playback(0,s.edict,read<unsigned short>(s.weapon+0x218),0,&origin,&a,static_cast<float>(radius),static_cast<float>(distance),0,0,code,0);}
void frostMessage(const std::vector<unsigned char>& bytes){const auto id=read<int>(at(0x21d2a8c));if(id<=61||id>4095||bytes.empty()||bytes.size()>16)throw std::runtime_error("Frost message contract");if(read<int>(hat(0x2078f9c))!=0)throw std::runtime_error("Nested engine message");msgBegin(2,id,0,0);for(auto b:bytes)msgByte(b);msgEnd();}
void sound(const std::string& key,Vec3 position){if(active&&!stopping&&World::ready())World::ambient("frost",key,position);}
void visual(const std::string& kind,Vec3 position,double radius,std::optional<Vec3> direction={}){Queued q;q.kind=kind;q.position=position;q.radius=radius;q.direction=direction;queue.push_back(q);}
void queueGauge(const std::shared_ptr<State>& s){
    const auto expires=read<float>(s->weapon+0x22c);const auto remaining=expires-now();const auto index=engine.index(s->edict);if(!std::isfinite(remaining)||remaining<=0||remaining>prop(*s,"ShieldTime")+.01||index<1||index>32)return;
    const auto old=hud.find(index);if(old!=hud.end()&&old->second.expires==expires&&old->second.state==s)return;const bool start=old==hud.end()||old->second.state!=s||old->second.expires<=now();hud[index]={s,expires};Queued q;q.type=Queued::Type::Hud;q.entity=index;q.expires=expires;q.state=s;q.start=start;queue.push_back(q);
}
void clearGauge(int index,const std::shared_ptr<State>& s){Queued q;q.type=Queued::Type::Hud;q.entity=index;q.clear=true;q.state=s;queue.push_back(q);}
void sendGauge(int entity,double remaining,bool clear,bool start){if(start)frostMessage({3,static_cast<unsigned char>(entity)});const auto p=shieldGaugePayload(entity,remaining);frostMessage({p.begin(),p.end()});if(clear)frostMessage({4,static_cast<unsigned char>(entity)});}
bool hit(const State& s,const CombatTarget& e,double amount){return valid(e.e)&&amount>0&&amount<200000&&damage(e.e,s,amount).delta>0;}
void gain(const State& s){write(s.weapon+0x168,static_cast<int>(std::min(prop(s,"MaxSpecialClip"),read<int>(s.weapon+0x168)+value(s,"DebuffChargingGauge"))));}
void slow(const State& s,const CombatTarget& e,const std::string& prefix){
    if(!hostile(e.e,s))return;const auto duration=value(s,prefix+"FixedMove"+(prefix=="KillEvasion"?"Duration":"Time")),speed=value(s,prefix+"FixedMoveSpeed");if(duration<=0)return;
    const auto g=s.props.find(prefix+"FixedMoveGravity"+(s.mode==1?"Zombie":s.mode==2?"Scenario":""));std::optional<double> gravity;if(g!=s.props.end()&&std::isfinite(g->second))gravity=g->second;
    auto i=statuses.find(e.e.object);if(i==statuses.end()||!valid(i->second.e))i=statuses.insert_or_assign(e.e.object,Status{e.e,0,speed,gravity}).first;auto& x=i->second;x.until=std::max(x.until,now()+duration);x.speed=std::min(x.speed,speed);if(gravity)x.gravity=gravity;
}
void queueSpike(const Entity& e,bool on){
    const auto t=now();const auto owners=std::count_if(spikes.begin(),spikes.end(),[&](const auto& item){const auto& x=item.second;return valid(x.target.e)&&x.target.e.object==e.object&&x.target.e.serial==e.serial&&x.until>t;});if((on&&owners>1)||(!on&&owners>0))return;
    Queued q;q.type=Queued::Type::Spike;q.entity=engine.index(e.edict);q.on=on;queue.push_back(q);
}
void markHit(const std::shared_ptr<State>& s,const CombatTarget& e,double probability){
    if(!hostile(e.e,*s))return;const auto key=std::make_pair(s->weapon,e.e.object);const auto old=spikes.find(key);const auto t=now();
    if(old!=spikes.end()&&valid(old->second.target.e)&&old->second.until>t){spikes.erase(old);queueSpike(e.e,false);gain(*s);const auto origin=center(e.e);const auto radius=prop(*s,"DebuffExpRadius")*Meter;visual("spike_burst",origin,radius);sound("frostbite-1_exp2",origin);
        for(const auto& target:combatSphere(*s,origin,radius))if(length(closest(target.e,origin)-origin)<=radius&&visible(*s,origin,target)&&hit(*s,target,value(*s,"DebuffExpDamage")))slow(*s,target,"DebuffExp");return;
    }
    if(read<float>(e.e.pev+0x178)<=0)return;if(probability>=100||std::generate_canonical<double,53>(random)*100<probability){spikes[key]={e,s,t+prop(*s,"DebuffDuration")};queueSpike(e.e,true);slow(*s,e,"Debuff");}
}
void fireOrb(const std::shared_ptr<State>& s,const Aim& a){const auto t=now();queueGauge(s);s->orbs.push_back({++orbId,a.source,a.direction,t+prop(*s,"BModeLifeTime"),t,{}});effect(*s,read<float>(s->weapon+0x22c)>t?8:2);engine.animation(s->weapon,8,0);}
void fireBeam(const std::shared_ptr<State>& s){
    const auto t=now(),cost=prop(*s,"CModeCost");const auto w=s->weapon;const auto energy=read<int>(w+0x168);if((read<unsigned>(s->pev+0x1a4)&0x801)!=0x801||cost<=0||energy<cost||t<s->nextBeam)return;
    const auto a=combatAim(*s);const auto range=prop(*s,"CModeMaxDistance")*Meter,radius=prop(*s,"CModeRadius")*Meter;const auto end=trace(a.source,a.source+a.direction*range,s->edict,1).end;
    write(w+0x168,static_cast<int>(energy-cost));shieldNative(w);queueGauge(s);s->nextBeam=t+prop(*s,"CModeCycleTime");const auto wt=engine.weaponTime();attackTimers(w,prop(*s,"CModeFastShootTime"),prop(*s,"CModeCycleTime"));idleTimer(w,1);write(s->object+0x174,static_cast<float>(wt+prop(*s,"CModeFastShootTime")));write(w+0x21c,static_cast<float>(t+prop(*s,"SpecialClipChargeDelayAfterAttack")));
    const auto mid=(a.source+end)*.5;const auto half=length(end-a.source)*.5;for(const auto& e:combatSphere(*s,mid,half+radius)){const auto q=segmentPoint(e.center,a.source,end);if(length(closest(e.e,q)-q)<=radius&&trace(a.source,e.center,s->edict,1).fraction>=.999&&hit(*s,e,value(*s,"CModeDamage"))){slow(*s,e,"CMode");markHit(s,e,prop(*s,"CModePassiveProb"));}}
    effect(*s,9,radius,length(end-a.source));engine.animation(w,9,0);
}
Before normalBefore(Address w){Before x;x.state=state(w);x.aim=combatAim(*x.state);x.clip=read<int>(w+0x160);for(const auto& e:combatSphere(*x.state,x.aim.source,8192))x.health[e.e.object]={e,read<float>(e.e.pev+0x178)};return x;}
void normalAfter(Before& x){
    const auto s=x.state;if(read<int>(s->weapon+0x160)!=x.clip-1)return;const auto begin=ptr(s->object+0x84),end=ptr(s->object+0x88);if(end<begin||end-begin>28*128||(end-begin)%28)throw std::runtime_error("Frost native impact vector");
    struct Hit {CombatTarget target;Vec3 point;};std::set<Address> seen;std::vector<Hit> hits;
    for(auto q=begin;q<end;q+=28){const auto key=ptr(q);const auto i=x.health.find(key);if(i!=x.health.end()&&valid(i->second.first.e)&&read<float>(i->second.first.e.pev+0x178)<i->second.second&&seen.insert(key).second)hits.push_back({i->second.first,vec(q+16)});}
    if(std::any_of(hits.begin(),hits.end(),[&](const auto& h){return hostile(h.target.e,*s);}))++s->hits;for(const auto& h:hits)markHit(s,h.target,prop(*s,"AModePassiveProb"));
    if(!hits.empty()){const auto first=hits.front();visual("fan",first.point,0,x.aim.direction*-1);sound("frostbite-1_exp1",first.point);const auto radius=prop(*s,"AModeFanAttackMeter")*Meter,angle=prop(*s,"AModeFanAttackAngle");
        for(const auto& e:combatSphere(*s,first.point,radius)){if(seen.count(e.e.object)||!inFan(e.center,first.point,x.aim.direction,radius,angle)||trace(first.point,e.center,s->edict,1).fraction<.999)continue;if(hit(*s,e,value(*s,"AModeFanAttackDamage")))markHit(s,e,prop(*s,"AModePassiveProb"));}
    }
}
void knock(const State& s,const CombatTarget& e,double side,double up,Vec3 origin){if(!hostile(e.e,s))return;const auto d=unit(Vec3{e.center.x-origin.x,e.center.y-origin.y,0},{1,0,0});putVec(e.e.pev+0x20,{float(d.x*side),float(d.y*side),float(up)});}
void shieldFrame(State& s,double t){
    if(read<float>(s.weapon+0x22c)<=t||t<s.nextShield||t<s.nextShieldScan)return;s.nextShieldScan=t+prop(s,"ShieldAttackTick");const auto origin=vec(s.pev+8);const auto radius=prop(s,"ShieldRadius")*Meter;auto nearby=combatSphere(s,origin,radius);
    nearby.erase(std::remove_if(nearby.begin(),nearby.end(),[&](const auto& e){return !hostile(e.e,s)||length(closest(e.e,origin)-origin)>radius||!visible(s,origin,e);}),nearby.end());if(nearby.empty())return;std::stable_sort(nearby.begin(),nearby.end(),[&](const auto& a,const auto& b){return length(a.center-origin)<length(b.center-origin);});
    const auto impact=nearby.front().center;const auto area=prop(s,"ShieldAttackRadius")*Meter;for(const auto& e:combatSphere(s,impact,area))if(hostile(e.e,s)&&length(closest(e.e,impact)-impact)<=area&&visible(s,impact,e)&&hit(s,e,value(s,"ShieldAttackDamage"))){slow(s,e,"ShieldAttack");knock(s,e,value(s,"ShieldAttackKnockbackSide"),value(s,"ShieldAttackKnockbackUp"),origin);}
    s.nextShield=t+prop(s,"ShieldAttackCoolTime");const auto ground=trace(impact,impact+Vec3{0,0,-256},s.edict,1).end;visual("shield_burst",ground,area);sound("frostbite_fx_exp",impact);
}
bool orbFrame(const std::shared_ptr<State>& s,Orb& o,double t){
    if(t>=o.until)return false;if(t<=o.last)return true;const auto step=clamp(t-o.last,0,.1);const auto from=o.position,next=from+o.direction*(prop(*s,"BModeSpeed")*step);const auto tr=trace(from,next,s->edict,1);o.last=t;o.position=tr.end;
    if(!tr.startSolid){const auto radius=prop(*s,"BModeRadius")*Meter,height=prop(*s,"BModeHeight")*Meter;const auto mid=(from+tr.end)*.5;const auto broad=length(tr.end-from)*.5+std::hypot(radius,height*.5);
        for(const auto& e:combatSphere(*s,mid,broad)){const auto key=std::make_pair(e.e.object,e.e.serial);if(o.hit.count(key)||!valid(e.e))continue;const auto contact=sweptCylinderContact(from,tr.end,vec(e.e.pev+0xd4),vec(e.e.pev+0xe0),radius,height);if(!contact)continue;const auto cover=trace(contact->point,closest(e.e,contact->point),s->edict,1);if(cover.startSolid||cover.fraction<.999)continue;o.hit.insert(key);if(hit(*s,e,value(*s,"BModeDamage"))){slow(*s,e,"BMode");markHit(s,e,prop(*s,"BModePassiveProb"));}}
    }
    if(tr.startSolid||tr.fraction<.999){visual("orb_impact",o.position,prop(*s,"BModeRadius")*Meter);sound("frostbite-2_exp",o.position);return false;}return true;
}
void network(){
    if(!World::ready()){queue.clear();return;}const auto t=now();std::set<std::string> wanted;
    if(active&&!stopping)for(const auto& [w,s]:states)if(validState(*s))for(const auto& orb:s->orbs){const auto key="orb/"+std::to_string(orb.id);wanted.insert(key);auto v=visuals.ensure(key,597,"orb",orb.position,1,t);if(v){v->until=t+.25;v->sticky=v->loop=true;v->options.angles=angles(orb.direction);}}
    const auto count=std::min<std::size_t>(queue.size(),256);for(std::size_t i=0;i<count;i++){const auto& x=queue[i];
        if(x.type==Queued::Type::Hud){if(!x.state||!valid(*x.state))continue;const auto left=x.clear||stopping||!active?0:std::min(prop(*x.state,"ShieldTime"),std::max(0.,x.expires-t));sendGauge(x.entity,left,left==0,x.start&&left>0);}
        else if(x.type==Queued::Type::Spike)frostMessage({static_cast<unsigned char>(x.on?0:1),static_cast<unsigned char>(x.entity&255),static_cast<unsigned char>((x.entity>>8)&255)});
        else if(active&&!stopping){const auto& meta=profiles()["frost"]["visual_assets"]["597/"+x.kind];const auto scale=meta["type"].text()=="sprite"?2*x.radius/meta["width"].number():1;auto v=visuals.ensure("fx/"+std::to_string(++visualId),597,x.kind,x.position,scale,t);if(v&&x.direction)v->options.angles=angles(*x.direction);}
    }queue.erase(queue.begin(),queue.begin()+count);if(stopping||!active){visuals.clear();return;}visuals.tick(t,wanted);
}
void cleanup(){for(const auto& [index,x]:hud)if(valid(*x.state))clearGauge(index,x.state);hud.clear();for(const auto& [o,s]:statuses)StatusBroker::release("frost",s.e);statuses.clear();while(!spikes.empty()){const auto e=spikes.begin()->second.target.e;spikes.erase(spikes.begin());if(valid(e))queueSpike(e,false);}defenses.clear();for(auto& [w,s]:states){s->orbs.clear();s->godUntil=0;s->pendingArmor=false;}network();visuals.clear();cleaned=(stopping||!active)&&queue.empty();}
void tick(){
    const auto t=now();if(lastTime>=0&&t<lastTime&&lastTime-t<=1)return;if(lastTime>=0&&t<lastTime){cleanup();states.clear();properties.clear();}lastTime=t;
    for(auto i=defenses.begin();i!=defenses.end();)if(!alive(*i->second))i=defenses.erase(i);else ++i;
    for(auto i=states.begin();i!=states.end();){auto s=i->second;if(!validState(*s)||!alive(*s)){i=states.erase(i);continue;}s->held+=std::max(0.,t-s->lastFrame);s->lastFrame=t;
        s->orbs.erase(std::remove_if(s->orbs.begin(),s->orbs.end(),[&](Orb& o){return !orbFrame(s,o,t);}),s->orbs.end());shieldFrame(*s,t);
        if(s->pendingArmor){s->pendingArmor=false;const auto origin=vec(s->pev+8);const auto radius=prop(*s,"KillEvasionAttackRadius")*Meter;for(const auto& e:combatSphere(*s,origin,radius))if(hostile(e.e,*s)&&visible(*s,origin,e)&&length(closest(e.e,origin)-origin)<=radius){hit(*s,e,value(*s,"KillEvasionDamage"));knock(*s,e,value(*s,"KillEvasionKnockbackSide"),value(*s,"KillEvasionKnockbackUp"),origin);slow(*s,e,"KillEvasion");}visual("armor",origin+Vec3{0,0,-36},radius);sound("frostbite_fx_exp",origin);}++i;
    }
    for(auto i=statuses.begin();i!=statuses.end();){const auto& x=i->second;if(!valid(x.e)){i=statuses.erase(i);continue;}if(t>=x.until||read<float>(x.e.pev+0x178)<=0){StatusBroker::release("frost",x.e);i=statuses.erase(i);continue;}if(x.speed>=0){StatusBroker::set("frost",x.e,0x240,x.speed);auto v=vec(x.e.pev+0x20);const auto n=std::hypot(v.x,v.y);if(n>x.speed){v.x=static_cast<float>(v.x*x.speed/n);v.y=static_cast<float>(v.y*x.speed/n);putVec(x.e.pev+0x20,v);}}if(x.gravity&&*x.gravity>=0)StatusBroker::set("frost",x.e,0x12c,*x.gravity);++i;}
    for(auto i=hud.begin();i!=hud.end();){const auto& x=i->second;if(!validState(*x.state)||read<float>(x.state->pev+0x178)<=0||x.expires<=t){if(valid(*x.state))clearGauge(i->first,x.state);i=hud.erase(i);}else ++i;}
    for(auto i=spikes.begin();i!=spikes.end();){const auto x=i->second;if(!valid(x.target.e)||!validState(*x.state)||read<float>(x.target.e.pev+0x178)<=0||t>=x.until){i=spikes.erase(i);if(valid(x.target.e))queueSpike(x.target.e,false);}else ++i;}
}
bool armorReady(const State& s,const Defense& d,double t){return s.mode!=0&&t>=d.cooldown&&s.held>=value(s,"KillEvasionWeaponCooltime")&&s.hits>=value(s,"KillEvasionShotCount");}
Address armorWeapon(Address victim){std::vector<std::shared_ptr<State>> owned,ready;const auto t=now();for(const auto& [w,s]:states)if(s->object==victim&&validState(*s)){owned.push_back(s);const auto d=defenses.find(victim);if(d!=defenses.end()&&valid(*d->second)&&armorReady(*s,*d->second,t))ready.push_back(s);}if(owned.empty())return 0;auto& list=ready.empty()?owned:ready;const auto held=ptr(victim+0x1024);for(const auto& s:list)if(s->weapon==held)return s->weapon;std::stable_sort(list.begin(),list.end(),[](const auto& a,const auto& b){return a->held!=b->held?a->held>b->held:a->order<b->order;});return list.front()->weapon;}
void __fastcall primary(Address w,void*){std::lock_guard<std::recursive_mutex> lock(gameplayMutex);if(active&&!stopping&&isActive(w)&&(read<unsigned>(ptr(ptr(w+0xd0)+8)+0x1a4)&0x801)==0x801){guarded("frost.primary_both",[&]{fireBeam(state(w));});return;}primaryNative(w);}
void secondary(Address w){
    if(!active||stopping||!isActive(w)){secondaryNative(w);return;}std::shared_ptr<State> s;guarded("frost.secondary_state",[&]{s=state(w);});if(!s){secondaryNative(w);return;}
    if((read<unsigned>(s->pev+0x1a4)&0x801)==0x801){guarded("frost.secondary_beam",[&]{fireBeam(s);});return;}
    const auto before=read<int>(w+0x168);std::optional<Aim> a;guarded("frost.secondary_aim",[&]{a=combatAim(*s);});secondaryNative(w);const auto cost=prop(*s,"BModeCost");if(active&&!stopping&&a&&cost>0&&before-read<int>(w+0x168)==cost)guarded("frost.orb",[&]{fireOrb(s,*a);});
}
void __fastcall secondaryDispatch(Address w,void*){std::lock_guard<std::recursive_mutex> lock(gameplayMutex);secondary(w);}
}
bool frostClean(){return frost::cleaned;}
void installFrost(){
    using namespace frost;random.seed(std::random_device{}());shieldNative=native<decltype(shieldNative)>(at(0xc8bfc0));skipNativeSecondary=native<decltype(skipNativeSecondary)>(at(0xa63c10));
    if(ptr(at(0x21d3b1c))!=hat(0x73f180)||ptr(at(0x21d3b24))!=hat(0x73fc80)||ptr(at(0x21d3b20))!=hat(0x73f260))throw std::runtime_error("Frost message binding");msgBegin=native<decltype(msgBegin)>(hat(0x73f180));msgByte=native<decltype(msgByte)>(hat(0x73fc80));msgEnd=native<decltype(msgEnd)>(hat(0x73f260));
    World::onMapReset([]{visuals.forget();queue.clear();states.clear();statuses.clear();defenses.clear();spikes.clear();hud.clear();properties.clear();lastTime=-1;});
    Hooks::attach(at(0xc8c040),"frost",[](Invocation& v){auto& x=v.createState<std::optional<Before>>();if(active&&!stopping&&isActive(v.registers->ecx))x=normalBefore(v.registers->ecx);},[](Invocation& v){auto& x=v.state<std::optional<Before>>();if(x&&active&&!stopping)normalAfter(*x);});
    Hooks::attach(at(0xc84850),"frost",[](Invocation& v){if(active&&!stopping&&isFrost(v.registers->ecx))state(v.registers->ecx);});
    Hooks::replace(at(0xc84190),reinterpret_cast<void*>(&primary),reinterpret_cast<void**>(&primaryNative),"frost.primary");Hooks::replace(at(0xc84660),reinterpret_cast<void*>(&secondaryDispatch),reinterpret_cast<void**>(&secondaryNative),"frost.secondary");
    Hooks::attach(at(0x14b3be0),"frost",[](Invocation& v){auto& w=v.createState<Address>(0);const auto a=v.argument<Address>(0);if(active&&!stopping&&v.returnAddress==at(0x1528e67)&&isActive(a)&&ptr(a+0xd0)==v.registers->ecx)w=a;},[](Invocation& v){const auto w=v.state<Address>();if(w&&active&&!stopping&&skipNativeSecondary())secondary(w);});
    Hooks::attach(at(0x15225f0),"frost",[](Invocation& v){v.createState<Address>(v.registers->ecx);},[](Invocation& v){const auto w=v.state<Address>();if(active&&!stopping&&v.registers->eax&&isFrost(w))state(w);});
    Hooks::attach(at(0x13f8a80),"frost",[](Invocation&){if(!active||stopping){if(!cleaned)cleanup();return;}tick();network();});
    DamageBroker::add("frost",[](const DamageContext& c){const auto d=defenses.find(c.victim);return active&&!stopping&&d!=defenses.end()&&valid(*d->second)&&read<float>(d->second->pev+0x178)>0&&now()<d->second->until;});
    ArmorGate::add({"frost",[](Address victim){return active&&!stopping?armorWeapon(victim):0;},[](const ArmorContext& c)->std::any{if(!active||stopping)return {};const auto i=states.find(c.weapon);if(i==states.end())return {};auto s=i->second;if(!validState(*s)||s->object!=c.object||s->serial!=c.serial||s->we.serial!=c.weaponSerial||s->mode!=1)return {};const auto d=defenses.find(c.object);if(d==defenses.end()||!valid(*d->second)||c.time<d->second->cooldown||c.time<d->second->until||!armorReady(*s,*d->second,c.time))return {};return ArmorToken{s,d->second};},[](const ArmorContext& c,const std::any& any){const auto token=std::any_cast<ArmorToken>(any);auto& s=*token.state;auto& d=*token.defense;if(!active||stopping||!validState(s)||!valid(d))return;d.cooldown=s.cooldown=c.time+value(s,"KillEvasionPlayerCooltime");d.until=c.time+prop(s,"KillEvasionGodTime");s.held=0;s.hits=0;s.pendingArmor=true;}});
}
}
