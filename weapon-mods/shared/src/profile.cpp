#include "config.hpp"
#include <filesystem>

namespace csnz {
namespace {
void validateImage(Address b,unsigned imageSize=0){
    const auto dos=read<IMAGE_DOS_HEADER>(b);
    if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<64||dos.e_lfanew>0x100000)throw std::runtime_error("Invalid DOS header");
    const auto pe=read<IMAGE_NT_HEADERS32>(b+dos.e_lfanew);
    if(pe.Signature!=IMAGE_NT_SIGNATURE||pe.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||pe.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||!pe.OptionalHeader.SizeOfImage||
       (imageSize&&pe.OptionalHeader.SizeOfImage!=imageSize))throw std::runtime_error("Server PE layout mismatch");
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
    // The host EXE has no weapon RVA hooks. Accept compatible x86 hosts;
    // metadata/signatures/overlays are not a gameplay compatibility contract.
    validateImage(reinterpret_cast<Address>(GetModuleHandleW(nullptr)));
    mp=reinterpret_cast<Address>(GetModuleHandleW(L"mp.dll"));hw=reinterpret_cast<Address>(GetModuleHandleW(L"hw.dll"));
    if(!mp||!hw)throw std::runtime_error("Server modules not loaded");
    const auto& canonical=profiles()["shared"]["modules"];
    for(const auto& [name,c]:canonical.object()){
        const Address b=base(name);const auto loaded=path(reinterpret_cast<HMODULE>(b));
        if(_wcsicmp(std::filesystem::path(loaded).parent_path().c_str(),directory.c_str()))throw std::runtime_error("Server module outside executable directory");
        validateImage(b,c["size"].u32());
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
    log("GUARDS_OK: x86/PE layout, relocation-aware code and vtable slots; no whole-file SHA/timestamp gate");
}
bool approvedEntry(Address p){
    if(p==at(0x1528948)||p==at(0x1407a27))return false;
    for(const auto& [name,c]:entryPolicy()["modules"].object())for(const auto& r:c["entries"].array())if(p==base(name)+r.u32())return true;
    return false;
}
}
