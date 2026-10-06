#pragma once
#include "game.hpp"
#include <any>

namespace csnz {
class StatusBroker {
public:
    static bool set(const std::string& family,const Entity&,unsigned offset,double value);
    static void release(const std::string& family,const Entity&);
    static void releaseAll(const std::string& family);
    static void maintain(double time);
    static void forget(); // map epoch changed: never restore through stale edicts
    static unsigned size();
};
struct DamageContext {Address victim{},inflictor{},attacker{};float damage{};int bits{};Address info{};};
class DamageBroker {
public:
    static void add(const char* family,std::function<bool(const DamageContext&)>);
    static void install();
};
struct ArmorContext : Entity {Address weapon{},weaponEdict{},activeWeapon{};unsigned weaponSerial{},id{};double time{},before{},pendingHp{},damage{};};
struct ArmorAdapter {
    std::string family;
    std::function<Address(Address)> resolveWeapon;
    std::function<std::any(const ArmorContext&)> select;
    std::function<void(const ArmorContext&,const std::any&)> commit;
};
class ArmorGate {
public:
    static void add(ArmorAdapter);
    static void install();
};
}
