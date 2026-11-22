#include "CoaQuietAchievements.h"

#include <cassert>
#include <vector>

using namespace CoaQuietAchievements;

namespace
{
std::vector<uint8_t> Chat(uint8_t type, uint64_t sender)
{
    std::vector<uint8_t> b{type, 0, 0, 0, 0};
    for (int i = 0; i < 8; ++i)
        b.push_back(uint8_t(sender >> (8 * i)));
    b.insert(b.end(), {0, 0, 0, 0});
    return b;
}
}  // namespace

int main()
{
    uint64_t sender = 0;
    std::vector<uint8_t> b = Chat(0x30, 0x0000000000001234ull);  // CHAT_MSG_ACHIEVEMENT
    assert(AchievementChatSender(b.data(), b.size(), sender) && sender == 0x1234);
    b = Chat(0x31, 0x77);  // CHAT_MSG_GUILD_ACHIEVEMENT
    assert(AchievementChatSender(b.data(), b.size(), sender) && sender == 0x77);
    b = Chat(0x01, 0x77);  // a /say: never touched
    assert(!AchievementChatSender(b.data(), b.size(), sender));
    assert(!AchievementChatSender(b.data(), 6, sender));  // too short

    std::vector<uint8_t> first{'F', 'e', 's', 't', 'o', 'r', 'i', 'n', 0};
    for (int i = 0; i < 8; ++i)
        first.push_back(uint8_t(0x99ull >> (8 * i)));
    first.insert(first.end(), {1, 0, 0, 0});
    assert(ServerFirstSender(first.data(), first.size(), sender) && sender == 0x99);
    assert(!ServerFirstSender(first.data(), 5, sender));
    // 3.2: the first minutes online.
    assert(CoaQuietAchievements::InLoginWindow(1000, 181000));
    assert(!CoaQuietAchievements::InLoginWindow(181000, 181000));
    assert(!CoaQuietAchievements::InLoginWindow(5000, 0));
    // 3.2: one announcement per bot per gap.
    assert(!GapMutes(1000, 0, 60000));        // none announced yet
    assert(GapMutes(10000, 5000, 60000));     // 5 s after the last one
    assert(!GapMutes(70000, 5000, 60000));    // the gap is over
    assert(!GapMutes(10000, 5000, 0));        // 0: every one announced
    // 3.2: one announcement of a random bot per realm gap. 50 bots levelled up within 10 s: 1 announcement.
    {
        std::vector<uint64_t> botLast(50, 0);
        uint64_t realmLast = 0;
        int announced = 0;
        for (int i = 0; i < 50; ++i)
        {
            uint64_t const now = 100000 + uint64_t(i) * 200;  // 50 bots, 10 s
            if (Announces(now, botLast[i], realmLast, 60000, 60000))
            {
                botLast[i] = realmLast = now;
                ++announced;
            }
        }
        assert(announced == 1);
        assert(Announces(170000, 0, realmLast, 60000, 60000));     // the realm gap is over
        assert(!Announces(170000, 165000, 0, 60000, 60000));       // the gap per bot still holds
        assert(Announces(100200, 0, 100000, 60000, 0));             // 0: no realm gap
    }
    // 3.2.1: a level achievement below the bot's level replays it (the core saves no bot achievement).
    assert(LevelReplay(45, 10));
    assert(!LevelReplay(50, 50));    // just reached
    assert(!LevelReplay(45, 0));     // not a level achievement
    // 3.2.1: the fate counted, the gap per bot first.
    assert(GapFate(10000, 5000, 5000, 60000, 60000) == AnnounceFate::BotGap);
    assert(GapFate(10000, 0, 5000, 60000, 60000) == AnnounceFate::RealmGap);
    assert(GapFate(70000, 5000, 5000, 60000, 60000) == AnnounceFate::Shown);
    return 0;
}
