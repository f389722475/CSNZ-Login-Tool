#include "families.hpp"
#include "hooks.hpp"
#include "game.hpp"
#include "world.hpp"

namespace csnz {
namespace {
struct Scope {Address weapon{};unsigned id{};};
thread_local std::vector<Scope> scopes;
struct Spawn {Address modelName{};int spriteIndex{};};
void(__cdecl* setModel)(Address,Address){};
void enterAction(Invocation& v){
    v.createState<bool>(false);if(!active||stopping||!weaponIs(v.registers->ecx,"halo"))return;
    const auto w=v.registers->ecx;entity(ptr(w+0xd0));scopes.push_back({w,read<unsigned>(w+0xe4)});v.state<bool>()=true;
}
void leaveAction(Invocation& v){if(v.state<bool>()){if(scopes.empty())throw std::runtime_error("Halo scope stack");scopes.pop_back();}}
void createEnter(Invocation& v){
    auto& spawn=v.createState<Spawn>();if(!active||stopping||scopes.empty())return;const auto& s=scopes.back();
    if(s.id!=591||v.returnAddress!=at(0xcdc623)||v.argument<Address>(0)!=at(0x1850384))return;
    if(!weaponIs(s.weapon,"halo"))throw std::runtime_error("Halo alias weapon identity");const auto owner=entity(ptr(s.weapon+0xd0));
    if(ptr(owner.object+0x1024)!=s.weapon||v.argument<Address>(3)!=owner.edict)throw std::runtime_error("Halo alias owner mismatch");
    if(readText(at(0x185039c),19)!=std::string("halogun_projectile\0",19))throw std::runtime_error("Halo alias literal");
    if(!World::ready())return;
    const int model=World::modelIndex("models/ef_halogunex_projectile.mdl"),sprite=World::modelIndex("sprites/ef_halogunex_projectile.spr");
    const auto name=ptr(hat(0x2493430+model*4));if(!name||!ptr(hat(0x2499430+model*4))||!ptr(hat(0x2499430+sprite*4)))return;
    if(readText(name,34)!="models/ef_halogunex_projectile.mdl")throw std::runtime_error("Halo engine-owned name");
    spawn={name,sprite};v.argument<Address>(0,at(0x185039c));
}
void createLeave(Invocation& v){
    const auto s=v.state<Spawn>();const auto object=v.registers->eax;if(!s.modelName||!object||!active||stopping)return;
    if(ptr(object)!=at(0x184fc88))throw std::runtime_error("Halo native projectile vtable");const auto e=entity(object);
    // Engine string storage, never our heap pointer: SetModel stores an offset.
    setModel(e.edict,s.modelName);write(object+0x140,s.spriteIndex);
}
}
void installHalo(){
    const auto p=ptr(at(0x21d3a64));if(p!=hat(0x741250))throw std::runtime_error("Halo SetModel binding");setModel=native<decltype(setModel)>(p);
    for(const Address rva:{0xcce020u,0xcce180u,0xcdc3c0u})Hooks::attach(at(rva),"halo",enterAction,leaveAction);
    Hooks::attach(at(0x13e6940),"halo",createEnter,createLeave);
    // Physics, damage and expiration remain the existing native projectile.
    // The original 537 Arbalest is an unchanged reference variant, not patched.
}
}
