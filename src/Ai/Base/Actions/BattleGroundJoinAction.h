/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_BATTLEGROUNDJOINACTION_H
#define PLAYERBOTS_BATTLEGROUNDJOINACTION_H

#include "Action.h"
#include "DBCEnums.h"

#include <unordered_map>

class PlayerbotAI;

struct CreatureData;

enum ArenaType : uint8;
enum BattlegroundQueueTypeId : uint8;
enum BattlegroundBracketId : uint8;

class BGJoinAction : public Action
{
public:
    BGJoinAction(PlayerbotAI* botAI, std::string const name = "bg join") : Action(botAI, name) {}

    bool isUseful() override;
    bool canJoinBg(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId);
    virtual bool shouldJoinBg(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId);
    bool Execute(Event event) override;
    virtual bool gatherArenaTeam(ArenaType type);
    // Where a real player waits for this queue at the bot's level: what the bot's faction still lacks
    // for the battleground to start, or nullptr when nothing.
    uint32* FactionMissing(BattlegroundQueueTypeId queueTypeId);

protected:
    bool JoinQueue(uint32 type);
    // A rated solo queue a real player waits in, short of a bot of this one's role and level (CoaRatedSoloQueue):
    // the counter to take down, or nullptr when the bot cannot fill it.
    uint32* RatedSoloNeed(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId);
    bool JoinRatedSolo(BattlegroundQueueTypeId queueTypeId);
    // A bot that would unbalance the average levels of the two sides skips most of its tries
    // (AiPlayerbot.CoaBgLevelBalanceChance), and stays out of that queue a minute after a refusal.
    bool LevelUnbalances(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId);
    std::unordered_map<uint32, uint32> levelRefusedAt;  // queue type -> getMSTime() of the last refusal
    std::vector<uint32> bgList;
    std::vector<uint32> ratedList;
    std::vector<uint32> ratedSoloList;
};

class FreeBGJoinAction : public BGJoinAction
{
public:
    FreeBGJoinAction(PlayerbotAI* botAI, std::string const name = "free bg join") : BGJoinAction(botAI, name) {}

    bool shouldJoinBg(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId) override;
};

class BGLeaveAction : public Action
{
public:
    BGLeaveAction(PlayerbotAI* botAI, std::string const name = "bg leave") : Action(botAI, name) {}

    bool Execute(Event event) override;
};

class BGStatusAction : public Action
{
public:
    BGStatusAction(PlayerbotAI* botAI) : Action(botAI, "bg status") {}

    bool Execute(Event event) override;
    bool isUseful() override;
    static bool LeaveBG(PlayerbotAI* botAI);
};

class BGStatusCheckAction : public Action
{
public:
    BGStatusCheckAction(PlayerbotAI* botAI, std::string const name = "bg status check") : Action(botAI, name) {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

class BGStrategyCheckAction : public Action
{
public:
    BGStrategyCheckAction(PlayerbotAI* botAI, std::string const name = "bg strategy check") : Action(botAI, name) {}

    bool Execute(Event event) override;
};
#endif
