/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PetitionSignAction.h"
#include "CoaWorldCharters.h"
#include "ArenaTeam.h"
#include "CoaLive.h"
#include "Event.h"
#include "Playerbots.h"

bool PetitionSignAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);
    ObjectGuid petitionGuid;
    ObjectGuid inviter;
    uint8 unk = 0;
    bool isArena = false;
    p >> petitionGuid >> inviter;

    QueryResult result =
        CharacterDatabase.Query("SELECT `type` FROM `petition` WHERE `petitionguid` = {}", petitionGuid.GetCounter());
    if (!result)
    {
        return false;
    }

    Field* fields = result->Fetch();
    uint32 type = fields[0].Get<uint32>();

    bool accept = true;

    if (type != 9)
    {
        isArena = true;
        uint8 slot = ArenaTeam::GetSlotByType(ArenaType(type));
        if (bot->GetArenaTeamId(slot))
        {
            // player is already in an arena team
            botAI->TellError("Sorry, I am already in such team");
            accept = false;
        }
    }
    else
    {
        if (bot->GetGuildId())
        {
            botAI->TellError("Sorry, I am in a guild already");
            accept = false;
        }

        if (bot->GetGuildIdInvited())
        {
            botAI->TellError("Sorry, I am invited to a guild already");
            accept = false;
        }

        // check for same acc id
        /*if (QueryResult* result = CharacterDatabase.Query("SELECT playerguid FROM petition_sign WHERE player_account =
        {} AND petitionguid = {}'", bot->GetSession()->GetAccountId(), petitionGuid.GetCounter()))
        {
            botAI->TellError("Sorry, I already signed this pettition");
            accept = false;
        }
        */
    }

    Player* _inviter = ObjectAccessor::FindPlayer(inviter);
    if (!_inviter)
        return false;

    if (_inviter == bot)
        return false;

    // CoA World (Guilds.Charters): a real player's guild charter offered to a random bot of its own without a guild
    // is signed after a few seconds (or refused now and then). Only this request skips the security check below.
    if (!isArena && accept && CoaWorldCharterOffered(bot, petitionGuid, _inviter, true))
        return true;

    if (!accept || !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, _inviter, true))
    {
        WorldPacket data(MSG_PETITION_DECLINE);
        data << petitionGuid;
        bot->GetSession()->HandlePetitionDeclineOpcode(data);
        LOG_INFO("playerbots", "Bot {} <{}> declines {} invite", bot->GetGUID().ToString().c_str(),
                 bot->GetName().c_str(), isArena ? "Arena" : "Guild");
        return false;
    }

    if (accept)
    {
        WorldPacket data(CMSG_PETITION_SIGN, 20);
        data << petitionGuid << unk;
        bot->GetSession()->HandlePetitionSignOpcode(data);
        // CoA Live bank first ("charter_signed" said aloud when the holder stands near, else "charter_signed_w" whispered
        // to the holder; chance and delay in the bank's rules), for a guild charter. The older line only where the
        // older chat speaks.
        std::unordered_map<std::string, std::string> const slots{{"player", _inviter->GetName()}};
        bool const said = !isArena && (bot->IsWithinDistInMap(_inviter, 20.0f)
                                           ? CoaLive::SayLine(bot, "charter_signed", slots)
                                           : CoaLive::SayLine(bot, "charter_signed_w", slots, _inviter->GetGUID()));
        if (!said && CoaLive::LegacyChatOn())
            bot->Say("Thanks for the invite!", LANG_UNIVERSAL);
        LOG_INFO("playerbots", "Bot {} <{}> accepts {} invite", bot->GetGUID().ToString().c_str(),
                 bot->GetName().c_str(), isArena ? "Arena" : "Guild");
        return true;
    }

    return false;
}
