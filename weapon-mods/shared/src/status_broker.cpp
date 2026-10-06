#include "brokers.hpp"
#include <optional>

namespace csnz {
namespace {
struct Row {Entity e;unsigned offset;float original,last;std::map<std::string,float> owners;};
using Key=std::tuple<Address,Address,unsigned,Address,unsigned>;
std::map<Key,Row> rows;std::optional<double> clockValue;
bool same(const Entity& a,const Entity& b){return a.object==b.object&&a.edict==b.edict&&a.pev==b.pev&&a.serial==b.serial;}
bool apply(Row& r){
    if(!valid(r.e))return false;const auto p=r.e.pev+r.offset;const auto current=read<float>(p);
    if(!std::isfinite(current))throw std::runtime_error("Nonfinite native status");
    if(current!=r.last)r.original=current;
    if(r.owners.empty()){if(current==r.last)write(p,r.original);return false;}
    float next=r.owners.begin()->second;for(const auto& [name,value]:r.owners)next=r.offset==0x12c?std::max(next,value):std::min(next,value);
    write(p,next);r.last=read<float>(p);return true;
}
}
bool StatusBroker::set(const std::string& family,const Entity& e,unsigned offset,double value){
    if((offset!=0x240&&offset!=0x12c)||!std::isfinite(value)||value<0||value>(offset==0x12c?100:10000))throw std::runtime_error("Unsupported status field/value");
    if(!valid(e))return false;const Key k{e.object,e.edict,e.serial,e.pev,offset};auto i=rows.find(k);
    if(i==rows.end()){const auto current=read<float>(e.pev+offset);if(!std::isfinite(current))throw std::runtime_error("Status baseline");i=rows.emplace(k,Row{e,offset,current,current,{}}).first;}
    i->second.owners[family]=static_cast<float>(value);apply(i->second);return true;
}
void StatusBroker::release(const std::string& family,const Entity& e){for(auto i=rows.begin();i!=rows.end();){auto& r=i->second;if(same(r.e,e)&&r.owners.erase(family)&&!apply(r))i=rows.erase(i);else ++i;}}
void StatusBroker::releaseAll(const std::string& family){for(auto i=rows.begin();i!=rows.end();){auto& r=i->second;if(r.owners.erase(family)&&!apply(r))i=rows.erase(i);else ++i;}}
void StatusBroker::maintain(double time){if(!std::isfinite(time))throw std::runtime_error("Status clock");if(clockValue&&time<*clockValue-1)rows.clear();clockValue=time;for(auto i=rows.begin();i!=rows.end();)if(!valid(i->second.e))i=rows.erase(i);else ++i;}
unsigned StatusBroker::size(){return static_cast<unsigned>(rows.size());}
void StatusBroker::forget(){rows.clear();clockValue.reset();}
}
