/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RaidBossScripts.h"

#include "CoaPlayerMainTank.h"
#include "Event.h"
#include "Group.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "SharedDefines.h"

bool RaidKillOrderMarkTrigger::IsActive()
{
    return PlayerbotAI::IsMainTank(bot) && AI_VALUE2(Unit*, "find target", bossName);
}

Unit* RaidKillOrderMarkAction::GetTarget()
{
    Unit* boss = AI_VALUE2(Unit*, "find target", bossName);
    if (!boss)
        return nullptr;

    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    ObjectGuid currentSkullGuid = group->GetTargetIcon(RtiTargetValue::skullIndex);
    Unit* currentSkullUnit = currentSkullGuid.IsEmpty() ? nullptr : botAI->GetUnit(currentSkullGuid);

    for (uint32 entry : addEntries)
    {
        Unit* best = nullptr;
        for (auto const& target : AI_VALUE(GuidVector, "possible targets no los"))
        {
            Unit* unit = botAI->GetUnit(target);
            if (!unit || !unit->IsAlive() || unit->GetEntry() != entry)
                continue;

            // The most damaged first, so an in-progress kill finishes.
            if (!best || unit->GetHealth() < best->GetHealth())
                best = unit;
        }

        if (!best)
            continue;

        // Sticky within the active tier so the mark doesn't flap between equally-valid adds.
        if (currentSkullUnit && currentSkullUnit->IsAlive() && currentSkullUnit->GetEntry() == entry)
            return nullptr;

        return best;
    }

    // All add tiers cleared: skull the boss.
    if (currentSkullGuid.IsEmpty() || currentSkullGuid != boss->GetGUID())
        return boss;

    return nullptr;
}

bool RaidKillOrderMarkAction::Execute(Event /*event*/)
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    bot->GetGroup()->SetTargetIcon(RtiTargetValue::skullIndex, bot->GetGUID(), target->GetGUID());
    return true;
}

bool RaidGroundEffectAuraTrigger::IsActive()
{
    // No boss check: fire patches (and their DoTs) outlive target switches.
    return bot->HasAura(spellId);
}

bool RaidMoveFromGroundEffectAction::Execute(Event /*event*/)
{
    // Any direction out of the patch works; FleePosition picks a safe nearby
    // spot away from where the bot is standing.
    return FleePosition(bot->GetPosition(), 8.0f);
}

bool RaidNearCreatureTrigger::IsActive()
{
    return bot->FindNearestCreature(entry, range) != nullptr;
}

bool RaidMoveFromCreatureAction::Execute(Event /*event*/)
{
    Creature* hazard = bot->FindNearestCreature(entry, range);
    if (!hazard)
        return false;

    return MoveAway(hazard, range + 3.0f - bot->GetDistance(hazard));
}

bool RaidStandoffTrigger::IsActive()
{
    if (PlayerbotAI::IsTank(bot) || PlayerbotAI::IsMelee(bot))
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", bossName);
    if (!boss || boss->GetVictim() == bot)
        return false;

    return bot->GetDistance(boss) < minRange;
}

bool RaidStandoffAction::Execute(Event /*event*/)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", bossName);
    if (!boss)
        return false;

    // A little past the line so drift doesn't re-trigger every tick.
    return MoveAway(boss, minRange + 3.0f - bot->GetDistance(boss));
}

bool RaidBackupTauntTrigger::IsActive()
{
    if (!PlayerbotAI::IsTank(bot) || !bot->IsAlive())
        return false;

    Unit* boss = AI_VALUE2(Unit*, "find target", bossName);
    if (!boss || !boss->IsAlive())
        return false;

    Unit* victim = boss->GetVictim();
    if (!victim)
        return false;

    // A player main tank ("tank main on") holds the boss in any spec, and is rescued under the rescue health even in
    // a tank spec (06/11 review, point 7).
    if (Player* victimPlayer = victim->ToPlayer())
        if (!CoaLooseOnMember(CoaPlayerMainTank::Hold(bot, boss), PlayerbotAI::IsTank(victimPlayer)))
            return false;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        // A feared or stunned tank cannot take it back, so the election passes over it.
        if (!member || !member->IsAlive() || member == victim || !PlayerbotAI::IsTank(member) ||
            !GET_PLAYERBOT_AI(member) || member->GetMapId() != bot->GetMapId() ||
            member->HasUnitState(UNIT_STATE_LOST_CONTROL))
            continue;

        return member == bot;
    }

    return false;
}

bool RaidBackupTauntAction::Execute(Event event)
{
    Unit* boss = AI_VALUE2(Unit*, "find target", bossName);
    if (!boss)
        return false;

    if (bot->GetVictim() != boss)
        return Attack(boss);

    std::string const tauntAction = bot->getClass() == CLASS_DRUID ? "growl" : "taunt spell";
    return botAI->DoSpecificAction(tauntAction, event, true);
}
