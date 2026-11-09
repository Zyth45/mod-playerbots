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

#include "CoaLive.h"
#include "CoaWorldBots.h"

#include "AuctionHouseMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
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

#include <algorithm>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    using CoaWorld::BotLine;
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
    };

    Settings settings;

    struct Speech
    {
        ObjectGuid speaker;
        uint32 guildId;
        std::string text;
        time_t at;
    };

    // From map threads: what happened, to be told by the world thread.
    struct GuildEvent
    {
        enum Kind : uint8 { LEVEL, LOOT, LOGIN, ORDER, HUMAN_LEVEL } kind;
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

    bool IsBot(Player* player)
    {
        return player && GET_PLAYERBOT_AI(player);
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

    // The gear the guild may give, by required level (1 to 80), found once: armour and weapons, uncommon or rare,
    // not bound on pickup, among the items the bots' gear code knows (RandomItemMgr), when it knows any.
    std::vector<std::vector<uint32>> const& GearPool()
    {
        static std::vector<std::vector<uint32>> pool;
        if (!pool.empty())
            return pool;
        pool.resize(81);
        std::vector<std::vector<uint32>> unchecked(81);
        bool anyKnown = false;
        for (auto const& [id, proto] : *sObjectMgr->GetItemTemplateStore())
        {
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

        void Update(uint32 diff)
        {
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
                        Welcome(player, now);
                        break;
                    case GuildEvent::ORDER:
                        Order(player, e.value, now);
                        break;
                    case GuildEvent::HUMAN_LEVEL:
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
                if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(speaker))
                    botAI->SayToGuild(s.text);
            }
            _speeches.swap(later);
        }

        // A real player of a guild with bots logs in: a guildmate says hello, and now and then sends a gift.
        void Welcome(Player* player, time_t now)
        {
            Player* bot = OnlineGuildBot(player->GetGuildId(), player->GetGUID());
            if (!bot)
                return;

            static char const* const hi[] = { "hi {}", "hey {}!", "o/ {}", "wb {}" };
            if (!settings.coaLive && roll_chance_i(60))
                Say(bot, Acore::StringFormat(Pick(hi), player->GetName()), now + urand(4, 12));

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
            MailDraft draft("From the guild", Acore::StringFormat("Had some spare, thought you could use them. - {}", bot->GetName()));
            if (Item* item = Item::CreateItem(potion, 5, bot))
            {
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
            if (Item* item = Item::CreateItem(bandage, 10, bot))
            {
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
            draft.SendMailTo(trans, MailReceiver(player), MailSender(bot), MAIL_CHECK_MASK_NONE, urand(5, 30) * MINUTE);
            CharacterDatabase.CommitTransaction(trans);
            if (!BotLine(bot, "guild_gift", { { "player", player->GetName() } }))
                Say(bot, Acore::StringFormat("{}, sent you a few potions", player->GetName()), now + urand(20, 40));
        }

        // An item of the bot's bags by mail to the player, free or cash on delivery.
        static void MailFromBags(Player* bot, Player* to, Item* item, uint64 cod, uint32 delay)
        {
            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
            bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
            item->DeleteFromInventoryDB(trans);
            item->SetOwnerGUID(to->GetGUID());
            item->SaveToDB(trans);
            MailDraft draft(item->GetTemplate()->Name1, "From the guild.");
            draft.AddItem(item);
            if (cod)
                draft.AddCOD(uint32(cod));
            draft.SendMailTo(trans, MailReceiver(to), MailSender(bot), MAIL_CHECK_MASK_COPIED, delay);
            bot->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);
        }

        // A new item by mail to the player, from a guildmate.
        static bool MailNew(Player* bot, Player* to, uint32 entry, uint32 count, uint64 cod, uint32 delay,
                            std::string const& body)
        {
            Item* item = Item::CreateItem(entry, count, bot);
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
        void Order(Player* player, uint32 entry, time_t now)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
            if (!proto || proto->Quality > settings.orderMaxQuality || proto->Bonding == BIND_QUEST_ITEM)
                return;
            uint32 const today = uint32(now / DAY);
            std::pair<uint32, uint32>& count = _orders[player->GetGUID().GetCounter()];
            if (count.first != today)
                count = { today, 0 };
            if (!CoaWorldGuildRules::MayOrder(count.second, settings.ordersPerDay))
                return;

            std::vector<Player*> mates;
            for (auto const& [guid, bot] : RandomBots())
                if (bot && bot->IsInWorld() && bot->GetGuildId() == player->GetGuildId() && bot->IsAlive())
                    mates.push_back(bot);
            if (mates.empty())
                return;
            std::shuffle(mates.begin(), mates.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
            CoaWorld::Slots slots = { { "player", player->GetName() }, { "item", Link(proto, player->GetLevel()) } };

            // 1. From the bags: free.
            for (Player* bot : mates)
            {
                Item* item = bot->GetItemByEntry(entry);
                if (!item || item->IsEquipped() || item->IsSoulBound() || item->IsInTrade())
                    continue;
                ++count.second;
                MailFromBags(bot, player, item, 0, urand(1, 4) * MINUTE);
                Answer(bot, "guild_order_share", slots, Acore::StringFormat("{} I have one, sending it", player->GetName()),
                       now + urand(5, 15));
                LogEvent("guild", bot->GetName(), Acore::StringFormat("sent {} to {}", proto->Name1, player->GetName()));
                return;
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
                        if (!MailNew(bot, player, entry, std::min<uint32>(made, proto->GetMaxStackSize()), cost,
                                     urand(10, 30) * MINUTE, "Made it for you, you only pay the materials."))
                            continue;
                        ++count.second;
                        slots["price"] = Money(cost);
                        Answer(bot, "guild_order_craft", slots,
                               Acore::StringFormat("{} I can make that, mats only ({}), will mail it", player->GetName(),
                                                   Money(cost)), now + urand(5, 15));
                        LogEvent("guild", bot->GetName(), Acore::StringFormat("crafts {} for {}", proto->Name1,
                                                                               player->GetName()));
                        return;
                    }

            // 3. Bought out at the auction house of the player's side.
            if (Auction(player, mates, proto, slots, now))
            {
                ++count.second;
                return;
            }

            if (roll_chance_i(50))
                Answer(mates.front(), "guild_order_none", slots,
                       Acore::StringFormat("sorry {}, nobody has that right now", player->GetName()), now + urand(8, 20));
        }

        bool Auction(Player* player, std::vector<Player*> const& mates, ItemTemplate const* proto,
                     CoaWorld::Slots& slots, time_t now)
        {
            AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(player->GetFaction());
            if (!house)
                return false;
            AuctionEntry* best = nullptr;
            for (auto const& [id, auction] : house->GetAuctions())
                if (auction && auction->item_template == proto->ItemId && auction->buyout && !auction->bidder &&
                    auction->owner != player->GetGUID() && (!best || auction->buyout < best->buyout))
                    best = auction;
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
            item->SaveToDB(trans);
            MailDraft(proto->Name1, "Got it at the auction house, you pay what I paid.")
                .AddItem(item)
                .AddCOD(price)
                .SendMailTo(trans, MailReceiver(player), MailSender(buyer), MAIL_CHECK_MASK_COPIED, urand(5, 15) * MINUTE);
            buyer->SaveInventoryAndGoldToDB(trans);
            CharacterDatabase.CommitTransaction(trans);

            slots["price"] = Money(price);
            Answer(buyer, "guild_order_ah", slots,
                   Acore::StringFormat("{} saw one on the AH for {}, I'll grab it for you", player->GetName(), Money(price)),
                   now + urand(5, 15));
            LogEvent("guild", buyer->GetName(), Acore::StringFormat("bought {} at the auction house for {}", proto->Name1,
                                                                     player->GetName()));
            return true;
        }

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
            uint32 bestEntry = 0;
            float bestGain = 0.0f;
            for (uint32 entry : candidates)
            {
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                if (player->CanUseItem(proto) != EQUIP_ERR_OK)
                    continue;
                uint8 const slot = player->FindEquipSlot(proto, NULL_SLOT, true);
                if (slot == NULL_SLOT)
                    continue;
                Item* worn = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                float const wornScore = worn ? calc.CalculateItem(worn->GetEntry(), worn->GetItemRandomPropertyId()) : 0.0f;
                float const score = calc.CalculateItem(entry);
                if (CoaWorldGuildRules::WorthGiving(score, wornScore) && score - wornScore > bestGain)
                {
                    bestGain = score - wornScore;
                    bestEntry = entry;
                }
            }
            if (!bestEntry)
                return;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(bestEntry);
            if (!MailNew(bot, player, bestEntry, 1, 0, urand(5, 20) * MINUTE,
                         Acore::StringFormat("Found this, it should suit you better. - {}", bot->GetName())))
                return;
            player->UpdatePlayerSetting(GEAR_SETTING, GEAR_SETTING_LAST_DAY, today);
            Answer(bot, "guild_gear_gift", { { "player", player->GetName() }, { "item", Link(proto, player->GetLevel()) } },
                   Acore::StringFormat("{}, sent you something you can use, check your mail", player->GetName()),
                   now + urand(20, 40));
            LogEvent("guild", bot->GetName(), Acore::StringFormat("gave {} to {}", proto->Name1, player->GetName()));
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
            if (BotLine(bot, "guild_recruit", { { "guild", guild->GetName() } }))
                return;
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                botAI->SayToChannel(Acore::StringFormat(Pick(calls), guild->GetName()), GENERAL);
        }

        std::vector<Speech> _speeches;
        std::unordered_map<uint32, time_t> _lastMotd;
        std::unordered_map<uint32, time_t> _lastGift;
        std::unordered_map<uint32, std::pair<uint32, uint32>> _orders;  // player -> (day, orders that day)
        uint32 _timer = 0;
        uint32 _slow = 0;
        uint32 _recruit = 0;
    };

    void LoadSettings()
    {
        auto key = [](char const* name) { return std::string(PREFIX) + name; };
        settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), false, false);
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
        settings.coaLive = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaLive.Enable", false, false);
    }
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
        if (!IsBot(player))
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
        if (!settings.enabled || !player || !player->GetGuildId() || IsBot(player))
            return;
        Push({ GuildEvent::LOGIN, player->GetGUID(), 0 });
    }

    // A real player asks the guild for an item: queued for the world thread (Order).
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*language*/, std::string& msg, Guild* guild) override
    {
        if (!settings.enabled || !settings.orders || !player || !guild || IsBot(player))
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
