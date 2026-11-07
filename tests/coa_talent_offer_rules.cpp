#include "CoaTalentOfferRules.h"

#include <cassert>
#include <cstdint>

namespace
{
// One free-choice pair of each support specialization (CharacterAdvancement.dbc, client of 03/11): class, spec,
// the two entries and their group. Cost 0, one rank, in the specialization's tree.
struct FreePair
{
    std::uint32_t classId, specId, first, second, group;
};

constexpr FreePair Pairs[] = {
    { 12, 3, 9172, 11172, 8456221 },   // Barbarian, Ancestry
    { 16, 13, 11940, 34940, 128464 },  // Stormbringer, Wind
    { 18, 20, 13616, 29216, 743521 },  // Guardian, Inspiration
    { 20, 25, 11206, 11237, 143567 },  // Son of Arugal, Fleshweaver (abilities)
    { 21, 29, 6955, 11255, 247611 },   // Ranger, Survival
    { 31, 58, 6243, 11243, 127887 },   // Wildwalker, Life
};
}  // namespace

int main()
{
    for (FreePair const& pair : Pairs)
    {
        // Offered in the active specialization: both entries of the pair (the core keeps one at a time).
        assert(CoaTalentEntryOffered(pair.classId, pair.classId, 0, 0, 1, pair.group, pair.specId, pair.specId));
        // Not in another specialization of the class, nor to another class.
        assert(!CoaTalentEntryOffered(pair.classId, pair.classId, 0, 0, 1, pair.group, pair.specId, pair.specId + 1));
        assert(!CoaTalentEntryOffered(pair.classId, pair.classId + 1, 0, 0, 1, pair.group, pair.specId, pair.specId));
        // Without its group, a costless entry is an automatic rank: never offered.
        assert(!CoaTalentEntryOffered(pair.classId, pair.classId, 0, 0, 1, 0, pair.specId, pair.specId));
    }

    // Paid entries as before: class tree whatever the specialization, specialization tree only when active.
    assert(CoaTalentEntryOffered(12, 12, 1, 0, 3, 0, 0, 3));
    assert(CoaTalentEntryOffered(12, 12, 0, 1, 1, 0, 3, 3));
    assert(!CoaTalentEntryOffered(12, 12, 0, 1, 1, 0, 3, 4));
    // No rank: never.
    assert(!CoaTalentEntryOffered(12, 12, 1, 0, 0, 0, 0, 3));
    assert(!CoaTalentEntryOffered(12, 12, 0, 0, 0, 8456221, 3, 3));
    return 0;
}
