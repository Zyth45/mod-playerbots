/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ResetInstancesAction.h"
#include "InstancePackets.h"
#include "PlayerbotAI.h"

bool ResetInstancesAction::Execute(Event /*event*/)
{
    // A world thread opcode (PROCESS_THREADUNSAFE), queued for the world thread instead of run from the bot's map thread (Map::SendObjectUpdates crash, 06/11).
    bot->GetSession()->QueuePacket(new WorldPacket(CMSG_RESET_INSTANCES, 0));

    return true;
}

bool ResetInstancesAction::isUseful() { return botAI->GetGroupLeader() == bot; };
