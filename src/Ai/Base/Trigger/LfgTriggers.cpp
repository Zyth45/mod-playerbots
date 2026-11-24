/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LfgTriggers.h"

#include "CoaLfgRules.h"
#include "LFGMgr.h"
#include "LfgActions.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

bool LfgProposalActiveTrigger::IsActive() { return AI_VALUE(uint32, "lfg proposal"); }

bool LfgRoleWantedTrigger::IsActive()
{
    if (!sPlayerbotAIConfig.coaLfgRolePriority || !sPlayerbotAIConfig.randomBotJoinLfg || bot->GetGroup())
        return false;
    TeamId const team = bot->GetTeamId();
    if (team > TEAM_HORDE)
        return false;
    bool const needTank = RandomPlayerbotMgr::instance().LfgNeedTank[team];
    bool const needHeal = RandomPlayerbotMgr::instance().LfgNeedHeal[team];
    if ((!needTank && !needHeal) || !RandomPlayerbotMgr::instance().IsRandomBot(bot) ||
        sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
        return false;
    uint32 const roles = LfgJoinAction(botAI).GetRoles();
    return CoaLfgRoleWanted(roles & lfg::PLAYER_ROLE_TANK, roles & lfg::PLAYER_ROLE_HEALER, needTank, needHeal);
}

bool UnknownDungeonTrigger::IsActive()
{
    return IsRealPlayer(botAI->GetMaster()) && botAI->GetMaster() && botAI->GetMaster()->IsInWorld() &&
           botAI->GetMaster()->GetMap()->IsDungeon() && bot->GetMapId() == botAI->GetMaster()->GetMapId();
}
