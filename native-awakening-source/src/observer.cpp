#include "validate.hpp"
#include "MinHook.h"
namespace aw {
static std::mutex gate;
static Json events = Json::array();
static std::set<std::string> seen;
static std::atomic<bool> enabled = false;
static bool initialized = false;
static std::string error;
static std::atomic<unsigned long long> attachments = 0, clipCalls = 0;
using Pair = bool(__thiscall *)(void *, void *);
using Clip = int(__thiscall *)(void *);
static Pair originalPair = nullptr;
static Clip originalClip = nullptr;
static void event(Json j) {
    std::lock_guard lock(gate);
    j["timeMs"] = millis();
    events.push_back(std::move(j));
    if (events.size() > 60)
        events.erase(events.begin());
}
static Json weapon(unsigned char *w) {
    auto p = *reinterpret_cast<unsigned char **>(w + 0xd0);
    auto t = *reinterpret_cast<unsigned char **>(w + 0x210);
    Json trait = nullptr;
    if (t)
        trait = {{"itemID", *reinterpret_cast<unsigned int *>(t)},
                 {"category", *reinterpret_cast<unsigned int *>(t + 4)},
                 {"type", *reinterpret_cast<unsigned short *>(t + 8)},
                 {"subtype", *reinterpret_cast<unsigned short *>(t + 10)}};
    return {{"pointer", reinterpret_cast<uintptr_t>(w)},
            {"weaponID", *reinterpret_cast<unsigned int *>(w + 0xe4)},
            {"classID", p ? Json(*reinterpret_cast<unsigned short *>(p + 0x2156)) : Json(nullptr)},
            {"trait", trait}};
}
static bool __fastcall pairHook(void *self, void *, void *trait) {
    auto w = *reinterpret_cast<unsigned char **>(static_cast<unsigned char *>(self) + 4);
    bool result = originalPair(self, trait);
    if (enabled) {
        try {
            attachments++;
            event({{"kind", "paired_attached"}, {"weapon", weapon(w)}});
        } catch (const std::exception &) {
        }
    }
    return result;
}
static int __fastcall clipHook(void *self, void *) {
    int value = originalClip(self);
    if (enabled) {
        clipCalls++;
        try {
            auto w = static_cast<unsigned char *>(self);
            auto info = weapon(w);
            if (info.at("weaponID") == 473) {
                std::string key = std::to_string(reinterpret_cast<uintptr_t>(w)) + ":" +
                                  info.at("trait").dump() + ":" + std::to_string(value);
                bool fresh;
                {
                    std::lock_guard lock(gate);
                    if (seen.size() > 512)
                        seen.clear();
                    fresh = seen.insert(key).second;
                }
                if (fresh) {
                    event({{"kind", "max_clip"}, {"value", value}, {"weapon", info}});
                    bool paired = !info.at("trait").is_null() && info["trait"]["type"] == 1001 &&
                                  info["trait"]["subtype"] == 2;
                    auto data = *reinterpret_cast<unsigned char **>(w + 0x204);
                    auto prop = data + (paired ? 0xbcc : 0x930);
                    auto start = *reinterpret_cast<float **>(prop + 0x1c),
                         end = *reinterpret_cast<float **>(prop + 0x20);
                    if (end > start && end - start < 256)
                        event({{"kind", "tuning_read"},
                               {"property", "chargeTime"},
                               {"value", *start},
                               {"valueSource", "validated native branch property sampled with maxClip; not a "
                                               "charge invocation count"},
                               {"weapon", info}});
                }
            }
        } catch (const std::exception &) {
        }
    }
    return value;
}
static void initialize() {
    auto exe = imagePath();
    need(exe.filename() == L"CSOLauncher.exe", "Wrong observer host");
    HMODULE mp = nullptr;
    for (int i = 0; i < 600 && !mp; i++) {
        mp = GetModuleHandleW(L"mp.dll");
        if (!mp)
            Sleep(100);
    }
    need(mp != nullptr, "mp.dll not loaded");
    need(_wcsicmp(imagePath(mp).c_str(), (exe.parent_path() / L"mp.dll").c_str()) == 0,
         "Unexpected mp.dll path");
    validateModule(mp, gameProfile(), "module");
    need(MH_Initialize() == MH_OK, "Observer MinHook initialization failed");
    auto b = reinterpret_cast<unsigned char *>(mp);
    std::vector<void *> hooks;
    try {
        need(MH_CreateHook(b + 0x1537110, reinterpret_cast<void *>(pairHook),
                           reinterpret_cast<void **>(&originalPair)) == MH_OK,
             "Pairing hook failed");
        hooks.push_back(b + 0x1537110);
        need(MH_CreateHook(b + 0xf61320, reinterpret_cast<void *>(clipHook),
                           reinterpret_cast<void **>(&originalClip)) == MH_OK,
             "Clip hook failed");
        hooks.push_back(b + 0xf61320);
        for (auto p : hooks)
            need(MH_QueueEnableHook(p) == MH_OK, "Observer hook queue failed");
        need(MH_ApplyQueued() == MH_OK, "Observer hook enable failed");
        initialized = true;
        enabled = true;
    } catch (...) {
        for (auto p : hooks)
            MH_RemoveHook(p);
        throw;
    }
}
static Json status() {
    std::lock_guard lock(gate);
    return {{"version", Version},
            {"pid", GetCurrentProcessId()},
            {"loaded", initialized},
            {"enabled", enabled.load()},
            {"readOnly", true},
            {"error", error.empty() ? Json(nullptr) : Json(error)},
            {"counts", {{"attachments", attachments.load()}, {"maxClip", clipCalls.load()}}},
            {"recent", events},
            {"scope", "All pairing attachment callbacks; numeric tuning for Blistering Bolt Repeater only."}};
}
static DWORD WINAPI worker(void *) {
    try {
        initialize();
    } catch (const std::exception &e) {
        error = e.what();
    }
    try {
        servePipe(true, [](const Json &j) {
            auto c = j.at("command");
            if (c == "status")
                return status();
            need(initialized, error);
            if (c == "enable")
                enabled = true;
            else if (c == "disable")
                enabled = false;
            else
                throw std::runtime_error("Read-only observer command rejected");
            return status();
        });
    } catch (...) {
    }
    return 0;
}
} // namespace aw
BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(dll);
        HANDLE t = CreateThread(nullptr, 0, aw::worker, nullptr, 0, nullptr);
        if (t)
            CloseHandle(t);
    }
    return TRUE;
}
