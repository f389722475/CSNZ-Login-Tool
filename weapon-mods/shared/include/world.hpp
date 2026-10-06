#pragma once
#include "game.hpp"

namespace csnz {
struct VisualOptions {Vec3 angles{};float scale=1,alpha=255,frame=0;int sequence=0,skin=0,body=0,renderMode=-1;};
struct WorldEntity : Entity {int modelIndex{};std::string model,family;int trailEntity{};};
enum class TrailResult {Sent,Budget,Range,Degenerate,Stopped};
class World {
public:
    static void initialize();
    static void onMapReset(std::function<void()>);
    static void precache();
    static bool ready();
    static bool has(const std::string& path);
    static int modelIndex(const std::string& path);
    static bool valid(const WorldEntity&) noexcept;
    static WorldEntity* set(const std::string& family,const std::string& key,const std::string& path,Vec3 position,const VisualOptions& = {});
    static void erase(const std::string& family,const std::string& key);
    static void retain(const std::string& family,const std::set<std::string>& keys);
    static void clear(const std::string& family);
    static void stop();
    static bool follow(const std::string& family,const std::string& key,const std::string& sprite,double life,double width,Vec3 rgb,double brightness);
    static TrailResult trailSpan(const std::string& sprite,Vec3 from,Vec3 to,double life,double width,Vec3 rgb,double brightness);
    static void event(const std::string& name,Address owner,Vec3 origin,Vec3 angles,int code,int variant);
    static void ambient(const std::string& family,const std::string& key,Vec3 position,float volume=1,float attenuation=.8f);
    static unsigned size();
};

struct EffectRecord {
    std::string key,kind;unsigned id{};const Config* metadata{};Entity entity;
    Vec3 position;VisualOptions options;double born{},last{},until{};bool loop=false,sticky=false,manualFrame=false;
};
// The three existing server sprite owners share the same lifetime/animation
// contract. Gameplay chooses keys, timing, position and any frame override.
class EffectBank {
    std::string family_;std::size_t limit_;
public:
    std::map<std::string,EffectRecord> records;
    EffectBank(std::string family,std::size_t limit):family_(std::move(family)),limit_(limit){}
    EffectRecord* ensure(const std::string& key,unsigned id,const std::string& kind,Vec3 position,double scale,double t);
    void tick(double t,const std::set<std::string>& wanted={});
    void clear();
    void forget(){records.clear();}
};
}
