#include "platform.hpp"
#include <fstream>

namespace csnz {
Address mp{},hw{};
std::atomic<bool> active{false},stopping{false};
std::atomic<bool> failed{false};
std::recursive_mutex gameplayMutex;
namespace {std::mutex logMutex;std::ofstream logFile;}
void setLogFile(const std::wstring& path){std::lock_guard<std::mutex> lock(logMutex);logFile.open(path,std::ios::app);if(!logFile)throw std::runtime_error("Cannot open native diagnostic log");}
__declspec(noinline) bool rawRead(Address p,void* out,std::size_t n) noexcept {
    if(p<0x10000||!n||p+n<p)return false;
    __try{std::memcpy(out,reinterpret_cast<const void*>(p),n);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
__declspec(noinline) bool rawWrite(Address p,const void* in,std::size_t n) noexcept {
    if(p<0x10000||!n||p+n<p)return false;
    __try{std::memcpy(reinterpret_cast<void*>(p),in,n);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool executable(Address p) noexcept {
    MEMORY_BASIC_INFORMATION m{};
    return p>=0x10000&&VirtualQuery(reinterpret_cast<void*>(p),&m,sizeof(m))==sizeof(m)&&m.State==MEM_COMMIT&&
        !(m.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
std::string readText(Address p,std::size_t n){if(n>1048576)throw std::runtime_error("Text length");std::string s(n,'\0');if(n&&!rawRead(p,s.data(),n))throw std::runtime_error("Text read");return s;}
void log(const std::string& s) noexcept {
    // No game API calls from logging or the external status/query thread.
    try{std::lock_guard<std::mutex> lock(logMutex);OutputDebugStringA(("[CSNZWeaponCore] "+s+"\n").c_str());if(logFile){SYSTEMTIME t{};GetSystemTime(&t);char stamp[64];sprintf_s(stamp,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);logFile<<stamp<<s<<std::endl;}}catch(...){}
}
void fail(const std::string& s) noexcept {failed=true;active=false;stopping=true;log("DISABLED: "+s);}
}
