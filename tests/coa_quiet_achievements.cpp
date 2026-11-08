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
    return 0;
}
