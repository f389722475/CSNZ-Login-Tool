#include <windows.h>
#include <cstring>
#include <cstdio>
#include "MinHook.h"

// CSNZ 0930 only. No disk patch, auth bypass, chat hook, TCP proxy or weapon logic.
// Send the normal /login packet through the existing engine connection, on the
// existing login callback thread. The server still decides whether to accept it.
namespace {
struct Credentials { DWORD size; char account[16]; char password[16]; };
static_assert(sizeof(Credentials) == 36);
Credentials credentials{};
BYTE* launcher{};
volatile LONG phase{}; // 0=not started, 1=hook ready, 2=request sent, 3=failed
volatile LONG attempted{};
HANDLE resultEvent{};
using NativeLogin = int(__stdcall*)(void*, void*);
using SendBody = void(__thiscall*)(void*, const char*, int);

bool executable(const void* address) {
    MEMORY_BASIC_INFORMATION info{};
    return address && VirtualQuery(address, &info, sizeof(info)) == sizeof(info) && info.State == MEM_COMMIT &&
        !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
bool build(BYTE* base, DWORD timestamp, DWORD imageSize) {
    if (!base) return false;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 0x1000) return false;
    auto pe = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    return pe->Signature == IMAGE_NT_SIGNATURE && pe->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        pe->FileHeader.TimeDateStamp == timestamp && pe->OptionalHeader.SizeOfImage == imageSize;
}
bool validCredentials(const Credentials& value) {
    if (value.size != sizeof(Credentials)) return false;
    size_t a = strnlen_s(value.account, 16), p = strnlen_s(value.password, 16);
    if (a < 5 || a > 15 || p < 5 || p > 15) return false;
    for (size_t i = 0; i < a; ++i) {
        char c = value.account[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    for (size_t i = 0; i < p; ++i) {
        unsigned char c = value.password[i];
        if (c < 33 || c > 126 || c == '"' || c == '\\') return false;
    }
    return true;
}
bool engineMatches(BYTE* engine) {
    const BYTE prologue[] = {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68};
    const BYTE loginReturn[] = {0x8b, 0xe5, 0x5d, 0xc2, 0x08, 0x00};
    return build(engine, 1783989493u, 73768960u) &&
        !memcmp(engine + 0xa53e50, prologue, sizeof(prologue)) &&
        !memcmp(engine + 0x9807b8, loginReturn, sizeof(loginReturn));
}

// Current hw.dll uses TWO stack arguments (RET 8). The old launcher wrapper's
// three-argument declaration is stale; do not copy that ABI into this bridge.
int __stdcall login(void* first, void* second) {
    auto native = *reinterpret_cast<NativeLogin*>(launcher + 0xbf190);
    if (!executable(reinterpret_cast<void*>(native))) {
        SecureZeroMemory(&credentials, sizeof(credentials));
        InterlockedExchange(&phase, 3); SetEvent(resultEvent); return 0;
    }
    if (InterlockedCompareExchange(&attempted, 1, 0) == 0) {
        bool sent = false;
        __try {
            auto engine = reinterpret_cast<BYTE*>(GetModuleHandleW(L"hw.dll"));
            if (engineMatches(engine) && validCredentials(credentials)) {
                auto connection = *reinterpret_cast<void**>(engine + 0x2227e38);
                // Same connection object and serializer used by native game packets.
                if (connection && *reinterpret_cast<void**>(static_cast<BYTE*>(connection) + 4)) {
                    char body[64] = {67, 1};
                    int length = sprintf_s(body + 2, sizeof(body) - 2, "/login %s %s", credentials.account, credentials.password);
                    if (length > 0) {
                        reinterpret_cast<SendBody>(engine + 0xa53e50)(connection, body, length + 3);
                        sent = true;
                    }
                    SecureZeroMemory(body, sizeof(body));
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { sent = false; }
        SecureZeroMemory(&credentials, sizeof(credentials));
        InterlockedExchange(&phase, sent ? 2 : 3); SetEvent(resultEvent);
    }
    return native(first, second);
}
}

extern "C" const char* __cdecl CSNZAuth_Version() { return "1.0.0"; }
extern "C" DWORD WINAPI CSNZAuth_Status(void*) { return static_cast<DWORD>(InterlockedCompareExchange(&phase, 0, 0)); }
extern "C" DWORD WINAPI CSNZAuth_Start(void* input) {
    if (InterlockedCompareExchange(&phase, 0, 0) != 0) return ERROR_ALREADY_INITIALIZED;
    launcher = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    const BYTE entry[] = {0x55, 0x8b, 0xec, 0x81, 0xec, 0x04, 0x02, 0, 0};
    if (!build(launcher, 1790721054u, 811008u) || memcmp(launcher + 0x5eb0, entry, sizeof(entry))) return ERROR_REVISION_MISMATCH;
    __try {
        if (!input || !validCredentials(*static_cast<Credentials*>(input))) return ERROR_INVALID_DATA;
        credentials = *static_cast<Credentials*>(input);
        SecureZeroMemory(input, sizeof(Credentials));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return ERROR_INVALID_DATA; }
    wchar_t name[96]; swprintf_s(name, L"Local\\CSNZ_Desktop_Auth_Result_%lu", GetCurrentProcessId());
    resultEvent = CreateEventW(nullptr, TRUE, FALSE, name);
    if (!resultEvent) { SecureZeroMemory(&credentials, sizeof(credentials)); return GetLastError(); }
    auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        SecureZeroMemory(&credentials, sizeof(credentials)); return ERROR_INVALID_FUNCTION;
    }
    // The original wrapper is never called: its PrintToChat path can silently
    // discard the command before any auth packet reaches the server.
    void* unused{};
    if (MH_CreateHook(launcher + 0x5eb0, reinterpret_cast<void*>(login), &unused) != MH_OK ||
        MH_EnableHook(launcher + 0x5eb0) != MH_OK) {
        SecureZeroMemory(&credentials, sizeof(credentials)); return ERROR_INVALID_FUNCTION;
    }
    InterlockedCompareExchange(&phase, 1, 0);
    return ERROR_SUCCESS;
}
BOOL WINAPI DllMain(HMODULE module, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    return TRUE;
}
