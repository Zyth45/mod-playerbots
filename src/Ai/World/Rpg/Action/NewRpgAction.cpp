/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NewRpgAction.h"
#include "CoaBotAuctions.h"
#include "CoaCityLife.h"
#include "CoaRpgTrade.h"
#include "CoaPathProbe.h"
#include "RandomPlayerbotMgr.h"
#include "CoaSafeMove.h"
#include "CoaSafeMoveRules.h"
#include "CoaEconomyRules.h"
#include "AreaDefines.h"
#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "DBCStores.h"
#include "GossipDef.h"
#include "IVMapMgr.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Engine.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTextMgr.h"
#include "QuestDef.h"
#include "Random.h"
#include "SharedDefines.h"
#include "Timer.h"
#include "TravelMgr.h"
#include "WaypointMovementGenerator.h"
#include "G3D/Vector2.h"
#include <cmath>
#include <cstdlib>

void TellRpgStatusAction::WhisperStatusChange(Player* owner, std::string const& statusName)
{
    std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
        RPG_STATUS_CHANGED_KEY, RPG_STATUS_CHANGED_DEFAULT,
        {{"%status", statusName}});
    bot->Whisper(msg, LANG_UNIVERSAL, owner);
}

bool TellRpgStatusAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    std::string const text = event.getParam();
    if (text.empty())
    {
        std::string out = botAI->rpgInfo.ToString();
        bot->Whisper(out.c_str(), LANG_UNIVERSAL, owner);
        return true;
    }

    Player* master = botAI->GetMaster();
    bool isMaster = master && master->GetGUID() == owner->GetGUID();
    bool isGM = owner->GetSession() && owner->GetSession()->GetSecurity() >= SEC_GAMEMASTER;
    if (!isMaster && !isGM)
    {
        std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "rpg_debug_permission_error",
            "Only your master or a GM can change my rpg status.", {});
        bot->Whisper(msg, LANG_UNIVERSAL, owner);
        return false;
    }

    std::string name = text;
    uint32 questId = 0;
    static std::string const doQuestPrefix = "do quest ";
    size_t doQuestPos = text.find(doQuestPrefix);
    if (doQuestPos != std::string::npos)
    {
        name = "do quest";
        std::string idStr = text.substr(doQuestPos + doQuestPrefix.length());
        try
        {
            questId = static_cast<uint32>(std::stoul(idStr));
        }
        catch (std::exception const&)
        {
            questId = 0;
        }
    }

    NewRpgStatus status = NewRpgInfo::StatusFromString(name);
    NewRpgInfo& info = botAI->rpgInfo;

    if (status == RPG_IDLE)
    {
        info.ChangeToIdle();
        WhisperStatusChange(owner, "IDLE");
        return true;
    }
    else if (status == RPG_REST)
    {
        info.ChangeToRest();
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        WhisperStatusChange(owner, "REST");
        return true;
    }
    else if (status == RPG_WANDER_RANDOM)
    {
        info.ChangeToWanderRandom();
        WhisperStatusChange(owner, "WANDER_RANDOM");
        return true;
    }
    else if (status == RPG_WANDER_NPC)
    {
        info.ChangeToWanderNpc();
        WhisperStatusChange(owner, "WANDER_NPC");
        return true;
    }
    else if (status == RPG_GO_GRIND)
    {
        WorldPosition pos = SelectRandomGrindPos(bot);
        if (pos == WorldPosition())
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_no_grind_pos_error", "No grind position available.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        info.ChangeToGoGrind(pos);
        WhisperStatusChange(owner, "GO_GRIND");
        return true;
    }
    else if (status == RPG_GO_CAMP)
    {
        WorldPosition pos = SelectRandomCampPos(bot);
        if (pos == WorldPosition())
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_no_camp_pos_error", "No camp position available.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        info.ChangeToGoCamp(pos);
        WhisperStatusChange(owner, "GO_CAMP");
        return true;
    }
    else if (status == RPG_TRAVEL_FLIGHT)
    {
        uint32 flightMasterEntry = 0;
        WorldPosition flightMasterPos;
        std::vector<uint32> path;
        if (!SelectRandomFlightTaxiNode(flightMasterEntry, flightMasterPos, path))
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_no_flight_path_error", "No flight path available.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        info.ChangeToTravelFlight(flightMasterEntry, flightMasterPos, std::move(path));
        WhisperStatusChange(owner, "TRAVEL_FLIGHT");
        return true;
    }
    else if (status == RPG_OUTDOOR_PVP)
    {
        info.ChangeToOutdoorPvp();
        WhisperStatusChange(owner, "OUTDOOR_PVP");
        return true;
    }
    else if (status == RPG_GO_CITY)
    {
        WorldPosition pos = SelectRandomCityPos(bot);
        if (pos == WorldPosition())
            return false;
        info.ChangeToGoCity(pos);
        WhisperStatusChange(owner, "GO_CITY");
        return true;
    }
    else if (status == RPG_DO_QUEST)
    {
        if (!questId)
        {
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 qid = bot->GetQuestSlotQuestId(slot);
                if (!qid)
                    continue;
                std::vector<POIInfo> poi;
                if (GetQuestPOIPosAndObjectiveIdx(qid, poi, true))
                {
                    questId = qid;
                    break;
                }
            }
        }
        if (!questId)
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_no_quest_error", "No quest available; use 'do quest <id>'.", {});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        QuestStatus questStatus = bot->GetQuestStatus(questId);
        if (!quest || (questStatus != QUEST_STATUS_INCOMPLETE && questStatus != QUEST_STATUS_COMPLETE))
        {
            std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "rpg_invalid_quest_error", "Invalid quest %quest_id",
                {{"%quest_id", std::to_string(questId)}});
            bot->Whisper(msg, LANG_UNIVERSAL, owner);
            return false;
        }
        info.ChangeToDoQuest(questId, quest);
        WhisperStatusChange(owner, "DO_QUEST " + std::to_string(questId));
        return true;
    }

    std::string msg = PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "rpg_unknown_status_error",
        "Unknown rpg status. Options: idle, rest, wander random, wander npc, "
        "go grind, go camp, do quest [<id>], travel flight, outdoor pvp, go city.", {});
    bot->Whisper(msg, LANG_UNIVERSAL, owner);
    return false;
}

bool StartRpgDoQuestAction::Execute(Event event)
{
    Player* owner = event.getOwner();
    if (!owner)
        return false;

    std::string const text = event.getParam();
    PlayerbotChatHandler ch(owner);
    uint32 questId = ch.extractQuestId(text);
    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (quest)
    {
        botAI->rpgInfo.ChangeToDoQuest(questId, quest);
        bot->Whisper("Start to do quest " + std::to_string(questId), LANG_UNIVERSAL, owner);
        return true;
    }
    bot->Whisper("Invalid quest " + text, LANG_UNIVERSAL, owner);
    return false;
}

bool NewRpgStatusUpdateAction::Execute(Event /*event*/)
{
    // 3.2.2: a random bot stuck on a closed patch of a capital's navmesh (under a building) back on the city's floor.
    if (RandomPlayerbotMgr::instance().IsRandomBot(bot) && !botAI->HasGameClientMaster() &&
        CoaPathProbe::RescueFromPatch(bot))
        return true;
    NewRpgInfo& info = botAI->rpgInfo;
    // 3.2.2 (the user: bots in the Stormwind inn's floor, one still there for an hour): in the floor, or still on a
    // wander no step leads anywhere, put on the capital's nearest checked arrival (CityUnstick).
    if (RandomPlayerbotMgr::instance().IsRandomBot(bot) && !botAI->HasGameClientMaster())
    {
        bool wandering = false;
        if (info.GetStatus() == RPG_WANDER_NPC)
            if (auto const* wander = std::get_if<NewRpgInfo::WanderNpc>(&info.data))
                wandering = !wander->lastReach && !(wander->npcOrGo && CoaCityLife::Holds(botAI, wander->npcOrGo));
        if (CoaPathProbe::CityUnstick(bot, wandering))
        {
            info.ChangeToIdle();
            return true;
        }
    }
    NewRpgStatus status = info.GetStatus();
    switch (status)
    {
        case RPG_IDLE:
            // Back from a city visit first: home again, then pick the next activity.
            if (info.cityStayMs || info.cityReturnPos != WorldPosition())
            {
                LeaveCity();
                return true;
            }
            // CoA Economy: business with a vendor or an auctioneer in reach: go there first
            if (CoaEconomy::RpgTrade::HasTradeNpcInReach(botAI))
            {
                info.ChangeToWanderNpc();
                return true;
            }
            return RandomChangeStatus({RPG_GO_CAMP, RPG_GO_GRIND, RPG_WANDER_RANDOM, RPG_WANDER_NPC, RPG_DO_QUEST,
                                       RPG_TRAVEL_FLIGHT, RPG_REST, RPG_OUTDOOR_PVP, RPG_GO_CITY});

        case RPG_GO_GRIND:
        {
            auto& data = std::get<NewRpgInfo::GoGrind>(info.data);
            WorldPosition& originalPos = data.pos;
            if (originalPos == WorldPosition())
            {
                LOG_DEBUG("playerbots", "[New RPG] {} in GO_GRIND with no destination, back to idle", bot->GetName());
                info.ChangeToIdle();
                return true;
            }
            // GO_GRIND -> WANDER_RANDOM
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderRandom();
                return true;
            }
            // Could not get there in time: pick something else rather than walking forever.
            if (info.HasStatusPersisted(statusGoGrindDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_GO_CAMP:
        {
            auto& data = std::get<NewRpgInfo::GoCamp>(info.data);
            WorldPosition& originalPos = data.pos;
            if (originalPos == WorldPosition())
            {
                LOG_DEBUG("playerbots", "[New RPG] {} in GO_CAMP with no destination, back to idle", bot->GetName());
                info.ChangeToIdle();
                return true;
            }
            // GO_CAMP -> WANDER_NPC
            if (bot->GetExactDist(originalPos) < 10.0f)
            {
                info.ChangeToWanderNpc();
                return true;
            }
            if (info.HasStatusPersisted(statusGoCampDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_WANDER_RANDOM:
        {
            // WANDER_RANDOM -> IDLE
            if (info.HasStatusPersisted(statusWanderRandomDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_GO_CITY:
        {
            auto& data = std::get<NewRpgInfo::GoCity>(info.data);
            // GO_CITY -> WANDER_NPC for CoaCityStayMin/MaxMinutes among the city NPCs
            if (bot->GetExactDist(data.pos) < 15.0f)
            {
                info.ChangeToWanderNpc();
                uint32 const shortest =
                    std::min(sPlayerbotAIConfig.coaCityStayMinMinutes, sPlayerbotAIConfig.coaCityStayMaxMinutes);
                uint32 const longest =
                    std::max(sPlayerbotAIConfig.coaCityStayMinMinutes, sPlayerbotAIConfig.coaCityStayMaxMinutes);
                // At least a minute: a zero stay would fall back to the short stop of a plain NPC visit.
                info.cityStayMs = std::max<uint32>(urand(shortest, longest), 1) * MINUTE * IN_MILLISECONDS;
                // CoA Live City Life: the residents stay longer (unchanged when off).
                info.cityStayMs = CoaCityLife::VisitMs(bot, info.cityStayMs);
                return true;
            }
            if (info.HasStatusPersisted(statusGoCityDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_WANDER_NPC:
        {
            // 3.2.2 (Pilote, bench of 96b4114c: auctioneers not reached 11 % the first hour, 25 % the second, the
            // bots still 70 to 150 yards off, the wander's action last run 190 to 880 s before, nothing run since): the
            // bot's AI slept (no player near it) and woke up past its stay, this check first on its tick. The time
            // asleep is not part of the stay: the wander goes on where it stopped.
            if (auto* wander = std::get_if<NewRpgInfo::WanderNpc>(&info.data))
            {
                constexpr uint32 WANDER_SLEPT_MS = 30 * IN_MILLISECONDS;
                uint32 const slept = wander->lastRun ? GetMSTimeDiffToNow(wander->lastRun) : 0;
                if (slept > WANDER_SLEPT_MS)
                {
                    info.startT += slept;
                    wander->lastRun = getMSTime();
                    if (wander->lastReach)
                        wander->lastReach += slept;
                }
            }
            if (info.HasStatusPersisted(info.cityStayMs ? info.cityStayMs : statusWanderNpcDuration))
            {
                if (auto* wander = std::get_if<NewRpgInfo::WanderNpc>(&info.data))
                {
                    Creature* npc = wander->npcOrGo.IsCreature() ? ObjectAccessor::GetCreature(*bot, wander->npcOrGo)
                                                                 : nullptr;
                    bool const auctioneer = npc && npc->HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER);
                    bool const trade = auctioneer || (npc && npc->HasNpcFlag(UNIT_NPC_FLAG_VENDOR) &&
                                                      CoaEconomy::RpgTrade::WantsTrade(botAI));
                    // 3.2.2 (bench of d061adec): a bot whose AI slept for minutes by its NPC woke up there, and this check,
                    // first on its tick, ended the wander before the wander's action could count it there ("not
                    // reached", in reach, its action last run 90 to 640 s before). Once, the wander is kept for it.
                    if (npc && !wander->lastReach && !wander->reachGrace && IsWithinInteractionDist(npc))
                    {
                        wander->reachGrace = true;
                        break;
                    }
                    // 3.2: on its way to a trade NPC, the bot gets there before the wander ends.
                    if (CoaEconomyRules::KeepsTradeGoal(trade, wander->lastReach != 0,
                                                        wander->pickedAt ? GetMSTimeDiffToNow(wander->pickedAt) : 0))
                        break;
                    // 3.2 (BuyLog): an auctioneer the wander never got to (counted here, not as left for idle).
                    if ((auctioneer || wander->toAuctioneer) && !wander->lastReach)
                    {
                        CoaEconomy::BotAuctions::NoteWay(CoaEconomy::BotAuctions::Way::TimedOut);
                        // 3.2.1: where it stood, per capital.
                        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId());
                        // 3.2.2: bots still within a few yards and never counted as there: did the wander's action
                        // still run, was the NPC in reach for it, and what ran instead.
                        Engine* engine = botAI->GetEngine(BOT_STATE_NON_COMBAT);
                        CoaEconomy::BotAuctions::NoteNotReached(
                            zone && (zone->flags & AREA_FLAG_CAPITAL) ? zone->area_name[0] : "",
                            npc ? npc->GetEntry() : 0, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                            npc ? bot->GetExactDist(npc) : 0.0f, bot->isMoving(), wander->refusals,
                            wander->lastRun ? GetMSTimeDiffToNow(wander->lastRun) / IN_MILLISECONDS : 0,
                            npc && IsWithinInteractionDist(npc),
                            engine ? engine->GetLastExecutedAction().c_str() : "");
                    }
                    wander->toAuctioneer = false;
                }
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_DO_QUEST:
        {
            // DO_QUEST -> IDLE
            if (info.HasStatusPersisted(statusDoQuestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_TRAVEL_FLIGHT:
        {
            auto& data = std::get<NewRpgInfo::TravelFlight>(info.data);
            if (data.inFlight && !bot->IsInFlight())
            {
                // flight arrival
                info.ChangeToIdle();
                return true;
            }
            // Never cut a flight short: the cap is for a bot that cannot reach its flight master.
            if (!bot->IsInFlight() && info.HasStatusPersisted(statusTravelFlightDuration))
            {
                LOG_DEBUG("playerbots", "[New RPG] {} gave up travelling to flight master {}", bot->GetName(),
                          data.flightMasterEntry);
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_REST:
        {
            // REST -> IDLE
            if (info.HasStatusPersisted(statusRestDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        case RPG_OUTDOOR_PVP:
        {
            if (info.HasStatusPersisted(statusOutDoorPvPDuration))
            {
                info.ChangeToIdle();
                return true;
            }
            break;
        }
        default:
            break;
    }
    return false;
}

bool NewRpgGoGrindAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;
    if (auto* data = std::get_if<NewRpgInfo::GoGrind>(&botAI->rpgInfo.data))
    {
        if (MoveFarTo(data->pos))
            return true;
        // Small nudge so the next tick's MoveFarTo starts from a
        // slightly different position. Kept small so it doesn't look
        // like the bot is abandoning its destination.
        return MoveRandomNear(10.0f);
    }

    return false;
}

bool NewRpgGoCampAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    if (auto* data = std::get_if<NewRpgInfo::GoCamp>(&botAI->rpgInfo.data))
    {
        if (MoveFarTo(data->pos))
            return true;
        return MoveRandomNear(10.0f);
    }

    return false;
}

bool NewRpgGoCityAction::Execute(Event /*event*/)
{
    if (auto* data = std::get_if<NewRpgInfo::GoCity>(&botAI->rpgInfo.data))
    {
        if (MoveFarTo(data->pos))
            return true;
        return MoveRandomNear(10.0f);
    }

    return false;
}

bool NewRpgWanderRandomAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    return MoveRandomNear();
}

bool NewRpgWanderNpcAction::Execute(Event /*event*/)
{
    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::WanderNpc>(&info.data);
    if (!dataPtr)
        return false;
    auto& data = *dataPtr;
    data.lastRun = getMSTime();
    if (!data.npcOrGo)
    {
        // CoA Live City Life: on a city visit, now and then a station (inn, bank, auction house, forge, dummy).
        ObjectGuid npcOrGo = CoaCityLife::ChooseStation(botAI);
        bool const station = !npcOrGo.IsEmpty();
        if (npcOrGo.IsEmpty())
            npcOrGo = ChooseNpcOrGameObjectToInteract();
        // 3.2 (BuyLog): the way to an auctioneer, counted.
        data.toAuctioneer = false;
        if (Creature* npc = npcOrGo.IsCreature() ? ObjectAccessor::GetCreature(*bot, npcOrGo) : nullptr)
            if (npc->HasNpcFlag(UNIT_NPC_FLAG_AUCTIONEER))
            {
                data.toAuctioneer = true;
                CoaEconomy::BotAuctions::NoteWay(station ? CoaEconomy::BotAuctions::Way::Station
                                                         : CoaEconomy::BotAuctions::Way::Picked);
            }
        // No npc can be found, switch to IDLE
        if (npcOrGo.IsEmpty())
        {
            info.ChangeToIdle();
            return true;
        }
        data.npcOrGo = npcOrGo;
        data.lastReach = 0;
        data.refusals = 0;
        data.reachGrace = false;
        data.pickedAt = getMSTime();
        return true;
    }

    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, data.npcOrGo);
    // CoA Live City Life: a bot that stepped from its auctioneer into the room still stays there.
    // 3.2.2: a square of the city is reached at its slot, never at its anchor NPC (no quest, no trade there).
    bool const square = object && CoaCityLife::HoldsSquare(botAI, data.npcOrGo);
    if (object && (square ? CoaCityLife::AtSquare(botAI)
                          : IsWithinInteractionDist(object) || (data.lastReach && CoaCityLife::InRoom(botAI, object))))
    {
        if (!data.lastReach)
        {
            data.lastReach = getMSTime();
            data.givenUp = 0;
            if (!square && bot->CanInteractWithQuestGiver(object))
                InteractWithNpcOrGameObjectForQuest(data.npcOrGo);
            // CoA Economy: sell, repair or use the auction house, as people do between two quests
            if (!square)
                CoaEconomy::RpgTrade::TradeAtNpc(botAI, object);
            CoaCityLife::OnReach(botAI, object);
            return true;
        }

        if (data.lastReach && GetMSTimeDiffToNow(data.lastReach) < CoaCityLife::WhileStaying(botAI, object, npcStayTime))
            return false;

        // has reached the npc for more than `npcStayTime`, select the next target
        CoaCityLife::Leave(botAI);
        data.toAuctioneer = false;
        data.npcOrGo = ObjectGuid();
        data.lastReach = 0;
    }
    else
    {
        if (MoveWorldObjectTo(data.npcOrGo))
        {
            data.refusals = 0;
            return true;
        }
        // CoA Live City Life (3.1.6): a station no walkable path leads to (inside a building, off the mesh) is given
        // up for another stop, never reached in a straight line through the walls.
        // 3.2: in a capital, any NPC whose way the city guard refuses (a dead end, a straight line) is given up too,
        // for another one, instead of tries and small random steps for the whole wander.
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId());
        bool const inCapital = zone && (zone->flags & AREA_FLAG_CAPITAL);
        if (!IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL) &&
            (CoaCityLife::Holds(botAI, data.npcOrGo) || inCapital))
        {
            // 3.2: given up only when the NPC is gone, no walkable stop by it exists, or the moves keep being refused
            // (a move to the same stop within 5 s, a bot that cannot move...): then it waits for the next tick.
            bool wayExists = false;
            if (object)
            {
                float sx = object->GetPositionX(), sy = object->GetPositionY(), sz = object->GetPositionZ();
                wayExists = CoaSafeMove::StandByNpc(bot, object, sx, sy, sz);
            }
            CoaSafeMove::GiveUp const why = CoaSafeMove::GivesUpNpc(!object, wayExists, ++data.refusals);
            if (why == CoaSafeMove::GiveUp::No)
                return false;
            // 3.2.2: a bot on a closed island of the navmesh gets off it (a short walk, a teleport no player sees),
            // then tries again; otherwise the visit is given up below.
            if (why == CoaSafeMove::GiveUp::NoWay && object)
            {
                CoaSafeMove::Unstick const unstick = CoaSafeMove::UnstickFromIsland(bot, object);
                CoaEconomy::BotAuctions::NoteUnstick(int(unstick));
                if (unstick == CoaSafeMove::Unstick::Walk || unstick == CoaSafeMove::Unstick::Teleport)
                {
                    data.refusals = 0;
                    return true;
                }
            }
            // 3.2 (Stormwind, Chilton): no way to this auctioneer, another of the same house it can walk to instead.
            if (why == CoaSafeMove::GiveUp::NoWay && data.toAuctioneer)
            {
                ObjectGuid const other = CoaSafeMove::OtherReachableAuctioneer(bot, object);
                if (!other.IsEmpty())
                {
                    data.npcOrGo = other;
                    data.refusals = 0;
                    data.pickedAt = getMSTime();
                    return true;
                }
            }
            // 3.2 (BuyLog): counted from the pick's flag, the NPC may be gone (out of the grid).
            if (data.toAuctioneer)
            {
                CoaEconomy::BotAuctions::NoteWay(CoaEconomy::BotAuctions::Way::GivenUp);
                CoaEconomy::BotAuctions::NoteGiveUp(int(why), zone ? zone->area_name[0] : "");
                if (why == CoaSafeMove::GiveUp::NoWay)
                {
                    float gap = 0.0f;
                    char const* reason = CoaSafeMove::NoWayWhy(bot, object, gap);
                    CoaEconomy::BotAuctions::NoteNoWay(zone ? zone->area_name[0] : "", reason, object->GetEntry(),
                                                       bot->GetPositionX(),
                                                       bot->GetPositionY(), bot->GetPositionZ(), gap);
                }
            }
            data.refusals = 0;
            data.toAuctioneer = false;
            CoaCityLife::Leave(botAI);
            // 3.2 (".squidbots why": a bot still for minutes by the Stormwind bank, WANDER_NPC with no destination):
            // it picked and gave up NPCs every tick. After a few in a row, the wander ends and it does something else.
            if (++data.givenUp >= CoaSafeMove::WANDER_GIVE_UPS)
            {
                info.ChangeToIdle();
                return true;
            }
            data.npcOrGo = ObjectGuid();
            data.lastReach = 0;
            return true;
        }
        // NPC pathing failed (random offset in a wall, mmap hiccup, etc).
        // Take a small random step so the next tick retries from a
        // different spot instead of staring at the NPC from afar.
        return MoveRandomNear(15.0f);
    }

    return true;
}

bool NewRpgDoQuestAction::Execute(Event /*event*/)
{
    if (SearchQuestGiverAndAcceptOrReward())
        return true;

    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::DoQuest>(&info.data);
    if (!dataPtr)
        return false;
    auto& data = *dataPtr;
    uint32 questId = data.questId;
    uint8 questStatus = bot->GetQuestStatus(questId);
    switch (questStatus)
    {
        case QUEST_STATUS_INCOMPLETE:
            return DoIncompleteQuest(data);
        case QUEST_STATUS_COMPLETE:
            return DoCompletedQuest(data);
        default:
            break;
    }
    info.ChangeToIdle();
    return true;
}

bool NewRpgDoQuestAction::DoIncompleteQuest(NewRpgInfo::DoQuest& data)
{
    uint32 questId = data.questId;
    if (data.pos != WorldPosition())
    {
        /// @TODO: extract to a new function
        int32 currentObjective = data.objectiveIdx;
        // check if the objective has completed
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        QuestStatusData const* statusData = GetQuestStatusData(questId);
        if (!quest || !statusData)
        {
            // Template gone from the DB or quest no longer in the log.
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        QuestStatusData const& q_status = *statusData;
        bool completed = true;
        if (currentObjective < QUEST_OBJECTIVES_COUNT)
        {
            if (q_status.CreatureOrGOCount[currentObjective] < quest->RequiredNpcOrGoCount[currentObjective])
                completed = false;
        }
        else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        {
            if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] <
                quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
                completed = false;
        }
        // the current objective is completed, clear and find a new objective later
        if (completed)
        {
            data.lastReachPOI = 0;
            data.pos = WorldPosition();
            data.objectiveIdx = 0;
        }
    }
    if (data.pos == WorldPosition())
    {
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo))
        {
            // can't find a poi pos to go, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        uint32 rndIdx = urand(0, poiInfo.size() - 1);
        G3D::Vector2 nearestPoi = poiInfo[rndIdx].pos;
        int32 objectiveIdx = poiInfo[rndIdx].objectiveIdx;

        float dx = nearestPoi.x, dy = nearestPoi.y;

        // z = MAX_HEIGHT as we do not know accurate z
        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

        // double check for GetQuestPOIPosAndObjectiveIdx
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            return false;

        WorldPosition pos(bot->GetMapId(), dx, dy, dz);
        data.lastReachPOI = 0;
        data.pos = pos;
        data.objectiveIdx = objectiveIdx;
    }

    if (bot->GetDistance(data.pos) > 10.0f && !data.lastReachPOI)
    {
        if (MoveFarTo(data.pos))
            return true;
        // Long-range sampler couldn't land a candidate — nudge the
        // bot a short distance so the next tick retries from a
        // different position instead of sitting idle.
        return MoveRandomNear(10.0f);
    }
    // Now we are near the quest objective
    // kill mobs and looting quest should be done automatically by grind strategy

    if (!data.lastReachPOI)
    {
        data.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(data.lastReachPOI) >= poiStayTime)
    {
        bool hasProgression = false;
        int32 currentObjective = data.objectiveIdx;
        // check if the objective has progression
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        QuestStatusData const* statusData = GetQuestStatusData(questId);
        if (!quest || !statusData)
        {
            // Template gone from the DB or quest no longer in the log.
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        QuestStatusData const& q_status = *statusData;
        if (currentObjective < QUEST_OBJECTIVES_COUNT)
        {
            if (q_status.CreatureOrGOCount[currentObjective] != 0 && quest->RequiredNpcOrGoCount[currentObjective])
                hasProgression = true;
        }
        else if (currentObjective < QUEST_OBJECTIVES_COUNT + QUEST_ITEM_OBJECTIVES_COUNT)
        {
            if (q_status.ItemCount[currentObjective - QUEST_OBJECTIVES_COUNT] != 0 &&
                quest->RequiredItemCount[currentObjective - QUEST_OBJECTIVES_COUNT])
                hasProgression = true;
        }
        if (!hasProgression)
        {
            // we has reach the poi for more than 5 mins but no progession
            // may not be able to complete this quest, marked as abandoned
            /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
            botAI->lowPriorityQuest.insert(questId);
            botAI->rpgStatistic.questAbandoned++;
            LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        // clear and select another poi later
        data.lastReachPOI = 0;
        data.pos = WorldPosition();
        data.objectiveIdx = 0;
        return true;
    }

    // At the POI: keep the bot actively placed but avoid large
    // random 20yd hops that look like pacing back and forth. A small
    // ~8yd wander reads as the bot looking around while grind/loot
    // strategies do their work.
    return MoveRandomNear(8.0f);
}

bool NewRpgDoQuestAction::DoCompletedQuest(NewRpgInfo::DoQuest& data)
{
    uint32 questId = data.questId;
    Quest const* quest = data.quest;

    if (data.objectiveIdx != -1)
    {
        // if quest is completed, back to poi with -1 idx to reward
        BroadcastHelper::BroadcastQuestUpdateComplete(botAI, bot, quest);
        botAI->rpgStatistic.questCompleted++;
        std::vector<POIInfo> poiInfo;
        if (!GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true))
        {
            // can't find a poi pos to reward, stop doing quest for now
            botAI->rpgInfo.ChangeToIdle();
            return false;
        }
        if (poiInfo.empty())
        {
            LOG_DEBUG("playerbots", "[New RPG] {} has no reward POI for quest {}, back to idle", bot->GetName(),
                      questId);
            botAI->rpgInfo.ChangeToIdle();
            return false;
        }
        // now we get the place to get rewarded
        float dx = poiInfo[0].pos.x, dy = poiInfo[0].pos.y;
        // z = MAX_HEIGHT as we do not know accurate z
        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

        // double check for GetQuestPOIPosAndObjectiveIdx
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            return false;

        WorldPosition pos(bot->GetMapId(), dx, dy, dz);
        data.lastReachPOI = 0;
        data.pos = pos;
        data.objectiveIdx = -1;
    }

    if (data.pos == WorldPosition())
        return false;

    if (bot->GetDistance(data.pos) > 10.0f && !data.lastReachPOI)
    {
        if (MoveFarTo(data.pos))
            return true;
        return MoveRandomNear(10.0f);
    }

    // Now we are near the qoi of reward
    // the quest should be rewarded by SearchQuestGiverAndAcceptOrReward
    if (!data.lastReachPOI)
    {
        data.lastReachPOI = getMSTime();
        return true;
    }
    // stayed at this POI for more than 5 minutes
    if (GetMSTimeDiffToNow(data.lastReachPOI) >= poiStayTime)
    {
        // e.g. Can not reward quest to gameobjects
        /// @TODO: It may be better to make lowPriorityQuest a global set shared by all bots (or saved in db)
        botAI->lowPriorityQuest.insert(questId);
        botAI->rpgStatistic.questAbandoned++;
        LOG_DEBUG("playerbots", "[New RPG] {} marked as abandoned quest {}", bot->GetName(), questId);
        botAI->rpgInfo.ChangeToIdle();
        return true;
    }
    return false;
}

bool NewRpgTravelFlightAction::Execute(Event /*event*/)
{
    NewRpgInfo& info = botAI->rpgInfo;
    auto* dataPtr = std::get_if<NewRpgInfo::TravelFlight>(&info.data);
    if (!dataPtr)
        return false;

    auto& data = *dataPtr;
    if (bot->IsInFlight())
    {
        data.inFlight = true;
        ContinueCrossMapTaxi();
        return false;
    }

    if (bot->GetDistance(data.flightMasterPos) > INTERACTION_DISTANCE)
        return MoveFarTo(data.flightMasterPos);

    Creature* flightMaster = bot->FindNearestCreature(data.flightMasterEntry, INTERACTION_DISTANCE * 3);
    if (!flightMaster || !flightMaster->IsAlive())
    {
        info.ChangeToIdle();
        return true;
    }
    if (bot->GetDistance(flightMaster) > INTERACTION_DISTANCE)
        return MoveFarTo(flightMaster);

    std::vector<uint32> nodes = data.path;

    botAI->RemoveShapeshift();
    if (bot->IsMounted())
        bot->Dismount();

    bot->GetSession()->SendLearnNewTaxiNode(flightMaster);

    if (!bot->ActivateTaxiPathTo(nodes, flightMaster, 0))
    {
        LOG_DEBUG("playerbots", "[New RPG] {} active taxi path {} (from {} to {}) failed", bot->GetName(),
                  flightMaster->GetEntry(), nodes.empty() ? 0 : nodes.front(), nodes.empty() ? 0 : nodes.back());
        info.ChangeToIdle();
        return true;
    }
    return true;
}

void NewRpgTravelFlightAction::ContinueCrossMapTaxi()
{
    if (bot->IsBeingTeleported())
        return;

    if (!bot->movespline->Finalized())
        return;

    MotionMaster* mm = bot->GetMotionMaster();
    if (!mm || mm->GetCurrentMovementGeneratorType() != FLIGHT_MOTION_TYPE)
        return;

    // Check if we are at our destination.
    uint32 nextDest = bot->m_taxi.GetTaxiDestination();
    if (!nextDest)
        return;

    // Confirm next node needs different map.
    TaxiNodesEntry const* nextNode = sTaxiNodesStore.LookupEntry(nextDest);
    if (!nextNode || nextNode->map_id == bot->GetMapId())
        return;

    FlightPathMovementGenerator* flight = dynamic_cast<FlightPathMovementGenerator*>(mm->top());
    if (!flight)
        return;

    LOG_DEBUG("playerbots", "[New RPG] {} continuing taxi across map boundary (next node {} on map {})",
              bot->GetName(), nextDest, nextNode->map_id);

    flight->SetCurrentNodeAfterTeleport();

    if (flight->HasArrived())
        return;

    TaxiPathNodeEntry const* node = flight->GetPath()[flight->GetCurrentNode()];
    flight->SkipCurrentNode();

    bot->TeleportTo(nextNode->map_id, node->x, node->y, node->z, bot->GetOrientation(), TELE_TO_NOT_LEAVE_TAXI);
}
