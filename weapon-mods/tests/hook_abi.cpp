#include "hooks.hpp"
#include <cstdio>
#include <thread>
using namespace csnz;
namespace csnz {bool approvedEntry(Address){return true;}}
static std::atomic<unsigned> entered{},left{};
static std::vector<int> order;
static volatile int seed=7;
__declspec(noinline) int __cdecl CdeclFn(int a,int b){return a+b+seed;}
__declspec(noinline) int __stdcall StdcallFn(int a,int b,int c){return a*b+c+seed;}
__declspec(noinline) double __stdcall FloatFn(double a){return a*1.25+seed;}
__declspec(noinline) std::int64_t __cdecl WideFn(int a){return 0x123456789abc0000LL+a+seed;}
__declspec(noinline) int __cdecl NestedFn(int n);
int(__cdecl* volatile nested)(int)=NestedFn;
__declspec(noinline) int __cdecl NestedFn(int n){return n>0?n+nested(n-1):seed;}
struct Obj {int n;__declspec(noinline) int run(int a){return n*a+seed;}};
float sseAdd=1.25f;
__declspec(naked) int SseFn(){
    __asm {
        addss xmm0,sseAdd
        cvttss2si eax,xmm0
        nop
        nop
        nop
        ret
    }
}
static void trashVolatile(){
    __asm {
        xorps xmm0,xmm0
        xorps xmm1,xmm1
    }
    volatile double x=std::sin(1.25);(void)x;
}
static void check(bool okay,const char* name){if(!okay){std::printf("FAIL %s\n",name);ExitProcess(2);}}
static void ordinary(Address p,const char* name){Hooks::attach(p,name,[](Invocation& v){v.createState<unsigned>(v.registers->ecx);++entered;trashVolatile();},[](Invocation& v){(void)v.state<unsigned>();++left;trashVolatile();});}
int main(){
    ordinary(reinterpret_cast<Address>(&CdeclFn),"cdecl");
    Hooks::attach(reinterpret_cast<Address>(&StdcallFn),"std",[](Invocation& v){v.createState<int>(v.argument<int>(0));order.push_back(1);v.argument(0,v.argument<int>(0)+1);},[](Invocation& v){check(v.state<int>()==2,"per-invocation state");order.push_back(4);v.registers->eax+=3;});
    Hooks::attach(reinterpret_cast<Address>(&StdcallFn),"std2",[](Invocation&){order.push_back(2);},[](Invocation&){order.push_back(3);});
    ordinary(reinterpret_cast<Address>(&FloatFn),"float");ordinary(reinterpret_cast<Address>(&WideFn),"wide");
    ordinary(reinterpret_cast<Address>(&NestedFn),"nested");ordinary(reinterpret_cast<Address>(&SseFn),"sse");
    auto member=&Obj::run;static_assert(sizeof(member)==4);Address method{};std::memcpy(&method,&member,4);ordinary(method,"thiscall");
    Hooks::install();active=true;
    int(__cdecl* volatile cdeclFn)(int,int)=CdeclFn;
    int(__stdcall* volatile stdFn)(int,int,int)=StdcallFn;
    double(__stdcall* volatile fpFn)(double)=FloatFn;
    std::int64_t(__cdecl* volatile wideFn)(int)=WideFn;
    check(cdeclFn(10,20)==37,"cdecl args and return");
    check(stdFn(2,4,6)==28,"stdcall cleanup and mutable args/return");
    check(order==std::vector<int>({1,2,3,4}),"shared entry/leave ordering");
    check(std::abs(fpFn(3.0)-10.75)<1e-9,"x87 double return");
    check(wideFn(2)==0x123456789abc0009LL,"EDX:EAX int64 return");
    check(nested(7)==35,"recursive per-thread frames");
    Obj o{8};check((o.*member)(9)==79,"thiscall ECX and ret4");
    float input=2.f,output{};int sse{};
    __asm {
        movss xmm0,input
        call SseFn
        mov sse,eax
        movss output,xmm0
    }
    check(sse==3&&output==3.25f,"SSE register preservation");
    std::thread a([&]{for(int i=0;i<1000;i++)check(nested(3)==13,"thread A recursion");});
    std::thread b([&]{for(int i=0;i<1000;i++)check(nested(4)==17,"thread B recursion");});a.join();b.join();
    check(entered==left,"entry/leave balance");
    const unsigned previous=entered;active=false;check(cdeclFn(1,2)==10&&entered==previous,"disabled native passthrough");
    bool rejected=false;try{ordinary(reinterpret_cast<Address>(&CdeclFn),"late");}catch(...){rejected=true;}check(rejected,"resident mutation rejected");
    Hooks::resetForTests();check(cdeclFn(2,3)==12,"test-only removal");
    std::printf("PASS x86 ABI: cdecl/stdcall/thiscall, recursion, shared ordering, x87/SSE, int64, threads; %u balanced callbacks\n",entered.load());
}
