/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef _PLAYERBOT_COAAIOBJECTCONTEXT_H
#define _PLAYERBOT_COAAIOBJECTCONTEXT_H

#include "AiObjectContext.h"
#include "ObjectGuid.h"

#include <array>
#include <ctime>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

    // The spells this bot's rotation lines name (spell name, priority), highest priority first: in a
    // "buff missing" or "can cast" trigger, or in a cast action. Of a family the world database makes
    // exclusive (spell_group_stack_rules), the member the highest of them names is the one kept (see
    // CoaRotationMayCast). Read from the combat and non-combat engines, again whenever one of them is
    // built anew (a new specialization, "co", a strategy change). Same owner as benchedSpells.
    std::vector<std::pair<std::string, float>> rotationBuffs;
    std::array<uint32, 2> rotationBuffsStamps{};

    // Ce qu'un soin rend vraiment, pour ce bot : les données du jeu mentent parfois (un sort
    // annoncé « heals for 0 », un autre « heals for 1 104 384 »), seule l'observation tranche.
    // Chez le bot et non sur le serveur : pas de verrou partagé, et cela survit à la coupure des
    // journaux — le module de diagnostic s'enlève et se remet sans rien changer au comportement.

    // Whether this healer already told its group it is low on mana in the current fight.
    bool lowManaSaid = false;

    // When this tank last pulled on its own (strategy "coa auto pull").
    time_t lastAutoPull = 0;
    time_t autoPullWaitLogged = 0;

    // getMSTime() of this bot's last kick in a group: the duty to interrupt goes to the bot that
    // kicked longest ago (AiPlayerbot.CoaInterruptCoordination).
    uint32 lastInterrupt = 0;

    // The add this bot put in its "prioritized targets" (AiPlayerbot.CoaDungeonPriorities): only that
    // entry is ever replaced, never a target the player chose.
    ObjectGuid priorityAdd;

    // Since when this bot has been far from the real player it follows, out of a fight.
    time_t farFromPlayerSince = 0;
};

#endif
