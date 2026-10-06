#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

static_assert(sizeof(void*)==4,"CSNZ weapon modules require Win32/x86");
namespace csnz {
using Address=std::uintptr_t;
bool rawRead(Address,void*,std::size_t) noexcept;
bool rawWrite(Address,const void*,std::size_t) noexcept;
bool executable(Address) noexcept;
template<class T> T read(Address p){T v{};if(!rawRead(p,&v,sizeof(v)))throw std::runtime_error("Invalid game read");return v;}
template<class T> void write(Address p,const T& v){if(!rawWrite(p,&v,sizeof(v)))throw std::runtime_error("Invalid game write");}
inline Address ptr(Address p){return read<Address>(p);}
template<class F> F native(Address p){if(!executable(p))throw std::runtime_error("Non-executable native target");return reinterpret_cast<F>(p);}
std::string readText(Address,std::size_t);
void log(const std::string&) noexcept;
void setLogFile(const std::wstring&);
void fail(const std::string&) noexcept;
extern Address mp,hw;
extern std::atomic<bool> active,stopping;
extern std::atomic<bool> failed;
extern std::recursive_mutex gameplayMutex;
inline Address at(Address rva){return mp+rva;}
inline Address hat(Address rva){return hw+rva;}
template<class F> void guarded(const char* where,F&& fn) noexcept {std::lock_guard<std::recursive_mutex> lock(gameplayMutex);try{fn();}catch(const std::exception& e){fail(std::string(where)+": "+e.what());}catch(...){fail(std::string(where)+": native exception");}}
}
