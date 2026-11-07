/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AcceptDuelAction.h"
#include "Event.h"
#include "Playerbots.h"

bool AcceptDuelAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());

    ObjectGuid flagGuid;
    p >> flagGuid;
    ObjectGuid playerGuid;
    p >> playerGuid;

    // Do not auto duel with low health
    if ((!botAI->HasGameClientMaster() ||
        (botAI->GetMaster() && botAI->GetMaster()->GetGUID() != playerGuid)) &&
        AI_VALUE2(uint8, "health", "self target") < 90)
    {
        WorldPacket* packet = new WorldPacket(CMSG_DUEL_CANCELLED, 8);
        *packet << flagGuid;
        bot->GetSession()->QueuePacket(packet);
        return false;
    }

    // CMSG_DUEL_ACCEPTED and CMSG_DUEL_CANCELLED are world thread opcodes (PROCESS_THREADUNSAFE) that change the
    // opponent too: queued for the world thread (Map::SendObjectUpdates crash, 06/11).
    WorldPacket* packet = new WorldPacket(CMSG_DUEL_ACCEPTED, 8);
    *packet << flagGuid;
    bot->GetSession()->QueuePacket(packet);

    botAI->ResetStrategies();
    return true;
}
