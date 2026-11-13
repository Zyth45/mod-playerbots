#include "CoaCityLifeRules.h"

#include <cassert>
#include <cmath>
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
    assert(StationOfCreature(0x1, "Guard") == Station::None);
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

    // Beggars and buyers.
    assert(StationOfCreature(0x80, "Merchant") == Station::Vendor);
    assert(StationOfCreature(0x00020000 | 0x80, "Banker") == Station::Bank);  // a banker first
    assert(MaxBots(Station::Beg) == 1 && MaxBots(Station::Vendor) == MAX_BOTS_PER_STATION);
    assert(Pick({{Station::Beg, 7, 1}}, between) == 0);  // one beggar at a door already
    assert(Pick({{Station::Beg, 7, 0}}, between) == 7);
    assert(PoseOf(Station::Beg).emote == 20 && PoseOf(Station::Vendor).emote == 1);

    // Residents: about a tenth, always the same, three times the stay.
    int residents = 0;
    for (uint64_t g = 1; g <= 10000; ++g)
        residents += IsResident(g);
    assert(residents > 800 && residents < 1200);
    uint64_t r = 1;
    while (!IsResident(r))
        ++r;
    assert(StayMs(r, 600000) == 1800000 && IsResident(r) == IsResident(r));

    // Into the auction house's room: toward the middle, never past it, nothing when the middle is too close.
    float x = 0.0f, y = 0.0f;
    assert(IntoRoom(0.0f, 0.0f, 20.0f, 0.0f, 6.0f, 0.0f, x, y) && std::fabs(x - 6.0f) < 0.01f && std::fabs(y) < 0.01f);
    assert(IntoRoom(0.0f, 0.0f, 20.0f, 0.0f, 6.0f, 2.0f, x, y) && std::fabs(x - 6.0f) < 0.01f && std::fabs(y - 2.0f) < 0.01f);
    assert(IntoRoom(0.0f, 0.0f, 7.0f, 0.0f, ROOM_STEP_MAX, 0.0f, x, y) && x <= 7.0f + 0.01f);
    assert(!IntoRoom(0.0f, 0.0f, 3.0f, 0.0f, 6.0f, 0.0f, x, y));
    // Closer to the middle than the auctioneer, from any side.
    assert(IntoRoom(10.0f, 10.0f, 0.0f, 0.0f, 5.0f, -3.0f, x, y) && x * x + y * y < 200.0f);
    return 0;
}
