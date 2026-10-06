#pragma once
#include "game.hpp"
#include <optional>
namespace csnz::divine_policy {
struct Charge {int energy;double deadline;};
inline std::optional<Charge> charge(int energy,double deadline,double t,double interval,int maximum){
    if(!std::isfinite(deadline)||!std::isfinite(t)||!std::isfinite(interval)||energy<0||interval<.01||interval>60||maximum<1||maximum>10000)throw std::runtime_error("Divine charge bounds");
    if(energy>=maximum||t<deadline)return {};return Charge{energy+1,t+interval};
}
inline int stage(int previous,double last,double t,double reset){return t-last>reset?0:(previous+1)%4;}
inline bool inArc(Vec3 forward,Vec3 delta,double angle){if(!std::isfinite(angle)||angle<=0||angle>360)throw std::runtime_error("Divine attack angle");const auto n=length(delta);return n<1e-5||dot(forward,delta)/n>=std::cos(angle*Pi/360)-1e-7;}
struct Node {Vec3 position;double at{},distance{};};
inline bool spanFits(const std::vector<Node>& points,const Node& next){
    if(points.empty())return false;const auto origin=points.front().position,d=next.position-origin;const auto dd=dot(d,d);if(dd<1e-8||dd>256*256)return false;
    double previous=-1e-7;for(const auto& p:points){const auto v=p.position-origin;const auto u=dot(v,d)/dd;if(u<previous-1e-7||u>1+1e-7)return false;previous=u;if(dot(v-d*u,v-d*u)>.5*.5)return false;}return true;
}
using Points=std::array<Vec3,4>;
inline Vec3 bezier(const Points& c,double u){const auto v=1-u;return c[0]*(v*v*v)+c[1]*(3*v*v*u)+c[2]*(3*v*u*u)+c[3]*(u*u*u);}
inline Vec3 tangent(const Points& c,double u){const auto v=1-u;return unit((c[1]-c[0])*(3*v*v)+(c[2]-c[1])*(6*v*u)+(c[3]-c[2])*(3*u*u),{1,0,0});}
struct Curve {Points points;std::array<double,33> arc{};double total{},distance{},planned{};};
inline std::optional<Curve> plan(Vec3 position,Vec3 forward,Vec3 target,double speed,double t){
    const auto delta=target-position;const auto dist=length(delta);if(dist<.001)return {};const auto direct=unit(delta,{1,0,0});const auto handle=std::min(dist*.4,speed*.12);
    Curve q;q.points={position,position+forward*handle,target-direct*(dist*.25),target};q.planned=t;auto previous=position;
    for(unsigned i=1;i<=32;i++){const auto p=bezier(q.points,i/32.);q.total+=length(p-previous);q.arc[i]=q.total;previous=p;}return q;
}
inline Vec3 advance(Curve& q,double distance,Vec3& forward){q.distance=std::min(q.total,q.distance+distance);unsigned i=1;while(i<32&&q.arc[i]<q.distance)i++;const auto span=q.arc[i]-q.arc[i-1];const auto u=((i-1)+(span>1e-8?(q.distance-q.arc[i-1])/span:0))/32.;forward=tangent(q.points,u);return bezier(q.points,u);}
inline Vec3 studioAngles(Vec3 d){return {float(std::atan2(d.z,std::hypot(d.x,d.y))*180/Pi),float(std::atan2(d.y,d.x)*180/Pi),0};}
}
