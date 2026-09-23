/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

/*
 * How a group of a player and bots fares in a fight, written to CoaBots.log when the fight ends:
 * how long each member spent under half and under a quarter of its health, who died, who the
 * healing went to and how much of it was wasted, the healers' mana from the pull to the end, and
 * how long something other than the tank was being hit. It is what a dungeon run is measured by,
 * before and after a change to the way bots heal and tank.
 *
 * Only groups with a real player in them are followed, sampled from that player's own update so
 * that every member read is on the same map, and therefore on the same map thread.
 */

#include "CoaSpecLookup.h"
#include "CoaSpecialization.h"
#include "Config.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
constexpr uint32 SampleEvery = 500;      // ms between two looks at the group
constexpr uint32 LongestSample = 2000;   // a longer gap (a loading screen) counts as this much
constexpr uint32 EndAfter = 4000;        // ms with nobody in combat that close the fight
constexpr uint32 ShortestFight = 5000;   // shorter fights (a critter, a stray hit) are not written

struct MemberStats
{
    std::string name;
    std::string spec;   // the specialization played, not only the class
    CoaRole role = CoaRole::Dps;
    bool bot = false;
    uint32 below50 = 0;   // ms
    uint32 below25 = 0;   // ms
    uint32 deaths = 0;
    bool wasAlive = true;
    uint64 healReceived = 0;
    // As a healer.
    uint64 healDone = 0;
    uint64 damageDone = 0;  // to enemies, its pets and summons included, overkill left out
    uint64 overheal = 0;
    // Mana, for members that use it.
    bool mana = false;
    float manaStart = 0.0f;
    float manaLowest = 100.0f;
    float manaEnd = 0.0f;
    uint32 outOfMana = 0;  // ms under 10%
    // How long something was hitting this member.
    uint32 hit = 0;  // ms
    // Each time it fell under 25%: how long until the first heal reached it.
    bool critical = false;
    uint32 criticalSince = 0;
    uint32 criticalTimes = 0;
    uint32 answered = 0;
    uint32 reactionTotal = 0;  // ms
    uint32 reactionLongest = 0;  // ms
    uint32 diedUnhealed = 0;
    // As a tank: taunts that went off.
    uint32 taunts = 0;
    // Every heal this member tried, by spell and outcome (0 = cast).
    std::map<std::pair<uint32, uint16>, uint32> healTries;
    // Damage dealt by spell (0 = melee swing): the amount, and how many times it landed.
    std::map<uint32, std::pair<uint64, uint32>> damageBySpell;
    // Mana paid for each spell it cast, heals or not, and how many casts: where a healer's mana went.
    std::map<uint32, std::pair<uint32, uint32>> manaSpent;
    // The heals a healer bot considers, taken when it is first seen in the fight.
    std::string healKit;
};

struct Fight
{
    std::string mapName;
    uint32 start = 0;
    uint32 lastSample = 0;
    uint32 lastInCombat = 0;
    uint32 nonTankHit = 0;  // ms during which something was hitting a member that is not a tank
    std::vector<uint64> order;  // members in the order they were first seen
    std::unordered_map<uint64, MemberStats> members;
};

std::mutex Lock;
std::unordered_map<uint64, Fight> Fights;      // by group

// Groups of bots only, followed all the same at a test tool's request (CoaTelemetryFollowGroup).
std::mutex FollowedLock;
std::unordered_set<uint64> Followed;
std::atomic<uint32> FollowedCount{ 0 };

bool IsFollowed(Group* group)
{
    if (!FollowedCount.load(std::memory_order_relaxed))
        return false;
    std::lock_guard<std::mutex> guard(FollowedLock);
    return Followed.count(group->GetGUID().GetRawValue()) != 0;
}
std::atomic<uint32> ActiveFights{ 0 };

// The raw amount of the spell heal being applied on this thread, set just before the effective
// amount reaches OnHeal: the difference is the overheal.
thread_local Unit const* PendingHealTarget = nullptr;
thread_local uint32 PendingHealRaw = 0;

char const* RoleWord(CoaRole role)
{
    switch (role)
    {
        case CoaRole::Tank: return "tank";
        case CoaRole::Heal: return "heal";
        default: return "dps";
    }
}

// What happened to a heal a bot tried: cast, or why not.
std::string OutcomeWord(uint16 outcome)
{
    switch (outcome)
    {
        case 0: return "cast";
        case 12: return "bad target";
        case 22: return "caster aura state";
        case 29: return "needs a weapon";
        case 85: return "no power";
        case 97: return "out of range";
        case 105: return "busy casting";
        case 1000: return "refused by the cast";
        case 1001: return "nothing to cast";
        case 1002: return "moving";
        case 1003: return "sitting";
        case 1004: return "still casting";
        case 2000: return "on cooldown";
        case 2001: return "set aside";
        default: return "failed " + std::to_string(outcome);
    }
}

std::string Seconds(uint32 ms)
{
    std::ostringstream out;
    out.precision(1);
    out << std::fixed << ms / 1000.0f << " s";
    return out.str();
}

void Write(Fight const& fight, uint32 now)
{
    uint32 const length = getMSTimeDiff(fight.start, now);
    if (length < ShortestFight)
        return;

    uint64 healing = 0;
    for (auto const& [guid, member] : fight.members)
        healing += member.healReceived;

    LOG_INFO("playerbots.coa", "coa group fight: {} in {}, {} members, something other than a tank was being hit for {}",
             Seconds(length), fight.mapName, fight.members.size(), Seconds(fight.nonTankHit));

    for (uint64 guid : fight.order)
    {
        MemberStats const& m = fight.members.at(guid);
        std::ostringstream line;
        line << "  " << RoleWord(m.role) << " " << m.name << (m.bot ? "" : " (player)")
             << ": under 50% " << Seconds(m.below50) << ", under 25% " << Seconds(m.below25)
             << ", deaths " << m.deaths << ", healing received " << m.healReceived;
        if (healing)
            line << " (" << (m.healReceived * 100 / healing) << "%)";
        if (m.damageDone)
            line << ", damage done " << m.damageDone << " (" << (m.damageDone * 1000 / std::max<uint32>(length, 1)) << "/s)";
        if (m.healDone)
            line << ", healing done " << m.healDone << ", overheal "
                 << (m.overheal * 100 / (m.healDone + m.overheal)) << "%";
        line << ", hit " << Seconds(m.hit);
        if (m.role == CoaRole::Tank)
            line << ", taunts " << m.taunts;
        if (m.criticalTimes)
        {
            line << ", under 25% " << m.criticalTimes << " times";
            if (m.answered)
                line << ", first heal after " << Seconds(m.reactionTotal / m.answered) << " on average (longest "
                     << Seconds(m.reactionLongest) << ")";
            if (m.diedUnhealed)
                line << ", died " << m.diedUnhealed << " times before any heal";
        }
        if (m.mana)
            line << ", mana " << int32(m.manaStart) << "% -> " << int32(m.manaEnd) << "% (lowest "
                 << int32(m.manaLowest) << "%, under 10% for " << Seconds(m.outOfMana) << ")";
        LOG_INFO("playerbots.coa", "{}", line.str());

        if (!m.healTries.empty())
        {
            std::ostringstream tries;
            tries << "    heals of " << m.name << ":";
            for (auto const& [key, count] : m.healTries)
            {
                SpellInfo const* info = sSpellMgr->GetSpellInfo(key.first);
                tries << " " << (info ? info->SpellName[0] : "?") << " (" << key.first << ") "
                      << OutcomeWord(key.second) << " x" << count << ";";
            }
            LOG_INFO("playerbots.coa", "{}", tries.str());
        }

        if (!m.spec.empty())
            LOG_INFO("playerbots.coa", "    spec of {}: {}", m.name, m.spec);

        if (m.role == CoaRole::Heal && !m.healKit.empty())
            LOG_INFO("playerbots.coa", "    heal kit of {}: {}", m.name, m.healKit);

        if (!m.damageBySpell.empty())
        {
            std::vector<std::pair<uint32, std::pair<uint64, uint32>>> dealt(m.damageBySpell.begin(),
                                                                           m.damageBySpell.end());
            std::sort(dealt.begin(), dealt.end(), [](auto const& a, auto const& b) { return a.second.first > b.second.first; });
            uint64 total = 0;
            for (auto const& entry : dealt)
                total += entry.second.first;

            std::ostringstream what;
            what << "    damage of " << m.name << ": " << total << " dealt;";
            for (std::size_t i = 0; i < dealt.size() && i < 12; ++i)
            {
                SpellInfo const* info = dealt[i].first ? sSpellMgr->GetSpellInfo(dealt[i].first) : nullptr;
                what << " " << (dealt[i].first ? (info ? info->SpellName[0] : "?") : "Melee") << " ("
                     << dealt[i].first << ") " << dealt[i].second.first << " in " << dealt[i].second.second << ";";
            }
            LOG_INFO("playerbots.coa", "{}", what.str());
        }

        if (!m.manaSpent.empty())
        {
            std::vector<std::pair<uint32, std::pair<uint32, uint32>>> spent(m.manaSpent.begin(), m.manaSpent.end());
            std::sort(spent.begin(), spent.end(), [](auto const& a, auto const& b) { return a.second.first > b.second.first; });
            uint32 total = 0;
            for (auto const& entry : spent)
                total += entry.second.first;

            std::ostringstream where;
            where << "    mana of " << m.name << ": " << total << " spent;";
            for (std::size_t i = 0; i < spent.size() && i < 8; ++i)
            {
                SpellInfo const* info = sSpellMgr->GetSpellInfo(spent[i].first);
                where << " " << (info ? info->SpellName[0] : "?") << " (" << spent[i].first << ") "
                      << spent[i].second.first << " in " << spent[i].second.second << ";";
            }
            LOG_INFO("playerbots.coa", "{}", where.str());
        }
    }
}

// The member of the group that samples it: its first real player that is in the world.
bool IsSampler(Player* player, Group* group)
{
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (member->IsInWorld() && !GET_PLAYERBOT_AI(member))
                return member == player;
    return false;
}

// A group that broke up, or whose player logged out, mid fight is never sampled again: its fight
// is closed here after a minute of silence, so that nothing is kept and the heal hook goes idle.
void CloseAbandoned(uint32 now)
{
    for (auto itr = Fights.begin(); itr != Fights.end();)
    {
        if (getMSTimeDiff(itr->second.lastSample, now) < 60000)
        {
            ++itr;
            continue;
        }
        Write(itr->second, itr->second.lastSample);
        itr = Fights.erase(itr);
        --ActiveFights;
    }
}

void Sample(Player* sampler, Group* group, uint32 now)
{
    uint64 const key = group->GetGUID().GetRawValue();
    std::lock_guard<std::mutex> guard(Lock);
    CloseAbandoned(now);

    auto found = Fights.find(key);
    Fight* fight = found != Fights.end() ? &found->second : nullptr;
    if (fight && getMSTimeDiff(fight->lastSample, now) < SampleEvery)
        return;

    // Members read here are the ones on the sampler's map instance, updated by this very thread.
    std::vector<Player*> present;
    bool combat = false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld() || member->GetMapId() != sampler->GetMapId() ||
            member->GetInstanceId() != sampler->GetInstanceId())
            continue;
        present.push_back(member);
        combat = combat || member->IsInCombat();
    }

    if (!fight)
    {
        if (!combat || present.size() < 2)
            return;
        fight = &Fights[key];
        fight->mapName = sampler->GetMap()->GetMapName();
        fight->start = fight->lastSample = fight->lastInCombat = now;
        ++ActiveFights;
    }

    uint32 const elapsed = std::min(getMSTimeDiff(fight->lastSample, now), LongestSample);
    fight->lastSample = now;
    if (combat)
        fight->lastInCombat = now;

    bool nonTankHit = false;
    for (Player* member : present)
    {
        uint64 const guid = member->GetGUID().GetRawValue();
        auto [entry, added] = fight->members.try_emplace(guid);
        MemberStats& m = entry->second;
        if (added)
        {
            fight->order.push_back(guid);
            m.name = member->GetName();
            m.role = GetCoaRole(member);
            if (CoaSpecStrategy const* played = GetCoaSpecStrategyFor(member))
                if (played->specName)
                    m.spec = played->specName;
            m.bot = GET_PLAYERBOT_AI(member) != nullptr;
            m.mana = member->getPowerType() == POWER_MANA;
            if (m.mana)
                m.manaStart = m.manaLowest = m.manaEnd = member->GetPowerPct(POWER_MANA);
            // Dead since an earlier fight: not a death of this one.
            m.wasAlive = member->IsAlive();
            if (m.bot && m.role == CoaRole::Heal)
                m.healKit = CoaHealKit(member);
        }

        if (!member->IsAlive())
        {
            if (m.wasAlive)
                ++m.deaths;
            if (m.critical)
                ++m.diedUnhealed;
            m.critical = false;
            m.wasAlive = false;
            continue;
        }
        m.wasAlive = true;

        float const health = member->GetHealthPct();
        if (health < 50.0f)
            m.below50 += elapsed;
        if (health < 25.0f)
        {
            m.below25 += elapsed;
            if (!m.critical)
            {
                m.critical = true;
                m.criticalSince = now;
                ++m.criticalTimes;
            }
        }
        else
            m.critical = false;  // back up without a heal reaching it (a leech, a potion)

        if (m.mana)
        {
            float const mana = member->GetPowerPct(POWER_MANA);
            m.manaEnd = mana;
            m.manaLowest = std::min(m.manaLowest, mana);
            if (mana < 10.0f)
                m.outOfMana += elapsed;
        }

        bool beingHit = false;
        for (Unit* attacker : member->getAttackers())
            if (attacker->GetVictim() == member)
            {
                beingHit = true;
                break;
            }
        if (beingHit)
        {
            m.hit += elapsed;
            if (m.role != CoaRole::Tank)
                nonTankHit = true;
        }
    }
    if (nonTankHit)
        fight->nonTankHit += elapsed;

    if (!combat && getMSTimeDiff(fight->lastInCombat, now) >= EndAfter)
    {
        Write(*fight, now);
        Fights.erase(key);
        --ActiveFights;
    }
}

class CoaGroupTelemetryPlayerScript : public PlayerScript
{
public:
    CoaGroupTelemetryPlayerScript() : PlayerScript("CoaGroupTelemetryPlayerScript", { PLAYERHOOK_ON_UPDATE, PLAYERHOOK_ON_SPELL_CAST }) {}

    // What each member of a followed fight pays in mana, spell by spell.
    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!sPlayerbotAIConfig.coaGroupTelemetry || !spell || spell->GetPowerCost() <= 0 ||
            spell->GetSpellInfo()->PowerType != POWER_MANA || !ActiveFights.load(std::memory_order_relaxed))
            return;

        Group* group = player->GetGroup();
        if (!group)
            return;

        std::lock_guard<std::mutex> guard(Lock);
        auto fight = Fights.find(group->GetGUID().GetRawValue());
        if (fight == Fights.end())
            return;
        auto member = fight->second.members.find(player->GetGUID().GetRawValue());
        if (member == fight->second.members.end())
            return;
        auto& entry = member->second.manaSpent[spell->GetSpellInfo()->Id];
        entry.first += uint32(spell->GetPowerCost());
        ++entry.second;
    }

    void OnPlayerUpdate(Player* player, uint32 /*diff*/) override
    {
        if (!sPlayerbotAIConfig.coaGroupTelemetry)
            return;

        Group* group = player->GetGroup();
        if (!group || group->isRaidGroup())
            return;

        // A group of bots only is sampled by its leader, when a test tool asked for it.
        if (GET_PLAYERBOT_AI(player))
        {
            if (group->GetLeaderGUID() != player->GetGUID() || !IsFollowed(group))
                return;
        }
        else if (!IsSampler(player, group))
            return;

        Sample(player, group, getMSTime());
    }
};

class CoaGroupTelemetryUnitScript : public UnitScript
{
public:
    CoaGroupTelemetryUnitScript()
        : UnitScript("CoaGroupTelemetryUnitScript", true,
                     { UNITHOOK_ON_HEAL, UNITHOOK_MODIFY_HEAL_RECEIVED, UNITHOOK_ON_DAMAGE,
                       UNITHOOK_MODIFY_MELEE_DAMAGE, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
                       UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK }) {}

    // Damage a group member deals to a creature, pets and summons counted for their owner. Called before
    // the blow is applied: what exceeds the creature's health is overkill and left out.
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
    {
        if (!ActiveFights.load(std::memory_order_relaxed) || !attacker || !victim || !damage ||
            victim->GetTypeId() != TYPEID_UNIT || victim->IsCharmedOwnedByPlayerOrPlayer())
            return;
        Player* source = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        Group* group = source ? source->GetGroup() : nullptr;
        if (!group)
            return;
        uint32 const dealt = std::min<uint32>(damage, victim->GetHealth());

        std::lock_guard<std::mutex> guard(Lock);
        auto fight = Fights.find(group->GetGUID().GetRawValue());
        if (fight == Fights.end())
            return;
        auto member = fight->second.members.find(source->GetGUID().GetRawValue());
        if (member != fight->second.members.end())
            member->second.damageDone += dealt;
    }

    // The spell behind a hit: OnDamage gives the total, these give what each spell did.
    void NoteDamage(Unit* attacker, Unit* victim, uint32 damage, uint32 spellId)
    {
        if (!ActiveFights.load(std::memory_order_relaxed) || !attacker || !victim || !damage ||
            victim->GetTypeId() != TYPEID_UNIT || victim->IsCharmedOwnedByPlayerOrPlayer())
            return;
        Player* source = attacker->GetCharmerOrOwnerPlayerOrPlayerItself();
        Group* group = source ? source->GetGroup() : nullptr;
        if (!group)
            return;

        std::lock_guard<std::mutex> guard(Lock);
        auto fight = Fights.find(group->GetGUID().GetRawValue());
        if (fight == Fights.end())
            return;
        auto member = fight->second.members.find(source->GetGUID().GetRawValue());
        if (member == fight->second.members.end())
            return;
        auto& entry = member->second.damageBySpell[spellId];
        entry.first += std::min<uint32>(damage, victim->GetHealth());
        ++entry.second;
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (damage > 0)
            NoteDamage(attacker, target, uint32(damage), spellInfo ? spellInfo->Id : 0);
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override
    {
        NoteDamage(attacker, target, damage, spellInfo ? spellInfo->Id : 0);
    }

    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        NoteDamage(attacker, target, damage, 0);
    }

    // Unit::HealBySpell hands the raw amount here just before it is applied.
    void ModifyHealReceived(Unit* healer, Unit* target, uint32& heal, SpellInfo const* /*spellInfo*/) override
    {
        if (!ActiveFights.load(std::memory_order_relaxed))
            return;
        PendingHealTarget = target;
        PendingHealRaw = heal;
        (void)healer;
    }

    void OnHeal(Unit* healer, Unit* receiver, uint32& gain) override
    {
        uint32 raw = gain;
        if (PendingHealTarget == receiver && PendingHealRaw >= gain)
            raw = PendingHealRaw;
        PendingHealTarget = nullptr;

        if (!ActiveFights.load(std::memory_order_relaxed) || !healer || !receiver)
            return;

        Player* target = receiver->ToPlayer();
        Player* source = healer->GetCharmerOrOwnerPlayerOrPlayerItself();
        if (!target || !source)
            return;
        Group* group = target->GetGroup();
        if (!group || source->GetGroup() != group)
            return;

        std::lock_guard<std::mutex> guard(Lock);
        auto fight = Fights.find(group->GetGUID().GetRawValue());
        if (fight == Fights.end())
            return;

        auto& members = fight->second.members;
        auto to = members.find(target->GetGUID().GetRawValue());
        auto from = members.find(source->GetGUID().GetRawValue());
        if (to != members.end())
        {
            MemberStats& m = to->second;
            m.healReceived += gain;
            if (m.critical && gain)
            {
                uint32 const reaction = getMSTimeDiff(m.criticalSince, getMSTime());
                ++m.answered;
                m.reactionTotal += reaction;
                m.reactionLongest = std::max(m.reactionLongest, reaction);
                m.critical = false;
            }
        }
        if (from != members.end())
        {
            from->second.healDone += gain;
            from->second.overheal += raw - gain;
        }
    }
};
// ".reload config" rereads the core's settings, not those of the bots: the switches a dungeon is
// measured with and without are read again here, so that both runs share one server start.
class CoaGroupSettingsWorldScript : public WorldScript
{
public:
    CoaGroupSettingsWorldScript() : WorldScript("CoaGroupSettingsWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) {}

    void OnAfterConfigLoad(bool reload) override
    {
        if (!reload)
            return;

        sPlayerbotAIConfig.coaGroupTelemetry = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaGroupTelemetry", true);
        sPlayerbotAIConfig.coaSmartHeal = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaSmartHeal", true);
        sPlayerbotAIConfig.coaSmartTank = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaSmartTank", true);
        sPlayerbotAIConfig.coaThreatHold = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaThreatHold", 0);
        sPlayerbotAIConfig.coaTankOpenerSeconds = sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaTankOpenerSeconds", 2);
        sPlayerbotAIConfig.coaExcludedSpecializations.clear();
        std::string const excluded = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaExcludedSpecializations", "51,101");
        std::istringstream ids(excluded);
        for (std::string id; std::getline(ids, id, ',');)
            if (!id.empty())
                sPlayerbotAIConfig.coaExcludedSpecializations.insert(uint32(std::stoul(id)));
        sPlayerbotAIConfig.coaOffensiveHealerSpecs.clear();
        std::istringstream offensive(sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaOffensiveHealerSpecs", "40"));
        for (std::string id; std::getline(offensive, id, ',');)
            if (!id.empty())
                sPlayerbotAIConfig.coaOffensiveHealerSpecs.insert(uint32(std::stoul(id)));
        LOG_INFO("playerbots.coa", "coa settings reloaded: smart heal {}, smart tank {}, group telemetry {}",
                 sPlayerbotAIConfig.coaSmartHeal, sPlayerbotAIConfig.coaSmartTank, sPlayerbotAIConfig.coaGroupTelemetry);
    }
};
}  // namespace

// A taunt of `bot` went off: counted in its group's fight, if one is being followed.
void CoaTelemetryNoteTaunt(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group || !ActiveFights.load(std::memory_order_relaxed))
        return;

    std::lock_guard<std::mutex> guard(Lock);
    auto fight = Fights.find(group->GetGUID().GetRawValue());
    if (fight == Fights.end())
        return;
    auto member = fight->second.members.find(bot->GetGUID().GetRawValue());
    if (member != fight->second.members.end())
        ++member->second.taunts;
}

// A heal `bot` tried, and what came of it: counted in its group's fight, if one is being followed.
void CoaTelemetryNoteHeal(Player* bot, uint32 spellId, uint16 outcome)
{
    Group* group = bot->GetGroup();
    if (!group || !ActiveFights.load(std::memory_order_relaxed))
        return;

    std::lock_guard<std::mutex> guard(Lock);
    auto fight = Fights.find(group->GetGUID().GetRawValue());
    if (fight == Fights.end())
        return;
    auto member = fight->second.members.find(bot->GetGUID().GetRawValue());
    if (member != fight->second.members.end())
        ++member->second.healTries[{ spellId, outcome }];
}

// Follows a group made of bots only, as if a real player were in it (test tools); false to stop.
void CoaTelemetryFollowGroup(Group* group, bool follow)
{
    if (!group)
        return;
    std::lock_guard<std::mutex> guard(FollowedLock);
    bool const changed = follow ? Followed.insert(group->GetGUID().GetRawValue()).second
                                : Followed.erase(group->GetGUID().GetRawValue()) != 0;
    if (changed)
        FollowedCount.store(uint32(Followed.size()), std::memory_order_relaxed);
}

void AddSC_coa_group_telemetry()
{
    new CoaGroupTelemetryPlayerScript();
    new CoaGroupTelemetryUnitScript();
    new CoaGroupSettingsWorldScript();
}
