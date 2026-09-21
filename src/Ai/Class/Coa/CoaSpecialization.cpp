/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaSpecialization.h"
#include "CoaLevelBuildData.h"

#include "Group.h"
#include "GroupMgr.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "PlayerbotFactory.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "mod-ascension-compat/src/AscensionSpecialization.h"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <set>
#include <vector>

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
// heals: in dungeons such a healer did nothing at all.
bool IsExcludedSpecialization(uint32 specializationId)
{
    return specializationId == 101;
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
    { 58, 31, CASTER, STR | INT }, { 59, 31, MELEE, STR },      { 60, 31, MELEE, STR | STA },  // Primalist
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

uint32 ApplyCoaTalents(Player* bot)
{
    if (!bot || !IsAscensionCustomClassId(bot->getClass()) || bot->GetLevel() < SpecializationLevel ||
        !sRandomPlayerbotMgr.IsRandomBot(bot))
        return 0;

    uint32 const specializationId = GetAscensionActiveSpecialization(bot);
    if (!specializationId)
        return 0;

    // Rank each entry should hold at the bot's level: the build's picks up to that level.
    std::vector<std::pair<uint32, uint8>> wanted;
    for (CoaLevelBuildData::Pick const& pick : CoaLevelBuildData::Picks)
    {
        if (pick.ClassId != bot->getClass() || pick.SpecId != specializationId || pick.Level > bot->GetLevel())
            continue;

        auto itr = std::find_if(wanted.begin(), wanted.end(),
            [&pick](std::pair<uint32, uint8> const& w) { return w.first == pick.EntryId; });
        if (itr == wanted.end())
            wanted.emplace_back(pick.EntryId, pick.Rank);
        else if (itr->second < pick.Rank)
            itr->second = pick.Rank;
    }

    uint32 raised = 0;
    for (auto const& [entryId, rank] : wanted)
        if (GetAscensionTalentRank(bot, entryId) < rank && SetAscensionTalentRank(bot, entryId, rank))
            ++raised;

    if (raised)
        LOG_INFO("playerbots", "coa: {} (class {}, level {}, specialization {}) raised {} talent entries",
                 bot->GetName(), bot->getClass(), bot->GetLevel(), specializationId, raised);
    return raised;
}

bool RecruitCoaBot(Player* master, CoaRole role, std::string& message)
{
    Group* group = master->GetGroup();
    if (group && group->IsFull())
    {
        message = "Your group is full.";
        return false;
    }

    // A free random bot whose class can fill the role, preferably on the master's map (a
    // dungeon instance has none, so any map will do), then one that already holds a
    // specialization of the role, then the nearest.
    Player* chosen = nullptr;
    bool chosenSameMap = false;
    bool chosenFits = false;
    uint32 chosenLevelGap = 0;
    float chosenDistance = 0.0f;
    for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
    {
        if (!bot || bot == master || !bot->IsInWorld() || bot->IsBeingTeleported() ||
            !IsAscensionCustomClassId(bot->getClass()) || !bot->IsAlive() || bot->IsInCombat() || bot->GetGroup() ||
            bot->InBattleground() || bot->IsInFlight())
            continue;

        // The bot is added to the group directly, past the invitation checks, so the realm's
        // cross-faction rule has to be applied here: an Alliance player was handed a Forsaken
        // healer, whom the first city guard outside the dungeon would have attacked.
        if (bot->GetTeamId() != master->GetTeamId() && !sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP))
            continue;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || botAI->GetMaster())
            continue;

        uint32 const specialization = GetAscensionActiveSpecialization(bot);
        bool const fits = specialization && !IsExcludedSpecialization(specialization) && GetCoaRole(bot) == role;
        if (!fits && SpecializationsByRole(bot->getClass())[uint8(role)].empty())
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

    if (!chosen)
    {
        message = std::string("No free bot able to play ") + RoleName(role) + ".";
        return false;
    }

    // At the master's level, down as well as up. CoA scales every creature to the highest level
    // player in its sight, so a level 42 tank beside a level 22 player turns every pull into a
    // skull. The bot is rebuilt the way a random bot is - gear, talents and spells of that level -
    // rather than merely relabelled, which would leave it in gear it could no longer wear.
    if (chosen->GetLevel() != master->GetLevel())
    {
        uint32 const level = master->GetLevel();
        sRandomPlayerbotMgr.SetValue(chosen, "level", level);
        PlayerbotFactory factory(chosen, level);
        factory.Randomize(false);
        // The rebuild picks a specialization of its own: whether it still plays the role is
        // decided below, on what it holds now.
        uint32 const rebuilt = GetAscensionActiveSpecialization(chosen);
        chosenFits = rebuilt && !IsExcludedSpecialization(rebuilt) && GetCoaRole(chosen) == role;
    }

    if (!chosenFits)
    {
        // Keep the array alive: a reference into the temporary would dangle.
        std::array<std::vector<uint32>, 3> const byRole = SpecializationsByRole(chosen->getClass());
        std::vector<uint32> const& candidates = byRole[uint8(role)];
        uint32 const specialization = candidates[urand(0, candidates.size() - 1)];
        if (!SwitchAscensionSpecialization(chosen, specialization))
        {
            message = "Could not give " + chosen->GetName() + " a specialization.";
            return false;
        }

        // A recruit is chosen as explicitly as "talents spec": recorded the same way, or the
        // strategy reset below would hand the bot straight back its earlier specialization.
        if (sRandomPlayerbotMgr.IsRandomBot(chosen))
            sRandomPlayerbotMgr.SetValue(chosen, "coa_manual_spec", specialization);
    }

    // Points for every level it just skipped.
    ApplyCoaTalents(chosen);

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

    // After joining the group, so the bot may enter the master's dungeon instance.
    chosen->TeleportTo(master->GetMapId(), master->GetPositionX(), master->GetPositionY(), master->GetPositionZ(),
                       master->GetOrientation());

    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(chosen))
    {
        botAI->SetMaster(master);
        botAI->ResetStrategies();
    }

    LOG_INFO("playerbots", "coa: {} recruited {} (class {}, level {}, specialization {}) as {}",
             master->GetName(), chosen->GetName(), chosen->getClass(), chosen->GetLevel(),
             GetAscensionActiveSpecialization(chosen), RoleName(role));

    message = chosen->GetName() + " joins as " + RoleName(role) + " (specialization " +
              std::to_string(GetAscensionActiveSpecialization(chosen)) + ", level " +
              std::to_string(chosen->GetLevel()) + ").";
    return true;
}
