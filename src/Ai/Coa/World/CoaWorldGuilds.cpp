/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: guilds of bots that live.
 *
 *  - Balance: a new random-bot guild is only made for the side with fewer (idea of Coralis' fork,
 *    GPL-2.0; one call in PlayerbotGuildMgr::AssignToGuild).
 *  - Message of the day: a bot guild's leader changes it now and then (once a day at most).
 *  - Guild chat: a bot that reaches a round level (every 10, and each level from 55) or loots a rare
 *    or better item says so in guild chat, and a guildmate online congratulates it a few seconds later.
 *  - Recruiting: now and then a member of a bot guild that is not full calls for members in General.
 *  - Helping the player: a real player in a guild with bots is greeted at login by a guildmate, and
 *    once a day at most receives a small gift by mail from one (potions and bandages of their level).
 *  - Guild orders: a real player asks the guild for an item in guild chat ("anyone have [link]?", "can
 *    someone craft [link]"). An online bot of the guild that has one sends it (free); else one that knows
 *    the recipe crafts it and mails it cash on delivery at the materials' price (no fee between
 *    guildmates); else one buys it out at the auction house and mails it at the price it paid. A few
 *    orders a day per player, items up to rare, an auction price cap.
 *  - The guild equips the player: at login or a level-up, every few days at most, a guildmate mails a
 *    piece of gear of the player's level (uncommon by default) that is clearly better than what the
 *    player wears in that slot, for their spec (the bots' own stat weights).
 *    Paid crafting for anyone, by whisper, is CoA Economie's work (CraftOrders).
 *
 * Lines come from IA MIND's bank (zz_coa_live_3_0_04: guild_ding, guild_gift, guild_recruit, guild_motd), with
 * English text as fallback. With CoaLive on, the congratulations, the rare loot and the hello at login are
 * CoaLive's own (guild_levelup, guild_self_loot, guild_loot, guild_login): they are not said twice here.
 * Level-ups and loot come from map threads and are queued; the world thread speaks.
 * Config: AiPlayerbot.CoaWorld.Guilds.*, off by default.
 */

#include "CoaWorldGuilds.h"
#include "CoaWorldGuildRules.h"
#include "CoaWorldAddonRules.h"
#include "CoaWorldState.h"

#include "CoaLive.h"
#include "CoaWorldBots.h"

#include "AuctionHouseMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomItemMgr.h"
#include "RandomPlayerbotMgr.h"
#include "SpellMgr.h"
#include "StatsWeightCalculator.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "WorldSession.h"

#include <algorithm>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    using CoaWorld::BotLine;
    using CoaWorld::BotsFrench;
    using CoaWorld::Tr;
    using CoaWorld::LogEvent;
    using CoaWorld::RandomBots;

    constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.Guilds.";

    struct Settings
    {
        bool enabled = false;
        bool balance = true;
        bool motd = true;
        bool chat = true;
        uint32 recruitMinutes = 45;
        bool help = true;
        uint32 giftHours = 24;
        bool orders = true;
        uint32 ordersPerDay = 3;
        uint32 orderMaxQuality = ITEM_QUALITY_RARE;
        uint32 orderAuctionMaxGold = 50;
        bool gear = true;
        uint32 gearDays = 3;
        uint32 gearQuality = ITEM_QUALITY_UNCOMMON;
        bool coaLive = false;  // AiPlayerbot.CoaLive.Enable: CoaLive says the guild reactions
        bool addon = false;    // the SquidBots Lite world protocol (GORD, GLIST), CoaWorldAddon.cpp
        bool greetings = false;  // guildmates answer a real player's hello and greet it at login
        bool gearAsk = false;    // a gift of gear is offered first (accept or refuse), mailed only if accepted
        uint32 loginGrace = 40;  // seconds after a real player's login before the guild's gifts and offers
        bool catalog = false;    // the addon's searchable list of what the guild's bots can send (GCAT)
    };

    Settings settings;

    struct Speech
    {
        ObjectGuid speaker;
        uint32 guildId;
        std::string text;
        time_t at;
        std::string situation = {};  // IA MIND's bank first (BotLine), text if it has nothing
        std::string player = {};
    };

    // From map threads: what happened, to be told by the world thread.
    struct GuildEvent
    {
        enum Kind : uint8 { LEVEL, LOOT, LOGIN, ORDER, HUMAN_LEVEL, GREET } kind;
        ObjectGuid guid;
        uint32 value;  // level, or item entry
    };

    // Player settings (character_settings) that keep the guild's gifts of gear across restarts.
    constexpr char const* GEAR_SETTING = "coa_world_guild";
    constexpr uint32 GEAR_SETTING_LAST_DAY = 0;

    std::mutex g_lock;
    std::vector<GuildEvent> g_events;

    void Push(GuildEvent e)
    {
        std::lock_guard<std::mutex> lock(g_lock);
        g_events.push_back(e);
    }

    // coa_world_guild_log (playerbots): the orders (kind 1, 7 days) and gear gifts (kind 2, 30 days) of each real
    // player, for the addon's GLIST. Its ids are ours (the addon's GOK comes before the insert is done).
    constexpr uint8 LOG_ORDER = 1;
    constexpr uint8 LOG_GIFT = 2;
    constexpr uint8 LOG_GIFT_REFUSED = 3;  // an item refused: not offered again for 30 days
    bool g_logReady = false;
    bool g_logChecked = false;
    uint32 g_nextLogId = 1;

    void LogInit(time_t now)
    {
        if (g_logChecked)
            return;
        g_logChecked = true;
        // The core aborts the server on a missing table or column: information_schema first.
        QueryResult check = PlayerbotsDatabase.Query(
            "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = "
            "'coa_world_guild_log' AND column_name IN ('id','guid','kind','item','count','state','bot','at','cod','reason')");
        if (!check || check->Fetch()[0].Get<uint64>() != 10)
        {
            LOG_ERROR("server.loading", ">> CoA World Guilds: table coa_world_guild_log missing or out of date "
                                        "(data/sql/playerbots not applied): the orders and gifts are not kept for the addon.");
            return;
        }
        g_logReady = true;
        if (QueryResult result = PlayerbotsDatabase.Query("SELECT MAX(`id`) FROM `coa_world_guild_log`"))
            g_nextLogId = result->Fetch()[0].Get<uint32>() + 1;
        PlayerbotsDatabase.Execute("DELETE FROM `coa_world_guild_log` WHERE (`kind` = {} AND `at` < {}) OR (`kind` IN ({}, {}) AND `at` < {})",
                                   uint32(LOG_ORDER), uint32(now - 7 * DAY), uint32(LOG_GIFT), uint32(LOG_GIFT_REFUSED),
                                   uint32(now - 30 * DAY));
    }

    uint32 LogRow(Player* player, uint8 kind, uint32 item, uint32 count, char const* state, std::string bot, time_t at,
                  uint64 cod, char const* reason)
    {
        uint32 const id = g_nextLogId++;
        if (!g_logReady)
            return id;
        PlayerbotsDatabase.EscapeString(bot);
        // Written at once: the addon asks GLIST about a second after its GORD.
        PlayerbotsDatabase.DirectExecute("INSERT INTO `coa_world_guild_log` (`id`, `guid`, `kind`, `item`, `count`, `state`, `bot`, "
                                   "`at`, `cod`, `reason`) VALUES ({}, {}, {}, {}, {}, '{}', '{}', {}, {}, '{}')",
                                   id, player->GetGUID().GetCounter(), uint32(kind), item, std::min<uint32>(count, 65535),
                                   state, bot, uint32(at), uint32(std::min<uint64>(cod, 0xFFFFFFFF)), reason);
        return id;
    }

    // What became of an order: its log id, its state and the reason of a refusal or "none".
    struct OrderResult
    {
        uint32 id = 0;
        char const* state = "none";
        char const* reason = "";
    };

    // A gift of gear offered to a real player (Guilds.GearAsk), mailed only if accepted. World thread; memory only
    // (a restart drops the offers; the player gets a new one at a login after GearDays).
    struct GiftOffer
    {
        uint32 id;
        ObjectGuid player, bot;
        std::string botName;
        uint32 item;
        int32 random;
        time_t at, expires;
        std::string reason;  // "ilvl +N" or "empty"
    };
    std::vector<GiftOffer> g_offers;
    // The bot's answer to the player's yes/no, sent 1 to 2 s later as a player would (not before the player's own echo).
    struct LaterWhisper
    {
        ObjectGuid bot, player;
        std::string text;
        uint32 dueMs;
    };
    std::vector<LaterWhisper> g_later;
    void WhisperLater(Player* bot, Player* player, std::string text)
    {
        g_later.push_back({ bot->GetGUID(), player->GetGUID(), std::move(text), getMSTime() + urand(1000, 2000) });
    }
    uint32 g_nextOffer = 1;
    constexpr time_t OFFER_SECONDS = DAY;

    bool IsBot(Player* player)
    {
        return player && GET_PLAYERBOT_AI(player);
    }

    // A real player. At a bot's login its PlayerbotAI is not attached yet: the session says it is a bot (a9 on the
    // LAPTOP-mini gave gear to bots, by mail to online bots).
    bool IsHuman(Player* player)
    {
        return player && player->GetSession() && !player->GetSession()->IsBot() && !GET_PLAYERBOT_AI(player) && !CoaWorld::IsTestDriver(player);
    }

    bool IsRandomBot(Player* player)
    {
        PlayerbotAI* botAI = player ? GET_PLAYERBOT_AI(player) : nullptr;
        return botAI && !botAI->GetMaster() && sRandomPlayerbotMgr.IsRandomBot(player);
    }

    std::string Link(ItemTemplate const* proto, uint32 level)
    {
        return Acore::StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:0:0:0:{}|h[{}]|h|r", ItemQualityColors[proto->Quality],
                                   proto->ItemId, level, proto->Name1);
    }

    std::string Money(uint64 copper)
    {
        if (copper >= GOLD)
            return Acore::StringFormat("{}g {}s", copper / GOLD, (copper % GOLD) / SILVER);
        if (copper >= SILVER)
            return Acore::StringFormat("{}s {}c", copper / SILVER, copper % SILVER);
        return Acore::StringFormat("{}c", copper);
    }

    // The recipes that make each item, found once (the crafting spells of the spell store).
    std::unordered_map<uint32, std::vector<uint32>> const& Recipes()
    {
        static std::unordered_map<uint32, std::vector<uint32>> recipes;
        static bool built = false;
        if (built)
            return recipes;
        built = true;
        for (uint32 id = 1; id < sSpellMgr->GetSpellInfoStoreSize(); ++id)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
            if (!info || info->Reagent[0] <= 0)
                continue;
            for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                if (info->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM && info->Effects[e].ItemType)
                    recipes[info->Effects[e].ItemType].push_back(id);
        }
        return recipes;
    }

    // Items a player can get on this realm: dropped (creature, object or reference loot) or sold by a vendor.
    std::unordered_set<uint32> Obtainable()
    {
        std::unordered_set<uint32> items;
        if (QueryResult result = WorldDatabase.Query(
                "SELECT Item FROM creature_loot_template UNION SELECT Item FROM reference_loot_template "
                "UNION SELECT Item FROM gameobject_loot_template UNION SELECT item FROM npc_vendor"))
        {
            do
                items.insert(result->Fetch()[0].Get<uint32>());
            while (result->NextRow());
        }
        return items;
    }

    bool LooksLikeTestItem(std::string const& name)
    {
        static char const* const marks[] = { "Test", "TEST", "Monster", "Deprecated", "DEPRECATED", "[PH]", "OLD",
                                             "BT48", "Unused", "UNUSED", "zz" };
        for (char const* mark : marks)
            if (name.find(mark) != std::string::npos)
                return true;
        return false;
    }

    // The gear the guild may give, by required level (1 to 80), found once: armour and weapons, uncommon or rare,
    // not bound on pickup, that drop or are sold on the realm (a9 gave "BT48 Cloth Spell Bracer3"), among the items
    // the bots' gear code knows (RandomItemMgr), when it knows any.
    std::vector<std::vector<uint32>> const& GearPool()
    {
        static std::vector<std::vector<uint32>> pool;
        if (!pool.empty())
            return pool;
        pool.resize(81);
        std::vector<std::vector<uint32>> unchecked(81);
        bool anyKnown = false;
        std::unordered_set<uint32> const obtainable = Obtainable();
        for (auto const& [id, proto] : *sObjectMgr->GetItemTemplateStore())
        {
            if (!obtainable.count(id) || LooksLikeTestItem(proto.Name1))
                continue;
            if ((proto.Class != ITEM_CLASS_ARMOR && proto.Class != ITEM_CLASS_WEAPON) ||
                proto.Quality < ITEM_QUALITY_UNCOMMON || proto.Quality > ITEM_QUALITY_RARE ||
                proto.Bonding == BIND_WHEN_PICKED_UP || proto.Bonding == BIND_QUEST_ITEM ||
                !proto.RequiredLevel || proto.RequiredLevel > 80 || proto.InventoryType == INVTYPE_NON_EQUIP ||
                proto.InventoryType == INVTYPE_BODY || proto.InventoryType == INVTYPE_TABARD ||
                proto.InventoryType == INVTYPE_AMMO || proto.InventoryType == INVTYPE_QUIVER ||
                proto.InventoryType == INVTYPE_RELIC)
                continue;
            unchecked[proto.RequiredLevel].push_back(id);
            if (sRandomItemMgr.HasStatWeight(id))
            {
                pool[proto.RequiredLevel].push_back(id);
                anyKnown = true;
            }
        }
        if (!anyKnown)
            pool = std::move(unchecked);
        return pool;
    }

    template <size_t N>
    char const* Pick(char const* const (&lines)[N])
    {
        return lines[urand(0, N - 1)];
    }

    // Potions and bandages of the level (all on CoA).
    std::pair<uint32, uint32> GiftFor(uint8 level)
    {
        if (level >= 45) return { 13446, 14529 };
        if (level >= 35) return { 3928, 8544 };
        if (level >= 21) return { 1710, 6450 };
        if (level >= 12) return { 929, 3530 };
        if (level >= 5) return { 858, 1251 };
        return { 118, 1251 };
    }

    class GuildLife
    {
    public:
        static GuildLife& Instance()
        {
            static GuildLife instance;
            return instance;
        }

        // The addon's GORD (world thread): the same order as in guild chat, at most qty.
        OrderResult AddonOrder(Player* player, uint32 entry, uint32 qty) { return Order(player, entry, qty, time(nullptr)); }

        void Update(uint32 diff)
        {
            if (!_welcomes.empty())
            {
                time_t const now = time(nullptr);
                for (auto itr = _welcomes.begin(); itr != _welcomes.end();)
                {
                    if (itr->second > now)
                    {
                        ++itr;
                        continue;
                    }
                    Player* player = ObjectAccessor::FindConnectedPlayer(itr->first);
                    if (player && player->IsInWorld() && player->GetGuildId() && IsHuman(player))
                        Welcome(player, now);
                    itr = _welcomes.erase(itr);
                }
            }
            if (!g_later.empty())
            {
                uint32 const nowMs = getMSTime();
                for (auto itr = g_later.begin(); itr != g_later.end();)
                {
                    if (int32(nowMs - itr->dueMs) < 0)
                    {
                        ++itr;
                        continue;
                    }
                    Player* bot = ObjectAccessor::FindPlayer(itr->bot);
                    Player* player = ObjectAccessor::FindConnectedPlayer(itr->player);
                    if (bot && player)
                        bot->Whisper(itr->text, LANG_UNIVERSAL, player);
                    itr = g_later.erase(itr);
                }
            }
            {
                time_t const now = time(nullptr);
                g_offers.erase(std::remove_if(g_offers.begin(), g_offers.end(), [now](GiftOffer const& o) { return o.expires <= now; }),
                               g_offers.end());
            }
            _timer += diff;
            if (_timer < 2 * IN_MILLISECONDS)
                return;
            uint32 elapsed = _timer;
            _timer = 0;
            time_t now = time(nullptr);

            HandleEvents(now);
            Speak(now);

            _slow += elapsed;
            if (_slow >= 10 * MINUTE * IN_MILLISECONDS)
            {
                _slow = 0;
                if (settings.motd)
                    Motds(now);
            }

            _recruit += elapsed;
            if (settings.recruitMinutes && _recruit >= settings.recruitMinutes * MINUTE * IN_MILLISECONDS)
            {
                _recruit = 0;
                Recruit();
            }
        }

    private:
        // An online bot of the guild other than "not", for a reply.
        static Player* OnlineGuildBot(uint32 guildId, ObjectGuid notGuid)
        {
            std::vector<Player*> found;
            for (auto const& [guid, bot] : RandomBots())
                if (bot && bot->IsInWorld() && bot->GetGuildId() == guildId && bot->GetGUID() != notGuid)
                    found.push_back(bot);
            return found.empty() ? nullptr : found[urand(0, found.size() - 1)];
        }

        static bool HumanOnlineInGuild(uint32 guildId)
        {
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
                if (player && player->IsInWorld() && player->GetGuildId() == guildId && !GET_PLAYERBOT_AI(player))
                    return true;
            return false;
        }

        void Say(Player* speaker, std::string text, time_t at)
        {
            if (speaker && speaker->GetGuildId())
                _speeches.push_back({ speaker->GetGUID(), speaker->GetGuildId(), std::move(text), at });
        }

        void HandleEvents(time_t now)
        {
            std::vector<GuildEvent> events;
            {
                std::lock_guard<std::mutex> lock(g_lock);
                events.swap(g_events);
            }

            for (GuildEvent const& e : events)
            {
                Player* player = ObjectAccessor::FindPlayer(e.guid);
                if (!player || !player->IsInWorld() || !player->GetGuildId())
                    continue;

                switch (e.kind)
                {
                    case GuildEvent::LEVEL:
                    {
                        if (Guild* g = sGuildMgr->GetGuildById(player->GetGuildId()))
                            LogEvent("guild", player->GetName(), Acore::StringFormat("level {} in <{}>", e.value, g->GetName()));
                        if (!HumanOnlineInGuild(player->GetGuildId()))
                            break;
                        if (BotLine(player, "guild_ding", { { "level", std::to_string(e.value) } }))
                        {
                            if (!settings.coaLive)
                                if (Player* other = OnlineGuildBot(player->GetGuildId(), player->GetGUID()))
                                    Say(other, Acore::StringFormat("gz {}", player->GetName()), now + urand(6, 15));
                            break;
                        }
                        static char const* const ding[] = { "ding {}", "{}, finally", "ding! {} now", "and {}. slowly getting there" };
                        static char const* const gz[] = { "gz", "grats!", "gz {}", "nice, gz" };
                        Say(player, Acore::StringFormat(Pick(ding), e.value), now + urand(1, 4));
                        if (!settings.coaLive)
                            if (Player* other = OnlineGuildBot(player->GetGuildId(), player->GetGUID()))
                                Say(other, Acore::StringFormat(Pick(gz), player->GetName()), now + urand(6, 15));
                        break;
                    }
                    case GuildEvent::LOOT:
                    {
                        if (ItemTemplate const* looted = sObjectMgr->GetItemTemplate(e.value))
                            LogEvent("guild", player->GetName(), Acore::StringFormat("looted {}", looted->Name1));
                        if (settings.coaLive || !HumanOnlineInGuild(player->GetGuildId()))
                            break;
                        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(e.value);
                        if (!proto)
                            break;
                        std::string link = Acore::StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:0:0:0:{}|h[{}]|h|r",
                            ItemQualityColors[proto->Quality], proto->ItemId, player->GetLevel(), proto->Name1);
                        static char const* const got[] = { "finally {}", "got {} :)", "{} dropped!", "look at this, {}" };
                        static char const* const gz[] = { "gz!", "nice drop", "grats", "jealous, gz" };
                        Say(player, Acore::StringFormat(Pick(got), link), now + urand(2, 6));
                        if (Player* other = OnlineGuildBot(player->GetGuildId(), player->GetGUID()))
                            Say(other, Pick(gz), now + urand(8, 18));
                        break;
                    }
                    case GuildEvent::LOGIN:
                        if (IsHuman(player))
                        {
                            // The gifts and offers wait loginGrace: a player who just logged in is not met with a
                            // gear offer in the first second. The guild's "hi"s keep their own few seconds.
                            _welcomes.push_back({ player->GetGUID(), now + settings.loginGrace });
                            if (settings.greetings)
                                Greet(player, now, true);
                        }
                        break;
                    case GuildEvent::GREET:
                        if (IsHuman(player))
                            Greet(player, now, false);
                        break;
                    case GuildEvent::ORDER:
                        if (IsHuman(player))
                            Order(player, e.value, 0, now);
                        break;
                    case GuildEvent::HUMAN_LEVEL:
                        if (IsHuman(player))
                            if (Player* bot = OnlineGuildBot(player->GetGuildId(), player->GetGUID()))
                                GiveGear(player, bot, now);
                        break;
                }
            }
        }

        void Speak(time_t now)
        {
            std::vector<Speech> later;
            for (Speech& s : _speeches)
            {
                if (s.at > now)
                {
                    later.push_back(std::move(s));
                    continue;
                }
                Player* speaker = ObjectAccessor::FindPlayer(s.speaker);
                if (!speaker || !speaker->IsInWorld() || speaker->GetGuildId() != s.guildId)
                    continue;
                if (!s.situation.empty() && BotLine(speaker, s.situation.c_str(), { { "player", s.player } }))
                    continue;
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(speaker); botAI && CoaWorld::BotChatOn())
                    botAI->SayToGuild(s.text);
            }
            _speeches.swap(later);
        }

        // A real player of a guild with bots logs in: a guildmate says hello, and now and then sends a gift.
        // Guildmates answer a real player's hello in guild chat (1 to 3, not again within 10 minutes), or greet it at
        // login (1 or 2; 0 or 1 more when CoaLive already has one say it). Each after a human delay, never all at once.
        void Greet(Player* player, time_t now, bool login)
        {
            time_t& last = _greeted[player->GetGUID().GetCounter()];
            if (!login && last && now - last < 10 * MINUTE)
                return;
            last = now;
            std::vector<Player*> mates;
            for (auto const& [guid, bot] : RandomBots())
                if (bot && bot->IsInWorld() && bot->GetGuildId() == player->GetGuildId())
                    mates.push_back(bot);
            if (mates.empty())
                return;
            std::shuffle(mates.begin(), mates.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
            uint32 const count = login ? (settings.coaLive ? urand(0, 1) : urand(1, 2)) : urand(1, 3);
            bool const fr = BotsFrench();
            static char const* const replyEn[] = { "hey {}", "hi {}!", "o/", "yo {}", "hey hey", "hello {}" };
            static char const* const replyFr[] = { "salut {}", "coucou {}", "yo", "slt", "bonjour {} !", "o/" };
            static char const* const welcomeEn[] = { "hey {}", "wb {}", "re", "o/ {}" };
            static char const* const welcomeFr[] = { "salut {}", "re", "wb {}", "coucou {} !" };
            std::vector<time_t> delays;
            for (uint32 i = 0; i < count && i < mates.size(); ++i)
                delays.push_back(now + urand(2, 20));
            std::sort(delays.begin(), delays.end());
            for (size_t i = 0; i < delays.size(); ++i)
            {
                char const* const* pool = login ? (fr ? welcomeFr : welcomeEn) : (fr ? replyFr : replyEn);
                size_t const size = login ? 4 : 6;
                std::string text = Acore::StringFormat(pool[urand(0, size - 1)], player->GetName());
                Speech speech{ mates[i]->GetGUID(), mates[i]->GetGuildId(), text, delays[i] + time_t(i) };
                speech.situation = login ? "guild_welcome" : "guild_greet_reply";
                speech.player = player->GetName();
                _speeches.push_back(std::move(speech));
            }
        }

        void Welcome(Player* player, time_t now)
        {
            Player* bot = OnlineGuildBot(player->GetGuildId(), player->GetGUID());
            if (!bot)
                return;

            static char const* const hi[] = { "hi {}", "hey {}!", "o/ {}", "wb {}" };
            static char const* const hiFr[] = { "salut {}", "hey {} !", "o/ {}", "re {}" };
            if (!settings.coaLive && !settings.greetings && roll_chance_i(60))
                Say(bot, Acore::StringFormat(BotsFrench() ? Pick(hiFr) : Pick(hi), player->GetName()), now + urand(4, 12));

            GiveGear(player, bot, now);

            if (!settings.help)
                return;
            time_t& last = _lastGift[player->GetGUID().GetCounter()];
            if (last && now - last < time_t(settings.giftHours) * HOUR)
                return;
            if (!roll_chance_i(35))
                return;
            last = now;

            auto [potion, bandage] = GiftFor(player->GetLevel());
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            MailDraft draft(Tr("From the guild", "De la part de la guilde"),
                            Acore::StringFormat(Tr("Had some spare, thought you could use them. - {}",
                                                   "J'en avais en trop, je me suis dit que \u00e7a te servirait. - {}"), bot->GetName()));
            if (Item* item = Item::CreateItem(potion, 5, player))
            {
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
            if (Item* item = Item::CreateItem(bandage, 10, player))
            {
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
            draft.SendMailTo(trans, MailReceiver(player), MailSender(bot), MAIL_CHECK_MASK_NONE, 0);
            CharacterDatabase.CommitTransaction(trans);
            if (!BotLine(bot, "guild_gift", { { "player", player->GetName() } }))
                Say(bot, Acore::StringFormat(Tr("{}, sent you a few potions", "{}, je t'ai envoy\u00e9 quelques potions"), player->GetName()), now + urand(20, 40));
        }

        // An item of the bot's bags by mail to the player, free or cash on delivery.
        static void MailFromBags(Player* bot, Player* to, Item* item, uint64 cod, uint32 delay)
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
            item->DeleteFromInventoryDB(trans);
            item->SetOwnerGUID(to->GetGUID());
            item->SetState(ITEM_CHANGED);  // as MailHandler: saved standalone, in no player's update queue
            item->SaveToDB(trans);
            MailDraft draft(item->GetTemplate()->Name1, Tr("From the guild.", "De la part de la guilde."));
            draft.AddItem(item);
            if (cod)
                draft.AddCOD(uint32(cod));
            draft.SendMailTo(trans, MailReceiver(to), MailSender(bot), MAIL_CHECK_MASK_COPIED, delay);
            bot->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);
        }

        // A new item by mail to the player, from a guildmate.
        static bool MailNew(Player* bot, Player* to, uint32 entry, uint32 count, uint64 cod, uint32 delay,
                            std::string const& body, int32 randomProperty = 0)
        {
            // Owned by the receiver, as the core's mailed rewards; with the random suffix the gift was scored with.
            Item* item = Item::CreateItem(entry, count, to, false, randomProperty);
            if (!item)
                return false;
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            item->SaveToDB(trans);
            MailDraft draft(item->GetTemplate()->Name1, body);
            draft.AddItem(item);
            if (cod)
                draft.AddCOD(uint32(cod));
            draft.SendMailTo(trans, MailReceiver(to), MailSender(bot), MAIL_CHECK_MASK_COPIED, delay);
            CharacterDatabase.CommitTransaction(trans);
            return true;
        }

        void Answer(Player* bot, char const* situation, CoaWorld::Slots const& slots, std::string fallback, time_t at)
        {
            if (!BotLine(bot, situation, slots))
                Say(bot, std::move(fallback), at);
        }

        // A real player asked the guild for an item: from a guildmate's bags, from one who can craft it, or from
        // the auction house; the first that can.
        static std::string GuildName(Player* player)
        {
            Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId());
            return guild ? guild->GetName() : std::string();
        }

        // An item of the bot's bags it may mail (as MailHandler: never equipped, traded, a bag, or soulbound), whatever
        // its stack (the order splits a bigger one than asked).
        static Item* BagItem(Player* bot, uint32 entry)
        {
            auto fits = [&](Item* item)
            {
                return item && item->GetEntry() == entry && !item->IsEquipped() && !item->IsInTrade() && !item->IsBag() &&
                       item->CanBeTraded(true);
            };
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot); fits(item))
                    return item;
            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
                if (Bag* bag = bot->GetBagByPos(bagSlot))
                    for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                        if (Item* item = bag->GetItemByPos(uint8(slot)); fits(item))
                            return item;
            return nullptr;
        }

        OrderResult Logged(Player* player, uint32 entry, uint32 count, char const* state, char const* reason,
                           std::string const& bot, uint64 cod, time_t now)
        {
            OrderResult result;
            result.id = LogRow(player, LOG_ORDER, entry, count, state, bot, now, cod, reason);
            result.state = state;
            result.reason = reason;
            CoaWorldState::Order(GuildName(player), entry, count, state, bot);
            return result;
        }

        // qty: at most this many (the addon's GORD), 0 for any (guild chat).
        OrderResult Order(Player* player, uint32 entry, uint32 qty, time_t now)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
            if (!proto || proto->Bonding == BIND_QUEST_ITEM)
                return Logged(player, entry, 0, "refused", "item", "", 0, now);
            if (proto->Quality > settings.orderMaxQuality)
                return Logged(player, entry, 0, "refused", "quality", "", 0, now);
            uint32 const today = uint32(now / DAY);
            std::pair<uint32, uint32>& count = _orders[player->GetGUID().GetCounter()];
            if (count.first != today)
                count = { today, 0 };
            if (!CoaWorldGuildRules::MayOrder(count.second, settings.ordersPerDay))
                return Logged(player, entry, 0, "refused", "daily", "", 0, now);

            std::vector<Player*> mates;
            for (auto const& [guid, bot] : RandomBots())
                if (bot && bot->IsInWorld() && bot->GetGuildId() == player->GetGuildId() && bot->IsAlive())
                    mates.push_back(bot);
            if (mates.empty())
                return Logged(player, entry, 0, "none", "nobot", "", 0, now);
            std::shuffle(mates.begin(), mates.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
            CoaWorld::Slots slots = { { "player", player->GetName() }, { "item", Link(proto, player->GetLevel()) } };

            // 1. From the bags: free.
            for (Player* bot : mates)
            {
                // What the core lets a player mail (MailHandler): never a bag in a bag slot or with items in it, a
                // soulbound item or the loot being looted. A bag moved out with its contents left them dangling.
                Item* item = BagItem(bot, entry);
                if (!item)
                    continue;
                uint32 sent = item->GetCount();
                if (qty && sent > qty)
                {
                    // Part of a stack (one Silverleaf out of twenty): the rest stays in the bot's bags.
                    if (!MailNew(bot, player, entry, qty, 0, 0, Tr("From the guild.", "De la part de la guilde.")))
                        continue;
                    item->SetCount(sent - qty);
                    item->SetState(ITEM_CHANGED, bot);
                    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                    bot->SaveInventoryAndGoldToDB(trans);
                    CharacterDatabase.CommitTransaction(trans);
                    sent = qty;
                }
                else
                    MailFromBags(bot, player, item, 0, 0);
                ++count.second;
                Answer(bot, "guild_order_share", slots, Acore::StringFormat(Tr("{} I have one, sending it", "{} j'en ai un, je te l'envoie"), player->GetName()),
                       now + urand(5, 15));
                LogEvent("guild", bot->GetName(), Acore::StringFormat("sent {} to {}", proto->Name1, player->GetName()));
                return Logged(player, entry, sent, "shared", "", bot->GetName(), 0, now);
            }

            // 2. Crafted: cash on delivery at the materials' price.
            auto const recipes = Recipes().find(entry);
            if (recipes != Recipes().end() && proto->Bonding != BIND_WHEN_PICKED_UP)
                for (Player* bot : mates)
                    for (uint32 spellId : recipes->second)
                    {
                        if (!bot->HasSpell(spellId))
                            continue;
                        SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
                        uint64 cost = 0;
                        for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                            if (info->Reagent[r] > 0 && info->ReagentCount[r])
                                if (ItemTemplate const* reagent = sObjectMgr->GetItemTemplate(info->Reagent[r]))
                                    cost += CoaWorldGuildRules::ReagentPrice(reagent->BuyPrice, reagent->SellPrice) *
                                            info->ReagentCount[r];
                        uint32 made = 1;
                        for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                            if (info->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM && info->Effects[e].ItemType == entry)
                                made = uint32(std::max<int32>(1, info->Effects[e].CalcValue(bot)));
                        uint32 const sent = std::min<uint32>(std::min<uint32>(made, proto->GetMaxStackSize()), qty ? qty : made);
                        if (!MailNew(bot, player, entry, sent, cost, 0,
                                     Tr("Made it for you, you only pay the materials.",
                                        "Fait pour toi, tu ne paies que les composants.")))
                            continue;
                        ++count.second;
                        slots["price"] = Money(cost);
                        Answer(bot, "guild_order_craft", slots,
                               Acore::StringFormat(Tr("{} I can make that, mats only ({}), will mail it", "{} je peux te le faire, juste les compos ({}), je te l'envoie"), player->GetName(),
                                                   Money(cost)), now + urand(5, 15));
                        LogEvent("guild", bot->GetName(), Acore::StringFormat("crafts {} for {}", proto->Name1,
                                                                               player->GetName()));
                        return Logged(player, entry, sent, "crafted", "", bot->GetName(), cost, now);
                    }

            // 3. Bought out at the auction house of the player's side.
            OrderResult bought;
            if (Auction(player, mates, proto, slots, qty, now, bought))
            {
                ++count.second;
                return bought;
            }

            if (roll_chance_i(50))
                Answer(mates.front(), "guild_order_none", slots,
                       Acore::StringFormat(Tr("sorry {}, nobody has that right now", "d\u00e9sol\u00e9 {}, personne n'a \u00e7a pour l'instant"), player->GetName()), now + urand(8, 20));
            return Logged(player, entry, 0, "none", "", "", 0, now);
        }

        bool Auction(Player* player, std::vector<Player*> const& mates, ItemTemplate const* proto,
                     CoaWorld::Slots& slots, uint32 qty, time_t now, OrderResult& result)
        {
            AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(player->GetFaction());
            if (!house)
                return false;
            // The cheapest stack of at most qty; else the cheapest of any size (the player sees the price on delivery).
            AuctionEntry* best = nullptr;
            AuctionEntry* bestAny = nullptr;
            for (auto const& [id, auction] : house->GetAuctions())
            {
                if (!auction || auction->item_template != proto->ItemId || !auction->buyout || auction->bidder ||
                    auction->owner == player->GetGUID())
                    continue;
                if ((!qty || auction->itemCount <= qty) && (!best || auction->buyout < best->buyout))
                    best = auction;
                if (!bestAny || auction->buyout < bestAny->buyout)
                    bestAny = auction;
            }
            if (!best)
                best = bestAny;
            if (!best || best->buyout > uint64(settings.orderAuctionMaxGold) * GOLD)
                return false;
            Player* buyer = nullptr;
            for (Player* bot : mates)
                if (bot->GetMoney() >= best->buyout && bot->GetGUID() != best->owner)
                {
                    buyer = bot;
                    break;
                }
            Item* item = buyer ? sAuctionMgr->GetAItem(best->item_guid) : nullptr;
            if (!item)
                return false;

            // As a buyout by the bot (AuctionHouseHandler), but the item goes on to the player instead of the won mail.
            uint32 const price = best->buyout;
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            buyer->ModifyMoney(-int32(price));
            best->bidder = buyer->GetGUID();
            best->bid = price;
            sAuctionMgr->SendAuctionSalePendingMail(best, trans);
            sAuctionMgr->SendAuctionSuccessfulMail(best, trans);
            sScriptMgr->OnAuctionSuccessful(house, best);
            best->DeleteFromDB(trans);
            sAuctionMgr->RemoveAItem(best->item_guid);
            house->RemoveAuction(best);
            item->SetOwnerGUID(player->GetGUID());
            item->SetState(ITEM_CHANGED);  // the new owner reaches item_instance (an auction item is saved as unchanged)
            item->SaveToDB(trans);
            MailDraft(proto->Name1, Tr("Got it at the auction house, you pay what I paid.",
                                       "Trouv\u00e9 \u00e0 l'h\u00f4tel des ventes, tu paies ce que j'ai pay\u00e9."))
                .AddItem(item)
                .AddCOD(price)
                .SendMailTo(trans, MailReceiver(player), MailSender(buyer), MAIL_CHECK_MASK_COPIED, 0);
            buyer->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);

            slots["price"] = Money(price);
            Answer(buyer, "guild_order_ah", slots,
                   Acore::StringFormat(Tr("{} saw one on the AH for {}, I'll grab it for you", "{} j'en ai vu un \u00e0 l'HV pour {}, je te le prends"), player->GetName(), Money(price)),
                   now + urand(5, 15));
            LogEvent("guild", buyer->GetName(), Acore::StringFormat("bought {} at the auction house for {}", proto->Name1,
                                                                     player->GetName()));
            result = Logged(player, proto->ItemId, item->GetCount(), "bought", "", buyer->GetName(), price, now);
            return true;
        }

        // The items this player refused in the last 30 days (coa_world_guild_log kind 3).
        static std::unordered_set<uint32> Refused(Player* player, time_t now)
        {
            std::unordered_set<uint32> items;
            if (!g_logReady)
                return items;
            if (QueryResult result = PlayerbotsDatabase.Query(
                    "SELECT `item` FROM `coa_world_guild_log` WHERE `guid` = {} AND `kind` = {} AND `at` >= {}",
                    player->GetGUID().GetCounter(), uint32(LOG_GIFT_REFUSED), uint32(now - 30 * DAY)))
            {
                do
                    items.insert(result->Fetch()[0].Get<uint32>());
                while (result->NextRow());
            }
            return items;
        }

        // Guilds.GearAsk: the gift waits for the player's answer (addon GACC/GREF, or a whisper yes/no to the bot).
        void Offer(Player* player, Player* bot, ItemTemplate const* proto, int32 random, std::string const& reason,
                   uint32 today, time_t now)
        {
            g_offers.erase(std::remove_if(g_offers.begin(), g_offers.end(),
                                          [&](GiftOffer const& o) { return o.player == player->GetGUID(); }),
                           g_offers.end());
            GiftOffer offer{ g_nextOffer++, player->GetGUID(), bot->GetGUID(), bot->GetName(), proto->ItemId, random, now,
                             now + OFFER_SECONDS, reason };
            g_offers.push_back(offer);
            player->UpdatePlayerSetting(GEAR_SETTING, GEAR_SETTING_LAST_DAY, today);  // the period's gift, whatever the answer
            std::string const link = Link(proto, player->GetLevel());
            if (!BotLine(bot, "guild_gift_offer", { { "player", player->GetName() }, { "item", link } }))
                bot->Whisper(Acore::StringFormat(Tr("found {} for you, want it? (yes/no)", "j'ai trouv\u00e9 {} pour toi, tu le veux ? (oui/non)"), link), LANG_UNIVERSAL, player);
            if (settings.addon)
                CoaWorldAddonSendLines(player, { OfferLine(offer) });
        }

    public:
        static std::string OfferLine(GiftOffer const& o)
        {
            return Acore::StringFormat("GF\t{}\t{}\t{}\t{}\t{}\t{}", o.id, o.item, o.botName, int64(o.at), int64(o.expires),
                                       o.reason);
        }

        // The player's answer: mailed if accepted; a refusal blocks that item 30 days. False: no such offer.
        bool AnswerOffer(Player* player, uint32 offerId, bool accept)
        {
            auto itr = std::find_if(g_offers.begin(), g_offers.end(), [&](GiftOffer const& o)
                                    { return o.id == offerId && o.player == player->GetGUID(); });
            if (itr == g_offers.end())
                return false;
            GiftOffer const offer = *itr;
            g_offers.erase(itr);
            time_t const now = time(nullptr);
            Player* bot = ObjectAccessor::FindPlayer(offer.bot);
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(offer.item);
            if (!accept)
            {
                LogRow(player, LOG_GIFT_REFUSED, offer.item, 1, "refused", offer.botName, now, 0, "");
                if (bot)
                    WhisperLater(bot, player, Tr("no worries", "pas de souci"));
                return true;
            }
            if (bot && proto &&
                MailNew(bot, player, offer.item, 1, 0, 0,
                        Acore::StringFormat(Tr("Here it is. - {}", "Le voil\u00e0. - {}"), offer.botName), offer.random))
            {
                LogEvent("guild", offer.botName, Acore::StringFormat("gave {} to {}", proto->Name1, player->GetName()));
                LogRow(player, LOG_GIFT, offer.item, 1, "gift", offer.botName, now, 0, "");
                WhisperLater(bot, player, Tr("sent, check your mail", "envoy\u00e9, regarde ta bo\u00eete aux lettres"));
            }
            return true;
        }

        // A whisper "yes"/"no" to the bot that made the player an offer.
        bool Whispered(Player* player, Player* bot, std::string msg)
        {
            std::transform(msg.begin(), msg.end(), msg.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            bool const yes = msg == "yes" || msg == "y" || msg == "oui" || msg == "ok" || msg == "sure";
            bool const no = msg == "no" || msg == "n" || msg == "non" || msg == "nah";
            if (!yes && !no)
                return false;
            for (GiftOffer const& o : g_offers)
                if (o.player == player->GetGUID() && o.bot == bot->GetGUID())
                    return AnswerOffer(player, o.id, yes);
            return false;
        }

    private:
        // A piece of gear of the player's level, clearly better than what they wear there, every gearDays at most.
        void GiveGear(Player* player, Player* bot, time_t now)
        {
            if (!settings.gear || player->GetLevel() < 5)
                return;
            uint32 const today = uint32(now / DAY);
            if (!CoaWorldGuildRules::GiftDue(today, player->GetPlayerSetting(GEAR_SETTING, GEAR_SETTING_LAST_DAY).value,
                                             settings.gearDays))
                return;

            std::vector<uint32> candidates;
            auto const& pool = GearPool();
            for (uint32 level = player->GetLevel() > 4 ? player->GetLevel() - 4 : 1;
                 level <= player->GetLevel() && level < pool.size(); ++level)
                for (uint32 entry : pool[level])
                    if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry))
                        if (proto->Quality <= settings.gearQuality &&
                            CoaWorldGuildRules::GiftLevelFits(proto->RequiredLevel, player->GetLevel()))
                            candidates.push_back(entry);
            if (candidates.empty())
                return;
            std::shuffle(candidates.begin(), candidates.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
            if (candidates.size() > 300)
                candidates.resize(300);

            StatsWeightCalculator calc(player);
            // A real player's role, as the bots' gear code reads it (by spec): the Cold-Iron Scepter (spell power
            // only) went to a physical spec for its weapon damage (user's report, 09/11).
            bool const caster = PlayerbotAI::IsCaster(player, true) || PlayerbotAI::IsHeal(player, true);
            uint32 bestEntry = 0;
            int32 bestRandom = 0;
            float bestGain = 0.0f;
            std::string bestReason;
            std::unordered_set<uint32> const refused = Refused(player, now);
            for (uint32 entry : candidates)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                if (refused.count(entry) || player->CanUseItem(proto) != EQUIP_ERR_OK)
                    continue;
                int physical = 0, casterStats = 0;
                for (uint8 i = 0; i < proto->StatsCount && i < MAX_ITEM_PROTO_STATS; ++i)
                    switch (proto->ItemStat[i].ItemStatType)
                    {
                        case ITEM_MOD_STRENGTH: case ITEM_MOD_AGILITY: case ITEM_MOD_ATTACK_POWER:
                        case ITEM_MOD_RANGED_ATTACK_POWER:
                            ++physical;
                            break;
                        case ITEM_MOD_INTELLECT: case ITEM_MOD_SPIRIT: case ITEM_MOD_SPELL_POWER:
                        case ITEM_MOD_MANA_REGENERATION: case ITEM_MOD_SPELL_HEALING_DONE: case ITEM_MOD_SPELL_DAMAGE_DONE:
                            ++casterStats;
                            break;
                        default:
                            break;
                    }
                if (!CoaWorldGuildRules::RoleFits(caster, physical, casterStats))
                    continue;
                uint8 const slot = player->FindEquipSlot(proto, NULL_SLOT, true);
                if (slot == NULL_SLOT)
                    continue;
                Item* worn = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                float wornScore = worn ? calc.CalculateItem(worn->GetEntry(), worn->GetItemRandomPropertyId()) : 0.0f;
                // A two-hand weapon replaces both hands.
                if (proto->InventoryType == INVTYPE_2HWEAPON)
                    if (Item* off = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
                        wornScore += calc.CalculateItem(off->GetEntry(), off->GetItemRandomPropertyId());
                // Scored with the random suffix it will have.
                int32 const random = Item::GenerateItemRandomPropertyId(entry);
                float const score = calc.CalculateItem(entry, random);
                if (CoaWorldGuildRules::WorthGiving(score, wornScore) && score - wornScore > bestGain)
                {
                    bestGain = score - wornScore;
                    bestEntry = entry;
                    bestRandom = random;
                    bestReason = worn ? Acore::StringFormat("ilvl +{}", int32(proto->ItemLevel) - int32(worn->GetTemplate()->ItemLevel))
                                      : std::string("empty");
                }
            }
            if (!bestEntry)
                return;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(bestEntry);
            if (settings.gearAsk)
            {
                Offer(player, bot, proto, bestRandom, bestReason, today, now);
                return;
            }
            if (!MailNew(bot, player, bestEntry, 1, 0, 0,
                         Acore::StringFormat(Tr("Found this, it should suit you better. - {}", "Trouv\u00e9 \u00e7a, \u00e7a devrait mieux t'aller. - {}"), bot->GetName()), bestRandom))
                return;
            player->UpdatePlayerSetting(GEAR_SETTING, GEAR_SETTING_LAST_DAY, today);
            Answer(bot, "guild_gear_gift", { { "player", player->GetName() }, { "item", Link(proto, player->GetLevel()) } },
                   Acore::StringFormat(Tr("{}, sent you something you can use, check your mail", "{}, je t'ai envoy\u00e9 un truc qui devrait te servir, regarde ton courrier"), player->GetName()),
                   now + urand(20, 40));
            LogEvent("guild", bot->GetName(), Acore::StringFormat("gave {} to {}", proto->Name1, player->GetName()));
            LogRow(player, LOG_GIFT, bestEntry, 1, "gift", bot->GetName(), now, 0, "");
        }

        // Once a day at most, the leader of a bot guild writes a new message of the day.
        void Motds(time_t now)
        {
            static char const* const motds[] = {
                "Raid night soon, bring flasks and repair before you come.",
                "Welcome to all our new members! Ask in guild chat if you need help.",
                "Guild bank is open, put your spare cloth in tab 1 for the crafters.",
                "Dungeon runs every evening, whisper an officer to join.",
                "Be nice in trade chat, you represent the guild.",
                "Congrats to everyone who dinged this week!",
            };
            std::unordered_set<uint32> seen;
            for (auto const& [guid, bot] : RandomBots())
            {
                if (!bot || !bot->IsInWorld() || !bot->GetGuildId() || !seen.insert(bot->GetGuildId()).second)
                    continue;
                Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
                if (!guild || guild->GetLeaderGUID() != bot->GetGUID() || !IsRandomBot(bot))
                    continue;
                time_t& last = _lastMotd[guild->GetId()];
                if (last && now - last < DAY)
                    continue;
                last = now;
                std::string motd = CoaLive::SystemLine(bot, "guild_motd", {});
                guild->HandleSetMOTD(bot->GetSession(), motd.empty() ? std::string(Pick(motds)) : motd);
            }
        }

        // A member of a bot guild that is not full calls for members in General.
        void Recruit()
        {
            std::vector<Player*> callers;
            for (auto const& [guid, bot] : RandomBots())
            {
                if (!bot || !bot->IsInWorld() || !bot->GetGuildId() || bot->IsInCombat() || !IsRandomBot(bot))
                    continue;
                Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
                if (guild && guild->GetMemberSize() < sPlayerbotAIConfig.randomBotGuildSizeMax)
                    callers.push_back(bot);
            }
            if (callers.empty())
                return;

            Player* bot = callers[urand(0, callers.size() - 1)];
            Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
            static char const* const calls[] = {
                "<{}> is recruiting, all levels and classes welcome. /w me",
                "<{}> looking for more people to level and run dungeons with, whisper me",
                "friendly guild <{}> recruiting, we help with quests and crafting",
                "<{}> recruiting! raids later, dungeons now. /w for invite",
            };
            static char const* const callsFr[] = {
                "<{}> recrute, tous niveaux et toutes classes bienvenus, /w moi",
                "<{}> cherche du monde pour monter en niveau et faire des donjons, chuchotez-moi",
                "guilde sympa <{}> recrute, on aide pour les qu\u00eates et l'artisanat",
                "<{}> recrute ! raids plus tard, donjons maintenant. /w pour une invit",
            };
            if (BotLine(bot, "guild_recruit", { { "guild", guild->GetName() } }) || !CoaWorld::BotChatOn())
                return;
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                botAI->SayToChannel(Acore::StringFormat(BotsFrench() ? Pick(callsFr) : Pick(calls), guild->GetName()), GENERAL);
        }

        std::vector<Speech> _speeches;

        std::vector<std::pair<ObjectGuid, time_t>> _welcomes;  // real players logged in: (who, when to welcome)
        std::unordered_map<uint32, time_t> _lastMotd;
        std::unordered_map<uint32, time_t> _lastGift;
        std::unordered_map<uint32, std::pair<uint32, uint32>> _orders;  // player -> (day, orders that day)
        std::unordered_map<uint32, time_t> _greeted;  // real player -> last greeting by the guild
        uint32 _timer = 0;
        uint32 _slow = 0;
        uint32 _recruit = 0;
    };

    void LoadSettings()
    {
        auto key = [](char const* name) { return std::string(PREFIX) + name; };
        settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), true, false);
        settings.balance = sConfigMgr->GetOption<bool>(key("Balance"), true, false);
        settings.motd = sConfigMgr->GetOption<bool>(key("Motd"), true, false);
        settings.chat = sConfigMgr->GetOption<bool>(key("Chat"), true, false);
        settings.recruitMinutes = sConfigMgr->GetOption<uint32>(key("RecruitMinutes"), 45, false);
        settings.help = sConfigMgr->GetOption<bool>(key("Help"), true, false);
        settings.giftHours = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(key("GiftHours"), 24, false));
        settings.orders = sConfigMgr->GetOption<bool>(key("Orders"), true, false);
        settings.ordersPerDay = std::min<uint32>(50, sConfigMgr->GetOption<uint32>(key("OrdersPerDay"), 3, false));
        settings.orderMaxQuality = std::min<uint32>(ITEM_QUALITY_EPIC,
            sConfigMgr->GetOption<uint32>(key("OrderMaxQuality"), ITEM_QUALITY_RARE, false));
        settings.orderAuctionMaxGold = std::min<uint32>(10000, sConfigMgr->GetOption<uint32>(key("OrderAuctionMaxGold"), 50, false));
        settings.gear = sConfigMgr->GetOption<bool>(key("Gear"), true, false);
        settings.gearDays = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("GearDays"), 3, false), 1, 60);
        settings.gearQuality = std::clamp<uint32>(sConfigMgr->GetOption<uint32>(key("GearQuality"), ITEM_QUALITY_UNCOMMON, false),
            ITEM_QUALITY_UNCOMMON, ITEM_QUALITY_RARE);
        settings.coaLive = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Enable", true, false);
        settings.addon = sConfigMgr->GetOption<bool>(key("Addon"), true, false);
        settings.greetings = sConfigMgr->GetOption<bool>(key("Greetings"), true, false);
        settings.gearAsk = sConfigMgr->GetOption<bool>(key("GearAsk"), true, false);
        settings.loginGrace = std::min<uint32>(600, sConfigMgr->GetOption<uint32>(key("LoginGraceSeconds"), 40, false));
        settings.catalog = sConfigMgr->GetOption<bool>(key("Catalog"), true, false);
        if (settings.enabled)
            LogInit(time(nullptr));
    }
}

// --- The SquidBots Lite world protocol (CoaWorldAddon.cpp), world thread ---
bool CoaWorldGuildAddonOn() { return settings.enabled && settings.orders && settings.addon; }

std::vector<std::string> CoaWorldGuildAddonOrder(Player* player, std::vector<std::string> const& args)
{
    using namespace CoaWorldAddonRules;
    auto error = [](char const* reason) { return std::vector<std::string>{ std::string("ERR\tGORD\t") + reason }; };
    if (!IsHuman(player))
        return error("off");
    if (!player->GetGuildId())
        return error("noguild");
    uint32 const entry = args.size() > 1 ? Number(args[1]) : 0;
    ItemTemplate const* proto = entry ? sObjectMgr->GetItemTemplate(entry) : nullptr;
    if (!proto)
        return error("item");
    uint32 const qty = args.size() > 2 ? Quantity(args[2], proto->GetMaxStackSize()) : 0;
    if (!qty)
        return error("qty");
    OrderResult const result = GuildLife::Instance().AddonOrder(player, entry, qty);
    if (std::string(result.state) == "refused")
        return error(result.reason);
    return { "GOK\t" + std::to_string(result.id) };
}

std::vector<std::string> CoaWorldGuildAddonList(Player* player)
{
    using namespace CoaWorldAddonRules;
    std::vector<std::string> out;
    if (!g_logReady || !IsHuman(player))
        return out;
    time_t const now = time(nullptr);
    if (QueryResult result = PlayerbotsDatabase.Query(
            "SELECT `id`, `kind`, `item`, `count`, `state`, `bot`, `at`, `cod`, `reason` FROM `coa_world_guild_log` WHERE "
            "`guid` = {} AND ((`kind` = {} AND `at` >= {}) OR (`kind` = {} AND `at` >= {})) ORDER BY `at` DESC LIMIT 40",
            player->GetGUID().GetCounter(), uint32(LOG_ORDER), uint32(now - 7 * DAY), uint32(LOG_GIFT), uint32(now - 30 * DAY)))
    {
        do
        {
            Field* f = result->Fetch();
            std::string const at = std::to_string(f[6].Get<uint32>());
            if (f[1].Get<uint8>() == LOG_ORDER)
                out.push_back(Cut("GO\t" + std::to_string(f[0].Get<uint32>()) + "\t" + std::to_string(f[2].Get<uint32>()) +
                                  "\t" + std::to_string(f[3].Get<uint16>()) + "\t" + Text(f[4].Get<std::string>()) + "\t" +
                                  Text(f[5].Get<std::string>()) + "\t" + at + "\t" + std::to_string(f[7].Get<uint32>()) +
                                  "\t" + Text(f[8].Get<std::string>())));
            else
                out.push_back(Cut("GG\t" + std::to_string(f[2].Get<uint32>()) + "\t" + Text(f[5].Get<std::string>()) +
                                  "\t" + at));
        } while (result->NextRow());
    }
    return out;
}

bool CoaWorldJustLoggedIn(Player* player)
{
    return player && getMSTimeDiff(player->GetInGameTime(), GameTime::GetGameTimeMS().count()) <
                         settings.loginGrace * IN_MILLISECONDS;
}

bool CoaWorldGuildGiftsOn() { return settings.enabled && settings.gear && settings.gearAsk && settings.addon; }

std::vector<std::string> CoaWorldGuildGiftList(Player* player)
{
    std::vector<std::string> out;
    for (GiftOffer const& o : g_offers)
        if (o.player == player->GetGUID())
            out.push_back(GuildLife::OfferLine(o));
    return out;
}

std::vector<std::string> CoaWorldGuildGiftAnswer(Player* player, std::vector<std::string> const& args, bool accept)
{
    uint32 const id = args.size() > 1 ? CoaWorldAddonRules::Number(args[1]) : 0;
    if (!id || !GuildLife::Instance().AnswerOffer(player, id, accept))
        return { std::string("ERR\t") + (accept ? "GACC" : "GREF") + "\toffer" };
    return CoaWorldGuildGiftList(player);
}

// --- Guilds.Catalog: what the guild's online bots can send, for the addon's search (GCAT). ---
namespace
{
    struct CatalogEntry
    {
        uint32 item;
        bool craft;
        std::string bot;
        uint32 amount;  // stock: count; craft: skill rank
        uint64 price;   // craft: the materials' cost (cash on delivery); stock: 0
        uint32 skill;
        std::string lower;  // English name, lower case, for the search
    };
    struct CatalogCache
    {
        time_t at = 0;
        std::vector<CatalogEntry> entries;
    };
    std::unordered_map<uint32, CatalogCache> g_catalogs;
    constexpr time_t CATALOG_SECONDS = 5 * MINUTE;

    // What an order may ask for (Order's own checks).
    bool Orderable(ItemTemplate const* proto)
    {
        return proto && proto->Bonding != BIND_QUEST_ITEM && proto->Quality <= settings.orderMaxQuality;
    }

    std::unordered_map<uint32, uint32> const& SpellToItem()
    {
        static std::unordered_map<uint32, uint32> map;
        static bool built = false;
        if (built)
            return map;
        built = true;
        for (auto const& [item, spells] : Recipes())
            for (uint32 spell : spells)
                map.emplace(spell, item);
        return map;
    }

    uint64 MaterialsCost(SpellInfo const* info)
    {
        uint64 cost = 0;
        for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
            if (info->Reagent[r] > 0 && info->ReagentCount[r])
                if (ItemTemplate const* reagent = sObjectMgr->GetItemTemplate(info->Reagent[r]))
                    cost += CoaWorldGuildRules::ReagentPrice(reagent->BuyPrice, reagent->SellPrice) * info->ReagentCount[r];
        return cost;
    }

    std::vector<CatalogEntry> BuildCatalog(uint32 guildId)
    {
        std::unordered_map<uint64, CatalogEntry> best;  // (item, craft) -> the best offer
        auto keep = [&best](CatalogEntry entry)
        {
            uint64 const key = (uint64(entry.item) << 1) | (entry.craft ? 1 : 0);
            auto itr = best.find(key);
            bool const better = itr == best.end() || (entry.craft ? entry.price < itr->second.price : entry.amount > itr->second.amount);
            if (better)
                best[key] = std::move(entry);
        };
        auto lower = [](std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        };
        for (auto const& [guid, bot] : RandomBots())
        {
            if (!bot || !bot->IsInWorld() || bot->GetGuildId() != guildId)
                continue;
            // In stock: what the bot may mail (as the share of an order).
            auto stock = [&](Item* item)
            {
                if (!item || item->IsEquipped() || item->IsInTrade() || item->IsBag() || !item->CanBeTraded(true))
                    return;
                ItemTemplate const* proto = item->GetTemplate();
                if (Orderable(proto))
                    keep({ proto->ItemId, false, bot->GetName(), item->GetCount(), 0, 0, lower(proto->Name1) });
            };
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                stock(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
                if (Bag* bag = bot->GetBagByPos(bagSlot))
                    for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                        stock(bag->GetItemByPos(uint8(slot)));
            // Crafted: the recipes it knows (as the craft of an order: no soulbound result).
            for (auto const& [spellId, spell] : bot->GetSpellMap())
            {
                auto const itr = SpellToItem().find(spellId);
                if (itr == SpellToItem().end() || !spell || spell->State == PLAYERSPELL_REMOVED)
                    continue;
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itr->second);
                SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
                if (!Orderable(proto) || !info || proto->Bonding == BIND_WHEN_PICKED_UP)
                    continue;
                uint32 skill = 0;
                auto const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
                if (bounds.first != bounds.second)
                    skill = bounds.first->second->SkillLine;
                keep({ proto->ItemId, true, bot->GetName(), skill ? uint32(bot->GetSkillValue(skill)) : 0, MaterialsCost(info), skill,
                       lower(proto->Name1) });
            }
        }
        std::vector<CatalogEntry> out;
        out.reserve(best.size());
        for (auto& [key, entry] : best)
            out.push_back(std::move(entry));
        std::sort(out.begin(), out.end(), [](CatalogEntry const& a, CatalogEntry const& b)
                  { return a.lower != b.lower ? a.lower < b.lower : a.craft < b.craft; });
        return out;
    }
}

bool CoaWorldGuildCatalogOn() { return settings.enabled && settings.orders && settings.catalog && settings.addon; }

std::vector<std::string> CoaWorldGuildCatalog(Player* player, std::vector<std::string> const& args)
{
    using namespace CoaWorldAddonRules;
    if (!IsHuman(player) || !player->GetGuildId())
        return { "ERR\tGCAT\tnoguild" };
    std::string search = args.size() > 1 ? args[1] : std::string();
    std::transform(search.begin(), search.end(), search.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    uint32 const page = std::max<uint32>(1, args.size() > 2 ? Number(args[2]) : 1);

    time_t const now = time(nullptr);
    CatalogCache& cache = g_catalogs[player->GetGuildId()];
    if (!cache.at || now - cache.at >= CATALOG_SECONDS)
        cache = { now, BuildCatalog(player->GetGuildId()) };

    std::vector<CatalogEntry const*> found;
    for (CatalogEntry const& e : cache.entries)
        if (search.empty() || e.lower.find(search) != std::string::npos)
            found.push_back(&e);
    constexpr uint32 PER_PAGE = 30;
    std::vector<std::string> out;
    for (uint32 i = (page - 1) * PER_PAGE; i < found.size() && i < page * PER_PAGE; ++i)
    {
        CatalogEntry const& e = *found[i];
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(e.item);
        if (!proto)
            continue;
        out.push_back(Cut(Acore::StringFormat("GC\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", e.item,
                                              e.craft ? "craft" : "stock", Text(e.bot), e.amount, e.price, proto->Quality,
                                              proto->ItemLevel, proto->RequiredLevel, proto->InventoryType, e.skill,
                                              Text(proto->Name1))));
    }
    out.push_back(Acore::StringFormat("GCN\t{}\t{}", found.size(), page));
    return out;
}

bool CoaWorldGuildGiftWhisper(Player* player, Player* bot, std::string const& msg)
{
    if (!settings.enabled || !settings.gearAsk || !player || !bot || !IsHuman(player))
        return false;
    return GuildLife::Instance().Whispered(player, bot, msg);
}

bool CoaWorldGuildSideMayCreate(uint8 side, uint32 allianceGuilds, uint32 hordeGuilds)
{
    if (!settings.enabled || !settings.balance)
        return true;
    return side == TEAM_ALLIANCE ? allianceGuilds <= hordeGuilds : hordeGuilds <= allianceGuilds;
}

class CoaWorldGuildsPlayerScript : public PlayerScript
{
public:
    CoaWorldGuildsPlayerScript()
        : PlayerScript("CoaWorldGuildsPlayerScript", { PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_LOOT_ITEM, PLAYERHOOK_ON_LOGIN,
                                                        PLAYERHOOK_CAN_PLAYER_USE_GUILD_CHAT })
    {
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!settings.enabled || !player || !player->GetGuildId())
            return;
        // A real player levels up: the guild may have gear for them.
        if (IsHuman(player))
        {
            if (settings.gear)
                Push({ GuildEvent::HUMAN_LEVEL, player->GetGUID(), player->GetLevel() });
            return;
        }
        if (!settings.chat)
            return;
        uint8 level = player->GetLevel();
        if (level % 10 == 0 || level >= 55)
            Push({ GuildEvent::LEVEL, player->GetGUID(), level });
    }

    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/) override
    {
        if (!settings.enabled || !settings.chat || !player || !item || !player->GetGuildId() || !IsBot(player))
            return;
        if (item->GetTemplate()->Quality >= ITEM_QUALITY_RARE)
            Push({ GuildEvent::LOOT, player->GetGUID(), item->GetEntry() });
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!settings.enabled || !player || !player->GetGuildId() || !IsHuman(player))
            return;
        Push({ GuildEvent::LOGIN, player->GetGUID(), 0 });
    }

    // A real player asks the guild for an item: queued for the world thread (Order).
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/, std::string& msg, Guild* guild) override
    {
        if (!settings.enabled || !player || !guild || !IsHuman(player))
            return true;
        if (settings.greetings && CoaWorldGuildRules::IsGreeting(msg))
        {
            Push({ GuildEvent::GREET, player->GetGUID(), 0 });
            return true;
        }
        if (!settings.orders)
            return true;
        if (uint32 entry = CoaWorldGuildRules::OrderedItem(msg))
            Push({ GuildEvent::ORDER, player->GetGUID(), entry });
        return true;
    }
};

class CoaWorldGuildsWorldScript : public WorldScript
{
public:
    CoaWorldGuildsWorldScript()
        : WorldScript("CoaWorldGuildsWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
    {
    }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            LoadSettings();
    }

    void OnStartup() override { LoadSettings(); }

    void OnUpdate(uint32 diff) override
    {
        if (settings.enabled)
            GuildLife::Instance().Update(diff);
    }
};

void AddSC_coa_world_guilds()
{
    new CoaWorldGuildsPlayerScript();
    new CoaWorldGuildsWorldScript();
}
