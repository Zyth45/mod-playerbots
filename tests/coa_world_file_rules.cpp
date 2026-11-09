#include "CoaWorldFileRules.h"

#include <cassert>

using namespace CoaWorldFile;

int main()
{
    // Activities.
    BotState s;
    s.rpg = "quest";
    assert(Classify(s) == Activity::Quest);
    s.dungeon = true;
    assert(Classify(s) == Activity::Dungeon);
    s.battleground = true;
    assert(Classify(s) == Activity::Pvp);
    s = BotState();
    s.rpg = "wander_npc";
    assert(Classify(s) == Activity::Grind);
    s.inCapital = true;
    assert(Classify(s) == Activity::City);
    s = BotState();
    s.rpg = "rest";
    s.fishing = true;
    assert(Classify(s) == Activity::Fish);
    s = BotState();
    s.rpg = "flight";
    assert(Classify(s) == Activity::Travel);
    s.rpg = "idle";
    assert(Classify(s) == Activity::Idle);
    s.rpg = "pvp";
    assert(Classify(s) == Activity::Pvp);

    // The watch request: "world" asked less than 90 s ago.
    assert(Watched("{\"at\": 1000, \"topics\": [\"world\", \"economy\"]}", 0, 1050));
    assert(!Watched("{\"at\": 1000, \"topics\": [\"world\"]}", 0, 1090));
    assert(!Watched("{\"at\": 1000, \"topics\": [\"economy\"]}", 0, 1010));
    assert(Watched("{\"topics\":[\"world\"]}", 1000, 1010));  // no "at": the file's date
    assert(!Watched("", 1000, 1010));

    // JSON.
    assert(Quote("a\"b\\c\n") == "\"a\\\"b\\\\c \"");
    Snapshot snap;
    snap.online = 3;
    snap.players = 1;
    snap.zones = {{0, 12, 1, false}, {1, 1637, 2, true}};
    snap.activities[uint8_t(Activity::City)] = 2;
    snap.activities[uint8_t(Activity::Quest)] = 1;
    std::string const data = Data(snap);
    assert(data.find("\"online\":3,\"players\":1") != std::string::npos);
    assert(data.find("\"zones\":[{\"map\":1,\"zone\":1637,\"bots\":2,\"capital\":true,\"faction\":{\"alliance\":0,\"horde\":0}}") != std::string::npos);
    assert(data.find("\"city\":2") != std::string::npos && data.find("\"fish\":0") != std::string::npos);
    assert(data.find("\"souls\":null,\"surname\":null") != std::string::npos);
    assert(data.find("\"tick_ms\":{\"avg\":null,\"p95\":null,\"max\":null}") != std::string::npos);
    assert(data.find("errors_10m") == std::string::npos);

    snap.soulsOn = true;
    snap.souls[1] = 2;
    snap.souls[4] = 1;
    assert(Data(snap).find("\"souls\":{\"lazy\":0,\"quester\":2,\"wanderer\":0,\"trader\":0,\"pvp\":1}") != std::string::npos);
    snap.surnameMode = 2;
    snap.surnameListSize = 64;
    snap.surnames = {{"Ashford", 3}, {"Stormhand", 5}};
    snap.tickAvgMs = 38;
    snap.tickMaxMs = 140;
    std::string const d2 = Data(snap);
    assert(d2.find("\"surname\":{\"mode\":2,\"list_size\":64,\"top\":[{\"name\":\"Stormhand\",\"bots\":5},"
                   "{\"name\":\"Ashford\",\"bots\":3}]}") != std::string::npos);
    assert(d2.find("\"avg\":38,\"p95\":null,\"max\":140") != std::string::npos);

    // Gathering, the factions of a zone, the p95 tick.
    {
        BotState g;
        g.gathering = true;
        g.rpg = "rest";
        assert(Classify(g) == Activity::Gather && std::string(ActivityName(Activity::Gather)) == "gather");
        g.fishing = true;
        assert(Classify(g) == Activity::Fish);
        Snapshot f;
        ZoneCount z;
        z.map = 0;
        z.zone = 1519;
        z.bots = 5;
        z.capital = true;
        z.alliance = 4;
        z.horde = 1;
        f.zones.push_back(z);
        f.tickAvgMs = 30;
        f.tickMaxMs = 90;
        f.tickP95Ms = 61;
        std::string const d3 = Data(f);
        assert(d3.find("\"capital\":true,\"faction\":{\"alliance\":4,\"horde\":1}}") != std::string::npos);
        assert(d3.find("\"avg\":30,\"p95\":61,\"max\":90") != std::string::npos);
        assert(d3.find("\"gather\":0") != std::string::npos);
    }

    std::string const env = Envelope(1762600000, 10, 420, 1762500000, "{}");
    assert(env == "{\"schema\":\"squidbot.world/1\",\"at\":1762600000,\"every\":10,\"build_us\":420,"
                  "\"server_start\":1762500000,\"module\":null,\"data\":{}}");
    return 0;
}
