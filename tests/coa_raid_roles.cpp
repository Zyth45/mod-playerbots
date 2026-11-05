#include "CoaRaidRoles.h"

#include <cassert>

namespace
{
void Check(std::uint32_t size, std::uint32_t tanks, std::uint32_t heals, std::uint32_t wantTanks, std::uint32_t wantHeals)
{
    CoaRaidRoles(size, tanks, heals);
    assert(tanks == wantTanks);
    assert(heals == wantHeals);
}
}

int main()
{
    // Up to 10: as before.
    Check(6, 0, 0, 2, 2);
    Check(10, 0, 0, 2, 3);
    // 11 to 20: 2 tanks, 3 healers.
    Check(11, 0, 0, 2, 3);
    Check(15, 0, 0, 2, 3);
    Check(20, 0, 0, 2, 3);
    // 21 to 25: 2 tanks, 4 healers.
    Check(21, 0, 0, 2, 4);
    Check(25, 0, 0, 2, 4);
    // Past 25: as before.
    Check(26, 0, 0, 3, 7);
    Check(40, 0, 0, 5, 10);
    // Counts the player gave are kept, one by one.
    Check(25, 3, 6, 3, 6);
    Check(15, 1, 0, 1, 3);
    Check(15, 0, 2, 2, 2);
    // One seat stays for a damage dealer.
    Check(6, 4, 4, 4, 1);
    Check(6, 9, 0, 5, 0);
}
