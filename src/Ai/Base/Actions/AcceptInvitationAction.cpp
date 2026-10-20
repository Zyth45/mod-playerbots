/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptInvitationAction.h"
#include "CoaSpecialization.h"
#include "Event.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "WorldPacket.h"

bool AcceptInvitationAction::Execute(Event /*event*/)
{
    Group* grp = bot->GetGroupInvite();
    if (!grp)
        return false;
    Player* inviter = ObjectAccessor::FindPlayer(grp->GetLeaderGUID());
    if (!inviter)
        return false;

    // Offered to this player by "lfg bot": it said yes already, whatever its own invitation rules.
    bool const offered = CoaLfgTakeOffer(bot, inviter);

    // Offered to someone else: it waits for that player's invitation.
    if (!offered && (CoaLfgOfferedToOther(bot, inviter) ||
                     !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter)))
    {
        WorldPacket data(SMSG_GROUP_DECLINE, 10);
        data << bot->GetName();
        inviter->SendDirectMessage(&data);
        bot->UninviteFromGroup();
        return false;
    }

    if (bot->isAFK() && !IsSelfBot(bot))
        bot->ToggleAFK();

    WorldPacket p;
    uint32 roles_mask = 0;
    p << roles_mask;
    bot->GetSession()->HandleGroupAcceptOpcode(p);

    if (!bot->GetGroup() || !bot->GetGroup()->IsMember(inviter->GetGUID()))
        return false;

    // A random bot queued for a battleground by itself kept its new group out of the Dungeon Finder
    // (PTR, 28/09): with a player, it leaves the queue.
    if (IsRealPlayer(inviter))
    {
        CoaLeaveBattlegroundQueues(bot);
        CoaDropForeignInstanceBinds(bot, inviter);
    }

    if (sRandomPlayerbotMgr.IsRandomBot(bot))
        botAI->SetMaster(inviter);
    // else
    // PlayerbotRepository::instance().Save(botAI);

    botAI->ResetStrategies();
    botAI->ChangeStrategy("+follow,-lfg,-bg", BOT_STATE_NON_COMBAT);
    botAI->Reset();

    botAI->TellMasterGreeting(PlayerbotTextMgr::instance().GetBotTextOrDefault("hello", "Hello", {}));

    // An offered bot may be anywhere in the world: it comes to the player, into its dungeon if need be.
    if (offered && (bot->GetMap() != inviter->GetMap() || bot->GetDistance(inviter) > 10.0f))
    {
        bot->TeleportTo(inviter->GetMapId(), inviter->GetPositionX(), inviter->GetPositionY(), inviter->GetPositionZ(),
                        inviter->GetOrientation(), 0, nullptr, CoaInOtherInstance(bot, inviter));
    }
    else if (sPlayerbotAIConfig.summonWhenGroup && bot->GetDistance(inviter) > sPlayerbotAIConfig.sightDistance)
    {
        Teleport(inviter, bot, true);
    }
    return true;
}
