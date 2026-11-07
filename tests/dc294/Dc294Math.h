// Dungeon Clear 2.9.4 (coa/2.9.4 @ 0590afb): the pure geometry the module's party combat guards took over, copied
// verbatim as their non-regression ORACLE (Util/DungeonClearMath.h/.cpp, Util/DcEngageGeometry.cpp AggroReachYards,
// Data/RoomAggroRegistry.cpp rows + Find/SkirtOverride). Only minimal stand-ins for G3D::Vector3 / Position /
// the registry's row type replace the core headers so it compiles alone. Never edit the copied bodies.
#ifndef DC294_MATH_H
#define DC294_MATH_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
using uint32 = std::uint32_t;
namespace G3D
{
    struct Vector3
    {
        float x = 0, y = 0, z = 0;
        Vector3() = default;
        Vector3(float a, float b, float c) : x(a), y(b), z(c) {}
        Vector3 operator-(Vector3 const& o) const { return {x - o.x, y - o.y, z - o.z}; }
        Vector3 operator+(Vector3 const& o) const { return {x + o.x, y + o.y, z + o.z}; }
        Vector3 operator*(float f) const { return {x * f, y * f, z * f}; }
        float length() const { return std::sqrt(x * x + y * y + z * z); }
    };
}
struct Position
{
    float m_x = 0, m_y = 0, m_z = 0, m_o = 0;
    Position() = default;
    Position(float x, float y, float z, float o) : m_x(x), m_y(y), m_z(z), m_o(o) {}
    float GetPositionX() const { return m_x; }
    float GetPositionY() const { return m_y; }
    float GetPositionZ() const { return m_z; }
};
constexpr float DC_PI = 3.14159265358979323846f;
namespace DungeonClearMath
{
    inline G3D::Vector3 PointShortOfPathEnd(std::vector<G3D::Vector3> const& path,
                                            float stopShort)
    {
        if (path.empty())
            return G3D::Vector3(0.0f, 0.0f, 0.0f);
        float remaining = std::max(0.0f, stopShort);
        for (std::size_t i = path.size() - 1; i > 0; --i)
        {
            G3D::Vector3 const& a = path[i - 1];
            G3D::Vector3 const& b = path[i];
            float const seg = (b - a).length();
            if (seg >= remaining)
            {
                if (seg <= 0.0f)
                    return b;
                return b + (a - b) * (remaining / seg);
            }
            remaining -= seg;
        }
        return path.front();
    }
    inline bool IsRegroupAnchorCandidate(float dist, float engagementRadius,
                                         bool levelReachable)
    {
        return dist <= engagementRadius && levelReachable;
    }
    inline bool ChaseWakesSleepingBoss(float chaseEndToBoss, float wakeSphere, bool bossInCombat)
    {
        return !bossInCombat && chaseEndToBoss < wakeSphere;
    }
    inline float ChaseEndToBoss(float botX, float botY, float targetX, float targetY,
                                float bossX, float bossY, float range)
    {
        float const dx = botX - targetX;
        float const dy = botY - targetY;
        float const toBot = std::sqrt(dx * dx + dy * dy);
        float endX = targetX;
        float endY = targetY;
        if (toBot <= range)
        {
            endX = botX;
            endY = botY;
        }
        else if (range > 0.0f)
        {
            endX = targetX + dx / toBot * range;
            endY = targetY + dy / toBot * range;
        }
        return std::sqrt((endX - bossX) * (endX - bossX) + (endY - bossY) * (endY - bossY));
    }
    inline std::vector<Position> StandoffCandidates(Position const& target, Position const& bot, float standoffRadius, std::uint32_t ringPoints);
}
inline std::vector<Position> DungeonClearMath::StandoffCandidates(Position const& target,
                                                           Position const& bot,
                                                           float standoffRadius,
                                                           std::uint32_t ringPoints)
{
    std::vector<Position> out;
    out.reserve(ringPoints + 1);

    // Base direction: from the center toward the bot's current side, so the first
    // candidate is the shortest reposition. Degenerate (bot on center) -> +X.
    float dx = bot.GetPositionX() - target.GetPositionX();
    float dy = bot.GetPositionY() - target.GetPositionY();
    float const len = std::sqrt(dx * dx + dy * dy);
    float baseAngle;
    if (len < 0.01f)
        baseAngle = 0.0f;
    else
        baseAngle = std::atan2(dy, dx);

    float const z = target.GetPositionZ();
    auto emit = [&](float angle)
    {
        out.emplace_back(target.GetPositionX() + std::cos(angle) * standoffRadius,
                         target.GetPositionY() + std::sin(angle) * standoffRadius,
                         z, 0.0f);
    };

    // First candidate dead on the bot's side, then fan out alternately +/- so
    // nearer-to-bot angles are tried before the far side of the target.
    emit(baseAngle);
    float const step = (2.0f * DC_PI) / float(ringPoints + 1);
    for (std::uint32_t k = 1; k <= ringPoints; ++k)
    {
        float const off = step * float((k + 1) / 2);
        float const sign = (k % 2 == 1) ? 1.0f : -1.0f;
        emit(baseAngle + sign * off);
    }
    return out;
}
namespace DcEngageGeometry
{
inline float AggroReachYards(float aggroRange, float mobReach,
                                        float botReach, float aggroMargin,
                                        float partyBuffer)
{
    return aggroRange + mobReach + botReach + aggroMargin + partyBuffer;
}
}
struct RoomAggroBoss
{
    uint32 mapId{0};
    uint32 bossEntry{0};
    float  radius{0.0f};
    std::vector<uint32> memberEntries;
    bool   hasYBand{false};
    float  minY{0.0f};
    float  maxY{0.0f};
    float  pullOutRadius{0.0f};
    float  skirtRadius{0.0f};
    bool   hasCampBox{false};
    float  campMinX{0.0f};
    float  campMaxX{0.0f};
    float  campMinY{0.0f};
    float  campMaxY{0.0f};
    bool   detourReachable{false};
    float  bossSideLastRadius{0.0f};
};
namespace Dc294RoomAggro
{
    inline RoomAggroBoss const kRoomAggroBosses[] =
    {
        // SM Cathedral (189): Mograine -> Whitemane. The grid AttackStart fires
        // on Mograine's engage; both encounter entries are flagged so the room
        // gate holds whichever the boss index selects.
        { 189, 3976, 80.0f, {} },   // Highlord Mograine
        { 189, 3977, 80.0f, {} },   // Inquisitor Whitemane

        // Scholomance (289): the linked pair Marduk Blackpool / Vectus. They do
        // not pre-aggro but are wired together in SmartAI (each "On Aggro -> Set
        // Data 3" and "On Data Set 3 -> Attack Start"), so pulling EITHER pulls
        // BOTH. They stand ~18yd apart in a chamber of ~32 Scholomance Students;
        // an AoE that wakes one wakes the pair while the room is still up. The DC
        // roster collapses the two into ONE boss anchored on Vectus (see
        // BossRosterRegistry, map 289), so 10432 is the live tracked boss (room
        // centre, nearest the close trash) and 10433 (Marduk, off the roster) is
        // listed here ONLY so the room-trash value's partner check excludes it
        // from being chased as a faction-15 "student". Radius 55 covers the
        // student room; the RoomClearTimeout valve handles far stragglers.
        //
        // CRITICAL: the whole room — students AND the two bosses — stands NEUTRAL
        // (yellow), not hostile, until struck. An EXPLICIT memberEntries whitelist
        // is what makes this work: (1) it scopes "the room" to the students alone,
        // so the unrelated HOSTILE packs that happen to sit within 55yd (Diseased
        // Ghouls to the east, Plagued Hatchlings to the north, Risen Adepts) are
        // NOT counted as room trash — without it those hostiles were the only
        // things the value could see, and as they cleared the count flicked to 0
        // and the boss was pulled with the students still up; and (2) a non-empty
        // whitelist signals DungeonClearRoomTrashValue to clear these entries even
        // though they are neutral (the hostile gate is bypassed for explicit
        // members only — see that file). Marduk is whitelisted identically so the
        // partner row behaves the same if it is ever the tracked boss.
        { 289, 10432, 55.0f, { 10475 } }, // Scholomance — Vectus (tracked boss)
        { 289, 10433, 55.0f, { 10475 } }, // Scholomance — Marduk Blackpool (partner)

        { 555, 18667, 100.0f, {} }, // Shadow Labyrinth — Blackheart the Inciter
        { 109, 5710,   90.0f, {} }, // Sunken Temple — Jammal'an the Prophet
        // Mana-Tombs — Pandemonius. boss_pandemonius::PullRoom() gathers the three
        // room adds within ROOM_PULL_RANGE(70) but force-pulls ONLY those with
        // ROOM_EXIT(-145) < Y < ROOM_ENTERANCE(-50): the Scavengers/Crypt
        // Raiders/Sorcerers BEHIND him (toward Tavarok, Y <= -145) never join the
        // fight. Mirror that exactly — entry whitelist + the Y corridor — or the
        // room-clear fixates on the unreachable behind-the-boss adds, orbits him,
        // and burns the full RoomClearTimeout before giving up (live: kept=2..7
        // flickering on 18309/18311/18313, far=108+, never 0).
        { 557, 18341, 70.0f, { 18309, 18311, 18313 }, true, -145.0f, -50.0f },
        { 601, 28684,  40.0f, {} }, // Azjol-Nerub — Krik'thir the Gatewatcher
        // Blackrock Depths — Emperor Dagran Thaurissan. CallForHelp(VISIBLE_RANGE)
        // wakes the whole throne room, the two balconies included, so their packs
        // count however long the ramp walk is (see RoomAggroBoss::detourReachable).
        // The throne-side floor goes last (bossSideLastRadius): 36yd takes in the
        // flanking packs (<= 29.9yd from him) and stops short of the balconies'
        // nearest mob (39.6yd).
        { 230, 9019, 166.0f, {}, false, 0.0f, 0.0f, /*pullOutRadius*/ 0.0f,
          /*skirtRadius*/ 0.0f, /*hasCampBox*/ false, 0.0f, 0.0f, 0.0f, 0.0f,
          /*detourReachable*/ true, /*bossSideLastRadius*/ 36.0f },
        { 429, 14354,  50.0f, {} }, // Dire Maul East — Pusillin

        // Upper Blackrock Spire (229) — General Drakkisath. NOT a script radius:
        // his own engage only DoZoneInCombats his two Chromatic Elite Guards (his
        // formation, fought with him). The room around him is the problem — 14
        // Blackhand / Rage Talon mobs stand within 57yd of him at the north pair,
        // the south archway the party walks in by, and the western group, and a
        // boss fight among them pulls them in. 60yd is the measured gap: the next
        // spawn out is 71.5yd off, in the corridors. Single level (z 111).
        { 229, 10363,  60.0f, {} }, // Upper Blackrock Spire — General Drakkisath

        // Deadmines (36) — Gilnid, the goblin foundry.
        //
        // THE RADIUS IS NOT READ FROM A SCRIPT HERE, because there is no script to
        // read: Gilnid's whole SmartAI is a Molten Metal cast, an out-of-combat
        // yell, and a door open on death. He force-pulls nothing. This row rests on
        // the SCHOLOMANCE precedent above — "does not pre-aggro, but the room and
        // the boss cannot be separated by ordinary means" — and 60yd is the
        // measured spawn envelope of the foundry, not a script constant.
        //
        // What makes the room undecomposable: 19 ELITES at level 18, against a
        // level-18 party, standing 7.1-57.6yd from Gilnid. At a same-level elite's
        // ~20yd aggro radius, 17 of the 19 form ONE connected chain — single-linkage
        // clustering finds no pack boundary anywhere in the room, so waking any of
        // them can cascade through the lot. The dynamic pull governor cannot see
        // this: it sizes a pack with 6yd spread / 10yd assist windows, so on a room
        // whose mobs sit 15-20yd apart it reports "estimated 2 aggro" and returns
        // LEEROY every time (424 LEEROY vs 4 ADVANCED across tp-20260815-171416-1).
        // The party then walks in, collects the room, and Gilnid — who is simply the
        // next roster target — joins on top of it.
        //
        // Live (tp-20260815-171416-1): 5 of 20 runs wiped "on Gilnid" with the
        // status line alternating fighting_boss / fighting_trash every 1-2 seconds
        // (tr-...-10: Gilnid, Craftsman, Engineer, Remote-Controlled Golem, Craftsman
        // in fourteen seconds). Four of those five logged EVERY death as ON BOSS
        // with no trash on the sheet at all — Gilnid lands the killing blows, so the
        // record reads like a boss-difficulty problem when it is a room problem.
        //
        // 60 sweeps all 19 (50 would leave the three western-most at 52.0/55.3/57.6).
        // It also takes in the adjacent mine trash — 2 Defias Taskmaster, 7 Strip
        // Miner, 1 Overseer, 1 Miner — which costs nothing in practice: the clear
        // approaches the foundry from the west THROUGH that tunnel, so those are
        // already dead when the gate is evaluated, and RoomClearTimeout is the valve
        // for any straggler that is not.
        //
        // No skirtRadius / pullOutRadius override. The computed boss sphere already
        // does the right thing at both ends: the two Craftsmen at 7.1 and 9.2yd fall
        // INSIDE it and are excluded from room trash, so they come with the boss as
        // a normal 1-boss + 2-adds pull, and everything past it is ordinary
        // clearable room trash.
        { 36, 1763, 60.0f, {} }, // Deadmines — Gilnid (goblin foundry)

        // NOTE: Tendris Warpwood (11489) is deliberately NOT a room-aggro boss.
        // The radius-around-the-boss model is the wrong tool for him: live runs
        // showed it clearing the Eldreth ghosts standing behind him (which don't
        // assist the pull) while the mobs that actually matter — the Warpwood
        // treants in the entrance room ~200yd south, on a higher floor — sit far
        // outside any sane boss radius. That room is cleared instead by an
        // anchored ClearRadius objective co-located with crystal generator 1
        // (DireMaulEvents 429/11 / BossRosterRegistry OBJ(8)), ordered first.

        // Trial of the Champion (650) — Grand Champions, CallForHelp(100) on
        // unmount. Both faction rosters flagged; the other live champions are
        // themselves encounter bosses, so they fall out via the boss exclusion
        // and only genuine arena trash (if any) counts as the room.
        { 650, 34701, 100.0f, {} }, // Colosos
        { 650, 34702, 100.0f, {} }, // Jaelyne Evensong / Ambrose Boltspark
        { 650, 34703, 100.0f, {} }, // Lana Stouthammer / Mokra
        { 650, 34705, 100.0f, {} }, // Marshal Jacob Alerius
        { 650, 35569, 100.0f, {} }, // Eressea Dawnsinger
        { 650, 35570, 100.0f, {} }, // Zul'tore
        { 650, 35571, 100.0f, {} }, // Runok Wildmane
        { 650, 35572, 100.0f, {} }, // Mokra the Skullcrusher
        { 650, 35617, 100.0f, {} }, // Visceri

        // Gundrak (604) — Slad'ran, the snake pit.
        //
        // Like Gilnid above, there is NO script force-pull to read a radius from:
        // boss_slad_ran's Reset()/JustEngagedWith schedule only Poison Nova, Powerful
        // Bite, Venom Bolt and the two summon waves. The row exists because the ROOM
        // joins the fight anyway, by geometry rather than by CallForHelp — the boss
        // spawns at the NORTH end of a ~70yd pit (1775.13, 674.98, 129.3) while his
        // five Slad'ran Summon Targets (29682) sit 29-73yd SOUTH at y 606-646, i.e.
        // right on top of the trash. The two Vipers at 90% and three Constrictors at
        // 50% (75% heroic) therefore SPAWN inside the trash packs and run north
        // through them, and Poison Nova (55081, 15yd) pushes the whole party 17yd
        // outward into them every 16-53s.
        //
        // THE ROOM IS THE NINE ELITES, and only those:
        //   * 29774 Spitting Cobra          x5, spawns 26.9-62.0yd — level 77, hp x4
        //   * 29768 Unyielding Constrictor  x4, spawns 29.9-48.0yd — level 77, hp x4
        //
        // radius 75 is measured, not read from a script, and it is sized off the
        // PATROL envelope rather than the spawn points. Two of the four constrictors
        // are MovementType 2: guid 127014 runs path 1270140 (28 points, 24.9-69.9yd
        // from the boss) and guid 127017 runs path 1270170 (26 points, 33.2-64.1yd).
        // So the real far edge of the room is 69.9, not the 62.0 the spawn rows
        // suggest — a radius sized to the spawns would drop a patrolling constrictor
        // out of the set at the far end of its path and call the room clear while it
        // was still walking back. 75 covers 69.9 and still stops short of the NEXT
        // room's mobs of the same entries: the nearest non-room Spitting Cobra is
        // 79.5yd out and the nearest non-room Unyielding Constrictor 88.9yd. (Of the
        // 12 cobras and 12 constrictors on map 604 only these 9 are Slad'ran's; the
        // rest reach 153 and 209yd.)
        //
        // WHY THE WHITELIST, given every entry on it reads hostile anyway. It is here
        // to keep two NON-threats out of the room set:
        //
        // (1) 29630 Fanged Pit Viper, x86 at 21.1-71.3yd — the snakes carpeting the
        //     pit floor. These were briefly whitelisted so the clear would sweep them;
        //     a live run showed that going badly, and they should not have been in
        //     scope to begin with. They are faction template 7 — FriendGroup 0,
        //     EnemyGroup 0, no Enemies_* — so they stand NEUTRAL and never aggro on
        //     proximity; nothing but a stray AoE brings them into the fight, and at
        //     level 1 with HealthModifier 1 that same AoE kills them outright. They
        //     were never part of the room-joins-the-fight problem, and clearing them
        //     meant 86 individual engage cycles strung across a 70yd room against a
        //     180s RoomClearTimeout. Leaving them alone is free.
        // (2) 29834 Drakkari Frenzy, x8 within 73yd but at z 104-110 — down in the
        //     MOAT, ~20yd below the boss platform (the column under the Slad'ran
        //     altar has water at 110.14 and ground at 103.22, no 129 surface). These
        //     DO read hostile, so an empty "any hostile" row would count them and
        //     send the clear after mobs it cannot walk to: the Pandemonius failure
        //     above, orbiting the boss until the timeout expires.
        //
        // The 13321 Frogs and 29637 Crafty Snake are critters and the 29682 Summon
        // Targets carry UNIT_FLAG_NOT_SELECTABLE, so IsPossibleTarget already drops
        // those three without help from the whitelist.
        //
        // No pullOutRadius / skirtRadius override. The computed sphere is ~26-28yd
        // here (Slad'ran's aggro range + his 2.0 combat reach + the bot's + margin 2
        // + padding 3), so at most the single closest Spitting Cobra (26.9yd) falls
        // inside it and comes with the boss as a normal 1-boss + 1-add pull — the
        // same call the Gilnid row makes for its two Craftsmen. Shrinking the
        // exclusion to claw that one cobra back would force advanced pull-to-camp
        // for the whole room (see pullOutRadius), which is not worth it for one add.
        { 604, 29304, 75.0f, { 29774, 29768 } }, // Gundrak — Slad'ran

        // NOTE: The Mechanar (554) Mechano-Lord Capacitus had a room-aggro entry
        // here to pre-clear his SE Driller pack, back when the clear reached him
        // LAST on floor 1 (up the corridor from the dead Iron-Hand), which put that
        // pack right on the pull approach. The boss order now runs Gyro-Kill ->
        // Capacitus -> Iron-Hand, so the tank approaches the pit from the NW and the
        // SE pack falls on the post-Capacitus walk down to Iron-Hand — plain trash-
        // clear handles it once the boss is dead. Entry removed (see MechanarEvents).

        // The Mechanar (554) Nethermancer Sepethrea (floor 2). Her chamber holds
        // THREE elite trash groups the party must clear before the pull, and all
        // three should be dragged back WEST toward the entrance (the elevator
        // landing / Nethermancer door at ~x268) — NOT fought in place, because she
        // summons roaming Raging Flames and the fight wants the open floor. From the
        // world-DB spawns + a navmesh probe (TestMechanarElevatorProbe,
        // MechanarSepethreaRoomProbe):
        //   * Pack A  — 2 Astromage + 2 Centurion, ~(273,-23), 60-67yd from her.
        //   * Robots  — 2 Tempest-Forge Destroyers (one stationary, one roaming),
        //               ~(291,29) & (294,-15), 39-43yd out.
        //   * Pack B  — 2 Centurion + 2 Astromage, ~(309,13), only 17-19yd out —
        //               right on her ~18yd aggro line.
        // radius 70 covers the farthest (Pack A at 67yd); the whole set reads hostile
        // so no whitelist is needed. The catch is Pack B: it sits INSIDE her ~28yd
        // exclusion sphere, so the ordinary room-clear would treat it as "comes with
        // the boss" and leave it up. pullOutRadius 14 shrinks the room-trash exclusion
        // to 14yd (below Pack B's 17yd), KEEPING Pack B as clearable room trash, and
        // — because a non-zero pullOutRadius also forces advanced pull-to-camp — the
        // tank drags Pack B out from ~35yd west instead of meleeing it in place. See
        // RoomAggroBoss::pullOutRadius for the coupling that makes this safe.
        //
        // skirtRadius 40 WIDENS the fight standoff (her raw ~28yd sphere is too tight):
        // Pack B sits only 17yd out, so a camp cleared to the generic ~25yd from her
        // still landed the kill on top of her aggro/CallForHelp and woke her while the
        // party was still on the trash. The wider skirt drags Pack B's camp out toward
        // the ~x268 entrance landing before the fight, clear of her and of the open
        // floor her Raging Flames roam. It does NOT reclassify Pack B (membership uses
        // pullOutRadius 14, not the skirt).
        { 554, 19221, 70.0f, {}, false, 0.0f, 0.0f, /*pullOutRadius*/ 14.0f,
          /*skirtRadius*/ 40.0f },

        // Karazhan (532) — Moroes. The Banquet Hall is cleared before he is
        // touched. He stands on a dais 3yd above the hall floor (-10982.7,
        // -1877.9, 81.8), aggressive, and on engage does CallForHelp(20) +
        // DoZoneInCombat. His Reset() summons four of six guests 4-7yd around
        // him, and each guest's SmartAI does CallForHelp 15 +
        // SetInCombatWithZone on aggro. So coming within ~20yd of the dais, or
        // pulling anything a guest can reach, starts the fight with every
        // nearby pack.
        //
        // The hall (world DB, distance from Moroes): Phantom Guest formations
        // of 8-9 elites led by 135233 (west, 21.8-38.6yd) and 135206 (east,
        // 21.1-37.9yd), 135217 (south row, 41.6-45.3yd) and 135229 (far
        // south, 54.4-56.4yd); four Ghostly Stewards (45.5-53.7yd) and three
        // pathing Skeletal Waiters (36.5-41.4yd). Four more guests stand on the
        // upper gallery at z 93.8; the reach filter drops them.
        //
        // radius 60 covers the far formation. The whitelist (Phantom Guest,
        // Steward, Waiter) is what keeps his summoned guests (17007,
        // 19872-19876) out of the room-trash set.
        //
        // pullOutRadius 12. The computed exclusion sphere would swallow both
        // 21yd formations as "comes with the boss": Moroes plus 17 elites.
        // Twelve keeps the summoned guests (<=7yd) excluded and makes every
        // formation clearable, and it forces the advanced pull, so the tank
        // tags a formation from outside and drags it south instead of meleeing
        // it next to the dais.
        //
        // skirtRadius 32: his 20yd CallForHelp, plus a guest's 15yd call
        // anchored ~6yd off him, minus overlap, plus margin. The approach
        // orbit, the boss standoff and the pull camp all stay 32yd off the
        // dais, which puts the camp at the south end of the hall.
        //
        // Camp box: his evade box (x >= -11028, y >= -1955), pulled 15yd in
        // so the fanned-out camp, a step-out and the pack around the tank all
        // stay inside it. Without it the drag walked camps back to the south
        // door (y -1955..-1957), and a fresh camp at y -1943 put the fight
        // against the Phantom Attendants' corridor (tr-20260926-174359-2).
        // The max bounds are past the dais and the east wall, so they never
        // bind.
        { 532, 15687, 60.0f, { 16409, 16414, 16415 }, false, 0.0f, 0.0f,
          /*pullOutRadius*/ 12.0f, /*skirtRadius*/ 32.0f,
          /*hasCampBox*/ true, /*campMinX*/ -11013.0f, /*campMaxX*/ -10900.0f,
          /*campMinY*/ -1940.0f, /*campMaxY*/ -1800.0f },
    };
    inline RoomAggroBoss const* Find(uint32 mapId, uint32 bossEntry)
    {
        for (RoomAggroBoss const& b : kRoomAggroBosses)
            if (b.mapId == mapId && b.bossEntry == bossEntry)
                return &b;
        return nullptr;
    }
    inline float SkirtOverride(uint32 mapId, uint32 bossEntry)
    {
        RoomAggroBoss const* b = Find(mapId, bossEntry);
        return b ? b->skirtRadius : 0.0f;
    }
}
#endif
