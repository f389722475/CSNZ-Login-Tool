#include "hooks.hpp"
#include "config.hpp"
#include "MinHook.h"

namespace csnz {
namespace {
struct Adapter {std::string family;Callback enter,leave;};
struct Group {Address target{};void* original{};void* replacement{};void** out{};std::vector<Adapter> adapters;};
struct Frame {Address originalReturn{};Group* group{};std::vector<Invocation> calls;};
std::vector<std::unique_ptr<Group>> groups;
bool installed=false;
thread_local std::vector<std::unique_ptr<Frame>> frames;
constexpr unsigned MaxHooks=64;
unsigned defaultMxcsr=0x1f80;
extern "C" void HookLeaveThunk();

extern "C" void __cdecl DispatchEnter(Registers* r) noexcept {
    const auto index=r->transfer;
    if(index>=groups.size())std::terminate();
    auto& g=*groups[index];r->transfer=reinterpret_cast<unsigned>(g.original);
    if(!active&&!stopping)return;
    Frame* current=nullptr;
    try{
        if(frames.size()>=128)throw std::runtime_error("Hook recursion limit");
        auto f=std::make_unique<Frame>();f->group=&g;
        const auto entry=reinterpret_cast<Address>(r)+sizeof(Registers);
        f->originalReturn=ptr(entry);f->calls.reserve(g.adapters.size());
        // Store the frame BEFORE invoking callbacks: a callback can recursively
        // call another intercepted native function, including the same target.
        current=f.get();frames.push_back(std::move(f));
        for(const auto& a:g.adapters){
            Invocation v;v.registers=r;v.entryStack=entry;v.returnAddress=current->originalReturn;v.threadId=GetCurrentThreadId();
            current->calls.push_back(std::move(v));
            if(a.enter)guarded(a.family.c_str(),[&]{a.enter(current->calls.back());});
        }
        bool leave=false;for(const auto& a:g.adapters)leave=leave||static_cast<bool>(a.leave);
        if(leave)write(entry,reinterpret_cast<Address>(&HookLeaveThunk));else frames.pop_back();
    }catch(const std::exception& e){if(!frames.empty()&&frames.back().get()==current)frames.pop_back();fail(e.what());}
    catch(...){if(!frames.empty()&&frames.back().get()==current)frames.pop_back();fail("Hook entry exception");}
}
extern "C" void __cdecl DispatchLeave(Registers* r) noexcept {
    // A leave thunk is installed only after the complete entry frame exists.
    // Silent fallback to a guessed address would corrupt the caller's stack.
    if(frames.empty())std::terminate();
    auto f=std::move(frames.back());frames.pop_back();r->transfer=f->originalReturn;
    for(std::size_t i=f->calls.size();i-->0;){
        const auto& a=f->group->adapters[i];if(!a.leave)continue;
        auto& v=f->calls[i];v.registers=r;
        guarded(a.family.c_str(),[&]{a.leave(v);});
    }
}

// FXSAVE/FXRSTOR preserve both x87 return values/stack and SSE registers/MXCSR.
// RET consumes the reserved transfer word; the engine's own RET N determines
// argument cleanup, so cdecl, stdcall and thiscall are not conflated here.
__declspec(naked) void HookEnterThunk(){
    __asm {
        pushfd
        pushad
        mov ebx,esp
        sub esp,528
        and esp,-16
        fxsave [esp]
        fninit
        ldmxcsr defaultMxcsr
        cld
        push ebx
        call DispatchEnter
        add esp,4
        fxrstor [esp]
        mov esp,ebx
        popad
        popfd
        ret
    }
}
extern "C" __declspec(naked) void HookLeaveThunk(){
    __asm {
        push 0
        pushfd
        pushad
        mov ebx,esp
        sub esp,528
        and esp,-16
        fxsave [esp]
        fninit
        ldmxcsr defaultMxcsr
        cld
        push ebx
        call DispatchLeave
        add esp,4
        fxrstor [esp]
        mov esp,ebx
        popad
        popfd
        ret
    }
}
#define STUB(n) __declspec(naked) void Stub##n(){__asm push n __asm jmp HookEnterThunk}
STUB(0) STUB(1) STUB(2) STUB(3) STUB(4) STUB(5) STUB(6) STUB(7)
STUB(8) STUB(9) STUB(10) STUB(11) STUB(12) STUB(13) STUB(14) STUB(15)
STUB(16) STUB(17) STUB(18) STUB(19) STUB(20) STUB(21) STUB(22) STUB(23)
STUB(24) STUB(25) STUB(26) STUB(27) STUB(28) STUB(29) STUB(30) STUB(31)
STUB(32) STUB(33) STUB(34) STUB(35) STUB(36) STUB(37) STUB(38) STUB(39)
STUB(40) STUB(41) STUB(42) STUB(43) STUB(44) STUB(45) STUB(46) STUB(47)
STUB(48) STUB(49) STUB(50) STUB(51) STUB(52) STUB(53) STUB(54) STUB(55)
STUB(56) STUB(57) STUB(58) STUB(59) STUB(60) STUB(61) STUB(62) STUB(63)
#undef STUB
#define S(n) reinterpret_cast<void*>(&Stub##n)
void* stubs[]={S(0),S(1),S(2),S(3),S(4),S(5),S(6),S(7),S(8),S(9),S(10),S(11),S(12),S(13),S(14),S(15),
S(16),S(17),S(18),S(19),S(20),S(21),S(22),S(23),S(24),S(25),S(26),S(27),S(28),S(29),S(30),S(31),
S(32),S(33),S(34),S(35),S(36),S(37),S(38),S(39),S(40),S(41),S(42),S(43),S(44),S(45),S(46),S(47),
S(48),S(49),S(50),S(51),S(52),S(53),S(54),S(55),S(56),S(57),S(58),S(59),S(60),S(61),S(62),S(63)};
#undef S
Group& group(Address target){
    if(installed)throw std::runtime_error("Resident hooks cannot be changed");
    if(!approvedEntry(target)||!executable(target))throw std::runtime_error("Unapproved function entry");
    for(auto& g:groups)if(g->target==target)return *g;
    if(groups.size()>=MaxHooks)throw std::runtime_error("Native hook capacity");
    for(const auto& g:groups)if(std::abs(static_cast<std::int64_t>(target)-g->target)<16)throw std::runtime_error("Overlapping detours");
    auto g=std::make_unique<Group>();g->target=target;groups.push_back(std::move(g));return *groups.back();
}
void checked(MH_STATUS s){if(s!=MH_OK)throw std::runtime_error(std::string("MinHook: ")+MH_StatusToString(s));}
}
void Hooks::attach(Address target,const char* family,Callback enter,Callback leave){
    auto& g=group(target);if(g.replacement)throw std::runtime_error("Attach conflicts with replacement");
    for(const auto& a:g.adapters)if(a.family==family)throw std::runtime_error("Duplicate family attachment");
    g.adapters.push_back({family,std::move(enter),std::move(leave)});
}
void Hooks::replace(Address target,void* replacement,void** original,const char* family){
    auto& g=group(target);if(g.replacement||!g.adapters.empty()||!replacement||!original)throw std::runtime_error("Duplicate native replacement");
    g.replacement=replacement;g.out=original;g.adapters.push_back({family,{},{}});
}
void Hooks::install(){
    if(installed)throw std::runtime_error("Already installed");
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)checked(init);
    std::vector<Address> created;
    try{
        for(std::size_t i=0;i<groups.size();++i){auto& g=*groups[i];checked(MH_CreateHook(reinterpret_cast<void*>(g.target),g.replacement?g.replacement:stubs[i],&g.original));created.push_back(g.target);if(g.out)*g.out=g.original;}
        for(auto& g:groups)checked(MH_QueueEnableHook(reinterpret_cast<void*>(g->target)));
        checked(MH_ApplyQueued());installed=true;
    }catch(...){
        for(const auto p:created){MH_DisableHook(reinterpret_cast<void*>(p));MH_RemoveHook(reinterpret_cast<void*>(p));}
        for(auto& g:groups)if(g->out)*g->out=nullptr;
        throw;
    }
}
unsigned Hooks::size(){return static_cast<unsigned>(groups.size());}
#ifdef CSNZ_HOOK_TEST
void Hooks::resetForTests(){active=false;stopping=false;for(auto& g:groups){MH_DisableHook(reinterpret_cast<void*>(g->target));MH_RemoveHook(reinterpret_cast<void*>(g->target));}groups.clear();installed=false;frames.clear();}
#endif
}
