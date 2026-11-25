/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaStranded.h"

#include "CoaMissionMgr.h"
#include "CoaStrandedRules.h"
#include "Group.h"
#include "LFGMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotOperation.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RandomBotLevelMgr.h"
#include "RandomPlayerbotMgr.h"
#include "Timer.h"

#include <atomic>
#include <ctime>

namespace
{
constexpr uint32 CHECK_MS = 30 * IN_MILLISECONDS;

std::atomic<uint32> releasedGrouped{0};
std::atomic<uint32> releasedFree{0};
std::atomic<int64> hourAt{0};

// Back to the life of a free random bot, as UpdateAIGroupMaster does for a bot out of its group.
void BackToOwnLife(PlayerbotAI* botAI, Player* bot)
{
    botAI->SetMaster(nullptr);
    botAI->Reset(true);
    botAI->ResetStrategies();
    if (bot->IsMounted())
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
    botAI->coaStranded.sinceMs = 0;
}

// The group is left in the world thread (a group spans maps).
class LeaveStrandedGroupOperation : public PlayerbotOperation
{
public:
    explicit LeaveStrandedGroupOperation(ObjectGuid botGuid) : m_botGuid(botGuid) {}

    bool Execute() override
    {
        Player* bot = ObjectAccessor::FindPlayer(m_botGuid);
        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        if (!botAI || !bot->IsInWorld() || bot->IsBeingTeleported() || bot->IsInCombat())
            return false;
        if (bot->GetGroup())
            bot->RemoveFromGroup(GROUP_REMOVEMETHOD_LEAVE);
        if (bot->GetGroup())
            return false;
        BackToOwnLife(botAI, bot);
        ++releasedGrouped;
        return true;
    }

    ObjectGuid GetBotGuid() const override { return m_botGuid; }
    uint32 GetPriority() const override { return 20; }
    std::string GetName() const override { return "CoaStrandedLeaveGroup"; }

private:
    ObjectGuid m_botGuid;
};

// One line an hour, written by the first bot update past the hour.
void HourlyLine()
{
    int64 const now = int64(time(nullptr));
    int64 at = hourAt.load(std::memory_order_relaxed);
    if (!at)
    {
        hourAt.compare_exchange_strong(at, now + 3600);
        return;
    }
    if (now < at || !hourAt.compare_exchange_strong(at, now + 3600))
        return;
    uint32 const grouped = releasedGrouped.exchange(0), free = releasedFree.exchange(0);
    if (grouped || free)
        LOG_INFO("playerbots", "coa stranded: {} bots released in the last hour ({} out of a group of bots, {} free)",
                 grouped + free, grouped, free);
}

bool RealPlayerIn(Group* group, Player* bot)
{
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (member != bot && IsRealPlayer(member))
                return true;
    return false;
}
}  // namespace

void CoaStranded::Tick(PlayerbotAI* botAI)
{
    uint32 const limit = sPlayerbotAIConfig.coaLiveStrandedMinutes;
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!limit || !bot || !bot->IsInWorld())
        return;
    auto& still = botAI->coaStranded;
    uint32 const now = getMSTime();
    if (still.checkMs && getMSTimeDiff(still.checkMs, now) < CHECK_MS)
        return;
    still.checkMs = now ? now : 1;
    HourlyLine();

    float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
    if (!still.sinceMs || still.mapId != bot->GetMapId() ||
        CoaStrandedRules::Moved(x - still.x, y - still.y, z - still.z))
    {
        still.x = x;
        still.y = y;
        still.z = z;
        still.mapId = bot->GetMapId();
        still.sinceMs = now ? now : 1;
        return;
    }

    // Nothing else decides for it: a random bot, alive, out of combat, a battleground, a flight, a test or a mission.
    if (!sRandomPlayerbotMgr.IsRandomBot(bot) || !bot->IsAlive() || bot->IsInCombat() || bot->InBattleground() ||
        bot->InArena() || bot->InBattlegroundQueue() || bot->IsInFlight() || bot->IsBeingTeleported() ||
        RandomBotLevelMgr::IsHeld(bot->GetGUID()) || sCoaMissionMgr.IsOnMission(bot))
        return;
    Group* group = bot->GetGroup();
    if (group && (group->isBGGroup() || group->isBFGroup() ||
                  (group->isLFGGroup() && sLFGMgr->GetState(group->GetGUID()) != lfg::LFG_STATE_NONE)))
        return;
    NewRpgStatus const status = botAI->rpgInfo.GetStatus();
    bool const idleRpg = status == RPG_IDLE || status == RPG_REST;
    if (!CoaStrandedRules::Stranded(getMSTimeDiff(still.sinceMs, now), limit, group != nullptr,
                                    group && RealPlayerIn(group, bot), idleRpg))
        return;

    if (group)
    {
        still.sinceMs = 0;  // once: the operation runs in the world thread
        PlayerbotWorldThreadProcessor::instance().QueueOperation(
            std::make_unique<LeaveStrandedGroupOperation>(bot->GetGUID()));
        return;
    }
    BackToOwnLife(botAI, bot);
    ++releasedFree;
}
