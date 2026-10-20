/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_GATHERALLSTRATEGY_H
#define PLAYERBOTS_GATHERALLSTRATEGY_H

#include "Strategy.h"

class PlayerbotAI;

// With AiPlayerbot.GatherOnlySkillUp, a gathering bot leaves the herbs, veins and corpses that no longer raise
// its skill (grey for its client). "nc +gather all" lets this one bot gather them again.
class GatherAllStrategy : public Strategy
{
public:
    GatherAllStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}
    std::string const getName() override { return "gather all"; }
};

#endif
