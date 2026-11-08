/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MCACTIONS_H
#define PLAYERBOTS_MCACTIONS_H

#include "AttackAction.h"
#include "MovementActions.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"

class McMoveFromGroupAction : public MovementAction
{
public:
    McMoveFromGroupAction(PlayerbotAI* botAI, std::string const name = "mc move from group")
        : MovementAction(botAI, name) {}
    bool Execute(Event event) override;
};

class McMoveFromBaronGeddonAction : public MovementAction
{
public:
    McMoveFromBaronGeddonAction(PlayerbotAI* botAI, std::string const name = "mc move from baron geddon")
        : MovementAction(botAI, name) {}
    bool Execute(Event event) override;
};

class McShazzrahMoveAwayAction : public MovementAction
{
public:
    McShazzrahMoveAwayAction(PlayerbotAI* botAI, std::string const name = "mc shazzrah move away")
        : MovementAction(botAI, name) {}
    bool Execute(Event event) override;
};

class McMoveFromShazzrahReflectionAction : public MovementAction
{
public:
    McMoveFromShazzrahReflectionAction(PlayerbotAI* botAI, std::string const name = "mc move from shazzrah reflection")
        : MovementAction(botAI, name) {}
    bool Execute(Event event) override;
};

class McShazzrahTankMoveFromReflectionAction : public MovementAction
{
public:
    McShazzrahTankMoveFromReflectionAction(PlayerbotAI* botAI,
                                           std::string const name = "mc shazzrah tank move from reflection")
        : MovementAction(botAI, name) {}
    bool Execute(Event event) override;
};

class McAttackSacrificialChainsAction : public AttackAction
{
public:
    McAttackSacrificialChainsAction(PlayerbotAI* botAI, std::string const name = "mc attack sacrificial chains")
        : AttackAction(botAI, name) {}
    bool Execute(Event event) override;
};

class McMoveFromLavaAction : public MovementAction
{
public:
    McMoveFromLavaAction(PlayerbotAI* botAI, std::string const name = "mc move from lava")
        : MovementAction(botAI, name) {}
    bool Execute(Event event) override;
};

class McGolemaggBackOffAction : public MovementAction
{
public:
    McGolemaggBackOffAction(PlayerbotAI* botAI, std::string const name = "mc golemagg back off")
        : MovementAction(botAI, name) {}
    bool Execute(Event event) override;
};

class McGolemaggMarkBossAction : public Action
{
public:
    McGolemaggMarkBossAction(PlayerbotAI* botAI, std::string const name = "mc golemagg mark boss")
        : Action(botAI, name) {};
    bool Execute(Event event) override;
};

class McGolemaggTankAction : public AttackAction
{
public:
    McGolemaggTankAction(PlayerbotAI* botAI, std::string const name)
        : AttackAction(botAI, name) {}
protected:
    bool MoveUnitToPosition(Unit* target, Position const& tankPosition, float maxDistance, float stepDistance = 3.0f);
    bool FindCoreRagers(Unit*& coreRager1, Unit*& coreRager2) const;
};

class McGolemaggMainTankAttackGolemaggAction : public McGolemaggTankAction
{
public:
    McGolemaggMainTankAttackGolemaggAction(PlayerbotAI* botAI, std::string const name = "mc golemagg main tank attack golemagg")
        : McGolemaggTankAction(botAI, name) {};
    bool Execute(Event event) override;
};

class McGolemaggAssistTankAttackCoreRagerAction : public McGolemaggTankAction
{
public:
    McGolemaggAssistTankAttackCoreRagerAction(PlayerbotAI* botAI, std::string const name = "mc golemagg assist tank attack core rager")
        : McGolemaggTankAction(botAI, name) {};
    bool Execute(Event event) override;
};

class McGolemaggHealerPositionAction : public MovementAction
{
public:
    McGolemaggHealerPositionAction(PlayerbotAI* botAI, std::string const name = "mc golemagg healer position")
        : MovementAction(botAI, name) {}
    bool Execute(Event event) override;
};

// CoA Combat (A1): skull the Son of Flame to kill now (FindSonOfFlameToKill), sticky while the skulled one lives.
class McRagnarosSonsMarkAction : public Action
{
public:
    McRagnarosSonsMarkAction(PlayerbotAI* botAI) : Action(botAI, "mc ragnaros sons mark") {}
    bool Execute(Event event) override;
};
// CoA Combat (R1): skull the Firesworn to kill now (FindFireswornToKill), sticky while the skulled one lives.
class McGarrFireswornMarkAction : public Action
{
public:
    McGarrFireswornMarkAction(PlayerbotAI* botAI) : Action(botAI, "mc garr firesworn mark") {}
    bool Execute(Event event) override;
};
// CoA Combat (R1): step out of the Eruption of a Firesworn about to die.
class McGarrMoveFromFireswornAction : public MovementAction
{
public:
    McGarrMoveFromFireswornAction(PlayerbotAI* botAI) : MovementAction(botAI, "mc garr move from firesworn") {}
    bool Execute(Event event) override;
};
class McCoreHoundMarkAction : public Action
{
public:
    McCoreHoundMarkAction(PlayerbotAI* botAI, std::string const name = "mc core hound mark")
        : Action(botAI, name) {};
    Unit* GetTarget() override;
    bool Execute(Event event) override;
};

class McMajordomoStopAttackAction : public Action
{
public:
    McMajordomoStopAttackAction(PlayerbotAI* botAI, std::string const name = "mc majordomo stop attack")
        : Action(botAI, name) {};
    bool Execute(Event event) override;
};

#endif
