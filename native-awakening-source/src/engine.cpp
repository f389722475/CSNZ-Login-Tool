#include "engine.hpp"
#include <random>
namespace aw {
static std::vector<std::string> csv(const std::string &line) {
    std::vector<std::string> a;
    std::string s;
    bool q = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            if (q && i + 1 < line.size() && line[i + 1] == '"') {
                s += c;
                i++;
            } else
                q = !q;
        } else if (c == ',' && !q) {
            a.push_back(s);
            s.clear();
        } else if (c != '\r')
            s += c;
    }
    a.push_back(s);
    return a;
}
static int number(const std::string &s) {
    size_t n = 0;
    int v = std::stoi(s, &n);
    need(n == s.size(), "Invalid numeric metadata");
    return v;
}
static auto rows(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    need(bool(f), "Missing CSV: " + p.filename().string());
    std::vector<std::vector<std::string>> all;
    std::string l;
    while (std::getline(f, l)) {
        auto r = csv(l);
        if (!r.empty() && !r[0].empty() && r[0][0] >= '0' && r[0][0] <= '9')
            all.push_back(std::move(r));
    }
    return all;
}
Catalog::Catalog(const fs::path &p) {
    config = readJson(p / L"ClassModConfig.json");
    caps = config.at("CategoryMaxSlots").get<std::array<int, 9>>();
    need(caps == std::array<int, 9>{0, 0, 1, 1, 2, 1, 5, 5, 5}, "Unknown category limits");
    for (auto &r : rows(p / L"ClassMod.csv")) {
        need(r.size() >= 4, "Short trait row");
        int id = number(r[0]);
        Trait t{number(r[1]), number(r[2]), number(r[3])};
        need(t.category >= 2 && t.category <= 8 && traits.emplace(id, t).second, "Invalid/duplicate trait");
    }
    std::map<int, std::array<unsigned char, 5>> stats;
    for (auto &r : rows(p / L"ZBS_class.csv")) {
        need(r.size() >= 6, "Short stats row");
        std::array<unsigned char, 5> a;
        for (int i = 0; i < 5; i++) {
            int v = number(r[i + 1]) + 1;
            need(v >= 0 && v <= 30, "Invalid default stats");
            a[i] = static_cast<unsigned char>(v);
        }
        stats.emplace(number(r[0]), a);
    }
    for (auto &r : rows(p / L"ClassModPreset.csv")) {
        need(r.size() >= 22, "Short preset row");
        int id = number(r[0]);
        Record rec;
        rec.stats = stats.at(id);
        int k = 2;
        for (int c = 2; c <= 8; c++)
            for (int i = 0; i < 5; i++) {
                int v = i < caps[c] ? number(r[k++]) : -1;
                need(v >= -1, "Invalid preset value");
                need(v <= 0 || (traits.count(v) && traits.at(v).category == c), "Invalid preset trait");
                rec.cats[c - 2][i] = v;
            }
        need(presets.emplace(id, rec).second, "Duplicate preset");
    }
    inventoryLimit = readJson(p / L"ServerConfig.json").at("InventorySlotMax").get<int>();
    need(inventoryLimit > 1 && inventoryLimit <= 65000, "Invalid inventory limit");
    for (auto key : {"ClassMod_Item_Protect", "ClassMod_Item_Changes"}) {
        need(config.at(key).at("id").get<int>() > 0 && config.at(key).at("cost").get<int>() > 0,
             "Invalid material configuration");
    }
    auto costs = config.at("ClassMod_Item_AddSlot").at("costs");
    need(costs.size() == 20, "Invalid slot costs");
    for (auto &v : costs)
        need(v.get<int>() > 0, "Invalid slot cost");
    need(!presets.empty() && !traits.empty(), "Empty catalog");
}
Json recordJson(const Record &r) {
    return {{"wire", r.wire}, {"stats", r.stats}, {"categories", r.cats}};
}
Json Catalog::json() const {
    Json t = Json::object(), p = Json::object();
    for (auto &[id, v] : traits)
        t[std::to_string(id)] = {{"category", v.category}, {"type", v.type}, {"subtype", v.subtype}};
    for (auto &[id, v] : presets)
        p[std::to_string(id)] = recordJson(v);
    return {{"config", config},
            {"caps", caps},
            {"traits", t},
            {"presets", p},
            {"inventoryLimit", inventoryLimit}};
}
template <class T> static T api(HMODULE h, const char *n) {
    auto p = reinterpret_cast<T>(GetProcAddress(h, n));
    need(p != nullptr, std::string("Missing system SQLite API: ") + n);
    return p;
}
Db::Db(const fs::path &p) {
    lib = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    need(lib != nullptr, "Windows system SQLite unavailable");
#define SQL_FN(member, exportName) member = api<decltype(member)>(lib, "sqlite3_" exportName)
    SQL_FN(open, "open_v2");
    SQL_FN(close, "close_v2");
    SQL_FN(prepare, "prepare_v2");
    SQL_FN(step, "step");
    SQL_FN(finalize, "finalize");
    SQL_FN(bindInt, "bind_int64");
    SQL_FN(bindText, "bind_text");
    SQL_FN(count, "column_count");
    SQL_FN(name, "column_name");
    SQL_FN(text, "column_text");
    SQL_FN(error, "errmsg");
#undef SQL_FN
    int rc = open(utf8(p.wstring()).c_str(), &db, 0x10002, nullptr);
    if (rc) {
        if (db)
            close(db);
        db = nullptr;
        throw std::runtime_error("Cannot open existing database");
    }
    api<int(__cdecl *)(void *, int)>(lib, "sqlite3_busy_timeout")(db, 750);
}
Db::~Db() {
    if (db)
        close(db);
    if (lib)
        FreeLibrary(lib);
}
Json Db::sql(const std::string &s, const Json &args) {
    void *stmt = nullptr;
    need(prepare(db, s.c_str(), -1, &stmt, nullptr) == 0, error(db));
    struct Guard {
        void *s;
        int(__cdecl *f)(void *);
        ~Guard() {
            f(s);
        }
    } guard{stmt, finalize};
    std::vector<std::string> strings;
    strings.reserve(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        int rc;
        if (args[i].is_string()) {
            strings.push_back(args[i].get<std::string>());
            auto &v = strings.back();
            rc = bindText(stmt, int(i + 1), v.c_str(), int(v.size()),
                          reinterpret_cast<void(__cdecl *)(void *)>(-1));
        } else
            rc = bindInt(stmt, int(i + 1), args[i].get<long long>());
        need(rc == 0, error(db));
    }
    Json out = Json::array();
    for (;;) {
        int rc = step(stmt);
        if (rc == 101)
            break;
        need(rc == 100, error(db));
        Json row = Json::object();
        for (int i = 0; i < count(stmt); i++) {
            auto p = text(stmt, i);
            row[name(stmt, i)] = p ? Json(reinterpret_cast<const char *>(p)) : Json(nullptr);
        }
        out.push_back(std::move(row));
    }
    return out;
}
void Db::backup(const fs::path &p) {
    need(!fs::exists(p), "Backup already exists");
    void *dest = nullptr;
    need(open(utf8(p.wstring()).c_str(), &dest, 6, nullptr) == 0, "Backup create failed");
    auto init =
        api<void *(__cdecl *)(void *, const char *, void *, const char *)>(lib, "sqlite3_backup_init");
    auto stepBackup = api<int(__cdecl *)(void *, int)>(lib, "sqlite3_backup_step");
    auto finish = api<int(__cdecl *)(void *)>(lib, "sqlite3_backup_finish");
    void *b = init(dest, "main", db, "main");
    if (!b) {
        close(dest);
        throw std::runtime_error("Backup initialize failed");
    }
    int rc;
    int tries = 0;
    do {
        rc = stepBackup(b, 256);
        if (rc == 5 || rc == 6)
            Sleep(25);
    } while ((rc == 0 || rc == 5 || rc == 6) && ++tries < 400);
    int done = finish(b);
    close(dest);
    need(rc == 101 && done == 0, "Backup did not complete; no mutation performed");
}
static int integer(const Json &r, const char *k) {
    return number(r.at(k).get<std::string>());
}
static long long big(const Json &r, const char *k) {
    return std::stoll(r.at(k).get<std::string>());
}
static std::string joined(const auto &a) {
    std::string s;
    for (auto v : a) {
        if (!s.empty())
            s += ',';
        s += std::to_string(v);
    }
    return s;
}
Record Engine::record(int uid, int slot, int item, int wire, bool unlockDefault) {
    need(meta.presets.count(item) != 0, "Not an awakening character");
    auto rows = db.sql("SELECT * FROM UserClassMod WHERE userID=? AND slot=?", {uid, slot});
    need(rows.size() <= 1, "Duplicate ClassMod row");
    Record r = meta.presets.at(item);
    r.wire = wire;
    if (rows.empty()) {
        if (unlockDefault)
            for (int c = 0; c < 7; c++)
                for (int i = 0; i < meta.caps[c + 2]; i++)
                    r.cats[c][i] = std::max(0, r.cats[c][i]);
    } else {
        auto a = csv(rows[0].at("status").get<std::string>());
        need(a.size() == 5, "Invalid saved stats");
        for (int i = 0; i < 5; i++) {
            int v = number(a[i]);
            need(v >= 0 && v <= 30, "Invalid saved stats");
            r.stats[i] = static_cast<unsigned char>(v);
        }
        for (int c = 0; c < 7; c++) {
            auto cat = csv(rows[0].at(Columns[c + 1]).get<std::string>());
            need(cat.size() == 5, "Invalid saved category");
            for (int i = 0; i < 5; i++)
                r.cats[c][i] = number(cat[i]);
        }
    }
    for (int c = 0; c < 7; c++)
        for (int i = 0; i < 5; i++) {
            int v = r.cats[c][i];
            need(i < meta.caps[c + 2] ? v >= -1 : v == -1, "Invalid saved slot");
            need(v <= 0 || (meta.traits.count(v) && meta.traits.at(v).category == c + 2),
                 "Unknown or mismatched saved trait");
        }
    return r;
}
void Engine::save(int uid, int slot, const Record &r) {
    Json args = Json::array({joined(r.stats)});
    for (auto &a : r.cats)
        args.push_back(joined(a));
    bool exists = !db.sql("SELECT 1 FROM UserClassMod WHERE userID=? AND slot=?", {uid, slot}).empty();
    std::string q;
    if (exists) {
        q = "UPDATE UserClassMod SET ";
        for (int i = 0; i < 8; i++) {
            if (i)
                q += ',';
            q += std::string(Columns[i]) + "=?";
        }
        q += " WHERE userID=? AND slot=?";
        args.push_back(uid);
        args.push_back(slot);
    } else {
        q = "INSERT INTO UserClassMod "
            "(status,sessionbonus,displayinfo,modbuff,activeskill,passiveskill,addon,pairingweapon,userID,"
            "slot) VALUES (?,?,?,?,?,?,?,?,?,?)";
        args.push_back(uid);
        args.push_back(slot);
    }
    db.sql(q, args);
}
Json Engine::owned(int uid, int slot) {
    auto a = db.sql("SELECT * FROM UserInventory WHERE userID=? AND slot=?", {uid, slot});
    need(a.size() == 1 && integer(a[0], "count") > 0, "Inventory slot not owned");
    need(!big(a[0], "expiryDate") || big(a[0], "expiryDate") > minutes(), "Item expired");
    return a[0];
}
void Engine::consume(int uid, int item, int qty, std::set<int> &changed, int preferred) {
    need(qty > 0, "Invalid material cost");
    auto rows = db.sql("SELECT * FROM UserInventory WHERE userID=? AND itemID=? AND count>0 ORDER BY slot",
                       {uid, item});
    long long total = 0;
    Json usable = Json::array();
    for (auto &r : rows)
        if ((preferred < 0 || integer(r, "slot") == preferred) && !integer(r, "lockStatus") &&
            (!big(r, "expiryDate") || big(r, "expiryDate") > minutes())) {
            usable.push_back(r);
            total += integer(r, "count");
        }
    need(total >= qty, "Insufficient unlocked materials or trait");
    for (auto &r : usable) {
        if (!qty)
            break;
        int slot = integer(r, "slot"), take = std::min(qty, integer(r, "count")),
            remain = integer(r, "count") - take;
        qty -= take;
        if (remain)
            db.sql("UPDATE UserInventory SET count=? WHERE userID=? AND slot=?", {remain, uid, slot});
        else
            db.sql("DELETE FROM UserInventory WHERE userID=? AND slot=?", {uid, slot});
        changed.insert(slot);
    }
}
void Engine::grant(int uid, int item, std::set<int> &changed) {
    need(meta.traits.count(item) > 0, "Unknown return trait");
    auto a = db.sql("SELECT slot,count FROM UserInventory WHERE userID=? AND itemID=? AND expiryDate=0 AND "
                    "count<65535 AND lockStatus=0 ORDER BY slot LIMIT 1",
                    {uid, item});
    int slot;
    if (!a.empty()) {
        slot = integer(a[0], "slot");
        db.sql("UPDATE UserInventory SET count=count+1 WHERE userID=? AND slot=?", {uid, slot});
    } else {
        auto usedRows = db.sql("SELECT slot FROM UserInventory WHERE userID=?", {uid});
        std::set<int> used;
        for (auto &r : usedRows)
            used.insert(integer(r, "slot"));
        slot = 1;
        while (used.count(slot))
            slot++;
        need(slot < meta.inventoryLimit, "Inventory is full");
        db.sql("INSERT INTO UserInventory VALUES (?,?,?,1,1,1,?,0,0,0,0,0,0,'',0,0,0)",
               {uid, slot, item, minutes()});
    }
    changed.insert(slot);
}
Mutation Engine::apply(int uid, const std::vector<unsigned char> &b, int offset) {
    std::lock_guard lock(mutex);
    need(uid > 0, "Invalid user");
    need(b.size() >= 4, "Malformed awakening packet");
    int op = b[0];
    need(op >= 0 && op <= 4, "Unsupported operation: transfer is not configured");
    constexpr int lens[] = {5, 10, 7, 7, 4};
    need(b.size() == lens[op], "Malformed awakening packet");
    int wire = b[2] | (b[3] << 8), slot = wire - offset, cat = op != 4 ? b[4] : 0,
        index = (op != 4 && op != 0) ? b[5] : 0;
    need(slot > 0, "Pseudo-default character is not modifiable");
    Mutation m{};
    m.op = op;
    m.slot = slot;
    db.sql("BEGIN IMMEDIATE");
    try {
        auto inv = owned(uid, slot);
        m.record = record(uid, slot, integer(inv, "itemID"), wire);
        auto &r = m.record;
        auto &changed = m.changed;
        if (op != 4) {
            need(cat >= 2 && cat <= 8, "Invalid category");
            if (op != 0)
                need(index < meta.caps[cat] && r.cats[cat - 2][index] >= 0, "Invalid or locked slot");
        }
        if (op == 0) {
            auto &a = r.cats[cat - 2];
            auto end = a.begin() + meta.caps[cat];
            auto it = std::find(a.begin(), end, -1);
            need(it != end, "Category already full");
            int n = 0;
            for (auto &c : r.cats)
                for (int v : c)
                    n += v >= 0;
            need(n < 20, "All slots unlocked");
            auto cfg = meta.config.at("ClassMod_Item_AddSlot");
            consume(uid, cfg.at("id"), cfg.at("costs").at(n), changed);
            *it = 0;
        } // Packet bounds were checked before indexing.
        if (op == 1) {
            int tslot = (b[7] | (b[8] << 8)) - offset;
            auto t = owned(uid, tslot);
            int id = integer(t, "itemID"), protect = b[9];
            need(meta.traits.count(id) && meta.traits.at(id).category == cat, "Wrong trait category");
            need(protect == 0 || protect == 1, "Invalid extraction option");
            auto &a = r.cats[cat - 2];
            for (int i = 0; i < 5; i++)
                if (i != index) {
                    need(a[i] != id, "Identical trait already installed");
                    if (cat == 8 && a[i] > 0)
                        need(meta.traits.at(a[i]).type != meta.traits.at(id).type,
                             "Pairing weapon already installed");
                }
            need(a[index] != id, "Trait already equipped");
            consume(uid, id, 1, changed, tslot);
            if (a[index] > 0 && protect) {
                auto cfg = meta.config.at("ClassMod_Item_Protect");
                consume(uid, cfg.at("id"), cfg.at("cost"), changed);
                grant(uid, a[index], changed);
            }
            a[index] = id;
        } else if (op == 2) {
            auto &a = r.cats[cat - 2];
            int old = a[index], protect = b[6];
            need(old > 0, "Slot is empty");
            need(protect <= 1, "Invalid extraction option");
            if (protect) {
                auto cfg = meta.config.at("ClassMod_Item_Protect");
                consume(uid, cfg.at("id"), cfg.at("cost"), changed);
                grant(uid, old, changed);
            }
            a[index] = 0;
        } else if (op == 3) {
            auto &a = r.cats[cat - 2];
            int target = b[6];
            need(target < meta.caps[cat] && target != index && a[target] >= 0, "Invalid swap target");
            std::swap(a[index], a[target]);
        } else if (op == 4) {
            auto cfg = meta.config.at("ClassMod_Item_Changes");
            consume(uid, cfg.at("id"), cfg.at("cost"), changed);
            static std::mt19937 rng(std::random_device{}());
            r.stats.fill(15);
            int remain = std::uniform_int_distribution<int>(105, 125)(rng) - 75;
            while (remain) {
                int i = std::uniform_int_distribution<int>(0, 4)(rng);
                if (r.stats[i] < 30) {
                    r.stats[i]++;
                    remain--;
                }
            }
        }
        save(uid, slot, r);
        db.sql("COMMIT");
        return m;
    } catch (...) {
        try {
            db.sql("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
}
Json Engine::characters(int uid) {
    std::lock_guard lock(mutex);
    need(uid > 0, "Invalid user");
    Json out = Json::array();
    for (auto &i :
         db.sql("SELECT slot,itemID FROM UserInventory WHERE userID=? AND count>0 ORDER BY slot", {uid})) {
        int id = integer(i, "itemID"), slot = integer(i, "slot");
        if (!meta.presets.count(id))
            continue;
        auto r = record(uid, slot, id, 0);
        auto j = recordJson(r);
        j.erase("wire");
        int n = 0;
        for (auto &c : r.cats)
            for (int v : c)
                n += v >= 0;
        j.update({{"itemID", id}, {"slot", slot}, {"unlocked", n}, {"maxSlots", 20}});
        out.push_back(j);
    }
    return out;
}
Json Engine::unlock(int uid) {
    std::lock_guard lock(mutex);
    need(uid > 0, "Invalid user");
    Json changed = Json::array();
    db.sql("BEGIN IMMEDIATE");
    try {
        for (auto &i : db.sql("SELECT slot,itemID FROM UserInventory WHERE userID=? AND count>0 AND "
                              "(expiryDate=0 OR expiryDate>?) ORDER BY slot",
                              {uid, minutes()})) {
            int id = integer(i, "itemID"), slot = integer(i, "slot");
            if (!meta.presets.count(id))
                continue;
            auto r = record(uid, slot, id, 0, false);
            bool dirty = db.sql("SELECT 1 FROM UserClassMod WHERE userID=? AND slot=?", {uid, slot}).empty();
            for (int c = 0; c < 7; c++)
                for (int x = 0; x < meta.caps[c + 2]; x++)
                    if (r.cats[c][x] < 0) {
                        r.cats[c][x] = 0;
                        dirty = true;
                    }
            if (dirty) {
                save(uid, slot, r);
                changed.push_back(slot);
            }
        }
        db.sql("COMMIT");
    } catch (...) {
        try {
            db.sql("ROLLBACK");
        } catch (...) {
        }
        throw;
    }
    return {{"userID", uid},
            {"changed", changed.size()},
            {"slots", changed},
            {"refresh", "Reopen Class Awakening or rejoin the room."}};
}
Json Engine::unlockEveryone() {
    std::lock_guard lock(mutex);
    Json out = Json::array();
    for (auto &u : db.sql("SELECT DISTINCT userID FROM UserInventory"))
        out.push_back(unlock(integer(u, "userID")));
    return out;
}
} // namespace aw
