#include "world.hpp"

namespace csnz {
namespace {
struct Model {std::string path;int index{};const Config* meta{};};
struct Sound {std::string path,file;int index{};const Config* meta{};};
std::map<std::string,Model> models;std::map<std::string,Sound> sounds;
std::map<std::string,unsigned> events;
std::map<std::string,WorldEntity> entities;
std::vector<std::function<void()>> mapResets;
bool loading=false,modelsReady=false,stopped=false;int lastState=-1;unsigned epoch=0;
std::vector<double> trailLeases;double trailPacketTime=-1;unsigned trailPacketCount=0;
struct Network {
    Address(__cdecl* load)(const char*,int*){};void(__cdecl* free)(Address){};
    int(__cdecl* precache)(const char*){};int(__cdecl* precacheSound)(const char*){};
    void(__cdecl* origin)(Address,const Vec3*){};void(__cdecl* remove)(Address){};
    Address(__cdecl* create)(const char*,const Vec3*,int){};
    void(__cdecl* begin)(int,int,Address,Address){};void(__cdecl* end)(){};
    void(__cdecl* byte)(int){};void(__cdecl* shortValue)(int){};
    void(__cdecl* coord)(float){};void(__cdecl* longValue)(int){};
    int(__cdecl* entityIndex)(Address){};unsigned(__cdecl* eventPrecache)(int,const char*){};
    void(__cdecl* eventPlayback)(int,Address,int,float,const Vec3*,const Vec3*,float,float,int,int,int,int){};
    void(__cdecl* ambient)(Address,const Vec3*,const char*,float,float,int,int){};
} api;
template<class T> void bind(T& field,unsigned slot,unsigned rva){const auto p=ptr(at(slot));if(p!=hat(rva))throw std::runtime_error("Network engine binding");field=native<T>(p);}
void verifyFile(const std::string& path,const Config& meta){
    int length=0;const auto bytes=api.load(path.c_str(),&length);if(!bytes)throw std::runtime_error("Missing asset: "+path);
    bool okay=false;try{std::array<unsigned char,16> head{};if(!rawRead(bytes,head.data(),head.size()))throw std::runtime_error("Asset header read");std::string hex;constexpr char h[]="0123456789abcdef";for(auto b:head){hex+=h[b>>4];hex+=h[b&15];}okay=length==meta["bytes"].integer()&&hex==meta["head16"].text();}catch(...){api.free(bytes);throw;}api.free(bytes);
    if(!okay)throw std::runtime_error("Asset identity: "+path);
}
void killTrail(WorldEntity& v){if(!v.trailEntity)return;api.begin(2,23,0,0);api.byte(99);api.shortValue(v.trailEntity);api.end();v.trailEntity=0;}
void erase(const std::string& key){const auto i=entities.find(key);if(i==entities.end())return;auto v=i->second;entities.erase(i);if(World::valid(v)){killTrail(v);api.remove(v.edict);}}
bool encodable(Vec3 p){return std::abs(p.x)<=4095.75&&std::abs(p.y)<=4095.75&&std::abs(p.z)<=4095.75;}
}
void World::initialize(){
    bind(api.load,0x21d3be8,0x6dcbf0);bind(api.free,0x21d3bec,0x6dc290);bind(api.precache,0x21d3a5c,0x740f00);bind(api.precacheSound,0x21d3a60,0x741150);
    bind(api.origin,0x21d3ad0,0x741300);bind(api.remove,0x21d3ab8,0x73f600);api.create=native<decltype(api.create)>(at(0x14759f0));
    bind(api.begin,0x21d3b1c,0x73f180);bind(api.end,0x21d3b20,0x73f260);bind(api.byte,0x21d3b24,0x73fc80);bind(api.shortValue,0x21d3b2c,0x73fe10);
    // The float writer at 21d3b38 is NOT Coord16 in this build.
    bind(api.coord,0x21d3b40,0x73fce0);bind(api.longValue,0x21d3b30,0x73fde0);bind(api.entityIndex,0x21d3b88,0x7455e0);
    bind(api.eventPrecache,0x21d3c58,0x73dff0);bind(api.eventPlayback,0x21d3c5c,0x73da70);bind(api.ambient,0x21d3ad8,0x7402b0);
}
void World::onMapReset(std::function<void()> fn){mapResets.push_back(std::move(fn));}
void World::precache(){
    if(stopped||loading)return;const auto state=read<int>(hat(0x256dd4c));if(state!=1){lastState=state;return;}
    if(lastState!=1){models.clear();sounds.clear();events.clear();entities.clear();trailLeases.clear();trailPacketTime=-1;trailPacketCount=0;modelsReady=false;lastState=1;++epoch;for(const auto& fn:mapResets)fn();}
    if(modelsReady)return;loading=true;
    struct End {~End(){loading=false;}} end;
    auto addModel=[&](const std::string& path,const Config& meta){if(models.count(path))return;verifyFile(path,meta);auto& m=models.emplace(path,Model{path,0,&meta}).first->second;m.index=api.precache(m.path.c_str());if(m.index<=0||m.index>=6144)throw std::runtime_error("Model index");};
    for(const auto& [path,meta]:profiles()["shared"]["network_assets"].object())addModel(path,meta);
    for(const char* family:{"leapstrike","soul","frost"})if(familySelected(family)){
        const auto& p=profiles()[family];for(const auto& [key,meta]:p["visual_assets"].object())addModel(meta["path"].text(),meta);
        for(const auto& [key,meta]:p["sound_assets"].object()){
            const auto path=meta["path"].text(),file=meta["file"].text();verifyFile(file,meta);
            auto& s=sounds.emplace(std::string(family)+"/"+key,Sound{path,file,0,&meta}).first->second;s.index=api.precacheSound(s.path.c_str());if(s.index<0||s.index>=2560)throw std::runtime_error("Sound index");
        }
    }
    for(const auto& name:profiles()["shared"]["network_events"].array()){auto [i,_]=events.emplace(name.text(),0);i->second=api.eventPrecache(1,i->first.c_str());if(i->second<1||i->second>=512)throw std::runtime_error("Event precache");}
    modelsReady=true;log("NETWORK_MODELS_READY epoch="+std::to_string(epoch)+" count="+std::to_string(models.size()));
}
bool World::ready(){return modelsReady&&!stopped;}
bool World::has(const std::string& path){return models.count(path)!=0;}
int World::modelIndex(const std::string& path){return models.at(path).index;}
bool World::valid(const WorldEntity& v) noexcept {try{return csnz::valid(v)&&ptr(v.object)==at(0x196daa4)&&read<unsigned short>(v.pev+0xc4)==v.modelIndex;}catch(...){return false;}}
WorldEntity* World::set(const std::string& family,const std::string& key,const std::string& path,Vec3 position,const VisualOptions& o){
    if(stopped)return nullptr;if(!modelsReady)throw std::runtime_error("Visual before precache");if(!finite(position)||!finite(o.angles))throw std::runtime_error("Network vector");
    const auto mi=models.find(path);if(mi==models.end())throw std::runtime_error("Unregistered model: "+path);const auto& model=mi->second;const auto id=family+"/"+key;
    auto i=entities.find(id);if(i!=entities.end()&&(!valid(i->second)||i->second.model!=path)){::csnz::erase(id);i=entities.end();}
    if(i==entities.end()){
        const bool separate=family=="soul"||family=="frost"||family=="leapstrike";
        const auto used=std::count_if(entities.begin(),entities.end(),[&](const auto& pair){const auto& f=pair.second.family;return separate?f==family:f!="soul"&&f!="frost"&&f!="leapstrike";});
        const auto limit=separate?(family=="leapstrike"?192:128):384;if(used>=limit)return nullptr;const auto object=api.create(model.path.c_str(),&position,0);if(!object)return nullptr;
        WorldEntity v;static_cast<Entity&>(v)=entity(object);v.modelIndex=model.index;v.model=path;v.family=family;
        if(!valid(v)){if(v.edict)api.remove(v.edict);throw std::runtime_error("Created sprite identity");}
        i=entities.emplace(id,std::move(v)).first;const auto p=i->second.pev;
        write(p+0x114,0.f);write(p+0x118,0);write(p+0x11c,0);write(p+0x128,0u);write(p+0x148,0.f);putVec(p+0x168,{255,255,255});write(p+0x174,0);
    }
    const auto p=i->second.pev;const auto t=now();
    if(!std::isfinite(o.scale)||!std::isfinite(o.alpha)||!std::isfinite(o.frame)||o.scale<=0||o.scale>128||o.alpha<0||o.alpha>255||o.frame<0||o.frame>=256||o.sequence<0||o.sequence>255)throw std::runtime_error("Network animation range");
    write(p+0x138,o.sequence);write(p+0x140,o.frame);write(p+0x144,static_cast<float>(t));write(p+0x120,o.skin);write(p+0x124,o.body);
    putVec(p+0x154,{o.scale,o.scale,o.scale});const int render=o.renderMode>=0?o.renderMode:model.meta->operator[]("type").text()=="sprite"?5:o.alpha<255?2:0;
    write(p+0x160,render);write(p+0x164,o.alpha);putVec(p+0x50,o.angles);api.origin(i->second.edict,&position);return &i->second;
}
void World::erase(const std::string& family,const std::string& key){::csnz::erase(family+"/"+key);}
void World::retain(const std::string& family,const std::set<std::string>& keys){std::vector<std::string> stale;for(const auto& [key,v]:entities)if(v.family==family&&!keys.count(key.substr(family.size()+1)))stale.push_back(key);for(const auto& key:stale)::csnz::erase(key);}
void World::clear(const std::string& family){retain(family,{});}
void World::stop(){stopped=true;while(!entities.empty())::csnz::erase(entities.begin()->first);}
bool World::follow(const std::string& family,const std::string& key,const std::string& sprite,double life,double width,Vec3 rgb,double brightness){
    auto i=entities.find(family+"/"+key);const auto mi=models.find(sprite);if(i==entities.end()||!valid(i->second)||mi==models.end())return false;auto& v=i->second;if(v.trailEntity)return true;
    if(!finite(rgb)||!std::isfinite(life)||!std::isfinite(width)||!std::isfinite(brightness)||life<=0||life>25.5||width<1||width>255||rgb.x<0||rgb.y<0||rgb.z<0||rgb.x>255||rgb.y>255||rgb.z>255||brightness<0||brightness>255)throw std::runtime_error("Follow trail range");
    const auto index=api.entityIndex(v.edict);if(index<1||index>=32768||mi->second.index>=32768)throw std::runtime_error("Follow entity index");
    api.begin(2,23,0,0);api.byte(22);api.shortValue(index);api.shortValue(mi->second.index);api.byte(std::max(1,static_cast<int>(std::round(life*10))));api.byte(static_cast<int>(std::round(width)));for(double x:{double(rgb.x),double(rgb.y),double(rgb.z),brightness})api.byte(static_cast<int>(std::round(x)));api.end();v.trailEntity=index;return true;
}
TrailResult World::trailSpan(const std::string& sprite,Vec3 from,Vec3 to,double life,double width,Vec3 rgb,double brightness){
    if(stopped)return TrailResult::Stopped;const auto i=models.find(sprite);if(!modelsReady||i==models.end()||i->second.meta->operator[]("type").text()!="sprite")throw std::runtime_error("Trail sprite unavailable");
    if(!finite(from)||!finite(to)||!finite(rgb)||!std::isfinite(life)||!std::isfinite(width)||!std::isfinite(brightness)||life<=0||life>25.5||width<=0||width>25.5||rgb.x<0||rgb.y<0||rgb.z<0||rgb.x>255||rgb.y>255||rgb.z>255||brightness<0||brightness>255)throw std::runtime_error("Trail span parameters");
    if(length(to-from)<.125)return TrailResult::Degenerate;if(!encodable(from)){if(encodable(to))std::swap(from,to);else return TrailResult::Range;}
    const auto t=now();trailLeases.erase(std::remove_if(trailLeases.begin(),trailLeases.end(),[&](double until){return until<=t;}),trailLeases.end());
    if(t!=trailPacketTime){trailPacketTime=t;trailPacketCount=0;}if(trailLeases.size()>=64||trailPacketCount>=8)return TrailResult::Budget;
    const auto ticks=std::max(1,static_cast<int>(std::round(life*10)));
    api.begin(2,23,0,0);api.byte(148);for(unsigned j=0;j<3;j++)api.coord(from[j]);for(unsigned j=0;j<3;j++){int bits;const float f=to[j];std::memcpy(&bits,&f,4);api.longValue(bits);}
    api.shortValue(i->second.index);for(const int b:{0,0,ticks,static_cast<int>(std::round(width*10)),0,static_cast<int>(std::round(rgb.x)),static_cast<int>(std::round(rgb.y)),static_cast<int>(std::round(rgb.z)),static_cast<int>(std::round(brightness)),0,4})api.byte(b);api.end();
    trailLeases.push_back(t+ticks*.1+.25);++trailPacketCount;return TrailResult::Sent;
}
void World::event(const std::string& name,Address owner,Vec3 origin,Vec3 direction,int code,int variant){
    if(stopped)return;const auto e=events.find(name);if(!modelsReady||e==events.end())throw std::runtime_error("Event unavailable");
    if(!owner||read<int>(owner)!=0||(code!=2&&code!=3)||variant<0||variant>2||!finite(origin)||!finite(direction))throw std::runtime_error("Divine event identity");
    api.eventPlayback(0,owner,e->second,0,&origin,&direction,0,0,0,variant,0,code);
}
void World::ambient(const std::string& family,const std::string& key,Vec3 position,float volume,float attenuation){if(stopped)return;const auto& s=sounds.at(family+"/"+key);if(!modelsReady||!finite(position))throw std::runtime_error("Sound state");api.ambient(engine.byIndex(0),&position,s.path.c_str(),volume,attenuation,0,100);}
unsigned World::size(){return static_cast<unsigned>(entities.size());}
}
