/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ArenaTeamActions.h"
#include "ArenaTeamMgr.h"
#include "RandomPlayerbotFactory.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"

bool ArenaTeamAcceptAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);
    Player* inviter = nullptr;
    std::string Invitedname;
    p >> Invitedname;

    if (normalizePlayerName(Invitedname))
        inviter = ObjectAccessor::FindPlayerByName(Invitedname.c_str());

    if (!inviter)
        return false;

    ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(bot->GetArenaTeamIdInvited());
    if (!at)
        return false;

    bool accept = true;

    // Invited by its master to play rated arenas together: a bot gives up the team it holds in that slot when that
    // team is only its own solo queue team or a random bot team it does not lead.
    if (uint32 heldId = bot->GetArenaTeamId(at->GetSlot()))
    {
        ArenaTeam* held = sArenaTeamMgr->GetArenaTeamById(heldId);
        if (held && inviter == botAI->GetMaster())
        {
            if (held->GetMembersSize() <= 1)
                held->Disband(nullptr);
            else if (RandomPlayerbotFactory::IsBotArenaTeam(held) && held->GetCaptain() != bot->GetGUID())
                held->DelMember(bot->GetGUID(), true);
        }
    }

    if (bot->GetArenaTeamId(at->GetSlot()))
    {
        // bot is already in an arena team
        std::string text = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "arena_team_already_in_team", "Sorry, I am already in such team", {});
        bot->Say(text, LANG_UNIVERSAL);
        accept = false;
    }

    if (accept)
    {
        WorldPacket data(CMSG_ARENA_TEAM_ACCEPT);
        bot->GetSession()->HandleArenaTeamAcceptOpcode(data);
        std::string text = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "arena_team_thanks_for_invite", "Thanks for the invite!", {});
        bot->Say(text, LANG_UNIVERSAL);
        LOG_INFO("playerbots", "Bot {} <{}> accepts Arena Team invite", bot->GetGUID().ToString().c_str(),
                 bot->GetName().c_str());
        return true;
    }
    else
    {
        WorldPacket data(CMSG_ARENA_TEAM_DECLINE);
        bot->GetSession()->HandleArenaTeamDeclineOpcode(data);
        LOG_INFO("playerbots", "Bot {} <{}> declines Arena Team invite", bot->GetGUID().ToString().c_str(),
                 bot->GetName().c_str());
        return false;
    }

    return false;
}
