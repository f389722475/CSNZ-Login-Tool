#include "config.hpp"
#include <bcrypt.h>
#include <fstream>
#include <filesystem>

namespace csnz {
namespace {
std::string sha256(const std::wstring& path){
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA provider");
    std::array<unsigned char,32> digest{};
    try{
        if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("SHA hash");
        std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Module disk read");
        std::array<unsigned char,65536> block{};
        while(file){file.read(reinterpret_cast<char*>(block.data()),block.size());const auto n=file.gcount();if(n&&BCryptHashData(hash,block.data(),static_cast<ULONG>(n),0)<0)throw std::runtime_error("SHA update");}
        if(!file.eof()||BCryptFinishHash(hash,digest.data(),digest.size(),0)<0)throw std::runtime_error("SHA finish");
    }catch(...){if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);throw;}
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
    constexpr char hex[]="0123456789abcdef";std::string s;for(auto v:digest){s+=hex[v>>4];s+=hex[v&15];}return s;
}
std::wstring path(HMODULE m){wchar_t text[32768]{};auto n=GetModuleFileNameW(m,text,32768);if(!n||n>=32768)throw std::runtime_error("Module path");return {text,n};}
std::vector<unsigned char> unhex(const std::string& s){
    if(s.size()%2)throw std::runtime_error("Guard hex length");std::vector<unsigned char> v;
    for(std::size_t i=0;i<s.size();i+=2)v.push_back(static_cast<unsigned char>(std::stoul(s.substr(i,2),nullptr,16)));return v;
}
Address base(const std::string& name){if(name=="mp.dll")return mp;if(name=="hw.dll")return hw;throw std::runtime_error("Forbidden module in profile");}
}
void validateBuild(){
    const auto exe=path(nullptr);const auto directory=std::filesystem::path(exe).parent_path();
    if(_wcsicmp(std::filesystem::path(exe).filename().c_str(),L"CSOHLDS.exe"))throw std::runtime_error("Server-only: expected CSOHLDS.exe");
    if(sha256(exe)!="a2ce29976618699a408da0b21d97ed9eddf220d221254ab4265d90549ac26c6f")throw std::runtime_error("CSOHLDS build mismatch");
    mp=reinterpret_cast<Address>(GetModuleHandleW(L"mp.dll"));hw=reinterpret_cast<Address>(GetModuleHandleW(L"hw.dll"));
    if(!mp||!hw)throw std::runtime_error("Server modules not loaded");
    const auto& canonical=profiles()["shared"]["modules"];
    for(const auto& [name,c]:canonical.object()){
        const Address b=base(name);const auto loaded=path(reinterpret_cast<HMODULE>(b));
        if(_wcsicmp(std::filesystem::path(loaded).parent_path().c_str(),directory.c_str()))throw std::runtime_error("Server module outside executable directory");
        const auto dos=read<IMAGE_DOS_HEADER>(b);if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x100000)throw std::runtime_error("Invalid DOS header");
        const auto pe=read<IMAGE_NT_HEADERS32>(b+dos.e_lfanew);
        if(pe.Signature!=IMAGE_NT_SIGNATURE||pe.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||pe.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||pe.FileHeader.TimeDateStamp!=c["timestamp"].u32()||pe.OptionalHeader.SizeOfImage!=c["size"].u32())throw std::runtime_error("Server PE build mismatch");
        if(sha256(loaded)!=c["sha256"].text())throw std::runtime_error("Server module SHA mismatch");
    }
    std::set<std::tuple<std::string,unsigned,std::string>> verified;
    for(const auto& [family,p]:profiles().object()){
        for(const auto& [name,m]:p["modules"].object()){
            const auto b=base(name);if(m.has("slots"))for(const auto& [v,slots]:m["slots"].object())for(const auto& [s,r]:slots.object())
                if(ptr(b+std::stoul(v)+std::stoul(s)*4)!=b+r.u32())throw std::runtime_error("Vtable slot mismatch: "+family);
        }
        for(const auto& g:p["hooks"].array()){
            const auto name=g["module"].text();const auto rva=g["rva"].u32();
            if(!verified.emplace(name,rva,g["head"].text()).second)continue;
            const auto b=base(name);auto expected=unhex(g["head"].text());
            const auto delta=b-p["modules"][name]["preferred_base"].u32();
            for(const auto& index:g["relocs"].array()){
                const auto i=index.u32();if(i+4>expected.size())throw std::runtime_error("Partial relocation");
                unsigned n{};std::memcpy(&n,expected.data()+i,4);n+=delta;std::memcpy(expected.data()+i,&n,4);
            }
            std::vector<unsigned char> actual(expected.size());if(!rawRead(b+rva,actual.data(),actual.size())||actual!=expected)throw std::runtime_error("Live code guard: "+family+"/"+g["label"].text());
        }
    }
    log("GUARDS_OK: server SHA/PE, relocation-aware code and vtable slots");
}
bool approvedEntry(Address p){
    if(p==at(0x1528948)||p==at(0x1407a27))return false;
    for(const auto& [name,c]:entryPolicy()["modules"].object())for(const auto& r:c["entries"].array())if(p==base(name)+r.u32())return true;
    return false;
}
}
