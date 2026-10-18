/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_SMTRIGGERS_H
#define PLAYERBOTS_SMTRIGGERS_H

#include "DungeonStrategyUtils.h"
#include "GenericTriggers.h"
#include "Trigger.h"

// Scarlet Monastery (map 189, all four wings). CoA runs the same boss kits on Normal, Heroic and Mythic; only
// health and damage change, and the Heroic/Mythic spells keep their names, so the spells are matched by name.
namespace ScarletMonastery
{
    // Arcanist Doan: at half health he shields himself in Arcane Bubble (6 s) and casts Detonation, which hits
    // everyone within 30 yd (Spell.dbc 9435).
    constexpr float DETONATION_SAFE_DISTANCE = 33.0f;

    // Doan's area Silence (8988, every 20-22 s) reaches 20 yd, his Arcanist's Explosion (2100692) 15 yd: casters
    // and healers stay outside both.
    constexpr float DOAN_RANGED_DISTANCE = 22.0f;

    // Herod's Whirlwind (8989, 10 s) hits within 5 yd of him (15578).
    constexpr float WHIRLWIND_SAFE_DISTANCE = 9.0f;

    bool HasAuraNamed(Unit const* unit, char const* name);
    bool IsCastingNamed(Unit const* unit, char const* name);

    bool IsDetonating(Unit const* doan);
    bool IsWhirlwinding(Unit const* herod);

    // Whitemane while Mograine stands again beside her: she is the one to kill first.
    Unit* WhitemaneToFocus(PlayerbotAI* botAI, AiObjectContext* context);
}

class SmDoanDetonationTrigger : public Trigger
{
public:
    SmDoanDetonationTrigger(PlayerbotAI* ai) : Trigger(ai, "sm doan detonation") {}
    bool IsActive() override;
};

class SmDoanRangedTrigger : public Trigger
{
public:
    SmDoanRangedTrigger(PlayerbotAI* ai) : Trigger(ai, "sm doan ranged") {}
    bool IsActive() override;
};

class SmHerodWhirlwindTrigger : public Trigger
{
public:
    SmHerodWhirlwindTrigger(PlayerbotAI* ai) : Trigger(ai, "sm herod whirlwind") {}
    bool IsActive() override;
};

class SmWhitemaneFocusTrigger : public Trigger
{
public:
    SmWhitemaneFocusTrigger(PlayerbotAI* ai) : Trigger(ai, "sm whitemane focus") {}
    bool IsActive() override;
};

#endif
