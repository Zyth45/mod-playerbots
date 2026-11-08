#include "CoaLootCompanionRules.h"

#include <cassert>

int main()
{
    CoaKnownCompanion const kitten{10673, 7385, false};
    CoaKnownCompanion const lootbot{90100, 44022, true};
    CoaKnownCompanion const demon{90200, 79026, true};
    std::vector<CoaKnownCompanion> const all{demon, kitten, lootbot};
    std::vector<CoaKnownCompanion> const onlyKitten{kitten};

    CoaLootCompanionMoment calm{true, false, false, false, false, false, false, false};
    assert(CoaLootCompanionMomentOk(calm));
    CoaLootCompanionMoment m = calm;
    m.alive = false;
    assert(!CoaLootCompanionMomentOk(m));
    m = calm;
    m.inCombat = true;
    assert(!CoaLootCompanionMomentOk(m));
    m = calm;
    m.mounted = true;
    assert(!CoaLootCompanionMomentOk(m));
    m = calm;
    m.flying = true;
    assert(!CoaLootCompanionMomentOk(m));
    m = calm;
    m.inBattlegroundOrArena = true;
    m.lootAppearance = true;
    assert(!CoaLootCompanionMomentOk(m));
    m = calm;
    m.busy = true;
    assert(!CoaLootCompanionMomentOk(m));
    // A raid: only with the appearance active (Lootbot and the Demon do not loot there).
    m = calm;
    m.inRaid = true;
    assert(!CoaLootCompanionMomentOk(m));
    m.lootAppearance = true;
    assert(CoaLootCompanionMomentOk(m));

    // No favourite: a companion that loots by itself, the lowest spell id.
    assert(CoaChooseLootCompanion(all, 0, false, false) == lootbot.spellId);
    // The favourite, known and looting here.
    assert(CoaChooseLootCompanion(all, demon.spellId, false, false) == demon.spellId);
    // A favourite that does not loot without the appearance: Lootbot instead.
    assert(CoaChooseLootCompanion(all, kitten.spellId, false, false) == lootbot.spellId);
    // With the appearance any companion loots: the favourite kitten.
    assert(CoaChooseLootCompanion(all, kitten.spellId, true, false) == kitten.spellId);
    // A favourite the alt no longer knows: as without one.
    assert(CoaChooseLootCompanion(onlyKitten, lootbot.spellId, true, false) == kitten.spellId);
    // Only an ordinary companion and no appearance: nothing.
    assert(CoaChooseLootCompanion(onlyKitten, 0, false, false) == 0);
    assert(CoaChooseLootCompanion({}, 0, true, false) == 0);
    // A raid without the appearance: nothing; with it, Lootbot comes out only as the favourite or the lowest id.
    assert(CoaChooseLootCompanion(all, 0, false, true) == 0);
    assert(CoaChooseLootCompanion(all, 0, true, true) == kitten.spellId);
    assert(CoaChooseLootCompanion(all, demon.spellId, true, true) == demon.spellId);

    // Nothing out: cast. The chosen one out: no cast (a second cast would dismiss it).
    assert(CoaLootCompanionNeedsSummon(44022, false, 0, false));
    assert(!CoaLootCompanionNeedsSummon(44022, true, 44022, true));
    assert(!CoaLootCompanionNeedsSummon(0, false, 0, false));
    // Another companion out that does not loot: replaced.
    assert(CoaLootCompanionNeedsSummon(44022, false, 7385, false));
    // Another one that loots: kept, unless the chosen one is the favourite.
    assert(!CoaLootCompanionNeedsSummon(44022, false, 79026, true));
    assert(CoaLootCompanionNeedsSummon(44022, true, 79026, true));

    // The corpse left to the companion: within reach of both, in sight.
    assert(CoaLeaveCorpseToCompanion(true, true, 40.0f, 30.0f, 32.0f, 3.0f, true));
    assert(!CoaLeaveCorpseToCompanion(false, true, 40.0f, 30.0f, 32.0f, 3.0f, true));
    assert(!CoaLeaveCorpseToCompanion(true, false, 40.0f, 30.0f, 32.0f, 3.0f, true));
    assert(!CoaLeaveCorpseToCompanion(true, true, 0.0f, 0.0f, 0.0f, 0.0f, true));
    assert(!CoaLeaveCorpseToCompanion(true, true, 40.0f, 45.0f, 32.0f, 3.0f, true));
    assert(!CoaLeaveCorpseToCompanion(true, true, 40.0f, 30.0f, 41.0f, 3.0f, true));
    assert(!CoaLeaveCorpseToCompanion(true, true, 40.0f, 30.0f, 32.0f, 42.0f, true));
    assert(!CoaLeaveCorpseToCompanion(true, true, 40.0f, 30.0f, 32.0f, 3.0f, false));
    // Goblin salvage field testing: the core's reach is 10 yards.
    assert(!CoaLeaveCorpseToCompanion(true, true, 10.0f, 12.0f, 11.0f, 2.0f, true));

    // The stored choice round-trips, and the default deletes the row.
    CoaLootPetChoice none;
    assert(CoaLootPetStoredValue(none) == 0 && CoaLootPetStoredData(none).empty());
    CoaLootPetChoice fav{lootbot.spellId, false};
    CoaLootPetChoice back = CoaLootPetChoiceFrom(CoaLootPetStoredValue(fav), CoaLootPetStoredData(fav));
    assert(back.favourite == lootbot.spellId && !back.off);
    CoaLootPetChoice off{0, true};
    assert(CoaLootPetStoredValue(off) == 1);
    back = CoaLootPetChoiceFrom(CoaLootPetStoredValue(off), CoaLootPetStoredData(off));
    assert(back.favourite == 0 && back.off);
    CoaLootPetChoice favOff{demon.spellId, true};
    back = CoaLootPetChoiceFrom(CoaLootPetStoredValue(favOff), CoaLootPetStoredData(favOff));
    assert(back.favourite == demon.spellId && back.off);

    assert(CoaLootPetVerbOf("") == CoaLootPetVerb::Status);
    assert(CoaLootPetVerbOf("  ") == CoaLootPetVerb::Status);
    assert(CoaLootPetVerbOf("status") == CoaLootPetVerb::Status);
    assert(CoaLootPetVerbOf("list") == CoaLootPetVerb::List);
    assert(CoaLootPetVerbOf(" off ") == CoaLootPetVerb::Off);
    assert(CoaLootPetVerbOf("auto") == CoaLootPetVerb::Auto);
    assert(CoaLootPetVerbOf("on") == CoaLootPetVerb::Auto);
    assert(CoaLootPetVerbOf("90100") == CoaLootPetVerb::Set);

    // The addon contract, verbatim.
    assert(CoaLootPetLine(lootbot, "Lootbot 3000") == "LP\tpet\t90100\t44022\tLootbot 3000\t1");
    assert(CoaLootPetLine(kitten, "Cat\tCarrier") == "LP\tpet\t10673\t7385\tCat Carrier\t0");
    assert(CoaLootPetFavLine(none) == "LP\tfav\t0\t1");
    assert(CoaLootPetFavLine(fav) == "LP\tfav\t90100\t1");
    assert(CoaLootPetFavLine(off) == "LP\tfav\t0\t0");
    assert(CoaLootPetEndLine() == "LP\t-");
    assert(CoaLootPetAppLine(true) == "LP\tapp\t1");
    assert(CoaLootPetAppLine(false) == "LP\tapp\t0");

    // Only the looting companions are listed, plus the favourite; never every known pet.
    std::vector<CoaKnownCompanion> const many = {kitten, lootbot, {10674, 7386, false}, {10675, 7387, false}};
    assert(CoaListedCompanions(many, 0).size() == 1 && CoaListedCompanions(many, 0)[0].spellId == 90100);
    std::vector<CoaKnownCompanion> const withFav = CoaListedCompanions(many, 10673);
    assert(withFav.size() == 2 && withFav[0].spellId == 10673 && withFav[1].spellId == 90100);
    assert(CoaListedCompanions({kitten}, 0).empty());
    return 0;
}
