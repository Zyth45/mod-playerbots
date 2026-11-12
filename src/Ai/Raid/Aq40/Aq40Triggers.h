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
    // The Dancer whose blades reach the bot, or nullptr. A melee damage dealer heeds Dance of Death (26 yd) only: the
    // skull sends it onto the Dancers, and a 6 yd push-back would bounce it off its target for good.
    Creature* FindDangerousDancer(Player* bot);

    // SA1: Sartura (15516) gains Frustration (2116135, 4 max) for every interrupted royal cast; the 4th starts
    // Furious Outbreak. Interrupts on her are held from 3 stacks on.
    constexpr uint32 NPC_SARTURA = 15516;
    constexpr uint32 SPELL_FRUSTRATION = 2116135;
    constexpr uint8 FRUSTRATION_HOLD_STACKS = 3;

    // VS1 (stock script): 200 frost hits freeze Viscidus (25937, 15 s), then melee hits shatter him; damage alone
    // stops at 5 %.
    constexpr uint32 SPELL_VISCIDUS_FREEZE = 25937;
    // The frost spell this bot's class lands on Viscidus, or an empty string.
    std::string ViscidusFrostSpell(Player* bot);

    // TW1: Vek'lor heals his brother 1 % a second while they stand within 50 yd; their tanks keep them 55 yd apart,
    // each on the dais nearest to its emperor.
    constexpr float TWINS_APART_YARDS = 55.0f;
    // The emperor this tank holds and his brother, or false.
    bool GetHeldEmperor(Player* bot, Unit*& held, Unit*& brother);

    // CT1: the melee damage dealers who go down with the prey of Devours (2117117) to kill the Flesh Tentacles.
    constexpr uint32 DEVOUR_COMPANIONS = 3;
    constexpr float DEVOUR_JOIN_YARDS = 5.0f;
    // The prey of Devours anywhere in the room, or nullptr.
    Player* GetDevourPrey(Player* bot);
    // The bot is one of the first DEVOUR_COMPANIONS melee damage dealers of the group, prey excluded.
    bool IsDevourCompanion(Player* bot, Player* prey);
}

// CoA Combat (VS1): Viscidus is not frozen and this bot can hit him with frost.
class Aq40ViscidusFrostTrigger : public Trigger
{
public:
    Aq40ViscidusFrostTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 viscidus frost") {}
    bool IsActive() override;
};

// CoA Combat (VS1): Viscidus is frozen and this melee bot is not hitting him.
class Aq40ViscidusShatterTrigger : public Trigger
{
public:
    Aq40ViscidusShatterTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 viscidus shatter") {}
    bool IsActive() override;
};

// CoA Combat (TW1): this tank holds an emperor who stands within 55 yd of his brother.
class Aq40TwinEmperorsApartTrigger : public Trigger
{
public:
    Aq40TwinEmperorsApartTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 twin emperors apart") {}
    bool IsActive() override;
};

// CoA Combat (CT1): this bot goes down with the prey of Devours and is not beside it yet.
class Aq40CthunJoinDevourTrigger : public Trigger
{
public:
    Aq40CthunJoinDevourTrigger(PlayerbotAI* botAI) : Trigger(botAI, "aq40 cthun join devour") {}
    bool IsActive() override;
};

#endif
