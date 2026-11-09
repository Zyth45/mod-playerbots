/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: simulated battlegrounds of random bots (AiPlayerbot.CoaWorld.BgSim.*), on the model of DungeonSim.
 *
 * While no real player is queued for a battleground, free bots of one level bracket, Alliance against Horde, say
 * they queue for Warsong Gulch or Arathi Basin and log out where they stand (unseen by real players). No instance
 * is loaded. After a believable time they log back in with a win or a loss and the honor the core's battleground
 * would have given (end of match, flag captures or resource ticks, a few shared honorable kills). Back in the world,
 * a bot with enough honor may buy one PvP piece: only what a real player can buy today, from a vendor that is
 * spawned in the world and sells it for honor (npc_vendor + ItemExtendedCost), with the item's reputation needs.
 * Nothing is hard-coded: when CoA opens such vendors, the bots use them.
 *
 * BgSim.ReplaceAutoJoin (default 1, user's choice of 09/11): the bots no longer start real battlegrounds and arenas
 * by themselves (RandomBotAutoJoinBG): real battles start for real players, whose queues the bots fill at once
 * (RequestBgCheck when a real player queues).
 * World thread only. Off by default (BgSim.Enable).
 */

#include "CoaWorldBgSim.h"

#include "CoaWorldBgSimRules.h"
#include "CoaWorldBots.h"
#include "CoaWorldGuildRules.h"

#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "StatsWeightCalculator.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <random>
#include <unordered_map>
#include <vector>

namespace
{
using namespace CoaWorld;
using namespace CoaWorldBgSimRules;

constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.BgSim.";
constexpr uint32 CHECK_MS = 30 * IN_MILLISECONDS;
constexpr uint32 RETURN_WAIT_SECONDS = 15 * MINUTE;

struct Settings
{
    bool enabled = false;
    bool replaceAutoJoin = true;
    uint32 matches = 2;               // at the same time
    uint32 intervalSeconds = 300;     // between two starts
    uint32 botCooldownMinutes = 60;
    bool shop = true;
    bool announce = true;
};
Settings settings;

struct BgDef
{
    char const* name;
    uint32 mapId;
    uint32 teamSize;
    uint32 minMinutes, maxMinutes;
    bool flags;  // Warsong: captures; else Arathi: resource ticks
};
std::vector<BgDef> const BGS = {
    { "Warsong Gulch", 489, 10, 12, 25, true },
    { "Arathi Basin", 529, 15, 10, 20, false },
};

struct Match
{
    BgDef const* bg = nullptr;
    uint32 minLevel = 0, maxLevel = 0;
    std::vector<ObjectGuid> members;
    std::unordered_map<ObjectGuid, uint32> honor;  // still to give
    TeamId winner = TEAM_ALLIANCE;
    time_t until = 0;
    bool returning = false;
};

// A piece a real player can buy today for honor: a spawned vendor's item with an honor-only extended cost.
struct Offer
{
    uint32 item;
    uint32 honor;
    uint32 vendorFaction;
};

bool IsHumanPlayer(Player* player)
{
    return player && player->IsInWorld() && !GET_PLAYERBOT_AI(player) && player->GetSession() &&
           !player->GetSession()->IsBot();
}

class BgSimManager
{
public:
    static BgSimManager& Instance()
    {
        static BgSimManager instance;
        return instance;
    }

    void Update(uint32 diff)
    {
        _timer += diff;
        if (_timer < CHECK_MS)
            return;
        _timer = 0;
        time_t const now = time(nullptr);

        for (auto itr = _matches.begin(); itr != _matches.end();)
            itr = UpdateMatch(*itr, now) ? std::next(itr) : _matches.erase(itr);

        if (!settings.enabled || _matches.size() >= settings.matches || now < _nextStart || HumansQueued())
            return;
        if (TryStart(now))
            _nextStart = now + settings.intervalSeconds;
    }

    void StopAll()
    {
        for (Match& match : _matches)
            for (ObjectGuid const& guid : match.members)
                ReleaseHold(guid.GetCounter());
        _matches.clear();
    }

    std::string Status() const
    {
        std::string out = Acore::StringFormat("{} simulated battleground(s){}.", _matches.size(),
                                              settings.enabled ? "" : " (switched off)");
        for (Match const& m : _matches)
            out += Acore::StringFormat(" [{} {}-{}, {} bots, {}]", m.bg->name, m.minLevel, m.maxLevel, m.members.size(),
                                       m.returning ? "back" : "playing");
        return out;
    }

private:
    // A real player in a battleground queue: real battles go on, the bots fill them.
    static bool HumansQueued()
    {
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            if (IsHumanPlayer(player) && player->InBattlegroundQueue())
                return true;
        return false;
    }

    bool Free(Player* bot, time_t now) const
    {
        if (!bot || !IsFreeBot(bot) || IsHeld(bot->GetGUID().GetCounter()) || bot->IsInCombat() || !bot->IsAlive() ||
            bot->InBattleground() || bot->InBattlegroundQueue() || bot->GetGroup())
            return false;
        auto itr = _cooldown.find(bot->GetGUID().GetCounter());
        if (itr != _cooldown.end() && itr->second > now)
            return false;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        return botAI && !botAI->HasPlayerNearby(150.0f);  // logs out unseen
    }

    bool TryStart(time_t now)
    {
        std::vector<Player*> free;
        for (auto const& [guid, bot] : RandomBots())
            if (Free(bot, now))
                free.push_back(bot);
        if (free.size() < 4)
            return false;
        std::shuffle(free.begin(), free.end(), std::mt19937(urand(0, 0x7FFFFFFF)));

        BgDef const& bg = BGS[urand(0, BGS.size() - 1)];
        // The bracket of a random free bot, as the battleground's own (PvPDifficulty.dbc).
        for (Player* seed : free)
        {
            PvPDifficultyEntry const* bracket = GetBattlegroundBracketByLevel(bg.mapId, seed->GetLevel());
            if (!bracket)
                continue;
            std::vector<Player*> sides[2];
            for (Player* bot : free)
            {
                uint32 const level = bot->GetLevel();
                if (level < bracket->minLevel || level > bracket->maxLevel)
                    continue;
                std::vector<Player*>& side = sides[bot->GetTeamId() == TEAM_ALLIANCE ? 0 : 1];
                if (side.size() < bg.teamSize)
                    side.push_back(bot);
            }
            if (!EnoughPerSide(sides[0].size(), bg.teamSize) || !EnoughPerSide(sides[1].size(), bg.teamSize))
                continue;
            Start(bg, *bracket, sides, now);
            return true;
        }
        return false;
    }

    void Start(BgDef const& bg, PvPDifficultyEntry const& bracket, std::vector<Player*> (&sides)[2], time_t now)
    {
        Match match;
        match.bg = &bg;
        match.minLevel = bracket.minLevel;
        match.maxLevel = bracket.maxLevel;
        uint32 const minutes = MatchMinutes(bg.minMinutes, bg.maxMinutes, urand(0, 100));
        match.until = now + minutes * MINUTE;

        float levels[2] = { 0.0f, 0.0f };
        for (int s = 0; s < 2; ++s)
        {
            for (Player* bot : sides[s])
                levels[s] += bot->GetLevel();
            levels[s] /= float(sides[s].size());
        }
        match.winner = urand(1, 100) <= AllianceWinPct(levels[0], levels[1]) ? TEAM_ALLIANCE : TEAM_HORDE;
        Roll(match, sides);

        if (settings.announce)
            for (int s = 0; s < 2; ++s)
                Announce(sides[s].front(), bg);

        for (int s = 0; s < 2; ++s)
            for (Player* bot : sides[s])
            {
                match.members.push_back(bot->GetGUID());
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                    botAI->rpgInfo.ChangeToIdle();
                HoldBot(bot->GetGUID().GetCounter(), match.until);
                sRandomPlayerbotMgr.LogoutPlayerBot(bot->GetGUID());
            }
        LOG_INFO("module", "CoA World: simulated {} ({}-{}), {} v {} bots, {} minutes", bg.name, bracket.minLevel,
                 bracket.maxLevel, sides[0].size(), sides[1].size(), minutes);
        _matches.push_back(std::move(match));
    }

    // The honor of each, as the core's battleground gives it.
    void Roll(Match& match, std::vector<Player*> (&sides)[2])
    {
        uint32 const maxLevel = match.maxLevel;
        uint32 objectives[2];
        for (int s = 0; s < 2; ++s)
        {
            bool const won = (s == 0) == (match.winner == TEAM_ALLIANCE);
            if (match.bg->flags)
                objectives[s] = 2 * (won ? 3 : urand(0, 2));
            else
                objectives[s] = ArathiTicks(won ? 1600 : urand(400, 1500));
        }
        uint32 const today = uint32(time(nullptr) / DAY);
        for (int s = 0; s < 2; ++s)
        {
            bool const won = (s == 0) == (match.winner == TEAM_ALLIANCE);
            for (Player* bot : sides[s])
            {
                // The first win of the day gives the "first" reward, as the core's random winner flag.
                uint32& lastWin = _lastWinDay[bot->GetGUID().GetCounter()];
                bool const first = lastWin != today;
                uint32 endKills;
                if (won)
                {
                    endKills = sWorld->getIntConfig(first ? CONFIG_BG_REWARD_WINNER_HONOR_FIRST : CONFIG_BG_REWARD_WINNER_HONOR_LAST);
                    lastWin = today;
                }
                else
                    endKills = sWorld->getIntConfig(first ? CONFIG_BG_REWARD_LOSER_HONOR_FIRST : CONFIG_BG_REWARD_LOSER_HONOR_LAST);
                match.honor[bot->GetGUID()] = MatchHonor(maxLevel, endKills, objectives[s], urand(1, 10), urand(2, 5));
            }
        }
    }

    // (Lines to move to IA MIND's phrase bank.)
    void Announce(Player* bot, BgDef const& bg)
    {
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (!botAI || BotLine(bot, "bgsim_queue", { { "bg", bg.name } }))
            return;
        static char const* const lines[] = { "queueing {}, anyone?", "{} time, who's in", "off to {}, wish us luck",
                                             "{} queue popped, brb" };
        std::string const text = Acore::StringFormat(lines[urand(0, 3)], bg.name);
        if (bot->GetGuildId())
            botAI->SayToGuild(text);
        else
            botAI->Say(text);
    }

    // false: the match is over.
    bool UpdateMatch(Match& match, time_t now)
    {
        if (!match.returning)
        {
            if (now < match.until)
                return true;
            for (ObjectGuid const& guid : match.members)
                ReleaseHold(guid.GetCounter());  // RandomPlayerbotMgr logs them back in
            match.returning = true;
            match.until = now + RETURN_WAIT_SECONDS;
            return true;
        }

        for (ObjectGuid const& guid : match.members)
        {
            auto itr = match.honor.find(guid);
            if (itr == match.honor.end())
                continue;
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
                continue;
            bot->ModifyHonorPoints(int32(itr->second));
            match.honor.erase(itr);
            _cooldown[guid.GetCounter()] = now + settings.botCooldownMinutes * MINUTE;
            if (settings.shop)
                Shop(bot);
            Back(bot, match);
        }
        return !match.honor.empty() && now < match.until;
    }

    void Back(Player* bot, Match const& match)
    {
        if (urand(0, 3))
            return;  // a word from a few of them only
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        bool const won = bot->GetTeamId() == match.winner;
        if (!botAI || BotLine(bot, won ? "bgsim_won" : "bgsim_lost", { { "bg", match.bg->name } }))
            return;
        static char const* const wonLines[] = { "gg, {} won", "easy {} win", "{} done, we won" };
        static char const* const lostLines[] = { "lost {} again", "gg, rough {}", "{} lost, next one" };
        std::string const text = Acore::StringFormat(won ? wonLines[urand(0, 2)] : lostLines[urand(0, 2)], match.bg->name);
        if (bot->GetGuildId())
            botAI->SayToGuild(text);
    }

    // The offers of the vendors in the world, read once.
    std::vector<Offer> const& Offers()
    {
        if (_offersRead)
            return _offers;
        _offersRead = true;
        if (QueryResult result = WorldDatabase.Query(
                "SELECT DISTINCT v.`item`, v.`ExtendedCost`, t.`faction` FROM `npc_vendor` v "
                "JOIN `creature_template` t ON t.`entry` = v.`entry` "
                "WHERE v.`item` > 0 AND v.`ExtendedCost` > 0 AND EXISTS (SELECT 1 FROM `creature` c WHERE c.`id1` = v.`entry`)"))
        {
            do
            {
                Field* f = result->Fetch();
                ItemExtendedCostEntry const* cost = sItemExtendedCostStore.LookupEntry(f[1].Get<uint32>());
                if (!cost || !cost->reqhonorpoints || cost->reqarenapoints || cost->reqpersonalarenarating)
                    continue;
                bool items = false;
                for (uint32 i = 0; i < MAX_ITEM_EXTENDED_COST_REQUIREMENTS; ++i)
                    items |= cost->reqitem[i] != 0;
                if (items || !sObjectMgr->GetItemTemplate(f[0].Get<uint32>()))
                    continue;
                _offers.push_back({ f[0].Get<uint32>(), cost->reqhonorpoints, f[2].Get<uint32>() });
            } while (result->NextRow());
        }
        LOG_INFO("module", "CoA World: {} PvP pieces sold for honor by vendors in the world", _offers.size());
        return _offers;
    }

    // One piece, the best upgrade the bot can afford, use, and buy from a vendor of its side.
    void Shop(Player* bot)
    {
        std::vector<Offer> const& offers = Offers();
        if (offers.empty())
            return;  // nothing sold for honor today: the honor waits
        FactionTemplateEntry const* own = sFactionTemplateStore.LookupEntry(bot->GetFaction());
        StatsWeightCalculator calc(bot);
        Offer const* best = nullptr;
        float bestGain = 0.0f;
        for (Offer const& offer : offers)
        {
            if (offer.honor > bot->GetHonorPoints())
                continue;
            FactionTemplateEntry const* vendor = sFactionTemplateStore.LookupEntry(offer.vendorFaction);
            if (!own || !vendor || vendor->IsHostileTo(*own))
                continue;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(offer.item);
            if (!proto || proto->RequiredHonorRank || bot->CanUseItem(proto) != EQUIP_ERR_OK)
                continue;
            if (proto->RequiredReputationFaction &&
                uint32(bot->GetReputationRank(proto->RequiredReputationFaction)) < proto->RequiredReputationRank)
                continue;
            if (bot->HasItemCount(offer.item, 1, true))
                continue;
            uint8 const slot = bot->FindEquipSlot(proto, NULL_SLOT, true);
            if (slot == NULL_SLOT)
                continue;
            Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            float const wornScore = worn ? calc.CalculateItem(worn->GetEntry(), worn->GetItemRandomPropertyId()) : 0.0f;
            float const score = calc.CalculateItem(offer.item);
            if (CoaWorldGuildRules::WorthGiving(score, wornScore) && score - wornScore > bestGain)
            {
                bestGain = score - wornScore;
                best = &offer;
            }
        }
        if (!best)
            return;
        ItemPosCountVec dest;
        if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, best->item, 1) != EQUIP_ERR_OK)
            return;
        bot->ModifyHonorPoints(-int32(best->honor));
        bot->StoreNewItem(dest, best->item, true);
        LOG_INFO("module", "CoA World: {} bought item {} for {} honor", bot->GetName(), best->item, best->honor);
        LogEvent("pvp", bot->GetName(), Acore::StringFormat("bought {} for {} honor",
                                                             sObjectMgr->GetItemTemplate(best->item)->Name1, best->honor));
    }

    std::vector<Match> _matches;
    std::unordered_map<uint32, time_t> _cooldown;
    std::unordered_map<uint32, uint32> _lastWinDay;
    std::vector<Offer> _offers;
    bool _offersRead = false;
    time_t _nextStart = 0;
    uint32 _timer = 0;
};

void LoadSettings()
{
    auto key = [](char const* name) { return std::string(PREFIX) + name; };
    settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), false, false);
    settings.replaceAutoJoin = sConfigMgr->GetOption<bool>(key("ReplaceAutoJoin"), true, false);
    settings.matches = std::min<uint32>(10, sConfigMgr->GetOption<uint32>(key("Matches"), 2, false));
    settings.intervalSeconds = sConfigMgr->GetOption<uint32>(key("IntervalSeconds"), 300, false);
    settings.botCooldownMinutes = sConfigMgr->GetOption<uint32>(key("BotCooldownMinutes"), 60, false);
    settings.shop = sConfigMgr->GetOption<bool>(key("Shop"), true, false);
    settings.announce = sConfigMgr->GetOption<bool>(key("Announce"), true, false);
}

class CoaWorldBgSimWorldScript : public WorldScript
{
public:
    CoaWorldBgSimWorldScript()
        : WorldScript("CoaWorldBgSimWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE,
                                                    WORLDHOOK_ON_SHUTDOWN })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadSettings();
    }
    void OnStartup() override { LoadSettings(); }
    void OnShutdown() override { BgSimManager::Instance().StopAll(); }
    void OnUpdate(uint32 diff) override { BgSimManager::Instance().Update(diff); }
};

// A real player queues for a battleground: the bots look at the queues now, not up to 35 s later.
class CoaWorldBgSimPlayerScript : public PlayerScript
{
public:
    CoaWorldBgSimPlayerScript()
        : PlayerScript("CoaWorldBgSimPlayerScript", { PLAYERHOOK_CAN_JOIN_IN_BATTLEGROUND_QUEUE, PLAYERHOOK_CAN_JOIN_IN_ARENA_QUEUE })
    {
    }

    bool OnPlayerCanJoinInBattlegroundQueue(Player* player, ObjectGuid, BattlegroundTypeId, uint8, GroupJoinBattlegroundResult&) override
    {
        if (settings.replaceAutoJoin && IsHumanPlayer(player))
            sRandomPlayerbotMgr.RequestBgCheck();
        return true;
    }

    bool OnPlayerCanJoinInArenaQueue(Player* player, ObjectGuid, uint8, BattlegroundTypeId, uint8, uint8, GroupJoinBattlegroundResult&) override
    {
        if (settings.replaceAutoJoin && IsHumanPlayer(player))
            sRandomPlayerbotMgr.RequestBgCheck();
        return true;
    }
};
}  // namespace

bool CoaWorldBgSimReplacesAutoJoin() { return settings.replaceAutoJoin; }

void AddSC_coa_world_bg_sim()
{
    new CoaWorldBgSimWorldScript();
    new CoaWorldBgSimPlayerScript();
}
