#include "engine.hpp"
#include "validate.hpp"
#include "MinHook.h"
namespace aw {
static std::unique_ptr<Engine> engine;
static unsigned char *base;
static std::atomic<bool> enabled = false;
static bool initialized = false, unlocked = false;
static std::string initializationError;
static std::atomic<unsigned long long> requests = 0, commits = 0, rejected = 0, defaults = 0, deliveries = 0;
static std::mutex eventsMutex;
static Json events = Json::array();
static void event(Json e) {
    std::lock_guard lock(eventsMutex);
    e["timeMs"] = millis();
    if (e.value("kind", "") == "game_delivery") {
        for (auto i = events.begin(); i != events.end();)
            if (i->value("kind", "") == "game_delivery")
                i = events.erase(i);
            else
                ++i;
    }
    events.push_back(std::move(e));
    if (events.size() > 60)
        events.erase(events.begin());
}
template <class T> static T at(size_t rva) {
    return reinterpret_cast<T>(base + rva);
}
template <class T> static T method(void *o, size_t offset) {
    return *reinterpret_cast<T *>(*reinterpret_cast<unsigned char **>(o) + offset);
}
template <class T> struct WireVector {
    T *begin;
    T *end;
    T *capacity;
};
using Request = uint64_t (*)(void *, void *, void *);
using Default = Record *(*)(void *, Record *, int);
using Delivery = void (*)(void *, void *, int, WireVector<Record> *);
static Request originalRequest = nullptr;
static Default originalDefault = nullptr;
static Delivery originalDelivery = nullptr;
static int inventoryOffset() {
    auto cfg = *reinterpret_cast<unsigned char **>(base + 0x2e15c8);
    need(cfg != nullptr, "Server configuration not ready");
    auto begin = *reinterpret_cast<uintptr_t *>(cfg + 0x100),
         end = *reinterpret_cast<uintptr_t *>(cfg + 0x108);
    need(end >= begin && (end - begin) % 4 == 0 && (end - begin) / 4 <= 512, "Invalid inventory offset");
    return int((end - begin) / 4);
}
static void ack(void *conn, int result, int op, int wire) {
    at<void (*)(void *, void *, int, int, int)>(0xcbfd0)(base + 0x2d9210, conn, result, op, wire);
}
static void notify(void *conn, int uid, const Mutation &m) {
    if (!m.changed.empty()) {
        struct Inv {
            alignas(8) unsigned char b[88];
        };
        std::vector<Inv> records(m.changed.size());
        size_t i = 0, constructed = 0;
        try {
            for (int slot : m.changed) {
                auto q = records[i++].b;
                at<void *(*)(void *)>(0x3db90)(q);
                constructed++;
                if (!at<uint64_t (*)(void *, int, int, void *)>(0xa0cb0)(base + 0x2e25a0, uid, slot, q)) {
                    *reinterpret_cast<int *>(q) = slot;
                    *reinterpret_cast<int *>(q + 4) = 0;
                }
            }
            WireVector<Inv> v{records.data(), records.data() + records.size(),
                              records.data() + records.size()};
            at<void (*)(void *, void *, void *, int)>(0xcdae0)(base + 0x2d9210, conn, &v, 0);
        } catch (...) {
            for (size_t j = 0; j < constructed; j++)
                at<void (*)(void *)>(0x137b0)(records[j].b + 32);
            throw;
        }
        for (size_t j = 0; j < constructed; j++)
            at<void (*)(void *)>(0x137b0)(records[j].b + 32);
    }
    Record r = m.record;
    WireVector<Record> v{&r, &r + 1, &r + 1};
    at<void (*)(void *, void *, void *)>(0xcad40)(base + 0x2d9210, conn, &v);
    ack(conn, 0, m.op, r.wire);
}
static uint64_t requestHook(void *manager, void *packet, void *conn) {
    if (!enabled.load())
        return originalRequest(manager, packet, conn);
    int op = -1, wire = 0, uid = 0;
    try {
        auto p = static_cast<unsigned char *>(packet);
        auto begin = *reinterpret_cast<unsigned char **>(p + 16),
             end = *reinterpret_cast<unsigned char **>(p + 24);
        auto cursor = *reinterpret_cast<unsigned int *>(p + 40);
        need(end >= begin && end - begin >= 6 && end - begin < 256 && cursor == 5,
             "Invalid ClassMod envelope");
        std::vector<unsigned char> body(begin + cursor, end);
        op = body[0];
        if (body.size() >= 4)
            wire = body[2] | (body[3] << 8);
        auto user = method<void *(*)(void *, void *)>(manager, 0x120)(manager, conn);
        if (!user)
            return 0;
        uid = method<int (*)(void *)>(user, 0x78)(user);
        requests++;
        if (op == 6)
            return originalRequest(manager, packet, conn);
        std::lock_guard lock(engine->mutex);
        if (!enabled.load())
            return originalRequest(manager, packet, conn);
        auto m = engine->apply(uid, body, inventoryOffset());
        commits++;
        event({{"kind", "commit"},
               {"userID", uid},
               {"op", op},
               {"slot", m.slot},
               {"record", recordJson(m.record)},
               {"inventorySlots", m.changed}});
        try {
            notify(conn, uid, m);
        } catch (const std::exception &e) {
            event({{"kind", "delivery_error"}, {"message", e.what()}});
        }
        return 1;
    } catch (const std::exception &e) {
        rejected++;
        event({{"kind", "rejected"}, {"userID", uid}, {"op", op}, {"wire", wire}, {"message", e.what()}});
        if (op >= 0 && op <= 6)
            ack(conn, 1, op, wire);
        return 1;
    }
}
static Record *defaultHook(void *m, Record *out, int item) {
    auto ret = originalDefault(m, out, item);
    if (enabled.load()) {
        auto it = engine->meta.presets.find(item);
        if (it != engine->meta.presets.end()) {
            out->cats = it->second.cats;
            for (int c = 0; c < 7; c++)
                for (int i = 0; i < engine->meta.caps[c + 2]; i++)
                    out->cats[c][i] = std::max(0, out->cats[c][i]);
            defaults++;
        }
    }
    return ret;
}
static void deliveryHook(void *s, void *conn, int uid, WireVector<Record> *v) {
    if (enabled.load()) {
        try {
            auto size = reinterpret_cast<uintptr_t>(v->end) - reinterpret_cast<uintptr_t>(v->begin);
            if (size <= 256 * sizeof(Record) && size % sizeof(Record) == 0) {
                Json r = Json::array();
                for (auto p = v->begin; p < v->end; p++)
                    r.push_back(recordJson(*p));
                deliveries++;
                event({{"kind", "game_delivery"}, {"userID", uid}, {"records", r}});
            }
        } catch (const std::exception &) {
        }
    }
    originalDelivery(s, conn, uid, v);
}
static void initialize() {
    base = reinterpret_cast<unsigned char *>(GetModuleHandleW(nullptr));
    need(imagePath().filename() == L"CSNZ_Server.exe", "Wrong host executable");
    validateModule(reinterpret_cast<HMODULE>(base), serverProfile(), "server");
    auto server = imagePath().parent_path();
    engine = std::make_unique<Engine>(server, server / L"UserDatabase.db3");
    need(MH_Initialize() == MH_OK, "MinHook initialization failed");
    std::vector<void *> created;
    try {
        auto add = [&](size_t r, void *f, void **out) {
            void *target = base + r;
            need(MH_CreateHook(target, f, out) == MH_OK, "Hook creation failed");
            created.push_back(target);
        };
        add(0x607a0, reinterpret_cast<void *>(&requestHook), reinterpret_cast<void **>(&originalRequest));
        add(0x10c500, reinterpret_cast<void *>(&defaultHook), reinterpret_cast<void **>(&originalDefault));
        add(0xd5dd0, reinterpret_cast<void *>(&deliveryHook), reinterpret_cast<void **>(&originalDelivery));
        for (auto p : created)
            need(MH_QueueEnableHook(p) == MH_OK, "Hook queue failed");
        need(MH_ApplyQueued() == MH_OK, "Hook enable failed");
        initialized = true;
    } catch (...) {
        for (auto p : created)
            MH_RemoveHook(p);
        throw;
    }
}
static Json status() {
    std::lock_guard lock(eventsMutex);
    return {{"pid", GetCurrentProcessId()},
            {"version", Version},
            {"loaded", initialized},
            {"enabled", enabled.load()},
            {"error", initializationError.empty() ? Json(nullptr) : Json(initializationError)},
            {"runtime", "C++ / Win32 / system SQLite; no Python or Frida"},
            {"counters",
             {{"requests", requests.load()},
              {"commits", commits.load()},
              {"rejected", rejected.load()},
              {"defaults", defaults.load()},
              {"gameDeliveries", deliveries.load()}}},
            {"events", events},
            {"transfer", "not configured in this client build"}};
}
static fs::path backup() {
    auto folder = imagePath().parent_path() / L"AwakeningBackups";
    fs::create_directories(folder);
    auto p = folder / (L"UserDatabase-before-native-" + std::to_wstring(millis()) + L".db3");
    engine->db.backup(p);
    return p;
}
static Json command(const Json &j) {
    auto c = j.at("command").get<std::string>();
    if (c == "status")
        return status();
    need(initialized, initializationError.empty() ? "Plugin not initialized" : initializationError);
    std::lock_guard lock(engine->mutex);
    if (c == "enable") {
        inventoryOffset();
        if (!unlocked) {
            auto p = backup();
            auto result = engine->unlockEveryone();
            event({{"kind", "unlock_all"}, {"backup", utf8(p.wstring())}, {"result", result}});
            unlocked = true;
        }
        enabled = true;
        return status();
    }
    if (c == "disable") {
        enabled = false;
        return status();
    }
    if (c == "catalog")
        return engine->meta.json();
    if (c == "characters")
        return engine->characters(j.value("userID", 1));
    if (c == "unlock_all") {
        need(enabled.load(), "Enable the plugin first");
        auto p = backup();
        auto result = engine->unlock(j.value("userID", 1));
        result["backup"] = utf8(p.wstring());
        return result;
    }
    if (c == "refresh")
        return {{"queued", false},
                {"requires", "Reopen Class Awakening; no off-thread game calls are performed."}};
    throw std::runtime_error("Unknown typed command");
}
static DWORD WINAPI worker(void *) {
    try {
        initialize();
    } catch (const std::exception &e) {
        initializationError = e.what();
    }
    try {
        servePipe(false, command);
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
