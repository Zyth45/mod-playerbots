/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: a faction raid on a capital (AiPlayerbot.CoaWorld.CapitalRaid.*): Horde and Alliance
 * raids in 3.0.
 *
 * About 60 Horde bots march on Stormwind to kill King Varian Wrynn, or 60 Alliance bots on Orgrimmar to kill Thrall.
 *  - Start (with Enable): a game master (.coaworld capitalraid start <stormwind|orgrimmar>, the addon's GM buttons), or now and then
 *    by itself (CapitalRaid.Auto, rare). Never while an invasion holds that capital, nor the other way round.
 *  - Gathering: the attackers are borrowed (CoA World's borrowing: those far away brought out of sight) to a spot
 *    outside the main gate; defenders of the city's side to the hall before the throne. Both sides' players are told.
 *  - Assault: the attackers walk the way to the throne on the navmesh (their own travel), gate, keep, hall, throne; the
 *    next step once half of them are at the current one (or after two minutes). The city's guards fight them as they
 *    always do; the king keeps his own script, with less health for the raid (KingHealthPct), given back after.
 *  - End: the king dies (the attackers win), no attacker is left standing in the city, or the time is up. Gold for the
 *    real players of the winning side near the throne; the bots go back to their lives.
 * Level: bots of MinLevel and up; with AiPlayerbot.CoaFreshStart.Enable, the top level only (MaxPlayerLevel, 60 on a
 * CoA realm), and no raid without enough.
 * World thread only; nothing to do while no raid is on (a timer, and a minute check for the automatic start).
 */

#include "CoaWorldCapitalRaid.h"

#include "CoaWorldAddonRules.h"
#include "CoaWorldBots.h"
#include "CoaWorldState.h"
#include "CoaWorldStateRules.h"

#include "AiObjectContext.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Creature.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <vector>

bool CoaWorldInvasionAt(std::string const& site);  // CoaWorldInvasionMgr.cpp

using namespace Acore::ChatCommands;

namespace
{
using namespace CoaWorld;

constexpr uint32 BOT_OWNER_RAID_ATTACK = 900;
constexpr uint32 BOT_OWNER_RAID_DEFEND = 901;
constexpr uint32 TICK_MS = 2 * IN_MILLISECONDS;
constexpr uint32 AUTO_CHECK_MS = MINUTE * IN_MILLISECONDS;
constexpr float AT_STEP_YARDS = 25.0f;
constexpr time_t STEP_MAX_SECONDS = 2 * MINUTE;
constexpr float IN_CITY_YARDS = 900.0f;     // an attacker this close to the throne still counts
constexpr time_t WIPE_SECONDS = 20;         // no attacker standing for this long: the raid is over
constexpr float KING_ENGAGE_YARDS = 45.0f;  // at the last step, an attacker this close to the king attacks him
constexpr float DEFEND_ENGAGE_YARDS = 40.0f;
constexpr float REWARD_YARDS = 400.0f;

struct Settings
{
    bool enabled = false;
    bool autoStart = false;
    uint32 autoHours = 24;
    uint32 attackers = 60;
    uint32 defenders = 20;
    uint32 minutes = 30;
    uint32 gatherSeconds = 180;
    uint32 kingHealthPct = 25;
    uint32 minLevel = 55;
    uint32 rewardSilverPerLevel = 50;
};
Settings settings;

struct CityDef
{
    char const* key;
    char const* name;  // as the invasions' site
    char const* nameFr;
    char const* king;
    char const* kingFr;
    uint32 mapId;
    TeamId defender;
    uint32 kingEntry;
    std::vector<Position> path;  // [0] the gathering outside the gate, last: before the throne
    uint32 defendStep;           // where the defenders wait
};

// Spots from the CoA world DB (coa-local): the kings' spawns, the keep's guards, the invasions' gate spots.
std::vector<CityDef> const CITIES = {
    { "stormwind", "Stormwind", "Hurlevent", "King Varian Wrynn", "le roi Varian Wrynn", 0, TEAM_ALLIANCE, 29611,
      { { -9200.0f, 330.0f, 84.0f, 0.75f },           // Elwynn road, outside the gate
        { -8888.2f, 566.2f, 93.3f, 2.20f },           // inside the main gate
        { -8533.0f, 456.9f, 105.1f, 0.0f },           // the keep's steps (Royal Guards)
        { -8408.4f, 300.0f, 147.0f, 0.0f },           // the keep's hall
        { -8370.0f, 240.0f, 155.5f, 0.0f } },         // before King Varian Wrynn (-8364.7, 230.9, 157)
      3 },
    { "orgrimmar", "Orgrimmar", "Orgrimmar", "Thrall", "Thrall", 1, TEAM_HORDE, 4949,
      { { 1310.0f, -4385.0f, 26.5f, 0.0f },           // Durotar, outside the gate
        { 1629.9f, -4373.6f, 31.6f, 3.70f },          // inside the main gate
        { 1930.5f, -4238.3f, 42.3f, 0.0f },           // Grommash Hold's door (Kor'kron Elite)
        { 1927.5f, -4175.9f, 41.2f, 0.0f },           // the hold's hall
        { 1921.0f, -4135.0f, 43.5f, 0.0f } },         // before Thrall (1919.9, -4123.8, 44.4)
      3 },
};

enum class Phase : uint8
{
    Idle,
    Gathering,
    Assault
};

struct Raid
{
    CityDef const* city = nullptr;
    Phase phase = Phase::Idle;
    Map* map = nullptr;
    ObjectGuid king;
    time_t startedAt = 0, assaultAt = 0, endsAt = 0, stepSince = 0, wipeSince = 0;
    uint32 step = 0;
};
Raid g_raid;
time_t g_nextAuto = 0;

TeamId Attacker(CityDef const& city) { return city.defender == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE; }

bool IsReal(Player* player)
{
    return player && player->IsInWorld() && player->GetSession() && !player->GetSession()->IsBot() &&
           !GET_PLAYERBOT_AI(player) && !CoaWorld::IsTestDriver(player);
}

bool French(Player* player)
{
    return player && player->GetSession() && player->GetSession()->GetSessionDbLocaleIndex() == LOCALE_frFR;
}

// Every real player hears it, both sides: who is coming, who must defend.
void TellAll(char const* situation, char const* en, char const* fr, CityDef const& city, uint32 minutes = 0)
{
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
    {
        if (!IsReal(player))
            continue;
        bool const f = French(player);
        bool const horde = Attacker(city) == TEAM_HORDE;
        std::string const side = f ? (horde ? "la Horde" : "l'Alliance") : (horde ? "the Horde" : "the Alliance");
        std::string const place = f ? city.nameFr : city.name;
        std::string const king = f ? city.kingFr : city.king;
        std::string text = Acore::StringFormat(f ? fr : en, side, place, king, minutes);
        if (!text.empty())
            text[0] = char(std::toupper(static_cast<unsigned char>(text[0])));
        text = WorldLine(player, situation,
                         { { "side", side }, { "city", place }, { "king", king }, { "minutes", std::to_string(minutes) } }, text);
        ChatHandler handler(player->GetSession());
        handler.SendSysMessage("|cffff8000[Living World]|r " + text);
        handler.SendNotification(text);
    }
}

std::string AddonLine(char const* state, time_t at)
{
    using namespace CoaWorldAddonRules;
    CityDef const& city = *g_raid.city;
    return Cut(std::string("WE\tcraid\tcraid:") + city.key + "\t" + state + "\t" + Text(city.name) + "\t" +
               Text(city.name) + "\t" + std::to_string(int64(at)) + "\t" +
               Text(std::string(Attacker(city) == TEAM_HORDE ? "Horde" : "Alliance") + " raid on " + city.name));
}

void Push(char const* state, time_t at)
{
    if (!CoaWorldAddon::On() || !g_raid.city)
        return;
    std::string const line = AddonLine(state, at);
    CoaWorldAddon::Push([&](Player*) { return line; });
}

Creature* King()
{
    return g_raid.map ? g_raid.map->GetCreature(g_raid.king) : nullptr;
}

void SetKingHealth(Creature* king, uint32 pct)
{
    if (!king || !king->IsAlive())
        return;
    king->SetStatPctModifier(UNIT_MOD_HEALTH, TOTAL_PCT, float(std::clamp<uint32>(pct, 1, 100)) / 100.0f);
    king->UpdateMaxHealth();
    king->SetFullHealth();
}

void Go(Player* bot, Position const& pos)
{
    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        botAI->rpgInfo.ChangeToGoGrind(WorldPosition(bot->GetMapId(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()));
}

void Attack(Player* bot, Unit* target)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || !bot->IsValidAttackTarget(target))
        return;
    // As the bots' own attack action: the target, the combat engine, then the swing.
    bot->SetSelection(target->GetGUID());
    botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(target);
    botAI->ChangeEngine(BOT_STATE_COMBAT);
    bot->Attack(target, !PlayerbotAI::IsRanged(bot));
}

std::vector<Player*> Bots(uint32 owner)
{
    std::vector<Player*> out;
    for (ObjectGuid const& guid : BorrowedBots(owner))
        if (Player* bot = ObjectAccessor::FindPlayer(guid))
            if (bot->IsInWorld())
                out.push_back(bot);
    return out;
}

// The realm's top level (MaxPlayerLevel: 60 on a CoA realm, as the installer writes it) with a fresh start, else
// MinLevel; never above the top level (a realm capped lower would otherwise never see a raid).
uint32 MinLevel()
{
    uint32 const top = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    return std::min(top, sPlayerbotAIConfig.coaFreshStartEnable ? top : settings.minLevel);
}

void Finish(bool attackersWon, char const* why)
{
    CityDef const& city = *g_raid.city;
    time_t const now = time(nullptr);
    if (attackersWon)
        TellAll("sys_craid_won", "{2} has fallen! {0} has sacked {1}.", "{2} est tomb\u00e9 ! {0} a mis {1} \u00e0 sac.", city);
    else
        TellAll("sys_craid_lost", "The assault on {1} is broken. {2} still stands.",
                "L'assaut sur {1} est repouss\u00e9. {2} tient toujours.", city);

    // Gold for the real players of the winning side who were there.
    TeamId const winners = attackersWon ? Attacker(city) : city.defender;
    Position const& throne = city.path.back();
    if (settings.rewardSilverPerLevel)
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            if (IsReal(player) && player->GetTeamId() == winners && player->GetMapId() == city.mapId &&
                player->GetExactDist(&throne) <= REWARD_YARDS)
                player->ModifyMoney(int32(settings.rewardSilverPerLevel * player->GetLevel() * SILVER));

    BorrowedSay(attackersWon ? BOT_OWNER_RAID_ATTACK : BOT_OWNER_RAID_DEFEND, 0, "craid_victory",
                { { "city", city.name } }, 2);
    ReleaseBots(BOT_OWNER_RAID_ATTACK, true);
    ReleaseBots(BOT_OWNER_RAID_DEFEND, true);
    if (Creature* king = King())
    {
        SetKingHealth(king, 100);
        king->setActive(false);
    }
    Push("end", now);
    LogEvent("capitalraid", "", Acore::StringFormat("{} raid on {} {} ({})", Attacker(city) == TEAM_HORDE ? "Horde" : "Alliance",
                                                     city.name, attackersWon ? "won" : "failed", why));
    LOG_INFO("module", "CoA World: capital raid on {} ended, {} ({})", city.name, attackersWon ? "king slain" : "failed", why);
    g_raid = Raid();
}

bool Start(CityDef const& city, std::string& error)
{
    if (g_raid.phase != Phase::Idle)
    {
        error = Acore::StringFormat("A raid on {} is already on.", g_raid.city->name);
        return false;
    }
    if (CoaWorldInvasionAt(city.name))
    {
        error = Acore::StringFormat("An invasion holds {} now.", city.name);
        return false;
    }
    Map* map = sMapMgr->CreateBaseMap(city.mapId);
    if (!map)
    {
        error = "No such map.";
        return false;
    }
    map->LoadGridsInRange(city.path.back(), 100.0f);

    Creature* king = nullptr;
    for (auto const& [spawnId, creature] : map->GetCreatureBySpawnIdStore())
        if (creature && creature->GetEntry() == city.kingEntry && creature->IsAlive())
        {
            king = creature;
            break;
        }
    if (!king)
    {
        error = Acore::StringFormat("{} is not there (dead or not spawned).", city.king);
        return false;
    }

    // Enough attackers of the level, as they are: no bot is raised (a fresh-start realm only takes its level 60s).
    TeamId const attacker = Attacker(city);
    uint32 const minLevel = MinLevel();
    uint32 free = 0;
    for (auto const& [guid, bot] : RandomBots())
        if (bot && bot->GetTeamId() == attacker && bot->GetLevel() >= minLevel && IsFreeBot(bot))
            ++free;
    uint32 const needed = std::max<uint32>(1, std::min<uint32>(20, settings.attackers));
    if (free < needed)
    {
        error = Acore::StringFormat("Only {} free {} bots of level {}+ (at least {} needed).", free,
                                    attacker == TEAM_HORDE ? "Horde" : "Alliance", minLevel, needed);
        return false;
    }

    BorrowRequest request;
    request.mapId = city.mapId;
    request.goal = city.path.front();
    request.rally = city.path.front();
    request.team = uint8(attacker);
    request.minLevel = minLevel;
    request.count = settings.attackers;
    request.nearRadius = 300.0f;
    request.teleport = true;
    uint32 const attackers = BorrowBots(BOT_OWNER_RAID_ATTACK, request);
    if (attackers < needed)
    {
        ReleaseBots(BOT_OWNER_RAID_ATTACK, true);
        error = Acore::StringFormat("Only {} attackers could come (at least {} needed).", attackers, needed);
        return false;
    }

    Position const& hold = city.path[city.defendStep];
    BorrowRequest defence;
    defence.mapId = city.mapId;
    defence.goal = hold;
    defence.rally = hold;
    defence.team = uint8(city.defender);
    defence.minLevel = minLevel;
    defence.count = settings.defenders;
    defence.nearRadius = 600.0f;
    defence.teleport = true;
    uint32 const defenders = settings.defenders ? BorrowBots(BOT_OWNER_RAID_DEFEND, defence) : 0;

    time_t const now = time(nullptr);
    g_raid.city = &city;
    g_raid.map = map;
    g_raid.king = king->GetGUID();
    g_raid.phase = Phase::Gathering;
    g_raid.startedAt = now;
    g_raid.assaultAt = now + settings.gatherSeconds;
    g_raid.endsAt = g_raid.assaultAt + settings.minutes * MINUTE;
    king->setActive(true);
    SetKingHealth(king, settings.kingHealthPct);

    uint32 const minutes = std::max<uint32>(1, (settings.gatherSeconds + 59) / 60);
    TellAll("sys_craid_warning", "{0} is massing outside {1} to march on {2}! The assault begins in {3} minutes.",
            "{0} se rassemble aux portes de {1} pour marcher sur {2} ! L'assaut commence dans {3} minutes.", city, minutes);
    BorrowedSay(BOT_OWNER_RAID_ATTACK, 0, "craid_rally", { { "city", city.name } }, 3);
    Push("warn", g_raid.assaultAt);
    LogEvent("capitalraid", "", Acore::StringFormat("{} attackers and {} defenders for a raid on {}", attackers, defenders, city.name));
    LOG_INFO("module", "CoA World: capital raid on {}: {} attackers, {} defenders, level {}+", city.name, attackers, defenders,
             minLevel);
    return true;
}

void Assault(time_t now)
{
    CityDef const& city = *g_raid.city;
    Creature* king = King();
    if (!king || !king->IsAlive())
    {
        Finish(true, "king slain");
        return;
    }
    if (now >= g_raid.endsAt)
    {
        Finish(false, "time up");
        return;
    }

    std::vector<Player*> attackers = Bots(BOT_OWNER_RAID_ATTACK);
    Position const& step = city.path[g_raid.step];
    uint32 standing = 0, atStep = 0;
    for (Player* bot : attackers)
        if (bot->IsAlive() && bot->GetMapId() == city.mapId && bot->GetExactDist(&city.path.back()) <= IN_CITY_YARDS)
        {
            ++standing;
            if (bot->GetExactDist(&step) <= AT_STEP_YARDS)
                ++atStep;
        }
    if (!standing)
    {
        if (!g_raid.wipeSince)
            g_raid.wipeSince = now;
        else if (now - g_raid.wipeSince >= WIPE_SECONDS)
            Finish(false, "attackers wiped");
        return;
    }
    g_raid.wipeSince = 0;

    // The next step once half of them are there, or after a while.
    bool const last = g_raid.step + 1 >= city.path.size();
    if (!last && (atStep * 2 >= standing || now - g_raid.stepSince >= STEP_MAX_SECONDS))
    {
        ++g_raid.step;
        g_raid.stepSince = now;
        for (Player* bot : attackers)
            if (bot->IsAlive() && !bot->IsInCombat())
                Go(bot, city.path[g_raid.step]);
    }
    Position const& goal = city.path[g_raid.step];
    bool const atThrone = g_raid.step + 1 >= city.path.size();

    for (Player* bot : attackers)
    {
        if (!bot->IsAlive() || bot->IsInFlight() || bot->GetMapId() != city.mapId)
            continue;
        if (atThrone && !bot->IsInCombat() && bot->GetExactDist(king) <= KING_ENGAGE_YARDS)
        {
            Attack(bot, king);
            continue;
        }
        // On its way: its own travel, set again when it dropped it (a fight on the way, a wander at the spot).
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        if (botAI && !bot->IsInCombat() && bot->GetExactDist(&goal) > AT_STEP_YARDS &&
            botAI->rpgInfo.GetStatus() != RPG_GO_GRIND)
            Go(bot, goal);
    }

    // The defenders hold the hall and fall on the attackers who come near.
    Position const& hold = city.path[city.defendStep];
    for (Player* bot : Bots(BOT_OWNER_RAID_DEFEND))
    {
        if (!bot->IsAlive() || bot->IsInCombat() || bot->GetMapId() != city.mapId)
            continue;
        Player* nearest = nullptr;
        float best = DEFEND_ENGAGE_YARDS;
        for (Player* enemy : attackers)
            if (enemy->IsAlive() && enemy->GetMapId() == city.mapId)
            {
                float const d = bot->GetExactDist(enemy);
                if (d < best)
                {
                    best = d;
                    nearest = enemy;
                }
            }
        if (nearest)
            Attack(bot, nearest);
        else if (bot->GetExactDist(&hold) > AT_STEP_YARDS)
        {
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (botAI && botAI->rpgInfo.GetStatus() != RPG_GO_GRIND)
                Go(bot, hold);
        }
    }
}

void Tick(time_t now)
{
    if (g_raid.phase == Phase::Gathering && now >= g_raid.assaultAt)
    {
        CityDef const& city = *g_raid.city;
        g_raid.phase = Phase::Assault;
        g_raid.step = 1;
        g_raid.stepSince = now;
        for (Player* bot : Bots(BOT_OWNER_RAID_ATTACK))
            if (bot->IsAlive())
                Go(bot, city.path[1]);
        TellAll("sys_craid_assault", "{0} storms the gates of {1}! Defend {2}!",
                "{0} enfonce les portes de {1} ! D\u00e9fendez {2} !", city);
        Push("on", g_raid.endsAt);
        LogEvent("capitalraid", "", Acore::StringFormat("the assault on {} begins", city.name));
        return;
    }
    if (g_raid.phase == Phase::Assault)
        Assault(now);
}

bool AnyRealPlayer()
{
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        if (IsReal(player))
            return true;
    return false;
}

void ReadSettings()
{
    auto key = [](char const* name) { return std::string("AiPlayerbot.CoaWorld.CapitalRaid.") + name; };
    settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), true, false);
    settings.autoStart = sConfigMgr->GetOption<bool>(key("Auto"), false, false);
    settings.autoHours = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("AutoHours"), 24, false), 1, 24 * 14);
    settings.attackers = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("Attackers"), 60, false), 5, 120);
    settings.defenders = std::min<uint32>(sConfigMgr->GetOption<uint32>(key("Defenders"), 20, false), 80);
    settings.minutes = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("Minutes"), 30, false), 5, 120);
    settings.gatherSeconds = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("GatherSeconds"), 180, false), 30, 1800);
    settings.kingHealthPct = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("KingHealthPct"), 25, false), 1, 100);
    settings.minLevel = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("MinLevel"), 55, false), 1, 80);
    settings.rewardSilverPerLevel = std::min<uint32>(sConfigMgr->GetOption<uint32>(key("RewardSilverPerLevel"), 50, false), 1000);
}

class CoaWorldCapitalRaidWorldScript : public WorldScript
{
public:
    CoaWorldCapitalRaidWorldScript()
        : WorldScript("CoaWorldCapitalRaidWorldScript",
                      { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN })
    {
    }
    void OnAfterConfigLoad(bool /*reload*/) override { ReadSettings(); }
    void OnStartup() override { ReadSettings(); }
    void OnShutdown() override
    {
        if (g_raid.phase != Phase::Idle)
            Finish(false, "shutdown");
    }

    void OnUpdate(uint32 diff) override
    {
        if (g_raid.phase != Phase::Idle)
        {
            _tick += diff;
            if (_tick >= TICK_MS)
            {
                _tick = 0;
                Tick(time(nullptr));
            }
            return;
        }
        if (!settings.enabled || !settings.autoStart)
            return;
        _auto += diff;
        if (_auto < AUTO_CHECK_MS)
            return;
        _auto = 0;
        time_t const now = time(nullptr);
        if (!g_nextAuto)
            g_nextAuto = now + time_t(float(settings.autoHours * HOUR) * frand(0.5f, 1.0f));
        if (now < g_nextAuto || !AnyRealPlayer())
            return;
        g_nextAuto = now + time_t(float(settings.autoHours * HOUR) * frand(0.75f, 1.25f));
        std::string error;
        CityDef const& city = CITIES[urand(0, CITIES.size() - 1)];
        if (!Start(city, error))
            LOG_INFO("module", "CoA World: no automatic capital raid on {}: {}", city.name, error);
    }

private:
    uint32 _tick = 0;
    uint32 _auto = 0;
};

CityDef const* FindCity(std::string const& name)
{
    for (CityDef const& city : CITIES)
        if (name == city.key || (name.size() >= 3 && std::string(city.key).rfind(name, 0) == 0))
            return &city;
    return nullptr;
}

class CoaWorldCapitalRaidCommandScript : public CommandScript
{
public:
    CoaWorldCapitalRaidCommandScript() : CommandScript("CoaWorldCapitalRaidCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable raidTable = {
            { "list", HandleList, SEC_GAMEMASTER, Console::Yes },
            { "status", HandleList, SEC_GAMEMASTER, Console::Yes },
            { "start", HandleStart, SEC_GAMEMASTER, Console::Yes },
            { "stop", HandleStop, SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable worldTable = {
            { "capitalraid", raidTable },
        };
        static ChatCommandTable commandTable = {
            { "coaworld", worldTable },
        };
        return commandTable;
    }

    static bool HandleList(ChatHandler* handler)
    {
        time_t const now = time(nullptr);
        for (CityDef const& city : CITIES)
        {
            std::string state = "idle";
            if (g_raid.city == &city)
                state = g_raid.phase == Phase::Gathering
                            ? Acore::StringFormat("gathering, assault in {} s", int64(g_raid.assaultAt - now))
                            : Acore::StringFormat("assault, step {}/{}, {} min left", g_raid.step, city.path.size() - 1,
                                                  int64(g_raid.endsAt - now) / 60);
            else if (CoaWorldInvasionAt(city.name))
                state = "an invasion holds it";
            handler->PSendSysMessage("{} ({} attack {}): {}. Attackers {}, defenders {}.", city.key,
                                     Attacker(city) == TEAM_HORDE ? "Horde" : "Alliance", city.king, state,
                                     g_raid.city == &city ? BorrowedCount(BOT_OWNER_RAID_ATTACK) : 0,
                                     g_raid.city == &city ? BorrowedCount(BOT_OWNER_RAID_DEFEND) : 0);
        }
        if (!settings.enabled)
            handler->SendSysMessage("AiPlayerbot.CoaWorld.CapitalRaid.Enable is 0: no raid can start.");
        return true;
    }

    // .coaworld capitalraid start <stormwind|orgrimmar>
    static bool HandleStart(ChatHandler* handler, std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        CityDef const* city = FindCity(name);
        if (!city)
        {
            handler->SendSysMessage("Usage: .coaworld capitalraid start <stormwind|orgrimmar>");
            handler->SetSentErrorMessage(true);
            return false;
        }
        if (!settings.enabled)
        {
            handler->SendSysMessage("AiPlayerbot.CoaWorld.CapitalRaid.Enable is 0.");
            handler->SetSentErrorMessage(true);
            return false;
        }
        std::string error;
        if (!Start(*city, error))
        {
            handler->SendSysMessage(error);
            handler->SetSentErrorMessage(true);
            return false;
        }
        handler->PSendSysMessage("Raid on {} gathering: {} attackers, {} defenders.", city->name,
                                 BorrowedCount(BOT_OWNER_RAID_ATTACK), BorrowedCount(BOT_OWNER_RAID_DEFEND));
        return true;
    }

    static bool HandleStop(ChatHandler* handler)
    {
        if (g_raid.phase == Phase::Idle)
        {
            handler->SendSysMessage("No raid is on.");
            return true;
        }
        Finish(false, "stopped by a game master");
        handler->SendSysMessage("Raid stopped.");
        return true;
    }
};
}  // namespace

bool CoaWorldCapitalRaidAt(std::string const& city)
{
    return g_raid.phase != Phase::Idle && g_raid.city && city == g_raid.city->name;
}

bool CoaWorldCapitalRaidOn() { return g_raid.phase != Phase::Idle; }

void CoaWorldCapitalRaidLines(std::vector<std::string>& out)
{
    if (g_raid.phase == Phase::Gathering)
        out.push_back(AddonLine("warn", g_raid.assaultAt));
    else if (g_raid.phase == Phase::Assault)
        out.push_back(AddonLine("on", g_raid.endsAt));
}

std::string CoaWorldCapitalRaidJson()
{
    if (!settings.enabled && g_raid.phase == Phase::Idle)
        return "null";
    if (g_raid.phase == Phase::Idle)
        return "{\"running\":null}";
    CityDef const& city = *g_raid.city;
    Creature* king = King();
    return Acore::StringFormat(
        "{{\"running\":{{\"city\":\"{}\",\"attackers\":\"{}\",\"phase\":\"{}\",\"step\":{},\"steps\":{},\"bots\":{},"
        "\"defenders\":{},\"kingHealthPct\":{},\"startedAt\":{},\"assaultAt\":{},\"endsAt\":{}}}}}",
        city.key, Attacker(city) == TEAM_HORDE ? "horde" : "alliance",
        g_raid.phase == Phase::Gathering ? "gathering" : "assault", g_raid.step, city.path.size() - 1,
        BorrowedCount(BOT_OWNER_RAID_ATTACK), BorrowedCount(BOT_OWNER_RAID_DEFEND),
        king && king->IsAlive() ? uint32(king->GetHealthPct()) : 0, int64(g_raid.startedAt), int64(g_raid.assaultAt),
        int64(g_raid.endsAt));
}

void AddSC_coa_world_capital_raid()
{
    new CoaWorldCapitalRaidWorldScript();
    new CoaWorldCapitalRaidCommandScript();
}
