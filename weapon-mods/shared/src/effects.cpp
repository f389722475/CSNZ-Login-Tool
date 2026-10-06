#include "world.hpp"

namespace csnz {
EffectRecord* EffectBank::ensure(const std::string& key,unsigned id,const std::string& kind,Vec3 position,double scale,double t){
    if(!World::ready())return nullptr;auto i=records.find(key);if(i!=records.end()&&!valid(i->second.entity)){World::erase(family_,key);records.erase(i);i=records.end();}
    if(i==records.end()){
        if(records.size()>=limit_)return nullptr;const auto& assets=profiles()[family_]["visual_assets"];const auto asset=std::to_string(id)+"/"+kind;if(!assets.has(asset))return nullptr;
        const auto& cfg=assets[asset];EffectRecord r;r.key=key;r.id=id;r.kind=kind;r.metadata=&cfg;r.position=position;r.born=r.last=t;r.until=t+cfg["duration"].number();r.options.scale=static_cast<float>(scale);
        r.options.renderMode=cfg.has("renderMode")?cfg["renderMode"].integer():cfg["type"].text()=="sprite"?5:(family_=="soul"||family_=="frost")?4:0;
        if(kind=="pulse")r.options.angles={90,0,0};
        const auto v=World::set(family_,key,cfg["path"].text(),position,r.options);if(!v)return nullptr;r.entity=*v;write(v->pev+0x134,0);i=records.emplace(key,std::move(r)).first;
    }
    i->second.position=position;return &i->second;
}
void EffectBank::tick(double t,const std::set<std::string>& wanted){
    for(auto i=records.begin();i!=records.end();){auto& r=i->second;
        if(!valid(r.entity)){i=records.erase(i);continue;}
        if(t<r.last||t>=r.until||(r.sticky&&!wanted.count(r.key))){World::erase(family_,r.key);i=records.erase(i);continue;}
        if(!r.manualFrame){const auto& c=*r.metadata;const auto count=c["frames"].number(),frame=std::max(0.,t-r.born)*c["fps"].number();
            const double f=c["type"].text()=="sprite"?(r.loop?std::fmod(frame,count):std::min(count-1,frame)):std::min(255.999,(r.loop?std::fmod(frame,count-1):std::min(count-1,frame))*256/(count-1));r.options.frame=static_cast<float>(f);}
        const auto entity=World::set(family_,r.key,(*r.metadata)["path"].text(),r.position,r.options);if(entity)r.entity=*entity;r.last=t;++i;
    }
}
void EffectBank::clear(){World::clear(family_);records.clear();}
}
