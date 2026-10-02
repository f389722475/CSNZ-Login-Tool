#include "engine.hpp"
#include <iostream>
using namespace aw;
int wmain(int argc, wchar_t **argv) {
    try {
        need(argc == 3, "Usage: AwakeningTests SERVER_DIR BASELINE_DB");
        auto out =
            imagePath().parent_path().parent_path() / (L"engine-test-" + std::to_wstring(millis()) + L".db3");
        {
            Db source(argv[2]);
            source.backup(out);
        }
        Engine e(argv[1], out);
        int passed = 0;
        auto pass = [&](bool condition, const char *s) {
            need(condition, s);
            std::cout << "PASS " << s << '\n';
            passed++;
        };
        auto fail = [&](const std::vector<unsigned char> &b, const char *s) {
            auto inv = e.db.sql("SELECT * FROM UserInventory ORDER BY userID,slot");
            auto rec = e.db.sql("SELECT * FROM UserClassMod ORDER BY userID,slot");
            bool rejected = false;
            try {
                e.apply(1, b, 51);
            } catch (const std::exception &) {
                rejected = true;
            }
            pass(rejected && inv == e.db.sql("SELECT * FROM UserInventory ORDER BY userID,slot") &&
                     rec == e.db.sql("SELECT * FROM UserClassMod ORDER BY userID,slot"),
                 s);
        };
        auto packet = [&](int op, int cat, int index, int extra = 0, int protect = 0) {
            int w = 3363 + 51;
            std::vector<unsigned char> b{static_cast<unsigned char>(op), 16,
                                         static_cast<unsigned char>(w & 255),
                                         static_cast<unsigned char>(w >> 8)};
            if (op != 4)
                b.push_back(static_cast<unsigned char>(cat));
            if (op != 0 && op != 4)
                b.push_back(static_cast<unsigned char>(index));
            if (op == 1) {
                int t = extra + 51;
                b.insert(b.end(), {16, static_cast<unsigned char>(t & 255),
                                   static_cast<unsigned char>(t >> 8), static_cast<unsigned char>(protect)});
            }
            if (op == 2 || op == 3)
                b.push_back(static_cast<unsigned char>(extra));
            return b;
        };
        auto chars = e.characters(1);
        pass(chars.size() == 138, "eligible owned characters");
        auto r = e.record(1, 3363, 9536, 3414);
        int slots = 0;
        for (auto &a : r.cats)
            for (int v : a)
                slots += v >= 0;
        pass(slots == 20 && r.stats == std::array<unsigned char, 5>{26, 28, 24, 18, 19},
             "new/default character 20 legal slots and preserved stats");
        auto beforeInv = e.db.sql("SELECT * FROM UserInventory ORDER BY userID,slot");
        auto unlocked = e.unlock(1);
        pass(unlocked.at("changed") == 138 &&
                 beforeInv == e.db.sql("SELECT * FROM UserInventory ORDER BY userID,slot"),
             "unlock preserves inventory");
        pass(e.unlock(1).at("changed") == 0, "unlock idempotent");
        auto m = e.apply(1, packet(1, 8, 4, 3613), 51);
        pass(m.record.cats[6][4] == 9825 &&
                 e.db.sql("SELECT * FROM UserInventory WHERE userID=1 AND slot=3613").empty(),
             "equip consumes trait and supports fifth pairing slot");
        fail(packet(1, 8, 0, 3612), "duplicate pairing family atomic rejection");
        fail(packet(1, 6, 0, 3612), "wrong category atomic rejection");
        fail(packet(1, 2, 1, 3612), "structural slot limit atomic rejection");
        fail(packet(5, 8, 0), "unsupported transfer fail closed");
        auto bad = packet(2, 8, 4, 1);
        bad.pop_back();
        fail(bad, "malformed packet rejection");
        e.db.sql("UPDATE UserInventory SET count=0 WHERE userID=1 AND itemID=9870");
        fail(packet(2, 8, 4, 1), "extraction material shortage rolls back");
        e.db.sql("UPDATE UserInventory SET count=1 WHERE userID=1 AND itemID=9870");
        m = e.apply(1, packet(3, 8, 4, 3), 51);
        pass(m.record.cats[6][3] == 9825 && m.record.cats[6][4] == 0, "swap preserves equipped trait");
        m = e.apply(1, packet(2, 8, 3, 1), 51);
        pass(m.record.cats[6][3] == 0 &&
                 e.db.sql("SELECT * FROM UserInventory WHERE userID=1 AND itemID=9825 AND count=1").size() ==
                     1 &&
                 e.db.sql("SELECT * FROM UserInventory WHERE userID=1 AND itemID=9870").empty(),
             "extraction returns trait and consumes material");
        m = e.apply(1, packet(4, 0, 0), 51);
        int total = 0;
        bool valid = true;
        for (int v : m.record.stats) {
            total += v;
            valid &= v >= 15 && v <= 30;
        }
        pass(valid && total >= 105 && total <= 125 &&
                 e.db.sql("SELECT * FROM UserInventory WHERE userID=1 AND itemID=9868").empty(),
             "reroll ranges and material cost");
        fail(packet(4, 0, 0), "reroll shortage is atomic");
        fail(packet(0, 8, 0), "already unlocked category rejects without cost");
        // Replacement with extraction: consume new trait, return the old one atomically.
        auto rows = e.db.sql("SELECT slot FROM UserInventory WHERE userID=1 AND itemID=9825");
        int bslot = std::stoi(rows[0]["slot"].get<std::string>());
        e.apply(1, packet(1, 8, 4, bslot), 51);
        e.db.sql("INSERT INTO UserInventory VALUES (1,3658,9870,1,1,1,0,0,0,0,0,0,0,'',0,0,0)");
        m = e.apply(1, packet(1, 8, 4, 3612, 1), 51);
        pass(m.record.cats[6][4] == 9824 &&
                 e.db.sql("SELECT * FROM UserInventory WHERE userID=1 AND itemID=9825").size() == 1 &&
                 e.db.sql("SELECT * FROM UserInventory WHERE userID=1 AND itemID=9824").empty(),
             "protected replacement returns old trait");
        std::cout << Json({{"passed", passed},
                           {"database", utf8(out.wstring())},
                           {"originalDatabaseWritten", false}})
                         .dump()
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
