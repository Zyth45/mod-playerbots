/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// CoA bots in PvP. The generic arena tactics place a bot by its class, and every CoA bot is class 27: they all went
// to the melee spot. Here the role decides. Each behaviour has its own AiPlayerbot.CoaArena* / CoaPvp* / CoaDuel*
// switch, read on every use so that ".reload config" takes effect at once.
//
// Strategy "coa pvp", held by every CoA bot in both engines. Its triggers only open the way (the right place, the
// switch on, the role); the actions then look at the fight and decline when there is nothing to do, so that
// outside an arena, a duel or a battleground nothing changes.

#include "CoaPvpTactics.h"

#include "AscensionSpecialization.h"
#include "AttackAction.h"
#include "Battleground.h"
#include "CellImpl.h"
#include "Config.h"
#include "CoaSpecialization.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MovementActions.h"
#include "Multiplier.h"
#include "NamedObjectContext.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Strategy.h"
#include "Timer.h"
#include "Trigger.h"

#include <algorithm>
#include <cmath>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>

// CoaAiObjectContext.cpp: the crowd control abilities of the bot's CoA class.
bool CoaHasReadyControl(PlayerbotAI* botAI, Player* bot);
bool CoaCastControl(PlayerbotAI* botAI, Player* bot, Unit* target);

namespace
{
    std::mutex OptionsLock;
    std::unordered_map<std::string, bool> Options;

    // Read once per key and again after ".reload config" (CoaPvpOptionsReload): a missing key makes the
    // core log a warning at every read, and these are read at every bot tick in PvP.
    bool On(char const* key)
    {
        std::lock_guard<std::mutex> lock(OptionsLock);
        auto found = Options.find(key);
        if (found == Options.end())
            found = Options.emplace(key, sConfigMgr->GetOption<bool>(key, true)).first;
        return found->second;
    }

    bool IsCoaBot(Player* bot) { return bot && IsAscensionCustomClassId(bot->getClass()); }

    bool Healer(Player* player) { return PlayerbotAI::IsHeal(player, true); }
    bool Tank(Player* player) { return PlayerbotAI::IsTank(player, true); }
    // Healers and ranged damage dealers: the ones that should not stand in melee.
    bool Squishy(Player* player) { return Healer(player) || (PlayerbotAI::IsRanged(player, true) && !Tank(player)); }

    Battleground* ArenaOf(Player* bot)
    {
        if (!bot->InArena())
            return nullptr;
        Battleground* bg = bot->GetBattleground();
        return bg && bg->isArena() ? bg : nullptr;
    }

    bool ArenaWaiting(Player* bot)
    {
        Battleground* bg = ArenaOf(bot);
        return bg && bg->GetStatus() == STATUS_WAIT_JOIN;
    }

    // Each arena, as the bots need it: the floor height at its centre (the melee spot of ArenaTactics::moveToCenter,
    // the water and floor of the Dalaran and Ring of Valor scripts), how far from the centre a bot may go in a fight,
    // and how deep into its starting room it may spread before the gates open (none on the Ring of Valor lifts).
    struct ArenaGround
    {
        uint32 map;
        float floorZ;
        float reach;
        float roomDepth;
    };

    constexpr ArenaGround Grounds[] = {
        { 559, 13.5f, 38.0f, 7.0f },  // Nagrand
        { 562, 0.9f, 42.0f, 7.0f },   // Blade's Edge, the floor under the bridge
        { 572, 34.0f, 38.0f, 7.0f },  // Ruins of Lordaeron
        { 617, 7.1f, 34.0f, 3.0f },   // Dalaran Sewers, narrow pipes
        { 618, 28.3f, 34.0f, 0.0f },  // Ring of Valor
    };

    ArenaGround const* GroundOf(uint32 map)
    {
        for (ArenaGround const& ground : Grounds)
            if (ground.map == map)
                return &ground;
        return nullptr;
    }

    // Halfway between the two starting points, which every arena puts on either side of its centre.
    bool ArenaCentre(Battleground* bg, Position& centre)
    {
        ArenaGround const* ground = GroundOf(bg->GetMapId());
        Position const* a = bg->GetTeamStartPosition(TEAM_ALLIANCE);
        Position const* b = bg->GetTeamStartPosition(TEAM_HORDE);
        if (!ground || !a || !b)
            return false;
        centre.Relocate((a->GetPositionX() + b->GetPositionX()) / 2.0f, (a->GetPositionY() + b->GetPositionY()) / 2.0f,
                        ground->floorZ);
        return true;
    }

    // The floor under x, y close to `z`: no jump onto a bridge or down a ledge.
    bool GroundAt(Player* bot, float x, float y, float& z)
    {
        float const found = bot->GetMap()->GetHeight(bot->GetPhaseMask(), x, y, z + 3.0f, true, 8.0f);
        if (found <= INVALID_HEIGHT || std::fabs(found - z) > 4.0f)
            return false;
        z = found;
        return true;
    }

    // Whether the bot walks there by a path not much longer than `maxLength`.
    bool Walkable(Player* bot, float x, float y, float z, float maxLength)
    {
        PathGenerator path(bot);
        path.CalculatePath(x, y, z, false);
        PathType const type = path.GetPathType();
        if (type & PATHFIND_NOT_USING_PATH)
            return true;
        if (!(type & PATHFIND_NORMAL) || (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_SHORTCUT | PATHFIND_SHORT)))
            return false;
        Movement::PointsArray const& points = path.GetPath();
        if (points.empty())
            return false;
        float length = 0.0f;
        for (size_t i = 1; i < points.size(); ++i)
            length += (points[i] - points[i - 1]).length();
        float const dx = points.back().x - x;
        float const dy = points.back().y - y;
        return length <= maxLength && dx * dx + dy * dy < 4.0f;
    }

    Battleground* ArenaFight(Player* bot)
    {
        Battleground* bg = ArenaOf(bot);
        return bg && bg->GetStatus() == STATUS_IN_PROGRESS ? bg : nullptr;
    }

    // The living arena fighters of one side, on the bot's map; enemies only when the bot sees them.
    template <typename Visit>
    void Fighters(Battleground* bg, Player* bot, bool enemies, Visit visit)
    {
        for (auto const& [guid, player] : bg->GetPlayers())
        {
            if (!player || player == bot || !player->IsInWorld() || !player->IsAlive() || player->GetMap() != bot->GetMap())
                continue;
            if ((player->GetBgTeamId() != bot->GetBgTeamId()) != enemies)
                continue;
            if (enemies && !bot->CanSeeOrDetect(player))
                continue;
            visit(player);
        }
    }

    bool InArenaBounds(Battleground* bg, float x, float y)
    {
        ArenaGround const* ground = GroundOf(bg->GetMapId());
        Position centre;
        return !ground || !ArenaCentre(bg, centre) || centre.GetExactDist2d(x, y) <= ground->reach;
    }

    constexpr float Rejected = 1.0e9f;

    struct Spot
    {
        float x, y, z, score;
    };

    // Points on circles around (x, y).
    void Rings(std::vector<std::pair<float, float>>& points, float x, float y, std::initializer_list<float> radii, int count)
    {
        for (float radius : radii)
            for (int i = 0; i < count; ++i)
            {
                float const angle = 2.0f * float(M_PI) * (float(i) + (radius > 8.0f ? 0.5f : 0.0f)) / float(count);
                points.emplace_back(x + radius * std::cos(angle), y + radius * std::sin(angle));
            }
    }

    // The best scored point (lowest score) that has a floor and that the bot reaches. Paths are only computed for
    // the few best ones.
    template <typename Score>
    bool BestSpot(Player* bot, std::vector<std::pair<float, float>> const& points, Score score, float maxPath, Spot& best)
    {
        std::vector<Spot> spots;
        for (auto const& [x, y] : points)
        {
            float z = bot->GetPositionZ();
            if (!GroundAt(bot, x, y, z))
                continue;
            float const value = score(x, y, z);
            if (value < Rejected)
                spots.push_back({ x, y, z, value });
        }
        std::sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b) { return a.score < b.score; });
        if (spots.size() > 4)
            spots.resize(4);
        for (Spot const& spot : spots)
            if (Walkable(bot, spot.x, spot.y, spot.z, maxPath))
            {
                best = spot;
                return true;
            }
        return false;
    }

    bool MeleeEnemyNear(Battleground* bg, Player* bot, float x, float y, float distance)
    {
        bool found = false;
        Fighters(bg, bot, true, [&](Player* enemy)
        {
            if (!Squishy(enemy) && enemy->GetExactDist2d(x, y) < distance)
                found = true;
        });
        return found;
    }

    /*
     * Cover: what breaks the line of sight in each arena, for the bots that kite or hide (AiPlayerbot.CoaArenaCover).
     *
     * The pillars of the static map are found once per arena map, by a walk over the floor around the centre: a
     * 2 yard grid, where two neighbouring points at the same height that do not see each other at chest height
     * have something between them. Those points gather into groups; a small group (at most 14 yards across) that
     * also blocks the view straight across its middle is a pillar or a block, a long one is a wall or a ramp and
     * is left out. The pillars found are written to the log ("coa arena cover") so they can be checked in game.
     *
     * The core spawns some blockers as objects, which the static walk cannot see: the four Ring of Valor pillars
     * and the Dalaran waterfall (BattlegroundRV / BattlegroundDS). They come from the table below; they are not
     * always up, and every use checks the sight again.
     */
    struct Cover
    {
        float x, y, z, radius;
    };

    struct KnownCover
    {
        uint32 map;
        float x, y, radius;
    };

    constexpr KnownCover ObjectCover[] = {
        { 618, 763.6f, -306.2f, 3.0f },
        { 618, 723.6f, -284.5f, 3.0f },
        { 618, 763.6f, -261.9f, 3.0f },
        { 618, 802.2f, -284.5f, 3.0f },
        { 617, 1291.6f, 790.8f, 4.0f },
        { 559, 4055.8f, 2889.8f, 5.5f },
        { 559, 4082.8f, 2905.1f, 5.5f },
        { 559, 4058.3f, 2952.4f, 5.5f },
        { 559, 4029.0f, 2936.4f, 5.5f },
        { 572, 1281.3f, 1666.9f, 4.0f },
        { 572, 1290.2f, 1668.0f, 4.0f },
        { 562, 6225.6f, 272.2f, 3.5f },
        { 562, 6228.8f, 249.8f, 5.3f },
        { 562, 6251.0f, 251.0f, 3.4f },
        { 562, 6248.7f, 274.1f, 5.4f },
    };

    std::mutex CoverLock;
    std::unordered_map<uint32, std::vector<Cover>> FoundCover;

    std::vector<Cover> ScanCover(Map* map, uint32 phase, Position const& centre, float reach)
    {
        constexpr float Step = 2.0f;
        int const half = int(reach / Step);
        int const side = half * 2 + 1;
        auto index = [side](int i, int j) { return j * side + i; };
        auto pointX = [&](int i) { return centre.GetPositionX() + float(i - half) * Step; };
        auto pointY = [&](int j) { return centre.GetPositionY() + float(j - half) * Step; };

        std::vector<float> floor(size_t(side * side), INVALID_HEIGHT);
        for (int j = 0; j < side; ++j)
            for (int i = 0; i < side; ++i)
                if (centre.GetExactDist2d(pointX(i), pointY(j)) <= reach)
                    floor[index(i, j)] = map->GetHeight(phase, pointX(i), pointY(j), centre.GetPositionZ() + 4.0f, true, 10.0f);

        std::vector<uint8> blocked(size_t(side * side), 0);
        for (int j = 0; j < side; ++j)
            for (int i = 0; i < side; ++i)
            {
                float const za = floor[index(i, j)];
                if (za <= INVALID_HEIGHT)
                    continue;
                for (auto [di, dj] : { std::pair<int, int>(1, 0), std::pair<int, int>(0, 1) })
                {
                    int const ni = i + di;
                    int const nj = j + dj;
                    if (ni >= side || nj >= side)
                        continue;
                    float const zb = floor[index(ni, nj)];
                    if (zb <= INVALID_HEIGHT || std::fabs(za - zb) > 1.5f)
                        continue;
                    float const h = std::max(za, zb) + 1.6f;
                    if (!map->isInLineOfSight(pointX(i), pointY(j), h, pointX(ni), pointY(nj), h, phase,
                                              LINEOFSIGHT_CHECK_VMAP, VMAP::ModelIgnoreFlags::Nothing))
                        blocked[index(i, j)] = blocked[index(ni, nj)] = 1;
                }
            }

        std::vector<Cover> found;
        std::vector<uint8> seen(size_t(side * side), 0);
        for (int start = 0; start < side * side && found.size() < 12; ++start)
        {
            if (!blocked[start] || seen[start])
                continue;
            int minI = side, maxI = -1, minJ = side, maxJ = -1, count = 0;
            float heights = 0.0f;
            std::vector<int> open{ start };
            seen[start] = 1;
            while (!open.empty())
            {
                int const cell = open.back();
                open.pop_back();
                int const i = cell % side;
                int const j = cell / side;
                minI = std::min(minI, i);
                maxI = std::max(maxI, i);
                minJ = std::min(minJ, j);
                maxJ = std::max(maxJ, j);
                heights += floor[cell];
                ++count;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di)
                    {
                        int const ni = i + di;
                        int const nj = j + dj;
                        if (ni < 0 || nj < 0 || ni >= side || nj >= side)
                            continue;
                        int const next = index(ni, nj);
                        if (blocked[next] && !seen[next])
                        {
                            seen[next] = 1;
                            open.push_back(next);
                        }
                    }
            }
            float const across = float(std::max(maxI - minI, maxJ - minJ) + 1) * Step;
            if (count < 3 || across > 14.0f)
                continue;
            float const x = (pointX(minI) + pointX(maxI)) / 2.0f;
            float const y = (pointY(minJ) + pointY(maxJ)) / 2.0f;
            float const z = heights / float(count);
            float const radius = across / 2.0f;
            if (centre.GetExactDist2d(x, y) > reach - 3.0f)
                continue;
            float const h = z + 1.6f;
            float const d = radius + 2.5f;
            bool const crossX = !map->isInLineOfSight(x - d, y, h, x + d, y, h, phase, LINEOFSIGHT_CHECK_VMAP,
                                                      VMAP::ModelIgnoreFlags::Nothing);
            bool const crossY = !map->isInLineOfSight(x, y - d, h, x, y + d, h, phase, LINEOFSIGHT_CHECK_VMAP,
                                                      VMAP::ModelIgnoreFlags::Nothing);
            if (crossX || crossY)
                found.push_back({ x, y, z, radius });
        }
        return found;
    }

    // The cover of the bot's arena, found on first use.
    std::vector<Cover> CoverOf(Player* bot, Battleground* bg)
    {
        std::vector<Cover> cover;
        ArenaGround const* ground = GroundOf(bg->GetMapId());
        Position centre;
        if (!ground || !On("AiPlayerbot.CoaArenaCover") || !ArenaCentre(bg, centre))
            return cover;
        {
            std::lock_guard<std::mutex> lock(CoverLock);
            auto found = FoundCover.find(bg->GetMapId());
            if (found == FoundCover.end())
            {
                found = FoundCover.emplace(bg->GetMapId(), ScanCover(bot->GetMap(), bot->GetPhaseMask(), centre, ground->reach)).first;
                for (Cover const& pillar : found->second)
                    LOG_INFO("playerbots.coa", "coa arena cover: map {} pillar at {:.1f} {:.1f} {:.1f}, {:.1f} yards across",
                             bg->GetMapId(), pillar.x, pillar.y, pillar.z, pillar.radius * 2.0f);
                LOG_INFO("playerbots.coa", "coa arena cover: map {} has {} pillars", bg->GetMapId(), found->second.size());
            }
            cover = found->second;
        }
        for (KnownCover const& known : ObjectCover)
            if (known.map == bg->GetMapId())
                cover.push_back({ known.x, known.y, ground->floorZ, known.radius });
        return cover;
    }

    // Points just behind each pillar, seen from `from`.
    void BehindCover(std::vector<std::pair<float, float>>& points, std::vector<Cover> const& cover, Position const& from)
    {
        for (Cover const& pillar : cover)
        {
            float const away = std::atan2(pillar.y - from.GetPositionY(), pillar.x - from.GetPositionX());
            for (float turn : { 0.0f, 0.5f, -0.5f })
            {
                float const distance = pillar.radius + 2.0f;
                points.emplace_back(pillar.x + distance * std::cos(away + turn), pillar.y + distance * std::sin(away + turn));
            }
        }
    }

    // Base of the moving actions: a combat move, and the time it should take, during which the action stands back.
    class CoaPvpMoveAction : public MovementAction
    {
    public:
        CoaPvpMoveAction(PlayerbotAI* botAI, std::string const name) : MovementAction(botAI, name) {}

    protected:
        bool Go(float x, float y, float z, MovementPriority priority = MovementPriority::MOVEMENT_COMBAT)
        {
            float const distance = bot->GetExactDist2d(x, y);
            if (!MoveTo(bot->GetMapId(), x, y, z, false, false, false, false, priority))
                return false;
            float const speed = std::max(bot->GetSpeed(MOVE_RUN), 2.0f);
            busyUntil = getMSTime() + uint32(distance / speed * IN_MILLISECONDS * 0.8f);
            return true;
        }

        bool Busy() const { return getMSTime() < busyUntil && bot->isMoving(); }

        uint32 busyUntil = 0;
    };

    /*
     * Before the gates open. The bots stood on their start point for the whole preparation. Now each one takes a
     * place of its role between the start point and the gate (healers at the back, then ranged, melee at the front),
     * side by side with the others of its row, and faces the gate. The room's depth is the distance to the nearest
     * door of the arena, capped per arena; none on the Ring of Valor lifts. Buffs and pets come from "coa buff",
     * which runs once the bot stands still, and CoaArenaReady still marks the bot ready.
     */
    class CoaArenaPreparationAction : public CoaPvpMoveAction
    {
    public:
        CoaArenaPreparationAction(PlayerbotAI* botAI) : CoaPvpMoveAction(botAI, "coa arena preparation") {}

        bool isUseful() override
        {
            if (!On("AiPlayerbot.CoaArenaPreparation") || !IsCoaBot(bot) || !ArenaWaiting(bot) || !bot->IsAlive() ||
                bot->IsBeingTeleported() || bot->IsNonMeleeSpellCast(false) || Busy())
                return false;
            Plan();
            return planned && (bot->GetExactDist2d(spot.GetPositionX(), spot.GetPositionY()) > 1.2f || !faced);
        }

        bool Execute(Event /*event*/) override
        {
            if (!planned)
                return false;
            if (bot->GetExactDist2d(spot.GetPositionX(), spot.GetPositionY()) > 1.2f)
            {
                faced = false;
                return Go(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(), MovementPriority::MOVEMENT_NORMAL);
            }
            bot->SetFacingTo(facing);
            faced = true;
            return true;
        }

    private:
        // Planned again every few seconds: the rows change as the team arrives.
        void Plan()
        {
            uint32 const now = getMSTime();
            Battleground* bg = ArenaOf(bot);
            if (!bg || (planned && bg->GetInstanceID() == plannedFor && now - plannedAt < 4000))
                return;
            plannedAt = now;
            plannedFor = bg->GetInstanceID();
            planned = false;

            ArenaGround const* ground = GroundOf(bg->GetMapId());
            Position const* start = bg->GetTeamStartPosition(bot->GetBgTeamId());
            Position centre;
            if (!ground || !start || !ArenaCentre(bg, centre))
                return;

            // Towards the gate when the room has one near, else towards the centre.
            float angle = start->GetAbsoluteAngle(&centre);
            float depth = 0.0f;
            if (GameObject* door = NearestDoor(*start))
            {
                angle = start->GetAbsoluteAngle(door);
                depth = std::min(start->GetExactDist2d(door) - 2.5f, ground->roomDepth);
            }
            else
                depth = std::min(3.0f, ground->roomDepth);
            depth = std::max(depth, 0.0f);
            facing = angle;

            // The row of its role, and its place in the row among the team members of the same row.
            int const row = Row(bot);
            float const forward = depth * (row == 0 ? 0.2f : row == 1 ? 0.55f : 0.9f);
            std::vector<ObjectGuid> mates;
            for (auto const& [guid, player] : bg->GetPlayers())
                if (player && player->GetBgTeamId() == bot->GetBgTeamId() && Row(player) == row)
                    mates.push_back(guid);
            std::sort(mates.begin(), mates.end());
            int const index = int(std::find(mates.begin(), mates.end(), bot->GetGUID()) - mates.begin());
            float side = float((index + 1) / 2) * 2.5f * (index % 2 ? 1.0f : -1.0f);
            side = std::clamp(side, -5.0f, 5.0f);
            if (depth < 1.0f)
                side = 0.0f;

            for (float lateral : { side, 0.0f })
            {
                float const x = start->GetPositionX() + forward * std::cos(angle) - lateral * std::sin(angle);
                float const y = start->GetPositionY() + forward * std::sin(angle) + lateral * std::cos(angle);
                float z = start->GetPositionZ();
                if (!GroundAt(bot, x, y, z))
                    continue;
                // In the room: seen from the start point, and walked to straight.
                if (!bot->GetMap()->isInLineOfSight(start->GetPositionX(), start->GetPositionY(), start->GetPositionZ() + 1.6f,
                                                    x, y, z + 1.6f, bot->GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS,
                                                    VMAP::ModelIgnoreFlags::Nothing))
                    continue;
                if (bot->GetExactDist2d(x, y) > 1.2f && !Walkable(bot, x, y, z, bot->GetExactDist2d(x, y) * 1.5f + 3.0f))
                    continue;
                spot.Relocate(x, y, z);
                planned = true;
                return;
            }
            // Nowhere better: the start point itself, facing the gate.
            spot.Relocate(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
            planned = true;
        }

        static int Row(Player* player)
        {
            if (Healer(player))
                return 0;
            return Squishy(player) ? 1 : 2;
        }

        GameObject* NearestDoor(Position const& start)
        {
            std::list<GameObject*> found;
            Acore::GameObjectInRangeCheck check(start.GetPositionX(), start.GetPositionY(), start.GetPositionZ(), 30.0f);
            Acore::GameObjectListSearcher<Acore::GameObjectInRangeCheck> searcher(bot, found, check);
            Cell::VisitObjects(start.GetPositionX(), start.GetPositionY(), bot->GetMap(), searcher, 30.0f);
            GameObject* nearest = nullptr;
            for (GameObject* go : found)
                if (go->GetGoType() == GAMEOBJECT_TYPE_DOOR &&
                    (!nearest || start.GetExactDist2d(go) < start.GetExactDist2d(nearest)))
                    nearest = go;
            return nearest;
        }

        Position spot;
        float facing = 0.0f;
        uint32 plannedAt = 0;
        uint32 plannedFor = 0;
        bool planned = false;
        bool faced = false;
    };

    /*
     * Out of sight in an arena. A ranged bot whose target went behind a pillar used to walk onto the target's own
     * spot, into its melee. Now it steps to the nearest reachable point that sees the target from casting range,
     * away from enemy melee, or else joins a team member who sees it. A healer does the same for the most hurt team
     * member it cannot see, before the generic "reach party member to heal" walks it to that member.
     */
    class CoaArenaSightAction : public CoaPvpMoveAction
    {
    public:
        CoaArenaSightAction(PlayerbotAI* botAI) : CoaPvpMoveAction(botAI, "coa arena sight") {}

        bool isUseful() override
        {
            if (Busy() || !On("AiPlayerbot.CoaArenaLineOfSight") || !ArenaFight(bot) || !bot->IsAlive())
                return false;
            return Hidden() != nullptr;
        }

        bool Execute(Event /*event*/) override
        {
            Unit* target = Hidden();
            if (!target)
                return false;
            Position spot;
            if (!CoaPvp::FindSightSpot(bot, target, Healer(bot) ? 36.0f : 28.0f, spot))
                return false;
            return Go(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ());
        }

    private:
        Unit* Hidden()
        {
            Battleground* bg = ArenaFight(bot);
            if (!bg)
                return nullptr;
            if (Healer(bot))
            {
                Player* worst = nullptr;
                Fighters(bg, bot, false, [&](Player* ally)
                {
                    if (ally->GetHealthPct() < 85.0f && bot->IsWithinDist(ally, 60.0f) &&
                        (!worst || ally->GetHealthPct() < worst->GetHealthPct()))
                        worst = ally;
                });
                if (worst && !bot->IsWithinLOSInMap(worst))
                    return worst;
                if (worst || bot->GetHealthPct() < 85.0f)
                    return nullptr;
            }
            Unit* victim = bot->GetVictim();
            if (victim && victim->IsAlive() && !bot->IsWithinLOSInMap(victim))
                return victim;
            return nullptr;
        }
    };

    // Whether the bot can move on its own at all.
    bool Held(Player* bot)
    {
        return bot->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING);
    }

    // Bonus for a point next to a pillar: around it a melee loses its target and a caster its line.
    float NextToCover(std::vector<Cover> const& cover, float x, float y)
    {
        for (Cover const& pillar : cover)
            if (std::hypot(pillar.x - x, pillar.y - y) < pillar.radius + 3.5f)
                return -3.0f;
        return 0.0f;
    }

    /*
     * Kiting. A CoA ranged bot or healer had no reason to leave an enemy melee: "coa ranged" is built on the plain
     * combat strategy, without the "enemy too close for spell" flee of the ranged one. In an arena it now steps
     * 7 to 11 yards away from the melee on it, rather towards a pillar, while staying within casting range and
     * sight of what it casts on (its target, or for a healer the most hurt team member). Never for long: after
     * 5 seconds of steps it stands and fights for 6 seconds. Dungeons are not concerned.
     */
    class CoaArenaKiteAction : public CoaPvpMoveAction
    {
    public:
        CoaArenaKiteAction(PlayerbotAI* botAI) : CoaPvpMoveAction(botAI, "coa arena kite") {}

        bool isUseful() override
        {
            if (Busy() || getMSTime() < restUntil || !On("AiPlayerbot.CoaArenaKite") || !bot->IsAlive() ||
                !Squishy(bot) || Held(bot))
                return false;
            Battleground* bg = ArenaFight(bot);
            return bg && Chaser(bg);
        }

        bool Execute(Event /*event*/) override
        {
            Battleground* bg = ArenaFight(bot);
            Player* chaser = bg ? Chaser(bg) : nullptr;
            if (!chaser)
                return false;

            uint32 const now = getMSTime();
            if (!streakStart || now - lastStep > 2500)
                streakStart = now;
            if (now - streakStart > 5000)
            {
                restUntil = now + 6000;
                streakStart = 0;
                return false;
            }

            Unit* anchor = Anchor(bg, chaser);
            std::vector<Cover> const cover = CoverOf(bot, bg);
            std::vector<std::pair<float, float>> points;
            Rings(points, bot->GetPositionX(), bot->GetPositionY(), { 7.0f, 11.0f }, 12);
            BehindCover(points, cover, chaser->GetPosition());
            float const gap = bot->GetExactDist2d(chaser);
            auto score = [&](float x, float y, float z)
            {
                if (bot->GetExactDist2d(x, y) > 15.0f || !InArenaBounds(bg, x, y))
                    return Rejected;
                float const away = chaser->GetExactDist2d(x, y);
                if (away < gap + 3.0f)
                    return Rejected;
                float value = -away;
                if (anchor)
                {
                    float const reach = anchor->GetExactDist2d(x, y);
                    if (reach > 30.0f)
                        value += 10.0f + (reach - 30.0f) * 2.0f;
                    if (!anchor->IsWithinLOS(x, y, z))
                        value += 8.0f;
                }
                if (MeleeEnemyNear(bg, bot, x, y, 6.0f))
                    value += 20.0f;
                return value + NextToCover(cover, x, y);
            };
            Spot best;
            if (!BestSpot(bot, points, score, 20.0f, best))
                return false;
            lastStep = now;
            return Go(best.x, best.y, best.z);
        }

    private:
        // The nearest enemy melee on the bot, or any one in its face.
        Player* Chaser(Battleground* bg)
        {
            Player* chaser = nullptr;
            Fighters(bg, bot, true, [&](Player* enemy)
            {
                if (Squishy(enemy))
                    return;
                float const distance = bot->GetExactDist2d(enemy);
                if ((distance < 7.0f && enemy->GetVictim() == bot) || distance < 4.0f)
                    if (!chaser || distance < bot->GetExactDist2d(chaser))
                        chaser = enemy;
            });
            return chaser;
        }

        Unit* Anchor(Battleground* bg, Player* chaser)
        {
            if (!Healer(bot))
            {
                Unit* victim = bot->GetVictim();
                return victim && victim != chaser ? victim : nullptr;
            }
            Player* worst = nullptr;
            Fighters(bg, bot, false, [&](Player* ally)
            {
                if (!worst || ally->GetHealthPct() < worst->GetHealthPct())
                    worst = ally;
            });
            return worst;
        }

        uint32 streakStart = 0;
        uint32 lastStep = 0;
        uint32 restUntil = 0;
    };

    // An enemy aims at this player: in melee with it, or casting at it.
    bool Aims(Player* enemy, Player* target)
    {
        return enemy->GetVictim() == target || enemy->GetTarget() == target->GetGUID();
    }

    /*
     * Focus. A damage dealer kept whatever "dps assist" handed it. In an arena a CoA damage dealer now goes for the
     * enemy healer it can see within reach (35 yards from range, 25 in melee), else for the most hurt enemy it sees.
     * It finishes a target under 25%, does not leave a target for one barely more hurt, and switches at most every
     * 4 seconds. While it holds an enemy player, "dps assist" waits (CoaArenaFocusMultiplier).
     */
    class CoaArenaFocusAction : public AttackAction
    {
    public:
        CoaArenaFocusAction(PlayerbotAI* botAI) : AttackAction(botAI, "coa arena focus") {}

        bool isUseful() override
        {
            if (getMSTime() - lastSwitch < 4000 || !On("AiPlayerbot.CoaArenaFocus") || !bot->IsAlive() || Healer(bot) ||
                Tank(bot))
                return false;
            Battleground* bg = ArenaFight(bot);
            Player* pick = bg ? Pick(bg) : nullptr;
            return pick && pick != bot->GetVictim();
        }

        bool Execute(Event /*event*/) override
        {
            Battleground* bg = ArenaFight(bot);
            Player* pick = bg ? Pick(bg) : nullptr;
            if (!pick || pick == bot->GetVictim())
                return false;
            lastSwitch = getMSTime();
            return Attack(pick);
        }

    private:
        Player* Pick(Battleground* bg)
        {
            float const reach = Squishy(bot) ? 35.0f : 25.0f;
            Player* healer = nullptr;
            Player* weakest = nullptr;
            Fighters(bg, bot, true, [&](Player* enemy)
            {
                if (!bot->IsWithinDist(enemy, reach) || !bot->IsWithinLOSInMap(enemy))
                    return;
                if (Healer(enemy) && (!healer || bot->GetExactDist2d(enemy) < bot->GetExactDist2d(healer)))
                    healer = enemy;
                if (!weakest || enemy->GetHealthPct() < weakest->GetHealthPct())
                    weakest = enemy;
            });

            Unit* victim = bot->GetVictim();
            Player* held = victim && victim->IsAlive() ? victim->ToPlayer() : nullptr;
            bool const heldEnemy = held && held->GetBgTeamId() != bot->GetBgTeamId() && bot->IsWithinLOSInMap(held);
            if (heldEnemy && held->GetHealthPct() < 25.0f)
                return held;
            if (healer)
                return healer;
            if (heldEnemy && weakest && weakest->GetHealthPct() > held->GetHealthPct() - 15.0f)
                return held;
            return weakest;
        }

        uint32 lastSwitch = 0;
    };

    class CoaArenaFocusMultiplier : public Multiplier
    {
    public:
        CoaArenaFocusMultiplier(PlayerbotAI* botAI) : Multiplier(botAI, "coa arena focus") {}

        float GetValue(Action* action) override
        {
            if (!action || action->getName() != "dps assist" || !bot->InArena())
                return 1.0f;
            if (!IsCoaBot(bot) || Healer(bot) || Tank(bot) || !On("AiPlayerbot.CoaArenaFocus") || !ArenaFight(bot))
                return 1.0f;
            Unit* victim = bot->GetVictim();
            Player* held = victim && victim->IsAlive() ? victim->ToPlayer() : nullptr;
            return held && held->GetBgTeamId() != bot->GetBgTeamId() ? 0.0f : 1.0f;
        }
    };

    /*
     * Peel. A tank, or a melee damage dealer once the healer drops under 60%, turns on the enemy that hits its
     * team's healer, and puts its crowd control on it (every 12 seconds at most).
     */
    class CoaArenaPeelAction : public AttackAction
    {
    public:
        CoaArenaPeelAction(PlayerbotAI* botAI) : AttackAction(botAI, "coa arena peel") {}

        bool isUseful() override
        {
            if (!On("AiPlayerbot.CoaArenaPeel") || !bot->IsAlive() || Healer(bot) || (Squishy(bot) && !Tank(bot)))
                return false;
            Battleground* bg = ArenaFight(bot);
            Player* threat = bg ? Threat(bg) : nullptr;
            if (!threat)
                return false;
            return threat != bot->GetVictim() ||
                   (getMSTime() >= nextControl && bot->IsWithinDist(threat, 20.0f) && CoaHasReadyControl(botAI, bot));
        }

        bool Execute(Event /*event*/) override
        {
            Battleground* bg = ArenaFight(bot);
            Player* threat = bg ? Threat(bg) : nullptr;
            if (!threat)
                return false;
            if (threat != bot->GetVictim())
                return Attack(threat);
            uint32 const now = getMSTime();
            if (now < nextControl || !CoaCastControl(botAI, bot, threat))
                return false;
            nextControl = now + 12000;
            return true;
        }

    private:
        Player* Threat(Battleground* bg)
        {
            Player* threat = nullptr;
            Fighters(bg, bot, false, [&](Player* healer)
            {
                if (!Healer(healer) || (!Tank(bot) && healer->GetHealthPct() >= 60.0f))
                    return;
                Fighters(bg, bot, true, [&](Player* enemy)
                {
                    if (!Healer(enemy) && Aims(enemy, healer) && bot->IsWithinDist(enemy, 30.0f) &&
                        (!threat || healer->GetExactDist2d(enemy) < healer->GetExactDist2d(threat)))
                        threat = enemy;
                });
            });
            return threat;
        }

        uint32 nextControl = 0;
    };

    /*
     * Under pressure. A CoA ranged bot or healer under 45% that enemies aim at gets out of the line of the casters
     * among them: behind a pillar, or any reachable point near it none of them sees, preferably one its own healer
     * still sees. With only melee on it, it falls back behind its tank or melee team mate. 6 seconds at most, then
     * 10 seconds before it hides again.
     */
    class CoaArenaPressureAction : public CoaPvpMoveAction
    {
    public:
        CoaArenaPressureAction(PlayerbotAI* botAI) : CoaPvpMoveAction(botAI, "coa arena pressure") {}

        bool isUseful() override
        {
            if (Busy() || getMSTime() < restUntil || !On("AiPlayerbot.CoaArenaPressure") || !bot->IsAlive() ||
                !Squishy(bot) || bot->GetHealthPct() >= 45.0f || Held(bot))
                return false;
            Battleground* bg = ArenaFight(bot);
            return bg && !Threats(bg).empty();
        }

        bool Execute(Event /*event*/) override
        {
            Battleground* bg = ArenaFight(bot);
            if (!bg)
                return false;
            std::vector<Player*> const threats = Threats(bg);
            if (threats.empty())
                return false;

            uint32 const now = getMSTime();
            if (!streakStart || now - lastStep > 3000)
                streakStart = now;
            if (now - streakStart > 6000)
            {
                restUntil = now + 10000;
                streakStart = 0;
                return false;
            }

            std::vector<Player*> casters;
            for (Player* threat : threats)
                if (Squishy(threat))
                    casters.push_back(threat);

            Spot best;
            if (!casters.empty() && Hide(bg, threats, casters, best))
            {
                lastStep = now;
                return Go(best.x, best.y, best.z);
            }
            if (FallBack(bg, threats, best))
            {
                lastStep = now;
                return Go(best.x, best.y, best.z);
            }
            return false;
        }

    private:
        std::vector<Player*> Threats(Battleground* bg)
        {
            std::vector<Player*> threats;
            Fighters(bg, bot, true, [&](Player* enemy)
            {
                if (Aims(enemy, bot) && bot->IsWithinDist(enemy, 40.0f))
                    threats.push_back(enemy);
            });
            return threats;
        }

        bool Hide(Battleground* bg, std::vector<Player*> const& threats, std::vector<Player*> const& casters, Spot& best)
        {
            Position from;
            float x = 0.0f, y = 0.0f;
            for (Player* caster : casters)
            {
                x += caster->GetPositionX();
                y += caster->GetPositionY();
            }
            from.Relocate(x / float(casters.size()), y / float(casters.size()), bot->GetPositionZ());

            std::vector<std::pair<float, float>> points;
            BehindCover(points, CoverOf(bot, bg), from);
            Rings(points, bot->GetPositionX(), bot->GetPositionY(), { 6.0f, 10.0f, 14.0f }, 12);
            std::vector<Player*> healers;
            if (!Healer(bot))
                Fighters(bg, bot, false, [&](Player* ally) { if (Healer(ally)) healers.push_back(ally); });

            auto score = [&](float px, float py, float pz)
            {
                float const move = bot->GetExactDist2d(px, py);
                if (move > 18.0f || !InArenaBounds(bg, px, py))
                    return Rejected;
                for (Player* caster : casters)
                    if (caster->IsWithinLOS(px, py, pz))
                        return Rejected;
                float value = move;
                for (Player* threat : threats)
                    if (!Squishy(threat) && threat->GetExactDist2d(px, py) < 5.0f)
                        value += 12.0f;
                for (Player* healer : healers)
                    if (healer->IsWithinDist2d(px, py, 36.0f) && healer->IsWithinLOS(px, py, pz))
                    {
                        value -= 4.0f;
                        break;
                    }
                return value;
            };
            return BestSpot(bot, points, score, 26.0f, best);
        }

        // Behind the nearest tank or melee team mate, on its far side from the nearest threat.
        bool FallBack(Battleground* bg, std::vector<Player*> const& threats, Spot& best)
        {
            Player* nearestThreat = nullptr;
            for (Player* threat : threats)
                if (!nearestThreat || bot->GetExactDist2d(threat) < bot->GetExactDist2d(nearestThreat))
                    nearestThreat = threat;
            Player* guard = nullptr;
            Fighters(bg, bot, false, [&](Player* ally)
            {
                if (!Squishy(ally) && (!guard || bot->GetExactDist2d(ally) < bot->GetExactDist2d(guard)))
                    guard = ally;
            });
            if (!guard || !nearestThreat || bot->GetExactDist2d(guard) > 30.0f)
                return false;
            float const angle = nearestThreat->GetAbsoluteAngle(guard);
            float const x = guard->GetPositionX() + 3.0f * std::cos(angle);
            float const y = guard->GetPositionY() + 3.0f * std::sin(angle);
            float z = guard->GetPositionZ();
            if (!GroundAt(bot, x, y, z) || !Walkable(bot, x, y, z, 40.0f))
                return false;
            best = { x, y, z, 0.0f };
            return true;
        }

        uint32 streakStart = 0;
        uint32 lastStep = 0;
        uint32 restUntil = 0;
    };

    // The duel spell's flag, which the duellists must not get too far from.
    GameObject* DuelFlag(Player* bot)
    {
        return bot->GetMap()->GetGameObject(bot->GetGuidValue(PLAYER_DUEL_ARBITER));
    }

    Player* DuelFoe(Player* bot, DuelState state)
    {
        if (!bot->duel || bot->duel->State != state)
            return nullptr;
        Player* foe = bot->duel->Opponent;
        return foe && foe->IsInWorld() && foe->IsAlive() && foe->GetMap() == bot->GetMap() ? foe : nullptr;
    }

    /*
     * Duel opening. During the three seconds of the duel countdown both bots stood where the challenge left them.
     * Now a melee bot closes in to 3.5 yards and a ranged bot or healer steps back to 20 yards, in sight of the
     * opponent and near the flag; then it faces the opponent and refreshes its buffs. A duellist whose AI is held
     * (the .coapvp bench holds the challenged one) does not move, and the bench still starts the fight itself.
     */
    class CoaDuelOpeningAction : public CoaPvpMoveAction
    {
    public:
        CoaDuelOpeningAction(PlayerbotAI* botAI) : CoaPvpMoveAction(botAI, "coa duel opening") {}

        bool isUseful() override
        {
            if (Busy() || !On("AiPlayerbot.CoaDuelOpening") || !IsCoaBot(bot) || !bot->IsAlive() ||
                bot->IsNonMeleeSpellCast(false))
                return false;
            Player* foe = DuelFoe(bot, DUEL_STATE_COUNTDOWN);
            return foe && (!InPlace(foe) || !bot->HasInArc(0.4f, foe) || buffedFor != foe->GetGUID());
        }

        bool Execute(Event /*event*/) override
        {
            Player* foe = DuelFoe(bot, DUEL_STATE_COUNTDOWN);
            if (!foe)
                return false;
            if (!InPlace(foe) && Step(foe))
                return true;
            if (!bot->HasInArc(0.4f, foe))
            {
                bot->SetFacingToObject(foe);
                return true;
            }
            if (buffedFor != foe->GetGUID())
            {
                buffedFor = foe->GetGUID();
                botAI->DoSpecificAction("coa buff", Event(), true);
                return true;
            }
            return false;
        }

    private:
        bool InPlace(Player* foe)
        {
            if (stuckFor == foe->GetGUID())
                return true;
            float const distance = bot->GetExactDist2d(foe);
            return Squishy(bot) ? distance >= 16.0f && distance <= 28.0f : distance <= 5.0f;
        }

        bool Step(Player* foe)
        {
            float const want = Squishy(bot) ? 20.0f : 3.5f;
            float const base = foe->GetAbsoluteAngle(bot);
            GameObject* flag = DuelFlag(bot);
            for (float turn : { 0.0f, 0.4f, -0.4f, 0.8f, -0.8f })
            {
                float const x = foe->GetPositionX() + want * std::cos(base + turn);
                float const y = foe->GetPositionY() + want * std::sin(base + turn);
                float z = bot->GetPositionZ();
                if (!GroundAt(bot, x, y, z) || !foe->IsWithinLOS(x, y, z) || (flag && flag->GetExactDist2d(x, y) > 28.0f))
                    continue;
                if (!Walkable(bot, x, y, z, bot->GetExactDist2d(x, y) * 1.5f + 4.0f))
                    continue;
                if (Go(x, y, z, MovementPriority::MOVEMENT_NORMAL))
                    return true;
            }
            stuckFor = foe->GetGUID();
            return false;
        }

        ObjectGuid buffedFor;
        ObjectGuid stuckFor;
    };

    // In a fight against players: a battleground or arena, or a duel under way.
    bool InPvp(Player* bot)
    {
        return bot->InBattleground() || (bot->duel && bot->duel->State == DUEL_STATE_IN_PROGRESS);
    }

    // The enemy players of the bot's battleground or arena, or its duel opponent.
    template <typename Visit>
    void Foes(Player* bot, Visit visit)
    {
        if (Battleground* bg = bot->GetBattleground())
            Fighters(bg, bot, true, visit);
        else if (Player* foe = DuelFoe(bot, DUEL_STATE_IN_PROGRESS))
            visit(foe);
    }

    bool Controlled(Unit* unit)
    {
        return unit->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING);
    }

    /*
     * Crowd control, run, recover. A CoA bot under 40% with an enemy player in its face, in any PvP fight, puts its
     * crowd control on it (every 20 seconds at most), then runs 9 to 14 yards away for up to 4 seconds, out of the
     * enemy's sight when it can (behind a pillar in an arena, near the flag in a duel). The bandage below then
     * gives it back some health while the enemy is held.
     */
    class CoaPvpRecoverAction : public CoaPvpMoveAction
    {
    public:
        CoaPvpRecoverAction(PlayerbotAI* botAI) : CoaPvpMoveAction(botAI, "coa pvp recover") {}

        bool isUseful() override
        {
            if (Busy() || !On("AiPlayerbot.CoaPvpRecovery") || !IsCoaBot(bot) || !bot->IsAlive() || !InPvp(bot) || Held(bot))
                return false;
            uint32 const now = getMSTime();
            if (now < escapeUntil && HeldFoe())
                return true;
            return bot->GetHealthPct() < 40.0f && now >= nextControl && Attacker() && CoaHasReadyControl(botAI, bot);
        }

        bool Execute(Event /*event*/) override
        {
            uint32 const now = getMSTime();
            if (now < escapeUntil)
            {
                if (Player* foe = HeldFoe())
                    return Run(foe);
                escapeUntil = 0;
            }
            Player* attacker = Attacker();
            if (!attacker)
                return false;
            if (!CoaCastControl(botAI, bot, attacker))
            {
                nextControl = now + 2000;
                return false;
            }
            held = attacker->GetGUID();
            escapeUntil = now + 4000;
            nextControl = now + 20000;
            return true;
        }

    private:
        Player* Attacker()
        {
            Player* attacker = nullptr;
            Foes(bot, [&](Player* foe)
            {
                if (Aims(foe, bot) && bot->GetExactDist2d(foe) < 8.0f &&
                    (!attacker || bot->GetExactDist2d(foe) < bot->GetExactDist2d(attacker)))
                    attacker = foe;
            });
            return attacker;
        }

        // The enemy it holds, while still close enough to run from.
        Player* HeldFoe()
        {
            Player* foe = ObjectAccessor::GetPlayer(*bot, held);
            return foe && foe->IsAlive() && foe->GetMap() == bot->GetMap() && bot->GetExactDist2d(foe) < 15.0f ? foe : nullptr;
        }

        bool Run(Player* foe)
        {
            Battleground* bg = ArenaFight(bot);
            GameObject* flag = bot->duel ? DuelFlag(bot) : nullptr;
            std::vector<std::pair<float, float>> points;
            Rings(points, bot->GetPositionX(), bot->GetPositionY(), { 9.0f, 14.0f }, 12);
            if (bg)
                BehindCover(points, CoverOf(bot, bg), foe->GetPosition());
            float const gap = bot->GetExactDist2d(foe);
            auto score = [&](float x, float y, float z)
            {
                float const away = foe->GetExactDist2d(x, y);
                if (away < gap + 4.0f || bot->GetExactDist2d(x, y) > 18.0f || (bg && !InArenaBounds(bg, x, y)) ||
                    (flag && flag->GetExactDist2d(x, y) > 28.0f))
                    return Rejected;
                return -away + (foe->IsWithinLOS(x, y, z) ? 6.0f : 0.0f);
            };
            Spot best;
            return BestSpot(bot, points, score, 22.0f, best) && Go(best.x, best.y, best.z);
        }

        ObjectGuid held;
        uint32 escapeUntil = 0;
        uint32 nextControl = 0;
    };

    /*
     * Bandage. In a PvP fight a CoA bot under 65% that carries a bandage it has the First Aid for, and that no free
     * enemy can hit (every enemy held, out of sight, or far), bandages itself: never while Recently Bandaged, a
     * damage over time runs on it, it moves, casts or eats. Eating and drinking between fights stay with the
     * "food" strategy.
     */
    class CoaPvpBandageAction : public Action
    {
    public:
        CoaPvpBandageAction(PlayerbotAI* botAI) : Action(botAI, "coa pvp bandage") {}

        bool isUseful() override
        {
            if (getMSTime() < noBandageUntil || !On("AiPlayerbot.CoaPvpBandage") || !IsCoaBot(bot) || !bot->IsAlive() ||
                !InPvp(bot) || bot->GetHealthPct() >= 65.0f || bot->HasAura(RecentlyBandaged) || bot->isMoving() ||
                bot->IsNonMeleeSpellCast(false) || bot->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) ||
                bot->HasAuraType(SPELL_AURA_MOD_REGEN) || bot->HasAuraType(SPELL_AURA_MOD_POWER_REGEN) || Held(bot))
                return false;
            return !Threatened() && Bandage();
        }

        bool Execute(Event /*event*/) override
        {
            Item* bandage = Bandage();
            if (!bandage)
                return false;
            botAI->ImbueItem(bandage, bot);
            return true;
        }

    private:
        static constexpr uint32 RecentlyBandaged = 11196;

        bool Threatened()
        {
            bool threatened = false;
            Foes(bot, [&](Player* foe)
            {
                if (threatened || Controlled(foe))
                    return;
                float const distance = bot->GetExactDist2d(foe);
                if ((distance < 15.0f || (Squishy(foe) && distance < 40.0f)) && foe->IsWithinLOSInMap(bot))
                    threatened = true;
            });
            return threatened;
        }

        Item* Bandage()
        {
            Item* item = botAI->FindBandage();
            ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
            if (!proto || (proto->RequiredSkill && bot->GetSkillValue(proto->RequiredSkill) < proto->RequiredSkillRank))
            {
                noBandageUntil = getMSTime() + 60000;
                return nullptr;
            }
            return item;
        }

        uint32 noBandageUntil = 0;
    };

    // The triggers only check the place, the switch and the role, each a cheap test; the action decides the rest.
    using Gate = bool (*)(Player* bot);

    class CoaPvpTrigger : public Trigger
    {
    public:
        CoaPvpTrigger(PlayerbotAI* botAI, std::string const name, int32 interval, Gate gate)
            : Trigger(botAI, name, interval), gate(gate)
        {
        }

        bool IsActive() override { return gate(bot); }

    private:
        Gate gate;
    };

    bool PreparationGate(Player* bot)
    {
        return ArenaWaiting(bot) && IsCoaBot(bot) && On("AiPlayerbot.CoaArenaPreparation");
    }

    bool SightGate(Player* bot)
    {
        return bot->InArena() && IsCoaBot(bot) && Squishy(bot) && On("AiPlayerbot.CoaArenaLineOfSight");
    }

    // A bot about to win stays on its kill: no stepping back from a target nearly dead, or lower than itself.
    bool Winning(Player* bot)
    {
        Unit* target = bot->GetVictim();
        if (!target)
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (!target || !target->IsAlive())
            return false;
        return target->GetHealthPct() < 35.0f || target->GetHealthPct() + 10.0f < bot->GetHealthPct();
    }

    bool KiteGate(Player* bot)
    {
        return bot->InArena() && IsCoaBot(bot) && Squishy(bot) && On("AiPlayerbot.CoaArenaKite") && !Winning(bot);
    }

    bool FocusGate(Player* bot)
    {
        return bot->InArena() && IsCoaBot(bot) && !Healer(bot) && !Tank(bot) && On("AiPlayerbot.CoaArenaFocus");
    }

    bool PeelGate(Player* bot)
    {
        return bot->InArena() && IsCoaBot(bot) && !Healer(bot) && On("AiPlayerbot.CoaArenaPeel");
    }

    bool PressureGate(Player* bot)
    {
        return bot->InArena() && bot->GetHealthPct() < 45.0f && IsCoaBot(bot) && Squishy(bot) &&
               On("AiPlayerbot.CoaArenaPressure") && !Winning(bot);
    }

    bool DuelGate(Player* bot)
    {
        return bot->duel && bot->duel->State == DUEL_STATE_COUNTDOWN && IsCoaBot(bot) && On("AiPlayerbot.CoaDuelOpening");
    }

    bool RecoverGate(Player* bot)
    {
        return InPvp(bot) && bot->GetHealthPct() < 45.0f && IsCoaBot(bot) && On("AiPlayerbot.CoaPvpRecovery") &&
               !Winning(bot);
    }

    bool BandageGate(Player* bot)
    {
        return InPvp(bot) && bot->GetHealthPct() < 65.0f && IsCoaBot(bot) && On("AiPlayerbot.CoaPvpBandage");
    }

    class CoaPvpStrategy : public Strategy
    {
    public:
        CoaPvpStrategy(PlayerbotAI* botAI) : Strategy(botAI) {}

        std::string const getName() override { return "coa pvp"; }

        void InitTriggers(std::vector<TriggerNode*>& triggers) override
        {
            // Just above "coa buff": into place first, buffs once standing.
            triggers.push_back(new TriggerNode("coa arena preparation", { NextAction("coa arena preparation", ACTION_NORMAL + 6) }));
            // Above "attack duel opponent" (70).
            triggers.push_back(new TriggerNode("coa duel countdown", { NextAction("coa duel opening", 75.0f) }));
            // The combat moves come above the attacks, which reach 88 in the authored rotations: an action of
            // this strategy only runs when it has a reason to, and then only for one tick.
            triggers.push_back(new TriggerNode("coa arena sight", { NextAction("coa arena sight", 89.5f) }));
            triggers.push_back(new TriggerNode("coa arena kite", { NextAction("coa arena kite", 92.0f) }));
            triggers.push_back(new TriggerNode("coa arena focus", { NextAction("coa arena focus", 90.5f) }));
            triggers.push_back(new TriggerNode("coa arena peel", { NextAction("coa arena peel", 91.0f) }));
            triggers.push_back(new TriggerNode("coa arena pressure", { NextAction("coa arena pressure", 93.0f) }));
            triggers.push_back(new TriggerNode("coa pvp cornered", { NextAction("coa pvp recover", 94.0f) }));
            triggers.push_back(new TriggerNode("coa pvp hurt", { NextAction("coa pvp bandage", 90.0f) }));
        }

        void InitMultipliers(std::vector<Multiplier*>& multipliers) override
        {
            multipliers.push_back(new CoaArenaFocusMultiplier(botAI));
        }
    };

    class CoaPvpStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        CoaPvpStrategyContext() : NamedObjectContext<Strategy>(false, false) { creators["coa pvp"] = &pvp; }

    private:
        static Strategy* pvp(PlayerbotAI* botAI) { return new CoaPvpStrategy(botAI); }
    };

    class CoaPvpActionContext : public NamedObjectContext<Action>
    {
    public:
        CoaPvpActionContext() : NamedObjectContext<Action>(false, false)
        {
            creators["coa arena preparation"] = &preparation;
            creators["coa arena sight"] = &sight;
            creators["coa arena kite"] = &kite;
            creators["coa arena focus"] = &focus;
            creators["coa arena peel"] = &peel;
            creators["coa arena pressure"] = &pressure;
            creators["coa duel opening"] = &duel;
            creators["coa pvp recover"] = &recover;
            creators["coa pvp bandage"] = &bandage;
        }

    private:
        static Action* recover(PlayerbotAI* botAI) { return new CoaPvpRecoverAction(botAI); }
        static Action* bandage(PlayerbotAI* botAI) { return new CoaPvpBandageAction(botAI); }
        static Action* duel(PlayerbotAI* botAI) { return new CoaDuelOpeningAction(botAI); }
        static Action* focus(PlayerbotAI* botAI) { return new CoaArenaFocusAction(botAI); }
        static Action* peel(PlayerbotAI* botAI) { return new CoaArenaPeelAction(botAI); }
        static Action* pressure(PlayerbotAI* botAI) { return new CoaArenaPressureAction(botAI); }
        static Action* kite(PlayerbotAI* botAI) { return new CoaArenaKiteAction(botAI); }
        static Action* preparation(PlayerbotAI* botAI) { return new CoaArenaPreparationAction(botAI); }
        static Action* sight(PlayerbotAI* botAI) { return new CoaArenaSightAction(botAI); }
    };

    class CoaPvpTriggerContext : public NamedObjectContext<Trigger>
    {
    public:
        CoaPvpTriggerContext() : NamedObjectContext<Trigger>(false, false)
        {
            creators["coa arena preparation"] = &preparation;
            creators["coa arena sight"] = &sight;
            creators["coa arena kite"] = &kite;
            creators["coa arena focus"] = &focus;
            creators["coa arena peel"] = &peel;
            creators["coa arena pressure"] = &pressure;
            creators["coa duel countdown"] = &duel;
            creators["coa pvp cornered"] = &cornered;
            creators["coa pvp hurt"] = &hurt;
        }

    private:
        static Trigger* cornered(PlayerbotAI* botAI) { return new CoaPvpTrigger(botAI, "coa pvp cornered", 300, &RecoverGate); }
        static Trigger* hurt(PlayerbotAI* botAI) { return new CoaPvpTrigger(botAI, "coa pvp hurt", 1000, &BandageGate); }
        static Trigger* duel(PlayerbotAI* botAI) { return new CoaPvpTrigger(botAI, "coa duel countdown", 250, &DuelGate); }
        static Trigger* focus(PlayerbotAI* botAI) { return new CoaPvpTrigger(botAI, "coa arena focus", 1000, &FocusGate); }
        static Trigger* peel(PlayerbotAI* botAI) { return new CoaPvpTrigger(botAI, "coa arena peel", 500, &PeelGate); }
        static Trigger* pressure(PlayerbotAI* botAI)
        {
            return new CoaPvpTrigger(botAI, "coa arena pressure", 500, &PressureGate);
        }
        static Trigger* kite(PlayerbotAI* botAI) { return new CoaPvpTrigger(botAI, "coa arena kite", 300, &KiteGate); }
        static Trigger* preparation(PlayerbotAI* botAI)
        {
            return new CoaPvpTrigger(botAI, "coa arena preparation", 1000, &PreparationGate);
        }
        static Trigger* sight(PlayerbotAI* botAI) { return new CoaPvpTrigger(botAI, "coa arena sight", 500, &SightGate); }
    };
}

NamedObjectContext<Strategy>* NewCoaPvpStrategyContext() { return new CoaPvpStrategyContext(); }
NamedObjectContext<Action>* NewCoaPvpActionContext() { return new CoaPvpActionContext(); }
NamedObjectContext<Trigger>* NewCoaPvpTriggerContext() { return new CoaPvpTriggerContext(); }

uint32 CoaPvp::ArenaSpot(Player* bot)
{
    if (!IsCoaBot(bot) || !On("AiPlayerbot.CoaArenaRoleSpot"))
        return 0;
    if (Healer(bot))
        return 3;
    if (Squishy(bot))
        return 9;
    return 6;
}

bool CoaPvp::HandlesLineOfSight(Player* bot)
{
    return IsCoaBot(bot) && Squishy(bot) && On("AiPlayerbot.CoaArenaLineOfSight");
}

bool CoaPvp::FindSightSpot(Player* bot, Unit* target, float range, Position& spot)
{
    Battleground* bg = ArenaOf(bot);
    if (!bg || !target)
        return false;

    std::vector<std::pair<float, float>> points;
    Rings(points, bot->GetPositionX(), bot->GetPositionY(), { 4.0f, 8.0f, 12.0f, 17.0f }, 12);
    auto score = [&](float x, float y, float z)
    {
        float const toTarget = target->GetExactDist2d(x, y);
        if (toTarget > range || !InArenaBounds(bg, x, y) || !target->IsWithinLOS(x, y, z))
            return Rejected;
        float value = bot->GetExactDist2d(x, y);
        if (MeleeEnemyNear(bg, bot, x, y, 7.0f))
            value += 15.0f;
        return value;
    };
    Spot best;
    if (BestSpot(bot, points, score, 28.0f, best))
    {
        spot.Relocate(best.x, best.y, best.z);
        return true;
    }

    // A team member who sees the target from range: next to it.
    Player* guide = nullptr;
    Fighters(bg, bot, false, [&](Player* ally)
    {
        if (ally->IsWithinDist(target, range) && ally->IsWithinLOSInMap(target) &&
            (!guide || bot->GetExactDist2d(ally) < bot->GetExactDist2d(guide)))
            guide = ally;
    });
    if (!guide)
        return false;
    float const angle = guide->GetAbsoluteAngle(bot);
    float x = guide->GetPositionX() + 2.0f * std::cos(angle);
    float y = guide->GetPositionY() + 2.0f * std::sin(angle);
    float z = guide->GetPositionZ();
    if (!GroundAt(bot, x, y, z))
    {
        x = guide->GetPositionX();
        y = guide->GetPositionY();
        z = guide->GetPositionZ();
    }
    spot.Relocate(x, y, z);
    return true;
}

void CoaPvpOptionsReload()
{
    std::lock_guard<std::mutex> lock(OptionsLock);
    Options.clear();
}
