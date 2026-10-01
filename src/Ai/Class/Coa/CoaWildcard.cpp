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

#include <algorithm>
#include <array>
#include <mutex>
#include <tuple>
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

// A bot tries this many sets of starting abilities and keeps the best, as a player rerolls until the four fit.
constexpr uint32 STARTER_DRAWS = 1024;
// Builds drafted under older rules are drafted again; raise it when the rules change.
constexpr uint32 DRAFT_RULES = 1;
char const* const DRAFT_RULES_SETTING = "playerbots.wildcard.rules";

// Spell tags of SpellTagTypes.dbc.
constexpr uint32 TAG_ROLE_TANK = 122;
constexpr uint32 FIRST_CLASS_TAG = 58;
constexpr uint32 LAST_CLASS_TAG = 67;
constexpr uint32 FIRST_SPEC_TAG = 78;
constexpr uint32 LAST_SPEC_TAG = 107;
constexpr uint32 FIRST_CROWD_CONTROL_TAG = 23;
constexpr uint32 LAST_CROWD_CONTROL_TAG = 48;
constexpr uint32 FIRST_DISPEL_TAG = 69;
constexpr uint32 LAST_DISPEL_TAG = 76;
// Mobility, raid, party and personal buffs, immunity, resurrection, defensives.
constexpr std::array<uint32, 8> SUPPORT_TAGS = { 2, 3, 55, 77, 120, 121, 159, 160 };

AscensionWildcard::Entry const* EntryOf(uint32 entryId)
{
    static std::unordered_map<uint32, AscensionWildcard::Entry> const byEntry = []
    {
        std::unordered_map<uint32, AscensionWildcard::Entry> entries;
        for (AscensionWildcard::Entry const& entry : AscensionWildcard::LoadedTables().Entries)
            entries.emplace(entry.EntryId, entry);
        return entries;
    }();

    auto const found = byEntry.find(entryId);
    return found == byEntry.end() ? nullptr : &found->second;
}

std::vector<uint32> const* RankSpellsOf(uint32 entryId)
{
    AscensionWildcard::Entry const* entry = EntryOf(entryId);
    return entry ? &entry->RankSpells : nullptr;
}

std::vector<uint32> const& TagsOf(uint32 spellId)
{
    static std::vector<uint32> const none;
    auto const& tags = AscensionWildcard::LoadedTables().SpellTags;
    auto const found = tags.find(spellId);
    return found == tags.end() ? none : found->second;
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

// Damage of its own or of the spells it triggers (Arcane Missiles).
bool Damages(SpellInfo const* info, uint8 depth = 0)
{
    for (SpellEffectInfo const& effect : info->Effects)
    {
        if (effect.Effect == SPELL_EFFECT_SCHOOL_DAMAGE || effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE ||
            effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL || effect.Effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG ||
            effect.Effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE ||
            (effect.IsAura() && (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE ||
                                 effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH)))
            return true;

        bool const triggers = effect.Effect == SPELL_EFFECT_TRIGGER_SPELL ||
            (effect.IsAura() && effect.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL);
        if (triggers && depth < 2)
            if (SpellInfo const* triggered = sSpellMgr->GetSpellInfo(effect.TriggerSpell))
                if (Damages(triggered, depth + 1))
                    return true;
    }
    return false;
}

std::vector<uint32> FormsGiven(SpellInfo const* info)
{
    std::vector<uint32> forms;
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.IsAura() && effect.ApplyAuraName == SPELL_AURA_MOD_SHAPESHIFT && effect.MiscValue > 0)
            forms.push_back(uint32(effect.MiscValue));
    return forms;
}

bool CastableIn(SpellInfo const* info, uint32 form)
{
    return info->CheckShapeshift(form) == SPELL_CAST_OK;
}

// Threat for a tank: a taunt, a threat aura, or a form the game marks as a tank's (Bear Form, Defensive Stance,
// Righteous Fury, Frost Presence).
bool Threatens(SpellInfo const* info)
{
    if (Taunts(info))
        return true;
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.IsAura() && effect.ApplyAuraName == SPELL_AURA_MOD_THREAT)
            return true;
    std::vector<uint32> const& tags = TagsOf(info->Id);
    return !FormsGiven(info).empty() && std::find(tags.begin(), tags.end(), TAG_ROLE_TANK) != tags.end();
}

bool Supports(SpellInfo const* info)
{
    for (uint32 tag : TagsOf(info->Id))
        if ((tag >= FIRST_CROWD_CONTROL_TAG && tag <= LAST_CROWD_CONTROL_TAG) ||
            (tag >= FIRST_DISPEL_TAG && tag <= LAST_DISPEL_TAG) ||
            std::find(SUPPORT_TAGS.begin(), SUPPORT_TAGS.end(), tag) != SUPPORT_TAGS.end())
            return true;
    return false;
}

// A spell that needs a form comes with it, and a form comes with something to cast in it: no cat attack
// without Cat Form, no Cat Form without a cat attack.
bool FormsFit(std::vector<SpellInfo const*> const& spells)
{
    std::vector<uint32> forms;
    for (SpellInfo const* info : spells)
        for (uint32 form : FormsGiven(info))
            forms.push_back(form);

    for (SpellInfo const* info : spells)
        if (!CastableIn(info, 0) &&
            std::none_of(forms.begin(), forms.end(), [info](uint32 form) { return CastableIn(info, form); }))
            return false;

    for (SpellInfo const* former : spells)
        for (uint32 form : FormsGiven(former))
            if (std::none_of(spells.begin(), spells.end(), [former, form](SpellInfo const* other)
                    { return other != former && FormsGiven(other).empty() && CastableIn(other, form); }))
                return false;
    return true;
}

// Two of one specialization, or at least of one class.
uint32 Grouping(std::vector<SpellInfo const*> const& spells)
{
    std::unordered_map<uint32, uint32> specs;
    std::unordered_map<uint32, uint32> classes;
    for (SpellInfo const* info : spells)
        for (uint32 tag : TagsOf(info->Id))
        {
            if (tag >= FIRST_SPEC_TAG && tag <= LAST_SPEC_TAG)
                ++specs[tag];
            else if (tag >= FIRST_CLASS_TAG && tag <= LAST_CLASS_TAG)
                ++classes[tag];
        }

    auto const paired = [](std::unordered_map<uint32, uint32> const& counts)
    { return std::any_of(counts.begin(), counts.end(), [](auto const& count) { return count.second >= 2; }); };
    return paired(specs) ? 2 : paired(classes) ? 1 : 0;
}

// How well four starting abilities suit a role, best first: forms that work, what the role needs (healer: a heal
// and a damage spell to quest with; tank: threat and a damage spell; damage: two of a group and a support
// ability), more of the role's own abilities, then their synergy with each other.
using StarterFit = std::tuple<bool, uint32, uint32, uint32>;

StarterFit FitOf(std::vector<AscensionWildcard::Slot> const& starters, CoaRole role)
{
    std::vector<SpellInfo const*> spells;
    std::vector<uint32> entries;
    for (AscensionWildcard::Slot const& slot : starters)
    {
        std::vector<uint32> const* ranks = slot.EntryId ? RankSpellsOf(slot.EntryId) : nullptr;
        if (SpellInfo const* info = ranks && !ranks->empty() ? sSpellMgr->GetSpellInfo(ranks->front()) : nullptr)
        {
            spells.push_back(info);
            entries.push_back(slot.EntryId);
        }
    }

    uint32 heals = 0;
    uint32 damage = 0;
    uint32 threat = 0;
    uint32 support = 0;
    for (SpellInfo const* info : spells)
    {
        heals += Heals(info) ? 1 : 0;
        damage += Damages(info) ? 1 : 0;
        threat += Threatens(info) ? 1 : 0;
        support += Supports(info) ? 1 : 0;
    }

    uint32 needs = 0;
    uint32 depth = 0;
    switch (role)
    {
        case CoaRole::Heal:
            needs = (heals ? 1 : 0) + (damage ? 1 : 0);
            depth = heals;
            break;
        case CoaRole::Tank:
            needs = (threat ? 1 : 0) + (damage ? 1 : 0);
            depth = threat;
            break;
        default:
            needs = Grouping(spells) + (support ? 1 : 0);
            depth = damage;
            break;
    }

    uint32 synergy = 0;
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        std::vector<uint32> others = entries;
        others.erase(others.begin() + index);
        synergy += AscensionWildcard::SynergyScore(AscensionWildcard::LoadedTables(), entries[index], others);
    }
    return { FormsFit(spells), needs, depth, synergy };
}

std::size_t PickStarters(std::vector<std::vector<AscensionWildcard::Slot>> const& candidates, CoaRole role)
{
    std::size_t best = 0;
    StarterFit bestFit{};
    for (std::size_t index = 0; index < candidates.size(); ++index)
        if (StarterFit const fit = FitOf(candidates[index], role); index == 0 || fit > bestFit)
        {
            best = index;
            bestFit = fit;
        }
    return best;
}

// One bot in five tanks and one in five heals, as a dungeon group wants; the GUID keeps the choice across drafts.
CoaRole TargetRoleOf(Player const* bot)
{
    switch (bot->GetGUID().GetCounter() % 5)
    {
        case 0:
            return CoaRole::Tank;
        case 1:
            return CoaRole::Heal;
        default:
            return CoaRole::Dps;
    }
}

bool DraftedUnderOldRules(Player const* bot)
{
    PlayerSettingVector const* rules = bot->FindPlayerSettings(DRAFT_RULES_SETTING);
    return !rules || rules->empty() || (*rules)[0].value < DRAFT_RULES;
}

// Rolls of a higher level than the bot has: it was rebuilt at a lower level.
bool BuildAboveLevel(Player const* bot)
{
    for (AscensionWildcard::Slot const& slot : AscensionWildcard::Slots(bot))
        if (AscensionWildcard::Entry const* entry = slot.EntryId ? EntryOf(slot.EntryId) : nullptr;
            entry && entry->MinLevel > bot->GetLevel())
            return true;
    return false;
}

// What the rolled abilities of a build do: strike, shoot or cast.
struct BuildShape
{
    uint32 strikes = 0;
    uint32 shots = 0;
    uint32 spells = 0;
};

BuildShape ShapeOf(Player const* bot)
{
    BuildShape shape;
    for (uint32 spellId : WildcardBuildSpells(bot))
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        if (!info || info->IsPassive() || Heals(info))
            continue;

        if (info->DmgClass == SPELL_DAMAGE_CLASS_MELEE)
            ++shape.strikes;
        else if (info->DmgClass == SPELL_DAMAGE_CLASS_RANGED)
            ++shape.shots;
        else if (info->DmgClass == SPELL_DAMAGE_CLASS_MAGIC)
            ++shape.spells;
    }
    return shape;
}

// The path of the bot's role: Healing for a healer, Strength for a tank; a damage dealer takes Agility for a
// weapon build, Intellect for a caster and Duality for a build that does both.
uint32 PathFor(CoaRole role, BuildShape const& shape)
{
    if (role == CoaRole::Heal)
        return PATH_HEALING;
    if (role == CoaRole::Tank)
        return PATH_STRENGTH;

    uint32 const weapon = shape.strikes + shape.shots;
    if (weapon > shape.spells + 1)
        return PATH_AGILITY;
    if (shape.spells > weapon + 1)
        return PATH_INTELLECT;
    return PATH_DUALITY;
}

// Whether a Hero's weapon attacks are mostly shots, so an Agility Hero fights from range. Bot AI runs
// in the map update threads, so this is kept here under a lock rather than in RandomPlayerbotMgr's
// value cache, which only the world thread may touch.
std::mutex ShooterLock;
std::unordered_map<ObjectGuid::LowType, bool> Shooters;

bool Shoots(Player const* bot)
{
    ObjectGuid::LowType const guid = bot->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> lock(ShooterLock);
        if (auto const found = Shooters.find(guid); found != Shooters.end())
            return found->second;
    }
    BuildShape const shape = ShapeOf(bot);
    std::lock_guard<std::mutex> lock(ShooterLock);
    return Shooters.insert_or_assign(guid, shape.shots > shape.strikes).first->second;
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
    if (player->HasSpell(SPELL_PATH_AGILITY) && Shoots(player))
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

    if (DraftedUnderOldRules(bot) || BuildAboveLevel(bot))
    {
        ResetWildcardBuild(bot);
        bot->UpdatePlayerSetting(DRAFT_RULES_SETTING, 0, DRAFT_RULES);
    }

    CoaRole const target = TargetRoleOf(bot);
    AscensionWildcard::DraftBuild(bot,
        [target](std::vector<std::vector<AscensionWildcard::Slot>> const& candidates)
        { return PickStarters(candidates, target); },
        STARTER_DRAWS);

    BuildShape const shape = ShapeOf(bot);
    {
        std::lock_guard<std::mutex> lock(ShooterLock);
        Shooters.insert_or_assign(bot->GetGUID().GetCounter(), shape.shots > shape.strikes);
    }
    // The server refuses a path change in combat; a bot levelling mid-fight picks it on its next draft.
    if (!bot->IsInCombat())
    {
        std::vector<AscensionCoATalentState::KnownEntry> upload = AscensionWildcard::KnownEntries(bot);
        upload.push_back({ PathFor(target, shape), 1 });
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
