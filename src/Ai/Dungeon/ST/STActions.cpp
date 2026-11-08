/*
 * Copyright (C) 2026+ Jared Wright and contributors
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero
 * General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version. This program is distributed WITHOUT ANY WARRANTY; see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "STActions.h"

#include "Creature.h"
#include "GameObject.h"
#include "Log.h"
#include "LootMgr.h"
#include "Player.h"
#include "Playerbots.h"
#include "STTriggers.h"

using namespace SunkenTemple;

bool StHakkarSuppressorAction::Execute(Event /*event*/)
{
    Creature* suppressor = ChannellingSuppressor(bot);
    if (!suppressor)
        return false;
    return Attack(suppressor);
}

bool StHakkarFlameAction::Execute(Event /*event*/)
{
    if (!bot->HasItemCount(ITEM_HAKKARI_BLOOD, 1))
        return false;
    GameObject* flame = FlameToDouse(bot);
    if (!flame)
        return false;
    if (!bot->IsWithinDistInMap(flame, FLAME_USE_RANGE))
        return MoveTo(bot->GetMapId(), flame->GetPositionX(), flame->GetPositionY(), flame->GetPositionZ(), false,
                      false, false, false, MovementPriority::MOVEMENT_NORMAL);

    // Using the flame fires its gossip event, the +1 on the Shade's douse counter. The blood is spent by hand: nothing
    // in the core takes the lock's key item on use, and the blood's own spell (a 1 s interruptible cast) does not
    // bump the counter, so one blood would otherwise douse several flames.
    bot->SetFacingToObject(flame);
    flame->Use(bot);
    bot->DestroyItemCount(ITEM_HAKKARI_BLOOD, 1, true);
    MarkDoused(bot, flame);
    LOG_DEBUG("playerbots", "[st:{}] Avatar of Hakkar: doused Eternal Flame {}", bot->GetName(),
              flame->GetGUID().ToString());
    return true;
}

bool StHakkarBloodAction::Execute(Event /*event*/)
{
    Creature* keeper = BloodkeeperToHarvest(bot);
    if (!keeper)
        return false;
    if (!bot->IsWithinDistInMap(keeper, LOOT_RANGE))
        return MoveTo(bot->GetMapId(), keeper->GetPositionX(), keeper->GetPositionY(), keeper->GetPositionZ(), false,
                      false, false, false, MovementPriority::MOVEMENT_NORMAL);

    // The real loot: Hakkari Blood is a 100 % drop of the Bloodkeeper, so opening the corpse's own loot and taking that
    // slot is the genuine item, not a granted copy.
    Loot& loot = keeper->loot;
    if (loot.items.empty() && loot.gold == 0)
        loot.FillLoot(keeper->GetCreatureTemplate()->lootid, LootTemplates_Creature, bot, true, true);

    bool got = false;
    bool sawBlood = false;
    for (uint8 slot = 0; slot < loot.items.size(); ++slot)
    {
        LootItem const& item = loot.items[slot];
        if (item.itemid != ITEM_HAKKARI_BLOOD || item.is_looted)
            continue;
        sawBlood = true;
        InventoryResult msg = EQUIP_ERR_OK;
        bot->StoreLootItem(slot, &loot, msg);
        got = msg == EQUIP_ERR_OK;
        break;
    }

    // Bags full for a moment: leave the corpse unmarked so this bot or another tries again. A corpse with no blood
    // left (a player took it) is marked, so the scan does not spin on it.
    if (!got && sawBlood)
        return false;
    MarkHarvested(bot, keeper);
    return got;
}
