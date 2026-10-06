#pragma once
#include "platform.hpp"
#include <any>

namespace csnz {
// PUSHAD layout followed by EFLAGS and the stub's reserved word. No guessed
// C++ prototypes for attach-only callbacks; the original owns its ABI/RET N.
struct Registers {
    unsigned edi,esi,ebp,savedEsp,ebx,edx,ecx,eax,eflags,transfer;
};
static_assert(sizeof(Registers)==40);
struct Invocation {
    Registers* registers{};
    Address entryStack{},returnAddress{};
    DWORD threadId{};
    std::any local;
    template<class T> T argument(unsigned i)const{static_assert(sizeof(T)<=4);return read<T>(entryStack+4+i*4);}
    template<class T> void argument(unsigned i,T x){static_assert(sizeof(T)<=4);write<T>(entryStack+4+i*4,x);}
    template<class T,class... A> T& createState(A&&... a){return local.emplace<T>(std::forward<A>(a)...);}
    template<class T> T& state(){return std::any_cast<T&>(local);}
};
using Callback=std::function<void(Invocation&)>;
class Hooks {
public:
    static void attach(Address,const char* family,Callback enter,Callback leave={});
    static void replace(Address,void* replacement,void** original,const char* family);
    static void install(); // preflight must have passed; one atomic enable batch
    static unsigned size();
#ifdef CSNZ_HOOK_TEST
    static void resetForTests(); // NEVER present in release DLL
#endif
};
}
