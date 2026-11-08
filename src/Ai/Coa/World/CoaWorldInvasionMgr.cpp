/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: the invasion manager (schedule, notice, waves, reward, bots sent to defend).
 *
 * The schedule and the run (Idle -> Warning -> Running, a notice 10 minutes before, a boss to kill
 * before the time runs out, the reward to everyone there, deaths queued from the map threads and
 * handled in the world update) are adapted from wow-mod-era-events by buildthehomelab (MIT License,
 * see CoaWorldInvasion.h). New on CoA: a run belongs to the level band of the players online
 * instead of an era of mod-individual-progression, and random bots of that band are borrowed to
 * defend (a budget, walking if near, otherwise brought behind the defenders' line out of sight of
 * real players), then handed back to their usual life when it ends.
 *
 * Everything runs in the world thread (WorldScript::OnUpdate), between map updates.
 *
 * Config: AiPlayerbot.CoaWorld.Invasions.* (playerbots.conf), off by default.
 *
 * Part of mod-playerbots (GPL-2.0-or-later).
 */

#include "CoaWorldInvasion.h"

#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "TemporarySummon.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <mutex>

using namespace Acore::ChatCommands;

void AddSC_coa_world_invader_ai();

namespace CoaWorld
{
    namespace
    {
        constexpr uint32 MINUTE_MS = MINUTE * IN_MILLISECONDS;
        constexpr uint32 CORPSE_DESPAWN_MS = 60 * IN_MILLISECONDS;
        constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.Invasions.";

        struct EventSettings
        {
            bool enabled = true;
            uint32 intervalMin = 120;  // minutes
            uint32 intervalMax = 240;
            uint32 minLevel = 1;
        };

        struct Settings
        {
            bool enabled = false;
            uint32 minRealPlayers = 1;
            uint32 retry = 5;           // minutes
            uint32 warning = 10;        // minutes
            uint32 duration = 30;       // minutes
            uint32 healthPct = 100;
            uint32 rewardSilverPerLevel = 25;
            int32 rewardReputation = 150;
            float rewardRange = 120.0f;
            uint32 botsPct = 10;
            uint32 botsMax = 120;
            uint32 botsLevelRange = 5;
            float botsNearRadius = 500.0f;
            bool botsTeleport = true;
            bool botsReturnHome = true;
            std::vector<EventSettings> events;
        };

        Settings settings;

        enum class State : uint8
        {
            Idle,
            Warning,
            Running,
        };

        char const* StateName(State state)
        {
            switch (state)
            {
                case State::Idle:    return "waiting";
                case State::Warning: return "announced";
                case State::Running: return "running";
            }
            return "?";
        }

        std::string FormatMinutes(int64 ms)
        {
            uint32 minutes = uint32((std::max<int64>(0, ms) + MINUTE_MS - 1) / MINUTE_MS);
            return minutes == 1 ? "1 minute" : Acore::StringFormat("{} minutes", minutes);
        }

        bool IsBotPlayer(Player* player)
        {
            return player && GET_PLAYERBOT_AI(player);
        }

        bool IsRealParticipant(Player* player)
        {
            return player && player->IsInWorld() && !IsBotPlayer(player) && !player->IsGameMaster() &&
                   player->GetSession() && !player->GetSession()->IsBot();
        }

        void Tell(Player* player, std::string const& text, bool onScreen)
        {
            if (!IsRealParticipant(player) && !(player && player->IsGameMaster()))
                return;

            ChatHandler handler(player->GetSession());
            handler.SendSysMessage("|cffff8000[Living World]|r " + text);
            if (onScreen)
                handler.SendNotification(text);
        }

        struct Borrowed
        {
            ObjectGuid guid;
            WorldLocation home;
            bool teleported = false;
        };

        struct Slot
        {
            InvasionPlan const* plan = nullptr;
            uint32 index = 0;
            State state = State::Idle;
            int64 timer = 0;

            // The run.
            uint32 site = 0;
            uint8 level = 0;
            Map* map = nullptr;
            std::vector<ObjectGuid> summons;
            ObjectGuid portal;
            ObjectGuid boss;
            uint32 wave = 0;
            int64 waveTimer = 0;
            bool bossUp = false;
            bool won = false;
            std::vector<Borrowed> bots;
            uint32 botBudget = 0;

            InvasionSite const& Site() const { return plan->sites[site]; }
            EventSettings const& Conf() const { return settings.events[index]; }
        };

        class Manager
        {
        public:
            static Manager& Instance()
            {
                static Manager instance;
                return instance;
            }

            void Init()
            {
                if (_initialized)
                    return;
                _initialized = true;

                RegisterInvasionMobs();
                std::vector<InvasionPlan> const& plans = GetInvasionPlans();
                _slots.resize(plans.size());
                for (uint32 i = 0; i < plans.size(); ++i)
                {
                    _slots[i].plan = &plans[i];
                    _slots[i].index = i;
                    _slots[i].timer = RollInterval(_slots[i]);
                }
            }

            std::vector<Slot>& Slots() { return _slots; }

            Slot* FindSlot(std::string_view key)
            {
                for (Slot& slot : _slots)
                    if (key == slot.plan->key)
                        return &slot;
                return nullptr;
            }

            bool IsAvailable(Slot const& slot) const
            {
                return settings.enabled && slot.index < settings.events.size() && slot.Conf().enabled;
            }

            void QueueDeath(ObjectGuid guid, uint32 entry)
            {
                std::lock_guard<std::mutex> lock(_queueLock);
                _deaths.emplace_back(guid, entry);
            }

            void Update(uint32 diff)
            {
                if (!_initialized)
                    return;

                HandleDeaths();

                for (Slot& slot : _slots)
                    UpdateSlot(slot, diff);
            }

            // Real players who may take part: the level band, and the side of a one-faction site.
            std::vector<Player*> Eligible(Slot const& slot, int8 team) const
            {
                std::vector<Player*> out;
                uint32 minLevel = slot.Conf().minLevel;
                for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                {
                    if (!IsRealParticipant(player) || player->GetLevel() < minLevel)
                        continue;
                    if (team != TEAM_NEUTRAL && player->GetTeamId() != team)
                        continue;
                    out.push_back(player);
                }
                return out;
            }

            // Picks the site and the level. With players == nullptr (a GM start) any site will do.
            bool Prepare(Slot& slot, Player* gm)
            {
                std::vector<InvasionSite> const& sites = slot.plan->sites;
                if (sites.empty())
                    return false;

                // A one-faction site goes to the side with the most players of the band online.
                std::vector<uint32> weights(sites.size(), 0);
                uint32 total = 0;
                for (uint32 i = 0; i < sites.size(); ++i)
                {
                    weights[i] = uint32(Eligible(slot, sites[i].team).size());
                    total += weights[i];
                }

                if (gm)
                {
                    slot.site = urand(0, sites.size() - 1);
                    for (uint32 i = 0; i < sites.size(); ++i)
                        if (sites[i].team == gm->GetTeamId())
                            slot.site = i;
                }
                else
                {
                    if (!total)
                        return false;
                    uint32 roll = urand(1, total);
                    for (uint32 i = 0; i < sites.size(); ++i)
                    {
                        if (roll <= weights[i])
                        {
                            slot.site = i;
                            break;
                        }
                        roll -= weights[i];
                    }
                }

                // The level of the run: the median of the band's players at that site, or the GM's.
                std::vector<uint8> levels;
                for (Player* player : Eligible(slot, slot.Site().team))
                    levels.push_back(player->GetLevel());
                uint32 maxLevel = std::min<uint32>(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL), 80);
                uint32 level;
                if (!levels.empty())
                {
                    std::nth_element(levels.begin(), levels.begin() + levels.size() / 2, levels.end());
                    level = levels[levels.size() / 2];
                }
                else
                    level = gm ? gm->GetLevel() : maxLevel;
                slot.level = uint8(std::clamp<uint32>(level, std::max<uint32>(1, slot.Conf().minLevel), maxLevel));
                return true;
            }

            bool ForceStart(Slot& slot, Player* gm, std::string& error)
            {
                if (slot.state == State::Running)
                {
                    error = "it's already running";
                    return false;
                }
                if (slot.state == State::Idle && !Prepare(slot, gm))
                {
                    error = "it couldn't pick a place to happen";
                    return false;
                }
                if (!Begin(slot))
                {
                    error = "nothing could be spawned";
                    return false;
                }
                return true;
            }

            void ForceStop(Slot& slot)
            {
                if (slot.state != State::Idle)
                    Finish(slot, false);
            }

            uint32 BorrowedCount() const
            {
                uint32 n = 0;
                for (Slot const& slot : _slots)
                    n += slot.bots.size();
                return n;
            }

        private:
            int64 RollInterval(Slot const& slot) const
            {
                if (slot.index >= settings.events.size())
                    return int64(120) * MINUTE_MS;
                uint32 low = std::max<uint32>(1, slot.Conf().intervalMin);
                uint32 high = std::max(low, slot.Conf().intervalMax);
                return int64(urand(low, high)) * MINUTE_MS;
            }

            void HandleDeaths()
            {
                std::vector<std::pair<ObjectGuid, uint32>> deaths;
                {
                    std::lock_guard<std::mutex> lock(_queueLock);
                    deaths.swap(_deaths);
                }

                for (auto const& death : deaths)
                {
                    UntrackCreature(death.first);
                    for (Slot& slot : _slots)
                        if (slot.state == State::Running && slot.bossUp && death.first == slot.boss)
                            slot.won = true;
                }
            }

            void UpdateSlot(Slot& slot, uint32 diff)
            {
                if (slot.state == State::Running)
                {
                    RunWaves(slot, diff);
                    if (slot.won)
                    {
                        Finish(slot, true);
                        return;
                    }
                }

                slot.timer -= diff;
                if (slot.timer > 0)
                    return;

                switch (slot.state)
                {
                    case State::Idle:
                        TryAnnounce(slot);
                        break;
                    case State::Warning:
                        if (!Begin(slot))
                            Reschedule(slot, int64(std::max<uint32>(1, settings.retry)) * MINUTE_MS);
                        break;
                    case State::Running:
                        Finish(slot, false);
                        break;
                }
            }

            void TryAnnounce(Slot& slot)
            {
                int64 retry = int64(std::max<uint32>(1, settings.retry)) * MINUTE_MS;
                if (!IsAvailable(slot))
                {
                    slot.timer = retry;
                    return;
                }

                uint32 eligible = uint32(Eligible(slot, TEAM_NEUTRAL).size());
                if (eligible < std::max<uint32>(1, settings.minRealPlayers) || !Prepare(slot, nullptr))
                {
                    slot.timer = retry;
                    return;
                }

                if (!settings.warning)
                {
                    if (!Begin(slot))
                        Reschedule(slot, retry);
                    return;
                }

                slot.state = State::Warning;
                slot.timer = int64(settings.warning) * MINUTE_MS;
                std::string text = Acore::StringFormat(slot.plan->warningText, slot.Site().name, slot.Site().zone,
                    settings.warning);
                for (Player* player : Eligible(slot, slot.Site().team))
                    Tell(player, text, true);

                LOG_INFO("module", "CoA World: {} announced at {} (level {}), in {} minutes", slot.plan->name,
                    slot.Site().name, slot.level, settings.warning);
            }

            Position Ground(Position const& pos) const
            {
                if (!_runMap)
                    return pos;
                float ground = _runMap->GetHeight(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ() + 10.0f);
                if (ground > INVALID_HEIGHT && std::fabs(ground - pos.GetPositionZ()) < 20.0f)
                    return Position(pos.GetPositionX(), pos.GetPositionY(), ground + 0.5f, pos.GetOrientation());
                return pos;
            }

            Position PointAround(Position const& center, float minDist, float maxDist) const
            {
                float angle = frand(0.0f, 2.0f * float(M_PI));
                float dist = frand(minDist, maxDist);
                float x = center.GetPositionX() + dist * std::cos(angle);
                float y = center.GetPositionY() + dist * std::sin(angle);
                if (!_runMap)
                    return Position(x, y, center.GetPositionZ(), center.GetOrientation());

                float z = _runMap->GetHeight(x, y, center.GetPositionZ() + 10.0f);
                // Off a cliff or into a wall: stay on the center.
                if (z <= INVALID_HEIGHT || std::fabs(z - center.GetPositionZ()) >= 20.0f)
                    return Ground(center);
                return Position(x, y, z + 0.5f, center.GetOrientation());
            }

            Creature* Spawn(Slot& slot, uint32 entry, Position const& pos)
            {
                MobDef const* def = FindMob(entry);
                if (!def || !slot.map)
                    return nullptr;

                uint8 level = uint8(std::clamp<int32>(int32(slot.level) + def->levelOffset, 1, 83));
                SetPendingSpawn(def, level);
                TempSummon* summon = slot.map->SummonCreature(entry, pos, nullptr, CORPSE_DESPAWN_MS);
                ClearPendingSpawn();
                if (!summon)
                    return nullptr;

                // InitStats made it a timed summon; the timer only runs on the corpse instead.
                summon->SetTempSummonType(TEMPSUMMON_CORPSE_TIMED_DESPAWN);
                summon->setActive(true);
                TrackCreature(summon->GetGUID(), def);
                ApplyMobStats(summon, *def, settings.healthPct);
                slot.summons.push_back(summon->GetGUID());
                return summon;
            }

            uint32 CountAlive(Slot const& slot, std::vector<uint32> const& entries) const
            {
                if (!slot.map)
                    return 0;
                uint32 count = 0;
                for (ObjectGuid const& guid : slot.summons)
                    if (std::find(entries.begin(), entries.end(), guid.GetEntry()) != entries.end())
                        if (Creature* creature = slot.map->GetCreature(guid))
                            if (creature->IsAlive())
                                ++count;
                return count;
            }

            bool Begin(Slot& slot)
            {
                InvasionSite const& site = slot.Site();
                Map* map = sMapMgr->CreateBaseMap(site.mapId);
                if (!map || map->Instanceable())
                {
                    LOG_ERROR("module", "CoA World: {} has no map {}", slot.plan->name, site.mapId);
                    return false;
                }

                // Load the spots so the spawns land among the world's own creatures.
                map->LoadGridsInRange(site.spawn, 150.0f);
                map->LoadGridsInRange(site.target, 150.0f);
                slot.map = map;
                _runMap = map;

                slot.summons.clear();
                slot.bots.clear();
                slot.wave = 0;
                slot.waveTimer = slot.plan->firstWaveMs;
                slot.bossUp = false;
                slot.won = false;
                slot.boss.Clear();
                slot.portal.Clear();

                if (slot.plan->portalObject)
                    if (GameObject* portal = map->SummonGameObject(slot.plan->portalObject, Ground(site.spawn), 0.0f, 0.0f,
                            0.0f, 0.0f, DAY))
                    {
                        portal->setActive(true);
                        slot.portal = portal->GetGUID();
                    }

                for (uint32 i = 0; i < slot.plan->defenderCount && slot.plan->defender; ++i)
                {
                    // Two ranks across the target spot, facing the attackers.
                    float rank = float(i % 2) * 3.0f;
                    float across = (float(i / 2) - float(slot.plan->defenderCount) / 4.0f) * 3.0f;
                    Position pos(site.target.GetPositionX() + rank, site.target.GetPositionY() + across,
                        site.target.GetPositionZ(), site.target.GetOrientation());
                    Spawn(slot, slot.plan->defender, Ground(pos));
                }

                // Nothing on the map yet but a portal or a line is fine: the waves come next.
                slot.state = State::Running;
                slot.timer = int64(std::max<uint32>(1, settings.duration)) * MINUTE_MS;
                slot.botBudget = BotBudget();

                std::string text = Acore::StringFormat(slot.plan->startText, site.name, site.zone);
                for (Player* player : Eligible(slot, site.team))
                    Tell(player, text, true);

                RecruitBots(slot);
                LOG_INFO("module", "CoA World: {} started at {} (level {}, {} bots budget)", slot.plan->name, site.name,
                    slot.level, slot.botBudget);
                _runMap = nullptr;
                return true;
            }

            void RunWaves(Slot& slot, uint32 diff)
            {
                if (slot.bossUp)
                    return;

                slot.waveTimer -= diff;
                if (slot.waveTimer > 0)
                    return;

                slot.waveTimer = slot.plan->waveMs;
                ++slot.wave;
                _runMap = slot.map;

                InvasionPlan const& plan = *slot.plan;
                InvasionSite const& site = slot.Site();
                if (slot.wave > plan.waveCount)
                {
                    slot.bossUp = true;
                    uint32 entry = plan.bosses[urand(0, plan.bosses.size() - 1)];
                    if (Creature* boss = Spawn(slot, entry, Ground(site.spawn)))
                    {
                        slot.boss = boss->GetGUID();
                        if (plan.bossYell && *plan.bossYell)
                            boss->Yell(plan.bossYell, LANG_UNIVERSAL);
                        SendTo(boss, site.target, false);
                    }
                    else
                        slot.won = true;  // nothing to kill: don't hold the run for nothing

                    for (uint32 i = 0; i < plan.bossEscortCount && plan.bossEscort; ++i)
                        if (Creature* escort = Spawn(slot, plan.bossEscort, PointAround(site.spawn, 2.0f, 6.0f)))
                            SendTo(escort, PointAround(site.target, 0.0f, 8.0f), false);
                }
                else
                {
                    std::vector<uint32> army = plan.minions;
                    army.insert(army.end(), plan.elites.begin(), plan.elites.end());
                    uint32 alive = CountAlive(slot, army);
                    for (uint32 i = 0; i < plan.minionsBase + slot.wave && alive < plan.maxAlive; ++i, ++alive)
                    {
                        uint32 entry = plan.minions[urand(0, plan.minions.size() - 1)];
                        if (Creature* mob = Spawn(slot, entry, PointAround(site.spawn, 0.0f, 6.0f)))
                            SendTo(mob, PointAround(site.target, 0.0f, 12.0f));
                    }

                    if (!plan.elites.empty() && plan.eliteFromWave && slot.wave >= plan.eliteFromWave && alive < plan.maxAlive)
                        if (Creature* elite = Spawn(slot, plan.elites[urand(0, plan.elites.size() - 1)],
                                PointAround(site.spawn, 0.0f, 4.0f)))
                            SendTo(elite, PointAround(site.target, 0.0f, 8.0f));

                    if (plan.lieutenant && slot.wave == plan.lieutenantWave)
                        if (Creature* lieutenant = Spawn(slot, plan.lieutenant, Ground(site.spawn)))
                        {
                            if (plan.lieutenantYell && *plan.lieutenantYell)
                                lieutenant->Yell(plan.lieutenantYell, LANG_UNIVERSAL);
                            SendTo(lieutenant, site.target, false);
                        }
                }

                RecruitBots(slot);
                _runMap = nullptr;
            }

            void Finish(Slot& slot, bool won)
            {
                if (slot.state == State::Running)
                {
                    if (won)
                        Reward(slot);

                    std::string text = Acore::StringFormat(won ? slot.plan->wonText : slot.plan->lostText, slot.Site().name);
                    for (Player* player : Eligible(slot, slot.Site().team))
                        Tell(player, text, true);
                }

                ReleaseBots(slot);
                Despawn(slot);
                LOG_INFO("module", "CoA World: {} ended ({})", slot.plan->name, won ? "won" : "lost or stopped");
                Reschedule(slot, RollInterval(slot));
            }

            void Despawn(Slot& slot)
            {
                if (slot.map)
                {
                    for (ObjectGuid const& guid : slot.summons)
                    {
                        UntrackCreature(guid);
                        if (Creature* creature = slot.map->GetCreature(guid))
                            creature->DespawnOrUnsummon();
                    }
                    if (GameObject* portal = slot.map->GetGameObject(slot.portal))
                        portal->DespawnOrUnsummon();
                }
                else
                    for (ObjectGuid const& guid : slot.summons)
                        UntrackCreature(guid);

                slot.summons.clear();
                slot.portal.Clear();
                slot.boss.Clear();
                slot.map = nullptr;
            }

            void Reschedule(Slot& slot, int64 timer)
            {
                slot.state = State::Idle;
                slot.timer = timer;
            }

            // Everyone near the fight when the boss dies: gold for all, reputation and the item
            // for the real players.
            void Reward(Slot& slot)
            {
                InvasionPlan const& plan = *slot.plan;
                InvasionSite const& site = slot.Site();
                FactionEntry const* faction = plan.rewardFaction ? sFactionStore.LookupEntry(plan.rewardFaction) : nullptr;

                for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                {
                    if (!player || !player->IsInWorld() || player->GetMapId() != site.mapId)
                        continue;
                    float range = settings.rewardRange;
                    if (player->GetExactDist(&site.target) > range && player->GetExactDist(&site.spawn) > range &&
                        player->GetExactDist(&site.rally) > range)
                        continue;

                    if (settings.rewardSilverPerLevel)
                        player->ModifyMoney(int32(settings.rewardSilverPerLevel * player->GetLevel() * SILVER));

                    if (!IsRealParticipant(player))
                        continue;

                    if (faction && settings.rewardReputation)
                        player->GetReputationMgr().ModifyReputation(faction, float(settings.rewardReputation));

                    if (plan.rewardItem && plan.rewardItemCount)
                        GiveItem(player, plan, plan.rewardItem, plan.rewardItemCount);
                }
            }

            void GiveItem(Player* player, InvasionPlan const& plan, uint32 itemId, uint32 count)
            {
                ItemPosCountVec dest;
                if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) == EQUIP_ERR_OK)
                {
                    if (Item* item = player->StoreNewItem(dest, itemId, true))
                        player->SendNewItem(item, count, true, false);
                    return;
                }

                // Bags full: it comes by mail instead.
                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                MailDraft draft(plan.name, "Your share of the spoils. Your bags were too full to carry it.");
                if (Item* item = Item::CreateItem(itemId, count, player))
                {
                    item->SaveToDB(trans);
                    draft.AddItem(item);
                }
                draft.SendMailTo(trans, MailReceiver(player), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM));
                CharacterDatabase.CommitTransaction(trans);
            }

            // --- Bots ------------------------------------------------------------------------

            uint32 BotBudget() const
            {
                uint32 online = 0;
                for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
                    if (bot && bot->IsInWorld())
                        ++online;

                uint32 budget = std::min<uint32>(settings.botsMax, online * settings.botsPct / 100);
                uint32 busy = BorrowedCount();
                return budget > busy ? budget - busy : 0;
            }

            bool IsBorrowed(ObjectGuid guid) const
            {
                for (Slot const& slot : _slots)
                    for (Borrowed const& b : slot.bots)
                        if (b.guid == guid)
                            return true;
                return false;
            }

            // Spread over the run: a share at the start and at each wave, until the budget is used.
            void RecruitBots(Slot& slot)
            {
                if (!slot.botBudget || slot.bots.size() >= slot.botBudget)
                    return;

                uint32 share = (slot.botBudget + slot.plan->waveCount) / (slot.plan->waveCount + 1);
                uint32 want = std::min<uint32>(share, slot.botBudget - slot.bots.size());
                InvasionSite const& site = slot.Site();
                int32 low = int32(slot.level) - int32(settings.botsLevelRange);
                int32 high = int32(slot.level) + int32(settings.botsLevelRange);

                struct Candidate
                {
                    Player* bot;
                    float dist;
                };
                std::vector<Candidate> candidates;
                for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
                {
                    if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight() ||
                        bot->IsBeingTeleported())
                        continue;
                    if (int32(bot->GetLevel()) < low || int32(bot->GetLevel()) > high)
                        continue;
                    if (site.team != TEAM_NEUTRAL && bot->GetTeamId() != site.team)
                        continue;
                    if (bot->GetGroup() || bot->InBattleground() || bot->InArena() || bot->InBattlegroundQueue())
                        continue;
                    if (!bot->GetMap() || bot->GetMap()->Instanceable() || IsBorrowed(bot->GetGUID()))
                        continue;
                    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
                    if (!botAI || botAI->GetMaster())
                        continue;

                    float dist = bot->GetMapId() == site.mapId ? bot->GetExactDist(&site.target) : 1e9f;
                    candidates.push_back({ bot, dist });
                }

                std::sort(candidates.begin(), candidates.end(),
                    [](Candidate const& a, Candidate const& b) { return a.dist < b.dist; });

                for (Candidate const& c : candidates)
                {
                    if (!want)
                        break;

                    Player* bot = c.bot;
                    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
                    WorldPosition goal(site.mapId, site.target.GetPositionX(), site.target.GetPositionY(),
                        site.target.GetPositionZ());
                    Borrowed borrowed{ bot->GetGUID(), bot->GetWorldLocation(), false };

                    if (c.dist <= settings.botsNearRadius)
                    {
                        // Near: walks there through its usual travel.
                        botAI->rpgInfo.ChangeToGoGrind(goal);
                    }
                    else
                    {
                        // Far: brought behind the defenders, never under the eyes of a real player.
                        if (!settings.botsTeleport || botAI->HasPlayerNearby(150.0f))
                            continue;

                        Position rally = PointAround(site.rally, 0.0f, 10.0f);
                        bot->GetMotionMaster()->Clear();
                        botAI->Reset(true);
                        bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
                        if (!bot->TeleportTo(site.mapId, rally.GetPositionX(), rally.GetPositionY(), rally.GetPositionZ(),
                                rally.GetOrientation()))
                            continue;
                        botAI->rpgInfo.ChangeToGoGrind(goal);
                        borrowed.teleported = true;
                    }

                    slot.bots.push_back(borrowed);
                    --want;
                }
            }

            // Back to their usual life: whoever still heads for the fight or wanders there picks
            // something else, and those brought from afar go home when nobody sees them leave.
            void ReleaseBots(Slot& slot)
            {
                for (Borrowed const& b : slot.bots)
                {
                    Player* bot = ObjectAccessor::FindPlayer(b.guid);
                    if (!bot || !bot->IsInWorld())
                        continue;
                    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
                    if (!botAI || botAI->GetMaster())
                        continue;

                    NewRpgStatus status = botAI->rpgInfo.GetStatus();
                    if (status == RPG_GO_GRIND || status == RPG_WANDER_RANDOM)
                        botAI->rpgInfo.ChangeToIdle();

                    if (b.teleported && settings.botsReturnHome && bot->IsAlive() && !bot->IsInCombat() &&
                        !bot->GetGroup() && !bot->IsBeingTeleported() && !botAI->HasPlayerNearby(150.0f))
                    {
                        bot->GetMotionMaster()->Clear();
                        botAI->Reset(true);
                        bot->TeleportTo(b.home);
                    }
                }
                slot.bots.clear();
            }

            bool _initialized = false;
            std::vector<Slot> _slots;
            Map* _runMap = nullptr;  // the map Ground() reads while a run spawns

            std::mutex _queueLock;
            std::vector<std::pair<ObjectGuid, uint32>> _deaths;
        };

        Manager& Mgr() { return Manager::Instance(); }

        void LoadSettings()
        {
            auto key = [](std::string const& name) { return std::string(PREFIX) + name; };

            settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), false, false);
            settings.minRealPlayers = sConfigMgr->GetOption<uint32>(key("MinRealPlayers"), 1, false);
            settings.retry = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(key("RetryMinutes"), 5, false));
            settings.warning = sConfigMgr->GetOption<uint32>(key("WarningMinutes"), 10, false);
            settings.duration = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(key("DurationMinutes"), 30, false));
            settings.healthPct = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("HealthPct"), 100, false), 10, 1000);
            settings.rewardSilverPerLevel = sConfigMgr->GetOption<uint32>(key("RewardSilverPerLevel"), 25, false);
            settings.rewardReputation = sConfigMgr->GetOption<int32>(key("RewardReputation"), 150, false);
            settings.rewardRange = float(sConfigMgr->GetOption<uint32>(key("RewardRange"), 120, false));
            settings.botsPct = std::min<uint32>(100, sConfigMgr->GetOption<uint32>(key("BotsPct"), 10, false));
            settings.botsMax = sConfigMgr->GetOption<uint32>(key("BotsMax"), 120, false);
            settings.botsLevelRange = sConfigMgr->GetOption<uint32>(key("BotsLevelRange"), 5, false);
            settings.botsNearRadius = float(sConfigMgr->GetOption<uint32>(key("BotsNearRadius"), 500, false));
            settings.botsTeleport = sConfigMgr->GetOption<bool>(key("BotsTeleport"), true, false);
            settings.botsReturnHome = sConfigMgr->GetOption<bool>(key("BotsReturnHome"), true, false);

            std::vector<InvasionPlan> const& plans = GetInvasionPlans();
            settings.events.assign(plans.size(), EventSettings{});
            for (uint32 i = 0; i < plans.size(); ++i)
            {
                std::string prefix = Acore::StringFormat("{}{}.", PREFIX, plans[i].configName);
                EventSettings& s = settings.events[i];
                s.enabled = sConfigMgr->GetOption<bool>(prefix + "Enable", true, false);
                s.intervalMin = sConfigMgr->GetOption<uint32>(prefix + "IntervalMin", 120, false);
                s.intervalMax = sConfigMgr->GetOption<uint32>(prefix + "IntervalMax", 240, false);
                s.minLevel = sConfigMgr->GetOption<uint32>(prefix + "MinLevel", plans[i].defaultMinLevel, false);
            }
        }
    }

    void NotifyMobDied(Creature* creature)
    {
        if (creature)
            Mgr().QueueDeath(creature->GetGUID(), creature->GetEntry());
    }
}

using namespace CoaWorld;

class CoaWorldInvasionWorldScript : public WorldScript
{
public:
    CoaWorldInvasionWorldScript()
        : WorldScript("CoaWorldInvasionWorldScript",
                      { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        // At the first load the playerbots config isn't read yet: OnStartup does it.
        if (reload)
            LoadSettings();
    }

    void OnStartup() override
    {
        LoadSettings();
        Mgr().Init();
    }

    void OnUpdate(uint32 diff) override
    {
        if (settings.enabled || HasRun())
            Mgr().Update(diff);
    }

private:
    // Switched off while a run goes on: let it end on its own.
    static bool HasRun()
    {
        for (Slot const& slot : Mgr().Slots())
            if (slot.state != State::Idle)
                return true;
        return false;
    }
};

class CoaWorldInvasionCommandScript : public CommandScript
{
public:
    CoaWorldInvasionCommandScript() : CommandScript("CoaWorldInvasionCommandScript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable invasionTable = {
            { "next",  HandleNext,  SEC_PLAYER,     Console::No },
            { "list",  HandleList,  SEC_GAMEMASTER, Console::Yes },
            { "start", HandleStart, SEC_GAMEMASTER, Console::Yes },
            { "stop",  HandleStop,  SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable worldTable = {
            { "invasion", invasionTable },
        };
        static ChatCommandTable commandTable = {
            { "coaworld", worldTable },
        };
        return commandTable;
    }

    static bool HandleNext(ChatHandler* handler)
    {
        if (!settings.enabled)
        {
            handler->SendSysMessage("Invasions are switched off on this realm.");
            return true;
        }

        for (Slot& slot : Mgr().Slots())
        {
            if (!Mgr().IsAvailable(slot))
                continue;
            switch (slot.state)
            {
                case State::Running:
                    handler->PSendSysMessage("{} is happening now at {} ({}).", slot.plan->name, slot.Site().name,
                        slot.Site().zone);
                    break;
                case State::Warning:
                    handler->PSendSysMessage("{} at {} in {}.", slot.plan->name, slot.Site().name, FormatMinutes(slot.timer));
                    break;
                case State::Idle:
                    handler->PSendSysMessage("{}: next chance in about {} (from level {}).", slot.plan->name,
                        FormatMinutes(slot.timer), slot.Conf().minLevel);
                    break;
            }
        }
        return true;
    }

    static bool HandleList(ChatHandler* handler)
    {
        for (Slot& slot : Mgr().Slots())
        {
            handler->PSendSysMessage("{} ({}): {}{}, next step in {}, level {}, site {}, {} bots borrowed, {} creatures",
                slot.plan->name, slot.plan->key, StateName(slot.state), Mgr().IsAvailable(slot) ? "" : " (disabled)",
                FormatMinutes(slot.timer), slot.level, slot.state == State::Idle ? "-" : slot.Site().name,
                slot.bots.size(), slot.summons.size());
        }
        return true;
    }

    static Slot* FindOrComplain(ChatHandler* handler, std::string const& key)
    {
        Slot* slot = Mgr().FindSlot(key);
        if (!slot)
        {
            handler->SendSysMessage("No such invasion: scourge, legion or aq.");
            handler->SetSentErrorMessage(true);
        }
        return slot;
    }

    static bool HandleStart(ChatHandler* handler, std::string key)
    {
        Slot* slot = FindOrComplain(handler, key);
        if (!slot)
            return false;

        if (!Mgr().IsAvailable(*slot))
        {
            handler->PSendSysMessage("{} is switched off (AiPlayerbot.CoaWorld.Invasions.Enable or .{}.Enable).",
                slot->plan->name, slot->plan->configName);
            handler->SetSentErrorMessage(true);
            return false;
        }

        std::string error;
        if (!Mgr().ForceStart(*slot, handler->GetPlayer(), error))
        {
            handler->PSendSysMessage("{} didn't start: {}.", slot->plan->name, error);
            handler->SetSentErrorMessage(true);
            return false;
        }

        handler->PSendSysMessage("{} started at {} (level {}).", slot->plan->name, slot->Site().name, slot->level);
        return true;
    }

    static bool HandleStop(ChatHandler* handler, std::string key)
    {
        Slot* slot = FindOrComplain(handler, key);
        if (!slot)
            return false;

        Mgr().ForceStop(*slot);
        handler->PSendSysMessage("{} stopped.", slot->plan->name);
        return true;
    }
};

void AddSC_coa_world_invasions()
{
    new CoaWorldInvasionWorldScript();
    new CoaWorldInvasionCommandScript();
    AddSC_coa_world_invader_ai();
}
