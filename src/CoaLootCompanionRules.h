/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef COA_LOOT_COMPANION_RULES_H
#define COA_LOOT_COMPANION_RULES_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// The loot pet of the alts (AiPlayerbot.AltLootCompanion, 2.9.5), apart from the game objects, so that
// tests/coa_loot_companion_rules.cpp can check it. The core loots the corpses around a player with a companion out
// (Ascension::CompanionLooting): any companion while the Loot-Transfigurator appearance is active, Lootbot 3000 and
// the Kleptomanic Demon by themselves outside raids. An alt brings one out and leaves those corpses to it.

constexpr std::uint32_t CoaLootCompanionCheckMs = 5000;

// A companion the alt knows: its summon spell, the creature it brings out, whether that creature loots by itself.
struct CoaKnownCompanion
{
    std::uint32_t spellId;
    std::uint32_t creatureEntry;
    bool selfLoots;
};

// When the alt may bring a companion out now.
struct CoaLootCompanionMoment
{
    bool alive;
    bool inCombat;
    bool mounted;
    bool flying;
    bool inBattlegroundOrArena;
    bool inRaid;
    bool lootAppearance;
    bool busy;  // casting, on a taxi, in a vehicle, controlled
};

inline bool CoaLootCompanionMomentOk(CoaLootCompanionMoment const& m)
{
    return m.alive && !m.inCombat && !m.mounted && !m.flying && !m.inBattlegroundOrArena && !m.busy &&
           (!m.inRaid || m.lootAppearance);
}

// A stealth or invisibility aura keeps the companion in only when the alt is really sneaking: a passive aura or one
// with no duration (a class's permanent form, e.g. the Reaper's) does not count.
inline bool CoaStealthBlocksCompanion(bool passive, bool permanent)
{
    return !passive && !permanent;
}

inline CoaKnownCompanion const* CoaFindCompanion(std::vector<CoaKnownCompanion> const& known, std::uint32_t spellId)
{
    for (CoaKnownCompanion const& companion : known)
        if (companion.spellId == spellId)
            return &companion;
    return nullptr;
}

// The list shown to the master: only the companions that loot by themselves, and the favourite (with the
// Loot-Transfigurator appearance any companion loots, so the favourite is the one that does), never every known pet.
inline std::vector<CoaKnownCompanion> CoaListedCompanions(std::vector<CoaKnownCompanion> const& known,
                                                          std::uint32_t favourite)
{
    std::vector<CoaKnownCompanion> listed;
    for (CoaKnownCompanion const& companion : known)
        if (companion.selfLoots || (favourite && companion.spellId == favourite))
            listed.push_back(companion);
    return listed;
}

// Whether this companion loots for the alt here.
inline bool CoaCompanionLootsHere(CoaKnownCompanion const& companion, bool lootAppearance, bool inRaid)
{
    return lootAppearance || (companion.selfLoots && !inRaid);
}

// The companion to bring out, its spell id (0: none). The favourite if it is known and loots here, else the first
// companion that loots by itself (outside raids), else with the appearance active any known companion. Lowest spell
// id first among equals, so that the choice does not hang on the order of the spell map.
inline std::uint32_t CoaChooseLootCompanion(std::vector<CoaKnownCompanion> const& known, std::uint32_t favourite,
                                            bool lootAppearance, bool inRaid)
{
    if (CoaKnownCompanion const* fav = favourite ? CoaFindCompanion(known, favourite) : nullptr)
        if (CoaCompanionLootsHere(*fav, lootAppearance, inRaid))
            return fav->spellId;
    std::uint32_t best = 0;
    for (CoaKnownCompanion const& companion : known)
        if (companion.selfLoots && !inRaid && (!best || companion.spellId < best))
            best = companion.spellId;
    if (best || !lootAppearance)
        return best;
    for (CoaKnownCompanion const& companion : known)
        if (!best || companion.spellId < best)
            best = companion.spellId;
    return best;
}

// Whether to cast the chosen companion now, given the companion already out (entry 0: none) and whether it loots.
// The chosen one already out stays; another one that loots stays too, unless the chosen one is the alt's favourite.
inline bool CoaLootCompanionNeedsSummon(std::uint32_t chosenEntry, bool chosenIsFavourite, std::uint32_t outEntry,
                                        bool outLoots)
{
    if (!chosenEntry || outEntry == chosenEntry)
        return false;
    return !outEntry || !outLoots || chosenIsFavourite;
}

// Whether the alt leaves this corpse to its companion instead of walking to it: the companion loots (or skins) here
// and reaches both the alt and the corpse (the core's condition in Player::LootCreatureWithCompanion), the corpse
// within the radius of the alt too. Farther corpses, and everything else, stay the alt's.
inline bool CoaLeaveCorpseToCompanion(bool enabled, bool companionLoots, float radius, float botToCorpse,
                                      float companionToCorpse, float companionToBot, bool companionSeesCorpse)
{
    return enabled && companionLoots && radius > 0.0f && botToCorpse <= radius && companionToCorpse <= radius &&
           companionToBot <= radius && companionSeesCorpse;
}

// The alt's choice, stored in the bot event store under "coa_loot_pet": value the favourite's spell id (1 when off
// with no favourite, 0 deletes the row: no favourite, on), data "off" when the alt brings out no companion.
struct CoaLootPetChoice
{
    std::uint32_t favourite = 0;
    bool off = false;
};

inline std::uint32_t CoaLootPetStoredValue(CoaLootPetChoice const& choice)
{
    return choice.favourite ? choice.favourite : (choice.off ? 1u : 0u);
}

inline std::string CoaLootPetStoredData(CoaLootPetChoice const& choice)
{
    return choice.off ? "off" : "";
}

inline CoaLootPetChoice CoaLootPetChoiceFrom(std::uint32_t value, std::string const& data)
{
    CoaLootPetChoice choice;
    choice.favourite = value > 1 ? value : 0;
    choice.off = data == "off";
    return choice;
}

// "loot pet" | "loot pet list" | "loot pet <spell id or link>" | "loot pet off" | "loot pet auto".
enum class CoaLootPetVerb
{
    Status,
    List,
    Set,
    Off,
    Auto,
    Unknown
};

inline CoaLootPetVerb CoaLootPetVerbOf(std::string arg)
{
    arg.erase(0, std::min(arg.find_first_not_of(' '), arg.size()));
    arg.erase(arg.find_last_not_of(' ') + 1);
    if (arg.empty() || arg == "status")
        return CoaLootPetVerb::Status;
    if (arg == "list")
        return CoaLootPetVerb::List;
    if (arg == "off")
        return CoaLootPetVerb::Off;
    if (arg == "auto" || arg == "on")
        return CoaLootPetVerb::Auto;
    return CoaLootPetVerb::Set;
}

// "loot pet off" puts away the companion the alt has out at once; the tick never brings one back while off.
inline bool CoaLootPetDismissNow(CoaLootPetVerb verb, bool companionOut)
{
    return verb == CoaLootPetVerb::Off && companionOut;
}

inline std::string CoaLootPetField(std::string text)
{
    std::replace(text.begin(), text.end(), '\t', ' ');
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}

// The lines to the SquidBots addon (prefix SQBA), in this order: one LP pet line per known companion, the LP fav
// line, the end line "LP\t-", so that no companion tells from no answer.
//   LP<TAB>pet<TAB><spell id><TAB><creature entry><TAB><name><TAB><1 loots by itself, else 0>
//   LP<TAB>app<TAB><1 Loot-Transfigurator appearance active (any companion loots), else 0>
//   LP<TAB>fav<TAB><favourite spell id, 0 for none><TAB><1 brings a companion out, 0 off>
//   LP<TAB>-
inline std::string CoaLootPetLine(CoaKnownCompanion const& companion, std::string const& name)
{
    return "LP\tpet\t" + std::to_string(companion.spellId) + "\t" + std::to_string(companion.creatureEntry) + "\t" +
           CoaLootPetField(name) + "\t" + (companion.selfLoots ? "1" : "0");
}

inline std::string CoaLootPetAppLine(bool lootAppearance)
{
    return std::string("LP\tapp\t") + (lootAppearance ? "1" : "0");
}

inline std::string CoaLootPetFavLine(CoaLootPetChoice const& choice)
{
    return "LP\tfav\t" + std::to_string(choice.favourite) + "\t" + (choice.off ? "0" : "1");
}

inline std::string CoaLootPetEndLine()
{
    return "LP\t-";
}

#endif
