#include "CoaCityLifeRules.h"

#include <cassert>
#include <map>
#include <random>

using namespace CoaCityLife;

int main()
{
    std::mt19937 gen(3);
    auto between = [&](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(gen); };

    // Stations of the city.
    assert(StationOfCreature(0x00010000, "Innkeeper Gryshka") == Station::Inn);
    assert(StationOfCreature(0x00020000 | 0x1, "Banker") == Station::Bank);
    assert(StationOfCreature(0x00200000, "Auctioneer") == Station::Auction);
    assert(StationOfCreature(0, "Training Dummy") == Station::Dummy);
    assert(StationOfCreature(0x80, "Guard") == Station::None);
    assert(StationOfFocus(1) == Station::Forge && StationOfFocus(3) == Station::Forge && StationOfFocus(4) == Station::None);

    // The pick: weighted by kind, never a full station, nothing when all are full or none fits.
    std::vector<Candidate> c = {{Station::Inn, 1, 0}, {Station::Bank, 2, 0}, {Station::Forge, 3, MAX_BOTS_PER_STATION}};
    std::map<uint64_t, int> count;
    for (int i = 0; i < 5000; ++i)
        ++count[Pick(c, between)];
    assert(count[3] == 0 && count[0] == 0);
    assert(count[1] > count[2]);  // 30 against 20
    assert(Pick({{Station::Bank, 9, MAX_BOTS_PER_STATION}}, between) == 0);
    assert(Pick({{Station::None, 9, 0}}, between) == 0);
    assert(Pick({}, between) == 0);

    // Stays and poses.
    for (int i = 0; i < 200; ++i)
    {
        uint32_t const inn = StayMs(Station::Inn, between), bank = StayMs(Station::Bank, between);
        assert(inn >= 180000 && inn <= 480000 && bank >= 40000 && bank <= 120000);
    }
    assert(StayMs(Station::None, between) == 0);
    assert(PoseOf(Station::Inn).sit && !PoseOf(Station::Inn).emote);
    assert(PoseOf(Station::Forge).emote == 36 && PoseOf(Station::Dummy).emote == 35 && PoseOf(Station::Bank).emote == 1);
    assert(!PoseOf(Station::None).sit && !PoseOf(Station::None).emote);
    return 0;
}
