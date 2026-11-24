/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GuildValues.h"
#include "Playerbots.h"

uint8 PetitionSignsValue::Calculate()
{
    if (bot->GetGuildId())
        return 0;

    std::vector<Item*> petitions = AI_VALUE2(std::vector<Item*>, "inventory items", chat->FormatQItem(5863));
    if (petitions.empty())
        return 0;

    // CoA 3.2: signatures are keyed by petition_id (the core leaves petition_sign.petitionguid at 0).
    QueryResult result = CharacterDatabase.Query(
        "SELECT ps.playerguid FROM petition_sign ps JOIN petition p ON p.petition_id = ps.petition_id "
        "WHERE p.petitionguid = {}",
        petitions.front()->GetGUID().GetCounter());
    return result ? (uint8)result->GetRowCount() : 0;
}
