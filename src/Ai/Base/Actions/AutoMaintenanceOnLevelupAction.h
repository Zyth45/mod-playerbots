/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTOMAINTENANCEONLEVELUPACTION_H
#define PLAYERBOTS_AUTOMAINTENANCEONLEVELUPACTION_H

#include "Action.h"

class PlayerbotAI;

class AutoMaintenanceOnLevelupAction : public Action
{
public:
    AutoMaintenanceOnLevelupAction(PlayerbotAI* botAI, std::string const name = "auto maintenance on levelup")
        : Action(botAI, name)
    {
    }

    bool Execute(Event event);

    // AiPlayerbot.CoaLive.MaintenanceSpread: the steps after the talents, one a second on the bot's next updates
    // (spells, teleport for the level, consumables, equipment), in the same order. Bot update (map thread).
    static void Tick(PlayerbotAI* botAI);

protected:
    void RunStep(uint32 step);
    void AutoTeleportForLevel();
    void AutoPickTalents();
    void AutoLearnSpell();
    void AutoUpgradeEquip();
    void AutoUpgradeEquipConsumables();
    void AutoUpgradeEquipGear();
    void LearnSpells(std::ostringstream* out);
    void LearnTrainerSpells(std::ostringstream* out);
    void LearnQuestSpells(std::ostringstream* out);
    std::string const FormatSpell(SpellInfo const* sInfo);
};

#endif
