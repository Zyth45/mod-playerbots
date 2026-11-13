/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

// CoA Economy: the artisans in the SquidBots Lite addon (D:\CoA-Addon\docs\WORLD-PROTOCOLE-3.0.md §0 and §2), off unless
// AiPlayerbot.CoaEconomy.Artisans.Addon = 1 (and Artisans.Enable; quotes and orders also need CraftOrders.Enable).
// The addon whispers itself "SQBW\t<request>"; this script takes only its own words (ALIST, ARCP, AQUOTE, AORDER,
// AMAIL), never lets them through, and answers with addon whispers to the player alone. The server checks everything:
// the player orders for itself only, from a random bot of its faction that is online; quantities and prices are its
// own. 1 request a second per player (more are dropped), list answers cached 10 s. World thread (CMSG_MESSAGECHAT
// is thread-unsafe, handled in the world update).

#include "CoaArtisans.h"

#include "CoaEconomyConf.h"
#include "CoaArtisansAddonRules.h"
#include "ChatHelper.h"
#include "CoaLive.h"
#include "CoaCraftOrders.h"
#include "CoaEconomyData.h"
#include "CoaEconomyDb.h"

#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"
#include "WorldPacket.h"

#include <algorithm>
#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

namespace CoaEconomy::ArtisansAddon
{

namespace
{
    namespace Rules = CoaArtisansAddonRules;

    // ---- What the player asked, per player (world thread) ----
    struct Asker
    {
        uint64 lastMs = 0;
        bool seen = false;
        std::string cachedRequest;
        uint64 cachedMs = 0;
        std::vector<std::string> cachedLines;
    };
    std::unordered_map<ObjectGuid::LowType, Asker> askers;

    struct Quote
    {
        ObjectGuid::LowType player = 0;
        ObjectGuid bot;
        std::string botName;
        uint32 itemId = 0;
        uint32 qty = 0;
        uint32 copper = 0;
        uint32 at = 0;
    };
    std::map<uint32, Quote> quotes;
    uint32 nextQuoteId = 1;

    // ---- The player's orders (AMAIL): kept 7 days in coa_economy_orders, read back at start-up ----
    struct OrderRec
    {
        uint32 id = 0;                    // the quote id
        ObjectGuid::LowType player = 0;
        ObjectGuid::LowType bot = 0;
        std::string botName;
        uint32 itemId = 0;
        uint32 qty = 0;
        uint32 perPiece = 1;              // items a craft makes (memory only: needed while preparing)
        uint32 copper = 0;
        std::string state;                // preparing, sent, awaiting, paid, returned, cancelled
        uint32 changed = 0;               // when the state last changed
        uint32 eta = 0;                   // when the mail is (or was) in the mailbox
        uint32 sentAt = 0;
        uint64 sendAtMs = 0;              // preparing: when the mail goes
    };
    std::map<uint32, OrderRec> orders;    // by id
    constexpr char const* OrdersTable = "coa_economy_orders";
    bool ordersTable = false;
    uint32 trackTimer = 0;
    constexpr uint32 TrackEveryMs = 2000;

    uint32 Now() { return uint32(GameTime::GetGameTime().count()); }
    uint64 NowMs() { return uint64(GameTime::GetGameTimeMS().count()); }

    // The crafting professions (checked with coa-local, SkillLine.dbc): blacksmithing, leatherworking, alchemy,
    // tailoring, engineering, enchanting, jewelcrafting, inscription, CoA's fletching, woodworking and bushcraft,
    // cooking and first aid. Not the gathering ones (mining, herbalism, skinning, fishing, woodcutting), riding or
    // racials. A profession is listed only when the bot knows a sellable recipe of it.
    constexpr uint32 CraftingSkills[] = {164, 165, 171, 197, 202, 333, 755, 773, 806, 757, 505, 185, 129};

    bool IsCrafting(uint32 skill)
    {
        return std::find(std::begin(CraftingSkills), std::end(CraftingSkills), skill) != std::end(CraftingSkills);
    }

    std::string ZoneName(uint32 zoneId)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(zoneId);
        return area && area->area_name[0] ? Rules::Clean(area->area_name[0]) : "";
    }

    void Send(Player* player, std::string const& line)
    {
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, LANG_ADDON, player, player, Rules::Wire(line));
        player->SendDirectMessage(&data);
    }

    // A random bot of the player's faction, online, by name (the addon gives names).
    Player* FindArtisan(Player* player, std::string const& name)
    {
        if (name.empty())
            return nullptr;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
            if (bot && bot->IsInWorld() && bot->GetName() == name)
                return bot->GetTeamId() == player->GetTeamId() ? bot : nullptr;
        return nullptr;
    }

    bool FindRecipe(Player* bot, uint32 itemId, Artisans::Recipe& out)
    {
        for (Artisans::Recipe const& recipe : Artisans::KnownRecipes(bot))
            if (recipe.itemId == itemId)
            {
                out = recipe;
                return true;
            }
        return false;
    }

    // ---- The requests ----
    void List(Player* player, std::vector<std::string>& lines)
    {
        // The best-ranked crafters of the faction first (a cheap skill read), then their real recipes.
        struct Candidate { Player* bot; uint32 best; };
        std::vector<Candidate> candidates;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        {
            if (!bot || !bot->IsInWorld() || bot->GetTeamId() != player->GetTeamId())
                continue;
            uint32 best = 0;
            for (uint32 skill : CraftingSkills)
                best = std::max<uint32>(best, bot->GetSkillValue(skill));
            if (best)
                candidates.push_back({bot, best});
        }
        std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) { return a.best > b.best; });

        struct Entry { uint32 mainSkill; uint32 mainRank; std::string line; };
        std::vector<Entry> entries;
        for (Candidate const& candidate : candidates)
        {
            if (entries.size() >= Rules::MaxArtisans)
                break;
            std::vector<uint32> skills;
            for (Artisans::Recipe const& recipe : Artisans::KnownRecipes(candidate.bot))
                if (IsCrafting(recipe.skillId) && std::find(skills.begin(), skills.end(), recipe.skillId) == skills.end())
                    skills.push_back(recipe.skillId);
            if (skills.empty())
                continue;
            // Highest rank first; the first is the artisan's main profession.
            std::sort(skills.begin(), skills.end(), [&](uint32 a, uint32 b)
            {
                return candidate.bot->GetSkillValue(a) > candidate.bot->GetSkillValue(b);
            });
            std::string list;
            for (uint32 skill : skills)
                list += (list.empty() ? "" : ",") + std::to_string(skill) + ":" + std::to_string(candidate.bot->GetSkillValue(skill));
            entries.push_back({skills.front(), candidate.bot->GetSkillValue(skills.front()),
                               "AB	" + Rules::Clean(candidate.bot->GetName()) + "	" + std::to_string(candidate.bot->GetLevel()) +
                               "	" + ZoneName(candidate.bot->GetZoneId()) + "	" + list});
        }
        // By profession, then by rank.
        std::sort(entries.begin(), entries.end(), [](Entry const& a, Entry const& b)
        {
            return a.mainSkill != b.mainSkill ? a.mainSkill < b.mainSkill : a.mainRank > b.mainRank;
        });
        for (Entry const& entry : entries)
            lines.push_back(entry.line);
    }

    void Recipes(Player* player, std::vector<std::string> const& fields, std::vector<std::string>& lines)
    {
        Player* bot = fields.size() > 1 ? FindArtisan(player, fields[1]) : nullptr;
        if (!bot)
        {
            lines.push_back("ERR\tARCP\toffline");
            return;
        }
        std::string const name = Rules::Clean(bot->GetName());
        // One entry per product, with the rank its recipe needs (to sort by tier).
        struct Listed { Artisans::Recipe recipe; ItemTemplate const* proto; uint32 rank; };
        std::vector<Listed> listed;
        for (Artisans::Recipe const& recipe : Artisans::KnownRecipes(bot))
        {
            if (std::any_of(listed.begin(), listed.end(), [&](Listed const& l) { return l.recipe.itemId == recipe.itemId; }))
                continue;
            uint32 rank = 0;
            SkillLineAbilityMapBounds const bounds = sSpellMgr->GetSkillLineAbilityMapBounds(recipe.spell->Id);
            for (auto it = bounds.first; it != bounds.second; ++it)
                if (it->second->SkillLine == recipe.skillId)
                    rank = it->second->MinSkillLineRank ? it->second->MinSkillLineRank : it->second->TrivialSkillLineRankLow;
            listed.push_back({recipe, sObjectMgr->GetItemTemplate(recipe.itemId), rank});
        }
        // The best first, so the cap drops the least: a bot's spells come in no order.
        std::sort(listed.begin(), listed.end(), [](Listed const& a, Listed const& b)
        {
            return Rules::RecipeBefore(a.proto ? a.proto->Quality : 0, a.rank, a.recipe.itemId,
                                       b.proto ? b.proto->Quality : 0, b.rank, b.recipe.itemId);
        });
        if (listed.size() > Rules::MaxRecipes)
            listed.resize(Rules::MaxRecipes);
        for (Listed const& entry : listed)
        {
            Artisans::Recipe const& recipe = entry.recipe;
            ItemTemplate const* proto = entry.proto;
            uint32 const rank = entry.rank;
            // The English name (the client may not have the item cached) is last: Wire() cuts the line, so only the
            // name can be cut.
            lines.push_back("AR\t" + name + "\t" + std::to_string(recipe.itemId) + "\t" + std::to_string(recipe.spell->Id) +
                            "\t" + std::to_string(recipe.skillId) + "\t" + std::to_string(proto ? proto->Quality : 0) +
                            "\t" + std::to_string(rank) + "\t" + (proto ? Rules::Clean(proto->Name1) : std::string()));
            // Its reagents (the client cannot read those of a spell it does not know).
            std::string reagents;
            for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
                if (recipe.spell->Reagent[i] > 0 && recipe.spell->ReagentCount[i])
                    reagents += (reagents.empty() ? "" : ",") + std::to_string(recipe.spell->Reagent[i]) + ":" +
                                std::to_string(recipe.spell->ReagentCount[i]);
            if (!reagents.empty())
                lines.push_back("ARR\t" + name + "\t" + std::to_string(recipe.itemId) + "\t" + reagents);
        }
    }

    void QuoteRequest(Player* player, std::vector<std::string> const& fields, std::vector<std::string>& lines)
    {
        std::string const name = fields.size() > 1 ? Rules::Clean(fields[1]) : "";
        uint32 const itemId = fields.size() > 2 ? Rules::ParseNumber(fields[2]) : 0;
        uint32 const qty = fields.size() > 3 ? Rules::ParseNumber(fields[3]) : 0;
        auto answer = [&](uint32 copper, char const* state, uint32 quoteId)
        {
            lines.push_back("AQ\t" + name + "\t" + std::to_string(itemId) + "\t" + std::to_string(qty) + "\t" +
                            std::to_string(copper) + "\t" + state + "\t" + std::to_string(quoteId));
        };

        if (!CraftOrders::IsEnabled())
            return answer(0, "off", 0);
        if (!Rules::QtyOk(qty))
            return answer(0, "qty", 0);
        Player* bot = nullptr;
        for (auto const& [guid, candidate] : sRandomPlayerbotMgr.GetAllBots())
            if (candidate && candidate->IsInWorld() && candidate->GetName() == name)
                bot = candidate;
        if (!bot)
            return answer(0, "offline", 0);
        if (bot->GetTeamId() != player->GetTeamId())
            return answer(0, "faction", 0);
        Artisans::Recipe recipe;
        if (!itemId || !FindRecipe(bot, itemId, recipe))
            return answer(0, "cannot", 0);
        if (CraftOrders::IsBusy(bot))
            return answer(0, "busy", 0);
        // Everything goes in one CoD mail: 12 stacks at most (20 elixirs are one stack, 20 swords are too many).
        if (ItemTemplate const* product = sObjectMgr->GetItemTemplate(recipe.itemId);
            !product || Rules::Stacks(qty * recipe.count, product->GetMaxStackSize()) > Rules::MaxMailItems)
            return answer(0, "qty", 0);
        if (Artisans::MissingCost(bot, recipe, qty) > bot->GetMoney())
            return answer(0, "mats", 0);

        uint64 const price = std::min<uint64>(uint64(CraftOrders::QuoteFor(bot, recipe)) * qty, MAX_MONEY_AMOUNT);
        // Old quotes go (10 min); then this one.
        uint32 const now = Now();
        for (auto it = quotes.begin(); it != quotes.end();)
            it = Rules::QuoteExpired(it->second.at, now) ? quotes.erase(it) : std::next(it);
        uint32 const quoteId = nextQuoteId++;
        quotes[quoteId] = {player->GetGUID().GetCounter(), bot->GetGUID(), name, itemId, qty, uint32(price), now};
        answer(uint32(price), "ok", quoteId);

        // The artisan also says it, as a player would (CoA Live's bank and pace; nothing when its chat is off). The
        // addon's card stays the way to order.
        std::unordered_map<std::string, std::string> slots;
        if (ItemTemplate const* product = sObjectMgr->GetItemTemplate(recipe.itemId))
            slots["item"] = ChatHelper::FormatItem(product);
        slots["qty"] = std::to_string(qty);
        slots["price"] = ChatHelper::formatMoney(uint32(price));
        slots["profession"] = Artisans::ProfessionLink(bot, recipe.skillId);
        CoaLive::SayLine(bot, "craft_quote_addon", slots, player->GetGUID());
    }

    std::string OrderLine(OrderRec const& order)
    {
        return "AM\t" + std::to_string(order.id) + "\t" + Rules::Clean(order.botName) + "\t" + std::to_string(order.itemId) +
               "\t" + std::to_string(order.qty) + "\t" + std::to_string(order.copper) + "\t" + std::to_string(order.changed) +
               "\t" + order.state + "\t" + std::to_string(order.eta);
    }

    void Save(OrderRec const& order)
    {
        if (!ordersTable)
            return;
        std::string name = order.botName;
        name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c == '\'' || c == '\\'; }), name.end());
        PlayerbotsDatabase.Execute("REPLACE INTO {} (id, player, bot, bot_name, item, qty, copper, state, changed, eta) "
                                   "VALUES ({}, {}, {}, '{}', {}, {}, {}, '{}', {}, {})", OrdersTable, order.id, order.player,
                                   order.bot, name, order.itemId, order.qty, order.copper, order.state, order.changed, order.eta);
    }

    // A new state: saved, and pushed to the player's addon at once (the same AM line, then "-").
    void SetState(OrderRec& order, std::string const& state)
    {
        if (order.state == state)
            return;
        order.state = state;
        order.changed = Now();
        Save(order);
        if (Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(order.player)))
        {
            Send(player, OrderLine(order));
            Send(player, "-");
            auto asker = askers.find(order.player);
            if (asker != askers.end() && asker->second.cachedRequest == "AMAIL")
                asker->second.cachedRequest.clear();
        }
    }

    // The artisan has "crafted" it: the pieces go in one CoD mail, no delivery delay (the core's MailDeliveryDelay is
    // for mail between players; the whispers play the time it takes).
    void SendOrder(OrderRec& order)
    {
        ObjectGuid const receiver = ObjectGuid::Create<HighGuid::Player>(order.player);
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(order.itemId);
        MailDraft draft(proto ? proto->Name1 : "", "");
        uint32 made = 0;
        // Full stacks: 20 elixirs in one slot, 3 swords in three.
        uint32 const maxStack = std::max<uint32>(1, proto ? proto->GetMaxStackSize() : 1);
        for (uint32 left = order.qty * order.perPiece; left && made < Rules::MaxMailItems;)
        {
            uint32 const count = std::min(left, maxStack);
            left -= count;
            if (Item* item = Item::CreateItem(order.itemId, count, nullptr, false,
                                              Item::GenerateItemRandomPropertyId(order.itemId)))
            {
                item->SetOwnerGUID(receiver);
                item->SaveToDB(trans);
                draft.AddItem(item);
                ++made;
            }
        }
        if (!made)
        {
            CharacterDatabase.CommitTransaction(trans);
            SetState(order, "cancelled");
            return;
        }
        // Online, the mail must go to the Player object (the core then puts it in its mailbox at once).
        Player* online = ObjectAccessor::FindConnectedPlayer(receiver);
        draft.AddCOD(order.copper).SendMailTo(trans, online ? MailReceiver(online) : MailReceiver(order.player),
                                              MailSender(MAIL_NORMAL, order.bot),
                                              MAIL_CHECK_MASK_COPIED, 0);
        CharacterDatabase.CommitTransaction(trans);
        order.sentAt = Now();
        order.eta = order.sentAt;
        SetState(order, "sent");
    }

    void Order(Player* player, std::vector<std::string> const& fields, std::vector<std::string>& lines)
    {
        uint32 const quoteId = fields.size() > 1 ? Rules::ParseNumber(fields[1]) : 0;
        auto answer = [&](char const* state) { lines.push_back("AO\t" + std::to_string(quoteId) + "\t" + state); };

        if (!CraftOrders::IsEnabled())
            return answer("off");
        auto found = quotes.find(quoteId);
        if (found == quotes.end() || found->second.player != player->GetGUID().GetCounter())
            return answer("expired");
        Quote const quote = found->second;
        if (Rules::QuoteExpired(quote.at, Now()))
        {
            quotes.erase(found);
            return answer("expired");
        }
        Player* bot = ObjectAccessor::FindPlayer(quote.bot);
        if (!bot || !bot->IsInWorld() || !sRandomPlayerbotMgr.GetAllBots().count(quote.bot))
            return answer("offline");
        if (bot->GetTeamId() != player->GetTeamId())
            return answer("faction");
        Artisans::Recipe recipe;
        if (!FindRecipe(bot, quote.itemId, recipe))
            return answer("cannot");
        if (CraftOrders::IsBusy(bot))
            return answer("busy");
        if (Rules::PriceRose(quote.copper, uint64(CraftOrders::QuoteFor(bot, recipe)) * quote.qty))
            return answer("price");
        if (!Artisans::Consume(bot, recipe, quote.qty))
            return answer("mats");

        quotes.erase(found);
        CraftOrders::MarkBusy(bot);

        // Taken: "preparing" until the mail goes, a few seconds later.
        OrderRec& order = orders[quoteId];
        order.id = quoteId;
        order.player = player->GetGUID().GetCounter();
        order.bot = bot->GetGUID().GetCounter();
        order.botName = quote.botName;
        order.itemId = quote.itemId;
        order.qty = quote.qty;
        order.perPiece = recipe.count;
        order.copper = quote.copper;
        order.sendAtMs = NowMs() + Rules::SendDelayMs;
        order.eta = Now() + Rules::SendDelayMs / IN_MILLISECONDS;
        order.state = "preparing";
        order.changed = Now();
        Save(order);

        // As a real artisan: a word that it takes the order, then one that the mail is gone (CoA Live whispers, the
        // rules' typing delay; nothing said when the bank has no such lines).
        std::unordered_map<std::string, std::string> slots;
        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(quote.itemId))
            slots["item"] = ChatHelper::FormatItem(proto);
        slots["price"] = ChatHelper::formatMoney(quote.copper);
        slots["qty"] = std::to_string(quote.qty);
        slots["profession"] = Artisans::ProfessionLink(bot, recipe.skillId);
        slots["recipe"] = Artisans::RecipeLink(bot, recipe);
        CoaLive::SayLine(bot, "craft_order_ack", slots, player->GetGUID());
        CoaLive::SayLine(bot, "craft_order_sent", slots, player->GetGUID());

        Data::RecordOrder(player->GetName(), bot->GetName(), quote.itemId, quote.copper, "mailed", recipe.skillId, "player");
        answer("preparing");
        lines.push_back(OrderLine(order));
    }

    // The CoD mail of an order in the player's mailbox (sender, price and item match), or null.
    Mail const* FindMail(Player* player, OrderRec const& order)
    {
        for (Mail const* mail : player->GetMails())
            if (mail && mail->state != MAIL_STATE_DELETED && mail->messageType == MAIL_NORMAL && mail->sender == order.bot &&
                (mail->COD == order.copper || (order.state == "paid" && !mail->COD)))
                return mail;
        return nullptr;
    }

    // Brings a sent order up to date from the player's mailbox (online players only; offline, it waits).
    void Track(OrderRec& order, Player* player, uint32 now)
    {
        if (order.state != "sent" && order.state != "awaiting")
            return;
        Mail const* mail = FindMail(player, order);
        if (mail)
            order.eta = uint32(mail->deliver_time);
        SetState(order, Rules::NextState(order.state, false, mail != nullptr, mail && mail->deliver_time <= time_t(now),
                                         now - order.sentAt));
    }

    void Mails(Player* player, std::vector<std::string>& lines)
    {
        uint32 const now = Now();
        std::vector<OrderRec*> mine;
        for (auto& [id, order] : orders)
            if (order.player == player->GetGUID().GetCounter() && order.changed + Rules::MailDays * DAY >= now)
            {
                Track(order, player, now);
                mine.push_back(&order);
            }
        // Newest first, 20 at most.
        std::sort(mine.begin(), mine.end(), [](OrderRec const* a, OrderRec const* b) { return a->id > b->id; });
        for (OrderRec const* order : mine)
        {
            if (lines.size() >= Rules::MaxMails)
                break;
            lines.push_back(OrderLine(*order));
        }
    }

    // Start-up: the orders of the last 7 days come back; an order caught "preparing" by the stop is cancelled.
    void LoadOrders()
    {
        ordersTable = Db::HasTable(OrdersTable, {"id", "player", "bot", "bot_name", "item", "qty", "copper", "state",
                                                 "changed", "eta"});
        if (!ordersTable)
        {
            LOG_ERROR("server.loading", "CoA Economy: table {} is missing or out of date in the playerbots database; "
                      "addon orders are tracked for this session only", OrdersTable);
            return;
        }
        uint32 const now = Now();
        PlayerbotsDatabase.DirectExecute("DELETE FROM {} WHERE changed < {}", OrdersTable, now - Rules::MailDays * DAY);
        if (QueryResult result = PlayerbotsDatabase.Query(
                "SELECT id, player, bot, bot_name, item, qty, copper, state, changed, eta FROM {}", OrdersTable))
            do
            {
                Field* f = result->Fetch();
                OrderRec order;
                order.id = f[0].Get<uint32>();
                order.player = f[1].Get<uint32>();
                order.bot = f[2].Get<uint32>();
                order.botName = f[3].Get<std::string>();
                order.itemId = f[4].Get<uint32>();
                order.qty = f[5].Get<uint32>();
                order.copper = f[6].Get<uint32>();
                order.state = f[7].Get<std::string>();
                order.changed = f[8].Get<uint32>();
                order.eta = f[9].Get<uint32>();
                order.sentAt = order.changed;
                nextQuoteId = std::max(nextQuoteId, order.id + 1);
                OrderRec& kept = orders[order.id] = order;
                if (kept.state == "preparing")
                    SetState(kept, "cancelled");
            } while (result->NextRow());
    }

    // World update: mails that are due, and the online players' sent orders.
    void UpdateOrders(uint32 diff)
    {
        trackTimer += diff;
        if (trackTimer < TrackEveryMs)
            return;
        trackTimer = 0;
        uint64 const nowMs = NowMs();
        uint32 const now = Now();
        for (auto& [id, order] : orders)
        {
            if (order.state == "preparing" && nowMs >= order.sendAtMs)
                SendOrder(order);
            else if (order.state == "sent" || order.state == "awaiting")
                if (Player* player = ObjectAccessor::FindConnectedPlayer(ObjectGuid::Create<HighGuid::Player>(order.player)))
                    Track(order, player, now);
        }
        // Forget what is final and older than 7 days.
        for (auto it = orders.begin(); it != orders.end();)
            it = Rules::IsFinal(it->second.state) && it->second.changed + Rules::MailDays * DAY < now ? orders.erase(it)
                                                                                                    : std::next(it);
    }

    // ---- The channel ----
    bool IsOurs(std::string const& word)
    {
        return word == "ALIST" || word == "ARCP" || word == "AQUOTE" || word == "AORDER" || word == "AMAIL";
    }

    void Handle(Player* player, std::string const& request)
    {
        std::vector<std::string> const fields = Rules::SplitTabs(request);
        Asker& asker = askers[player->GetGUID().GetCounter()];
        uint64 const nowMs = NowMs();
        if (!Rules::RateAllowed(asker.lastMs, nowMs, !asker.seen))
            return;  // dropped, no answer
        asker.lastMs = nowMs;
        asker.seen = true;

        std::string const& word = fields[0];
        bool const isList = word == "ALIST" || word == "ARCP" || word == "AMAIL";
        std::vector<std::string> lines;
        if (isList && asker.cachedRequest == request && Rules::CacheFresh(asker.cachedMs, nowMs))
            lines = asker.cachedLines;
        else
        {
            if (word == "ALIST")
                List(player, lines);
            else if (word == "ARCP")
                Recipes(player, fields, lines);
            else if (word == "AQUOTE")
                QuoteRequest(player, fields, lines);
            else if (word == "AORDER")
            {
                Order(player, fields, lines);
                // The addon asks AMAIL right after an order: the new mail must be in it.
                if (asker.cachedRequest == "AMAIL")
                    asker.cachedRequest.clear();
            }
            else
                Mails(player, lines);
            if (isList)
            {
                asker.cachedRequest = request;
                asker.cachedMs = nowMs;
                asker.cachedLines = lines;
            }
        }
        for (std::string const& line : lines)
            Send(player, line);
        Send(player, "-");
    }

    class CoaArtisansAddonPlayer : public PlayerScript
    {
    public:
        CoaArtisansAddonPlayer()
            : PlayerScript("CoaArtisansAddonPlayer", {PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT, PLAYERHOOK_ON_LOGOUT}) { }

        using PlayerScript::OnPlayerCanUseChat;

        // The addon's whisper to itself: ours are answered and never sent on; any other word goes on.
        bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
        {
            if (type != CHAT_MSG_WHISPER || lang != LANG_ADDON || !player || receiver != player ||
                msg.compare(0, 5, Rules::Prefix) != 0)
                return true;
            std::string const request = msg.substr(5);
            if (!IsOurs(request.substr(0, request.find('\t'))))
                return true;
            if (Artisans::IsEnabled())
                Handle(player, request);
            return false;
        }

        void OnPlayerLogout(Player* player) override
        {
            if (player)
                askers.erase(player->GetGUID().GetCounter());
        }
    };

    // A CoD of ours paid: the payment mail goes from the player to the bot, with the price.
    class CoaArtisansAddonMail : public MailScript
    {
    public:
        CoaArtisansAddonMail() : MailScript("CoaArtisansAddonMail", {MAILHOOK_ON_BEFORE_MAIL_DRAFT_SEND_MAIL_TO}) { }

        void OnBeforeMailDraftSendMailTo(MailDraft* draft, MailReceiver const& receiver, MailSender const& sender,
                                         MailCheckMask& checked, uint32& /*deliverDelay*/, uint32& /*customExpiration*/,
                                         bool& /*deleteMailItemsFromDB*/, bool& /*sendMail*/) override
        {
            if (!draft || !(checked & MAIL_CHECK_MASK_COD_PAYMENT) || sender.GetMailMessageType() != MAIL_NORMAL)
                return;
            for (auto& [id, order] : orders)
                if ((order.state == "sent" || order.state == "awaiting") && order.player == sender.GetSenderId() &&
                    order.bot == receiver.GetPlayerGUIDLow() && order.copper == draft->GetMoney())
                {
                    SetState(order, "paid");
                    return;
                }
        }
    };

    class CoaArtisansAddonWorld : public WorldScript
    {
    public:
        CoaArtisansAddonWorld() : WorldScript("CoaArtisansAddonWorld", {WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE}) { }
        void OnStartup() override { LoadOrders(); }
        void OnUpdate(uint32 diff) override { UpdateOrders(diff); }
    };
}

} // namespace CoaEconomy::ArtisansAddon

void AddSC_coa_economy_artisans_addon()
{
    // Off by default; read at start-up (restart to change it).
    if (!CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.Enable", true, false) ||
        !CoaEconomy::Conf::Get<bool>("AiPlayerbot.CoaEconomy.Artisans.Addon", true, false))
        return;
    new CoaEconomy::ArtisansAddon::CoaArtisansAddonPlayer();
    new CoaEconomy::ArtisansAddon::CoaArtisansAddonMail();
    new CoaEconomy::ArtisansAddon::CoaArtisansAddonWorld();
}
