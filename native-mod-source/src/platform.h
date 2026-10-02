#pragma once
#ifndef _M_IX86
#error This project must be compiled for Windows x86 (GoldSrc ia32).
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace giga_break_le {
using Address = std::uintptr_t;
constexpr char Version[] = "0.7.4-native-r2-partial";
constexpr wchar_t Product[] = L"CSNZ Giga Break LE Native 0.7.4-r2";
struct Build { WORD machine; DWORD timestamp, imageSize; };
constexpr Build LauncherBuild{IMAGE_FILE_MACHINE_I386,1790721054,811008};
constexpr Build ServerBuild{IMAGE_FILE_MACHINE_I386,1783989892,38477824};
constexpr Build ClientBuild{IMAGE_FILE_MACHINE_I386,1783989872,41570304};
inline bool sameBuild(const IMAGE_NT_HEADERS32& nt, const Build& b) {
    return nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == b.machine &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        nt.FileHeader.TimeDateStamp == b.timestamp && nt.OptionalHeader.SizeOfImage == b.imageSize;
}
inline std::wstring modulePath(HMODULE module = nullptr) {
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(module, buf.data(), static_cast<DWORD>(buf.size()));
    if (!n || n >= buf.size()) throw std::runtime_error("Cannot resolve module path");
    return std::wstring(buf.data(),n);
}
inline std::wstring directory(const std::wstring& s) {
    auto p=s.find_last_of(L"\\/");
    if (p==std::wstring::npos) throw std::runtime_error("Expected an absolute path");
    return s.substr(0,p);
}
inline bool samePath(const std::wstring& a,const std::wstring& b) { return _wcsicmp(a.c_str(),b.c_str())==0; }
inline std::wstring fullPath(const std::wstring& s) {
    std::vector<wchar_t> buf(32768);
    DWORD n=GetFullPathNameW(s.c_str(),static_cast<DWORD>(buf.size()),buf.data(),nullptr);
    if(!n||n>=buf.size()) throw std::runtime_error("Invalid path");
    return std::wstring(buf.data(),n);
}
inline bool fileBuild(const std::wstring& path,const Build& b) {
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return false;
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 nt{}; DWORD n{};
    bool ok=ReadFile(h,&dos,sizeof(dos),&n,nullptr)&&n==sizeof(dos)&&dos.e_magic==IMAGE_DOS_SIGNATURE&&
        dos.e_lfanew>=sizeof(dos)&&dos.e_lfanew<=1024*1024;
    if(ok) { LARGE_INTEGER pos{}; pos.QuadPart=dos.e_lfanew;
        ok=SetFilePointerEx(h,pos,nullptr,FILE_BEGIN)&&ReadFile(h,&nt,sizeof(nt),&n,nullptr)&&n==sizeof(nt)&&sameBuild(nt,b); }
    CloseHandle(h); return ok;
}
inline std::wstring stopEventName(DWORD pid) { return L"Local\\CSNZ_GigaBreakLE_Native_Stop_"+std::to_wstring(pid); }
// Keep the v1 exclusivity key so old and renamed builds cannot attach together.
inline std::wstring activeMutexName(DWORD pid) { return L"Local\\CSNZ_LE_Native_Active_"+std::to_wstring(pid); }
struct Handle {
    HANDLE h{};
    explicit Handle(HANDLE v=nullptr):h(v){}
    ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
    operator HANDLE()const{return h;}
};
}
