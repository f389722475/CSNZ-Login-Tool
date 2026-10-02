#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <set>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <sstream>
#include <stdexcept>
#include "json.hpp"
namespace aw {
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline constexpr char Version[] = "0.3.0-native";
inline void need(bool ok, const std::string &s) {
    if (!ok)
        throw std::runtime_error(s);
}
inline std::string utf8(const std::wstring &s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}
inline std::wstring wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), int(s.size()), nullptr, 0);
    need(n > 0 || s.empty(), "Invalid UTF-8");
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), int(s.size()), r.data(), n);
    return r;
}
inline fs::path imagePath(HMODULE module = nullptr) {
    std::wstring p(32768, 0);
    DWORD n = GetModuleFileNameW(module, p.data(), DWORD(p.size()));
    need(n && n < p.size(), "Module path unavailable");
    p.resize(n);
    return p;
}
inline Json readJson(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    need(bool(f), "Missing metadata: " + p.filename().string());
    return Json::parse(f);
}
inline long long minutes() {
    return std::chrono::duration_cast<std::chrono::minutes>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
inline unsigned long long millis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
inline std::wstring pipeName(DWORD pid, bool game = false) {
    return L"\\\\.\\pipe\\CSNZ.Awakening." + std::wstring(game ? L"Game." : L"Server.") +
           std::to_wstring(pid);
}
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE v = INVALID_HANDLE_VALUE) : h(v) {}
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE && h)
            CloseHandle(h);
    }
    operator HANDLE() const {
        return h;
    }
    Handle(const Handle &) = delete;
};
// Only SYSTEM and this account can send typed commands; no remote clients.
inline PSECURITY_DESCRIPTOR pipeSecurity() {
    Handle token;
    need(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.h) != 0, "Token unavailable");
    DWORD n = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &n);
    std::vector<BYTE> b(n);
    need(GetTokenInformation(token, TokenUser, b.data(), n, &n) != 0, "Token user unavailable");
    LPWSTR sid = nullptr;
    need(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(b.data())->User.Sid, &sid) != 0,
         "SID unavailable");
    std::wstring s = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR sd = nullptr;
    need(ConvertStringSecurityDescriptorToSecurityDescriptorW(s.c_str(), SDDL_REVISION_1, &sd, nullptr) != 0,
         "Pipe ACL unavailable");
    return sd;
}
inline void servePipe(bool game, const std::function<Json(const Json &)> &call) {
    auto sd = pipeSecurity();
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd, FALSE};
    for (;;) {
        Handle p(CreateNamedPipeW(
            pipeName(GetCurrentProcessId(), game).c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
            1024 * 1024, 65536, 0, &sa));
        if (p.h == INVALID_HANDLE_VALUE)
            break;
        while (!ConnectNamedPipe(p, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED)
            Sleep(25);
        char b[65536];
        DWORD n = 0;
        bool got = false;
        auto until = GetTickCount64() + 3000;
        while (GetTickCount64() < until) {
            if (ReadFile(p, b, sizeof(b), &n, nullptr)) {
                got = n > 0;
                break;
            }
            auto e = GetLastError();
            if (e != ERROR_NO_DATA && e != ERROR_PIPE_LISTENING)
                break;
            Sleep(10);
        }
        if (got) {
            Json result;
            try {
                result = {{"ok", true}, {"result", call(Json::parse(b, b + n))}};
            } catch (const std::exception &e) {
                result = {{"ok", false}, {"error", e.what()}};
            }
            auto out = result.dump();
            DWORD sent = 0;
            WriteFile(p, out.data(), DWORD(out.size()), &sent,
                      nullptr); // no flush: an abandoned client must never stall the control loop
            Sleep(30);
        }
        DisconnectNamedPipe(p);
    }
    LocalFree(sd);
}
inline Json pipeCall(DWORD pid, const Json &request, bool game = false) {
    const auto name = pipeName(pid, game);
    if (!WaitNamedPipeW(name.c_str(), 3000))
        throw std::runtime_error("Plugin pipe unavailable");
    Handle p(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    need(p.h != INVALID_HANDLE_VALUE, "Cannot open plugin pipe");
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(p, &mode, nullptr, nullptr);
    auto s = request.dump();
    DWORD n = 0;
    need(WriteFile(p, s.data(), DWORD(s.size()), &n, nullptr) && n == s.size(), "Pipe write failed");
    std::string out;
    char b[16384];
    for (;;) {
        BOOL ok = ReadFile(p, b, sizeof(b), &n, nullptr);
        out.append(b, n);
        need(out.size() < 2 * 1024 * 1024, "Oversized plugin response");
        if (ok)
            break;
        need(GetLastError() == ERROR_MORE_DATA, "Pipe response failed");
    }
    auto j = Json::parse(out);
    need(j.at("ok").get<bool>(), j.value("error", "Plugin failure"));
    return j.at("result");
}
} // namespace aw
