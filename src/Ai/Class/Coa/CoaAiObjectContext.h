/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAAIOBJECTCONTEXT_H
#define _PLAYERBOT_COAAIOBJECTCONTEXT_H

#include "AiObjectContext.h"

#include <ctime>
#include <unordered_map>

class PlayerbotAI;

/*
 * Fallback combat context for Conquest of Azeroth custom classes (ids 12 and above).
 *
 * mod-playerbots ships one hand written context per vanilla class. A character whose
 * class id is outside 1-11 falls through to the plain AiObjectContext, which carries
 * movement, questing and social behaviour but no combat rotation at all: such bots
 * greet, follow and hand in quests, then stand still in a fight.
 *
 * Rather than writing one context per custom class (each vanilla one is roughly two
 * thousand lines), this context rotates through the class abilities listed in
 * ascension_custom_class_spell that the bot has learned. It therefore covers every
 * custom class at once, including any the server author adds later, at the cost of
 * not knowing class specific rotations.
 */
class CoaAiObjectContext : public AiObjectContext
{
public:
    CoaAiObjectContext(PlayerbotAI* botAI);

    static void BuildSharedContexts();
    static void BuildSharedStrategyContexts(SharedNamedObjectContextList<Strategy>& strategyContexts);
    static void BuildSharedActionContexts(SharedNamedObjectContextList<Action>& actionContexts);
    static void BuildSharedTriggerContexts(SharedNamedObjectContextList<Trigger>& triggerContexts);
    static void BuildSharedValueContexts(SharedNamedObjectContextList<UntypedValue>& valueContexts);

    static SharedNamedObjectContextList<Strategy> sharedStrategyContexts;
    static SharedNamedObjectContextList<Action> sharedActionContexts;
    static SharedNamedObjectContextList<Trigger> sharedTriggerContexts;
    static SharedNamedObjectContextList<UntypedValue> sharedValueContexts;

    // Spells this bot set aside after a failure that will not clear on the next tick (wrong
    // target state, shapeshift...), with the time they may be tried again. One per bot, only
    // touched by the bot's own AI update, so no locking.
    std::unordered_map<uint32, time_t> benchedSpells;

    // Whether this healer already told its group it is low on mana in the current fight.
    bool lowManaSaid = false;
};

#endif
