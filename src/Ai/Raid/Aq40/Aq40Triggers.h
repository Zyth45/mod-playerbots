/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40TRIGGERS_H
#define PLAYERBOTS_AQ40TRIGGERS_H

#include "RaidBossScripts.h"
#include "Trigger.h"

class Aq40CthunLeaveStomachTrigger : public Trigger
{
public:
    Aq40CthunLeaveStomachTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun leave stomach") {}
    bool IsActive() override;
};

class Aq40CthunDarkGlareTrigger : public Trigger
{
public:
    Aq40CthunDarkGlareTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun dark glare") {}
    bool IsActive() override;
};

class Aq40CthunEldritchTrigger : public Trigger
{
public:
    Aq40CthunEldritchTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun eldritch revelations") {}
    bool IsActive() override;
};

class Aq40CthunDevourTrigger : public Trigger
{
public:
    Aq40CthunDevourTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun devour") {}
    bool IsActive() override;
};

class Aq40CthunCenterTrigger : public Trigger
{
public:
    Aq40CthunCenterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun center") {}
    bool IsActive() override;
};

class Aq40CthunPositionTrigger : public Trigger
{
public:
    Aq40CthunPositionTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun position") {}
    bool IsActive() override;
};

class Aq40CthunTargetTrigger : public Trigger
{
public:
    Aq40CthunTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun target") {}
    bool IsActive() override;
};

class Aq40SkeramTargetTrigger : public Trigger
{
public:
    Aq40SkeramTargetTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram target") {}
    bool IsActive() override;
};

class Aq40SkeramHoldTrigger : public Trigger
{
public:
    Aq40SkeramHoldTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram hold") {}
    bool IsActive() override;
};

class Aq40SkeramPsychicShockTrigger : public Trigger
{
public:
    Aq40SkeramPsychicShockTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram psychic shock") {}
    bool IsActive() override;
};

class Aq40SkeramGazeSpreadTrigger : public Trigger
{
public:
    Aq40SkeramGazeSpreadTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 skeram gaze spread") {}
    bool IsActive() override;
};

// CoA Combat (SA2): a bot that is not a tank stands within the blades of one of Sartura's Phantom Dancers (15984):
// 5 yd in their dances, 25 yd in Dance of Death (2116108 on the Dancer).
class Aq40SarturaDancersTrigger : public Trigger
{
public:
    Aq40SarturaDancersTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 sartura dancers") {}
    bool IsActive() override;
};

// CoA Combat (SA2): the main tank skulls the Phantom Dancers first, the lowest first, then Sartura.
class Aq40SarturaMarkTrigger : public RaidKillOrderMarkTrigger
{
public:
    Aq40SarturaMarkTrigger(PlayerbotAI* botAI) : RaidKillOrderMarkTrigger(botAI, "aq40 sartura mark", "battleguard sartura") {}
    bool IsActive() override;
};

namespace CoaAq40
{
    constexpr uint32 NPC_PHANTOM_DANCER = 15984;
    constexpr uint32 SPELL_DANCE_OF_DEATH = 2116108;
    constexpr float DANCER_BLADE_YARDS = 6.0f;
    constexpr float DANCER_DEATH_YARDS = 26.0f;
    // The Dancer whose blades reach the bot, or nullptr.
    Creature* FindDangerousDancer(Unit* bot);
}

#endif
