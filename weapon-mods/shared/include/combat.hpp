#pragma once
#include "game.hpp"
#include <optional>

namespace csnz {
struct CombatTarget {Entity e;Vec3 center;unsigned flags{};};
inline Vec3 segmentPoint(Vec3 p,Vec3 a,Vec3 b){const auto d=b-a;const auto dd=dot(d,d);return a+d*(dd?clamp(dot(p-a,d)/dd,0,1):0);}
inline Vec3 closest(const Entity& e,Vec3 p){const auto lo=vec(e.pev+0xd4),hi=vec(e.pev+0xe0);for(unsigned i=0;i<3;i++)p[i]=std::max(lo[i],std::min(hi[i],p[i]));return p;}
inline bool inFan(Vec3 p,Vec3 origin,Vec3 forward,double radius,double fullAngle){const auto d=p-origin;return length(d)<=radius&&dot(unit(d,{1,0,0}),unit(forward,{1,0,0}))>=std::cos(fullAngle*Pi/360);}
inline bool visible(const Entity& owner,Vec3 origin,const CombatTarget& e){const auto tr=trace(origin,e.center,owner.edict);return tr.fraction>=.999||tr.edict==e.e.edict;}
std::vector<CombatTarget> combatSphere(const Entity& owner,Vec3 origin,double radius);
inline Aim combatAim(const Entity& owner){Aim a;a.angles=vec(owner.pev+0x68)+vec(owner.pev+0x74);a.source=vec(owner.pev+8)+vec(owner.pev+0x198);a.direction=direction(a.angles);return a;}
inline void attackTimers(Address w,double primary,double secondary){const auto wt=engine.weaponTime();for(const auto& [offset,delay]:std::array<std::pair<unsigned,double>,2>{{{0x134,primary},{0x13c,secondary}}}){const auto old=read<float>(w+offset);const auto t=static_cast<float>(wt+delay);write(w+offset,t);write(w+offset+4,t-old);}}
inline void idleTimer(Address w,double duration){const auto wt=engine.weaponTime(),old=read<float>(w+0x144);const auto t=static_cast<float>(wt+duration);write(w+0x144,t);write(w+0x148,t-old);}
}
