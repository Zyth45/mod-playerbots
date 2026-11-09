#include "CoaReborn.h"

#include "AscensionCoATalentState.h"
#include "AscensionFreepick.h"
#include "AscensionMysticEnchant.h"
#include "AscensionWarcraftRebornRules.h"
#include "CoaDbCheck.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "World.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace
{
constexpr char SPEC_VALUE[] = "wcr_spec";
constexpr uint32 SPEC_COUNT = 3;

struct SpecBuild
{
    std::string Name;
    WcrRole Role = WcrRole::MeleeDps;
    uint32 PrimaryStat = 0;
    std::vector<AscensionFreepick::GuideSpell> Spells;
    std::vector<AscensionMysticEnchant::GuideEnchant> Enchants;
};

using ClassBuilds = std::array<SpecBuild, SPEC_COUNT>;
using Builds = std::unordered_map<uint8, ClassBuilds>;

struct DefaultSpec
{
    char const* Name;
    WcrRole Role;
};

// The talent trees of each class in the order of its talent tabs, with the role the Reborn bots give them.
DefaultSpec const* DefaultSpecs(uint8 classId)
{
    static DefaultSpec const warrior[] = { { "Arms", WcrRole::MeleeDps }, { "Fury", WcrRole::MeleeDps },
                                           { "Protection", WcrRole::Tank } };
    static DefaultSpec const paladin[] = { { "Holy", WcrRole::Heal }, { "Protection", WcrRole::Tank },
                                           { "Retribution", WcrRole::MeleeDps } };
    static DefaultSpec const hunter[] = { { "Beast Mastery", WcrRole::RangedDps }, { "Marksmanship", WcrRole::RangedDps },
                                          { "Survival", WcrRole::RangedDps } };
    static DefaultSpec const rogue[] = { { "Assassination", WcrRole::MeleeDps }, { "Combat", WcrRole::MeleeDps },
                                         { "Subtlety", WcrRole::MeleeDps } };
    static DefaultSpec const priest[] = { { "Discipline", WcrRole::Heal }, { "Holy", WcrRole::Heal },
                                          { "Shadow", WcrRole::RangedDps } };
    static DefaultSpec const deathKnight[] = { { "Blood", WcrRole::Tank }, { "Frost", WcrRole::MeleeDps },
                                               { "Unholy", WcrRole::MeleeDps } };
    static DefaultSpec const shaman[] = { { "Elemental", WcrRole::RangedDps }, { "Enhancement", WcrRole::MeleeDps },
                                          { "Restoration", WcrRole::Heal } };
    static DefaultSpec const mage[] = { { "Arcane", WcrRole::RangedDps }, { "Fire", WcrRole::RangedDps },
                                        { "Frost", WcrRole::RangedDps } };
    static DefaultSpec const warlock[] = { { "Affliction", WcrRole::RangedDps }, { "Demonology", WcrRole::RangedDps },
                                           { "Destruction", WcrRole::RangedDps } };
    static DefaultSpec const druid[] = { { "Balance", WcrRole::RangedDps }, { "Feral", WcrRole::Tank },
                                         { "Restoration", WcrRole::Heal } };
    switch (classId)
    {
        case CLASS_WARRIOR: return warrior;
        case CLASS_PALADIN: return paladin;
        case CLASS_HUNTER: return hunter;
        case CLASS_ROGUE: return rogue;
        case CLASS_PRIEST: return priest;
        case CLASS_DEATH_KNIGHT: return deathKnight;
        case CLASS_SHAMAN: return shaman;
        case CLASS_MAGE: return mage;
        case CLASS_WARLOCK: return warlock;
        case CLASS_DRUID: return druid;
        default: return nullptr;
    }
}

bool ParseRole(std::string const& text, WcrRole& role)
{
    if (text == "tank")
        role = WcrRole::Tank;
    else if (text == "heal")
        role = WcrRole::Heal;
    else if (text == "mdps")
        role = WcrRole::MeleeDps;
    else if (text == "rdps")
        role = WcrRole::RangedDps;
    else
        return false;
    return true;
}

std::mutex BuildsLock;
std::shared_ptr<Builds const> LoadedBuilds;

std::shared_ptr<Builds const> ReadBuilds()
{
    auto builds = std::make_shared<Builds>();
    for (uint8 classId = CLASS_WARRIOR; classId <= CLASS_DRUID; ++classId)
        if (DefaultSpec const* defaults = DefaultSpecs(classId))
        {
            ClassBuilds& specs = (*builds)[classId];
            for (uint32 spec = 0; spec < SPEC_COUNT; ++spec)
            {
                specs[spec].Name = defaults[spec].Name;
                specs[spec].Role = defaults[spec].Role;
            }
        }

    // A missing or old table would abort the server on the query: the guide rows and the enchants are left out
    // (the bots keep the default specs, as with an empty table), one error line says so, the server starts.
    bool const buildsTable = CoaDbCheck::Require(
        "Warcraft Reborn builds", "playerbots_coa_wcr_builds",
        {"class", "spec", "role", "spec_name", "primary_stat", "spell_id", "level", "ord"});
    bool const enchantsTable = CoaDbCheck::Require("Warcraft Reborn enchants", "playerbots_coa_wcr_enchants",
                                                          {"class", "spec", "spell", "stacks", "level", "ord"});
    if (!buildsTable)
        return builds;

    QueryResult result = PlayerbotsDatabase.Query(
        "SELECT class, spec, role, spec_name, primary_stat, spell_id, level FROM playerbots_coa_wcr_builds "
        "ORDER BY class, spec, ord");
    if (!result)
        return builds;

    std::set<std::pair<uint8, uint8>> named;
    do
    {
        Field* fields = result->Fetch();
        uint8 const classId = fields[0].Get<uint8>();
        uint8 const spec = fields[1].Get<uint8>();
        auto const found = builds->find(classId);
        if (found == builds->end() || spec >= SPEC_COUNT)
            continue;
        SpecBuild& build = found->second[spec];
        if (named.emplace(classId, spec).second)
        {
            WcrRole role;
            if (ParseRole(fields[2].Get<std::string>(), role))
                build.Role = role;
            std::string const name = fields[3].Get<std::string>();
            if (!name.empty())
                build.Name = name;
            build.PrimaryStat = fields[4].Get<uint8>();
        }
        if (uint32 const spellId = fields[5].Get<uint32>())
            build.Spells.push_back({ spellId, fields[6].Get<uint8>() });
    } while (result->NextRow());

    if (!enchantsTable)
        return builds;
    if (QueryResult enchants = PlayerbotsDatabase.Query(
            "SELECT class, spec, spell, stacks, level FROM playerbots_coa_wcr_enchants ORDER BY class, spec, ord"))
        do
        {
            Field* fields = enchants->Fetch();
            auto const found = builds->find(fields[0].Get<uint8>());
            uint8 const spec = fields[1].Get<uint8>();
            if (found != builds->end() && spec < SPEC_COUNT)
                found->second[spec].Enchants.push_back(
                    { fields[2].Get<uint32>(), std::max<uint32>(1, fields[3].Get<uint8>()), fields[4].Get<uint8>() });
        } while (enchants->NextRow());
    return builds;
}

std::shared_ptr<Builds const> CurrentBuilds()
{
    std::lock_guard<std::mutex> guard(BuildsLock);
    if (!LoadedBuilds)
        LoadedBuilds = ReadBuilds();
    return LoadedBuilds;
}

SpecBuild const* BuildOf(Builds const& builds, uint8 classId, uint8 spec)
{
    auto const found = builds.find(classId);
    return found == builds.end() || spec >= SPEC_COUNT ? nullptr : &found->second[spec];
}

bool Exists(uint32 spellId)
{
    return sSpellMgr->GetSpellInfo(spellId) != nullptr;
}

// The core's Warcraft Reborn tables, loaded the way it loads them (its own copy is out of reach).
AscensionWarcraftReborn::Data const* RebornData()
{
    static std::unique_ptr<AscensionWarcraftReborn::Data> const data = []
    {
        auto loaded = std::make_unique<AscensionWarcraftReborn::Data>();
        if (!IsRebornRealm() || !AscensionWarcraftReborn::LoadData(*loaded))
        {
            if (IsRebornRealm())
                LOG_ERROR("playerbots", "coa wcr: the Warcraft Reborn tables did not load, bots train stock spells");
            loaded.reset();
        }
        return loaded;
    }();
    return data.get();
}

AscensionFreepick::Realm const& CurrentRealm()
{
    static AscensionFreepick::Realm const realm = AscensionFreepick::ReadRealm();
    return realm;
}

std::mutex SpecCacheLock;
std::unordered_map<ObjectGuid::LowType, uint8> SpecCache;

void ForgetSpec(Player const* bot)
{
    std::lock_guard<std::mutex> guard(SpecCacheLock);
    SpecCache.erase(bot->GetGUID().GetCounter());
}

std::vector<uint8> SpecsOfRole(uint8 classId, CoaRole role)
{
    std::vector<uint8> specs;
    for (uint8 spec = 0; spec < SPEC_COUNT; ++spec)
        if (ToCoaRole(GetWcrSpecRole(classId, spec)) == role)
            specs.push_back(spec);
    return specs;
}

// One in five a tank, one in five a healer when the class can, as the Wildcard bots.
uint8 PickedSpec(Player const* bot)
{
    // An account's characters have consecutive guids, one per class: mixed, every class gets each role.
    uint32 const mixed = uint32((uint64(bot->GetGUID().GetCounter()) * 2654435761u) >> 8);
    CoaRole const wanted = mixed % 5 == 0 ? CoaRole::Tank : mixed % 5 == 1 ? CoaRole::Heal : CoaRole::Dps;
    std::vector<uint8> specs = SpecsOfRole(bot->getClass(), wanted);
    if (specs.empty())
        specs = SpecsOfRole(bot->getClass(), CoaRole::Dps);
    return specs.empty() ? 0 : specs[(mixed / 5) % specs.size()];
}

// The tree whose build shares the most spells with what the character holds (an alt built by its owner).
int32 MatchedSpec(Player const* bot)
{
    AscensionWarcraftReborn::Data const* data = RebornData();
    std::shared_ptr<Builds const> const builds = CurrentBuilds();
    if (!data)
        return -1;
    std::unordered_set<uint32> held;
    for (AscensionCoATalentState::KnownEntry const& entry : AscensionFreepick::KnownEntries(bot))
        if (AscensionFreepick::Row const* row = data->Catalog.Find(entry.EntryId))
            for (uint32 spellId : row->Spells)
                if (spellId)
                    held.insert(spellId);
    int32 best = -1;
    uint32 bestCount = 0;
    for (uint8 spec = 0; spec < SPEC_COUNT; ++spec)
        if (SpecBuild const* build = BuildOf(*builds, bot->getClass(), spec))
        {
            uint32 const count = uint32(std::count_if(build->Spells.begin(), build->Spells.end(),
                [&held](AscensionFreepick::GuideSpell const& spell) { return held.contains(spell.Spell); }));
            if (count > bestCount)
            {
                best = spec;
                bestCount = count;
            }
        }
    return best;
}

uint32 StoredSpec(Player const* bot)
{
    return sRandomPlayerbotMgr.GetValue(bot->GetGUID().GetCounter(), SPEC_VALUE);
}

bool KnowsAnyRank(AscensionWarcraftReborn::Data const& data, Player const* bot, uint32 spellId)
{
    if (std::vector<uint32> const* ladder = data.LadderOf(spellId))
        return std::any_of(ladder->begin(), ladder->end(), [bot](uint32 rank) { return bot->HasSpell(rank); });
    return bot->HasSpell(spellId);
}

uint32 RankAtLevel(uint32 firstRank, uint8 level)
{
    uint32 rank = 0;
    for (SpellInfo const* info = sSpellMgr->GetSpellInfo(firstRank); info && info->SpellLevel <= level;
         info = info->GetNextRankSpell())
        rank = info->Id;
    return rank;
}

// Whether ApplyGuide would change the build: the same computation as the core's, without its "refused" log line
// for a build already in place.
bool BuildChanges(Player* bot, SpecBuild const& build)
{
    AscensionWarcraftReborn::Data const* data = RebornData();
    if (!data)
        return true;
    using namespace AscensionFreepick;
    std::vector<Entry> stored;
    for (AscensionCoATalentState::KnownEntry const& entry : KnownEntries(bot))
        stored.push_back({ entry.EntryId, entry.Rank });
    Catalog const& catalog = data->Catalog;
    Build const base(catalog, CurrentRealm(), bot->GetLevel(), stored, bot->getClass());
    std::vector<Entry> kept;
    for (Entry const& entry : base.Entries())
        if (Row const* row = catalog.Find(entry.EntryId); row && (row->Group == 1 || row->Has(ROW_RETAINED) ||
            (row->Has(ROW_AUTOMATIC) && !row->AECost && !row->TECost)))
            kept.push_back(entry);
    std::vector<Entry> const wanted = FitGuide(catalog, CurrentRealm(), bot->GetLevel(), bot->getClass(),
        GuideEntries(catalog, CurrentRealm(), bot->GetLevel(), bot->getClass(), build.Spells, build.PrimaryStat), kept);

    std::map<uint32, uint32> before, after;
    for (Entry const& entry : base.Entries())
        if (entry.Rank)
            before[entry.EntryId] = entry.Rank;
    for (Entry const& entry : wanted)
        if (entry.Rank)
            after[entry.EntryId] = entry.Rank;
    return before != after;
}

void NoteOnce(Player const* bot, std::string const& what)
{
    static std::mutex lock;
    static std::set<std::pair<ObjectGuid::LowType, std::string>> noted;
    std::lock_guard<std::mutex> guard(lock);
    if (noted.emplace(bot->GetGUID().GetCounter(), what).second)
        LOG_INFO("playerbots", "coa wcr: {} (class {}, level {}): {}", bot->GetName(), bot->getClass(), bot->GetLevel(),
                 what);
}
}  // namespace

bool IsRebornRealm()
{
    static bool const reborn = sConfigMgr->GetOption<std::string>("CoA.ClassModel", "coa", false) == "wcr";
    return reborn;
}

bool IsRebornClass(uint8 classId)
{
    return classId >= CLASS_WARRIOR && classId <= CLASS_DRUID && classId != CLASS_HERO;
}

bool IsRebornBot(Player const* player)
{
    return player && IsRebornRealm() && IsRebornClass(player->getClass());
}

uint32 RebornSpellId(uint32 spellId)
{
    return IsRebornRealm() ? AscensionWarcraftReborn::RebornSpell(spellId, &Exists) : spellId;
}

bool KnowsSpellOrTwin(Player const* player, uint32 spellId)
{
    if (player->HasSpell(spellId))
        return true;
    uint32 const twin = RebornSpellId(spellId);
    return twin != spellId && player->HasSpell(twin);
}

bool HasAuraOrTwin(Unit const* unit, uint32 spellId, ObjectGuid caster)
{
    if (unit->HasAura(spellId, caster))
        return true;
    uint32 const twin = RebornSpellId(spellId);
    return twin != spellId && unit->HasAura(twin, caster);
}

bool PlaysCatDruid(Player const* bot, uint32 catForm, uint32 thickHide)
{
    if (IsRebornBot(bot))
        return GetWcrRole(bot) != CoaRole::Tank;
    return bot->HasSpell(catForm) && !bot->HasAura(thickHide);
}

bool PlaysBearDruid(Player const* bot, uint32 thickHide)
{
    if (IsRebornBot(bot))
        return GetWcrRole(bot) == CoaRole::Tank;
    return bot->HasAura(thickHide);
}

Trainer::Trainer const* RebornTrainerFor(Trainer::Trainer const* stock, Player const* player)
{
    if (!stock || !IsRebornBot(player) || stock->GetTrainerType() != Trainer::Type::Class ||
        !IsRebornClass(uint8(stock->GetTrainerRequirement())))
        return stock;
    AscensionWarcraftReborn::Data const* data = RebornData();
    if (!data)
        return stock;

    static std::mutex lock;
    static std::unordered_map<uint32, Trainer::Trainer> trainers;
    std::lock_guard<std::mutex> guard(lock);
    if (auto const found = trainers.find(stock->GetTrainerId()); found != trainers.end())
        return &found->second;

    // As AscensionWarcraftReborn BuildTrainers: the stock rows turned into their Reborn ranks, priced like the stock
    // spells of their level, and the Reborn abilities no stock row teaches.
    using namespace AscensionWarcraftReborn;
    std::vector<AscensionWarcraftReborn::TrainerSpell> rows;
    std::map<uint32, uint32> prices;
    for (Trainer::Spell const& spell : stock->GetSpells())
    {
        AscensionWarcraftReborn::TrainerSpell row;
        row.SpellId = spell.SpellId;
        row.MoneyCost = spell.MoneyCost;
        row.ReqSkillLine = spell.ReqSkillLine;
        row.ReqSkillRank = spell.ReqSkillRank;
        std::copy(spell.ReqAbility.begin(), spell.ReqAbility.end(), row.ReqAbility.begin());
        row.ReqLevel = spell.ReqLevel;
        rows.push_back(row);
        prices[spell.ReqLevel] = std::max(prices[spell.ReqLevel], spell.MoneyCost);
    }
    auto const reborn = [&prices](uint32 spellId)
    {
        AscensionWarcraftReborn::TrainerSpell row;
        row.SpellId = spellId;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
        row.ReqLevel = info ? std::min<uint32>(info->SpellLevel, 255) : 0;
        auto const price = prices.upper_bound(std::max<uint32>(row.ReqLevel, 1));
        row.MoneyCost = price == prices.begin() ? 0 : std::prev(price)->second;
        return row;
    };

    std::vector<Trainer::Spell> spells;
    for (AscensionWarcraftReborn::TrainerSpell const& row :
         TrainerSpells(*data, CurrentRealm(), stock->GetTrainerRequirement(), rows, &Exists,
                       reborn))
    {
        Trainer::Spell spell;
        spell.SpellId = row.SpellId;
        spell.MoneyCost = row.MoneyCost;
        spell.ReqSkillLine = row.ReqSkillLine;
        spell.ReqSkillRank = row.ReqSkillRank;
        std::copy(row.ReqAbility.begin(), row.ReqAbility.end(), spell.ReqAbility.begin());
        spell.ReqLevel = uint8(row.ReqLevel);
        spells.push_back(spell);
    }
    return &trainers.emplace(stock->GetTrainerId(), stock->WithSpells(std::move(spells))).first->second;
}

void LearnRebornBasics(Player* bot)
{
    AscensionWarcraftReborn::Data const* data = RebornData();
    if (!IsRebornBot(bot) || !data)
        return;
    using namespace AscensionWarcraftReborn;
    uint8 const classId = bot->getClass();

    for (uint32 skillId : ClassSkillLines(*data, CurrentRealm(), classId))
    {
        SkillLineEntry const* skill = sSkillLineStore.LookupEntry(skillId);
        if (skill && skill->categoryId == SKILL_CATEGORY_CLASS && !bot->HasSkill(skillId))
            bot->LearnDefaultSkill(skillId, 0);
    }

    uint32 const startLevel = classId == CLASS_DEATH_KNIGHT ? sWorld->getIntConfig(CONFIG_START_HEROIC_PLAYER_LEVEL)
                                                            : sWorld->getIntConfig(CONFIG_START_PLAYER_LEVEL);
    for (std::vector<uint32> const& spells : { StartingSpells(*data, CurrentRealm(), classId, startLevel),
                                               AutomaticSpells(*data, CurrentRealm(), classId, bot->GetLevel()) })
        for (uint32 spellId : spells)
            if (Exists(spellId) && !KnowsAnyRank(*data, bot, spellId))
                bot->learnSpell(spellId);

    for (Grant const& grant : Grants())
        if (bot->HasSpell(grant.Source))
            for (uint32 firstRank : grant.Spells)
                if (uint32 const rank = RankAtLevel(firstRank, bot->GetLevel()); rank && !bot->HasSpell(rank))
                    bot->learnSpell(rank);
}

uint32 ForgetStockTwins(Player* bot)
{
    if (!IsRebornBot(bot))
        return 0;
    std::vector<uint32> stock;
    for (auto const& [spellId, spell] : bot->GetSpellMap())
        if (spell->State != PLAYERSPELL_REMOVED && spellId < AscensionWarcraftReborn::REBORN_SPELL_OFFSET &&
            bot->HasSpell(spellId + AscensionWarcraftReborn::REBORN_SPELL_OFFSET))
            stock.push_back(spellId);
    for (uint32 spellId : stock)
        bot->removeSpell(spellId, SPEC_MASK_ALL, false);
    return uint32(stock.size());
}

uint8 GetWcrSpec(Player const* bot)
{
    ObjectGuid::LowType const guid = bot->GetGUID().GetCounter();
    {
        std::lock_guard<std::mutex> guard(SpecCacheLock);
        if (auto const found = SpecCache.find(guid); found != SpecCache.end())
            return found->second;
    }
    uint8 spec;
    if (uint32 const stored = StoredSpec(bot); stored >= 1 && stored <= SPEC_COUNT)
        spec = uint8(stored - 1);
    else if (sRandomPlayerbotMgr.IsRandomBot(bot->GetGUID().GetCounter()))
        spec = PickedSpec(bot);
    else if (int32 const matched = MatchedSpec(bot); matched >= 0)
        spec = uint8(matched);
    else
        spec = PickedSpec(bot);
    std::lock_guard<std::mutex> guard(SpecCacheLock);
    SpecCache[guid] = spec;
    return spec;
}

WcrRole GetWcrSpecRole(uint8 classId, uint8 spec)
{
    std::shared_ptr<Builds const> const builds = CurrentBuilds();
    SpecBuild const* build = BuildOf(*builds, classId, spec);
    return build ? build->Role : WcrRole::MeleeDps;
}

std::string GetWcrSpecName(uint8 classId, uint8 spec)
{
    std::shared_ptr<Builds const> const builds = CurrentBuilds();
    SpecBuild const* build = BuildOf(*builds, classId, spec);
    return build ? build->Name : "Spec " + std::to_string(spec);
}

char const* WcrRoleName(WcrRole role)
{
    switch (role)
    {
        case WcrRole::Tank: return "tank";
        case WcrRole::Heal: return "heal";
        case WcrRole::RangedDps: return "rdps";
        default: return "mdps";
    }
}

CoaRole ToCoaRole(WcrRole role)
{
    return role == WcrRole::Tank ? CoaRole::Tank : role == WcrRole::Heal ? CoaRole::Heal : CoaRole::Dps;
}

CoaRole GetWcrRole(Player const* bot)
{
    return ToCoaRole(GetWcrSpecRole(bot->getClass(), GetWcrSpec(bot)));
}

bool IsWcrRanged(Player const* bot)
{
    WcrRole const role = GetWcrSpecRole(bot->getClass(), GetWcrSpec(bot));
    return role == WcrRole::RangedDps || role == WcrRole::Heal;
}

bool WcrClassCanPlay(uint8 classId, CoaRole role)
{
    return IsRebornClass(classId) && DefaultSpecs(classId) && !SpecsOfRole(classId, role).empty();
}

bool ApplyWcrBuild(Player* bot)
{
    if (!IsRebornBot(bot))
        return false;
    uint8 const spec = GetWcrSpec(bot);
    if (!StoredSpec(bot) && sRandomPlayerbotMgr.IsRandomBot(bot))
        sRandomPlayerbotMgr.SetValue(bot, SPEC_VALUE, spec + 1);

    std::shared_ptr<Builds const> const builds = CurrentBuilds();
    SpecBuild const* build = BuildOf(*builds, bot->getClass(), spec);
    if (!build || build->Spells.empty())
    {
        NoteOnce(bot, "no build rows in playerbots_coa_wcr_builds for spec " + std::to_string(spec));
        return false;
    }

    bool applied = true;
    if (BuildChanges(bot, *build))
    {
        // Unlearning an entry costs money above level 10 (AscensionFreepickRules CostToUnlearn, at most 2221 copper a
        // level): the bot is lent enough for every entry it holds and keeps the money it had.
        uint32 const money = bot->GetMoney();
        uint64 const loan = uint64(2221) * std::max<uint32>(bot->GetLevel(), 1) *
                            (AscensionFreepick::KnownEntries(bot).size() + 1);
        bot->SetMoney(uint32(std::min<uint64>(uint64(money) + loan, MAX_MONEY_AMOUNT)));
        AscensionFreepick::UploadResult const result = AscensionFreepick::ApplyGuide(bot, build->Spells,
                                                                                      build->PrimaryStat);
        bot->SetMoney(money);
        applied = !std::strcmp(result.Result, "CA_UPDATE_ENTRIES_OK");
        if (!applied)
            NoteOnce(bot, std::string("build of spec ") + std::to_string(spec) + " refused: " + result.Result + " " +
                              result.Learn + " entry " + std::to_string(result.EntryId) + " rank " +
                              std::to_string(result.Rank));
    }
    AscensionFreepick::Synchronize(bot);
    // Mystic Enchants as players set them at an altar, from the tree's list (playerbots_coa_wcr_enchants): the bot is
    // taught the ones it lacks, then the core fills its slots in list order within its level and quality limits.
    if (!build->Enchants.empty() && sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWcrMysticEnchants", true))
        AscensionMysticEnchant::ApplyGuide(bot, build->Enchants, true);
    return applied;
}

void EnsureWcrBuild(Player* bot)
{
    if (!IsRebornBot(bot) || !sRandomPlayerbotMgr.IsRandomBot(bot) || bot->IsInCombat() || !bot->IsAlive())
        return;
    static std::mutex lock;
    static std::unordered_set<ObjectGuid::LowType> done;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!done.insert(bot->GetGUID().GetCounter()).second)
            return;
    }
    ApplyWcrBuild(bot);
}

bool SetWcrSpec(Player* bot, uint8 spec, std::string& error)
{
    if (!IsRebornRealm())
    {
        error = "not a Warcraft Reborn realm";
        return false;
    }
    if (!IsRebornBot(bot))
    {
        error = "not a Warcraft Reborn class";
        return false;
    }
    if (spec >= SPEC_COUNT)
    {
        error = "spec must be 0, 1 or 2";
        return false;
    }
    if (bot->IsInCombat() || !bot->IsAlive())
    {
        error = "not now: in combat or dead";
        return false;
    }
    sRandomPlayerbotMgr.SetValue(bot, SPEC_VALUE, spec + 1);
    ForgetSpec(bot);
    if (!ApplyWcrBuild(bot))
    {
        error = "spec stored, but its Character Advancement build was not applied (see the server log)";
        return false;
    }
    return true;
}

bool SwitchWcrRole(Player* bot, CoaRole role)
{
    if (!IsRebornBot(bot))
        return false;
    if (GetWcrRole(bot) == role)
        return true;
    std::vector<uint8> const specs = SpecsOfRole(bot->getClass(), role);
    if (specs.empty())
        return false;
    std::string error;
    SetWcrSpec(bot, specs[urand(0, uint32(specs.size() - 1))], error);
    return GetWcrRole(bot) == role;
}

std::vector<std::string> DescribeWcr(Player const* bot)
{
    uint8 const classId = bot->getClass();
    uint8 const active = GetWcrSpec(bot);
    std::vector<std::string> lines;
    lines.push_back("WCR spec " + std::to_string(active) + ": " + GetWcrSpecName(classId, active) + " (" +
                    WcrRoleName(GetWcrSpecRole(classId, active)) + ")");
    for (uint8 spec = 0; spec < SPEC_COUNT; ++spec)
        lines.push_back(std::string(spec == active ? ">" : "") + "spec " + std::to_string(spec) + ": " +
                        GetWcrSpecName(classId, spec) + " (" + WcrRoleName(GetWcrSpecRole(classId, spec)) + ")");
    return lines;
}

uint32 LoadWcrBuilds()
{
    std::shared_ptr<Builds const> const builds = ReadBuilds();
    uint32 rows = 0;
    for (auto const& [classId, specs] : *builds)
        for (SpecBuild const& build : specs)
            rows += uint32(build.Spells.size());
    {
        std::lock_guard<std::mutex> guard(BuildsLock);
        LoadedBuilds = builds;
    }
    std::lock_guard<std::mutex> guard(SpecCacheLock);
    SpecCache.clear();
    return rows;
}
