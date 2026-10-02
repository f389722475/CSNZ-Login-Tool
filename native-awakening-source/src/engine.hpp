#pragma once
#include "common.hpp"
namespace aw {
// Exact MSVC x64 POD used on the wire. Never exchange C++ ownership across DLLs.
struct Record {
    int wire = 0;
    std::array<unsigned char, 5> stats{};
    unsigned char pad[3]{};
    std::array<std::array<int, 5>, 7> cats{};
};
static_assert(sizeof(Record) == 152);
inline constexpr const char *Columns[] = {"status",      "sessionbonus", "displayinfo", "modbuff",
                                          "activeskill", "passiveskill", "addon",       "pairingweapon"};
struct Trait {
    int category, type, subtype;
};
struct Catalog {
    Json config;
    std::array<int, 9> caps{};
    std::map<int, Trait> traits;
    std::map<int, Record> presets;
    int inventoryLimit;
    explicit Catalog(const fs::path &server);
    Json json() const;
};
class Db {
    HMODULE lib = nullptr;
    void *db = nullptr;
    int(__cdecl *open)(const char *, void **, int, const char *) = nullptr;
    int(__cdecl *close)(void *) = nullptr;
    int(__cdecl *prepare)(void *, const char *, int, void **, const char **) = nullptr;
    int(__cdecl *step)(void *) = nullptr;
    int(__cdecl *finalize)(void *) = nullptr;
    int(__cdecl *bindInt)(void *, int, long long) = nullptr;
    int(__cdecl *bindText)(void *, int, const char *, int, void(__cdecl *)(void *)) = nullptr;
    int(__cdecl *count)(void *) = nullptr;
    const char *(__cdecl *name)(void *, int) = nullptr;
    const unsigned char *(__cdecl *text)(void *, int) = nullptr;
    const char *(__cdecl *error)(void *) = nullptr;

  public:
    explicit Db(const fs::path &path);
    ~Db();
    Db(const Db &) = delete;
    Json sql(const std::string &, const Json &args = Json::array());
    void backup(const fs::path &path);
};
struct Mutation {
    Record record;
    std::set<int> changed;
    int op, slot;
};
class Engine {
  public:
    Catalog meta;
    Db db;
    std::recursive_mutex mutex;
    Engine(const fs::path &server, const fs::path &database) : meta(server), db(database) {}
    Record record(int uid, int slot, int item, int wire, bool unlockDefault = true);
    void save(int uid, int slot, const Record &);
    Mutation apply(int uid, const std::vector<unsigned char> &body, int offset);
    Json characters(int uid);
    Json unlock(int uid);
    Json unlockEveryone();

  private:
    Json owned(int uid, int slot);
    void consume(int uid, int item, int count, std::set<int> &changed, int preferred = -1);
    void grant(int uid, int item, std::set<int> &changed);
};
Json recordJson(const Record &);
} // namespace aw
