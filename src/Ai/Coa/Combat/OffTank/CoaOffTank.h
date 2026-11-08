/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COA_OFF_TANK_H
#define PLAYERBOTS_COA_OFF_TANK_H

class Player;
class Unit;

// Automatic off-tank (CoA Combat, lot 5).
//
// A group with two tanks used to have both on the same mob: the second tank's target picker counted the main tank's
// mob as "not on me" like any loose add, and its "lose aggro" taunt then pulled it off the main tank. With the option
// on, a tank that is not the group's reference tank (CoaGroupTank::Reference) is the off-tank:
//   - its target picker ranks a mob already held by another tank after every other mob, so it takes the loose adds,
//     the one on the most endangered victim first (healer before anyone, then the lowest health: the picker's own
//     order);
//   - it never taunts a mob another tank holds.
// The main tank, a lone tank and every non-tank are unchanged. AiPlayerbot.CoaCombat.OffTank.Enable, 0 by default.
namespace CoaOffTank
{
    // The option is on and `bot` is a tank that is not its group's reference tank.
    bool IsOffTank(Player* bot);

    // `unit` hits another tank of the group than `bot`.
    bool HeldByOtherTank(Player* bot, Unit* unit);

    // For the target picker: true when `a` must rank before `b` for an off-tank on this ground alone, false when not,
    // and `decided` is set only when the two differ.
    inline bool RanksBefore(bool aHeldByOtherTank, bool bHeldByOtherTank, bool& decided)
    {
        decided = aHeldByOtherTank != bHeldByOtherTank;
        return decided && !aHeldByOtherTank;
    }
}

#endif
