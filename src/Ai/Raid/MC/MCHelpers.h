/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_MCHELPERS_H
#define PLAYERBOTS_MCHELPERS_H

#include "Creature.h"
#include "Define.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "Timer.h"
#include "Unit.h"

#include <algorithm>
#include <cstddef>
#include <list>
#include <unordered_map>
#include <vector>

// Stock and Conquest of Azeroth (mod-coa-raid-difficulty) ids side by side.
// CoA runs Molten Core on four difficulties. Every cast is resolved through
// SpellDifficulty.dbc before it is applied, so an aura carries the id of the
// current difficulty and every variant is listed.
namespace MoltenCoreHelpers
{
enum MoltenCoreNPCs
{
    // Magmadar
    NPC_MAGMADAR = 11982,
    NPC_COA_MAGMADAR_RIGHT_HEAD = 80642,  // CoA: summoned heads, every hit is passed on to the body
    NPC_COA_MAGMADAR_LEFT_HEAD = 80643,

    // Gehennas
    NPC_FLAMEWAKER = 11661,
    NPC_COA_GEHENNAS_FLAME_ORB = 80644,  // CoA: summoned orb that pulses fire around it

    // Golemagg
    NPC_CORE_RAGER = 11672,

    // Majordomo Executus
    NPC_MAJORDOMO_EXECUTUS = 12018,
    NPC_FLAMEWAKER_HEALER = 11663,
    NPC_FLAMEWAKER_ELITE = 11664,

    // Core Hound (trash)
    NPC_CORE_HOUND = 11671,

    // CoA: left by Shazzrah's Blink where he stood; rooted and immune, it repeats Mirrored Arcane
    // Explosion (2105650) until the encounter ends (boss_shazzrah_coa.cpp)
    NPC_REFLECTION_OF_SHAZZRAH = 11504,

    // CoA: Majordomo's periodic add; it roots, silences and pacifies the players it chains until it dies
    // (npc_sacrificial_chains_coa.cpp)
    NPC_SACRIFICIAL_CHAINS = 92030,
    // CoA Combat (docs/MC-ETAT-3.0.md): Lucifron's and Sulfuron's adds, for their kill orders.
    NPC_COA_LESSER_SON_OF_FLAME = 12143,  // CoA: Ragnaros' Sons of Flame, ranked; two of one rank within 5 yd for
    NPC_COA_SON_OF_FLAME = 92026,         // 1.5 s merge into one of the next rank (Lesser, Son, Greater, Unstable)
    NPC_COA_GREATER_SON_OF_FLAME = 92027,
    NPC_COA_UNSTABLE_SON_OF_FLAME = 92028,
    NPC_SHADOW_OF_LUCIFRON = 12268,     // CoA: +39 % shadow damage to Lucifron while it lives
    NPC_FLAMEWAKER_PROTECTOR = 12119,   // Dominate Mind
    NPC_FLAMEWAKER_PRIEST = 11662,      // Sulfuron's healer
    NPC_COA_CULL_THE_DESTROYER = 92031,  // CoA Mythic/Ascended: Cauterize, Curse of Gehennas
    NPC_COA_PROXIMA_THE_OPRESSOR = 92032,  // Dampen Magic, Arcane Instability
    NPC_COA_EBON_THE_CRUEL = 92033,     // Dark Mending, Curse of Lucifron
};

// CoA difficulty copies of a Molten Core creature: base + offset (01_mc_difficulty_templates.sql)
constexpr uint32 MC_ENTRY_OFFSET_HEROIC = 100000;
constexpr uint32 MC_ENTRY_OFFSET_MYTHIC = 200000;
constexpr uint32 MC_ENTRY_OFFSET_ASCENDED = 300000;

enum MoltenCoreSpells
{
    // Magmadar
    SPELL_LAVA_BOMB_DOT = 19428,  // periodic fire while standing in the bomb's patch (GO 177704)
    SPELL_LAVA_BOMB_COA_D0 = 2105367,  // CoA: the heads' ground fire (persistent area aura),
    SPELL_LAVA_BOMB_COA_D1 = 2105368,  // SpellDifficulty row 2100
    SPELL_LAVA_BOMB_COA_D2 = 2105369,
    SPELL_LAVA_BOMB_COA_D3 = 2105370,

    // Gehennas
    SPELL_RAIN_OF_FIRE = 19717,
    SPELL_RAIN_OF_FIRE_COA_D0 = 2105407,  // CoA: 10 yd ground fire at the tank, 20 s (SpellDifficulty row 2104)
    SPELL_RAIN_OF_FIRE_COA_D1 = 2105408,
    SPELL_RAIN_OF_FIRE_COA_D2 = 2105409,
    SPELL_RAIN_OF_FIRE_COA_D3 = 2105410,
    SPELL_CONJURE_FLAME_ORB_COA = 2105417,  // CoA: 5 s cast; the orb spawns on Gehennas when it completes

    // Baron Geddon
    SPELL_INFERNO = 19695,                 // SpellDifficulty row 369: 19695/350084/350084/350084
    SPELL_INFERNO_DIFF_1_3 = 350084,
    SPELL_INFERNO_COA = 2105740,           // CoA: 3 s cast, then a 10 s aura on Geddon; no SpellDifficulty row
    SPELL_LIVING_BOMB = 20475,
    SPELL_LIVING_BOMB_COA_CAST = 2105701,  // CoA: 1 s dummy cast at the bomb's target
    SPELL_LIVING_BOMB_COA_D0 = 2105702,    // CoA: debuff applied when the dummy cast completes,
    SPELL_LIVING_BOMB_COA_D1 = 2105703,    // SpellDifficulty row 2115
    SPELL_LIVING_BOMB_COA_D2 = 2105704,
    SPELL_LIVING_BOMB_COA_D3 = 2105705,

    // Golemagg
    SPELL_GOLEMAGGS_TRUST = 20553,
    SPELL_MAGMA_SPLASH = 13880,            // SpellDifficulty row 295: 13880/13880/350099/350099
    SPELL_MAGMA_SPLASH_DIFF_2_3 = 350099,

    // Majordomo Executus
    SPELL_MAGIC_REFLECTION = 20619,
    SPELL_DAMAGE_SHIELD = 21075,
};

// Fire patches under Magmadar's bombs: the body's stock trap and the CoA heads' ground fire
constexpr uint32 MAGMADAR_LAVA_BOMB_AURAS[] = {SPELL_LAVA_BOMB_DOT, SPELL_LAVA_BOMB_COA_D0, SPELL_LAVA_BOMB_COA_D1,
                                               SPELL_LAVA_BOMB_COA_D2, SPELL_LAVA_BOMB_COA_D3};

// CoA Combat (O1): Golemagg's Cave In, a periodic-damage zone of 8 yd cast on his tank after each Massive Stomp, 15 s.
constexpr uint32 GOLEMAGG_CAVE_IN_AURAS[] = {2105825, 2105826, 2105827, 2105828};

// Standing in Gehennas' Rain of Fire (stock or CoA)
constexpr uint32 GEHENNAS_RAIN_OF_FIRE_AURAS[] = {SPELL_RAIN_OF_FIRE, SPELL_RAIN_OF_FIRE_COA_D0, SPELL_RAIN_OF_FIRE_COA_D1,
                                                  SPELL_RAIN_OF_FIRE_COA_D2, SPELL_RAIN_OF_FIRE_COA_D3};

// Aura on Baron Geddon while Inferno is active
constexpr uint32 INFERNO_AURAS[] = {SPELL_INFERNO, SPELL_INFERNO_DIFF_1_3};

// Debuff on the Living Bomb carrier
constexpr uint32 LIVING_BOMB_AURAS[] = {SPELL_LIVING_BOMB, SPELL_LIVING_BOMB_COA_D0, SPELL_LIVING_BOMB_COA_D1,
                                        SPELL_LIVING_BOMB_COA_D2, SPELL_LIVING_BOMB_COA_D3};

// Stacking debuff from striking Golemagg in melee
constexpr uint32 MAGMA_SPLASH_AURAS[] = {SPELL_MAGMA_SPLASH, SPELL_MAGMA_SPLASH_DIFF_2_3};

// CoA Combat (O2): CoA's own Magma Splash, a magic DoT that stacks to 100 (SpellDifficulty D0..D3). Counted with the
// stock stacks only when AiPlayerbot.CoaCombat.MoltenCore.Enable is 1.
constexpr uint32 MAGMA_SPLASH_COA_AURAS[] = {2105802, 2105803, 2105804, 2105805};

// CoA Combat (A3): Magma Strike, the 2 yd fire patch Ragnaros and his Sons throw on the ground (4 s, D0..D3 of both
// missiles). The Lava Burst geysers are not dodged on purpose: their trap (21158) hits everyone within 100 yd.
constexpr uint32 RAGNAROS_MAGMA_STRIKE_AURAS[] = {2108657, 2108658, 2108659, 2108660,
                                                  2108726, 2108727, 2108728, 2108729};

// CoA Combat (bench a12): Burning Ground, the 2 yd fire a CoA Living Bomb leaves where its carrier stood
// (boss_geddon_coa.cpp, D0..D3, 800 to 1,600 a second, with no end in the DBC).
constexpr uint32 GEDDON_BURNING_GROUND_AURAS[] = {2105713, 2105714, 2105715, 2105716};

// CoA Combat (bench a12): the debuffs the raid removes first, from the tanks down. Curses: Curse of Gehennas (stock
// 19716, CoA 2105416 on the whole raid, -50 % healing taken, and Cull the Destroyer's 2105974), the Ancient Core
// Hounds' Ancient Hysteria (19372 and CoA 2105311, -25 % stats) and Ancient Dread (19365 and CoA 2105312, -50 % stats,
// so health), CoA's Curse of Lucifron (2105207). Ancient Despair (2105310) stays on purpose: its -50 % damage taken
// outweighs its -50 % damage dealt in a pull that kills by burst. Magic: Shazzrah's Arcane Instability (2105606 on the whole raid,
// +100 % magic damage taken, 5 min; Proxima's 2105970), Withering Heat (19367, -25 % health), Incite Flames (19635)
// and Flame Buffet (16168, +5 % fire damage taken a stack). The removal spell follows the aura's own dispel type.
constexpr uint32 MC_RAID_DISPEL_AURAS[] = {2105416, 19716, 2105974, 2105606, 2105970, 19367,
                                           19372, 2105311, 19365, 2105312, 2105207, 16168, 19635};

// CoA Combat (R1): Garr's Firesworn erupt when they die, 12 yd around them (CoA cut the 100 yd radius down to 12).
constexpr uint32 NPC_GARR_FIRESWORN = 12099;
constexpr float FIRESWORN_ERUPTION_YARDS = 12.0f;
constexpr float FIRESWORN_DYING_PCT = 10.0f;

constexpr uint32 MAGMA_SPLASH_BACK_OFF_STACKS = 20;
constexpr float MAGMA_SPLASH_BACK_OFF_DISTANCE = 12.0f;

// Shazzrah's Arcane Explosion radius (stock 19712: 18 yd, CoA 2105601: 20 yd, Spell.dbc)
constexpr float ARCANE_EXPLOSION_DISTANCE = 26.0f;

// Distance kept from Gehennas' Flame Orb: its pulse (2105419-22) hits everything within 20 yd
// every 275 ms for 11 s, about 4,000 damage a second on Normal.
constexpr float FLAME_ORB_AVOID_DISTANCE = 24.0f;

// During Conjure Flame Orb Gehennas stands still: everyone, his tank included, gets out of the
// orb's reach of its spawn point before it appears.
constexpr float FLAME_ORB_CAST_CLEAR_DISTANCE = 26.0f;
// A Reflection's Mirrored Arcane Explosion (2105650) is 20 yd like Shazzrah's own: same distance kept
constexpr float REFLECTION_DISTANCE = ARCANE_EXPLOSION_DISTANCE;
// A bot leaving a Reflection stops this much further out than the trigger distance, so it does not
// stand on the edge and step in and out of it every tick.
constexpr float REFLECTION_CLEAR_MARGIN = 4.0f;
// Shazzrah blinks around his room; every Reflection of the pull lies within this of him.
constexpr float REFLECTION_SCAN_DISTANCE = 100.0f;
// Ranged bots leaving a Reflection stay within casting reach of Shazzrah.
constexpr float REFLECTION_RANGED_MAX_BOSS_DISTANCE = 35.0f;

constexpr float SACRIFICIAL_CHAINS_SEARCH_DISTANCE = 60.0f;

// How long a boss's add list is reused before the grid is searched again.
constexpr uint32 MC_ADD_CACHE_MS = 1000;

template <std::size_t N>
inline bool HasAnyAura(Unit const* unit, uint32 const (&spellIds)[N])
{
    for (uint32 spellId : spellIds)
        if (unit->HasAura(spellId))
            return true;
    return false;
}

// A unit only carries the variant of its own difficulty, so the highest stack
// among the listed ids is that one.
template <std::size_t N>
inline uint32 GetAuraStacks(Unit const* unit, uint32 const (&spellIds)[N])
{
    uint32 stacks = 0;
    for (uint32 spellId : spellIds)
        if (Aura* aura = unit->GetAura(spellId))
            stacks = std::max<uint32>(stacks, aura->GetStackAmount());
    return stacks;
}

// Inferno is up on Geddon, or (CoA) its cast is in progress.
inline bool IsInfernoActive(Unit const* geddon)
{
    return HasAnyAura(geddon, INFERNO_AURAS);
}

// CoA: Geddon is casting the Living Bomb dummy at this unit; the debuff lands
// when the cast completes (CoaBossAI.cpp).
inline bool IsLivingBombCastAt(Unit const* geddon, Unit const* target)
{
    Spell* spell = geddon->FindCurrentSpellBySpellId(SPELL_LIVING_BOMB_COA_CAST);
    return spell && spell->m_targets.GetUnitTargetGUID() == target->GetGUID();
}

// The living adds of `entry` around `boss`. Every bot of the raid asks every tick, so the grid search
// runs at most once per MC_ADD_CACHE_MS for a given boss and the guids are reused in between. The cache
// is per thread: a map and the bots in it are updated by one thread, so no lock is needed.
inline std::vector<Creature*> GetMcBossAdds(Unit const* boss, uint32 entry, float scanRange)
{
    struct AddCache
    {
        uint32 entry = 0;
        uint32 refreshedAt = 0;
        std::vector<ObjectGuid> guids;
    };
    thread_local std::unordered_map<ObjectGuid, AddCache> caches;

    uint32 const now = getMSTime();
    auto it = caches.find(boss->GetGUID());
    if (it == caches.end() || it->second.entry != entry || getMSTimeDiff(it->second.refreshedAt, now) >= MC_ADD_CACHE_MS)
    {
        // Drop the lists of bosses nobody asked about for a minute (pull over, instance gone).
        for (auto stale = caches.begin(); stale != caches.end();)
        {
            if (getMSTimeDiff(stale->second.refreshedAt, now) >= 60000u)
                stale = caches.erase(stale);
            else
                ++stale;
        }

        std::list<Creature*> found;
        boss->GetCreatureListWithEntryInGrid(found, entry, scanRange);
        AddCache& cache = caches[boss->GetGUID()];
        cache.entry = entry;
        cache.refreshedAt = now;
        cache.guids.clear();
        for (Creature* creature : found)
            if (creature->IsAlive())
                cache.guids.push_back(creature->GetGUID());
        it = caches.find(boss->GetGUID());
    }

    std::vector<Creature*> adds;
    for (ObjectGuid const& guid : it->second.guids)
        if (Creature* creature = ObjectAccessor::GetCreature(*boss, guid))
            if (creature->IsAlive())
                adds.push_back(creature);
    return adds;
}

inline std::vector<Creature*> GetShazzrahReflections(Unit const* shazzrah)
{
    return GetMcBossAdds(shazzrah, NPC_REFLECTION_OF_SHAZZRAH, REFLECTION_SCAN_DISTANCE);
}

// Blink wipes Shazzrah's threat, so after each one he is on whoever hits first until a tank taunts him
// back, and nobody walks him out of a Reflection while a tank is down or off him.
inline bool IsShazzrahHeldByTank(Unit const* shazzrah)
{
    Unit* victim = shazzrah->GetVictim();
    Player* player = victim ? victim->ToPlayer() : nullptr;
    return player && player->IsAlive() && PlayerbotAI::IsTank(player);
}

// The nearest of `reflections` within `range` of `where`.
inline Creature* FindReflectionNear(std::vector<Creature*> const& reflections, WorldObject const* where, float range)
{
    Creature* nearest = nullptr;
    float nearestDist = range;
    for (Creature* reflection : reflections)
    {
        float const dist = where->GetDistance2d(reflection);
        if (dist < nearestDist)
        {
            nearestDist = dist;
            nearest = reflection;
        }
    }
    return nearest;
}

inline bool IsClearOfReflections(std::vector<Creature*> const& reflections, float x, float y, float clearance)
{
    for (Creature* reflection : reflections)
        if (reflection->GetDistance2d(x, y) < clearance)
            return false;
    return true;
}

// The living Sacrificial Chains near `bot` with the least health, so every bot finishes the same one first.
inline Creature* FindSacrificialChains(Unit const* majordomo, WorldObject const* bot)
{
    Creature* weakest = nullptr;
    for (Creature* chain : GetMcBossAdds(majordomo, NPC_SACRIFICIAL_CHAINS, SACRIFICIAL_CHAINS_SEARCH_DISTANCE))
        if (bot->GetDistance(chain) <= SACRIFICIAL_CHAINS_SEARCH_DISTANCE &&
            (!weakest || chain->GetHealth() < weakest->GetHealth()))
            weakest = chain;
    return weakest;
}

inline bool IsSacrificialChains(Unit const* unit)
{
    return unit && unit->GetTypeId() == TYPEID_UNIT && unit->GetEntry() == NPC_SACRIFICIAL_CHAINS && unit->IsAlive();
}

// Matches the base entry or one of its CoA difficulty copies. A spawn that
// carries the base entry keeps it on every difficulty; the copies only show
// up on a creature that was created by its copy entry. Defensive: the current
// CoA Molten Core spawns all use the base entry, so today this matches the
// same units as before (the server itself maps back with entry % 100000).
inline bool IsMcEntry(uint32 entry, uint32 baseEntry)
{
    return entry == baseEntry || entry == baseEntry + MC_ENTRY_OFFSET_HEROIC ||
           entry == baseEntry + MC_ENTRY_OFFSET_MYTHIC || entry == baseEntry + MC_ENTRY_OFFSET_ASCENDED;
}

// CoA: Magmadar's two heads cast at their own victim and pass every hit on to the body.
inline bool IsMagmadarHead(Unit const* unit)
{
    return unit && (IsMcEntry(unit->GetEntry(), NPC_COA_MAGMADAR_RIGHT_HEAD) ||
                    IsMcEntry(unit->GetEntry(), NPC_COA_MAGMADAR_LEFT_HEAD));
}

inline bool IsMajordomoAdd(Unit const* unit)
{
    return unit &&
           (IsMcEntry(unit->GetEntry(), NPC_FLAMEWAKER_HEALER) || IsMcEntry(unit->GetEntry(), NPC_FLAMEWAKER_ELITE));
}
// CoA Combat (A1): the Son of Flame of Ragnaros to kill now: the highest rank first (a merged one hits hardest), the
// most damaged within a rank, within 100 yd. nullptr when none is up.
inline Creature* FindSonOfFlameToKill(Unit* from)
{
    uint32 const ranks[] = {NPC_COA_UNSTABLE_SON_OF_FLAME, NPC_COA_GREATER_SON_OF_FLAME, NPC_COA_SON_OF_FLAME,
                            NPC_COA_LESSER_SON_OF_FLAME};
    for (uint32 entry : ranks)
    {
        Creature* best = nullptr;
        for (uint32 variant : {entry, entry + MC_ENTRY_OFFSET_HEROIC, entry + MC_ENTRY_OFFSET_MYTHIC,
                               entry + MC_ENTRY_OFFSET_ASCENDED})
        {
            std::list<Creature*> sons;
            from->GetCreatureListWithEntryInGrid(sons, variant, 100.0f);
            for (Creature* son : sons)
                if (son && son->IsAlive() && (!best || son->GetHealth() < best->GetHealth()))
                    best = son;
        }
        if (best)
            return best;
    }
    return nullptr;
}

// CoA Combat (R1): every living Firesworn of Garr within 100 yd (all four difficulty entries).
inline std::vector<Creature*> FindFiresworn(Unit* from)
{
    std::vector<Creature*> found;
    for (uint32 variant : {NPC_GARR_FIRESWORN, NPC_GARR_FIRESWORN + MC_ENTRY_OFFSET_HEROIC,
                           NPC_GARR_FIRESWORN + MC_ENTRY_OFFSET_MYTHIC, NPC_GARR_FIRESWORN + MC_ENTRY_OFFSET_ASCENDED})
    {
        std::list<Creature*> adds;
        from->GetCreatureListWithEntryInGrid(adds, variant, 100.0f);
        for (Creature* add : adds)
            if (add && add->IsAlive())
                found.push_back(add);
    }
    return found;
}

// The Firesworn to kill now: the lowest in health, so they die one at a time.
inline Creature* FindFireswornToKill(Unit* from)
{
    Creature* best = nullptr;
    for (Creature* add : FindFiresworn(from))
        if (!best || add->GetHealth() < best->GetHealth())
            best = add;
    return best;
}

// A Firesworn about to die (FIRESWORN_DYING_PCT) within `range` of `bot`: its Eruption will hit there.
inline Creature* FindDyingFireswornNear(Unit* bot, float range)
{
    for (Creature* add : FindFiresworn(bot))
        if (add->GetHealthPct() <= FIRESWORN_DYING_PCT && bot->GetDistance2d(add) < range)
            return add;
    return nullptr;
}
}  // namespace MoltenCoreHelpers

#endif
