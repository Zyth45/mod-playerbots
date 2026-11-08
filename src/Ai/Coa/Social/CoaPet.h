/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_COAPET_H
#define PLAYERBOTS_COAPET_H

#include <string>

class Player;
class PlayerbotAI;

// The Pets tab of the SquidBots Lite addon, server side (PETS-PROTOCOLE-3.0, CoaPetRules.h): "pet list | set |
// use | dismiss | auto" whispered by an alt's master.
namespace CoaPet
{
// True when the param of "pet" is a Pets verb (it was answered); false leaves it to the hunter pet command.
bool Handle(PlayerbotAI* botAI, Player* requester, std::string const& param);
}  // namespace CoaPet

#endif
