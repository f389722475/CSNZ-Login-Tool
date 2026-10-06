#pragma once
#include "platform.hpp"

namespace csnz {
// Immutable, compiled-in profile DATA. There is no script engine, evaluator,
// Frida dependency or external JSON parser in the deployed runtime.
struct Config {
    using Array=std::vector<Config>;
    using Object=std::map<std::string,Config>;
    std::variant<std::nullptr_t,bool,double,std::string,Array,Object> value;
    Config():value(nullptr){}
    Config(std::nullptr_t):value(nullptr){}
    Config(bool x):value(x){}
    Config(double x):value(x){}
    Config(const char* x):value(std::string(x)){}
    Config(Array x):value(std::move(x)){}
    Config(Object x):value(std::move(x)){}
    const Config& operator[](const std::string& key)const{return object().at(key);}
    const Config& operator[](std::size_t i)const{return array().at(i);}
    const Array& array()const{return std::get<Array>(value);}
    const Object& object()const{return std::get<Object>(value);}
    bool has(const std::string& key)const{return std::holds_alternative<Object>(value)&&object().count(key)!=0;}
    double number()const{return std::get<double>(value);}
    unsigned u32()const{auto n=number();if(!std::isfinite(n)||n<0||n>4294967295.0||std::floor(n)!=n)throw std::runtime_error("Profile uint");return static_cast<unsigned>(n);}
    int integer()const{return static_cast<int>(number());}
    float real()const{return static_cast<float>(number());}
    const std::string& text()const{return std::get<std::string>(value);}
    bool boolean()const{return std::get<bool>(value);}
};
const Config& profiles();
const Config& entryPolicy();
void validateBuild();
bool approvedEntry(Address);
}
