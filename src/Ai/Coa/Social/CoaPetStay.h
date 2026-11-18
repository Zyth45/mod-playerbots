/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAPETSTAY_H
#define PLAYERBOTS_COAPETSTAY_H

class Player;

// CoA 3.1.6 (AiPlayerbot.CoaPetPassiveOnStay): a bot told "stay" sets its pets and guardians passive, so they do
// not run off to fight while it holds its place; "follow" gives them back the stance they had before.
namespace CoaPetStay
{
// stay true: every pet and guardian of the bot goes passive (its former stance kept). stay false: each one gets
// its former stance back; one that was passive before the order stays passive.
void Apply(Player* bot, bool stay);
}  // namespace CoaPetStay

#endif
