#include "families.hpp"
#include "hooks.hpp"
#include "brokers.hpp"
#include "world.hpp"
#include <optional>

namespace csnz {
namespace wolf {
struct Rush {int remaining{};double next{},started{};bool preparing=true;std::vector<Entity> lanes;};
struct OwnedField {unsigned old{},last{};};
struct State {
    Address weapon{};Entity we,owner;Properties props;int mode{},ownerIndex{},knownState{};
    double nextAttack{},nextDrain{},last{};unsigned lastButtons{};
    std::optional<Vec3> anchor;std::optional<double> anchorYaw,colorStarted,colorUntil;
    std::optional<Rush> rush;std::optional<OwnedField> handRing;
};
struct Slow {Entity e;double from{},until{},speed{};};
struct Bite {std::shared_ptr<State> state;Entity target;double due{},started{};Vec3 from;unsigned id{};};
struct Plan {const Config* curve{};Vec3 forward,left,up,from,angles;double distance{},scale{};};
struct Effect {std::string kind,key;Vec3 from,to;double duration{},scale=1,born{},fadeOut{};int sequence{};std::optional<Plan> plan;};
std::map<Address,std::shared_ptr<State>> states;
std::map<Address,Slow> slows;std::vector<Bite> bites;std::vector<Effect> queue;std::map<std::string,Effect> effects;
double lastTime=std::numeric_limits<double>::quiet_NaN();unsigned sequence=0;bool cleaned=false;
const Config& cfg(){return profiles()["wolf"];}
double prop(const State& s,const std::string& key,bool variant=false){return property(s.props,key+(variant?(s.mode==1?"Zombie":s.mode==2?"Scenario":""):""));}
bool validState(const State& s){return weaponIs(s.weapon,"wolf")&&valid(s.we)&&valid(s.owner)&&ptr(s.weapon+0xd0)==s.owner.object;}
bool isActive(const State& s){return validState(s)&&ptr(s.owner.object+0x1024)==s.weapon;}
bool isAlive(const State& s){return validState(s)&&read<float>(s.owner.pev+0x178)>0;}
double normalizeYaw(double yaw){if(!std::isfinite(yaw))throw std::runtime_error("Wolf yaw");return std::fmod(std::fmod(yaw,360.)+360.,360.);}
double ownerYaw(const State& s){return normalizeYaw(vec(s.owner.pev+0x74).y);}
void animation(const State& s,int n){if(!isActive(s)||n<0||n>11)return;const auto f=ptr(ptr(s.weapon)+0x294);if(f<mp||f>=mp+profiles()["shared"]["modules"]["mp.dll"]["size"].u32())throw std::runtime_error("Wolf animation target");native<void(__thiscall*)(Address,int,int)>(f)(s.weapon,n,0);}
void setMode(State& s,int n,double t,bool expiry=false){
    const auto previous=read<int>(s.weapon+0x218);write(s.weapon+0x218,n);write(s.weapon+0x21c,n);write(s.weapon+0x220,static_cast<unsigned char>(0));s.knownState=n;s.nextAttack=t;
    const auto suffix=s.mode==2?"Scenario":"";s.nextDrain=t+(n==1?prop(s,std::string("FixedDrone_GaugeDecrTime")+suffix):n==2?prop(s,std::string("AttachDrone_GaugeDecrTime")+suffix):1);
    if(n==1){double yaw=std::floor(ownerYaw(s)/90+.5)*90;s.anchorYaw=yaw==0?360:yaw;const auto from=vec(s.owner.pev+8)+Vec3{0,0,32};const auto floor=trace(from,from+Vec3{0,0,-128},s.owner.edict);s.anchor=floor.fraction<.999?floor.end+Vec3{0,0,1}:vec(s.owner.pev+8);}
    else{s.anchor.reset();s.anchorYaw.reset();}
    if(expiry&&(previous==1||previous==2)){animation(s,previous==1?6:9);write(s.weapon+0x238,0);write(s.weapon+0x23c,static_cast<float>(t+(previous==1?10.:20.)/30));}
}
std::shared_ptr<State> state(Address w){
    if(!weaponIs(w,"wolf"))return {};auto i=states.find(w);if(i!=states.end()&&validState(*i->second))return i->second;
    auto s=std::make_shared<State>();s->weapon=w;s->we=entity(w);s->owner=entity(ptr(w+0xd0));s->props=readProperties(w,0x1d18,4096,true);
    for(const auto& [name,value]:s->props)if(value<0||value>20000)throw std::runtime_error("Wolf property bounds");
    for(const char* key:{"FixedDrone_Cycle","AttachDrone_Cycle","C_Cycle","C_WolfDuration","C_NumTarget","C_NumAttack"})if(!(prop(*s,key)>0))throw std::runtime_error("Wolf required property");
    for(int index=1;index<=32;index++)if(engine.byIndex(index)==s->owner.edict){s->ownerIndex=index;break;}
    const auto t=now();s->mode=mode();s->knownState=read<int>(w+0x218);s->nextAttack=s->nextDrain=s->last=t;
    if(s->knownState<0||s->knownState>2)throw std::runtime_error("Wolf native state");states[w]=s;if(s->knownState)setMode(*s,s->knownState,t);return s;
}
void completeTransition(State& s,double t){
    const auto next=read<int>(s.weapon+0x21c);const auto deadline=read<float>(s.weapon+0x14c);
    if(read<unsigned char>(s.weapon+0x220)&&next>=0&&next<=2&&std::isfinite(deadline)&&t>=deadline){if(next!=read<int>(s.weapon+0x218))setMode(s,next,t);else write(s.weapon+0x220,static_cast<unsigned char>(0));}
    const auto n=read<int>(s.weapon+0x238);const auto animEnd=read<float>(s.weapon+0x23c);if(n>=0&&n<=11&&std::isfinite(animEnd)&&t>=animEnd){animation(s,n);write(s.weapon+0x238,-1);}
    const auto actual=read<int>(s.weapon+0x218);if(actual!=s.knownState&&actual>=0&&actual<=2)setMode(s,actual,t);
}
Vec3 origin(const State& s){return read<int>(s.weapon+0x218)==1&&s.anchor?*s.anchor:vec(s.owner.pev+8)+Vec3{0,0,static_cast<float>(prop(s,"AttachDrone_OffsetLocal"))};}
void knock(const State& s,const Entity& e,Vec3 from,const std::string& prefix){
    if(!valid(e))return;const auto flags=read<unsigned>(e.pev+0x1c8);const auto side=prop(s,prefix+(flags&0x4000?"_LenC":flags&0x200?"_LenG":"_LenJ"),true);
    auto v=vec(e.pev+0x20),d=center(e)-from;d.z=0;d=unit(d);if(side>=0){v.x=static_cast<float>(d.x*side);v.y=static_cast<float>(d.y*side);}putVec(e.pev+0x20,v);
}
void addSlow(const State& s,const Entity& e,const std::string& prefix,double t){
    if(!valid(e))return;const auto duration=prop(s,prefix+"_FixedMoveDuration"),speed=prop(s,prefix+"_FixedMoveSpeed");if(duration<=0)return;
    auto i=slows.find(e.object);if(i==slows.end()||!valid(i->second.e))i=slows.insert_or_assign(e.object,Slow{e,t,t,speed}).first;
    i->second.until=std::max(i->second.until,t+duration);i->second.speed=std::min(i->second.speed,speed);
}
void visual(const std::string& kind,const std::string& key,Vec3 from,Vec3 to,double duration,double scale=1,int seq=0,double fade=0){Effect e;e.kind=kind;e.key=key;e.from=from;e.to=to;e.duration=duration;e.scale=scale;e.sequence=seq;e.fadeOut=fade;queue.push_back(e);}
std::string nextKey(const char* stem){return std::string(stem)+"/"+std::to_string(++sequence);}
void drone(State& s,double t){
    const auto n=read<int>(s.weapon+0x218);if(n!=1&&n!=2)return;const std::string prefix=n==1?"FixedDrone":"AttachDrone";const auto o=origin(s);
    if(t>=s.nextDrain){const auto interval=prop(s,prefix+"_GaugeDecrTime"+(s.mode==2?"Scenario":""));if(!(interval>0))throw std::runtime_error("Wolf drain interval");const auto spent=std::min(10,static_cast<int>(std::floor((t-s.nextDrain)/interval))+1);const auto gauge=read<int>(s.weapon+0x160);write(s.weapon+0x160,std::max(0,gauge-spent));s.nextDrain=t+interval;if(gauge<=spent){setMode(s,0,t,true);return;}}
    if(t<s.nextAttack)return;s.nextAttack=t+prop(s,prefix+"_Cycle");auto list=targets(s.owner,o,prop(s,prefix+"_AttackDist")*Meter);const auto max=static_cast<std::size_t>(std::floor(prop(s,prefix+"_AttackCount")));if(list.size()>max)list.resize(max);
    for(const auto& x:list){const auto r=damage(x.entity,s.owner,prop(s,prefix+"_Damage",true));if(r.delta>0){knock(s,x.entity,o,prefix);addSlow(s,x.entity,prefix,t);visual("hit",nextKey("hit"),x.center,x.center,.5,n==1?prop(s,"FixedDrone_HitEffectScale"):1);}}
}
void beginRush(State& s,double t){
    const auto gauge=read<int>(s.weapon+0x168);const auto required=prop(s,"C_GaugeRequire");if(gauge<required||s.rush)return;const auto stance=read<int>(s.weapon+0x218);if(stance<0||stance>2)return;
    write(s.weapon+0x168,static_cast<int>(gauge-required));s.rush=Rush{static_cast<int>(std::floor(prop(s,"C_NumAttack"))),t+prop(s,"ChangeTime_D2C"),t,true,{}};
    s.colorStarted=t;s.colorUntil=t+prop(s,"ColorChangeTime");const auto p=vec(s.owner.pev+8);const auto floor=trace(p+Vec3{0,0,16},p+Vec3{0,0,-128},s.owner.edict);const auto o=floor.fraction<.999?floor.end+Vec3{0,0,1}:p;
    visual("rushExplo",nextKey("rushStart"),o,o,.6);engine.emitSound(s.owner.edict,0,reinterpret_cast<const char*>(at(0x191ca88)),1,.8f,0,100);
}
void rushFrame(const std::shared_ptr<State>& s,double t){
    if(!s->rush||t<s->rush->next)return;auto& r=*s->rush;const auto o=vec(s->owner.pev+8)+Vec3{0,0,static_cast<float>(prop(*s,"AttachDrone_OffsetLocal"))};r.preparing=false;
    if(r.remaining<=0){if(std::any_of(bites.begin(),bites.end(),[&](const Bite& b){return b.state==s;}))return;s->rush.reset();return;}
    const auto max=static_cast<std::size_t>(std::floor(prop(*s,"C_NumTarget")));std::set<Address> seen;
    r.lanes.erase(std::remove_if(r.lanes.begin(),r.lanes.end(),[&](const Entity& e){return !valid(e)||read<float>(e.pev+0x178)<=0||length(center(e)-o)>prop(*s,"C_AttackDistMax")*Meter||!hostile(e,s->owner);}),r.lanes.end());for(const auto& e:r.lanes)seen.insert(e.object);
    for(const auto& x:targets(s->owner,o,prop(*s,"C_AttackDist")*Meter)){if(r.lanes.size()>=max)break;if(seen.insert(x.entity.object).second)r.lanes.push_back(x.entity);}
    const auto wave=static_cast<int>(std::floor(prop(*s,"C_NumAttack")))-r.remaining;
    for(std::size_t lane=0;lane<r.lanes.size();lane++){
        const auto target=r.lanes[lane];const auto dest=center(target);const unsigned id=++sequence;const int side=(wave+lane)%2?-1:1;bites.push_back({s,target,t,t,o,id});
        const auto eye=vec(s->owner.pev+8)+vec(s->owner.pev+0x198),delta=dest-eye;const auto distance=length(delta);if(distance<.001)continue;
        const auto forward=unit(delta);const double horizontal=std::hypot(forward.x,forward.y),yaw=ownerYaw(*s)*Pi/180;
        const Vec3 left=horizontal>1e-6?Vec3{float(-forward.y/horizontal),float(forward.x/horizontal),0}:Vec3{float(-std::sin(yaw)),float(std::cos(yaw)),0};
        const auto depth=clamp(distance,96,220);const auto to=eye+forward*depth,from=eye+forward*(depth*.55)+left*(side*depth*1.05);
        visual("wolf","rush/"+std::to_string(id),from,to,prop(*s,"C_WolfDuration"),depth/220,wave%3,prop(*s,"C_WolfFadeOut"));
    }--r.remaining;r.next=t+prop(*s,"C_Cycle");
}
void biteFrame(double t){
    for(std::size_t i=bites.size();i-->0;){const auto b=bites[i];const auto s=b.state;if(!isAlive(*s)||!valid(b.target)||t<b.started||read<float>(b.target.pev+0x178)<=0){bites.erase(bites.begin()+i);continue;}if(t<b.due)continue;bites.erase(bites.begin()+i);
        const auto destination=center(b.target),o=vec(s->owner.pev+8)+Vec3{0,0,static_cast<float>(prop(*s,"AttachDrone_OffsetLocal"))};if(length(destination-o)>prop(*s,"C_AttackDistMax")*Meter)continue;const auto line=trace(o,destination,s->owner.edict);if(line.fraction<.999&&line.edict!=b.target.edict)continue;
        const auto r=damage(b.target,s->owner,prop(*s,"C_Damage",true));if(r.delta>0){visual("wolfExplo",nextKey("rushHit"),destination,destination,.5,prop(*s,"C_HitEffectScale"));knock(*s,b.target,o,"C");addSlow(*s,b.target,"C",t);}
        if(r.killed){const auto refund=prop(*s,gameMode()==59?"C_KillTargetChargeZSRift":"C_KillTargetCharge");write(s->weapon+0x168,static_cast<int>(std::min(100.,read<int>(s->weapon+0x168)+refund)));}
    }
}
void restoreHand(State& s){if(!s.handRing)return;if(valid(s.we)){const auto p=s.we.pev+0x124;if(read<unsigned>(p)==s.handRing->last)write(p,s.handRing->old);}s.handRing.reset();}
void syncHand(State& s){if(!isActive(s)||!isAlive(s)){restoreHand(s);return;}const auto p=s.we.pev+0x124,n=read<unsigned>(p);if(n>3)throw std::runtime_error("Wolf hand body");if(!s.handRing)s.handRing=OwnedField{n,n};else if(n!=s.handRing->last)s.handRing->old=n;write(p,n|1u);s.handRing->last=n|1u;}
void frame(const std::shared_ptr<State>& s,double t){
    if(!validState(*s))return;if(!isAlive(*s)){if(s->knownState)setMode(*s,0,t);s->rush.reset();s->colorStarted.reset();s->colorUntil.reset();return;}
    if(t<s->last){s->rush.reset();s->colorStarted.reset();s->colorUntil.reset();setMode(*s,0,t);}s->last=t;
    if(s->colorUntil&&t>=*s->colorUntil){s->colorStarted.reset();s->colorUntil.reset();}completeTransition(*s,t);
    const auto buttons=read<unsigned>(s->owner.object+0xec4);const bool reload=(buttons&0x2000)&&!(s->lastButtons&0x2000);s->lastButtons=buttons;
    if(isActive(*s)&&reload)beginRush(*s,t);drone(*s,t);if(s->rush)rushFrame(s,t);
}
Plan plan(const Effect& f){
    const auto& motion=cfg()["rush_motion"];const auto& curve=motion["sequences"][static_cast<std::size_t>(f.sequence)];
    if(motion["model"].text()!=cfg()["visual_assets"]["wolf"]["path"].text()||motion["forward_axis"].text()!="+X"||curve["frames"].integer()<2||curve["fps"].number()<=0)throw std::runtime_error("Wolf root motion asset");
    // The array contracts below are compiled from the accepted model samples.
    const auto count=curve["frames"].u32();for(const char* axis:{"x","y","z"})if(curve[axis].array().size()!=count)throw std::runtime_error("Wolf root curve length");
    if(curve["start"][0].number()>=-.001||f.scale<=0)throw std::runtime_error("Wolf root curve origin");const auto d=f.to-f.from,forward=unit(d);const double distance=length(d),horizontal=std::hypot(forward.x,forward.y);if(distance<.001)throw std::runtime_error("Wolf root direction");
    const Vec3 left=horizontal>1e-6?Vec3{float(-forward.y/horizontal),float(forward.x/horizontal),0}:Vec3{0,1,0};const Vec3 up{forward.y*left.z-forward.z*left.y,forward.z*left.x-forward.x*left.z,forward.x*left.y-forward.y*left.x};
    return {&curve,forward,left,up,f.from,angles(forward),distance,f.scale};
}
std::pair<Vec3,double> sample(const Plan& p,double age){
    const auto& c=*p.curve;const auto count=c["frames"].integer();const auto f=clamp(age*c["fps"].number(),0,count-1.);const auto i=static_cast<std::size_t>(std::floor(f)),j=std::min(i+1,static_cast<std::size_t>(count-1));Vec3 root;
    unsigned k=0;for(const char* axis:{"x","y","z"})root[k++]=static_cast<float>(c[axis][i].number()+(c[axis][j].number()-c[axis][i].number())*(f-i));
    const auto x=c["start"][0].number(),q=(root.x-x)/-x;const auto desired=p.from+p.forward*(p.distance*q),offset=p.forward*root.x+p.left*root.y+p.up*root.z;return {desired-offset*p.scale,f/(count-1)*255};
}
void network(double t){
    std::set<std::string> wanted;
    for(auto& [w,s]:states)syncHand(*s);
    for(const auto& [w,s]:states)if(isAlive(*s)){
        const auto n=read<int>(w+0x218);if(n!=1&&n!=2)continue;const auto yaw=n==1?s->anchorYaw.value():ownerYaw(*s);const auto o=origin(*s),feet=n==1?s->anchor.value():Vec3{o.x,o.y,vec(s->owner.pev+0xd4).z+1};const auto ring=prop(*s,n==1?"FixedDrone_GroundEffectScale":"AttachDrone_GroundEffectScale");
        for(const auto& item:std::array<std::tuple<const char*,Vec3,double>,3>{{{"totem",o,1},{"groundA",feet,ring},{"groundB",feet+Vec3{0,0,.2f},ring*.08}}}){
            const auto kind=std::get<0>(item);const auto key=std::string(kind)+"/"+std::to_string(w)+"/"+std::to_string(s->owner.serial);wanted.insert(key);VisualOptions v;v.scale=static_cast<float>(std::get<2>(item));v.angles={0,static_cast<float>(yaw),0};v.skin=s->colorUntil&&t<*s->colorUntil?1:0;World::set("wolf",key,cfg()["visual_assets"][kind]["path"].text(),std::get<1>(item),v);
        }
    }
    for(auto& f:queue){f.born=t;if(f.kind=="wolf")f.plan=plan(f);effects[f.key]=std::move(f);}queue.clear();
    for(auto i=effects.begin();i!=effects.end();){const auto& key=i->first;const auto& f=i->second;const auto age=t-f.born;if(age<0||age>=f.duration){World::erase("wolf",key);i=effects.erase(i);continue;}
        const auto& meta=cfg()["visual_assets"][f.kind];Vec3 position=f.from;double frame;
        if(f.plan){const auto result=sample(*f.plan,age);position=result.first;frame=result.second;}else frame=meta["type"].text()=="sprite"?std::min(meta["frames"].number()-1,age*30):std::min(255.,age/meta["duration"].number()*255);
        VisualOptions v;v.scale=static_cast<float>(f.scale);v.frame=static_cast<float>(frame);v.sequence=f.sequence;v.angles=f.plan?f.plan->angles:Vec3{};v.renderMode=f.kind=="wolf"?2:meta["type"].text()=="sprite"?5:0;
        if(f.kind=="wolf"&&f.fadeOut>0){const auto fade=std::min(f.duration,f.fadeOut);v.alpha=static_cast<float>(std::round(255*clamp((f.duration-age)/fade,0,1)));}wanted.insert(key);World::set("wolf",key,meta["path"].text(),position,v);++i;
    }World::retain("wolf",wanted);
}
void cleanup(){
    World::clear("wolf");effects.clear();queue.clear();bites.clear();for(auto& [w,s]:states){restoreHand(*s);if(validState(*s)){s->rush.reset();s->colorStarted.reset();s->colorUntil.reset();if(read<int>(w+0x218)!=0||read<unsigned char>(w+0x220))animation(*s,0);write(w+0x218,0);write(w+0x21c,0);write(w+0x220,static_cast<unsigned char>(0));write(w+0x238,-1);s->knownState=0;s->anchor.reset();s->anchorYaw.reset();}}
    for(const auto& [o,s]:slows)StatusBroker::release("wolf",s.e);slows.clear();cleaned=true;
}
void tick(){
    if(cleaned)return;if(!active||stopping){cleanup();return;}const auto t=now();if(lastTime!=t){lastTime=t;for(const auto& [w,s]:states)frame(s,t);biteFrame(t);
        for(auto i=slows.begin();i!=slows.end();){const auto& s=i->second;if(!valid(s.e)||t<s.from||t>=s.until||read<float>(s.e.pev+0x178)<=0){StatusBroker::release("wolf",s.e);i=slows.erase(i);}else{StatusBroker::set("wolf",s.e,0x240,s.speed);++i;}}}
    network(t);
}
struct Hud {Address owner{},out{};unsigned serial{};};
}
bool wolfClean(){return wolf::cleaned;}
void installWolf(){
    using namespace wolf;const std::string howl="weapons/wolfpack_fire_fx.wav";if(readText(at(0x191ca88),howl.size())!=howl)throw std::runtime_error("Wolf howl sample");
    World::onMapReset([]{states.clear();slows.clear();bites.clear();queue.clear();effects.clear();lastTime=std::numeric_limits<double>::quiet_NaN();});
    for(const Address rva:{0x1281070u,0x12811e0u,0x1281350u,0x1281380u})Hooks::attach(at(rva),"wolf",[](Invocation& v){if(active&&!stopping)state(v.registers->ecx);});
    Hooks::attach(at(0x13f8a80),"wolf",{},[](Invocation&){tick();});
    Hooks::attach(at(0x13f1fe0),"wolf",[](Invocation& v){auto& h=v.createState<Hud>();h.owner=v.argument<Address>(0);h.out=v.argument<Address>(1);h.serial=read<unsigned>(h.owner+4);},[](Invocation& v){
        const auto h=v.state<Hud>();if(!active||stopping||!v.registers->eax||!h.out||read<unsigned>(h.out)!=613||read<int>(h.owner)!=0||read<unsigned>(h.owner+4)!=h.serial)return;
        for(const auto& [w,s]:states)if(s->owner.edict==h.owner&&s->owner.serial==h.serial&&isActive(*s)&&isAlive(*s)){const bool ready=read<int>(w+0x168)>=prop(*s,"C_GaugeRequire");const auto before=read<unsigned>(h.out+0x34);write(h.out+0x34,ready?(before|4):(before&~4u));break;}
    });
}
}
