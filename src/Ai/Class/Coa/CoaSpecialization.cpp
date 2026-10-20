/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaSpecialization.h"
#include "CoaLevelBuildData.h"
#include "CoaSpecLookup.h"
#include "Channel.h"
#include "Chat.h"
#include "Config.h"
#include "ScriptMgr.h"

#include "AiFactory.h"
#include "BattlegroundMgr.h"
#include "DBCStores.h"
#include "LFGMgr.h"
#include "Group.h"
#include "InstanceSaveMgr.h"
#include "GroupMgr.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "Random.h"
#include "RandomBotLevelMgr.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldSession.h"
#include "WorldSessionMgr.h"
#include "AscensionSpecialization.h"
#include "AscensionCoATalentData.h"
#include "AscensionCoATalentState.h"
#include "AscensionSpellProgressionData.h"
#include "AscensionCustomClassData.h"
#include "AscensionLiveBaselineData.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <random>
#include <iterator>
#include <cstdlib>
#include <array>
#include <set>
#include <vector>
#include <ctime>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{

constexpr uint8 SpecializationLevel = 10;

// Nothing in the specialization data names a role. These come from the live tier lists,
// matched to the CoA specialization ids through the heals, taunts and mitigation each
// specialization's spells carry.
CoaRole RoleOf(uint32 specializationId)
{
    switch (specializationId)
    {
        case 6:   // Witch Doctor, Brewing
        case 25:  // Bloodmage, Fleshweaver
        case 31:  // Chronomancer, Time
        case 37:  // Pyromancer, Flameweaving
        case 40:  // Cultist, Heretic
        case 43:  // Starcaller, Moon Priest
        case 51:  // Tinker, Invention
        case 98:  // Sun Cleric, Blessings
        case 101: // Venomancer, Vizier
            return CoaRole::Heal;
        case 9:   // Felsworn, Tyrant
        case 17:  // Knight of Xoroth, Defiance
        case 21:  // Guardian, Vanguard
        case 22:  // Templar, Oathkeeper
        case 48:  // Sun Cleric, Seraphim
        case 52:  // Venomancer, Fortitude
        case 57:  // Reaper, Domination
        case 60:  // Primalist, Mountain King
        case 96:  // Cultist, Dreadnought
        case 97:  // Witch Hunter, Black Knight
        case 99:  // Bloodmage, Eternal
        case 100: // Starcaller, Moon Guard
            return CoaRole::Tank;
        default:
            return CoaRole::Dps;
    }
}

char const* RoleName(CoaRole role)
{
    switch (role)
    {
        case CoaRole::Tank: return "tank";
        case CoaRole::Heal: return "heal";
        default:            return "dps";
    }
}

// Specializations bots never play. Venomancer Vizier (101) heals from a scarab form that blocks its
// heals: in dungeons such a healer did nothing at all. Tinker Invention (51) knows its heals and the
// AI recognises them, but in a Razorfen Downs run at level 40 it cast its heal over time and one
// Zap!, nothing else: 27 healing a second where a Sun Cleric seven levels lower did 47, the tank
// healing itself almost as much. Out until its heals are understood.
bool IsExcludedSpecialization(uint32 specializationId)
{
    return sPlayerbotAIConfig.coaExcludedSpecializations.count(specializationId) != 0;
}

// Specializations of a class grouped by role, without those bots never play.
std::array<std::vector<uint32>, 3> SpecializationsByRole(uint8 classId)
{
    std::set<uint32> specializations;
    for (AscensionClassAbility const& learnable : GetAscensionClassAbilities(classId))
        if (learnable.SpecId && !IsExcludedSpecialization(learnable.SpecId))
            specializations.insert(learnable.SpecId);

    std::array<std::vector<uint32>, 3> byRole;
    for (uint32 specializationId : specializations)
        byRole[uint8(RoleOf(specializationId))].push_back(specializationId);

    return byRole;
}

struct SpecializationProfile
{
    uint16 SpecId;
    uint8 ClassId;
    CoaStyle Style;
    uint8 Stats;
};

constexpr uint8 STR = COA_STAT_STRENGTH;
constexpr uint8 AGI = COA_STAT_AGILITY;
constexpr uint8 INT = COA_STAT_INTELLECT;
constexpr uint8 SPI = COA_STAT_SPIRIT;
constexpr uint8 STA = COA_STAT_STAMINA;
constexpr CoaStyle MELEE = CoaStyle::Melee;
constexpr CoaStyle RANGED = CoaStyle::Ranged;
constexpr CoaStyle CASTER = CoaStyle::Caster;

// Roles and primary stats as ascensionsidekick.com lists them (coaSpecRoles). Healers cast from
// range; tanks and "Melee DPS" fight in melee; "Ranged DPS" on agility shoot, the others cast.
constexpr SpecializationProfile Profiles[] =
{
    { 1, 12, RANGED, AGI },       { 2, 12, MELEE, AGI },        { 3, 12, MELEE, AGI },         // Barbarian
    { 4, 13, RANGED, AGI },       { 5, 13, CASTER, INT | SPI }, { 6, 13, CASTER, SPI },        // Witch Doctor
    { 7, 14, CASTER, SPI | INT }, { 8, 14, MELEE, AGI },        { 9, 14, MELEE, AGI | STA },   // Felsworn
    { 10, 15, RANGED, AGI | INT }, { 11, 15, RANGED, AGI | INT }, { 12, 15, MELEE, AGI | INT }, // Witch Hunter
    { 97, 15, MELEE, AGI | STA },
    { 13, 16, CASTER, INT },      { 14, 16, CASTER, INT },      { 15, 16, CASTER, INT },       // Stormbringer
    { 16, 17, MELEE, STR | INT }, { 17, 17, MELEE, STR | STA }, { 18, 17, MELEE, STR },        // Knight of Xoroth
    { 19, 18, MELEE, STR },       { 20, 18, MELEE, STR },       { 21, 18, MELEE, STR | STA },  // Guardian
    { 22, 19, MELEE, AGI | STA }, { 23, 19, MELEE, AGI },       { 24, 19, MELEE, AGI },        // Templar
    { 25, 20, CASTER, SPI },      { 26, 20, CASTER, SPI | STA }, { 27, 20, MELEE, AGI },       // Bloodmage
    { 99, 20, MELEE, AGI | STA },
    { 28, 21, RANGED, AGI },      { 29, 21, RANGED, AGI },      { 30, 21, MELEE, AGI },        // Ranger
    { 31, 22, CASTER, SPI },      { 32, 22, CASTER, SPI },      { 33, 22, CASTER, SPI },       // Chronomancer
    { 34, 23, CASTER, INT },      { 35, 23, CASTER, INT },      { 36, 23, CASTER, INT },       // Necromancer
    { 37, 24, CASTER, SPI },      { 38, 24, CASTER, INT },      { 39, 24, CASTER, INT },       // Pyromancer
    { 40, 25, CASTER, INT | STR }, { 41, 25, CASTER, INT },     { 42, 25, MELEE, STR },        // Cultist
    { 96, 25, MELEE, STR | STA },
    { 43, 26, CASTER, INT },      { 44, 26, CASTER, INT },      { 45, 26, MELEE, INT },        // Starcaller
    { 100, 26, MELEE, INT | STA },
    { 46, 27, CASTER, INT },      { 47, 27, MELEE, STR },       { 48, 27, MELEE, STR | STA },  // Sun Cleric
    { 98, 27, CASTER, INT },
    { 49, 28, RANGED, AGI | INT }, { 50, 28, RANGED, AGI | INT }, { 51, 28, CASTER, INT },     // Tinker
    { 52, 29, MELEE, AGI | STA }, { 53, 29, MELEE, INT },       { 54, 29, CASTER, INT },       // Venomancer
    { 101, 29, CASTER, INT },
    { 55, 30, MELEE, STR },       { 56, 30, MELEE, STR },       { 57, 30, MELEE, STR | STA },  // Reaper
    { 58, 31, MELEE, STR | INT },  { 59, 31, MELEE, STR },      { 60, 31, MELEE, STR | STA },  // Primalist
    { 95, 31, CASTER, INT },
    { 61, 32, MELEE, AGI },       { 62, 32, CASTER, INT | SPI }, { 63, 32, MELEE, AGI },       // Runemaster
};

SpecializationProfile const* FindProfile(uint32 specializationId)
{
    for (SpecializationProfile const& profile : Profiles)
        if (profile.SpecId == specializationId)
            return &profile;

    return nullptr;
}

// The style most specializations of the class share (melee on a tie), with their stats.
CoaStyle ClassStyle(uint8 classId, uint8& stats)
{
    std::array<uint8, 3> counts = { 0, 0, 0 };
    for (SpecializationProfile const& profile : Profiles)
        if (profile.ClassId == classId)
            ++counts[uint8(profile.Style)];

    uint8 best = 0;
    for (uint8 style = 1; style < 3; ++style)
        if (counts[style] > counts[best])
            best = style;

    stats = 0;
    for (SpecializationProfile const& profile : Profiles)
        if (profile.ClassId == classId && uint8(profile.Style) == best)
            stats |= profile.Stats & ~STA;

    return CoaStyle(best);
}

}  // namespace

// Armor proficiencies of the 21 CoA classes, read from the proficiency spells CoA grants at
// character creation: acore_world.playercreateinfo_spell_custom, spells 750 (plate), 8737 (mail),
// 9077 (leather), 9078 (cloth) and 9116 (shield). Cross-checked against the skills the live
// characters actually carry (character_skills, skills 293/413/414/415/433): the two sources agree
// on all 21 classes, on every character, from level 1.
//
// The fallback_class of acore_world.ascension_custom_class is NOT usable here: Starcaller falls
// back to druid but wears plate and a shield, Stormbringer falls back to shaman but wears cloth
// only.
CoaArmorProficiency const* GetCoaArmorProficiency(uint8 playerClass)
{
    static constexpr CoaArmorProficiency Plate       = {ITEM_SUBCLASS_ARMOR_PLATE, false};
    static constexpr CoaArmorProficiency PlateShield = {ITEM_SUBCLASS_ARMOR_PLATE, true};
    static constexpr CoaArmorProficiency Mail        = {ITEM_SUBCLASS_ARMOR_MAIL, false};
    static constexpr CoaArmorProficiency MailShield  = {ITEM_SUBCLASS_ARMOR_MAIL, true};
    static constexpr CoaArmorProficiency Leather     = {ITEM_SUBCLASS_ARMOR_LEATHER, false};
    static constexpr CoaArmorProficiency Cloth       = {ITEM_SUBCLASS_ARMOR_CLOTH, false};

    switch (playerClass)
    {
        case CLASS_FLESHWARDEN:   return &PlateShield;  // Knight of Xoroth
        case CLASS_GUARDIAN:      return &PlateShield;
        case CLASS_CULTIST:       return &PlateShield;
        case CLASS_STARCALLER:    return &PlateShield;
        case CLASS_SUN_CLERIC:    return &PlateShield;
        case CLASS_REAPER:        return &Plate;
        case CLASS_WILDWALKER:    return &Plate;        // Primalist
        case CLASS_TINKER:        return &MailShield;
        case CLASS_WITCH_DOCTOR:  return &Mail;
        case CLASS_WITCH_HUNTER:  return &Mail;
        case CLASS_MONK:          return &Mail;         // Templar
        case CLASS_PROPHET:       return &Mail;         // Venomancer
        case CLASS_BARBARIAN:     return &Leather;
        case CLASS_DEMON_HUNTER:  return &Leather;      // Felsworn
        case CLASS_SON_OF_ARUGAL: return &Leather;      // Bloodmage
        case CLASS_RANGER:        return &Leather;
        case CLASS_SPIRIT_MAGE:   return &Leather;      // Runemaster
        case CLASS_STORMBRINGER:  return &Cloth;
        case CLASS_CHRONOMANCER:  return &Cloth;
        case CLASS_NECROMANCER:   return &Cloth;
        case CLASS_PYROMANCER:    return &Cloth;
        default:                  return nullptr;
    }
}

CoaRole GetCoaRole(Player const* player)
{
    if (!player || !IsAscensionCustomClassId(player->getClass()))
        return CoaRole::Dps;

    return RoleOf(GetAscensionActiveSpecialization(player));
}

CoaStyle GetCoaStyle(Player const* player)
{
    if (!player || !IsAscensionCustomClassId(player->getClass()))
        return CoaStyle::Melee;

    if (SpecializationProfile const* profile = FindProfile(GetAscensionActiveSpecialization(player)))
        return profile->Style;

    uint8 stats = 0;
    return ClassStyle(player->getClass(), stats);
}

uint8 GetCoaPrimaryStats(Player const* player)
{
    if (!player || !IsAscensionCustomClassId(player->getClass()))
        return 0;

    if (SpecializationProfile const* profile = FindProfile(GetAscensionActiveSpecialization(player)))
        return profile->Stats;

    uint8 stats = 0;
    ClassStyle(player->getClass(), stats);
    return stats;
}

// A stable number per bot and purpose, so the same bot keeps the same role and specialization
// across logins and restarts (splitmix64's finaliser, cut to 32 bits).
static uint32 Hash(uint32 value, uint32 salt)
{
    uint64 x = (uint64(value) << 32) ^ salt;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return uint32((x ^ (x >> 31)) & 0xFFFFFFFFu);
}

bool EnsureCoaSpecialization(Player* bot)
{
    if (!bot || !IsAscensionCustomClassId(bot->getClass()) || bot->GetLevel() < SpecializationLevel)
        return false;

    // Characters of real players, even played through the bot AI, keep their own choice.
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;

    // Explicit "talents spec <name>" choice.
    //
    // Random CoA bots normally receive a deterministic role/spec based on
    // their GUID. Once the player explicitly selects a specialization,
    // preserve that choice instead of allowing the automatic role picker
    // to overwrite it.
    uint32 const manualSpec = sRandomPlayerbotMgr.GetValue(bot, "coa_manual_spec");

    if (manualSpec)
    {
        uint32 const current = GetAscensionActiveSpecialization(bot);

        // Normally the CoA core has already persisted this specialization.
        // Reapply it only if something rebuilt/reloaded the bot with another one.
        if (current != manualSpec)
            return SwitchAscensionSpecialization(bot, manualSpec);

        return false;
    }

    std::array<std::vector<uint32>, 3> const byRole = SpecializationsByRole(bot->getClass());

    // Dungeon groups need a tank and a healer. Only 8 of the 21 classes can heal, so healing
    // weighs more than tanking to reach about one healer in eight specialized bots overall.
    // A role the class cannot fill gives its share to the others.
    std::array<uint32, 3> const shares = { 50, 20, 30 };
    uint32 total = 0;
    for (uint8 role = 0; role < 3; ++role)
        if (!byRole[role].empty())
            total += shares[role];

    if (!total)
        return false;

    // The role follows the bot's own guid, not a die: a character creation already gives every CoA
    // character a specialization, so without this the shares below would almost never apply - and a
    // die would hand the same bot a different role at every login, undoing its talents each time.
    uint32 roll = 1 + Hash(bot->GetGUID().GetCounter(), 0x5350454Cu) % total;   // 'SPEC'
    uint8 chosenRole = 0;
    for (uint8 role = 0; role < 3; ++role)
    {
        if (byRole[role].empty())
            continue;

        if (roll <= shares[role])
        {
            chosenRole = role;
            break;
        }
        roll -= shares[role];
    }

    std::vector<uint32> const& candidates = byRole[chosenRole];
    if (candidates.empty())
        return false;
    uint32 const specializationId = candidates[Hash(bot->GetGUID().GetCounter(), 0x50494B4Bu) % candidates.size()];   // 'PIKK'

    // The specialization a character is created with decides nothing about the group: keep it only
    // when it already plays the role this bot was given.
    uint32 const current = GetAscensionActiveSpecialization(bot);
    if (current == specializationId ||
        (current && !IsExcludedSpecialization(current) && uint8(RoleOf(current)) == chosenRole))
        return false;

    if (!SwitchAscensionSpecialization(bot, specializationId))
        return false;

    LOG_INFO("playerbots", "coa: {} (class {}, level {}) took specialization {} as {}",
             bot->GetName(), bot->getClass(), bot->GetLevel(), specializationId, RoleName(CoaRole(chosenRole)));
    return true;
}

AscensionCompatData::CoATalentEntry const* FindCoaEntry(uint32 entryId)
{
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
        if (entry.EntryId == entryId)
            return &entry;
    return nullptr;
}

// The paid entries of the bot's class it may hold now: its class tree and its active specialization's tree.
bool CoaEntryOffered(Player* bot, AscensionCompatData::CoATalentEntry const& entry)
{
    if (entry.ClassId != bot->getClass() || (!entry.AECost && !entry.TECost) || !entry.SpellCount)
        return false;
    return !entry.SpecId || entry.SpecId == GetAscensionActiveSpecialization(bot);
}

CoaPoints PointsOf(Player* bot)
{
    CoaPoints points;
    AscensionCompatData::GetCoATalentBudget(bot->getClass(), bot->GetLevel(), points.classBudget, points.specBudget);
    AscensionCoATalentState::SpentPoints const spent =
        AscensionCoATalentState::Spent(GetAscensionKnownTalentEntries(bot));
    points.classSpent = spent.AE;
    points.specSpent = spent.TE;
    return points;
}

// Why the next rank of this entry cannot be taken now, or empty: the rules the core applies to a player
// learning it from the talent frame (AscensionCompat SetTalentRank), which SetAscensionTalentRank leaves out.
std::string RefusalFor(Player* bot, AscensionCompatData::CoATalentEntry const& entry, uint32 rank)
{
    if (!CoaEntryOffered(bot, entry))
        return "not a talent of my class or of my active specialization";
    if (rank > entry.SpellCount)
        return "already at its highest rank";
    if (bot->GetLevel() < entry.RequiredLevel)
        return "requires level " + std::to_string(uint32(entry.RequiredLevel));
    for (uint32 const required : entry.RequiredEntryIds)
        if (required && !GetAscensionTalentRank(bot, required))
        {
            AscensionCompatData::CoATalentEntry const* before = FindCoaEntry(required);
            SpellInfo const* info = before && before->SpellIds[0] ? sSpellMgr->GetSpellInfo(before->SpellIds[0]) : nullptr;
            return "requires " + (info ? std::string(info->SpellName[0]) : std::to_string(required));
        }
    CoaPoints const points = PointsOf(bot);
    bool const classTree = !entry.SpecId;
    uint32 const cost = classTree ? entry.AECost : entry.TECost;
    if ((classTree ? points.classSpent : points.specSpent) + cost > (classTree ? points.classBudget : points.specBudget))
        return std::string("not enough ") + (classTree ? "class" : "specialization") + " points";
    return "";
}


namespace
{
// The free-choice group of an entry, or 0: entries of one group exclude each other, the core removes the others
// when one is set.
uint32 FreeGroupOf(uint32 entryId)
{
    for (AscensionCompatData::CoASelectableFreeEntry const& free : AscensionCompatData::CoASelectableFreeEntries)
        if (free.EntryId == entryId)
            return free.GroupId;
    return 0;
}

// Rank each entry should hold at the bot's level, in the order of the build: the build's picks up to that level,
// never an entry the Character Advancement does not offer yet at that level, never a rank the entry does not have,
// and one entry per free-choice group (the build's last).
std::vector<std::pair<uint32, uint8>> BuildRanksAtLevel(Player* bot, uint32 specializationId)
{
    // The build lists some end-game abilities early, and the core does not check the level when a rank is set:
    // level 10 bots cast Brutal Shot (spell level 59) for 2000 on a 548 health player (PvP bench, 28/09). Never a
    // rank the entry does not have either: the core refuses it, and a bot made above that pick had no rank at all.
    std::vector<std::pair<uint32, uint8>> wanted;
    for (CoaLevelBuildData::Pick const& pick : CoaLevelBuildData::Picks)
    {
        if (pick.ClassId != bot->getClass() || pick.SpecId != specializationId || pick.Level > bot->GetLevel())
            continue;
        AscensionCompatData::CoATalentEntry const* entry = FindCoaEntry(pick.EntryId);
        if (!entry || entry->RequiredLevel > bot->GetLevel())
            continue;

        uint8 const rank = std::min(pick.Rank, entry->SpellCount);
        auto itr = std::find_if(wanted.begin(), wanted.end(),
            [&pick](std::pair<uint32, uint8> const& w) { return w.first == pick.EntryId; });
        if (itr == wanted.end())
            wanted.emplace_back(pick.EntryId, rank);
        else if (itr->second < rank)
            itr->second = rank;
    }

    // Keeping two entries of one free-choice group in the build made the bot swap them back and forth on every
    // refresh.
    for (size_t i = wanted.size(); i-- > 0;)
    {
        uint32 const group = FreeGroupOf(wanted[i].first);
        if (!group)
            continue;
        for (size_t j = 0; j < i; ++j)
            if (FreeGroupOf(wanted[j].first) == group)
            {
                wanted.erase(wanted.begin() + j);
                --i;
                --j;
            }
    }
    return wanted;
}
}  // namespace

uint32 ApplyCoaTalents(Player* bot)
{
    if (!bot || !IsAscensionCustomClassId(bot->getClass()) || bot->GetLevel() < SpecializationLevel ||
        !sRandomPlayerbotMgr.IsRandomBot(bot))
        return 0;

    uint32 const specializationId = GetAscensionActiveSpecialization(bot);
    if (!specializationId)
        return 0;

    std::vector<std::pair<uint32, uint8>> const wanted = BuildRanksAtLevel(bot, specializationId);

    // The core teaches the class baseline again right after any change (SynchronizeProgression), so
    // lowering an entry whose spell is part of that baseline never sticks and was redone every refresh.
    auto const coreGrants = [bot](uint32 spellId)
    {
        for (AscensionLiveBaseline::Spell const& spell : AscensionLiveBaseline::Spells)
            if (spell.ClassId == bot->getClass() && spell.SpellId == spellId &&
                (!spell.RaceId || spell.RaceId == bot->getRace()))
                return true;
        for (AscensionCompatData::ClassSpell const& spell : AscensionCompatData::ClassSpells)
            if (spell.ClassId == bot->getClass() && spell.SpellId == spellId && spell.RequiredLevel <= bot->GetLevel())
                return true;
        return false;
    };

    // Two entries can hold the same spell: taking it away from the one the build does not want took it
    // from the wanted one too, which was raised again next time, for ever.
    std::unordered_set<uint32> wantedSpells;
    for (auto const& [entryId, rank] : wanted)
        if (AscensionCompatData::CoATalentEntry const* entry = FindCoaEntry(entryId))
            for (uint32 r = 0; r < rank && r < entry->SpellIds.size(); ++r)
                if (entry->SpellIds[r])
                    wantedSpells.insert(entry->SpellIds[r]);

    // Down first: a bot brought down in level kept the entries of the level it had, the core only ever
    // raises them (Fiery Judgement, spell level 31, on a level 3 Witch Hunter). Every entry of its class
    // above what the build holds at this level goes back down, to 0 when the build has none of it yet.
    uint32 lowered = 0;
    std::string changes;
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
    {
        if (entry.ClassId != bot->getClass())
            continue;
        auto itr = std::find_if(wanted.begin(), wanted.end(),
            [&entry](std::pair<uint32, uint8> const& w) { return w.first == entry.EntryId; });
        uint32 const allowed = itr == wanted.end() ? 0 : itr->second;
        uint32 const current = GetAscensionTalentRank(bot, entry.EntryId);
        if (current <= allowed || coreGrants(entry.SpellIds[current - 1]) || wantedSpells.count(entry.SpellIds[current - 1]))
            continue;
        if (SetAscensionTalentRank(bot, entry.EntryId, allowed) && GetAscensionTalentRank(bot, entry.EntryId) < current)
        {
            ++lowered;
            changes += " -" + std::to_string(entry.EntryId);
        }
    }
    // The higher ranks of an ability taken away stay otherwise: the rank table learns rank 2+ from the
    // first one, and nothing removes them with it.
    uint32 orphans = 0;
    if (lowered)
        for (AscensionProgression::Rank const& rank : AscensionProgression::Ranks)
            if (rank.ClassId == bot->getClass() && bot->HasSpell(rank.SpellId) && !bot->HasSpell(rank.FirstSpellId))
            {
                bot->removeSpell(rank.SpellId, SPEC_MASK_ALL, false);
                ++orphans;
            }
    // A rank left below one taken away stays inactive otherwise (see ReactivateHighestRanks).
    if (lowered)
        PlayerbotFactory::ReactivateHighestRanks(bot);

    uint32 raised = 0;
    for (auto const& [entryId, rank] : wanted)
        if (GetAscensionTalentRank(bot, entryId) < rank && SetAscensionTalentRank(bot, entryId, rank) &&
            GetAscensionTalentRank(bot, entryId) >= rank)
        {
            ++raised;
            changes += " +" + std::to_string(entryId);
        }

    if (raised || lowered)
        LOG_INFO("playerbots", "coa: {} (class {}, level {}, specialization {}) raised {}, lowered {} talent entries ({} orphan ranks):{}",
                 bot->GetName(), bot->getClass(), bot->GetLevel(), specializationId, raised, lowered, orphans, changes);
    return raised;
}

uint32 SpendCoaTalentPoints(Player* bot)
{
    if (!bot || !IsAscensionCustomClassId(bot->getClass()) || bot->GetLevel() < SpecializationLevel)
        return 0;

    uint32 const specializationId = GetAscensionActiveSpecialization(bot);
    if (!specializationId)
        return 0;

    uint32 raised = 0;
    std::string changes;
    for (auto const& [entryId, wantedRank] : BuildRanksAtLevel(bot, specializationId))
    {
        AscensionCompatData::CoATalentEntry const* entry = FindCoaEntry(entryId);
        if (!entry)
            continue;

        // The owner already chose another entry of this free-choice group: setting this one would take theirs away.
        if (uint32 const group = FreeGroupOf(entryId))
        {
            bool const otherChosen = std::any_of(AscensionCompatData::CoATalentEntries.begin(),
                AscensionCompatData::CoATalentEntries.end(),
                [bot, entryId, group](AscensionCompatData::CoATalentEntry const& other)
                {
                    return other.ClassId == bot->getClass() && other.EntryId != entryId &&
                        FreeGroupOf(other.EntryId) == group && GetAscensionTalentRank(bot, other.EntryId) > 0;
                });
            if (otherChosen)
                continue;
        }

        // One rank at a time, as the talent frame would: RefusalFor stops at the first rank the level, the
        // required entries or the points left do not allow.
        for (uint32 rank = GetAscensionTalentRank(bot, entryId) + 1; rank <= wantedRank; ++rank)
        {
            if (!RefusalFor(bot, *entry, rank).empty() || !SetAscensionTalentRank(bot, entryId, rank) ||
                GetAscensionTalentRank(bot, entryId) != rank)
                break;
            ++raised;
            changes += " +" + std::to_string(entryId) + "/" + std::to_string(rank);
        }
    }

    if (raised)
        LOG_INFO("playerbots", "coa: alt {} (class {}, level {}, specialization {}) spent {} talent ranks:{}",
                 bot->GetName(), bot->getClass(), bot->GetLevel(), specializationId, raised, changes);
    return raised;
}

bool SpendAltBotTalentPoints(Player* bot)
{
    if (!bot || !sPlayerbotAIConfig.autoPickTalentsAltBots)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || !botAI->IsAltBot())
        return false;

    if (IsAscensionCustomClassId(bot->getClass()))
        return SpendCoaTalentPoints(bot) > 0;

    if (bot->GetFreeTalentPoints() <= 0)
        return false;

    // Incremental and without reset: the points the owner already spent stay where they are.
    PlayerbotFactory factory(bot, bot->GetLevel());
    factory.InitTalentsTree(true, true, false);
    factory.InitPetTalents();
    return true;
}

namespace
{
constexpr char const* CoaClassNames[] =
{
    "Barbarian", "Witch Doctor", "Felsworn", "Witch Hunter", "Stormbringer", "Knight of Xoroth", "Guardian",
    "Templar", "Bloodmage", "Ranger", "Chronomancer", "Necromancer", "Pyromancer", "Cultist", "Starcaller",
    "Sun Cleric", "Tinker", "Venomancer", "Reaper", "Primalist", "Runemaster"
};
constexpr uint8 FirstCoaClass = 12;

std::string Folded(std::string const& text)
{
    std::string folded;
    for (char c : text)
        if (std::isalnum(static_cast<unsigned char>(c)))
            folded += char(std::tolower(static_cast<unsigned char>(c)));
    return folded;
}
}  // namespace

char const* CoaClassName(uint8 classId)
{
    if (classId < FirstCoaClass || classId >= FirstCoaClass + std::size(CoaClassNames))
        return nullptr;
    return CoaClassNames[classId - FirstCoaClass];
}

uint8 FindCoaClass(std::string const& name)
{
    std::string const wanted = Folded(name);
    if (wanted.empty())
        return 0;

    uint8 found = 0;
    for (uint8 i = 0; i < std::size(CoaClassNames); ++i)
    {
        std::string const candidate = Folded(CoaClassNames[i]);
        if (candidate == wanted)
            return FirstCoaClass + i;
        if (candidate.rfind(wanted, 0) == 0)
        {
            if (found)
                return 0;  // "wi" is Witch Doctor and Witch Hunter: say which
            found = FirstCoaClass + i;
        }
    }
    return found;
}

namespace
{
// The classes of the original game (Warcraft Reborn realms play them, not the CoA classes): their role is the
// talent tree they spent the most in, as the bot AI reads it, and they change role with a premade spec.
bool IsStockClass(uint8 classId)
{
    return classId && classId < CLASS_BARBARIAN && classId != CLASS_HERO;
}

CoaRole StockRole(Player* bot)
{
    if (PlayerbotAI::IsTank(bot, true))
        return CoaRole::Tank;
    return PlayerbotAI::IsHeal(bot, true) ? CoaRole::Heal : CoaRole::Dps;
}

CoaRole RoleOfMember(Player* player)
{
    return IsStockClass(player->getClass()) ? StockRole(player) : GetCoaRole(player);
}

bool ClassCanPlay(uint8 classId, CoaRole role)
{
    if (!IsStockClass(classId))
        return IsAscensionCustomClassId(classId) && !SpecializationsByRole(classId)[uint8(role)].empty();

    switch (role)
    {
        case CoaRole::Tank:
            return classId == CLASS_WARRIOR || classId == CLASS_PALADIN || classId == CLASS_DEATH_KNIGHT ||
                   classId == CLASS_DRUID;
        case CoaRole::Heal:
            return classId == CLASS_PRIEST || classId == CLASS_PALADIN || classId == CLASS_SHAMAN ||
                   classId == CLASS_DRUID;
        default:
            return true;
    }
}

bool FitsRole(Player* bot, CoaRole role)
{
    if (IsStockClass(bot->getClass()))
        return StockRole(bot) == role;
    uint32 const specialization = GetAscensionActiveSpecialization(bot);
    return specialization && !IsExcludedSpecialization(specialization) && GetCoaRole(bot) == role;
}

std::string ClassName(uint8 classId)
{
    if (char const* coaName = CoaClassName(classId))
        return coaName;
    ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId);
    return entry ? entry->name[sWorld->GetDefaultDbcLocale()] : "adventurer";
}

std::string SpecName(Player* bot)
{
    if (IsStockClass(bot->getClass()))
        return AiFactory::GetPlayerSpecName(bot);
    return std::to_string(GetAscensionActiveSpecialization(bot));
}

// Tries the premade specs of the class in a random order, PvE ones first, until the bot plays the role. Under
// level 10 the bot has no talent point and keeps the role of its class.
bool SwitchStockSpec(Player* bot, CoaRole role)
{
    uint8 const classId = bot->getClass();
    std::vector<int> specs;
    for (int specNo = 0; specNo < MAX_SPECNO; ++specNo)
        if (!sPlayerbotAIConfig.premadeSpecName[classId][specNo].empty())
            specs.push_back(specNo);
    std::shuffle(specs.begin(), specs.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
    std::stable_partition(specs.begin(), specs.end(), [classId](int specNo)
        { return sPlayerbotAIConfig.premadeSpecName[classId][specNo].find("pvp") == std::string::npos; });

    for (int specNo : specs)
    {
        PlayerbotFactory::InitTalentsBySpecNo(bot, specNo, true);
        if (StockRole(bot) == role)
            return true;
    }
    return false;
}
}  // namespace

Player* FindCoaRecruit(Player* master, CoaRole role, uint8 classId, std::set<ObjectGuid> const& skip, bool& chosenFits)
{
    // A free random bot whose class can fill the role, preferably on the master's map (a
    // dungeon instance has none, so any map will do), then one that already holds a
    // specialization of the role, then the nearest.
    Player* chosen = nullptr;
    bool chosenSameMap = false;
    chosenFits = false;
    uint32 chosenLevelGap = 0;
    float chosenDistance = 0.0f;
    for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
    {
        if (!bot || bot == master || !bot->IsInWorld() || bot->IsBeingTeleported() ||
            !(IsAscensionCustomClassId(bot->getClass()) || IsStockClass(bot->getClass())) || !bot->IsAlive() ||
            bot->IsInCombat() || bot->GetGroup() ||
            bot->GetGroupInvite() || bot->InBattleground() || bot->InBattlegroundQueue() || bot->IsInFlight() ||
            sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
            continue;

        // Already offered to another player (lfg bots) or otherwise set aside by the caller.
        if (skip.count(bot->GetGUID()) || CoaLfgOfferedToOther(bot, master))
            continue;

        // Held by a test bench for a match: recruited from there, it would leave its fight in the middle.
        if (RandomBotLevelMgr::IsHeld(bot->GetGUID()))
            continue;

        // The bot is added to the group directly, past the invitation checks, so the faction rule
        // has to be applied here: an Alliance player was handed a Forsaken healer, whom the first
        // city guard outside the dungeon would have attacked. A realm that allows cross-faction
        // groups still gets bots of the player's faction (jealous-sound #5266: six Horde tanks in a
        // row for an Alliance player, attacked by the Gnomeregan Exiles, deaf to "co ?" said in
        // Common), unless AiPlayerbot.CoaRecruitSameFaction is off.
        if (bot->GetTeamId() != master->GetTeamId() &&
            (sPlayerbotAIConfig.coaRecruitSameFaction || !sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP)))
            continue;

        if (classId && bot->getClass() != classId)
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || botAI->GetMaster())
            continue;

        bool const fits = FitsRole(bot, role);
        if (!fits && !ClassCanPlay(bot->getClass(), role))
            continue;
        // Without a talent point, at its level and at the master's, a stock class cannot change its role.
        if (!fits && IsStockClass(bot->getClass()) && bot->GetLevel() < 10 && master->GetLevel() < 10)
            continue;

        bool const sameMap = bot->GetMap() == master->GetMap();
        float const distance = sameMap ? master->GetDistance(bot) : 0.0f;
        // A bot close to the master's level needs no rebuild below, and keeps its own gear.
        uint32 const levelGap = uint32(std::abs(int32(bot->GetLevel()) - int32(master->GetLevel())));
        bool better = !chosen;
        if (!better && sameMap != chosenSameMap)
            better = sameMap;
        else if (!better && fits != chosenFits)
            better = fits;
        else if (!better && levelGap != chosenLevelGap)
            better = levelGap < chosenLevelGap;
        else if (!better)
            better = sameMap && distance < chosenDistance;

        if (better)
        {
            chosen = bot;
            chosenSameMap = sameMap;
            chosenFits = fits;
            chosenLevelGap = levelGap;
            chosenDistance = distance;
        }
    }

    return chosen;
}

// Takes off the tank stances a bot wears (its own lasting auras raising its threat): one recruited
// to heal or deal damage kept the one it wore before, and pulled the monsters off the tank.
void DropThreatStances(Player* bot)
{
    std::vector<uint32> worn;
    for (auto const& [id, application] : bot->GetAppliedAuras())
    {
        Aura const* aura = application->GetBase();
        SpellInfo const* info = aura->GetSpellInfo();
        if (aura->GetCasterGUID() != bot->GetGUID() || info->IsPassive() || aura->GetMaxDuration() >= 0)
            continue;
        for (SpellEffectInfo const& effect : info->Effects)
            if (effect.IsAura() && effect.ApplyAuraName == SPELL_AURA_MOD_THREAT && effect.CalcValue() > 0)
            {
                worn.push_back(info->Id);
                break;
            }
    }
    for (uint32 spellId : worn)
        bot->RemoveAurasDueToSpell(spellId);
}

void CoaRestoreAncestralCombat(Player* bot)
{
    static constexpr uint32 AncestralCombat = 801782;
    if (!sPlayerbotAIConfig.coaAncestralCombatFix || !bot || bot->getClass() != CLASS_BARBARIAN || !bot->IsAlive())
        return;
    if (bot->HasSpell(AncestralCombat) && !bot->HasAura(AncestralCombat))
        bot->AddAura(AncestralCombat, bot);
}

bool PrepareCoaRecruit(Player* master, Player* chosen, CoaRole role, bool chosenFits, uint32 levelTolerance,
                       std::string& message)
{
    // At the master's level, down as well as up. CoA scales every creature to the highest level
    // player in its sight, so a level 42 tank beside a level 22 player turns every pull into a
    // skull. The bot is rebuilt the way a random bot is - gear, talents and spells of that level -
    // rather than merely relabelled, which would leave it in gear it could no longer wear.
    // The dungeon finder takes no one under level 15: a level 14 bot recruited for a level 15 player kept the group
    // out of the queue (Server Manager, 11/10). Under that line the bot is rebuilt at the master's level too, and so
    // is one under a master at the realm's level cap: a level 58 or 59 bot kept the group out of the end-game
    // dungeons the finder only opens to everyone at the cap.
    bool const blocksFinder = (master->GetLevel() >= 15 && chosen->GetLevel() < 15) ||
                              (master->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL) &&
                               chosen->GetLevel() < master->GetLevel());
    if (blocksFinder || std::abs(int32(chosen->GetLevel()) - int32(master->GetLevel())) > int32(levelTolerance))
    {
        uint32 const level = master->GetLevel();
        sRandomPlayerbotMgr.SetValue(chosen, "level", level);
        PlayerbotFactory factory(chosen, level);
        factory.Randomize(false);
        // The rebuild picks a specialization of its own: whether it still plays the role is
        // decided below, on what it holds now.
        chosenFits = FitsRole(chosen, role);
    }
    // Rebuilt or not, no spell rank above the level it plays at.
    PlayerbotFactory::DropCoaRanksAbove(chosen, chosen->GetLevel());

    if (!chosenFits && IsStockClass(chosen->getClass()))
    {
        if (!SwitchStockSpec(chosen, role))
        {
            message = "Could not give " + chosen->GetName() + " talents to play " + RoleName(role) + ".";
            return false;
        }

        PlayerbotFactory factory(chosen, chosen->GetLevel());
        factory.InitGlyphs(false);
        factory.InitEquipment(false);
        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(chosen))
            botAI->ResetStrategies();
    }
    else if (!chosenFits)
    {
        // Keep the array alive: a reference into the temporary would dangle.
        std::array<std::vector<uint32>, 3> const byRole = SpecializationsByRole(chosen->getClass());
        std::vector<uint32> const& candidates = byRole[uint8(role)];
        uint32 const specialization = candidates.empty() ? 0 : candidates[urand(0, candidates.size() - 1)];
        if (!specialization || !SwitchAscensionSpecialization(chosen, specialization))
        {
            message = "Could not give " + chosen->GetName() + " a specialization.";
            return false;
        }

        // A recruit is chosen as explicitly as "talents spec": recorded the same way, or the
        // strategy reset below would hand the bot straight back its earlier specialization.
        if (sRandomPlayerbotMgr.IsRandomBot(chosen))
            sRandomPlayerbotMgr.SetValue(chosen, "coa_manual_spec", specialization);

        // Its gear was chosen for the specialization it had: a Venomancer made a healer kept the
        // strength and stamina of a tank, not a point of intellect (healer trial of 21/09).
        PlayerbotFactory(chosen, chosen->GetLevel()).InitEquipment(false);
    }
    else if (CoaSpecNeedsShootingWeapon(chosen))
    {
        // A bot geared before the rule still holds a wand or an idol and cannot fire its shots.
        Item const* ranged = chosen->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        // Only that slot is looked at again: with no bow to be had, the rest of its gear stays as it is.
        if (CoaRangedSlotCannotShoot(chosen, ranged ? ranged->GetTemplate() : nullptr))
            PlayerbotFactory(chosen, chosen->GetLevel()).InitEquipment(true, false, EQUIPMENT_SLOT_RANGED);
    }

    // Points for every level it just skipped.
    ApplyCoaTalents(chosen);

    if (role != CoaRole::Tank)
        DropThreatStances(chosen);

    return true;
}

void CoaLeaveBattlegroundQueues(Player* bot)
{
    if (!bot || bot->InBattleground())
        return;
    for (uint8 slot = 0; slot < PLAYER_MAX_BATTLEGROUND_QUEUES; ++slot)
    {
        BattlegroundQueueTypeId const queue = bot->GetBattlegroundQueueTypeId(slot);
        if (queue == BATTLEGROUND_QUEUE_NONE)
            continue;
        bot->RemoveBattlegroundQueueId(queue);
        sBattlegroundMgr->GetBattlegroundQueue(queue).RemovePlayer(bot->GetGUID(), true);
    }
    // And the Dungeon Finder queue it may have joined alone: the group tags as a whole.
    if (!bot->GetGroup() || !bot->GetGroup()->isLFGGroup())
        sLFGMgr->LeaveAllLfgQueues(bot->GetGUID(), false);
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
    {
        botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
        botAI->GetAiObjectContext()->GetValue<uint32>("bg role")->Set(0);
        botAI->GetAiObjectContext()->GetValue<uint32>("arena type")->Set(0);
    }
}

void CoaDropForeignInstanceBinds(Player* bot, Player* master)
{
    if (!bot || !master || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return;
    for (uint8 difficulty = 0; difficulty < MAX_DIFFICULTY; ++difficulty)
    {
        std::vector<InstanceSave*> foreign;
        for (auto const& [mapId, bind] : sInstanceSaveMgr->PlayerGetBoundInstances(bot->GetGUID(), Difficulty(difficulty)))
            if (bind.save && !(bind.save->GetMapId() == master->GetMapId() &&
                               bind.save->GetInstanceId() == master->GetInstanceId()))
                foreign.push_back(bind.save);
        for (InstanceSave* save : foreign)
            sInstanceSaveMgr->PlayerUnbindInstance(bot->GetGUID(), save->GetMapId(), save->GetDifficulty(), true, bot);
    }
}

bool CoaInOtherInstance(Player* bot, Player* master)
{
    return bot && master && bot->GetMapId() == master->GetMapId() && bot->GetInstanceId() != master->GetInstanceId();
}

bool RecruitCoaBot(Player* master, CoaRole role, std::string& message, uint8 classId)
{
    Group* group = master->GetGroup();
    if (group && group->IsFull())
    {
        message = "Your group is full.";
        return false;
    }

    if (classId && !ClassCanPlay(classId, role))
    {
        message = "A " + ClassName(classId) + " cannot play " + RoleName(role) + ".";
        return false;
    }

    bool chosenFits = false;
    Player* chosen = FindCoaRecruit(master, role, classId, {}, chosenFits);
    if (!chosen)
    {
        message = classId ? "No free " + ClassName(classId) + " bot of your faction to play " + RoleName(role) + "."
                          : std::string("No free bot able to play ") + RoleName(role) + ".";
        return false;
    }

    if (!PrepareCoaRecruit(master, chosen, role, chosenFits, 0, message))
        return false;

    if (!group)
    {
        group = new Group();
        if (!group->Create(master))
        {
            delete group;
            message = "Could not create a group.";
            return false;
        }
        sGroupMgr->AddGroup(group);
    }

    if (!group->AddMember(chosen))
    {
        message = "Could not add " + chosen->GetName() + " to the group.";
        return false;
    }
    CoaLeaveBattlegroundQueues(chosen);
    CoaDropForeignInstanceBinds(chosen, master);

    // After joining the group, so the bot may enter the master's dungeon instance.
    chosen->TeleportTo(master->GetMapId(), master->GetPositionX(), master->GetPositionY(), master->GetPositionZ(),
                       master->GetOrientation(), 0, nullptr, CoaInOtherInstance(chosen, master));

    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(chosen))
    {
        botAI->SetMaster(master);
        botAI->ResetStrategies();
    }

    LOG_INFO("playerbots", "coa: {} recruited {} (class {}, level {}, specialization {}) as {}",
             master->GetName(), chosen->GetName(), chosen->getClass(), chosen->GetLevel(),
             GetAscensionActiveSpecialization(chosen), RoleName(role));

    message = chosen->GetName() + " joins as " + RoleName(role) + " (specialization " + SpecName(chosen) +
              ", level " + std::to_string(chosen->GetLevel()) + ").";
    return true;
}

bool RecruitCoaRaid(Player* master, uint32 size, uint32 tanks, uint32 heals, std::string& message)
{
    size = std::clamp<uint32>(size ? size : 25, 6, MAXRAIDSIZE);
    if (!tanks)
        tanks = std::max<uint32>(2, size / 8);
    if (!heals)
        heals = (size + 2) / 4;
    tanks = std::min(tanks, size - 1);
    heals = std::min(heals, size - 1 - tanks);

    if (master->InBattleground() || master->InArena())
    {
        message = "Not in a battleground.";
        return false;
    }

    Group* group = master->GetGroup();
    if (group && (group->isLFGGroup() || group->isBGGroup() || group->isBFGroup()))
    {
        message = "Leave the Dungeon Finder or battleground group first.";
        return false;
    }
    if (group && !group->IsLeader(master->GetGUID()))
    {
        message = "Only the group leader can build a raid.";
        return false;
    }
    if (!group)
    {
        group = new Group();
        if (!group->Create(master))
        {
            delete group;
            message = "Could not create a group.";
            return false;
        }
        sGroupMgr->AddGroup(group);
    }
    if (!group->isRaidGroup())
        group->ConvertToRaid();

    // The members already there count: a tank or a healer already in the group is one less to find.
    std::array<uint32, 3> wanted = { 0, tanks, heals };
    std::map<uint8, uint32> classCount;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
        {
            ++classCount[member->getClass()];
            uint8 const role = uint8(RoleOfMember(member));
            if (role != uint8(CoaRole::Dps) && wanted[role])
                --wanted[role];
        }
    uint32 const members = group->GetMembersCount();
    if (members >= size)
    {
        message = "Your raid already has " + std::to_string(members) + " members.";
        return false;
    }
    uint32 const free = size - members;
    wanted[uint8(CoaRole::Tank)] = std::min(wanted[uint8(CoaRole::Tank)], free);
    wanted[uint8(CoaRole::Heal)] = std::min(wanted[uint8(CoaRole::Heal)], free - wanted[uint8(CoaRole::Tank)]);
    wanted[uint8(CoaRole::Dps)] = free - wanted[uint8(CoaRole::Tank)] - wanted[uint8(CoaRole::Heal)];

    // Classes taken in turn, the least represented first, so that 15 damage dealers are not 15 of a kind.
    std::mt19937 shuffler(urand(0, 0x7FFFFFFF));
    std::array<uint32, 3> got = {};
    std::string stopped;
    for (CoaRole role : { CoaRole::Tank, CoaRole::Heal, CoaRole::Dps })
    {
        for (uint32 i = 0; i < wanted[uint8(role)]; ++i)
        {
            std::vector<uint8> classes;
            for (uint8 classId = CLASS_WARRIOR; classId < FirstCoaClass + std::size(CoaClassNames); ++classId)
                if (ClassCanPlay(classId, role))
                    classes.push_back(classId);
            std::shuffle(classes.begin(), classes.end(), shuffler);
            std::stable_sort(classes.begin(), classes.end(),
                             [&classCount](uint8 a, uint8 b) { return classCount[a] < classCount[b]; });

            bool recruited = false;
            std::string reason;
            for (uint8 classId : classes)
                if (RecruitCoaBot(master, role, reason, classId))
                {
                    ++classCount[classId];
                    recruited = true;
                    break;
                }
            if (!recruited)
            {
                stopped += " No free bot left to play " + std::string(RoleName(role)) + ".";
                break;
            }
            ++got[uint8(role)];
        }
    }

    // The raid panel counts tanks and healers from the group roles: each bot shows the one it plays.
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (GET_PLAYERBOT_AI(member))
            {
                CoaRole const role = RoleOfMember(member);
                group->SetLfgRoles(member->GetGUID(), role == CoaRole::Tank   ? lfg::PLAYER_ROLE_TANK
                                                      : role == CoaRole::Heal ? lfg::PLAYER_ROLE_HEALER
                                                                              : lfg::PLAYER_ROLE_DAMAGE);
            }

    LOG_INFO("playerbots", "coa: {} built a raid of {}: {} tanks, {} healers, {} damage dealers recruited",
             master->GetName(), size, got[uint8(CoaRole::Tank)], got[uint8(CoaRole::Heal)], got[uint8(CoaRole::Dps)]);

    message = "Raid: " + std::to_string(got[uint8(CoaRole::Tank)]) + " tanks, " +
              std::to_string(got[uint8(CoaRole::Heal)]) + " healers and " + std::to_string(got[uint8(CoaRole::Dps)]) +
              " damage dealers joined (" + std::to_string(group->GetMembersCount()) + "/" + std::to_string(size) + ").";
    message += stopped;
    return got[0] + got[1] + got[2] > 0;
}

/*
 * Bots that answer "lfg bot heal" in a chat channel.
 *
 * A player who says "lfg bot heal", "lfg bot tank dps", "lfg bot" (every role)... in one of the chat
 * channels the realm lists (AiPlayerbot.CoaLfgChannels) is whispered by free random bots able to play
 * those roles: two healers, two tanks, three damage dealers by default, each with its class,
 * specialization and level. The player invites the ones it wants; an invited bot joins at once,
 * teleports next to the player, and follows and fights as any bot of the group. Made for realms where
 * /who hides the bots, so that a player without GM commands can still find a healer or a tank.
 *
 * An offer holds for AiPlayerbot.CoaLfgOfferMinutes: meanwhile the bot is offered to nobody else and
 * accepts that player's invitation whatever its own invitation rules. Every
 * AiPlayerbot.CoaLfgAnnounceMinutes the realm is reminded how to ask.
 */
namespace
{
struct CoaLfgSettings
{
    bool enabled = true;
    std::vector<std::string> channels = { "zone", "newcomers", "world", "lookingforgroup", "general" };  // lower case
    bool requireBotWord = true;
    std::array<uint32, 3> offers = { 3, 2, 2 };  // by CoaRole: dps, tank, heal
    uint32 levelRange = 2;
    uint32 offerSeconds = 2 * MINUTE;
    uint32 cooldownSeconds = 30;
    uint32 announceSeconds = 10 * MINUTE;
    std::string announce;
};

struct CoaLfgOffer
{
    ObjectGuid player;
    time_t until = 0;
};

std::mutex LfgLock;
CoaLfgSettings LfgSettings;
std::unordered_map<ObjectGuid, CoaLfgOffer> LfgOffers;  // by bot
std::map<std::pair<ObjectGuid, uint8>, time_t> LfgAsked;  // by player and role: when it last asked

std::string LowerCase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

std::string Trimmed(std::string const& text)
{
    std::size_t const first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return "";
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

void LoadLfgSettings()
{
    CoaLfgSettings settings;
    settings.enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLfgBots", true);
    settings.channels.clear();
    std::istringstream names(sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaLfgChannels", "Zone,Newcomers,World,LookingForGroup,General"));
    for (std::string name; std::getline(names, name, ',');)
        if (!Trimmed(name).empty())
            settings.channels.push_back(LowerCase(Trimmed(name)));
    settings.requireBotWord = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLfgRequireBotWord", true);
    settings.offers[uint8(CoaRole::Dps)] = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLfgDpsOffers", 3);
    settings.offers[uint8(CoaRole::Tank)] = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLfgTankOffers", 2);
    settings.offers[uint8(CoaRole::Heal)] = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLfgHealOffers", 2);
    for (uint32& count : settings.offers)
        count = std::min<uint32>(count, 5);
    settings.levelRange = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLfgLevelRange", 2);
    settings.offerSeconds = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLfgOfferMinutes", 2) * MINUTE;
    settings.cooldownSeconds = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLfgCooldown", 15);
    settings.announceSeconds = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaLfgAnnounceMinutes", 10) * MINUTE;
    settings.announce = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaLfgAnnounceText", "");

    std::lock_guard<std::mutex> guard(LfgLock);
    LfgSettings = settings;
}

CoaLfgSettings Settings()
{
    std::lock_guard<std::mutex> guard(LfgLock);
    return LfgSettings;
}

// "General - Elwynn Forest" is heard when "General" is listed.
bool Listened(CoaLfgSettings const& settings, Channel* channel)
{
    std::string const name = LowerCase(channel->GetName());
    for (std::string const& listened : settings.channels)
        if (name.rfind(listened, 0) == 0)
            return true;
    return false;
}

// The roles asked for, in the order they were written; empty when the message is no request.
std::vector<CoaRole> RolesAsked(CoaLfgSettings const& settings, std::string const& message)
{
    std::vector<std::string> words;
    std::string word;
    for (char c : LowerCase(message) + " ")
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            word += c;
        else if (!word.empty())
        {
            words.push_back(word);
            word.clear();
        }
    }

    bool lfg = false, botWord = false;
    std::vector<CoaRole> roles;
    auto add = [&roles](CoaRole role)
    {
        if (std::find(roles.begin(), roles.end(), role) == roles.end())
            roles.push_back(role);
    };
    for (std::string const& w : words)
    {
        if (w == "lfg" || w == "lfm" || w == "lf")
            lfg = true;
        else if (w == "bot" || w == "bots")
            botWord = true;
        else if (w == "heal" || w == "heals" || w == "healer" || w == "healers" || w == "healing")
            add(CoaRole::Heal);
        else if (w == "tank" || w == "tanks")
            add(CoaRole::Tank);
        else if (w == "dps" || w == "damage" || w == "dd")
            add(CoaRole::Dps);
    }

    if (!lfg || (settings.requireBotWord && !botWord))
        return {};
    if (roles.empty() && botWord)
        roles = { CoaRole::Tank, CoaRole::Heal, CoaRole::Dps };
    return roles;
}

std::string OfferText(Player* bot, CoaRole role)
{
    std::string const level = std::to_string(bot->GetLevel());
    std::string const className = ClassName(bot->getClass());
    CoaSpecStrategy const* spec = GetCoaSpecStrategyFor(bot);
    std::string const specName = spec && spec->specName ? spec->specName
                                 : IsStockClass(bot->getClass()) ? SpecName(bot)
                                                                 : className;

    std::vector<std::string> lines;
    switch (role)
    {
        case CoaRole::Heal:
            lines = {
                "Healer here! Level " + level + " " + className + " (" + specName + "), ready to go. Invite me if you need heals!",
                className + " healer, level " + level + ". Mana full, bandages packed. Invite me!",
                "Level " + level + " " + specName + " " + className + " looking for a group. I heal, I'll keep your tank alive.",
                "Need heals? Level " + level + " " + className + " (" + specName + ") at your service. Just invite me.",
            };
            break;
        case CoaRole::Tank:
            lines = {
                "Tank " + className + " level " + level + " (" + specName + ") available if you need one!",
                "Level " + level + " " + className + " tank here, I'll hold the aggro. Invite me.",
                specName + " " + className + ", level " + level + ". Point me at the boss and invite me!",
                "Need a tank? Level " + level + " " + className + " ready to pull. Send me an invite.",
            };
            break;
        default:
            lines = {
                "DPS " + className + " level " + level + " (" + specName + ") ready to go!",
                "Level " + level + " " + className + " here, I hit things hard. Invite me.",
                specName + " " + className + ", level " + level + ", looking for a group. Invite me!",
                "Need damage? Level " + level + " " + className + " at your service.",
            };
            break;
    }
    return lines[urand(0, lines.size() - 1)];
}

std::string AnnounceText(CoaLfgSettings const& settings)
{
    if (!settings.announce.empty())
        return settings.announce;

    std::string where;
    for (std::size_t i = 0; i < settings.channels.size() && i < 2; ++i)
    {
        std::string name = settings.channels[i];
        if (name == "lookingforgroup")
            name = "LookingForGroup";
        else if (name == "zone")
            name = "Zone";
        else if (!name.empty())
            name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
        where += (where.empty() ? "" : " or ") + name;
    }
    return "|cff66ccff[SquidBots]|r Need a healer, a tank or damage? Say |cffffff00lfg bot heal|r, "
           "|cffffff00lfg bot tank|r, |cffffff00lfg bot dps|r or |cffffff00lfg bot heal tank dps|r in the " +
           where + " channel, then invite the bots that whisper you.";
}
}  // namespace

void CoaLfgHeard(Player* player, std::string const& message, Channel* channel)
{
    if (!player || !channel || GET_PLAYERBOT_AI(player))
        return;

    // With the module switched off (AiPlayerbot.Enabled = 0, another bot system in charge), no bot answers.
    CoaLfgSettings const settings = Settings();
    if (!sPlayerbotAIConfig.enabled || !settings.enabled || !Listened(settings, channel))
        return;

    std::vector<CoaRole> const roles = RolesAsked(settings, message);
    if (roles.empty())
        return;

    ChatHandler chat(player->GetSession());
    time_t const now = time(nullptr);
    std::set<ObjectGuid> skip;
    std::vector<CoaRole> wanted;
    {
        std::lock_guard<std::mutex> guard(LfgLock);
        // The wait is per role: asking for a tank and then for a healer works, asking twice for the
        // same role in a row does not.
        for (CoaRole const role : roles)
        {
            auto const asked = LfgAsked.find({ player->GetGUID(), uint8(role) });
            if (asked == LfgAsked.end() || now - asked->second >= time_t(settings.cooldownSeconds))
            {
                LfgAsked[{ player->GetGUID(), uint8(role) }] = now;
                wanted.push_back(role);
            }
        }
        if (wanted.empty())
        {
            chat.SendSysMessage("The bots already heard you: invite the ones that whispered you, or ask again in a moment.");
            return;
        }

        for (auto itr = LfgOffers.begin(); itr != LfgOffers.end();)
        {
            if (itr->second.until < now)
                itr = LfgOffers.erase(itr);
            else
            {
                // A bot offered to someone else stays theirs; one offered to this player may be offered again.
                if (itr->second.player != player->GetGUID())
                    skip.insert(itr->first);
                ++itr;
            }
        }
    }

    if (player->GetGroup() && player->GetGroup()->IsFull())
    {
        chat.SendSysMessage("Your group is full.");
        return;
    }

    for (CoaRole const role : wanted)
    {
        uint32 const count = settings.offers[uint8(role)];
        std::set<uint8> classes;
        uint32 offered = 0;
        for (uint32 attempt = 0; offered < count && attempt < count * 4; ++attempt)
        {
            bool fits = false;
            Player* bot = FindCoaRecruit(player, role, 0, skip, fits);
            if (!bot)
                break;
            skip.insert(bot->GetGUID());

            // Different classes to choose from, as long as there are some.
            if (classes.count(bot->getClass()) && attempt < count * 2)
                continue;

            std::string reason;
            if (!PrepareCoaRecruit(player, bot, role, fits, settings.levelRange, reason))
                continue;

            {
                std::lock_guard<std::mutex> guard(LfgLock);
                LfgOffers[bot->GetGUID()] = { player->GetGUID(), now + time_t(settings.offerSeconds) };
            }
            classes.insert(bot->getClass());
            ++offered;
            bot->Whisper(OfferText(bot, role), LANG_UNIVERSAL, player);
            LOG_INFO("playerbots", "coa lfg: {} offered to {} as {} (class {}, level {})", bot->GetName(),
                     player->GetName(), RoleName(role), bot->getClass(), bot->GetLevel());
        }

        if (!offered)
            chat.PSendSysMessage("No free bot can play {} right now.", RoleName(role));
    }
}

bool CoaLfgOfferedToOther(Player* bot, Player* player)
{
    if (!bot)
        return false;
    std::lock_guard<std::mutex> guard(LfgLock);
    auto const offer = LfgOffers.find(bot->GetGUID());
    return offer != LfgOffers.end() && offer->second.until >= time(nullptr) &&
           (!player || offer->second.player != player->GetGUID());
}

bool CoaLfgTakeOffer(Player* bot, Player* inviter)
{
    if (!bot || !inviter)
        return false;

    std::lock_guard<std::mutex> guard(LfgLock);
    auto const offer = LfgOffers.find(bot->GetGUID());
    if (offer == LfgOffers.end() || offer->second.player != inviter->GetGUID() || offer->second.until < time(nullptr))
        return false;
    LfgOffers.erase(offer);
    return true;
}

namespace
{
class CoaLfgWorldScript : public WorldScript
{
public:
    CoaLfgWorldScript() : WorldScript("CoaLfgWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) {}

    void OnAfterConfigLoad(bool /*reload*/) override { LoadLfgSettings(); }
    void OnStartup() override { LoadLfgSettings(); }

    void OnUpdate(uint32 diff) override
    {
        sinceAnnounce += diff;
        if (sinceAnnounce < 10 * IN_MILLISECONDS)
            return;

        CoaLfgSettings const settings = Settings();
        if (!sPlayerbotAIConfig.enabled || !settings.enabled || !settings.announceSeconds ||
            sinceAnnounce < settings.announceSeconds * IN_MILLISECONDS)
            return;

        sinceAnnounce = 0;
        // To each real player: ChatHandler(nullptr).SendWorldText sends to its own session only, and
        // with none the server crashed (22/09 09:57).
        std::string const text = AnnounceText(settings);
        for (auto const& [id, session] : sWorldSessionMgr->GetAllSessions())
        {
            Player* player = session ? session->GetPlayer() : nullptr;
            if (player && player->IsInWorld() && !GET_PLAYERBOT_AI(player))
                ChatHandler(session).SendSysMessage(text);
        }
    }

private:
    uint32 sinceAnnounce = 0;
};
}  // namespace

void AddSC_coa_lfg()
{
    new CoaLfgWorldScript();
}
