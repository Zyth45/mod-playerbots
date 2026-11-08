/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaRpgTrade.h"

#include "Creature.h"
#include "Event.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"

namespace CoaEconomy::RpgTrade
{

namespace
{
    Creature* AsVendor(WorldObject* object, uint32 npcFlags)
    {
        Creature* creature = object ? object->ToCreature() : nullptr;
        return creature && creature->IsInWorld() && creature->HasNpcFlag(NPCFlags(npcFlags)) ? creature : nullptr;
    }

    bool CanSell(PlayerbotAI* botAI)
    {
        return botAI->GetAiObjectContext()->GetValue<bool>("can sell")->Get();
    }
}

bool WantsVendor(PlayerbotAI* botAI)
{
    if (!sPlayerbotAIConfig.coaEconomyRpgSellEnable || !botAI || !botAI->GetBot())
        return false;
    uint8 const bagUsed = botAI->GetAiObjectContext()->GetValue<uint8>("bag space")->Get();
    return bagUsed >= sPlayerbotAIConfig.coaEconomyRpgSellBagPercent && CanSell(botAI);
}

ObjectGuid ChooseVendor(PlayerbotAI* botAI, std::vector<ObjectGuid> const& targets, float distanceLimit)
{
    Player* bot = botAI->GetBot();
    Creature* nearest = nullptr;
    for (ObjectGuid const& guid : targets)
    {
        Creature* vendor = AsVendor(ObjectAccessor::GetWorldObject(*bot, guid), UNIT_NPC_FLAG_VENDOR);
        if (!vendor || !vendor->IsAlive())
            continue;
        if (distanceLimit > 0.0f && bot->GetDistance(vendor) > distanceLimit)
            continue;
        if (!nearest || bot->GetExactDist(vendor) < bot->GetExactDist(nearest))
            nearest = vendor;
    }
    return nearest ? nearest->GetGUID() : ObjectGuid();
}

bool TradeAtNpc(PlayerbotAI* botAI, WorldObject* npc)
{
    if (!sPlayerbotAIConfig.coaEconomyRpgSellEnable || !botAI || !npc)
        return false;

    AiObjectContext* context = botAI->GetAiObjectContext();
    bool traded = false;

    // What the bot has no use for: grey loot and the gear and goods its item usage marks for sale.
    if (AsVendor(npc, UNIT_NPC_FLAG_VENDOR) && CanSell(botAI))
        traded |= botAI->DoSpecificAction("sell", Event("rpg action", "vendor"), true);

    if (AsVendor(npc, UNIT_NPC_FLAG_REPAIR) && AI_VALUE(bool, "should repair") && AI_VALUE(bool, "can repair"))
        traded |= botAI->DoSpecificAction("repair", Event("rpg action"), true);

    return traded;
}

} // namespace CoaEconomy::RpgTrade
