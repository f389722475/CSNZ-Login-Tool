#include "common.hpp"
#include <tlhelp32.h>
#include <iostream>
namespace aw {
static std::wstring processPath(DWORD pid) {
    Handle p(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    need(p.h != nullptr, "Cannot query process");
    std::wstring out(32768, 0);
    DWORD n = DWORD(out.size());
    need(QueryFullProcessImageNameW(p, 0, out.data(), &n) != 0, "Cannot read process path");
    out.resize(n);
    return out;
}
static DWORD findProcess(const fs::path &expected) {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    PROCESSENTRY32W e{sizeof(e)};
    DWORD match = 0;
    if (Process32FirstW(snap, &e))
        do {
            if (_wcsicmp(e.szExeFile, expected.filename().c_str()) == 0) {
                auto p = processPath(e.th32ProcessID);
                if (_wcsicmp(fs::weakly_canonical(p).c_str(), fs::weakly_canonical(expected).c_str()) == 0) {
                    need(!match, "Multiple matching processes; select one instance");
                    match = e.th32ProcessID;
                }
            }
        } while (Process32NextW(snap, &e));
    return match;
}
static uintptr_t moduleBase(DWORD pid, const std::wstring &name) {
    for (int attempt = 0; attempt < 5; attempt++) {
        Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        if (snap.h == INVALID_HANDLE_VALUE) {
            Sleep(20);
            continue;
        }
        MODULEENTRY32W m{sizeof(m)};
        if (Module32FirstW(snap, &m))
            do {
                if (_wcsicmp(m.szModule, name.c_str()) == 0)
                    return reinterpret_cast<uintptr_t>(m.modBaseAddr);
            } while (Module32NextW(snap, &m));
        return 0;
    }
    throw std::runtime_error("Module enumeration failed");
}
static void inject(DWORD pid, const fs::path &dll) {
    need(fs::exists(dll), "Native payload is missing");
    if (moduleBase(pid, dll.filename().wstring()))
        return;
    Handle p(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                             PROCESS_QUERY_INFORMATION,
                         FALSE, pid));
    need(p.h != nullptr, "Cannot open target process");
    auto load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HMODULE owner = nullptr;
    need(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(load), &owner) != 0,
         "LoadLibrary owner unavailable");
    auto remote = moduleBase(pid, imagePath(owner).filename().wstring());
    need(remote != 0, "Remote system module unavailable");
    auto proc = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        remote + (reinterpret_cast<uintptr_t>(load) - reinterpret_cast<uintptr_t>(owner)));
    auto path = fs::absolute(dll).wstring();
    SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void *mem = VirtualAllocEx(p, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    need(mem != nullptr, "Remote allocation failed");
    SIZE_T n = 0;
    if (!WriteProcessMemory(p, mem, path.c_str(), bytes, &n) || n != bytes) {
        VirtualFreeEx(p, mem, 0, MEM_RELEASE);
        throw std::runtime_error("Remote path write failed");
    }
    Handle t(CreateRemoteThread(p, nullptr, 0, proc, mem, 0, nullptr));
    if (!t.h) {
        VirtualFreeEx(p, mem, 0, MEM_RELEASE);
        throw std::runtime_error("Native loading failed");
    }
    DWORD wait = WaitForSingleObject(t, 10000);
    if (wait != WAIT_OBJECT_0)
        throw std::runtime_error("Native loading timed out (process left untouched)");
    VirtualFreeEx(p, mem, 0, MEM_RELEASE);
    need(moduleBase(pid, dll.filename().wstring()) != 0, "Native DLL not loaded");
}
static int run(int argc, wchar_t **argv) {
    fs::path root;
    std::string command;
    int uid = 1;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--root" && i + 1 < argc)
            root = argv[++i];
        else if (a == L"--command" && i + 1 < argc)
            command = utf8(argv[++i]);
        else if (a == L"--user" && i + 1 < argc)
            uid = std::stoi(argv[++i]);
        else
            throw std::runtime_error(
                "Usage: --root GAME_ROOT --command "
                "enable|disable|status|characters|catalog|unlock_all|refresh [--user ID]");
    }
    need(!root.empty() && root.is_absolute(), "An absolute game root is required");
    need(uid > 0, "Invalid userID");
    std::set<std::string> allowed = {"enable",  "disable",    "status", "characters",
                                     "catalog", "unlock_all", "refresh"};
    need(allowed.count(command) != 0, "Unknown command");
#ifdef AW_GAME
    constexpr bool game = true;
    auto exe = root / L"Bin" / L"CSOLauncher.exe";
    auto dll = imagePath().parent_path() / L"ClassAwakening.Observer.dll";
#else
    constexpr bool game = false;
    auto exe = root / L"Server" / L"CSNZ_Server.exe";
    auto dll = imagePath().parent_path() / L"ClassAwakening.Server.dll";
#endif
    DWORD pid = findProcess(exe);
    if (!pid) {
        std::cout
            << Json({{"ok", true},
                     {"result",
                      {{"pid", nullptr}, {"loaded", false}, {"enabled", false}, {"state", "not_running"}}}})
                   .dump()
            << std::endl;
        return 0;
    }
    if (command == "enable")
        inject(pid, dll);
    Json result;
    try {
        Json req = {{"command", command}, {"userID", uid}};
        if (command == "enable") {
            auto deadline = GetTickCount64() + (game ? 65000 : 8000);
            while (!WaitNamedPipeW(pipeName(pid, game).c_str(), 100) && GetTickCount64() < deadline)
                Sleep(50);
        }
        result = pipeCall(pid, req, game);
    } catch (const std::exception &) {
        if ((command == "status" || command == "disable") && !moduleBase(pid, dll.filename().wstring()))
            result = {{"pid", pid}, {"loaded", false}, {"enabled", false}, {"state", "not_loaded"}};
        else
            throw;
    }
    std::cout << Json({{"ok", true}, {"result", result}}).dump() << std::endl;
    return 0;
}
} // namespace aw
int wmain(int argc, wchar_t **argv) {
    try {
        return aw::run(argc, argv);
    } catch (const std::exception &e) {
        std::cout << aw::Json({{"ok", false}, {"error", e.what()}}).dump() << std::endl;
        return 1;
    }
}
