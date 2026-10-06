#include "combat.hpp"

namespace csnz {
std::vector<CombatTarget> combatSphere(const Entity& owner,Vec3 origin,double radius){
    if(!finite(origin)||!std::isfinite(radius)||radius<0)throw std::runtime_error("Combat sphere bounds");
    const auto world=engine.byIndex(0);std::set<Address> seen;std::vector<CombatTarget> out;Address ed=0;
    for(unsigned i=0;i<1024;i++){
        ed=engine.findSphere(ed,&origin,static_cast<float>(radius));if(!ed||ed==world)return out;if(!seen.insert(ed).second)throw std::runtime_error("Sphere iteration loop");
        std::optional<CombatTarget> candidate;
        try{if(read<int>(ed)!=0)continue;const auto object=ptr(ed+0x80);if(!object||object==owner.object)continue;const auto e=entity(object);if(read<float>(e.pev+0x190)==0||read<float>(e.pev+0x178)<=0)continue;candidate=CombatTarget{e,center(e),read<unsigned>(e.pev+0x1c8)};}catch(...){continue;}
        if(candidate&&(!(candidate->flags&8)||hostile(candidate->e,owner)))out.push_back(*candidate);
    }throw std::runtime_error("Sphere iteration overflow");
}
}
