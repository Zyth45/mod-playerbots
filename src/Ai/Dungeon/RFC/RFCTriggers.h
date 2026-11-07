/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RFCTRIGGERS_H
#define PLAYERBOTS_RFCTRIGGERS_H

#include "DungeonStrategyUtils.h"
#include "GenericTriggers.h"
#include "PlayerbotAIConfig.h"
#include "Trigger.h"

#include <optional>

enum RagefireChasmIDs
{
    NPC_OGGLEFLINT                 = 11517,
    NPC_JERGOSH_THE_INVOKER        = 11518,
    NPC_BAZZALAN                   = 11519,
    NPC_TARAGAMAN_THE_HUNGERER     = 11520,

    NPC_RAGEFIRE_TROGG             = 11318,
    NPC_RAGEFIRE_SHAMAN            = 11319,
    NPC_SEARING_BLADE_CULTIST      = 11322,

    // Taragaman's small area spell. Its radius is what the ranged have to clear.
    SPELL_FIRE_NOVA                = 11970,

    // Every 35 seconds Taragaman raises a rock pillar under a player for 15 seconds and, seven
    // seconds later, starts a six second Massive Fire Nova that covers the whole room. Only
    // standing behind the pillar, out of his line of sight, survives it.
    NPC_RUMBLING_EARTH             = 663030,
    GO_EARTH_PILLAR_CORE           = 663043,
    SPELL_MASSIVE_FIRE_NOVA        = 2102516,

    // For five seconds after it is summoned the pillar carries this ground warning, then breaks out of
    // the ground with Earth Break, which deals 800 damage and knocks back everyone within four yards.
    SPELL_EARTH_PILLAR_WARNING     = 256852
};

// The extra yards cover the hit sphere of the bot and a little drift.
constexpr float EARTH_BREAK_RADIUS = 4.0f;
constexpr float EARTH_BREAK_SAFETY = 2.0f;

// Fire Nova reaches ten yards from Taragaman himself. The extra yards keep a bot that drifts
// slightly from being clipped by the next one.
constexpr float FIRE_NOVA_RADIUS = 10.0f;
constexpr float FIRE_NOVA_SAFETY = 3.0f;

// The nova reaches seventy yards, so any pillar the bot can find in that range is the one to use.
// Until the pillar's collision core has risen, the bot heads for a spot this far past the pillar's
// centre on the side facing away from the boss. Once it stands, the spot is chosen with the same line
// of sight test the nova uses, and must stay hidden this far to either side so a small drift on
// arrival does not expose the bot.
constexpr float EARTH_PILLAR_SEARCH = 70.0f;
constexpr float EARTH_PILLAR_COVER = 2.5f;
constexpr float EARTH_PILLAR_TOLERANCE = 1.0f;
constexpr float EARTH_PILLAR_CORE_SEARCH = 5.0f;
constexpr float EARTH_PILLAR_SHADOW_MARGIN = 0.75f;

// Taragaman stands on a cross of rock in the lava. Uppercut throws the tank straight away from him, so the tank holds
// him on his own spawn point and stands on the longest arm of rock leading away from it: the throw slides it along
// the rock instead of into the lava, and the pillars he raises under a player rise on rock too. The arm is found
// once per instance by walking out from the spawn point in steps until the ground drops or lava covers it.
constexpr float TARAGAMAN_ARM_STEP_ANGLE = 10.0f;
constexpr float TARAGAMAN_ARM_MAX = 30.0f;
constexpr float TARAGAMAN_ARM_MAX_DROP = 2.0f;
constexpr float TARAGAMAN_TANK_OFFSET = 2.5f;
constexpr float TARAGAMAN_TANK_TOLERANCE = 1.5f;

namespace TaragamanArena
{
    // The spot the tank holds while Taragaman is engaged.
    std::optional<Position> TankSpot(Player* bot, Unit* boss);
}

// The room a boss shares with its escort, measured from the spawns: Oggleflint stands within
// thirteen yards of a Trogg and a Shaman, Jergosh and Bazzalan within twelve of their Cultists.
constexpr float RFC_ESCORT_RANGE = 25.0f;

// The escort is not on anyone's threat table before it is pulled, so it cannot be found the way a
// boss is. Walk the units the bot can see instead, as the Frost Tomb trigger does in Utgarde. The
// entries are tried in order, so the caller decides what dies first.
namespace RagefireEscort
{
    Unit* Nearest(PlayerbotAI* botAI, Player* bot, AiObjectContext* context, std::string const& bossName,
                  std::initializer_list<uint32> entries);

    bool StillUp(PlayerbotAI* botAI, Player* bot, AiObjectContext* context, std::string const& bossName,
                 std::initializer_list<uint32> entries);
}

namespace TaragamanPillar
{
    Unit* Boss(PlayerbotAI* botAI, AiObjectContext* context);
    Creature* Find(Player* bot);
    bool ShouldHide(PlayerbotAI* botAI, Player* bot, AiObjectContext* context);
    bool HasCore(Creature* pillar);
    bool Hidden(Player* bot, Unit* boss, float x, float y, float z);
    bool SafelyHidden(Player* bot, Unit* boss, float x, float y, float z);
    bool InCover(Player* bot, Unit* boss, Creature* pillar);
    Position CoverSpot(Player* bot, Unit* boss, Creature* pillar);
    bool IsErupting(Creature* pillar);
    Creature* EruptingNear(Player* bot);
    bool PathCrossesEruption(Player* bot, Creature* pillar, Position const& spot);
}

class OggleflintAddsTrigger : public Trigger
{
public:
    OggleflintAddsTrigger(PlayerbotAI* ai) : Trigger(ai, "oggleflint adds") {}
    bool IsActive() override;
};

class JergoshAddsTrigger : public Trigger
{
public:
    JergoshAddsTrigger(PlayerbotAI* ai) : Trigger(ai, "jergosh adds") {}
    bool IsActive() override;
};

class BazzalanDpsTrigger : public Trigger
{
public:
    BazzalanDpsTrigger(PlayerbotAI* ai) : Trigger(ai, "bazzalan priority") {}
    bool IsActive() override;
};

class TaragamanFireNovaTrigger : public Trigger
{
public:
    TaragamanFireNovaTrigger(PlayerbotAI* ai) : Trigger(ai, "taragaman fire nova") {}
    bool IsActive() override;
};

class TaragamanEarthBreakTrigger : public Trigger
{
public:
    TaragamanEarthBreakTrigger(PlayerbotAI* ai) : Trigger(ai, "taragaman earth break") {}
    bool IsActive() override;
};

class TaragamanTankSpotTrigger : public Trigger
{
public:
    TaragamanTankSpotTrigger(PlayerbotAI* ai) : Trigger(ai, "taragaman tank spot") {}
    bool IsActive() override;
};

class TaragamanMassiveFireNovaTrigger : public Trigger
{
public:
    TaragamanMassiveFireNovaTrigger(PlayerbotAI* ai) : Trigger(ai, "taragaman massive fire nova") {}
    bool IsActive() override;
};

#endif
