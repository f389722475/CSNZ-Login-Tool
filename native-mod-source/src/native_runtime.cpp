#include "game_abi.h"
#include "build_profile.h"
#include "MinHook.h"
#include <cstdio>

extern "C" void* g_finalOriginal=nullptr;
namespace giga_break_le {
Engine engine;
std::recursive_mutex gameMutex;
std::atomic<bool> enabled{false},stopRequested{false},serverClean{false},clientClean{false};
std::atomic<unsigned> shots{0},shields{0},blocked{0},errors{0};
namespace {
HMODULE selfModule{};
HANDLE logFile=INVALID_HANDLE_VALUE,stopEvent{},resultEvent{},activeMutex{};
SRWLOCK logLock=SRWLOCK_INIT;
std::atomic<DWORD> phase{0}; // 0=not started, 1=waiting, 2=ready, 3=failed, 4=stopping, 5=stopped
WeaponFn originalNormal{},originalPrimary{},originalSecondary{},originalFrame{},originalBones{};
void(__cdecl* originalGlobalFrame)(){};
DamageFn originalDamage{};
TempUpdateFn originalTempUpdate{};
bool hooksEnabled=false;
}
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
    MEMORY_BASIC_INFORMATION mi{};
    return p>=0x10000&&VirtualQuery(reinterpret_cast<void*>(p),&mi,sizeof(mi))==sizeof(mi)&&mi.State==MEM_COMMIT&&
        !(mi.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(mi.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
void log(const char* text) noexcept {
    AcquireSRWLockExclusive(&logLock);
    if(logFile!=INVALID_HANDLE_VALUE){SYSTEMTIME t{};GetLocalTime(&t);char line[1024];
        int n=_snprintf_s(line,sizeof(line),_TRUNCATE,"%04u-%02u-%02u %02u:%02u:%02u %s\r\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,text);
        DWORD written{};WriteFile(logFile,line,n<0?static_cast<DWORD>(std::strlen(line)):n,&written,nullptr);FlushFileBuffers(logFile);}
    ReleaseSRWLockExclusive(&logLock);
}
void fail(const char* reason) noexcept {
    if(enabled.exchange(false)){++errors;phase=3;stopRequested=true;log(reason);log("DISABLED: cleanup will run on the server/client engine callbacks. Restart the game before retrying.");}
}
namespace {
void serverBoundary() noexcept {if(stopRequested&&!serverClean)serverCleanup();}
void clientBoundary() noexcept {if(stopRequested&&!clientClean)clientCleanup();}
void __fastcall normalHook(void* self,void*){
    Address w=reinterpret_cast<Address>(self);Aim a{};int clip{};bool fire=false;
    guarded([&]{if(active(w)){clip=read<int>(w+0x160);a=aim(w);fire=true;}});
    originalNormal(self);
    if(fire)guarded([&]{if(read<int>(w+0x160)==clip-1)shoot(w,a);});
}
void __fastcall primaryHook(void* self,void*){
    Address w=reinterpret_cast<Address>(self);Aim a{};bool fire=false;
    guarded([&]{if(active(w)){int m=read<int>(w+0x1c0);if(m==1||m==2){a=aim(w);fire=true;}}});
    originalPrimary(self);if(fire)guarded([&]{burst(w,a);});
}
void __fastcall secondaryHook(void* self,void*){
    Address w=reinterpret_cast<Address>(self);Aim a{};int before{};bool fire=false;
    guarded([&]{if(active(w)){before=read<int>(w+0x178);a=aim(w);fire=true;}});
    originalSecondary(self);if(fire)guarded([&]{if(before==0&&read<int>(w+0x178)==1)crashStart(w,a);});
}
void __fastcall frameHook(void* self,void*){
    serverBoundary();Address w=reinterpret_cast<Address>(self);int delayed{};bool fire=false;
    guarded([&]{if(active(w)){frame(w);delayed=read<int>(w+0x64);fire=true;}});
    originalFrame(self);
    if(fire)guarded([&]{if((delayed==2||delayed==3)&&read<int>(w+0x64)==0)swing(w,delayed);});
}
void __cdecl globalHook(){serverBoundary();guarded([]{globalFrame();});originalGlobalFrame();}
int __fastcall damageHook(void* self,void*,void* inflictor,void* attacker,float amount,int bits,void* info){
    bool prevent=false;guarded([&]{prevent=blockDamage(reinterpret_cast<Address>(self),amount);});
    return prevent?0:originalDamage(self,inflictor,attacker,amount,bits,info);
}
void __cdecl tempHook(double frameTime,double clientTime,double gravity,void* freeList,void* activeList,void* visible,void* sound){
    clientBoundary();guarded([]{wingVisualFrame();});originalTempUpdate(frameTime,clientTime,gravity,freeList,activeList,visible,sound);
}
void __fastcall bonesHook(void* self,void*){
    Address r=reinterpret_cast<Address>(self);bool ours=false;guarded([&]{ours=ourWing(r);});
    originalBones(self);if(ours)guarded([&]{balanceWing(r);});
}
bool moduleBuild(Address base,const Build& build){
    auto dos=read<IMAGE_DOS_HEADER>(base);
    return dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>=sizeof(dos)&&dos.e_lfanew<0x100000&&sameBuild(read<IMAGE_NT_HEADERS32>(base+dos.e_lfanew),build);
}
void verifyEntries(){
    for(const auto& entry:EntryGuards){
        Address base=entry.client?engine.client:engine.mp,preferred=entry.client?ClientPreferredBase:ServerPreferredBase;
        unsigned char wanted[16],actual[16];std::memcpy(wanted,entry.bytes,16);
        for(int offset:entry.reloc)if(offset>=0){Address a{};std::memcpy(&a,wanted+offset,4);a+=base-preferred;std::memcpy(wanted+offset,&a,4);}
        if(!rawRead(base+entry.rva,actual,16)||std::memcmp(actual,wanted,16))throw std::runtime_error("Instruction guard mismatch (unsupported build or another patch is active)");
    }
}
template<class T> bool resolveSlot(T& fn,Address p){Address v{};if(!rawRead(p,&v,4)||!executable(v))return false;fn=reinterpret_cast<T>(v);return true;}
bool resolveEngine(){
    engine.property=reinterpret_cast<PropertyFn>(engine.mp+0x6444d0);
    engine.zombieMode=reinterpret_cast<decltype(engine.zombieMode)>(engine.mp+0x6688c0);
    engine.bullet=reinterpret_cast<BulletFn>(engine.mp+0x13fc970);
    engine.aux=reinterpret_cast<WeaponFn>(engine.mp+0xba18c0);
    engine.isLocal=reinterpret_cast<decltype(engine.isLocal)>(engine.client+0x113a850);
    if(!resolveSlot(engine.manager,engine.mp+0x21d3cec)||!resolveSlot(engine.makeVectors,engine.mp+0x21d3aac)||
        !resolveSlot(engine.playback,engine.mp+0x21d3c5c)||!resolveSlot(engine.traceLine,engine.mp+0x21d3adc)||
        !resolveSlot(engine.findSphere,engine.mp+0x21d3a9c)||!resolveSlot(engine.entityByIndex,engine.mp+0x21d3b8c)||
        !resolveSlot(engine.entityIndex,engine.mp+0x21d3b88)||!resolveSlot(engine.clientTime,engine.client+0x2183a74)||
        !resolveSlot(engine.clientEntity,engine.client+0x2183a70))return false;
    Address effects{},events{},globals{};float time{};
    if(!rawRead(engine.client+0x2183ae8,&effects,4)||!effects||!rawRead(engine.client+0x2183aec,&events,4)||!events||
        !resolveSlot(engine.tempModel,effects+0xc4)||!resolveSlot(engine.findModel,events+0xc)||
        !rawRead(engine.mp+0x21d3dac,&globals,4)||!rawRead(globals,&time,4)||!std::isfinite(time))return false;
    return true;
}
void addHook(Address target,void* replacement,void** original){
    if(MH_CreateHook(reinterpret_cast<void*>(target),replacement,original)!=MH_OK)throw std::runtime_error("Could not create native detour");
}
}
} // namespace giga_break_le

extern "C" void __cdecl GigaBreakLE_FinalCallback(std::uintptr_t player,std::uintptr_t damage) noexcept {
    giga_break_le::guarded([&]{giga_break_le::finalDamage(player,damage);});
}
// A mid-function hook is not a C++ function call boundary. Preserve flags, all
// GPRs, x87 and SSE state; the trampoline replays the complete stolen instructions.
extern "C" __declspec(naked) void GigaBreakLE_FinalHook(){
    __asm {
        pushfd
        pushad
        mov eax,esp
        sub esp,528
        and esp,0fffffff0h
        mov dword ptr [esp+512],eax
        fxsave [esp]
        fninit
        mov dword ptr [esp+516],01f80h
        ldmxcsr dword ptr [esp+516]
        cld
        mov ecx,dword ptr [eax+8]
        sub ecx,013a8h
        push ecx
        push dword ptr [eax+4]
        call GigaBreakLE_FinalCallback
        add esp,8
        fxrstor [esp]
        mov esp,dword ptr [esp+512]
        popad
        popfd
        jmp dword ptr [g_finalOriginal]
    }
}
namespace giga_break_le {
namespace {
void installHooks(){
    verifyEntries();
    if(MH_Initialize()!=MH_OK)throw std::runtime_error("MinHook initialization failed");
    try{
        addHook(engine.mp+0xbab520,reinterpret_cast<void*>(normalHook),reinterpret_cast<void**>(&originalNormal));
        addHook(engine.mp+0xba0a40,reinterpret_cast<void*>(primaryHook),reinterpret_cast<void**>(&originalPrimary));
        addHook(engine.mp+0xba0ff0,reinterpret_cast<void*>(secondaryHook),reinterpret_cast<void**>(&originalSecondary));
        addHook(engine.mp+0xba1570,reinterpret_cast<void*>(frameHook),reinterpret_cast<void**>(&originalFrame));
        addHook(engine.mp+0x13f8a80,reinterpret_cast<void*>(globalHook),reinterpret_cast<void**>(&originalGlobalFrame));
        addHook(engine.mp+0x14d7030,reinterpret_cast<void*>(damageHook),reinterpret_cast<void**>(&originalDamage));
        addHook(engine.mp+0x1407a27,reinterpret_cast<void*>(GigaBreakLE_FinalHook),&g_finalOriginal);
        addHook(engine.client+0x11382d0,reinterpret_cast<void*>(tempHook),reinterpret_cast<void**>(&originalTempUpdate));
        addHook(engine.client+0x113e5d0,reinterpret_cast<void*>(bonesHook),reinterpret_cast<void**>(&originalBones));
        // All trampolines exist before any detour can execute. Enable as a batch.
        if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK)throw std::runtime_error("Could not enable native detours");
        hooksEnabled=true;enabled.store(true,std::memory_order_release);
    }catch(...){if(!hooksEnabled)MH_Uninitialize();throw;}
}
DWORD WINAPI worker(void*){
    try{
        log(Version);log("WAITING: matching local mp.dll/client.dll and initialized native APIs.");
        std::wstring bin=directory(modulePath());
        if(!moduleBuild(reinterpret_cast<Address>(GetModuleHandleW(nullptr)),LauncherBuild))throw std::runtime_error("Unsupported launcher build");
        ULONGLONG deadline=GetTickCount64()+300000;
        while(GetTickCount64()<deadline){
            if(WaitForSingleObject(stopEvent,100)==WAIT_OBJECT_0){phase=5;SetEvent(resultEvent);return 0;}
            HMODULE mp=GetModuleHandleW(L"mp.dll"),client=GetModuleHandleW(L"client.dll");if(!mp||!client)continue;
            if(!samePath(modulePath(mp),bin+L"\\mp.dll")||!samePath(modulePath(client),bin+L"\\client.dll"))throw std::runtime_error("Unexpected game module path");
            engine.mp=reinterpret_cast<Address>(mp);engine.client=reinterpret_cast<Address>(client);
            if(!moduleBuild(engine.mp,ServerBuild)||!moduleBuild(engine.client,ClientBuild))throw std::runtime_error("Unsupported mp.dll/client.dll build");
            if(!resolveEngine())continue;
            // Pin the two game modules. Stale trampolines may not outlive their code.
            HMODULE held{};
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(mp),&held)||
                !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(client),&held))throw std::runtime_error("Cannot retain game modules");
            {
                std::lock_guard<std::recursive_mutex> lock(gameMutex);
                installHooks();phase=2;
            }
            SetEvent(resultEvent);log("READY: 9 native detours installed; only weapon 726 / local listen-server scope.");break;
        }
        if(phase!=2)throw std::runtime_error("Engine initialization timed out; no hooks installed");
        while(WaitForSingleObject(stopEvent,500)!=WAIT_OBJECT_0&&!stopRequested){}
        enabled=false;stopRequested=true;if(phase!=3)phase=4;
        log("STOP REQUESTED: waiting for game-thread status cleanup and client-thread visual cleanup.");
        // Do not unload a DLL or remove trampolines under in-flight callbacks.
        // Stop leaves inert pass-through hooks until the process exits.
        while(!(serverClean&&clientClean))Sleep(100);
        if(phase!=3)phase=5;
        char summary[180];sprintf_s(summary,"CLEANED: shots=%u shields=%u blocked=%u errors=%u. DLL remains inert until game exit.",shots.load(),shields.load(),blocked.load(),errors.load());log(summary);
    }catch(const std::exception& e){enabled=false;stopRequested=true;phase=3;log(e.what());log("FAILED: no supported ready state. Exit the game before retrying.");SetEvent(resultEvent);}
    catch(...){enabled=false;stopRequested=true;phase=3;log("FAILED: native initialization exception.");SetEvent(resultEvent);}
    return 0;
}
}
}
extern "C" DWORD WINAPI GigaBreakLE_Start(void*){
    using namespace giga_break_le;DWORD expected=0;if(!phase.compare_exchange_strong(expected,1))return ERROR_ALREADY_INITIALIZED;
    try{
        HMODULE pinned{};
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(GigaBreakLE_Start),&pinned))throw std::runtime_error("Cannot retain mod DLL");
        std::wstring path=directory(modulePath(selfModule))+L"\\native-runtime.log";
        logFile=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(logFile==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot write native runtime log");
        activeMutex=CreateMutexW(nullptr,FALSE,activeMutexName(GetCurrentProcessId()).c_str());
        if(!activeMutex||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Another native mod is active");
        stopEvent=CreateEventW(nullptr,TRUE,FALSE,stopEventName(GetCurrentProcessId()).c_str());
        resultEvent=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\CSNZ_GigaBreakLE_Native_Result_"+std::to_wstring(GetCurrentProcessId())).c_str());
        if(!stopEvent||!resultEvent)throw std::runtime_error("Cannot create native control events");
        HANDLE h=CreateThread(nullptr,0,worker,nullptr,0,nullptr);if(!h)throw std::runtime_error("Cannot start native worker");CloseHandle(h);return 0;
    }catch(...){phase=3;return ERROR_DLL_INIT_FAILED;}
}
extern "C" DWORD WINAPI GigaBreakLE_Stop(void*){giga_break_le::enabled=false;giga_break_le::stopRequested=true;if(giga_break_le::stopEvent)SetEvent(giga_break_le::stopEvent);return 0;}
extern "C" DWORD WINAPI GigaBreakLE_Status(void*){return giga_break_le::phase.load();}
extern "C" const char* __cdecl GigaBreakLE_Version(){return giga_break_le::Version;}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*){
    if(reason==DLL_PROCESS_ATTACH)giga_break_le::selfModule=module;
    // /MT's CRT still receives thread notifications; do not disable them.
    // No native calls, waits, detours or worker startup under the Windows loader lock.
    return TRUE;
}
