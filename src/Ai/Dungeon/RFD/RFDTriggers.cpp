/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RFDTriggers.h"

#include "AiObjectContext.h"
#include "Group.h"
#include "Playerbots.h"

using namespace DungeonBoss;

namespace RazorfenDowns
{
    Unit* GluttonExpelling(PlayerbotAI* botAI, AiObjectContext* context)
    {
        Unit* glutton = AI_VALUE2(Unit*, "find target", "glutton");
        return glutton && glutton->IsAlive() && IsCastingNamed(glutton, "Expel Bloat") ? glutton : nullptr;
    }

    Unit* MordreshStorming(PlayerbotAI* botAI, AiObjectContext* context)
    {
        Unit* mordresh = AI_VALUE2(Unit*, "find target", "mordresh fire eye");
        return mordresh && mordresh->IsAlive() && HasAuraNamed(mordresh, "Bone Storm") ? mordresh : nullptr;
    }

    Player* MarkedNear(Player* bot, float& radius)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || member->GetMapId() != bot->GetMapId())
                continue;

            for (Mark const& mark : AMNENNAR_MARKS)
            {
                if (HasAuraNamed(member, mark.aura) && Within(bot, member, mark.radius))
                {
                    radius = mark.radius;
                    return member;
                }
            }
        }
        return nullptr;
    }
}

using namespace RazorfenDowns;

bool RfdGluttonExpelBloatTrigger::IsActive()
{
    Unit* glutton = GluttonExpelling(botAI, context);
    return glutton && Within(bot, glutton, EXPEL_BLOAT_RADIUS);
}

bool RfdMordreshBoneStormTrigger::IsActive()
{
    Unit* mordresh = MordreshStorming(botAI, context);
    return mordresh && Within(bot, mordresh, BONE_STORM_RADIUS);
}

bool RfdAmnennarMarkTrigger::IsActive()
{
    if (!AI_VALUE2(Unit*, "find target", "amnennar the coldbringer"))
        return false;

    float radius = 0.0f;
    return MarkedNear(bot, radius) != nullptr;
}
