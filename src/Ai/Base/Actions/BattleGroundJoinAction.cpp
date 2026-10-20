/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BattleGroundJoinAction.h"
#include "ArenaTeam.h"
#include "CoaSpecialization.h"
#include "ArenaTeamMgr.h"
#include "ArenaSeasonMgr.h"
#include "BattlegroundMgr.h"
#include "Event.h"
#include "Group.h"
#include "GroupMgr.h"
#include "LFGMgr.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "RandomPlayerbotFactory.h"
#include "ScriptMgr.h"

namespace
{
// CoA: the battles random bots keep going on their own in a battleground, RandomBotAutoJoinBG<type>Count.
uint32 AutoJoinBgCount(BattlegroundQueueTypeId queueTypeId)
{
    switch (queueTypeId)
    {
        case BATTLEGROUND_QUEUE_AV: return sPlayerbotAIConfig.randomBotAutoJoinBGAVCount;
        case BATTLEGROUND_QUEUE_WS: return sPlayerbotAIConfig.randomBotAutoJoinBGWSCount;
        case BATTLEGROUND_QUEUE_AB: return sPlayerbotAIConfig.randomBotAutoJoinBGABCount;
        case BATTLEGROUND_QUEUE_EY: return sPlayerbotAIConfig.randomBotAutoJoinBGEYCount;
        case BATTLEGROUND_QUEUE_IC: return sPlayerbotAIConfig.randomBotAutoJoinBGICCount;
        default: return 0;
    }
}

// CoA: bots filled TeamSize places per open battle. Bots leaving a battle queued again to be replaced, the core
// opened a new battle with them, and that new battle raised the limit by a full battle: on a 500-bot test realm
// set to one Alterac Valley, four ran at once with 318 bots (12/10/2026). Where no real player is in that queue,
// the limit is the number of battles the bots keep going, not the number open.
uint32 BotBattles(BattlegroundQueueTypeId queueTypeId, uint32 activeBgQueue, uint32 bgInstanceCount, uint32 players)
{
    uint32 const battles = activeBgQueue + bgInstanceCount;
    if (players)
        return battles;
    return std::min(battles, std::max(AutoJoinBgCount(queueTypeId), activeBgQueue));
}
}

bool BGJoinAction::Execute(Event /*event*/)
{
    // A real player waits in a rated solo queue for this bot: before any other.
    if (!ratedSoloList.empty())
        return JoinRatedSolo(BattlegroundQueueTypeId(ratedSoloList[urand(0, ratedSoloList.size() - 1)]));

    uint32 queueType = AI_VALUE(uint32, "bg type");
    if (!queueType)  // force join to fill bg
    {
        if (bgList.empty())
            return false;

        // A queue a real player waits in, short of the bot's faction, comes before the rest: drawn at
        // random among all, bots filled battlegrounds of their own and the player's never started.
        std::vector<uint32> wanted;
        for (uint32 listed : bgList)
            if (FactionMissing(BattlegroundQueueTypeId(listed)))
                wanted.push_back(listed);
        std::vector<uint32> const& pool = wanted.empty() ? bgList : wanted;
        BattlegroundQueueTypeId queueTypeId = (BattlegroundQueueTypeId)pool[urand(0, pool.size() - 1)];
        BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(queueTypeId);
        bool isRated = false;

        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
        if (!bg)
            return false;

        uint32 mapId = bg->GetMapId();
        PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
        if (!pvpDiff)
            return false;

        if (ArenaType type = ArenaType(BattlegroundMgr::BGArenaType(queueTypeId)))
        {
            std::vector<uint32>::iterator i = find(ratedList.begin(), ratedList.end(), queueTypeId);
            if (i != ratedList.end())
                isRated = true;

            if (isRated && !gatherArenaTeam(type))
                return false;

            botAI->GetAiObjectContext()->GetValue<uint32>("arena type")->Set(isRated);
        }

        // set bg type and bm guid
        // botAI->GetAiObjectContext()->GetValue<ObjectGuid>("bg master")->Set(bmGUID);
        botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(queueTypeId);
        queueType = queueTypeId;
    }

    return JoinQueue(queueType);
}

uint32* BGJoinAction::FactionMissing(BattlegroundQueueTypeId queueTypeId)
{
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BattlegroundMgr::BGTemplateId(queueTypeId));
    PvPDifficultyEntry const* pvpDiff = bg ? GetBattlegroundBracketByLevel(bg->GetMapId(), bot->GetLevel()) : nullptr;
    if (!pvpDiff)
        return nullptr;
    BattlegroundInfo& info = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][pvpDiff->GetBracketId()];
    uint32* missing = bot->GetTeamId() == TEAM_ALLIANCE ? &info.bgAllianceMissing : &info.bgHordeMissing;
    return *missing ? missing : nullptr;
}

bool BGJoinAction::gatherArenaTeam(ArenaType type)
{
    ArenaTeam* arenateam = sArenaTeamMgr->GetArenaTeamByCaptain(bot->GetGUID(), type);

    if (!arenateam)
        return false;

    if (arenateam->GetMembersSize() < ((uint32)arenateam->GetType()))
        return false;

    GuidVector members;

    // search for arena team members and make them online
    for (ArenaTeam::MemberList::iterator itr = arenateam->GetMembers().begin(); itr != arenateam->GetMembers().end();
         ++itr)
    {
        bool offline = false;
        Player* member = ObjectAccessor::FindConnectedPlayer(itr->Guid);
        if (!member)
        {
            offline = true;
        }
        // if (!member && !sObjectMgr->GetPlayerAccountIdByGUID(itr->guid))
        //     continue;

        if (offline)
            sRandomPlayerbotMgr.AddPlayerBot(itr->Guid, 0);

        if (member)
        {
            PlayerbotAI* memberBotAI = GET_PLAYERBOT_AI(member);
            if (!memberBotAI)
                continue;

            if (member->GetGroup() && memberBotAI->HasGameClientMaster())
                continue;

            if (!sPlayerbotAIConfig.IsInRandomAccountList(member->GetSession()->GetAccountId()))
                continue;

            if (member->IsInCombat())
                continue;

            if (member->GetGUID() == bot->GetGUID())
                continue;

            if (member->InBattleground())
                continue;

            if (member->InBattlegroundQueue())
                continue;

            if (member->GetGroup())
                member->GetGroup()->RemoveMember(member->GetGUID());

            memberBotAI->Reset();
        }

        if (member)
            members.push_back(member->GetGUID());
    }

    if (!members.size() || (int)members.size() < (int)(arenateam->GetType() - 1))
    {
        LOG_INFO("playerbots", "Team #{} <{}> has not enough members for match", arenateam->GetId(),
                 arenateam->GetName().c_str());
        return false;
    }

    Group* group = new Group();

    // disband leaders group
    if (bot->GetGroup())
        bot->GetGroup()->Disband(true);

    if (!group->Create(bot))
    {
        LOG_INFO("playerbots", "Team #{} <{}>: Can't create group for arena queue", arenateam->GetId(),
                 arenateam->GetName());
        return false;
    }
    else
        sGroupMgr->AddGroup(group);

    LOG_INFO("playerbots", "Bot {} <{}>: Leader of <{}>", bot->GetGUID().ToString().c_str(), bot->GetName(),
             arenateam->GetName());

    for (auto i = begin(members); i != end(members); ++i)
    {
        if (*i == bot->GetGUID())
            continue;

        // if (count >= (int)arenateam->GetType())
        // break;

        if (group->GetMembersCount() >= (uint32)arenateam->GetType())
            break;

        Player* member = ObjectAccessor::FindConnectedPlayer(*i);
        if (!member)
            continue;

        if (member->GetLevel() < RandomPlayerbotFactory::ArenaTeamMinLevel())
            continue;

        if (!group->AddMember(member))
            continue;

        PlayerbotAI* memberBotAI = GET_PLAYERBOT_AI(member);
        if (!memberBotAI)
            continue;

        memberBotAI->Reset();
        member->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
        member->TeleportTo(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), 0);

        LOG_INFO("playerbots", "Bot {} <{}>: Member of <{}>", member->GetGUID().ToString().c_str(),
                 member->GetName().c_str(), arenateam->GetName().c_str());
    }

    if (group && group->GetMembersCount() >= (uint32)arenateam->GetType())
    {
        LOG_INFO("playerbots", "Team #{} <{}> Group is ready for match", arenateam->GetId(),
                 arenateam->GetName().c_str());
        return true;
    }
    else
    {
        LOG_INFO("playerbots", "Team #{} <{}> Group is not ready for match (not enough members)", arenateam->GetId(),
                 arenateam->GetName().c_str());
        group->Disband();
    }

    return false;
}

bool BGJoinAction::canJoinBg(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId)
{
    // check if bot can join this bracket for the specific Battleground/Arena type
    BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(queueTypeId);

    // check if already in queue
    if (bot->InBattlegroundQueueForBattlegroundQueueType(queueTypeId))
        return false;

    // check too low/high level
    if (!bot->GetBGAccessByLevel(bgTypeId))
        return false;

    // check if the bracket exists for the bot's level for the specific Battleground/Arena type
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    uint32 mapId = bg->GetMapId();
    PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
    if (!pvpDiff)
        return false;

    BattlegroundBracketId bracket_temp = pvpDiff->GetBracketId();

    if (bracket_temp != bracketId)
        return false;

    return true;
}

bool BGJoinAction::shouldJoinBg(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId)
{
    BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(queueTypeId);
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bg)
        return false;

    TeamId teamId = bot->GetTeamId();
    uint32 BracketSize = bg->GetMaxPlayersPerTeam() * 2;
    uint32 TeamSize = bg->GetMaxPlayersPerTeam();

    // If the bot is in a group, only the leader can queue
    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetGUID()))
        return false;

    // Check if bots should join Arena
    ArenaType type = ArenaType(BattlegroundMgr::BGArenaType(queueTypeId));
    if (type != ARENA_TYPE_NONE)
    {
        BracketSize = (uint32)(type * 2);
        TeamSize = (uint32)type;

        if (RatedSoloNeed(queueTypeId, bracketId))
        {
            ratedSoloList.push_back(queueTypeId);
            return true;
        }

        // Check if bots should join Rated Arena (Only captains can queue)
        uint32 ratedArenaBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].ratedArenaBotCount;
        uint32 ratedArenaPlayerCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].ratedArenaPlayerCount;
        uint32 ratedArenaInstanceCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].ratedArenaInstanceCount;
        uint32 activeRatedArenaQueue =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].activeRatedArenaQueue;

        bool isRated = (ratedArenaBotCount + ratedArenaPlayerCount) <
                       (BracketSize * (activeRatedArenaQueue + ratedArenaInstanceCount));

        if (isRated)
        {
            // The personal team of a rated solo queue has no one else to gather.
            ArenaTeam* captainTeam = sArenaTeamMgr->GetArenaTeamByCaptain(bot->GetGUID(), type);
            if (captainTeam && captainTeam->GetMembersSize() > 1)
            {
                sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].ratedArenaBotCount += TeamSize;
                ratedList.push_back(queueTypeId);
                return true;
            }
        }

        // Check if bots should join Skirmish Arena
        // We have extra bots queue because same faction can vs each other but can't be in the same group.
        uint32 skirmishArenaBotCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].skirmishArenaBotCount;
        uint32 skirmishArenaPlayerCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].skirmishArenaPlayerCount;
        uint32 skirmishArenaInstanceCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].skirmishArenaInstanceCount;
        uint32 activeSkirmishArenaQueue =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].activeSkirmishArenaQueue;
        uint32 maxRequiredSkirmishBots = BracketSize * (activeSkirmishArenaQueue + skirmishArenaInstanceCount);
        if (maxRequiredSkirmishBots != 0)
            maxRequiredSkirmishBots = maxRequiredSkirmishBots + TeamSize;

        if ((skirmishArenaBotCount + skirmishArenaPlayerCount) < maxRequiredSkirmishBots)
        {
            return true;
        }

        return false;
    }

    // A real player waits and the bot's faction lacks players for the battleground to start.
    if (FactionMissing(queueTypeId))
        return true;

    // Queues a real player waits in are filled above, as before.
    if (LevelUnbalances(queueTypeId, bracketId))
        return false;

    // Check if bots should join Battleground
    uint32 bgAllianceBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgAllianceBotCount;
    uint32 bgAlliancePlayerCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgAlliancePlayerCount;
    uint32 bgHordeBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgHordeBotCount;
    uint32 bgHordePlayerCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgHordePlayerCount;
    uint32 activeBgQueue = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].activeBgQueue;
    uint32 bgInstanceCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgInstanceCount;
    uint32 const battles = BotBattles(queueTypeId, activeBgQueue, bgInstanceCount,
                                      bgAlliancePlayerCount + bgHordePlayerCount);

    if (teamId == TEAM_ALLIANCE)
    {
        if ((bgAllianceBotCount + bgAlliancePlayerCount) < TeamSize * battles)
            return true;
    }
    else
    {
        if ((bgHordeBotCount + bgHordePlayerCount) < TeamSize * battles)
            return true;
    }

    return false;
}

// CoA: random bots filled a battleground on a first come, first served basis, whatever their level in the
// bracket; on a 1,000-bot test realm the two sides started up to 5.5 levels apart and the higher one won 9
// games out of 10. Bots of every level still join (low ones are part of the game), but one that would take
// its side's average level more than 2 levels away from the other side's, and further than it is, only joins
// on CoaBgLevelBalanceChance percent of its tries. A refused bot stays out of that queue for a minute: it tried
// again about every second, so on a realm of a few hundred bots the chance let it in within seconds anyway and
// the sides of a Warsong Gulch still started up to 11 levels apart.
bool BGJoinAction::LevelUnbalances(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId)
{
    if (sPlayerbotAIConfig.coaBgLevelBalanceChance >= 100)
        return false;
    uint32 const now = getMSTime();
    auto const refused = levelRefusedAt.find(queueTypeId);
    if (refused != levelRefusedAt.end() && getMSTimeDiff(refused->second, now) < 60 * IN_MILLISECONDS)
        return true;
    BattlegroundInfo const& info = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId];
    bool const alliance = bot->GetTeamId() == TEAM_ALLIANCE;
    uint32 const mine = alliance ? info.bgAllianceBotCount + info.bgAlliancePlayerCount
                                 : info.bgHordeBotCount + info.bgHordePlayerCount;
    uint32 const theirs = alliance ? info.bgHordeBotCount + info.bgHordePlayerCount
                                   : info.bgAllianceBotCount + info.bgAlliancePlayerCount;
    if (!theirs)
        return false;
    uint32 const mineSum = alliance ? info.bgAllianceLevelSum : info.bgHordeLevelSum;
    float const target = float(alliance ? info.bgHordeLevelSum : info.bgAllianceLevelSum) / theirs;
    float const after = std::fabs(float(mineSum + bot->GetLevel()) / (mine + 1) - target);
    float const before = mine ? std::fabs(float(mineSum) / mine - target) : after;
    if (after <= 2.0f || after < before || urand(1, 100) <= sPlayerbotAIConfig.coaBgLevelBalanceChance)
        return false;
    levelRefusedAt[queueTypeId] = now;
    return true;
}

bool BGJoinAction::isUseful()
{
    // do not try if BG bots disabled
    if (!sPlayerbotAIConfig.randomBotJoinBG)
        return false;

    // Offered to a player by "lfg bot": it waits for the invitation.
    if (CoaLfgOfferedToOther(bot, nullptr))
        return false;

    // can't queue while in BG/Arena
    if (bot->InBattleground())
        return false;

    // can't queue while in BG/Arena queue
    if (bot->InBattlegroundQueue())
        return false;

    // do not try right after login (currently not working)
    if ((time(nullptr) - bot->GetInGameTime()) < 120)
        return false;

    // check level
    if (bot->GetLevel() < 10)
        return false;

    // do not try if with player master
    if (IsRealPlayer(GET_PLAYERBOT_AI(bot)->GetMaster()))
        return false;

    // do not try if in group, if in group only leader can queue
    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetGUID()))
        return false;

    // do not try if in combat
    if (bot->IsInCombat())
        return false;

    // check Deserter debuff
    if (bot->IsDeserter())
        return false;

    // check if has free queue slots (pointless as already making sure not in queue)
    // keeping just in case.
    if (!bot->HasFreeBattlegroundQueueId())
        return false;

    // do not try if in dungeon
    // Map* map = bot->GetMap();
    // if (map && map->Instanceable())
    //     return false;

    bgList.clear();
    ratedList.clear();
    ratedSoloList.clear();

    for (int bracket = BG_BRACKET_ID_FIRST; bracket < MAX_BATTLEGROUND_BRACKETS; ++bracket)
    {
        for (int queueType = BATTLEGROUND_QUEUE_AV; queueType < MAX_BATTLEGROUND_QUEUE_TYPES; ++queueType)
        {
            BattlegroundQueueTypeId queueTypeId = BattlegroundQueueTypeId(queueType);
            BattlegroundBracketId bracketId = BattlegroundBracketId(bracket);

            if (!canJoinBg(queueTypeId, bracketId))
                continue;

            if (shouldJoinBg(queueTypeId, bracketId))
                bgList.push_back(queueTypeId);
        }
    }

    if (!bgList.empty())
        return true;

    return false;
}

bool BGJoinAction::JoinQueue(uint32 type)
{
    // ignore if player is already in BG, is logging out, or already being teleport
    if (!bot || (!bot->IsInWorld() && !bot->IsBeingTeleported()) || bot->InBattleground())
        return false;

    // get BG TypeId
    BattlegroundQueueTypeId queueTypeId = BattlegroundQueueTypeId(type);
    BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(queueTypeId);
    BattlegroundBracketId bracketId;

    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bg)
        return false;

    uint32 mapId = bg->GetMapId();
    PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
    if (!pvpDiff)
        return false;

    bracketId = pvpDiff->GetBracketId();

    TeamId teamId = bot->GetTeamId();

    // check if already in queue
    if (bot->InBattlegroundQueueForBattlegroundQueueType(queueTypeId))
        return false;

    // check bg req level
    if (!bot->GetBGAccessByLevel(bgTypeId))
        return false;

    // get BG MapId
    uint32 bgTypeId_ = bgTypeId;
    uint32 instanceId = 0;  // 0 = First Available

    // bool isPremade = false; //not used, line marked for removal.
    bool isArena = false;
    bool isRated = false;
    uint8 arenaslot = 0;
    uint8 asGroup = false;

    std::string _bgType;

    // check if arena
    ArenaType arenaType = ArenaType(BattlegroundMgr::BGArenaType(queueTypeId));
    if (arenaType != ARENA_TYPE_NONE)
        isArena = true;

    // get battlemaster
    // Unit* unit = botAI->GetUnit(AI_VALUE2(CreatureData const*, "bg master", bgTypeId));
    Unit* unit = botAI->GetUnit(sRandomPlayerbotMgr.GetBattleMasterGUID(bot, bgTypeId));
    if (!unit && isArena)
    {
        botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
        LOG_DEBUG("playerbots", "Bot {} could not find Battlemaster to join", bot->GetGUID().ToString().c_str());
        return false;
    }

    // This breaks groups as refresh includes a remove from group function call.
    // refresh food/regs
    // sRandomPlayerbotMgr.Refresh(bot);

    bool joinAsGroup = bot->GetGroup() && bot->GetGroup()->GetLeaderGUID() == bot->GetGUID();

    // in wotlk only arena requires battlemaster guid
    // ObjectGuid guid = isArena ? unit->GetGUID() : bot->GetGUID(); //not used, line marked for removal.

    switch (bgTypeId)
    {
        case BATTLEGROUND_AV:
            _bgType = "AV";
            break;
        case BATTLEGROUND_WS:
            _bgType = "WSG";
            break;
        case BATTLEGROUND_AB:
            _bgType = "AB";
            break;
        case BATTLEGROUND_EY:
            _bgType = "EotS";
            break;
        case BATTLEGROUND_RB:
            _bgType = "Random";
            break;
        case BATTLEGROUND_SA:
            _bgType = "SotA";
            break;
        case BATTLEGROUND_IC:
            _bgType = "IoC";
            break;
        default:
            break;
    }

    if (isArena)
    {
        isArena = true;
        isRated = botAI->GetAiObjectContext()->GetValue<uint32>("arena type")->Get();

        if (joinAsGroup)
            asGroup = true;

        switch (arenaType)
        {
            case ARENA_TYPE_1v1:
                arenaslot = 2;
                _bgType = "1v1";
                break;
            case ARENA_TYPE_2v2:
                arenaslot = 0;
                _bgType = "2v2";
                break;
            case ARENA_TYPE_3v3:
                arenaslot = 1;
                _bgType = "3v3";
                break;
            case ARENA_TYPE_5v5:
                arenaslot = 2;
                _bgType = "5v5";
                break;
            default:
                break;
        }
    }

    LOG_INFO("playerbots", "Bot {} {}:{} <{}> queued {} {}", bot->GetGUID().ToString().c_str(),
             bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName().c_str(), _bgType.c_str(),
             isRated   ? "Rated Arena"
             : isArena ? "Arena"
                       : "");

    if (!isArena)
        if (uint32* missing = FactionMissing(queueTypeId))
            *missing -= std::min(*missing, joinAsGroup ? bot->GetGroup()->GetMembersCount() : 1u);

    if (isArena)
    {
        if (!isRated)
        {
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].skirmishArenaBotCount++;
        }
    }
    else
    {
        BattlegroundInfo& info = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId];
        uint32 const members = joinAsGroup ? bot->GetGroup()->GetMembersCount() : 1;
        if (teamId == TEAM_ALLIANCE)
        {
            info.bgAllianceBotCount += members;
            info.bgAllianceLevelSum += members * bot->GetLevel();
        }
        else
        {
            info.bgHordeBotCount += members;
            info.bgHordeLevelSum += members * bot->GetLevel();
        }
    }

    botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);

    WorldPacket* packet = nullptr;
    if (!isArena)
    {
        packet = new WorldPacket(CMSG_BATTLEMASTER_JOIN, 20);
        *packet << bot->GetGUID() << bgTypeId_ << instanceId << joinAsGroup;
    }
    else
    {
        packet = new WorldPacket(CMSG_BATTLEMASTER_JOIN_ARENA, 20);
        *packet << unit->GetGUID() << arenaslot << asGroup << uint8(isRated);
    }
    bot->GetSession()->QueuePacket(packet);
    // Its PvP gear goes on in the queue, not in the arena's preparation (CoaGearByContent).
    botAI->CoaCheckGearSoon();
    return true;
}

uint32* BGJoinAction::RatedSoloNeed(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId)
{
    if (!sPlayerbotAIConfig.coaRatedSoloQueue || !sRandomPlayerbotMgr.IsRandomBot(bot) || bot->GetGroup() ||
        !bot->IsAlive() || bot->IsInCombat() || sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE ||
        sArenaSeasonMgr->GetSeasonState() == ARENA_SEASON_STATE_DISABLED)
        return nullptr;

    ArenaType const type = ArenaType(BattlegroundMgr::BGArenaType(queueTypeId));
    if (type != ARENA_TYPE_1v1 && type != ARENA_TYPE_2v2 && type != ARENA_TYPE_3v3)
        return nullptr;

    BattlegroundInfo& info = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId];
    uint32* need = type != ARENA_TYPE_1v1 && GetCoaRole(bot) == CoaRole::Heal ? &info.ratedSoloHealersNeeded
                                                                              : &info.ratedSoloDamageNeeded;
    if (!*need)
        return nullptr;

    // The core plays it with a personal team in that slot (the 5v5 one for 1v1), one it creates or the bot
    // already has from an earlier match: a bot of a real team there is left out.
    uint8 const slot = ArenaTeam::GetSlotByType(type == ARENA_TYPE_1v1 ? uint32(ARENA_TEAM_5v5) : uint32(type));
    if (uint32 teamId = bot->GetArenaTeamId(slot))
    {
        ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(teamId);
        if (!team || team->GetMembersSize() != 1)
            return nullptr;
    }
    // The core names a new personal team "<name> <bracket>" and refuses the queue when that name is taken, as
    // by a bot team its captain named so: the need would be spent on a bot that never queues.
    else if (sArenaTeamMgr->GetArenaTeamByName(bot->GetName() + " " +
                                               (type == ARENA_TYPE_1v1 ? "1v1" : type == ARENA_TYPE_2v2 ? "2v2" : "3v3")))
        return nullptr;

    return need;
}

// The rated solo queue is joined by a CoA client packet, claimed by the core before any handler and read only
// from the network (AscensionCompat): a bot's own session never goes there, so the hook is called directly. It
// only stores the request under a lock; the core queues the bot from its next update.
bool BGJoinAction::JoinRatedSolo(BattlegroundQueueTypeId queueTypeId)
{
    uint16 constexpr CMSG_BATTLEMASTER_JOIN_ARENA_WITH_ROLE = 0x05C1;
    uint32 constexpr ARENA_ROLE_HEALER = 0x04;

    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
    PvPDifficultyEntry const* pvpDiff = bg ? GetBattlegroundBracketByLevel(bg->GetMapId(), bot->GetLevel()) : nullptr;
    if (!pvpDiff)
        return false;

    uint32* need = RatedSoloNeed(queueTypeId, pvpDiff->GetBracketId());
    if (!need)
        return false;

    ArenaType const type = ArenaType(BattlegroundMgr::BGArenaType(queueTypeId));
    bool const healer = type != ARENA_TYPE_1v1 && GetCoaRole(bot) == CoaRole::Heal;
    WorldPacket packet(CMSG_BATTLEMASTER_JOIN_ARENA_WITH_ROLE, 8);
    packet << uint32(type == ARENA_TYPE_1v1 ? 5 : type) << uint32(healer ? ARENA_ROLE_HEALER : 0);
    if (sScriptMgr->CanPacketReceiveEarly(bot->GetSession(), packet))
    {
        LOG_DEBUG("playerbots", "Bot {} <{}>: the rated solo queue is not available", bot->GetGUID().ToString(),
                  bot->GetName());
        return false;
    }

    --*need;
    LOG_INFO("playerbots", "Bot {} {}:{} <{}> queued rated solo {}v{} as {}", bot->GetGUID().ToString().c_str(),
             bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName().c_str(), uint32(type),
             uint32(type), healer ? "healer" : "damage");

    botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
    // Its PvP gear goes on in the queue, not in the arena's preparation (CoaGearByContent).
    botAI->CoaCheckGearSoon();
    return true;
}

// Not sure if this has ever worked, but it should be similar to BGJoinAction::shouldJoinBg
bool FreeBGJoinAction::shouldJoinBg(BattlegroundQueueTypeId queueTypeId, BattlegroundBracketId bracketId)
{
    BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(queueTypeId);
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bg)
        return false;

    TeamId teamId = bot->GetTeamId();

    uint32 BracketSize = bg->GetMaxPlayersPerTeam() * 2;
    uint32 TeamSize = bg->GetMaxPlayersPerTeam();

    // If the bot is in a group, only the leader can queue
    if (bot->GetGroup() && !bot->GetGroup()->IsLeader(bot->GetGUID()))
        return false;

    // Check if bots should join Arena
    ArenaType type = ArenaType(BattlegroundMgr::BGArenaType(queueTypeId));
    if (type != ARENA_TYPE_NONE)
    {
        BracketSize = (uint32)(type * 2);
        TeamSize = (uint32)type;

        // Check if bots should join Rated Arena (Only captains can queue)
        uint32 ratedArenaBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].ratedArenaBotCount;
        uint32 ratedArenaPlayerCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].ratedArenaPlayerCount;
        uint32 ratedArenaInstanceCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].ratedArenaInstanceCount;
        uint32 activeRatedArenaQueue =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].activeRatedArenaQueue;

        bool isRated = (ratedArenaBotCount + ratedArenaPlayerCount) <
                       (BracketSize * (activeRatedArenaQueue + ratedArenaInstanceCount));

        if (isRated)
        {
            // The personal team of a rated solo queue has no one else to gather.
            ArenaTeam* captainTeam = sArenaTeamMgr->GetArenaTeamByCaptain(bot->GetGUID(), type);
            if (captainTeam && captainTeam->GetMembersSize() > 1)
            {
                sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].ratedArenaBotCount += TeamSize;
                ratedList.push_back(queueTypeId);
                return true;
            }
        }

        // Check if bots should join Skirmish Arena
        // We have extra bots queue because same faction can vs each other but can't be in the same group.
        uint32 skirmishArenaBotCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].skirmishArenaBotCount;
        uint32 skirmishArenaPlayerCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].skirmishArenaPlayerCount;
        uint32 skirmishArenaInstanceCount =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].skirmishArenaInstanceCount;
        uint32 activeSkirmishArenaQueue =
            sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].activeSkirmishArenaQueue;
        uint32 maxRequiredSkirmishBots = BracketSize * (activeSkirmishArenaQueue + skirmishArenaInstanceCount);
        if (maxRequiredSkirmishBots != 0)
            maxRequiredSkirmishBots = maxRequiredSkirmishBots + TeamSize;

        if ((skirmishArenaBotCount + skirmishArenaPlayerCount) < maxRequiredSkirmishBots)
        {
            return true;
        }

        return false;
    }

    // Not where a real player waits for the bot's faction: that queue fills as before.
    if (!FactionMissing(queueTypeId) && LevelUnbalances(queueTypeId, bracketId))
        return false;

    // Check if bots should join Battleground
    uint32 bgAllianceBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgAllianceBotCount;
    uint32 bgAlliancePlayerCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgAlliancePlayerCount;
    uint32 bgHordeBotCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgHordeBotCount;
    uint32 bgHordePlayerCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgHordePlayerCount;
    uint32 activeBgQueue = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].activeBgQueue;
    uint32 bgInstanceCount = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].bgInstanceCount;
    uint32 const battles = BotBattles(queueTypeId, activeBgQueue, bgInstanceCount,
                                      bgAlliancePlayerCount + bgHordePlayerCount);

    if (teamId == TEAM_ALLIANCE)
    {
        if ((bgAllianceBotCount + bgAlliancePlayerCount) < TeamSize * battles)
            return true;
    }
    else
    {
        if ((bgHordeBotCount + bgHordePlayerCount) < TeamSize * battles)
            return true;
    }

    return false;
}

bool BGLeaveAction::Execute(Event /*event*/)
{
    if (!(bot->InBattlegroundQueue() || bot->InBattleground()))
        return false;

    // botAI->ChangeStrategy("-bg", BOT_STATE_NON_COMBAT);

    if (BGStatusAction::LeaveBG(botAI))
        return true;

    // leave queue if not in BG
    BattlegroundQueueTypeId queueTypeId = bot->GetBattlegroundQueueTypeId(0);
    BattlegroundTypeId _bgTypeId = BattlegroundMgr::BGTemplateId(queueTypeId);
    uint8 type = false;
    uint16 unk = 0x1F90;
    uint8 unk2 = 0x0;
    bool isArena = false;
    bool IsRandomBot = sRandomPlayerbotMgr.IsRandomBot(bot);

    ArenaType arenaType = ArenaType(BattlegroundMgr::BGArenaType(queueTypeId));
    if (arenaType)
    {
        isArena = true;
        type = arenaType;
    }

    uint32 queueType = AI_VALUE(uint32, "bg type");
    if (!queueType)
        return false;

    LOG_INFO("playerbots", "Bot {} {}:{} <{}> leaves {} queue", bot->GetGUID().ToString().c_str(),
             bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName().c_str(),
             isArena ? "Arena" : "BG");

    WorldPacket packet(CMSG_BATTLEFIELD_PORT, 20);
    packet << type << unk2 << (uint32)_bgTypeId << unk << uint8(0);
    bot->GetSession()->QueuePacket(new WorldPacket(packet));

    if (IsRandomBot)
        botAI->SetMaster(nullptr);

    botAI->ResetStrategies(!IsRandomBot);
    botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
    botAI->GetAiObjectContext()->GetValue<uint32>("bg role")->Set(0);
    botAI->GetAiObjectContext()->GetValue<uint32>("arena type")->Set(0);

    return true;
}

bool BGStatusAction::LeaveBG(PlayerbotAI* botAI)
{
    Player* bot = botAI->GetBot();
    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;
    bool isArena = bg->isArena();
    bool isRandomBot = sRandomPlayerbotMgr.IsRandomBot(bot);

    if (isRandomBot)
        botAI->SetMaster(nullptr);

    botAI->ChangeStrategy("-warsong", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-warsong", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-arathi", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-arathi", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-eye", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-eye", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-isle", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-isle", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-battleground", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-battleground", BOT_STATE_NON_COMBAT);
    botAI->ChangeStrategy("-arena", BOT_STATE_COMBAT);
    botAI->ChangeStrategy("-arena", BOT_STATE_NON_COMBAT);

    LOG_INFO("playerbots", "Bot {} {}:{} <{}> leaves {}", bot->GetGUID().ToString().c_str(),
             bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(),
             isArena ? "Arena" : "BG");

    WorldPacket packet(CMSG_LEAVE_BATTLEFIELD);
    packet << uint8(0);
    packet << uint8(0);  // BattlegroundTypeId-1 ?
    packet << uint32(0);
    packet << uint16(0);

    bot->GetSession()->HandleBattlefieldLeaveOpcode(packet);

    botAI->ResetStrategies(!isRandomBot);
    botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
    botAI->GetAiObjectContext()->GetValue<uint32>("bg role")->Set(0);
    botAI->GetAiObjectContext()->GetValue<uint32>("arena type")->Set(0);
    PositionMap& posMap = botAI->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get();
    PositionInfo pos = botAI->GetAiObjectContext()->GetValue<PositionMap&>("position")->Get()["bg objective"];
    pos.Reset();
    posMap["bg objective"] = pos;
    return true;
}

bool BGStatusAction::isUseful() { return bot->InBattlegroundQueue(); }

bool BGStatusAction::Execute(Event event)
{
    uint32 QueueSlot;
    uint32 instanceId;
    uint32 mapId = 0;
    uint32 statusid = 0;
    uint32 Time1 = 0;
    uint32 Time2 = 0;
    std::string _bgType;

    uint64 arenaByte;
    uint8 arenaTeam;
    uint8 isRated;
    uint64 unk0;
    uint8 minlevel;
    uint8 maxlevel;

    WorldPacket p(event.getPacket());
    statusid = 0;
    p >> QueueSlot;  // queue id (0...2) - player can be in 3 queues in time
    p >> arenaByte;
    if (arenaByte == 0)
        return false;
    p >> minlevel;
    p >> maxlevel;
    p >> instanceId;
    p >> isRated;
    p >> statusid;

    // check status
    switch (statusid)
    {
        case STATUS_WAIT_QUEUE:  // status_in_queue
            p >> Time1;          // average wait time, milliseconds
            p >> Time2;          // time in queue, updated every minute!, milliseconds
            break;
        case STATUS_WAIT_JOIN:  // status_invite
            p >> mapId;         // map id
            p >> unk0;
            p >> Time1;  // time to remove from queue, milliseconds
            break;
        case STATUS_IN_PROGRESS:  // status_in_progress
            p >> mapId;           // map id
            p >> unk0;
            p >> Time1;  // time to bg auto leave, 0 at bg start, 120000 after bg end, milliseconds
            p >> Time2;  // time from bg start, milliseconds
            p >> arenaTeam;
            break;
        default:
            LOG_ERROR("playerbots", "Unknown BG status!");
            break;
    }

    bool IsRandomBot = sRandomPlayerbotMgr.IsRandomBot(bot);
    BattlegroundQueueTypeId queueTypeId = bot->GetBattlegroundQueueTypeId(QueueSlot);
    BattlegroundTypeId _bgTypeId = BattlegroundMgr::BGTemplateId(queueTypeId);
    if (!queueTypeId)
        return false;

    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(_bgTypeId);
    mapId = bg->GetMapId();
    PvPDifficultyEntry const* pvpDiff = GetBattlegroundBracketByLevel(mapId, bot->GetLevel());
    if (!pvpDiff)
        return false;

    BattlegroundBracketId bracketId = pvpDiff->GetBracketId();

    bool isArena = false;
    uint8 type = false;  // arenatype if arena
    uint16 unk = 0x1F90;
    uint8 unk2 = 0x0;
    uint8 action = 0x1;

    ArenaType arenaType = ArenaType(BattlegroundMgr::BGArenaType(queueTypeId));
    if (arenaType)
    {
        isArena = true;
        type = arenaType;
    }

    switch (_bgTypeId)
    {
        case BATTLEGROUND_AV:
            _bgType = "AV";
            break;
        case BATTLEGROUND_WS:
            _bgType = "WSG";
            break;
        case BATTLEGROUND_AB:
            _bgType = "AB";
            break;
        case BATTLEGROUND_EY:
            _bgType = "EotS";
            break;
        case BATTLEGROUND_RB:
            _bgType = "Random";
            break;
        case BATTLEGROUND_SA:
            _bgType = "SotA";
            break;
        case BATTLEGROUND_IC:
            _bgType = "IoC";
            break;
        default:
            break;
    }

    switch (arenaType)
    {
        case ARENA_TYPE_1v1:
            _bgType = "1v1";
            break;
        case ARENA_TYPE_2v2:
            _bgType = "2v2";
            break;
        case ARENA_TYPE_3v3:
            _bgType = "3v3";
            break;
        case ARENA_TYPE_5v5:
            _bgType = "5v5";
            break;
        default:
            break;
    }

    //TeamId teamId = bot->GetTeamId(); //not used, line marked for removal.

    if (Time1 == TIME_TO_AUTOREMOVE)  // Battleground is over, bot needs to leave
    {
        LOG_INFO("playerbots", "Bot {} <{}> ({} {}): Received BG status TIME_TO_AUTOREMOVE for {} {}",
                 bot->GetGUID().ToString().c_str(), bot->GetName(), bot->GetLevel(),
                 bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", isArena ? "Arena" : "BG", _bgType);

        if (LeaveBG(botAI))
            return true;
    }

    if (statusid == STATUS_WAIT_QUEUE)  // bot is in queue
    {
        LOG_INFO("playerbots", "Bot {} {}:{} <{}>: Received BG status WAIT_QUEUE (wait time: {}) for {} {}",
                 bot->GetGUID().ToString().c_str(), bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(),
                 bot->GetName(), Time2, isArena ? "Arena" : "BG", _bgType);
        // temp fix for crash
        // return true;

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(queueTypeId);
        GroupQueueInfo ginfo;
        if (bgQueue.GetPlayerGroupInfoData(bot->GetGUID(), &ginfo))
        {
            if (ginfo.IsInvitedToBGInstanceGUID && !bot->InBattleground())
            {
                // BattlegroundMgr::GetBattleground() does not return battleground if bgTypeId==BATTLEGROUND_AA
                Battleground* bg = sBattlegroundMgr->GetBattleground(
                    ginfo.IsInvitedToBGInstanceGUID, _bgTypeId == BATTLEGROUND_AA ? BATTLEGROUND_TYPE_NONE : _bgTypeId);
                if (bg)
                {
                    if (isArena)
                    {
                        _bgTypeId = bg->GetBgTypeID();
                    }

                    LOG_INFO("playerbots", "Bot {} {}:{} <{}>: Force join {} {}", bot->GetGUID().ToString().c_str(),
                             bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(),
                             isArena ? "Arena" : "BG", _bgType);
                    WorldPacket emptyPacket;
                    bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);
                    action = 0x1;

                    WorldPacket packet(CMSG_BATTLEFIELD_PORT, 20);
                    packet << type << unk2 << (uint32)_bgTypeId << unk << action;
                    bot->GetSession()->QueuePacket(new WorldPacket(packet));

                    botAI->ResetStrategies(false);
                    if (!bot->GetBattleground())
                    {
                        // first bot to join wont have battleground and PlayerbotAI::ResetStrategies() wont set them up
                        // properly, set bg for "bg strategy check" to fix that
                        botAI->ChangeStrategy("+bg", BOT_STATE_NON_COMBAT);
                    }
                    context->GetValue<uint32>("bg role")->Set(urand(0, 9));
                    PositionMap& posMap = context->GetValue<PositionMap&>("position")->Get();
                    PositionInfo pos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
                    pos.Reset();
                    posMap["bg objective"] = pos;

                    return true;
                }
            }
        }

        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(_bgTypeId);
        if (!bg)
            return false;

        bool leaveQ = false;
        uint32 timer;
        if (isArena)
            timer = TIME_TO_AUTOREMOVE;
        else
            timer = TIME_TO_AUTOREMOVE + 1000 * (bg->GetMaxPlayersPerTeam() * 8);

        if (Time2 > timer && isArena)  // disabled for BG
            leaveQ = true;

        if (leaveQ && ((bot->GetGroup() && bot->GetGroup()->IsLeader(bot->GetGUID())) ||
                       !(bot->GetGroup() || botAI->GetMaster())))
        {
            //TeamId teamId = bot->GetTeamId(); //not used, line marked for removal.
            bool realPlayers = false;
            BattlegroundInfo& info = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId];
            // A bot of a rated solo queue waits as long as a real player waits there, unless no lineup needs it.
            if (isRated && ginfo.Players.size() == 1)
            {
                uint32& surplus = arenaType != ARENA_TYPE_1v1 && GetCoaRole(bot) == CoaRole::Heal
                                      ? info.ratedSoloHealersSurplus
                                      : info.ratedSoloDamageSurplus;
                realPlayers =
                    !surplus && info.ratedSoloPlayerHealers + info.ratedSoloPlayerDamage + info.ratedSoloPremades > 0;
                if (surplus)
                    --surplus;
            }
            else if (isRated)
                realPlayers = sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].ratedArenaPlayerCount > 0;
            else
                realPlayers =
                    sRandomPlayerbotMgr.BattlegroundData[queueTypeId][bracketId].skirmishArenaPlayerCount > 0;

            if (realPlayers)
                return false;

            LOG_INFO("playerbots", "Bot {} {}:{} <{}> waited too long and leaves queue ({} {}).",
                     bot->GetGUID().ToString().c_str(), bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(),
                     bot->GetName(), isArena ? "Arena" : "BG", _bgType);

            WorldPacket packet(CMSG_BATTLEFIELD_PORT, 20);
            action = 0;
            packet << type << unk2 << (uint32)_bgTypeId << unk << action;
            bot->GetSession()->QueuePacket(new WorldPacket(packet));

            botAI->ResetStrategies(!IsRandomBot);
            botAI->GetAiObjectContext()->GetValue<uint32>("bg type")->Set(0);
            botAI->GetAiObjectContext()->GetValue<uint32>("bg role")->Set(0);
            botAI->GetAiObjectContext()->GetValue<uint32>("arena type")->Set(0);

            return true;
        }
    }

    if (statusid == STATUS_IN_PROGRESS)  // placeholder for Leave BG if it takes too long
    {
        LOG_INFO("playerbots", "Bot {} {}:{} <{}>: Received BG status IN_PROGRESS for {} {}",
                 bot->GetGUID().ToString().c_str(), bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(),
                 bot->GetName(), isArena ? "Arena" : "BG", _bgType);
        return false;
    }

    if (statusid == STATUS_WAIT_JOIN)  // bot may join
    {
        LOG_INFO("playerbots", "Bot {} {}:{} <{}>: Received BG status WAIT_JOIN for {} {}",
                 bot->GetGUID().ToString().c_str(), bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(),
                 bot->GetName(), isArena ? "Arena" : "BG", _bgType);

        if (isArena)
        {
            isArena = true;
            BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(queueTypeId);

            GroupQueueInfo ginfo;
            if (!bgQueue.GetPlayerGroupInfoData(bot->GetGUID(), &ginfo))
            {
                LOG_ERROR("playerbots", "Bot {} {}:{} <{}>: Missing QueueInfo for {} {}",
                          bot->GetGUID().ToString().c_str(), bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H",
                          bot->GetLevel(), bot->GetName(), isArena ? "Arena" : "BG", _bgType);
                return false;
            }

            if (ginfo.IsInvitedToBGInstanceGUID)
            {
                // BattlegroundMgr::GetBattleground() does not return battleground if bgTypeId==BATTLEGROUND_AA
                Battleground* bg = sBattlegroundMgr->GetBattleground(
                    ginfo.IsInvitedToBGInstanceGUID, _bgTypeId == BATTLEGROUND_AA ? BATTLEGROUND_TYPE_NONE : _bgTypeId);
                if (!bg)
                {
                    LOG_ERROR("playerbots", "Bot {} {}:{} <{}>: Missing QueueInfo for {} {}",
                              bot->GetGUID().ToString().c_str(), bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H",
                              bot->GetLevel(), bot->GetName(), isArena ? "Arena" : "BG", _bgType);
                    return false;
                }

                _bgTypeId = bg->GetBgTypeID();
            }
        }

        LOG_INFO("playerbots", "Bot {} {}:{} <{}> joined {} - {}", bot->GetGUID().ToString().c_str(),
                 bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H", bot->GetLevel(), bot->GetName(),
                 isArena ? "Arena" : "BG", _bgType);

        WorldPacket emptyPacket;
        bot->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);

        if (bot->IsInCombat() && !botAI->GetMaster() && sRandomPlayerbotMgr.IsRandomBot(bot))
            bot->CombatStopWithPets(true);

        action = 0x1;

        WorldPacket packet(CMSG_BATTLEFIELD_PORT, 20);
        packet << type << unk2 << (uint32)_bgTypeId << unk << action;
        bot->GetSession()->QueuePacket(new WorldPacket(packet));

        botAI->ResetStrategies(false);
        if (!bot->GetBattleground())
        {
            // first bot to join wont have battleground and PlayerbotAI::ResetStrategies() wont set them up properly,
            // set bg for "bg strategy check" to fix that
            botAI->ChangeStrategy("+bg", BOT_STATE_NON_COMBAT);
        }
        context->GetValue<uint32>("bg role")->Set(urand(0, 9));
        PositionMap& posMap = context->GetValue<PositionMap&>("position")->Get();
        PositionInfo pos = context->GetValue<PositionMap&>("position")->Get()["bg objective"];
        PositionInfo pos2 = context->GetValue<PositionMap&>("position")->Get()["bg siege"];
        pos.Reset();
        pos2.Reset();
        posMap["bg objective"] = pos;
        posMap["bg siege"] = pos2;

        return true;
    }

    return true;
}

bool BGStatusCheckAction::Execute(Event /*event*/)
{
    if (bot->IsBeingTeleported())
        return false;

    // The core refuses the port of a player in combat: a random bot fighting outside (a mob, a High-Risk
    // player) would let the invitation run out and leave its team one short.
    if (bot->IsInCombat() && !botAI->GetMaster() && sRandomPlayerbotMgr.IsRandomBot(bot))
        bot->CombatStop(true);

    WorldPacket packet(CMSG_BATTLEFIELD_STATUS);
    bot->GetSession()->HandleBattlefieldStatusOpcode(packet);

    LOG_INFO("playerbots", "Bot {} <{}> ({} {}) : Checking BG invite status", bot->GetGUID().ToString().c_str(),
             bot->GetName(), bot->GetLevel(), bot->GetTeamId() == TEAM_ALLIANCE ? "A" : "H");

    return true;
}

bool BGStatusCheckAction::isUseful() { return bot->InBattlegroundQueue(); }

bool BGStrategyCheckAction::Execute(Event /*event*/)
{
    bool inside_bg = bot->InBattleground() && bot->GetBattleground();
    ;
    if (!inside_bg && botAI->HasStrategy("battleground", BOT_STATE_NON_COMBAT))
    {
        // A random bot without a master that comes back from a battleground or an arena still grouped with a real
        // player (solo queue team mates kept in the party) leaves that party: nobody invited it there.
        if (!botAI->GetMaster() && sRandomPlayerbotMgr.IsRandomBot(bot))
        {
            Group* group = bot->GetGroup();
            if (group && !group->isBGGroup() && !group->isBFGroup())
            {
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (member && member != bot && !GET_PLAYERBOT_AI(member))
                    {
                        LOG_INFO("playerbots", "Bot {} <{}> leaves the party it was left in after a battleground",
                                 bot->GetGUID().ToString().c_str(), bot->GetName());
                        bot->RemoveFromGroup();
                        break;
                    }
                }
            }
        }
        botAI->ResetStrategies();
        return true;
    }
    if (inside_bg && !botAI->HasStrategy("battleground", BOT_STATE_NON_COMBAT))
    {
        botAI->ResetStrategies();
        return false;
    }
    return false;
}
