/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaTrust.h"

#include "CoaAltAddon.h"
#include "CoaTrustRules.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "WorldSession.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
constexpr uint32 FLUSH_MS = MINUTE * IN_MILLISECONDS;

uint64 Key(uint32 player, uint32 bot) { return (uint64(player) << 32) | bot; }

std::mutex trustLock;
std::unordered_map<uint64, uint32> scores;  // (player, bot) -> score
std::unordered_set<uint64> changed;         // written at the next flush
uint32 sinceFlush = 0;

uint32 ScoreOf(uint32 player, uint32 bot)
{
    std::lock_guard<std::mutex> guard(trustLock);
    auto const it = scores.find(Key(player, bot));
    return it == scores.end() ? 0 : it->second;
}

// The gains and the rank starts of playerbots.conf (AiPlayerbot.CoaTrust.*); a wrong rank list keeps the defaults.
CoaTrustRules::Settings SettingsOf()
{
    CoaTrustRules::Settings s;
    s.kill = sPlayerbotAIConfig.coaTrustKill;
    s.boss = sPlayerbotAIConfig.coaTrustBoss;
    s.raidBoss = sPlayerbotAIConfig.coaTrustRaidBoss;
    CoaTrustRules::ParseRanks(sPlayerbotAIConfig.coaTrustRanks, s.from);
    return s;
}

bool IsHumanPlayer(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot();
}
}  // namespace

void CoaTrust::Load()
{
    std::lock_guard<std::mutex> guard(trustLock);
    scores.clear();
    changed.clear();
    if (QueryResult result = PlayerbotsDatabase.Query("SELECT `player`, `bot`, `score` FROM `playerbots_coa_trust`"))
    {
        do
        {
            Field* fields = result->Fetch();
            scores[Key(fields[0].Get<uint32>(), fields[1].Get<uint32>())] = fields[2].Get<uint32>();
        } while (result->NextRow());
    }
}

void CoaTrust::OnKill(Player* player, Unit* victim)
{
    if (!sPlayerbotAIConfig.coaTrust || !player || !victim)
        return;
    Creature* creature = victim->ToCreature();
    Group* group = player->GetGroup();
    if (!creature || !group || creature->GetCreatureType() == CREATURE_TYPE_CRITTER || creature->IsTotem())
        return;
    Map* map = creature->GetMap();
    bool const boss = creature->IsDungeonBoss() || creature->isWorldBoss();
    CoaTrustRules::Settings s;
    s.kill = sPlayerbotAIConfig.coaTrustKill;
    s.boss = sPlayerbotAIConfig.coaTrustBoss;
    s.raidBoss = sPlayerbotAIConfig.coaTrustRaidBoss;
    uint32 const gain = CoaTrustRules::Gain(s, boss, map && map->IsRaid());
    if (!gain)
        return;

    std::vector<uint64> keys;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* bot = ref->GetSource();
        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        Player* master = botAI ? botAI->GetMaster() : nullptr;
        // The bot and its master both there: the fight was shared.
        if (!IsHumanPlayer(master) || master->GetGroup() != group || bot->GetMap() != map || master->GetMap() != map)
            continue;
        keys.push_back(Key(master->GetGUID().GetCounter(), bot->GetGUID().GetCounter()));
    }
    if (keys.empty())
        return;
    std::lock_guard<std::mutex> guard(trustLock);
    for (uint64 key : keys)
    {
        uint32& score = scores[key];
        score = CoaTrustRules::Add(score, gain);
        changed.insert(key);
    }
}

void CoaTrust::Flush(uint32_t diff, bool now)
{
    std::vector<std::pair<uint64, uint32>> rows;
    {
        std::lock_guard<std::mutex> guard(trustLock);
        sinceFlush += diff;
        if ((!now && sinceFlush < FLUSH_MS) || changed.empty())
            return;
        sinceFlush = 0;
        for (uint64 key : changed)
            rows.emplace_back(key, scores[key]);
        changed.clear();
    }
    for (auto const& [key, score] : rows)
        PlayerbotsDatabase.Execute("REPLACE INTO `playerbots_coa_trust` (`player`, `bot`, `score`) VALUES ({}, {}, {})",
                                   uint32(key >> 32), uint32(key & 0xFFFFFFFF), score);
}

bool CoaTrust::Command(PlayerbotAI* botAI, std::string const& command, std::string const& /*arg*/)
{
    if (command != "trust")
        return false;
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    Player* master = botAI ? botAI->GetMaster() : nullptr;
    if (!bot || !master)
        return true;
    if (!sPlayerbotAIConfig.coaTrust)
    {
        CoaAltAddon::SendToMaster(botAI, "TRUST\terr\toff");
        return true;
    }
    CoaAltAddon::SendToMaster(botAI,
                              CoaTrustRules::Line(SettingsOf(), ScoreOf(master->GetGUID().GetCounter(), bot->GetGUID().GetCounter())));
    return true;
}
