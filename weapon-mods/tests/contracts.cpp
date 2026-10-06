#include "config.hpp"
#include "frost_policy.hpp"
#include "orb_samples.hpp"
#include "divine_policy.hpp"
#include <cstdio>
using namespace csnz;
static void check(bool okay,const char* name){if(!okay){std::printf("FAIL %s\n",name);ExitProcess(2);}}
int main(){
    const auto& p=profiles();check(p.object().size()==8,"eight compiled data profiles");check(p["shared"]["network_assets"].object().size()==29,"network model contracts");
    for(const auto& curve:p["wolf"]["rush_motion"]["sequences"].array()){check(curve["start"][0].number()<-.001,"wolf native root start");for(const char* axis:{"x","y","z"})check(curve[axis].array().size()==curve["frames"].u32(),"wolf curve frame count");}
    unsigned contacts=0;for(const auto& c:orbCases){const auto result=sweptCylinderContact(c.from,c.to,c.lo,c.hi,c.radius,c.height);check(result.has_value()==c.contact,"recorded Orb contact classification");if(result){++contacts;check(std::abs(result->fraction-c.fraction)<1e-5,"recorded Orb contact fraction");check(length(result->point-c.point)<.001,"recorded Orb contact position");}}
    check(contacts==22,"22 of 24 recorded Orb contacts");check(!sweptCylinderContact({0,0,0},{10,0,0},{4,-1,9},{6,1,11},2,4),"vertical non-contact");
    bool rejected=false;try{sweptCylinderContact({NAN,0,0},{10,0,0},{4,-1,-1},{6,1,1},2,4);}catch(...){rejected=true;}check(rejected,"NaN geometry rejected");
    const auto bytes=shieldGaugePayload(7,4);check(bytes==std::array<unsigned char,6>{2,7,0,0,128,64},"six-byte native Frost gauge");
    rejected=false;try{shieldGaugePayload(33,4);}catch(...){rejected=true;}check(rejected,"HUD owner bounds");
    using namespace divine_policy;
    check(!charge(20,1,4,1,20),"Divine max energy");check(!charge(5,6,5,1,20),"Divine charge deadline");
    const auto q=charge(5,1,99,1,20);check(q&&q->energy==6&&q->deadline==100,"Divine charge no catch-up");
    check(stage(2,10,10.5,1)==3&&stage(3,10,10.5,1)==0&&stage(2,10,12,1)==0,"Divine combo reset");
    check(inArc({1,0,0},{0,0,0},90)&&inArc({1,0,0},{1,1,0},90)&&!inArc({1,0,0},{-1,0,0},90),"Divine full-angle cone");
    std::vector<Node> nodes={{{0,0,0},0,0},{{12,0,0},.01,12}};
    check(spanFits(nodes,{{24,0,0},.02,24}),"Divine straight merge");check(!spanFits(nodes,{{6,0,0},.02,18}),"Divine reversal split");
    check(!spanFits(nodes,{{300,0,0},.02,300}),"Divine max span length");check(!spanFits(nodes,{{24,3,0},.02,24}),"Divine chord error split");
    auto curve=plan({0,0,0},{1,0,0},{100,50,25},600,0);check(curve.has_value(),"Divine curve construction");
    check(length(bezier(curve->points,0))<1e-6&&length(bezier(curve->points,1)-Vec3{100,50,25})<1e-6,"Divine exact curve endpoints");
    check(length(tangent(curve->points,0)-Vec3{1,0,0})<1e-6,"Divine incoming tangent");Vec3 forward;const auto endpoint=advance(*curve,curve->total,forward);check(length(endpoint-Vec3{100,50,25})<1e-5,"Divine arc-length completion");
    const auto a=studioAngles({1,0,1});check(std::abs(a.x-45)<1e-5,"Divine Studio positive pitch");
    std::printf("PASS native data/policy: 8 profiles, 29 network assets, Wolf curves, 24 recorded Orb samples, Frost HUD, Divine charge/combo/cone/Bezier/trail\n");
}
