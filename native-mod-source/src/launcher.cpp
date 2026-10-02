#include "platform.h"
#include <tlhelp32.h>
#include <cstdio>

namespace {
using namespace giga_break_le;
struct Config {std::wstring root,bin,launcher,server;unsigned port;};
std::wstring iniValue(const std::wstring& ini,const wchar_t* key,const wchar_t* fallback=L""){
    wchar_t value[32768];DWORD n=GetPrivateProfileStringW(L"Game",key,fallback,value,32768,ini.c_str());
    if(n>=32766)throw std::runtime_error("INI value is too long");return std::wstring(value,n);
}
bool regularFile(const std::wstring& path){DWORD a=GetFileAttributesW(path.c_str());return a!=INVALID_FILE_ATTRIBUTES&&!(a&FILE_ATTRIBUTE_DIRECTORY);}
std::wstring gameRoot(std::wstring root){
    if(root.size()>1&&root.front()==L'"'&&root.back()==L'"')root=root.substr(1,root.size()-2);
    root=fullPath(root);while(root.size()>3&&(root.back()==L'\\'||root.back()==L'/'))root.pop_back();
    auto name=root.substr(root.find_last_of(L"\\/")+1);
    if(_wcsicmp(name.c_str(),L"CSOLauncher.exe")==0&&regularFile(root))root=directory(root);
    name=root.substr(root.find_last_of(L"\\/")+1);
    if(_wcsicmp(name.c_str(),L"Bin")==0&&regularFile(root+L"\\CSOLauncher.exe")&&!regularFile(root+L"\\Bin\\CSOLauncher.exe"))root=directory(root);
    return root;
}
void requireBuild(const std::wstring& path,const Build& expected,const char* label){
    if(!regularFile(path))throw std::runtime_error(std::string("Missing ")+label+" under the configured game root. Select the folder containing Bin (or select Bin itself).");
    if(!fileBuild(path,expected))throw std::runtime_error(std::string("Unsupported PE build: ")+label+". Required machine=x86, timestamp="+std::to_string(expected.timestamp)+", SizeOfImage="+std::to_string(expected.imageSize)+". No files were changed.");
}
Config config(const std::wstring& home,bool validate=true,const std::wstring& overrideRoot=L""){
    std::wstring ini=home+L"\\native.ini",root=overrideRoot.empty()?iniValue(ini,L"Root"):overrideRoot;
    if(root.empty())throw std::runtime_error("Run Install.cmd first (native.ini is not configured)");
    root=gameRoot(root);
    std::wstring server=overrideRoot.empty()?iniValue(ini,L"Server",L"127.0.0.1"):L"127.0.0.1",port=overrideRoot.empty()?iniValue(ini,L"Port",L"30002"):L"30002";
    if(server.empty()||server.find_first_not_of(L"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-:[]")!=std::wstring::npos)throw std::runtime_error("Invalid server address");
    if(port.empty()||port.size()>5||port.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::runtime_error("Invalid server port");
    unsigned p=std::stoul(port);if(!p||p>65535)throw std::runtime_error("Invalid server port");
    Config c{root,root+L"\\Bin",root+L"\\Bin\\CSOLauncher.exe",server,p};
    if(validate){requireBuild(c.launcher,LauncherBuild,"Bin/CSOLauncher.exe");requireBuild(c.bin+L"\\mp.dll",ServerBuild,"Bin/mp.dll");requireBuild(c.bin+L"\\client.dll",ClientBuild,"Bin/client.dll");}
    return c;
}
std::wstring processPath(HANDLE process){
    wchar_t path[32768];DWORD n=32768;
    if(!QueryFullProcessImageNameW(process,0,path,&n))throw std::runtime_error("Cannot inspect target process");return std::wstring(path,n);
}
std::vector<DWORD> gameProcesses(const std::wstring& expected,bool rejectOld){
    Handle h(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(h.h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot list game processes");
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);std::vector<DWORD> result;
    for(BOOL ok=Process32FirstW(h,&entry);ok;ok=Process32NextW(h,&entry)){
        if(rejectOld&&_wcsicmp(entry.szExeFile,L"CSNZ_LEGuard.exe")==0)throw std::runtime_error("Close the old JS/Frida LEGuard before using the native edition");
        if(_wcsicmp(entry.szExeFile,L"CSOLauncher.exe"))continue;
        Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID));
        if(!p.h)throw std::runtime_error("A game process cannot be inspected; close it first");
        if(samePath(processPath(p),expected))result.push_back(entry.th32ProcessID);
    }
    return result;
}
MODULEENTRY32W remoteModule(DWORD pid,const std::wstring& path){
    for(int attempt=0;attempt<40;++attempt){
        Handle h(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
        if(h.h==INVALID_HANDLE_VALUE){Sleep(100);continue;}
        MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
        for(BOOL ok=Module32FirstW(h,&entry);ok;ok=Module32NextW(h,&entry))if(samePath(entry.szExePath,path))return entry;
        Sleep(100);
    }
    throw std::runtime_error("Expected module is not present in the game process");
}
DWORD callRemote(HANDLE process,Address function,void* argument,DWORD timeout=60000){
    Handle thread(CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(function),argument,0,nullptr));
    if(!thread.h)throw std::runtime_error("Cannot start the native DLL entry; check Windows security settings");
    if(WaitForSingleObject(thread,timeout)!=WAIT_OBJECT_0)throw std::runtime_error("Native entry timed out; exit the game before retrying");
    DWORD code{};if(!GetExitCodeThread(thread,&code))throw std::runtime_error("Cannot read native entry result");return code;
}
struct LocalDll {
    HMODULE h;
    explicit LocalDll(const std::wstring& file){
        h=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!h)throw std::runtime_error("Cannot load GigaBreakLE.dll (must be the packaged 32-bit DLL)");
    }
    ~LocalDll(){if(h)FreeLibrary(h);}
    Address entry(const char* name)const{auto p=GetProcAddress(h,name);if(!p)throw std::runtime_error("Native export missing");return reinterpret_cast<Address>(p);}
    Address rva(const char* name)const{return entry(name)-reinterpret_cast<Address>(h);}
    void check()const{
        auto version=reinterpret_cast<const char*(__cdecl*)()>(entry("GigaBreakLE_Version"));
        if(std::strcmp(version(),Version))throw std::runtime_error("Launcher/DLL version mismatch");
        entry("GigaBreakLE_Start");entry("GigaBreakLE_Stop");entry("GigaBreakLE_Status");
        if(reinterpret_cast<DWORD(WINAPI*)(void*)>(entry("GigaBreakLE_Status"))(nullptr)!=0)throw std::runtime_error("Unexpected DLL initial state");
    }
};
std::wstring quote(const std::wstring& s){if(s.find(L'"')!=std::wstring::npos)throw std::runtime_error("Unexpected quote in launch argument");return L"\""+s+L"\"";}
int start(const Config& c,const std::wstring& home){
    Handle mutex(CreateMutexW(nullptr,TRUE,L"Local\\CSNZ_GigaBreakLE_Native_Launcher"));
    if(!mutex.h||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Another native launcher is starting the game");
    if(!gameProcesses(c.launcher,true).empty())throw std::runtime_error("Exit the game first; this launcher never attaches to an existing session");
    std::wstring dllPath=home+L"\\GigaBreakLE.dll";LocalDll dll(dllPath);dll.check();
    std::wstring command=quote(c.launcher)+L" -ip "+quote(c.server)+L" -port "+std::to_wstring(c.port)+L" -loadmodeeventfromfile -loadzbskillfromfile -loadzombie5fromfile";
    STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
    if(!CreateProcessW(c.launcher.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,c.bin.c_str(),&si,&pi))throw std::runtime_error("Could not start CSOLauncher.exe");
    Handle process(pi.hProcess),primary(pi.hThread);
    std::wprintf(L"Game started (PID %lu). Loading the native DLL...\n",pi.dwProcessId);
    if(!samePath(processPath(process),c.launcher))throw std::runtime_error("Unexpected game process path");
    // LoadLibraryW may be forwarded to KernelBase. Find its actual owning module,
    // then apply the same RVA in the target instead of assuming identical ASLR.
    FARPROC load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");HMODULE owner{};
    if(!load||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(load),&owner))throw std::runtime_error("Cannot resolve LoadLibraryW");
    auto remoteOwner=remoteModule(pi.dwProcessId,modulePath(owner));
    Address loadRemote=reinterpret_cast<Address>(remoteOwner.modBaseAddr)+reinterpret_cast<Address>(load)-reinterpret_cast<Address>(owner);
    SIZE_T size=(dllPath.size()+1)*sizeof(wchar_t),written{};
    void* remotePath=VirtualAllocEx(process,nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remotePath)throw std::runtime_error("Cannot allocate the DLL path in the new game process");
    if(!WriteProcessMemory(process,remotePath,dllPath.c_str(),size,&written)||written!=size){VirtualFreeEx(process,remotePath,0,MEM_RELEASE);throw std::runtime_error("Cannot pass DLL path to the game");}
    // On timeout retain this one allocation until game exit: the thread may still
    // be using it. Never free a remote argument under a running loader thread.
    DWORD base=callRemote(process,loadRemote,remotePath);
    VirtualFreeEx(process,remotePath,0,MEM_RELEASE);
    if(!base)throw std::runtime_error("The game could not load GigaBreakLE.dll");
    auto remoteDll=remoteModule(pi.dwProcessId,dllPath);
    if(reinterpret_cast<Address>(remoteDll.modBaseAddr)!=base)throw std::runtime_error("Unexpected native DLL base");
    DWORD started=callRemote(process,base+dll.rva("GigaBreakLE_Start"),nullptr);
    if(started!=0)throw std::runtime_error("Native initialization rejected; see native-runtime.log");
    Handle ready(OpenEventW(SYNCHRONIZE,FALSE,(L"Local\\CSNZ_GigaBreakLE_Native_Result_"+std::to_wstring(pi.dwProcessId)).c_str()));
    if(!ready.h)throw std::runtime_error("Native startup signal is missing");
    std::wprintf(L"Waiting for the local server/client APIs (up to 5 minutes). Use the normal game login.\n");
    HANDLE waits[]{process.h,ready.h};DWORD event=WaitForMultipleObjects(2,waits,FALSE,305000);
    if(event==WAIT_OBJECT_0)throw std::runtime_error("The game exited before native initialization completed");
    if(event!=WAIT_OBJECT_0+1)throw std::runtime_error("Native initialization timed out; see native-runtime.log");
    if(callRemote(process,base+dll.rva("GigaBreakLE_Status"),nullptr)!=2)throw std::runtime_error("Native mod is not ready; see native-runtime.log and restart the game");
    std::wprintf(L"READY: native C++ mod loaded. This launcher may now close.\n");return 0;
}
int stop(const Config& c){
    auto ids=gameProcesses(c.launcher,false);unsigned count=0;
    for(DWORD pid:ids){Handle event(OpenEventW(EVENT_MODIFY_STATE,FALSE,stopEventName(pid).c_str()));if(event.h&&SetEvent(event))++count;}
    std::wprintf(L"Stop requested for %u native session(s). Cleanup runs on the next game/client frames.\n",count);
    std::wprintf(L"If paused, resume once or exit the game. The DLL stays inert until game exit.\n");return 0;
}
}
int wmain(int argc,wchar_t** argv){
    try{
        std::wstring home=giga_break_le::directory(giga_break_le::modulePath());std::wstring command=argc>1?argv[1]:L"--start";
        if(command==L"--self-check"){
            LocalDll dll(home+L"\\GigaBreakLE.dll");dll.check();std::wprintf(L"PASS: x86 DLL loads and exports match. No game or hooks started.\n");return 0;
        }
        if(command!=L"--check"&&command!=L"--check-root"&&command!=L"--start"&&command!=L"--stop")throw std::runtime_error("Use --start, --stop, --check, --check-root <root-or-Bin> or --self-check");
        if(command==L"--check-root"&&argc!=3)throw std::runtime_error("Use --check-root <game-root-or-Bin>");
        Config c=config(home,command!=L"--stop",command==L"--check-root"?argv[2]:L"");
        if(command==L"--check"||command==L"--check-root"){LocalDll dll(home+L"\\GigaBreakLE.dll");dll.check();std::wprintf(L"PASS: game PE build and native DLL load check. No game started.\nResolved game root: %ls\n",c.root.c_str());return 0;}
        return command==L"--stop"?stop(c):start(c,home);
    }catch(const std::exception& e){std::fprintf(stderr,"ERROR: %s\n",e.what());return 1;}
    catch(...){std::fprintf(stderr,"ERROR: unexpected native launcher failure\n");return 1;}
}
