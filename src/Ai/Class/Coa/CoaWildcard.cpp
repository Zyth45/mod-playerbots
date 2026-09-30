#include "CoaWildcard.h"

#include "AscensionCoATalentState.h"
#include "AscensionSpecialization.h"
#include "AscensionWildcard.h"
#include "Config.h"
#include "Player.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <unordered_map>

namespace
{
// The five stat paths: Character Advancement entries and the spells they teach.
constexpr uint32 PATH_STRENGTH = 1149;
constexpr uint32 PATH_AGILITY = 1150;
constexpr uint32 PATH_INTELLECT = 1151;
constexpr uint32 PATH_HEALING = 1152;
constexpr uint32 PATH_DUALITY = 18149;
constexpr uint32 SPELL_PATH_STRENGTH = 84864;
constexpr uint32 SPELL_PATH_AGILITY = 84865;
constexpr uint32 SPELL_PATH_INTELLECT = 84866;
constexpr uint32 SPELL_PATH_HEALING = 84867;

// Set when the bot's weapon attacks are mostly shots, so an Agility bot fights from range.
constexpr char const* RANGED_VALUE = "wildcard_ranged";

std::vector<uint32> const* RankSpellsOf(uint32 entryId)
{
    static std::unordered_map<uint32, std::vector<uint32>> const byEntry = []
    {
        std::unordered_map<uint32, std::vector<uint32>> ranks;
        for (AscensionWildcard::Entry const& entry : AscensionWildcard::LoadedTables().Entries)
            ranks.emplace(entry.EntryId, entry.RankSpells);
        return ranks;
    }();

    auto const found = byEntry.find(entryId);
    return found == byEntry.end() ? nullptr : &found->second;
}

bool Heals(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.Effect == SPELL_EFFECT_HEAL ||
            (effect.IsAura() && (effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL ||
                                 effect.ApplyAuraName == SPELL_AURA_SCHOOL_ABSORB)))
            return true;
    return false;
}

bool Taunts(SpellInfo const* info)
{
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.Effect == SPELL_EFFECT_ATTACK_ME ||
            (effect.IsAura() && effect.ApplyAuraName == SPELL_AURA_MOD_TAUNT))
            return true;
    return false;
}

// What the rolled abilities of a build do: heal, strike, shoot or cast, and whether one taunts.
struct BuildShape
{
    uint32 heals = 0;
    uint32 strikes = 0;
    uint32 shots = 0;
    uint32 spells = 0;
    uint32 taunts = 0;
};

BuildShape ShapeOf(Player const* bot)
{
    BuildShape shape;
    for (uint32 spellId : WildcardBuildSpells(bot))
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive())
            continue;

        shape.taunts += Taunts(info) ? 1 : 0;
        if (Heals(info))
            ++shape.heals;
        else if (info->DmgClass == SPELL_DAMAGE_CLASS_MELEE)
            ++shape.strikes;
        else if (info->DmgClass == SPELL_DAMAGE_CLASS_RANGED)
            ++shape.shots;
        else if (info->DmgClass == SPELL_DAMAGE_CLASS_MAGIC)
            ++shape.spells;
    }
    return shape;
}

// The path a player would take for this build: Healing for a healer, Strength for a weapon build
// that can taunt, Agility for any other weapon build, Intellect for a caster, Duality for a build
// that does both.
uint32 PathFor(BuildShape const& shape)
{
    if (shape.heals >= 2 && shape.heals >= shape.spells)
        return PATH_HEALING;

    uint32 const weapon = shape.strikes + shape.shots;
    if (weapon > shape.spells + 1)
        return shape.taunts && shape.strikes >= shape.shots ? PATH_STRENGTH : PATH_AGILITY;
    if (shape.spells > weapon + 1)
        return PATH_INTELLECT;
    return PATH_DUALITY;
}
}  // namespace

bool IsWildcardRealm()
{
    return AscensionWildcard::PlaysWildcard(
        sConfigMgr->GetOption<std::string>("CoAChallenges.GameModes.Realm", "", false));
}

bool IsWildcardHeroBot(Player const* player)
{
    return player && AscensionWildcard::IsWildcardHero(player);
}

bool IsCoaBot(Player const* player)
{
    return player && (IsAscensionCustomClassId(player->getClass()) || IsWildcardHeroBot(player));
}

CoaRole GetWildcardRole(Player const* player)
{
    if (player->HasSpell(SPELL_PATH_HEALING))
        return CoaRole::Heal;
    if (player->HasSpell(SPELL_PATH_STRENGTH))
        return CoaRole::Tank;
    return CoaRole::Dps;
}

CoaStyle GetWildcardStyle(Player const* player)
{
    if (player->HasSpell(SPELL_PATH_INTELLECT) || player->HasSpell(SPELL_PATH_HEALING))
        return CoaStyle::Caster;
    if (player->HasSpell(SPELL_PATH_AGILITY) &&
        sRandomPlayerbotMgr.GetValue(player->GetGUID().GetCounter(), RANGED_VALUE))
        return CoaStyle::Ranged;
    return CoaStyle::Melee;
}

uint8 GetWildcardPrimaryStats(Player const* player)
{
    if (player->HasSpell(SPELL_PATH_HEALING))
        return COA_STAT_INTELLECT | COA_STAT_SPIRIT;
    if (player->HasSpell(SPELL_PATH_INTELLECT))
        return COA_STAT_INTELLECT;
    if (player->HasSpell(SPELL_PATH_STRENGTH))
        return COA_STAT_STRENGTH | COA_STAT_STAMINA;
    if (player->HasSpell(SPELL_PATH_AGILITY))
        return COA_STAT_AGILITY;
    return COA_STAT_STRENGTH | COA_STAT_INTELLECT;
}

bool DraftWildcardBuild(Player* bot)
{
    if (!IsWildcardHeroBot(bot) || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;

    CoaRole const role = GetWildcardRole(bot);
    CoaStyle const style = GetWildcardStyle(bot);

    AscensionWildcard::DraftBuild(bot);

    BuildShape const shape = ShapeOf(bot);
    sRandomPlayerbotMgr.SetValue(bot, RANGED_VALUE, shape.shots > shape.strikes ? 1 : 0);
    // The server refuses a path change in combat; a bot levelling mid-fight picks it on its next draft.
    if (!bot->IsInCombat())
    {
        std::vector<AscensionCoATalentState::KnownEntry> upload = AscensionWildcard::KnownEntries(bot);
        upload.push_back({ PathFor(shape), 1 });
        AscensionWildcard::ApplyBuildUpload(bot, upload);
    }

    ForgetWildcardKit(bot);
    return role != GetWildcardRole(bot) || style != GetWildcardStyle(bot);
}

void ResetWildcardBuild(Player* bot)
{
    if (!IsWildcardHeroBot(bot))
        return;

    AscensionWildcard::PrestigeSpecialization(bot);
    ForgetWildcardKit(bot);
}

std::vector<uint32> WildcardBuildSpells(Player const* bot)
{
    std::vector<uint32> spells;
    for (AscensionWildcard::Slot const& slot : AscensionWildcard::Slots(bot))
    {
        std::vector<uint32> const* ranks = slot.EntryId ? RankSpellsOf(slot.EntryId) : nullptr;
        if (ranks && slot.Rank >= 1 && slot.Rank <= ranks->size())
            spells.push_back((*ranks)[slot.Rank - 1]);
    }
    return spells;
}
