/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_EQUIPACTION_H
#define PLAYERBOTS_EQUIPACTION_H

#include "ChatHelper.h"
#include "InventoryAction.h"
#include "Item.h"

#include <unordered_map>

class FindItemVisitor;
class Item;
class PlayerbotAI;

class EquipAction : public InventoryAction
{
public:
    EquipAction(PlayerbotAI* botAI, std::string const name = "equip") : InventoryAction(botAI, name) {}

    bool Execute(Event event) override;
    void EquipItems(ItemIds ids);
    ItemIds SelectInventoryItemsToEquip();

protected:
    // Automatic upgrades (loot, trade, "equip upgrade") say nothing unless AiPlayerbot.CoaTellEquipUpgrades is on;
    // an item the player asks for ("e <item>") is always answered.
    bool silent = false;
    // 3.3.1: the copy of each item id that scored as an upgrade (SelectInventoryItemsToEquip), put on by EquipItems.
    std::unordered_map<uint32, ObjectGuid> coaChosen;

private:
    void EquipItem(FindItemVisitor* visitor);
    uint8 GetSmallestBagSlot();
    void EquipItem(Item* item);
};

class EquipUpgradesPacketAction : public EquipAction
{
public:
    explicit EquipUpgradesPacketAction(PlayerbotAI* botAI, std::string const name = "equip upgrades packet action") : EquipAction(botAI, name) {}

    bool Execute(Event event) override;
};

class EquipUpgradeAction : public EquipAction
{
public:
    explicit EquipUpgradeAction(PlayerbotAI* botAI, std::string const name = "equip upgrade") : EquipAction(botAI, name) {}

    bool Execute(Event event) override;
};

// A self-found bot sorts its bags every two minutes: what it carries may have become its best piece (a level up, a
// lighter armor now allowed), and nothing else makes it look again until it loots the next piece.
class SelfFoundEquipAction : public EquipUpgradeAction
{
public:
    explicit SelfFoundEquipAction(PlayerbotAI* botAI) : EquipUpgradeAction(botAI, "self found equip") {}

    bool isUseful() override;

private:
    time_t nextCheck = 0;
};

#endif
