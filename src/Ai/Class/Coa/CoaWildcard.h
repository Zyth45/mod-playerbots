/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * Wildcard Heroes as bots.
 *
 * On a Wildcard realm (Darkmoon - Season 10, CoAChallenges.GameModes.Realm = WildCard) every character
 * is a Hero, class 10, whose abilities and talents come from dice rolls instead of trainers and talent
 * trees. The server's Wildcard code rolls them (AscensionWildcard::DraftBuild, with the realm's synergy
 * settings); this side creates random bots as Heroes on such a realm, has them roll their build level by
 * level, picks the stat path that fits what they rolled, and hands the CoA combat logic a role, a style
 * and stats derived from that path.
 */

#ifndef PLAYERBOTS_COAWILDCARD_H
#define PLAYERBOTS_COAWILDCARD_H

#include "CoaSpecialization.h"

#include <utility>
#include <vector>

class Player;
class SpellInfo;

// Whether this realm plays Wildcard, so its random bots are Heroes.
bool IsWildcardRealm();

// A Hero playing Wildcard.
bool IsWildcardHeroBot(Player const* player);

// A CoA class or a Wildcard Hero: both fight with the runtime spell classifier of the CoA logic.
bool IsCoaBot(Player const* player);

// Role, fighting style and primary stats of a Wildcard Hero, from its stat path.
CoaRole GetWildcardRole(Player const* player);
CoaStyle GetWildcardStyle(Player const* player);
uint8 GetWildcardPrimaryStats(Player const* player);

// Rolls every roll a random Wildcard bot has earned, its four starting abilities first, picked for the bot's
// role (one in five tanks, one in five heals), and moves it onto its role's stat path. A build above the bot's
// level is drafted again. True when its role or style changed.
bool DraftWildcardBuild(Player* bot);

// Forgets the whole build, for a bot rebuilt at a lower level than its rolls were made for.
void ResetWildcardBuild(Player* bot);

// Forgets the build of a random Wildcard Hero bot and drafts it again for its role (.playerbots wildcard redraft).
bool RedraftWildcardBot(Player* bot);

// True once after a draft in combat left a new stat path for later: the caller drafts again when the fight is over
// (PlayerbotAI::ChangeEngineOnNonCombat).
bool TakeDueWildcardPath(Player const* bot);

// The spell each rolled entry taught, at the rank it was rolled with.
std::vector<uint32> WildcardBuildSpells(Player const* bot);

// The same spells, each with the synergy score of its entry against the rest of the build.
std::vector<std::pair<uint32, uint32>> WildcardBuildSynergies(Player const* bot);

// Drops the cached ability kit of a Hero after its build changed (CoaAiObjectContext).
void ForgetWildcardKit(Player const* bot);

// Whether the CoA fighting logic ever casts this spell (CoaAiObjectContext).
bool CoaBotCasts(SpellInfo const* info);

#endif
