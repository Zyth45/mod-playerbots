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

#include <vector>

class Player;

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

// Rolls every roll a random Wildcard bot has earned (its four starting abilities first) and moves it
// onto the stat path its build suits. True when its role or style changed.
bool DraftWildcardBuild(Player* bot);

// Forgets the whole build, for a bot rebuilt at a lower level than its rolls were made for.
void ResetWildcardBuild(Player* bot);

// The spell each rolled entry taught, at the rank it was rolled with.
std::vector<uint32> WildcardBuildSpells(Player const* bot);

// Drops the cached ability kit of a Hero after its build changed (CoaAiObjectContext).
void ForgetWildcardKit(Player const* bot);

#endif
