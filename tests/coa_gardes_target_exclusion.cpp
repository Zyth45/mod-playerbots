// Target exclusion guard (hold fire), moved from Dungeon Clear: the table, the decision and the pure gates.
#include "Ai/Coa/Gardes/TargetExclusion/CoaTargetExclusionTable.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

namespace
{
    using namespace CoaTargetExclusion;

    // ---- Dungeon Clear 2.9.4, verbatim (DcTargetExclusionRegistry.cpp kRows + IsExcluded): the non-regression
    // reference. `gate` names the row's inForce function (0 = nullptr, i.e. always).
    struct DcRow
    {
        std::uint32_t mapId, entry;
        int gate;
        bool alsoTank;
    };

    DcRow const kDc294[] = {
        {469, 12435, 1, false},                                                    // Razorgore, egg phase
        {469, 11983, 2, true}, {469, 14601, 2, true}, {469, 11981, 2, true}, {469, 14020, 2, true},  // drake hall
        {604, 29982, 3, true},                                                     // Gundrak raider, purged
        {668, 36954, 4, true},                                                     // HoR Lich King, escape
        {532, 17211, 0, true}, {532, 17469, 0, true}, {532, 21160, 0, true}, {532, 21726, 0, true},
        {532, 21664, 0, true}, {532, 21748, 0, true}, {532, 21682, 0, true}, {532, 21747, 0, true},
        {532, 21683, 0, true}, {532, 21750, 0, true}, {532, 21684, 0, true}, {532, 21752, 0, true},
        {532, 22519, 0, true}, {532, 22521, 0, true},                              // chess pieces, triggers, fire
        {650, 34928, 5, false},                                                    // Paletress, shielded
    };

    template <class GateFn>
    bool DcIsExcluded(std::uint32_t mapId, std::uint32_t entry, bool forTank, GateFn&& inForce)
    {
        for (DcRow const& r : kDc294)
            if (r.mapId == mapId && r.entry == entry)
            {
                if (forTank && !r.alsoTank)
                    return false;
                return r.gate == 0 || inForce(r.gate);
            }
        return false;
    }

    // Gate verdicts as a bit set (bit g = gate g in force), so every combination can be swept.
    struct Gates
    {
        unsigned bits;
        bool operator()(Gate g) const { return bits & (1u << static_cast<unsigned>(g)); }
        bool operator()(int g) const { return bits & (1u << g); }
    };

    bool Never(Gate) { return false; }
}

int main()
{
    using namespace CoaTargetExclusion;

    // ---- which maps carry rows (TestBlackwingLair / CombatPurge / HoR / Chess / ToC) ----
    assert(HasRowsFor(469) && HasRowsFor(604) && HasRowsFor(668) && HasRowsFor(532) && HasRowsFor(650));
    assert(!HasRowsFor(409) && !HasRowsFor(608) && !HasRowsFor(0) && !HasRowsFor(470));
    // No classic CoA dungeon carries a row: a dungeon bot pays one table scan per pick.
    for (std::uint32_t map : {229u, 230u, 289u, 349u, 409u})
        assert(!HasRowsFor(map));

    // ---- with no live state (DC's nullptr bot): only the always-on rows bar ----
    assert(!Decide(469, Bwl::NPC_RAZORGORE, false, Never));
    assert(!Decide(469, 12422u, false, Never) && !Decide(469, 12416u, false, Never));  // eggs' adds: no row
    assert(!Decide(409, Bwl::NPC_RAZORGORE, false, Never));                           // wrong map
    for (std::uint32_t e : {Bwl::NPC_FIREMAW, Bwl::NPC_EBONROC, Bwl::NPC_FLAMEGOR, Bwl::NPC_CHROMAGGUS})
        assert(!Decide(469, e, false, Never) && !Decide(409, e, false, Never));
    assert(!Decide(469, Bwl::NPC_BROODLORD_LASHLAYER, false, Never));
    for (std::uint32_t e : {Karazhan::NPC_PAWN_A, Karazhan::NPC_KING_H, Karazhan::NPC_MOVE_TRIGGER, Karazhan::NPC_FIRE})
        assert(Decide(532, e, false, Never) && Decide(532, e, true, Never) && !Decide(469, e, false, Never));
    assert(!Decide(532, 15687u, false, Never));  // Moroes is no chess piece
    assert(!Decide(650, TrialOfTheChampion::NPC_PALETRESS, true, Never));

    // ---- the windows ----
    Gates const egg{1u << static_cast<unsigned>(Gate::RazorgoreEggPhase)};
    assert(Decide(469, Bwl::NPC_RAZORGORE, false, egg));
    assert(!Decide(469, Bwl::NPC_RAZORGORE, true, egg));  // the off-tank still holds him
    Gates const shield{1u << static_cast<unsigned>(Gate::PaletressShielded)};
    assert(Decide(650, TrialOfTheChampion::NPC_PALETRESS, false, shield));
    assert(!Decide(650, TrialOfTheChampion::NPC_PALETRESS, true, shield));  // the tank keeps her
    Gates const hall{1u << static_cast<unsigned>(Gate::BwlDrakeHallOutOfOrder)};
    assert(Decide(469, Bwl::NPC_FIREMAW, true, hall));  // the tank must not answer him either

    // ---- the pure gates ----
    assert(RazorgoreEggPhase(true, false, false) && !RazorgoreEggPhase(true, true, true));  // the script decides
    assert(RazorgoreEggPhase(false, false, true) && !RazorgoreEggPhase(false, false, false));
    assert(BwlDrakeHallOutOfOrder(false, true, false) && !BwlDrakeHallOutOfOrder(false, true, true));
    assert(BwlDrakeHallOutOfOrder(false, false, false));  // no script: still standing, the safe direction
    assert(!BwlDrakeHallOutOfOrder(true, true, false));   // operator skipped Broodlord
    assert(HorEscapeRunning(true, true, true) && !HorEscapeRunning(false, true, true) &&
           !HorEscapeRunning(true, false, false));
    assert(PaletressShielded(true, true, true, true) && !PaletressShielded(true, true, false, true) &&
           !PaletressShielded(true, false, true, true) && !PaletressShielded(true, true, true, false));

    // ---- NON-REGRESSION against Dungeon Clear 2.9.4: same rows, same order, same verdict for every input ----
    static_assert(sizeof(kRows) / sizeof(kRows[0]) == 22);
    assert(sizeof(kDc294) / sizeof(kDc294[0]) == sizeof(kRows) / sizeof(kRows[0]));
    for (unsigned i = 0; i < sizeof(kDc294) / sizeof(kDc294[0]); ++i)
    {
        assert(kRows[i].mapId == kDc294[i].mapId && kRows[i].entry == kDc294[i].entry);
        assert(static_cast<int>(kRows[i].gate) == kDc294[i].gate && kRows[i].alsoTank == kDc294[i].alsoTank);
    }

    unsigned checked = 0;
    std::uint32_t const maps[] = {0, 229, 230, 289, 349, 409, 469, 532, 604, 650, 668};
    for (unsigned bits = 0; bits < 64; ++bits)
    {
        Gates const g{bits};
        for (std::uint32_t map : maps)
        {
            for (DcRow const& r : kDc294)
                for (bool tank : {false, true})
                {
                    assert(Decide(map, r.entry, tank, g) == DcIsExcluded(map, r.entry, tank, g));
                    ++checked;
                }
            for (bool tank : {false, true})
                assert(Decide(map, 12017u, tank, g) == DcIsExcluded(map, 12017u, tank, g));
        }
    }

    std::printf("target exclusion: %u verdicts identical to Dungeon Clear 2.9.4\n", checked);
    return 0;
}
