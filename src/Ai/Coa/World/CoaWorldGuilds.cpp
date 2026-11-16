/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: guilds of bots that live.
 *
 *  - Balance: a new random-bot guild is only made for the side with fewer (idea of Coralis' mod-playerbots work,
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
#include "CoaWorldGuildAsk.h"
#include "CoaWorldGuildRules.h"
#include "CoaWorldAddonRules.h"
#include "CoaWorldState.h"

#include "CoaArtisans.h"
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
// CoA Economy's guild stock (3.1.4): the guildmates' banks join their bags in an order when it is on.
#if __has_include("CoaMaterials.h")
#include "CoaMaterials.h"
#endif
#include "StatsWeightCalculator.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <cstring>
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

    // What one reagent costs a guildmate who crafts for you: what it costs an artisan of CoA Economy (its market
    // price, or less when making it is cheaper) while the artisans are on, never more than the 3.1.3 price (its vendor
    // price, or four times what a vendor gives for it). No fee between guildmates.
    uint64 GuildReagentPrice(ItemTemplate const* reagent)
    {
        uint64 const cost = CoaEconomy::Artisans::IsEnabled() ? CoaEconomy::Artisans::ReagentCost(reagent->ItemId) : 0;
        return CoaWorldGuildRules::GuildCraftReagentPrice(cost, reagent->BuyPrice, reagent->SellPrice);
    }

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
        bool orderCombined = true;      // 3.1.4: guild bank, stock, craft, AH for what is missing, one answer
        bool orderGuildBank = true;     // 3.1.4: the guild bank first (a guildmate's rank rights)
        bool orderPersonalBank = true;  // 3.1.4: the guildmates' own banks with their bags
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
        std::vector<ObjectGuid> standIns = {};  // guildmates who say the line instead when the speaker's is refused
    };

    // From map threads: what happened, to be told by the world thread.
    struct GuildEvent
    {
        enum Kind : uint8 { LEVEL, LOOT, LOGIN, ORDER, HUMAN_LEVEL, GREET, BYE } kind;
        ObjectGuid guid;
        uint32 value;  // level, or item entry
        uint32 qty = 0;  // an order's count, 0 for any
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
    bool g_logAsked = false;  // the count asked (3.1.4: 2026_11_16_01), for the addon's GO "5 of 20"
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
        if (QueryResult asked = PlayerbotsDatabase.Query(
                "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = "
                "'coa_world_guild_log' AND column_name = 'asked'"))
            g_logAsked = asked->Fetch()[0].Get<uint64>() == 1;
        if (QueryResult result = PlayerbotsDatabase.Query("SELECT MAX(`id`) FROM `coa_world_guild_log`"))
            g_nextLogId = result->Fetch()[0].Get<uint32>() + 1;
        PlayerbotsDatabase.Execute("DELETE FROM `coa_world_guild_log` WHERE (`kind` = {} AND `at` < {}) OR (`kind` IN ({}, {}) AND `at` < {})",
                                   uint32(LOG_ORDER), uint32(now - 7 * DAY), uint32(LOG_GIFT), uint32(LOG_GIFT_REFUSED),
                                   uint32(now - 30 * DAY));
    }

    uint32 LogRow(Player* player, uint8 kind, uint32 item, uint32 count, char const* state, std::string bot, time_t at,
                  uint64 cod, char const* reason, uint32 asked = 0)
    {
        uint32 const id = g_nextLogId++;
        if (!g_logReady)
            return id;
        if (bot.size() > 32)  // the column (a longer name would lose the whole row)
            bot.resize(32);
        PlayerbotsDatabase.EscapeString(bot);
        // Written at once: the addon asks GLIST about a second after its GORD.
        if (g_logAsked)
        {
            PlayerbotsDatabase.DirectExecute("INSERT INTO `coa_world_guild_log` (`id`, `guid`, `kind`, `item`, `count`, `state`, `bot`, "
                                             "`at`, `cod`, `reason`, `asked`) VALUES ({}, {}, {}, {}, {}, '{}', '{}', {}, {}, '{}', {})",
                                             id, player->GetGUID().GetCounter(), uint32(kind), item, std::min<uint32>(count, 65535),
                                             state, bot, uint32(at), uint32(std::min<uint64>(cod, 0xFFFFFFFF)), reason,
                                             std::min<uint32>(asked, 65535));
            return id;
        }
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

    // A real player. At a bot's login its PlayerbotAI is not attached yet: the session says it is a bot (without it, a
    // test gave gear to bots, by mail to online bots).
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
                    case GuildEvent::BYE:
                        if (IsHuman(player))
                            Bye(player, now);
                        break;
                    case GuildEvent::ORDER:
                        if (IsHuman(player))
                            Order(player, e.value, e.qty, now);
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
                bool const present = speaker && speaker->IsInWorld() && speaker->GetGuildId() == s.guildId;
                CoaWorld::Slots const slots = { { "player", s.player } };
                if (present && !s.situation.empty() && BotLine(speaker, s.situation.c_str(), slots))
                    continue;
                // The bank may refuse a bot (its soul's sociability, a pace): a guildmate says it instead.
                bool said = false;
                if (!s.situation.empty())
                    for (ObjectGuid const& guid : s.standIns)
                    {
                        Player* mate = ObjectAccessor::FindPlayer(guid);
                        if (mate && mate->IsInWorld() && mate->GetGuildId() == s.guildId &&
                            BotLine(mate, s.situation.c_str(), slots))
                        {
                            said = true;
                            break;
                        }
                    }
                if (said || !present)
                    continue;
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(speaker); botAI && !s.text.empty() && CoaWorld::BotChatOn())
                    botAI->SayToGuild(s.text);
            }
            _speeches.swap(later);
        }

        // A real player of a guild with bots logs in: a guildmate says hello, and now and then sends a gift.
        // Guildmates answer a real player's hello in guild chat (2 or 3 of those online, 3 to 10 seconds later, at
        // least 2 seconds apart, with different lines; again a minute after), or greet it at login (1 or 2; 0 or 1 more
        // when CoaLive already has one say it). The login greeting has no hold on the answer to a hello: a new login
        // starts that clock over.
        void Greet(Player* player, time_t now, bool login)
        {
            uint32 const lowGuid = player->GetGUID().GetCounter();
            if (login)
                _greeted.erase(lowGuid);
            else
            {
                time_t& last = _greeted[lowGuid];
                if (!CoaWorldGuildRules::GreetReplyDue(last, now))
                    return;
                last = now;
            }
            std::vector<Player*> mates;
            for (auto const& [guid, bot] : RandomBots())
                if (bot && bot->IsInWorld() && bot->GetGuildId() == player->GetGuildId())
                    mates.push_back(bot);
            if (mates.empty())
                return;
            std::shuffle(mates.begin(), mates.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
            size_t const count = login ? (settings.coaLive ? urand(0, 1) : urand(1, 2))
                                       : CoaWorldGuildRules::AnswerCount(mates.size(), urand(0, 1));
            bool const fr = BotsFrench();
            static char const* const replyEn[] = { "hey {}", "hi {}!", "o/", "yo {}", "hey hey", "hello {}" };
            static char const* const replyFr[] = { "salut {}", "coucou {}", "yo", "slt", "bonjour {} !", "o/" };
            static char const* const welcomeEn[] = { "hey {}", "wb {}", "re", "o/ {}" };
            static char const* const welcomeFr[] = { "salut {}", "re", "wb {}", "coucou {} !" };
            std::vector<time_t> delays;
            for (size_t i = 0; i < count && i < mates.size(); ++i)
                delays.push_back(now + (login ? urand(2, 20) : urand(3, 10)));
            delays = CoaWorldGuildRules::AnswerTimes(delays);
            char const* const* pool = login ? (fr ? welcomeFr : welcomeEn) : (fr ? replyFr : replyEn);
            size_t const size = login ? 4 : 6;
            std::vector<size_t> lines(size);
            for (size_t i = 0; i < size; ++i)
                lines[i] = i;
            std::shuffle(lines.begin(), lines.end(), std::mt19937(urand(0, 0x7FFFFFFF)));  // two mates, two lines
            for (size_t i = 0; i < delays.size(); ++i)
            {
                std::string text = Acore::StringFormat(pool[lines[i % size]], player->GetName());
                Speech speech{ mates[i]->GetGUID(), mates[i]->GetGuildId(), text, delays[i] };
                speech.situation = login ? "guild_welcome" : "guild_greet_reply";
                speech.player = player->GetName();
                if (!login)
                    for (size_t m : CoaWorldGuildRules::StandIns(i, delays.size(), mates.size()))
                        speech.standIns.push_back(mates[m]->GetGUID());
                _speeches.push_back(std::move(speech));
            }
        }

        // A real player says goodbye in guild chat ("gn", "bye", "cya", "bonne nuit"): 2 or 3 guildmates online answer
        // 3 to 10 seconds later, at least 2 seconds apart, again a minute after. Lines from IA MIND's bank only
        // (guild_bye_reply): without one, nothing is said.
        void Bye(Player* player, time_t now)
        {
            time_t& last = _byed[player->GetGUID().GetCounter()];
            if (!CoaWorldGuildRules::GreetReplyDue(last, now))
                return;
            last = now;
            std::vector<Player*> mates;
            for (auto const& [guid, bot] : RandomBots())
                if (bot && bot->IsInWorld() && bot->GetGuildId() == player->GetGuildId())
                    mates.push_back(bot);
            std::shuffle(mates.begin(), mates.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
            std::vector<time_t> delays;
            for (size_t i = 0, count = CoaWorldGuildRules::AnswerCount(mates.size(), urand(0, 1)); i < count; ++i)
                delays.push_back(now + urand(3, 10));
            delays = CoaWorldGuildRules::AnswerTimes(delays);
            for (size_t i = 0; i < delays.size(); ++i)
            {
                Speech speech{ mates[i]->GetGUID(), mates[i]->GetGuildId(), std::string(), delays[i] };
                speech.situation = "guild_bye_reply";
                speech.player = player->GetName();
                for (size_t m : CoaWorldGuildRules::StandIns(i, delays.size(), mates.size()))
                    speech.standIns.push_back(mates[m]->GetGUID());
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

        OrderResult Logged(Player* player, uint32 entry, uint32 count, char const* state, char const* reason,
                           std::string const& bot, uint64 cod, time_t now)
        {
            OrderResult result;
            result.id = LogRow(player, LOG_ORDER, entry, count, state, bot, now, cod, reason, _askedQty);
            LOG_INFO("playerbots.coa", "CoA World: guild order by {} <{}>: item {} asked {} ({}), {} {}{}{}, sent {}, COD {}c",
                     player->GetName(), GuildName(player), entry, _askedQty, _askedQty ? "count" : "any", state,
                     *reason ? "reason " : "", reason, bot.empty() ? std::string() : " by " + bot, count, cod);
            result.state = state;
            result.reason = reason;
            CoaWorldState::Order(GuildName(player), entry, count, state, bot);
            return result;
        }

        // One path of a combined order (3.1.4): who sent how many, and at what price (cash on delivery, 0 free).
        struct OrderPart
        {
            char const* state;  // "bank", "shared", "crafted", "bought"
            Player* bot;
            uint32 count;
            uint64 cod;
        };

        // The guildmates' banks: Guilds.OrderPersonalBank and CoA Economy's guild stock (which keeps the materials
        // there); without it, the bags only, as in 3.1.3.
        static bool PersonalBankOn()
        {
#ifdef COA_ECONOMY_GUILD_STOCK
            return settings.orderPersonalBank && CoaEconomy::Materials::IsGuildStockEnabled();
#else
            return false;
#endif
        }

        // The items of the bot's bags (and of its bank with Guilds.OrderPersonalBank) it may mail for an order, as
        // MailHandler: never equipped, traded, a bag, or soulbound.
        static std::vector<Item*> StockItems(Player* bot, uint32 entry, bool bank)
        {
            std::vector<Item*> items;
            auto add = [&](Item* item)
            {
                if (item && item->GetEntry() == entry && !item->IsEquipped() && !item->IsInTrade() && !item->IsBag() &&
                    item->CanBeTraded(true))
                    items.push_back(item);
            };
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                add(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
                if (Bag* bag = bot->GetBagByPos(bagSlot))
                    for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                        add(bag->GetItemByPos(uint8(slot)));
            if (bank)
            {
                for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; ++slot)
                    add(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
                for (uint8 bagSlot = BANK_SLOT_BAG_START; bagSlot < BANK_SLOT_BAG_END; ++bagSlot)
                    if (Bag* bag = bot->GetBagByPos(bagSlot))
                        for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                            add(bag->GetItemByPos(uint8(slot)));
            }
            return items;
        }

        // Stacks of the bot (bags or bank) to the player in one mail, free or cash on delivery: a whole stack moves
        // (its own item, suffix and all), a stack taken in part splits (a new item for the part, the rest stays).
        static uint32 MailStacks(Player* bot, Player* to, std::vector<Item*> const& items,
                                 std::vector<CoaWorldGuildRules::StackTake> const& takes, uint64 cod, std::string const& body)
        {
            if (takes.empty())
                return 0;
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            MailDraft draft(items[takes.front().index]->GetTemplate()->Name1, body);
            uint32 sent = 0;
            for (CoaWorldGuildRules::StackTake const& take : takes)
            {
                Item* item = items[take.index];
                if (take.count < item->GetCount())
                {
                    Item* part = Item::CreateItem(item->GetEntry(), take.count, to);
                    if (!part)
                        continue;
                    item->SetCount(item->GetCount() - take.count);
                    item->SetState(ITEM_CHANGED, bot);
                    part->SaveToDB(trans);
                    draft.AddItem(part);
                }
                else
                {
                    bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
                    item->DeleteFromInventoryDB(trans);
                    item->SetOwnerGUID(to->GetGUID());
                    item->SetState(ITEM_CHANGED);  // as MailHandler: saved standalone, in no player's update queue
                    item->SaveToDB(trans);
                    draft.AddItem(item);
                }
                sent += take.count;
            }
            if (sent)
            {
                if (cod)
                    draft.AddCOD(uint32(cod));
                draft.SendMailTo(trans, MailReceiver(to), MailSender(bot), MAIL_CHECK_MASK_COPIED, 0);
            }
            bot->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);
            return sent;
        }

        // A free slot of the bot's backpack or ordinary bags (not a special bag: herbs, soul shards...).
        static bool FreeBagSlot(Player* bot, uint8& bag, uint8& slot)
        {
            for (uint8 s = INVENTORY_SLOT_ITEM_START; s < INVENTORY_SLOT_ITEM_END; ++s)
                if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, s))
                {
                    bag = INVENTORY_SLOT_BAG_0;
                    slot = s;
                    return true;
                }
            for (uint8 b = INVENTORY_SLOT_BAG_START; b < INVENTORY_SLOT_BAG_END; ++b)
                if (Bag* container = bot->GetBagByPos(b))
                    if (!container->GetTemplate()->BagFamily)
                        for (uint32 s = 0; s < container->GetBagSize(); ++s)
                            if (!container->GetItemByPos(uint8(s)))
                            {
                                bag = b;
                                slot = uint8(s);
                                return true;
                            }
            return false;
        }

        // 0. The guild bank (Guilds.OrderGuildBank): a guildmate takes the items out as a member would, with its
        // rank's rights and daily withdrawals (the core checks them and writes the bank's log), then mails them free.
        // From afar: the order is answered at once. The tabs are read from the database (the core keeps them private).
        bool FromGuildBank(Player* player, std::vector<Player*> const& mates, ItemTemplate const* proto, uint32 need,
                           OrderPart& part)
        {
            Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId());
            if (!guild)
                return false;
            QueryResult result = CharacterDatabase.Query(
                "SELECT gbi.TabId, gbi.SlotId, ii.count FROM guild_bank_item gbi JOIN item_instance ii ON ii.guid = "
                "gbi.item_guid WHERE gbi.guildid = {} AND ii.itemEntry = {} ORDER BY gbi.TabId, gbi.SlotId",
                guild->GetId(), proto->ItemId);
            if (!result)
                return false;
            struct BankStack
            {
                uint8 tab, slot;
                uint32 count;
            };
            // The player's own rights: never more out of a tab than the player's rank could withdraw there today.
            Guild::Member const* member = guild->GetMember(player->GetGUID());
            if (!member)
                return false;
            bool const leader = member->IsRank(GR_GUILDMASTER) || guild->GetLeaderGUID() == player->GetGUID();
            std::array<int64, GUILD_BANK_MAX_TABS> slotsPerDay{};
            if (!leader)
                if (QueryResult rights = CharacterDatabase.Query(
                        "SELECT `TabId`, `SlotPerDay` FROM `guild_bank_right` WHERE `guildid` = {} AND `rid` = {}", guild->GetId(),
                        uint32(member->GetRankId())))
                    do
                    {
                        Field* f = rights->Fetch();
                        if (uint8 const tab = f[0].Get<uint8>(); tab < GUILD_BANK_MAX_TABS)
                            slotsPerDay[tab] = int64(f[1].Get<uint32>());
                    } while (rights->NextRow());
            std::array<uint32, GUILD_BANK_MAX_TABS> allowed{};
            for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
                allowed[tab] = CoaWorldGuildRules::BankStacksAllowed(
                    leader, guild->MemberHasTabRights(player->GetGUID(), tab, GUILD_BANK_RIGHT_VIEW_TAB), slotsPerDay[tab],
                    member->GetBankWithdrawValue(tab));

            std::vector<BankStack> stacks;
            std::vector<uint32> counts;
            do
            {
                Field* f = result->Fetch();
                uint8 const tab = f[0].Get<uint8>();
                if (tab >= GUILD_BANK_MAX_TABS || !allowed[tab])
                    continue;
                stacks.push_back({ tab, f[1].Get<uint8>(), f[2].Get<uint32>() });
                counts.push_back(stacks.back().count);
            } while (result->NextRow());
            std::vector<CoaWorldGuildRules::StackTake> takes = CoaWorldGuildRules::TakeStacks(counts, need, MAX_MAIL_ITEMS);
            // At most the player's allowance of stacks a tab (the biggest ones were picked first).
            std::array<uint32, GUILD_BANK_MAX_TABS> taken{};
            takes.erase(std::remove_if(takes.begin(), takes.end(), [&](CoaWorldGuildRules::StackTake const& take)
                                       { return ++taken[stacks[take.index].tab] > allowed[stacks[take.index].tab]; }),
                        takes.end());
            if (takes.empty())
                return false;

            for (Player* bot : mates)
            {
                std::vector<Item*> got;
                std::vector<uint8> gotTabs;
                std::vector<CoaWorldGuildRules::StackTake> whole;
                for (CoaWorldGuildRules::StackTake const& take : takes)
                {
                    BankStack const& stack = stacks[take.index];
                    if (!guild->MemberHasTabRights(bot->GetGUID(), stack.tab, GUILD_BANK_RIGHT_VIEW_TAB))
                        continue;
                    uint8 bag = 0, slot = 0;
                    if (!FreeBagSlot(bot, bag, slot))
                        break;
                    guild->SwapItemsWithInventory(bot, true, stack.tab, stack.slot, bag, slot,
                                                  take.count < stack.count ? take.count : 0);
                    Item* item = bot->GetItemByPos(bag, slot);
                    if (!item)
                        break;  // no withdraw right on the tab, or the rank's withdrawals of the day are spent
                    if (item->GetEntry() != proto->ItemId)
                    {
                        guild->SwapItemsWithInventory(bot, false, stack.tab, stack.slot, bag, slot, 0);
                        continue;
                    }
                    whole.push_back({ got.size(), item->GetCount() });
                    got.push_back(item);
                    gotTabs.push_back(stack.tab);
                }
                if (got.empty())
                    continue;
                uint32 const sent = MailStacks(bot, player, got, whole, 0,
                                               Tr("From the guild bank.", "De la banque de guilde."));
                if (!sent)
                    continue;
                // The stacks count as the player's own withdrawals of the day (the bot's were spent by the core too).
                if (!leader)
                    if (Guild::Member* own = guild->GetMember(player->GetGUID()))
                    {
                        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                        for (uint8 tab : gotTabs)
                            own->UpdateBankWithdrawValue(trans, tab, 1);
                        CharacterDatabase.CommitTransaction(trans);
                    }
                LogEvent("guild", bot->GetName(), Acore::StringFormat("took {} {} out of the guild bank for {}", sent,
                                                                       proto->Name1, player->GetName()));
                part = { "bank", bot, sent, 0 };
                return true;
            }
            return false;
        }

        // 1. The guildmates' stock: bags (and banks), the best stocked first, free, one mail each.
        void FromStock(Player* player, std::vector<Player*> const& mates, ItemTemplate const* proto, uint32 need,
                       bool combined, std::vector<OrderPart>& parts)
        {
            std::vector<std::vector<Item*>> held(mates.size());
            std::vector<uint32> counts(mates.size(), 0);
            for (std::size_t i = 0; i < mates.size(); ++i)
            {
                held[i] = StockItems(mates[i], proto->ItemId, PersonalBankOn());
                for (Item* item : held[i])
                    counts[i] += item->GetCount();
            }
            std::vector<CoaWorldGuildRules::StackTake> holders = CoaWorldGuildRules::SplitStock(counts, need);
            if (!combined && holders.size() > 1)
                holders.resize(1);
            for (CoaWorldGuildRules::StackTake const& holder : holders)
            {
                Player* bot = mates[holder.index];
                std::vector<uint32> stacks;
                for (Item* item : held[holder.index])
                    stacks.push_back(item->GetCount());
                uint32 const sent = MailStacks(bot, player, held[holder.index],
                                               CoaWorldGuildRules::TakeStacks(stacks, need ? holder.count : 0, MAX_MAIL_ITEMS),
                                               0, Tr("From the guild.", "De la part de la guilde."));
                if (!sent)
                    continue;
                LogEvent("guild", bot->GetName(), Acore::StringFormat("sent {} {} to {}", sent, proto->Name1, player->GetName()));
                parts.push_back({ "shared", bot, sent, 0 });
            }
        }

        // 2. Crafted by a guildmate who knows the recipe: cash on delivery at the materials' price.
        bool FromCraft(Player* player, std::vector<Player*> const& mates, ItemTemplate const* proto, uint32 need, OrderPart& part)
        {
            uint32 const entry = proto->ItemId;
            auto const recipes = Recipes().find(entry);
            if (recipes == Recipes().end() || proto->Bonding == BIND_WHEN_PICKED_UP)
                return false;
            for (Player* bot : mates)
                for (uint32 spellId : recipes->second)
                {
                    if (!bot->HasSpell(spellId))
                        continue;
                    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
                    uint64 cost = 0;  // one cast
                    for (uint8 r = 0; r < MAX_SPELL_REAGENTS; ++r)
                        if (info->Reagent[r] > 0 && info->ReagentCount[r])
                            if (ItemTemplate const* reagent = sObjectMgr->GetItemTemplate(info->Reagent[r]))
                                cost += GuildReagentPrice(reagent) * info->ReagentCount[r];
                    uint32 made = 1;
                    for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                        if (info->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM && info->Effects[e].ItemType == entry)
                            made = uint32(std::max<int32>(1, info->Effects[e].CalcValue(bot)));
                    // As many casts as the count needs (one Silver Bar a cast: twenty casts for twenty bars).
                    CoaWorldGuildRules::GuildCraft const plan =
                        CoaWorldGuildRules::PlanGuildCraft(need, made, proto->GetMaxStackSize(), cost);
                    cost *= plan.casts;
                    if (!MailNew(bot, player, entry, plan.sent, cost, 0,
                                 Tr("Made it for you, you only pay the materials.",
                                    "Fait pour toi, tu ne paies que les composants.")))
                        continue;
                    LogEvent("guild", bot->GetName(), Acore::StringFormat("crafts {} {} for {}", plan.sent, proto->Name1,
                                                                           player->GetName()));
                    part = { "crafted", bot, plan.sent, cost };
                    return true;
                }
            return false;
        }

        // The rows of an order (one a path) and its one log line.
        OrderResult LoggedParts(Player* player, uint32 entry, std::vector<OrderPart> const& parts, time_t now)
        {
            OrderResult result;
            uint32 got = 0;
            uint64 cod = 0;
            std::string paths;
            for (OrderPart const& part : parts)
            {
                uint32 const id = LogRow(player, LOG_ORDER, entry, part.count, part.state, part.bot->GetName(), now,
                                         part.cod, "", _askedQty);
                if (!result.id)
                {
                    result.id = id;
                    result.state = part.state;
                }
                got += part.count;
                cod += part.cod;
                paths += Acore::StringFormat("{}{} {} by {}", paths.empty() ? "" : ", ", part.count, part.state,
                                             part.bot->GetName());
                CoaWorldState::Order(GuildName(player), entry, part.count, part.state, part.bot->GetName());
            }
            LOG_INFO("playerbots.coa", "CoA World: guild order by {} <{}>: item {} asked {} ({}), sent {} ({}), COD {}c",
                     player->GetName(), GuildName(player), entry, _askedQty, _askedQty ? "count" : "any", got, paths, cod);
            return result;
        }

        // The one answer in /g to an order served: the old lines for one path, else what came from where.
        void AnswerParts(Player* player, ItemTemplate const* proto, uint32 qty, std::vector<OrderPart> const& parts,
                         CoaWorld::Slots& slots, time_t now)
        {
            uint32 got = 0;
            for (OrderPart const& part : parts)
                got += part.count;
            bool const fewer = qty && got < qty;
            OrderPart const& first = parts.front();
            bool const over = qty && got > qty;  // a recipe that makes several, an auction stack bigger than the rest
            if (parts.size() == 1)
            {
                // The count said is the count sent: "it" for one, "them" for more, "N for the M you asked" over.
                Player* bot = first.bot;
                std::string const name = player->GetName();
                std::string const price = Money(first.cod);
                slots["price"] = price;
                time_t const at = now + urand(5, 15);
                if (!std::strcmp(first.state, "bank"))
                {
                    if (fewer)
                        Say(bot, Acore::StringFormat(Tr("{} only {} of the {} in the guild bank, sending them",
                                                        "{} seulement {} sur les {} à la banque de guilde, je te les envoie"),
                                                     name, got, qty), at);
                    else if (got == 1)
                        Say(bot, Acore::StringFormat(Tr("{} took it out of the guild bank for you, check your mail",
                                                        "{} je te l'ai sorti de la banque de guilde, regarde ta boîte"), name), at);
                    else
                        Say(bot, Acore::StringFormat(Tr("{} took {} out of the guild bank for you, check your mail",
                                                        "{} je t'en ai sorti {} de la banque de guilde, regarde ta boîte"), name, got), at);
                }
                else if (!std::strcmp(first.state, "shared"))
                {
                    if (fewer)
                        Say(bot, Acore::StringFormat(Tr("{} I only have {} of the {}, sending them", "{} j'en ai seulement {} sur les {}, je te les envoie"),
                                                     name, got, qty), at);
                    else if (got == 1)
                        Answer(bot, "guild_order_share", slots, Acore::StringFormat(Tr("{} I have one, sending it", "{} j'en ai un, je te l'envoie"), name), at);
                    else
                        Say(bot, Acore::StringFormat(Tr("{} I have {}, sending them", "{} j'en ai {}, je te les envoie"), name, got), at);
                }
                else if (!std::strcmp(first.state, "crafted"))
                {
                    if (fewer)
                        Say(bot, Acore::StringFormat(Tr("{} I can make {} of the {}, mats only ({}), will mail them",
                                                        "{} je peux t'en faire {} sur les {}, juste les compos ({}), je te les envoie"),
                                                     name, got, qty, price), at);
                    else if (over)
                        Say(bot, Acore::StringFormat(Tr("{} the recipe makes {} for the {} you asked, mats only ({}), will mail them",
                                                        "{} la recette en fait {} pour les {} demandés, juste les compos ({}), je te les envoie"),
                                                     name, got, qty, price), at);
                    else if (got == 1)
                        Answer(bot, "guild_order_craft", slots,
                               Acore::StringFormat(Tr("{} I can make that, mats only ({}), will mail it", "{} je peux te le faire, juste les compos ({}), je te l'envoie"),
                                                   name, price), at);
                    else
                        Say(bot, Acore::StringFormat(Tr("{} I can make {}, mats only ({}), will mail them",
                                                        "{} je peux t'en faire {}, juste les compos ({}), je te les envoie"), name, got, price), at);
                }
                else if (fewer)
                    Say(bot, Acore::StringFormat(Tr("{} only found {} of the {} on the AH, {} at cost, check your mail",
                                                    "{} j'en ai trouvé seulement {} sur les {} à l'HV, {} au prix coûtant, regarde ta boîte"),
                                                 name, got, qty, price), at);
                else if (over)
                    Say(bot, Acore::StringFormat(Tr("{} got {} on the AH for the {} you asked (sold in stacks), {} at cost, check your mail",
                                                    "{} j'en ai pris {} à l'HV pour les {} demandés (vendus par piles), {} au prix coûtant, regarde ta boîte"),
                                                 name, got, qty, price), at);
                else if (got == 1)
                    Answer(bot, "guild_order_ah", slots,
                           Acore::StringFormat(Tr("{} saw one on the AH for {}, I'll grab it for you", "{} j'en ai vu un à l'HV pour {}, je te le prends"), name, price), at);
                else
                    Say(bot, Acore::StringFormat(Tr("{} found {} on the AH for {}, I'll grab them for you", "{} j'en ai trouvé {} à l'HV pour {}, je te les prends"),
                                                 name, got, price), at);
                return;
            }

            // "5 from our bags, 15 bought (4g 60s COD)": each path once, in the order served.
            std::vector<std::pair<char const*, std::pair<uint32, uint64>>> sums;
            for (OrderPart const& part : parts)
            {
                auto it = std::find_if(sums.begin(), sums.end(), [&](auto const& s) { return !std::strcmp(s.first, part.state); });
                if (it == sums.end())
                    sums.push_back({ part.state, { part.count, part.cod } });
                else
                {
                    it->second.first += part.count;
                    it->second.second += part.cod;
                }
            }
            std::string text;
            for (auto const& [state, sum] : sums)
            {
                std::string piece;
                if (!std::strcmp(state, "bank"))
                    piece = Acore::StringFormat(Tr("{} from the guild bank", "{} de la banque de guilde"), sum.first);
                else if (!std::strcmp(state, "shared"))
                    piece = Acore::StringFormat(Tr("{} from our bags", "{} de nos sacs"), sum.first);
                else if (!std::strcmp(state, "crafted"))
                    piece = Acore::StringFormat(Tr("{} made ({} COD)", "{} fabriqués ({} contre remboursement)"), sum.first,
                                                Money(sum.second));
                else
                    piece = Acore::StringFormat(Tr("{} bought at the AH ({} COD)", "{} achetés à l'HV ({} contre remboursement)"),
                                                sum.first, Money(sum.second));
                text += (text.empty() ? "" : ", ") + piece;
            }
            std::string line = Acore::StringFormat(Tr("{} {}, check your mail", "{} {}, regarde ta boîte"), player->GetName(), text);
            if (fewer)
                line += Acore::StringFormat(Tr(" ({} of the {})", " ({} sur les {})"), got, qty);
            else if (over)
                line += Acore::StringFormat(Tr(" ({} for the {} asked)", " ({} pour les {} demandés)"), got, qty);
            Say(parts.back().bot, line, now + urand(5, 15));
        }

        // qty: at most this many (the addon's GORD), 0 for any (guild chat). The combined order (3.1.4,
        // Guilds.OrderCombined): the guild bank, the guildmates' stock, a crafter, then the auction house, each for what
        // is still missing; one answer, one order of the day. Off: the first path that sends anything, as in 3.1.3.
        OrderResult Order(Player* player, uint32 entry, uint32 qty, time_t now)
        {
            _askedQty = qty;
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

            if (qty)
                qty = std::min<uint32>(qty, std::max<uint32>(1, proto->GetMaxStackSize()));

            bool const combined = settings.orderCombined;
            std::vector<OrderPart> parts;
            auto got = [&]
            {
                uint32 n = 0;
                for (OrderPart const& part : parts)
                    n += part.count;
                return n;
            };
            auto done = [&] { return !CoaWorldGuildRules::OrderMissing(qty, got()) || (!combined && !parts.empty()); };
            OrderPart part{};

            if (settings.orderGuildBank && FromGuildBank(player, mates, proto, CoaWorldGuildRules::OrderAsk(qty, got()), part))
                parts.push_back(part);
            if (!done())
                FromStock(player, mates, proto, CoaWorldGuildRules::OrderAsk(qty, got()), combined, parts);
            if (!done() && FromCraft(player, mates, proto, CoaWorldGuildRules::OrderAsk(qty, got()), part))
                parts.push_back(part);
            char const* why = "noah";  // why none came: no stock, no crafter online, and at the auction house...
            if (!done() && Auction(player, mates, proto, CoaWorldGuildRules::OrderAsk(qty, got()), part, why))
                parts.push_back(part);

            if (!parts.empty())
            {
                ++count.second;  // one order of the day, whatever the paths
                AnswerParts(player, proto, qty, parts, slots, now);
                return LoggedParts(player, entry, parts, now);
            }

            // The reason goes to the addon (GO / GLIST) and to the guild line when it is said.
            if (roll_chance_i(50))
            {
                std::string const ah = !std::strcmp(why, "ahprice") ? Tr("too dear at the AH", "trop cher à l'HV")
                                       : !std::strcmp(why, "ahgold") ? Tr("nobody has the gold for the AH", "personne n'a l'or pour l'HV")
                                                                     : Tr("none at the AH", "rien à l'HV");
                Say(mates.front(),
                    Acore::StringFormat(Tr("sorry {}, no stock, no crafter online, {}", "désolé {}, pas de stock, pas d'artisan en ligne, {}"),
                                        player->GetName(), ah),
                    now + urand(8, 20));
            }
            return Logged(player, entry, 0, "none", why, "", 0, now);
        }

        // 3. Bought out at the auction house of the player's side, up to the count asked (OrderAuctionMaxGold for
        // them all), in one mail, cash on delivery at what the buyer paid.
        bool Auction(Player* player, std::vector<Player*> const& mates, ItemTemplate const* proto, uint32 qty,
                     OrderPart& part, char const*& why)
        {
            AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(player->GetFaction());
            if (!house)
                return false;
            std::vector<AuctionEntry*> entries;
            std::vector<CoaWorldGuildRules::AuctionOffer> offers;
            for (auto const& [id, auction] : house->GetAuctions())
            {
                if (!auction || auction->item_template != proto->ItemId || !auction->buyout || auction->bidder ||
                    auction->owner == player->GetGUID())
                    continue;
                entries.push_back(auction);
                offers.push_back({ auction->itemCount, auction->buyout });
            }
            std::vector<std::size_t> const picks = CoaWorldGuildRules::PickAuctions(
                offers, qty, uint64(settings.orderAuctionMaxGold) * GOLD, MAX_MAIL_ITEMS);
            if (picks.empty())
            {
                why = offers.empty() ? "noah" : "ahprice";  // none listed, or over OrderAuctionMaxGold
                return false;
            }
            uint64 total = 0;
            for (std::size_t i : picks)
                total += entries[i]->buyout;
            Player* buyer = nullptr;
            for (Player* bot : mates)
                if (bot->GetMoney() >= total &&
                    std::none_of(picks.begin(), picks.end(), [&](std::size_t i) { return entries[i]->owner == bot->GetGUID(); }))
                {
                    buyer = bot;
                    break;
                }
            if (!buyer)
            {
                why = "ahgold";  // no guildmate online has the gold
                return false;
            }
            std::vector<Item*> items;
            uint32 sent = 0;
            for (std::size_t i : picks)
            {
                Item* item = sAuctionMgr->GetAItem(entries[i]->item_guid);
                if (!item)
                    return false;
                items.push_back(item);
                sent += item->GetCount();
            }

            // As buyouts by the bot (AuctionHouseHandler), but the items go on to the player instead of the won mails.
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            buyer->ModifyMoney(-int32(total));
            MailDraft draft(proto->Name1, Tr("Got it at the auction house, you pay what I paid.",
                                             "Trouvé à l'hôtel des ventes, tu paies ce que j'ai payé."));
            for (std::size_t n = 0; n < picks.size(); ++n)
            {
                AuctionEntry* auction = entries[picks[n]];
                Item* item = items[n];
                auction->bidder = buyer->GetGUID();
                auction->bid = auction->buyout;
                sAuctionMgr->SendAuctionSalePendingMail(auction, trans);
                sAuctionMgr->SendAuctionSuccessfulMail(auction, trans);
                sScriptMgr->OnAuctionSuccessful(house, auction);
                auction->DeleteFromDB(trans);
                sAuctionMgr->RemoveAItem(auction->item_guid);
                house->RemoveAuction(auction);
                item->SetOwnerGUID(player->GetGUID());
                item->SetState(ITEM_CHANGED);  // the new owner reaches item_instance (an auction item is saved as unchanged)
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
            draft.AddCOD(uint32(total)).SendMailTo(trans, MailReceiver(player), MailSender(buyer), MAIL_CHECK_MASK_COPIED, 0);
            buyer->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);

            LogEvent("guild", buyer->GetName(), Acore::StringFormat("bought {} {} at the auction house for {}", sent, proto->Name1,
                                                                     player->GetName()));
            part = { "bought", buyer, sent, total };
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
            // only) went to a physical spec for its weapon damage.
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

        // An officer of a bot guild that may invite calls for members in General ("/w me": the whisper is heard by
        // CoaWorldGuildAsk). AiPlayerbot.RandomBotGuildSizeMax is only the bots' own fill: a guild full of bots still
        // takes real players, up to the game's limit.
        void Recruit()
        {
            std::vector<Player*> callers;
            for (auto const& [guid, bot] : RandomBots())
                if (bot && IsRandomBot(bot) && CoaWorldGuildAsk::InvitingGuild(bot))
                    callers.push_back(bot);
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
            if (BotLine(bot, "guild_recruit", { { "guild", guild->GetName() } }))
            {
                CoaWorldGuildAsk::RecruitCall(bot);
                return;
            }
            if (!CoaWorld::BotChatOn())
                return;
            CoaWorldGuildAsk::RecruitCall(bot);
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                botAI->SayToChannel(Acore::StringFormat(BotsFrench() ? Pick(callsFr) : Pick(calls), guild->GetName()), GENERAL);
        }

        std::vector<Speech> _speeches;

        std::vector<std::pair<ObjectGuid, time_t>> _welcomes;  // real players logged in: (who, when to welcome)
        std::unordered_map<uint32, time_t> _lastMotd;
        std::unordered_map<uint32, time_t> _lastGift;
        std::unordered_map<uint32, std::pair<uint32, uint32>> _orders;  // player -> (day, orders that day)
        uint32 _askedQty = 0;  // the order being served (its log line)
        std::unordered_map<uint32, time_t> _greeted;  // real player -> last answer to its hello
        std::unordered_map<uint32, time_t> _byed;     // real player -> last answer to its goodbye
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
        settings.orderCombined = sConfigMgr->GetOption<bool>(key("OrderCombined"), true, false);
        settings.orderGuildBank = sConfigMgr->GetOption<bool>(key("OrderGuildBank"), true, false);
        settings.orderPersonalBank = sConfigMgr->GetOption<bool>(key("OrderPersonalBank"), true, false);
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
            "SELECT `id`, `kind`, `item`, `count`, `state`, `bot`, `at`, `cod`, `reason`, {} FROM `coa_world_guild_log` WHERE "
            "`guid` = {} AND ((`kind` = {} AND `at` >= {}) OR (`kind` = {} AND `at` >= {})) ORDER BY `at` DESC LIMIT 40",
            g_logAsked ? "`asked`" : "0", player->GetGUID().GetCounter(), uint32(LOG_ORDER), uint32(now - 7 * DAY), uint32(LOG_GIFT), uint32(now - 30 * DAY)))
    {
        do
        {
            Field* f = result->Fetch();
            std::string const at = std::to_string(f[6].Get<uint32>());
            if (f[1].Get<uint8>() == LOG_ORDER)
                out.push_back(Cut("GO\t" + std::to_string(f[0].Get<uint32>()) + "\t" + std::to_string(f[2].Get<uint32>()) +
                                  "\t" + std::to_string(f[3].Get<uint16>()) + "\t" + Text(f[4].Get<std::string>()) + "\t" +
                                  Text(f[5].Get<std::string>()) + "\t" + at + "\t" + std::to_string(f[7].Get<uint32>()) +
                                  "\t" + Text(f[8].Get<std::string>()) + "\t" + std::to_string(f[9].Get<uint32>())));
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
                    cost += GuildReagentPrice(reagent) * info->ReagentCount[r];
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

bool CoaWorldGuildOrderHeard(std::string const& msg)
{
    return settings.enabled && settings.orders && CoaWorldGuildRules::OrderedItem(msg);
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
    // 3.1.4: a category (trade, consumable, equipment, other; empty or unknown: all), filtered before the pages.
    std::string const category = args.size() > 3 ? args[3] : std::string();

    time_t const now = time(nullptr);
    CatalogCache& cache = g_catalogs[player->GetGuildId()];
    if (!cache.at || now - cache.at >= CATALOG_SECONDS)
        cache = { now, BuildCatalog(player->GetGuildId()) };

    std::vector<CatalogEntry const*> found;
    for (CatalogEntry const& e : cache.entries)
        if (search.empty() || e.lower.find(search) != std::string::npos)
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(e.item))
                if (CoaWorldGuildRules::InCatalogCategory(proto->Class, category))
                    found.push_back(&e);
    constexpr uint32 PER_PAGE = 30;
    std::vector<std::string> out;
    for (uint32 i = (page - 1) * PER_PAGE; i < found.size() && i < page * PER_PAGE; ++i)
    {
        CatalogEntry const& e = *found[i];
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(e.item);
        if (!proto)
            continue;
        // 3.1.4: the item's class and subclass after the name (the 12 fields before stay as the addon reads them).
        out.push_back(Cut(Acore::StringFormat("GC\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}\t{}", e.item,
                                              e.craft ? "craft" : "stock", Text(e.bot), e.amount, e.price, proto->Quality,
                                              proto->ItemLevel, proto->RequiredLevel, proto->InventoryType, e.skill,
                                              Text(proto->Name1), proto->Class, proto->SubClass)));
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
        if (settings.greetings && CoaWorldGuildRules::IsGoodbye(msg))
        {
            Push({ GuildEvent::BYE, player->GetGUID(), 0 });
            return true;
        }
        if (!settings.orders)
            return true;
        if (uint32 entry = CoaWorldGuildRules::OrderedItem(msg))
            Push({ GuildEvent::ORDER, player->GetGUID(), entry, CoaWorldGuildRules::OrderedQty(msg) });
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
