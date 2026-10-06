#include "brokers.hpp"
#include "hooks.hpp"
#include <optional>

namespace csnz {
namespace {
std::vector<std::pair<std::string,std::function<bool(const DamageContext&)>>> defenses;
DamageFn originalDamage{};
thread_local unsigned damageDepth=0;
int __fastcall damageDispatch(Address victim,void*,Address inflictor,Address attacker,float amount,int bits,Address info){
    std::lock_guard<std::recursive_mutex> lock(gameplayMutex);
    struct Depth{Depth(){++damageDepth;}~Depth(){--damageDepth;}} depth;
    bool blocked=false;
    if(active&&!stopping&&damageDepth<=16&&std::isfinite(amount)&&amount>0&&amount<10000000){
        try{const DamageContext ctx{victim,inflictor,attacker,amount,bits,info};for(const auto& a:defenses)blocked=a.second(ctx)||blocked;}
        catch(const std::exception& e){blocked=false;fail(e.what());}catch(...){blocked=false;fail("Damage broker exception");}
    }
    if(blocked&&active&&!stopping)return 0;
    return originalDamage(victim,inflictor,attacker,amount,bits,info);
}
std::vector<ArmorAdapter> armors;
std::set<Address> pending;
struct Choice {std::size_t adapter;ArmorContext context;};
struct ArmorFrame {
    std::vector<Choice> choices;
    std::optional<ArmorContext> selected;
    std::array<unsigned char,32> info{};
    double before{},amount{};bool scoped=false,vetoed=false;
    std::size_t adapter{};std::any token;
};
thread_local std::vector<std::shared_ptr<ArmorFrame>> armorStack;
const Config& cfg(){return profiles()["shared"]["predeath"];}
bool inArray(Address value,const Config& a){for(const auto& x:a.array())if(value==at(x.u32()))return true;return false;}
Address scope(){
    const auto& m=cfg()["manager"];
    if(ptr(at(m["callback_slot"].u32()))!=hat(m["callback_rva"].u32()))return 0;
    const auto manager=hat(m["object_rva"].u32()),vt=ptr(manager);
    if(ptr(vt+0x188)!=hat(m["mode_getter_rva"].u32())||ptr(vt+0x238)!=hat(m["secondary_getter_rva"].u32()))return 0;
    if(read<unsigned char>(manager+0x34a)!=55||read<unsigned char>(manager+0x3d1)!=0)return 0;
    const auto rules=ptr(at(0x21cac30));if(!rules||ptr(rules)!=at(cfg()["rules_vtable"].u32())||ptr(ptr(rules)+94*4)!=at(0x871b50))return 0;
    return rules;
}
std::optional<ArmorContext> identity(Address victim,Address weapon,const ArmorContext* old=nullptr){
    if(!victim||!weapon||!inArray(ptr(victim),cfg()["player_vtables"]))return {};
    ArmorContext s;static_cast<Entity&>(s)=entity(victim);s.activeWeapon=ptr(victim+0x1024);s.weapon=weapon;
    if(ptr(weapon+0xd0)!=victim)return {};s.id=read<unsigned>(weapon+0xe4);const auto id=std::to_string(s.id);const auto& tables=cfg()["weapon_vtables"];
    if(!selected(s.id)||!tables.has(id)||!inArray(ptr(weapon),tables[id]))return {};
    const auto we=entity(weapon);s.weaponEdict=we.edict;s.weaponSerial=we.serial;
    if(old&&(old->object!=victim||old->pev!=s.pev||old->edict!=s.edict||old->serial!=s.serial||old->weapon!=weapon||old->weaponEdict!=we.edict||old->weaponSerial!=we.serial||old->id!=s.id))return {};
    return s;
}
struct Health {double hp;unsigned dead,flags;};
Health health(const ArmorContext& s){return {read<float>(s.pev+0x178),read<unsigned>(s.pev+0x194),read<unsigned>(s.pev+0x1c8)};}
void baseEnter(Invocation& v){
    auto f=std::make_shared<ArmorFrame>();v.createState<std::shared_ptr<ArmorFrame>>(f);armorStack.push_back(f);
    if(!active||stopping||!inArray(v.returnAddress,cfg()["base_return_addresses"])||!scope())return;
    for(std::size_t i=0;i<armors.size();i++){const auto victim=v.registers->ecx;auto s=identity(victim,armors[i].resolveWeapon(victim));if(s)f->choices.push_back({i,*s});}
    if(f->choices.empty())return;
    const auto h=health(f->choices.front().context);const auto amount=v.argument<float>(2);const auto info=v.argument<Address>(4);
    if(!std::isfinite(h.hp)||h.hp<=0||h.dead!=0||(h.flags&0x04000000)||!std::isfinite(amount)||amount<=0||amount>=10000000||!info)return;
    if(!rawRead(info,f->info.data(),32))throw std::runtime_error("Predeath info read");f->before=h.hp;f->amount=amount;f->scoped=true;
}
void baseLeave(Invocation& v){
    auto f=v.state<std::shared_ptr<ArmorFrame>>();
    // Pop even when validation/commit throws; never retain a dead transaction.
    struct Pop {std::shared_ptr<ArmorFrame> f;~Pop(){if(armorStack.empty()||armorStack.back()!=f){armorStack.clear();fail("Armor stack mismatch");}else armorStack.pop_back();}} pop{f};
    if(!f->vetoed||!f->selected)return;const auto old=*f->selected;pending.erase(old.object);
    auto s=identity(old.object,old.weapon,&old);if(!active||stopping||!scope()||!s)return;const auto h=health(*s);
    if(std::isfinite(h.hp)&&h.hp>0&&h.dead==0){armors[f->adapter].commit(old,f->token);log("ARMOR_SURVIVAL_CONFIRMED weapon="+std::to_string(old.id));}
}
void rulesEnter(Invocation& v){
    v.createState<std::shared_ptr<ArmorFrame>>();if(!active||stopping||armorStack.empty())return;auto f=armorStack.back();
    if(!f->scoped||f->vetoed||v.returnAddress!=at(0x1407aa8)||v.argument<Address>(0)!=f->choices.front().context.object)return;
    const auto rules=scope();if(!rules||v.registers->ecx!=rules)return;
    bool any=false;for(const auto& c:f->choices)if(identity(c.context.object,c.context.weapon,&c.context)){any=true;break;}if(!any)return;
    // By-value 32-byte info follows victim; not an info pointer.
    std::array<unsigned char,32> copy{};if(!rawRead(v.entryStack+8,copy.data(),32)||copy!=f->info)return;
    const auto h=health(f->choices.front().context);if(!std::isfinite(h.hp)||h.hp>0||h.dead!=0||(h.flags&0x04000000))return;
    v.state<std::shared_ptr<ArmorFrame>>()=f;
}
void rulesLeave(Invocation& v){
    auto f=v.state<std::shared_ptr<ArmorFrame>>();if(!active||stopping||!f||(v.registers->eax&255)!=0||!scope())return;
    const auto victim=f->choices.front().context.object;if(pending.count(victim))return;
    const auto h=health(f->choices.front().context);if(!std::isfinite(h.hp)||h.hp>0||h.dead!=0||(h.flags&0x04000000))return;
    struct Match {std::size_t adapter;ArmorContext context;std::any token;};std::vector<Match> matches;
    for(const auto& c:f->choices){auto s=identity(c.context.object,c.context.weapon,&c.context);if(!s)continue;s->time=now();s->before=f->before;s->pendingHp=h.hp;s->damage=f->amount;auto token=armors[c.adapter].select(*s);if(token.has_value())matches.push_back({c.adapter,*s,std::move(token)});}
    if(matches.empty())return;const auto activeWeapon=ptr(victim+0x1024);
    std::stable_sort(matches.begin(),matches.end(),[&](const Match& a,const Match& b){const bool aa=a.context.weapon==activeWeapon,bb=b.context.weapon==activeWeapon;if(aa!=bb)return aa;return std::tie(a.context.id,a.context.weapon)<std::tie(b.context.id,b.context.weapon);});
    auto& chosen=matches.front();f->selected=chosen.context;f->adapter=chosen.adapter;f->token=std::move(chosen.token);
    v.registers->eax=(v.registers->eax&0xffffff00)|1;f->vetoed=true;pending.insert(victim);
}
}
void DamageBroker::add(const char* family,std::function<bool(const DamageContext&)> fn){for(const auto& a:defenses)if(a.first==family)throw std::runtime_error("Duplicate defense");defenses.emplace_back(family,std::move(fn));}
void DamageBroker::install(){Hooks::replace(at(0x14d7030),reinterpret_cast<void*>(&damageDispatch),reinterpret_cast<void**>(&originalDamage),"shared.damage");}
void ArmorGate::add(ArmorAdapter a){for(const auto& x:armors)if(x.family==a.family)throw std::runtime_error("Duplicate armor adapter");armors.push_back(std::move(a));}
void ArmorGate::install(){if(armors.empty())return;Hooks::attach(at(0x1407280),"shared.armor",baseEnter,baseLeave);Hooks::attach(at(0x871b50),"shared.armor",rulesEnter,rulesLeave);}
}
