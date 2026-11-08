/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaLive.h"

#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "CellImpl.h"
#include "ChatHelper.h"
#include "Chat.h"
#include "CoaLiveRules.h"
#include "CoaSessionRules.h"
#include "Timer.h"
#include "CoaMarketPrices.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "EmoteAction.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "LFGMgr.h"
#include "Mail.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "Util.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <cstdlib>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

using namespace CoaLive;

namespace
{
constexpr uint32 SCAN_MS = 5000;        // how often a bot looks around
constexpr float HEAR_RANGE = 30.0f;      // a real player this close hears the bot's /say
constexpr uint32 GREET_PLAYER_CD_S = 3600;  // a bot greets the same player once an hour
constexpr size_t QUEUE_MAX = 4096;

std::mutex liveLock;

struct Pending
{
    uint64 dueMs = 0;
    ObjectGuid bot;
    ObjectGuid subject;  // the player the bot reacts to (emote target, whisper, invite)
    uint8 channel = CH_NEAR;
    uint32 emote = 0;
    std::string text;
    bool invite = false;
    ObjectGuid codItem;  // an item of the bot's bags mailed to the subject, cash on delivery
    uint32 cod = 0;
};

// A Trade announce that a real player may answer (spec: never an announce that leads nowhere).
struct Offer
{
    bool wts = true;
    ObjectGuid item;      // WTS: the very item of the bot's bags
    uint32 itemId = 0;
    uint32 price = 0;     // the price said, for the whole stack
    uint64 expiresMs = 0;
};
constexpr uint64 OFFER_MS = 15 * MINUTE * IN_MILLISECONDS;
std::unordered_map<uint64, Offer> offers;

// The offer is answered: the bot does not deal twice.
void ForgetOffer(Player* bot)
{
    std::lock_guard<std::mutex> guard(liveLock);
    offers.erase(bot->GetGUID().GetRawValue());
}

std::shared_ptr<Bank const> bank;
Pacer pacer;
std::unordered_map<uint64, Ring> botRings;
std::unordered_map<std::string, Ring> channelRings;
std::vector<Pending> liveQueue;

Rng const rng{[](uint32_t lo, uint32_t hi) { return urand(lo, hi); }};

bool Enabled() { return sPlayerbotAIConfig.coaLiveEnable; }

bool IsRandom(Player* bot) { return bot && sRandomPlayerbotMgr.IsRandomBot(bot); }

uint8 BotLocale()
{
    int32 const forced = sPlayerbotAIConfig.botTextLocale;
    return static_cast<uint8>(forced >= 0 && forced < TOTAL_LOCALES ? forced
                                                                       : PlayerbotTextMgr::instance().GetLocalePriority());
}

// "Tirage par bot" of the typing speed: the same for a bot at every line.
uint32 MsPerChar(Player* bot)
{
    uint32 const lo = sPlayerbotAIConfig.coaLiveTypingMsPerCharMin;
    uint32 const hi = std::max(lo, sPlayerbotAIConfig.coaLiveTypingMsPerCharMax);
    return lo + static_cast<uint32>(bot->GetGUID().GetCounter() * 2654435761u % (hi - lo + 1));
}

std::string AreaName(uint32 areaId, uint8 locale)
{
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
    if (!area)
        return "";
    char const* name = area->area_name[locale];
    if (!name || !*name)
        name = area->area_name[LOCALE_enUS];
    return name ? name : "";
}

bool InCapital(Player* bot)
{
    AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId());
    return zone && (zone->flags & AREA_FLAG_CAPITAL);
}

std::list<Player*> RealPlayersNear(Player* bot, float range)
{
    std::list<Player*> players;
    Acore::AnyPlayerInObjectRangeCheck check(bot, range, true, true);
    Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(bot, players, check);
    Cell::VisitObjects(bot, searcher, range);
    players.remove_if([bot](Player* p) { return p == bot || !IsRealPlayer(p) || !bot->CanSeeOrDetect(p); });
    return players;
}

bool GroupHasRealPlayer(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* member = ref->GetSource())
            if (member != bot && IsRealPlayer(member))
                return true;
    return false;
}

// What the placeholders can say about this moment. A placeholder left out skips the lines that use it.
std::map<std::string, std::string> Values(Player* bot, Player* subject, Unit* boss, uint8 locale)
{
    std::map<std::string, std::string> v;
    if (subject)
        v["player"] = subject->GetName();
    v["zone"] = AreaName(bot->GetZoneId(), locale);
    if (bot->GetAreaId() != bot->GetZoneId())
        v["subzone"] = AreaName(bot->GetAreaId(), locale);
    if (InCapital(bot))
        v["city"] = v["zone"];
    v["level"] = std::to_string((subject ? subject : bot)->GetLevel());
    v["role"] = PlayerbotAI::IsTank(bot) ? "tank" : PlayerbotAI::IsHeal(bot) ? "heal" : "dps";
    if (boss)
        v["boss"] = boss->GetName();
    if (uint32 guildId = bot->GetGuildId())
        if (Guild* guild = sGuildMgr->GetGuildById(guildId))
            v["guild"] = guild->GetName();
    return v;
}

// The reaction of a bot to a situation: rolled, paced, drawn and typed now; played by the world thread later.
// scope: the place or the person the pace of the situation is counted on.
struct ReactOptions
{
    Unit* boss = nullptr;
    uint32 perSubjectCdS = 0;
    bool invite = false;
    float populationFactor = 1.0f;                 // the channel timers (1, 2, 4, 5)
    std::map<std::string, std::string> extra;      // %item, %price, %dungeon, %need...
    bool now = false;                              // no typing delay (a bot logging out)
    ObjectGuid codItem;                            // mail this item to the subject, cash on delivery
    uint32 cod = 0;
};

bool React(Player* bot, std::string const& situation, Player* subject, std::string const& scope,
           ReactOptions const& opt = {})
{
    if (!bot || !bot->IsInWorld())
        return false;

    std::shared_ptr<Bank const> current;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        current = bank;
    }
    if (!current)
        return false;
    Rule const* rule = current->RuleOf(situation);
    if (!rule || !rng.Roll(ReactPct(*rule, -1, opt.populationFactor)))
        return false;

    uint8 const locale = BotLocale();
    std::map<std::string, std::string> values = Values(bot, subject, opt.boss, locale);
    for (auto const& [key, value] : opt.extra)
        values[key] = value;
    uint32 const perSubjectCdS = opt.perSubjectCdS;
    bool const invite = opt.invite;
    uint64 const botKey = bot->GetGUID().GetRawValue();
    uint64 const nowMs = GameTime::GetGameTimeMS().count();
    uint64 const nowS = nowMs / 1000;

    std::lock_guard<std::mutex> guard(liveLock);
    // The channel of the situation, from its first line (the bank keeps one channel per situation).
    std::vector<Line> const* lines = current->LinesOf(situation, locale);
    if (!lines)
        return false;
    uint8 const channel = lines->front().channel;
    std::string const channelScope = std::to_string(channel) + "|" + scope;

    if (!pacer.Allowed(*rule, botKey, situation, scope, channel, nowS))
        return false;
    std::string const subjectKey = subject ? situation + "@" + std::to_string(subject->GetGUID().GetCounter()) : "";
    Rule perSubject;
    perSubject.botCdS = perSubjectCdS;
    if (perSubjectCdS && subject && !pacer.Allowed(perSubject, botKey, subjectKey, scope, channel, nowS))
        return false;

    Ring& botRing = botRings.try_emplace(botKey, sPlayerbotAIConfig.coaLiveNoRepeatBot).first->second;
    Ring& channelRing = channelRings.try_emplace(channelScope, sPlayerbotAIConfig.coaLiveNoRepeatChannel).first->second;
    bool const rareRoll = rng.Roll(sPlayerbotAIConfig.coaLiveRareChance);
    Pick const pick = Draw(*current, situation, locale, LevelBand(bot->GetLevel()), ARCH_NONE, rareRoll, &botRing,
                           &channelRing, values, rng);
    if (!pick.line)
        return false;

    std::string text;
    if (!pick.line->text.empty() &&
        !Humanize(pick.line->text, values, current->SlangOf(locale), sPlayerbotAIConfig.coaLiveAbbrevChance,
                  sPlayerbotAIConfig.coaLiveTypoChance, locale == LOCALE_frFR, rng, text))
        return false;

    pacer.Said(botKey, situation, scope, channel, nowS);
    if (perSubjectCdS && subject)
        pacer.Said(botKey, subjectKey, scope, channel, nowS);
    botRing.Push(pick.line->id);
    channelRing.Push(pick.line->id);
    if (botRings.size() > 20000)
        botRings.clear();
    if (channelRings.size() > 20000)
        channelRings.clear();

    if (liveQueue.size() >= QUEUE_MAX)
        return false;
    Pending p;
    p.dueMs = opt.now ? 0 : nowMs + TypingMs(*rule, text.size(), text.empty() ? 0 : MsPerChar(bot), rng);
    p.bot = bot->GetGUID();
    p.subject = subject ? subject->GetGUID() : ObjectGuid::Empty;
    p.channel = channel;
    p.emote = pick.line->emote;
    p.text = text;
    p.invite = invite;
    p.codItem = opt.codItem;
    p.cod = opt.cod;
    liveQueue.push_back(std::move(p));
    return true;
}

void PlayEmote(Player* bot, uint32 emote, Player* target)
{
    WorldPacket data(CMSG_TEXT_EMOTE, 4 + 4 + 8);
    data << emote;
    data << EmoteAction::GetNumberOfEmoteVariants(static_cast<TextEmotes>(emote), bot->getRace(), bot->getGender());
    data << (target ? target->GetGUID() : ObjectGuid::Empty);
    bot->GetSession()->HandleTextEmoteOpcode(data);
}

// World thread: plays the lines the filter takes out of the queue.
template <class Due>
void Play(Due isDue)
{
    std::vector<Pending> due;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        if (liveQueue.empty())
            return;
        auto split = std::partition(liveQueue.begin(), liveQueue.end(), [&](Pending const& p) { return !isDue(p); });
        due.assign(std::make_move_iterator(split), std::make_move_iterator(liveQueue.end()));
        liveQueue.erase(split, liveQueue.end());
    }

    for (Pending const& p : due)
    {
        Player* bot = ObjectAccessor::FindPlayer(p.bot);
        if (!bot || !bot->GetSession() || (p.channel == CH_NEAR && (!bot->IsInWorld() || !bot->IsAlive())))
            continue;
        PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
        Player* subject = p.subject ? ObjectAccessor::FindPlayer(p.subject) : nullptr;
        // The player went away, or went too far for an emote at them.
        if (p.subject && !subject && (p.channel == CH_WHISPER || p.invite))
            continue;
        Player* emoteTarget = subject && subject->IsInWorld() && subject->GetMapId() == bot->GetMapId() &&
                                      bot->IsWithinDist(subject, HEAR_RANGE)
                                  ? subject
                                  : nullptr;

        if (p.emote && p.channel == CH_NEAR)
            PlayEmote(bot, p.emote, emoteTarget);

        if (!p.text.empty())
        {
            switch (p.channel)
            {
                case CH_NEAR:
                    bot->Say(p.text, LANG_UNIVERSAL);
                    break;
                case CH_PARTY:
                    if (Group* group = bot->GetGroup())
                    {
                        WorldPacket data;
                        ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, LANG_UNIVERSAL, bot, nullptr, p.text);
                        group->BroadcastPacket(&data, false, group->GetMemberGroup(bot->GetGUID()));
                    }
                    break;
                case CH_WHISPER:
                    if (subject)
                        bot->Whisper(p.text, LANG_UNIVERSAL, subject);
                    break;
                case CH_GENERAL:
                    if (botAI)
                        botAI->SayToChannelNow(p.text, ChatChannelId::GENERAL);
                    break;
                case CH_TRADE:
                    if (botAI)
                        botAI->SayToChannelNow(p.text, ChatChannelId::TRADE);
                    break;
                case CH_LFG:
                    if (botAI)
                        botAI->SayToChannelNow(p.text, ChatChannelId::LOOKING_FOR_GROUP);
                    break;
                case CH_GUILD:
                    if (botAI)
                        botAI->SayToGuild(p.text);
                    break;
                default:
                    break;
            }
        }

        // "Sent you a COD": the item leaves the bot's bags by mail, cash on delivery at the price said, with the
        // delivery delay a player's mail to another account gets.
        if (p.codItem && p.cod && subject)
        {
            Item* item = bot->GetItemByGuid(p.codItem);
            if (item && !item->IsSoulBound() && !item->IsInTrade() && item->GetTemplate())
            {
                CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
                bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
                item->DeleteFromInventoryDB(trans);
                item->SetOwnerGUID(subject->GetGUID());
                item->SaveToDB(trans);
                MailDraft(item->GetTemplate()->Name1, "")
                    .AddItem(item)
                    .AddCOD(p.cod)
                    .SendMailTo(trans, MailReceiver(subject), MailSender(bot), MAIL_CHECK_MASK_COPIED,
                                sWorld->getIntConfig(CONFIG_MAIL_DELIVERY_DELAY));
                bot->SaveInventoryAndGoldToDB(trans);
                CharacterDatabase.CommitTransaction(trans);
            }
        }

        // "Sure, inviting": the real invite, as the client sends it (world thread opcode).
        if (p.invite && subject && !bot->GetGroup() && !subject->GetGroup())
        {
            WorldPacket invite;
            invite << subject->GetName();
            invite << uint32(0);
            bot->GetSession()->HandleGroupInviteOpcode(invite);
        }
    }
}

// The bot looks around: a real player to greet, or an inn or a capital to idle in.
void LookAround(Player* bot, PlayerbotAI* botAI)
{
    if (!IsRandom(bot) || !bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight() || bot->InBattleground() ||
        bot->InArena() || botAI->HasGameClientMaster())
        return;

    std::list<Player*> const nearby = RealPlayersNear(bot, HEAR_RANGE);
    if (nearby.empty())
        return;

    float const greetRange = static_cast<float>(sPlayerbotAIConfig.coaLiveGreetRange);
    for (Player* player : nearby)
    {
        if (player->GetTeamId() != bot->GetTeamId() || !bot->IsWithinDist(player, greetRange))
            continue;
        // Only one bot greets a player at a time (the scope is the player).
        ReactOptions opt;
        opt.perSubjectCdS = GREET_PLAYER_CD_S;
        if (React(bot, "greet_pass", player, "p" + std::to_string(player->GetGUID().GetCounter()), opt))
            return;
    }

    if (bot->isMoving())
        return;
    std::string const place = "z" + std::to_string(bot->GetZoneId());
    if (bot->HasRestFlag(REST_FLAG_IN_TAVERN))
        React(bot, "inn_idle", nullptr, place + "a" + std::to_string(bot->GetAreaId()));
    else if (InCapital(bot))
        React(bot, "city_idle", nullptr, place);
}

// ---------------------------------------------------------------- channels (brick 3b)

constexpr uint32 CHANNEL_TICK_MS = 20000;  // the channel timers look at the world this often
uint64 nextChannelTick = 0;

// The price of one item: the market's usual price (CoA Economy), else the lowest buyout per unit on the bot's auction
// house, else the vendor price x 3 (spec).
uint64 PriceOf(Player* bot, ItemTemplate const* proto)
{
    // The usual price the market paid, learned from real sales by CoA Economy (when it is on and knows the item).
    if (CoaEconomy::MarketPrices* market = CoaEconomy::MarketPrices::instance(); market && market->IsEnabled())
        if (CoaEconomy::MarketBand const* band = market->GetBand(proto->ItemId); band && band->Usual)
            return band->Usual;

    uint64 best = 0;
    if (AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(bot->GetFaction()))
            for (auto const& [id, auction] : house->GetAuctions())
                if (auction && auction->item_template == proto->ItemId && auction->buyout && auction->itemCount)
                {
                    uint64 const unit = auction->buyout / auction->itemCount;
                    if (!best || unit < best)
                        best = unit;
                }
    return best ? best : uint64(proto->SellPrice) * 3;
}

// Something the bot really carries and could sell: tradable, not a quest item, worth something, green or better
// (any trade good).
bool WtsValues(Player* bot, std::map<std::string, std::string>& extra, Offer& offer)
{
    std::vector<Item*> items;
    auto consider = [&](Item* item)
    {
        if (!item || item->IsSoulBound() || item->IsInTrade())
            return;
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || proto->Bonding == BIND_QUEST_ITEM || proto->Class == ITEM_CLASS_QUEST || !proto->SellPrice)
            return;
        if (proto->Quality >= ITEM_QUALITY_UNCOMMON || proto->Class == ITEM_CLASS_TRADE_GOODS)
            items.push_back(item);
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* b = bot->GetBagByPos(bag))
            for (uint32 slot = 0; slot < b->GetBagSize(); ++slot)
                consider(b->GetItemByPos(slot));
    if (items.empty())
        return false;
    Item* item = items[urand(0, items.size() - 1)];
    ItemTemplate const* proto = item->GetTemplate();
    uint64 const unit = PriceOf(bot, proto);
    uint64 const total = std::min<uint64>(unit * item->GetCount(), MAX_MONEY_AMOUNT);
    extra["item"] = ChatHelper::FormatItem(proto, item->GetCount() > 1 ? item->GetCount() : 0);
    extra["price"] = FormatMoney(item->GetCount() > 1 ? total : unit);
    offer = {true, item->GetGUID(), proto->ItemId, static_cast<uint32>(total), 0};
    return true;
}

// Something on the bot's auction house that a bot of its level would want: a real listing, its real price.
bool WtbValues(Player* bot, std::map<std::string, std::string>& extra, Offer& offer)
{
    AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(bot->GetFaction());
    if (!house || house->GetAuctions().empty())
        return false;
    std::vector<AuctionEntry*> fit;
    for (auto const& [id, auction] : house->GetAuctions())
    {
        if (!auction || !auction->buyout || !auction->itemCount)
            continue;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(auction->item_template);
        if (proto && proto->RequiredLevel <= bot->GetLevel() + 5 && proto->Quality >= ITEM_QUALITY_NORMAL &&
            proto->Quality <= ITEM_QUALITY_EPIC)
            fit.push_back(auction);
        if (fit.size() >= 200)
            break;
    }
    if (fit.empty())
        return false;
    AuctionEntry* auction = fit[urand(0, fit.size() - 1)];
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(auction->item_template);
    extra["item"] = ChatHelper::FormatItem(proto);
    extra["price"] = FormatMoney(auction->buyout / auction->itemCount);
    offer = {false, ObjectGuid::Empty, proto->ItemId, auction->buyout / auction->itemCount, 0};
    return true;
}

// A real dungeon of the bot's level for its faction, and what a group built around the bot still needs.
bool LfgValues(Player* bot, uint8 locale, std::map<std::string, std::string>& extra)
{
    std::vector<LFGDungeonEntry const*> fit;
    for (uint32 id : sRandomPlayerbotMgr.LfgDungeons[bot->GetTeamId()])
        if (LFGDungeonEntry const* d = sLFGDungeonStore.LookupEntry(id))
            if (d->TypeID == lfg::LFG_TYPE_DUNGEON && d->Difficulty == 0 && bot->GetLevel() >= d->MinLevel &&
                bot->GetLevel() <= d->MaxLevel)
                fit.push_back(d);
    if (fit.empty())
        return false;
    LFGDungeonEntry const* dungeon = fit[urand(0, fit.size() - 1)];
    char const* name = dungeon->Name[locale];
    if (!name || !*name)
        name = dungeon->Name[LOCALE_enUS];
    if (!name || !*name)
        return false;
    extra["dungeon"] = name;

    // 1 tank, 1 heal, 3 dps, less the group as it is (the bot alone when it has none).
    int tanks = 1, heals = 1, dps = 3;
    auto count = [&](Player* member)
    {
        if (PlayerbotAI::IsTank(member))
            --tanks;
        else if (PlayerbotAI::IsHeal(member))
            --heals;
        else
            --dps;
    };
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                count(member);
    }
    else
        count(bot);
    std::string need;
    auto add = [&need](int n, char const* role)
    {
        if (n > 0)
            need += (need.empty() ? "" : " ") + std::to_string(n) + " " + role;
    };
    add(tanks, "tank");
    add(heals, "heal");
    add(dps, "dps");
    if (need.empty())
        return false;
    extra["need"] = "need " + need;
    return true;
}

bool FreeRandomBot(Player* p)
{
    if (!p || !p->IsInWorld() || !p->IsAlive() || p->IsInCombat() || !IsRandom(p) || p->InBattleground() ||
        p->InArena() || p->GetMap()->Instanceable())
        return false;
    PlayerbotAI* ai = GET_PLAYERBOT_AI(p);
    return ai && !ai->HasGameClientMaster();
}

// World thread, every CHANNEL_TICK_MS: in each zone with real players, one free random bot may talk in General,
// in Trade (capitals), in LookingForGroup; in each guild with real players online, one bot may talk in the guild.
// The rules of the bank pace them; the population factor keeps an empty channel calm.
void ChannelTimers(uint64 nowMs)
{
    if (nowMs < nextChannelTick)
        return;
    nextChannelTick = nowMs + CHANNEL_TICK_MS;

    std::unordered_map<uint32, uint32> realByZone, realByGuild;
    std::unordered_map<uint32, std::vector<Player*>> botsByZone, botsByGuild;
    std::shared_lock<std::shared_mutex> playersLock(*HashMapHolder<Player>::GetLock());
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
    {
        if (!player || !player->IsInWorld())
            continue;
        if (IsRealPlayer(player))
        {
            ++realByZone[player->GetZoneId()];
            if (player->GetGuildId())
                ++realByGuild[player->GetGuildId()];
        }
        else if (FreeRandomBot(player))
        {
            botsByZone[player->GetZoneId()].push_back(player);
            if (player->GetGuildId())
                botsByGuild[player->GetGuildId()].push_back(player);
        }
    }

    uint8 const locale = BotLocale();
    auto pick = [](std::vector<Player*> const& v) { return v[urand(0, v.size() - 1)]; };
    for (auto const& [zone, real] : realByZone)
    {
        auto it = botsByZone.find(zone);
        if (it == botsByZone.end() || it->second.empty())
            continue;
        ReactOptions opt;
        opt.populationFactor = PopulationFactor(real);
        std::string const scope = "z" + std::to_string(zone);

        React(pick(it->second), "general_chat", nullptr, scope, opt);

        Player* bot = pick(it->second);
        if (InCapital(bot))
        {
            ReactOptions trade = opt;
            Offer offer;
            uint32 const roll = urand(0, 2);
            bool offered = false;
            if (roll == 0 && WtsValues(bot, trade.extra, offer))
                offered = React(bot, "trade_wts", nullptr, scope, trade);
            else if (roll == 1 && WtbValues(bot, trade.extra, offer))
                offered = React(bot, "trade_wtb", nullptr, scope, trade);
            else
                React(bot, "trade_chat", nullptr, scope, trade);
            if (offered)
            {
                offer.expiresMs = nowMs + OFFER_MS;
                std::lock_guard<std::mutex> guard(liveLock);
                offers[bot->GetGUID().GetRawValue()] = offer;
                if (offers.size() > 4096)
                    offers.clear();
            }
        }

        bot = pick(it->second);
        ReactOptions lfg = opt;
        if (LfgValues(bot, locale, lfg.extra))
            React(bot, urand(0, 1) ? "lfg_lfm" : "lfg_lfg", nullptr, "lfg" + std::to_string(bot->GetTeamId()), lfg);
    }

    for (auto const& [guildId, real] : realByGuild)
    {
        auto it = botsByGuild.find(guildId);
        if (it == botsByGuild.end() || it->second.empty())
            continue;
        ReactOptions opt;
        opt.populationFactor = PopulationFactor(real);
        React(pick(it->second), "guild_idle", nullptr, "g" + std::to_string(guildId), opt);
    }
}

// ---------------------------------------------------------------- guild events

uint32 RealOnlineInGuild(uint32 guildId, Player* except)
{
    uint32 n = 0;
    std::shared_lock<std::shared_mutex> playersLock(*HashMapHolder<Player>::GetLock());
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        if (player && player != except && player->IsInWorld() && player->GetGuildId() == guildId && IsRealPlayer(player))
            ++n;
    return n;
}

// Another random bot of the guild, online and free to type.
Player* GuildBot(uint32 guildId, Player* except)
{
    std::vector<Player*> bots;
    std::shared_lock<std::shared_mutex> playersLock(*HashMapHolder<Player>::GetLock());
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        if (player && player != except && player->GetGuildId() == guildId && FreeRandomBot(player))
            bots.push_back(player);
    return bots.empty() ? nullptr : bots[urand(0, bots.size() - 1)];
}

// Guild events seen on a map thread wait for the world thread, where the guild's members can be looked at.
struct GuildEvent
{
    std::string situation;  // guild_levelup, guild_loot, guild_self_loot
    uint32 guildId = 0;
    ObjectGuid who;
    uint32 itemId = 0;
};
std::vector<GuildEvent> guildEvents;

void NoteGuildEvent(GuildEvent e)
{
    std::lock_guard<std::mutex> guard(liveLock);
    if (guildEvents.size() < 256)
        guildEvents.push_back(std::move(e));
}

void PlayGuildEvents()
{
    std::vector<GuildEvent> events;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        events.swap(guildEvents);
    }
    for (GuildEvent const& e : events)
    {
        Player* who = ObjectAccessor::FindPlayer(e.who);
        if (!who || !RealOnlineInGuild(e.guildId, nullptr))
            continue;
        ReactOptions opt;
        if (ItemTemplate const* proto = e.itemId ? sObjectMgr->GetItemTemplate(e.itemId) : nullptr)
            opt.extra["item"] = ChatHelper::FormatItem(proto);
        std::string const scope = "g" + std::to_string(e.guildId);
        if (e.situation == "guild_self_loot")
            React(who, e.situation, nullptr, scope, opt);
        else if (Player* bot = GuildBot(e.guildId, who))
            React(bot, e.situation, who, scope, opt);
    }
}

void Load()
{
    auto loaded = std::make_shared<Bank>();
    if (QueryResult r = PlayerbotsDatabase.Query(
            "SELECT situation, chance_pct, delay_min_ms, delay_max_ms, bot_cd_s, scope_cd_s, scope_per_10min, "
            "need_player FROM playerbots_coa_live_rules"))
        do
        {
            Field* f = r->Fetch();
            Rule rule;
            rule.chancePct = f[1].Get<uint8>();
            rule.delayMinMs = f[2].Get<uint16>();
            rule.delayMaxMs = f[3].Get<uint16>();
            rule.botCdS = f[4].Get<uint16>();
            rule.scopeCdS = f[5].Get<uint16>();
            rule.scopePer10Min = f[6].Get<uint8>();
            rule.needPlayer = f[7].Get<uint8>() != 0;
            loaded->rules[f[0].Get<std::string>()] = rule;
        } while (r->NextRow());

    if (QueryResult r = PlayerbotsDatabase.Query(
            "SELECT id, situation, channel, locale, level_band, emote, text, w_lazy, w_quester, w_wanderer, w_trader, "
            "w_pvp, rare FROM playerbots_coa_live_lines"))
        do
        {
            Field* f = r->Fetch();
            Line line;
            line.id = f[0].Get<uint32>();
            line.situation = f[1].Get<std::string>();
            line.channel = f[2].Get<uint8>();
            line.locale = f[3].Get<uint8>();
            line.levelBand = f[4].Get<uint8>();
            line.emote = f[5].Get<uint16>();
            line.text = f[6].Get<std::string>();
            for (uint8 i = 0; i < ARCH_COUNT; ++i)
                line.weight[i] = f[7 + i].Get<uint8>();
            line.rare = f[12].Get<uint8>() != 0;
            loaded->Add(std::move(line));
        } while (r->NextRow());

    if (QueryResult r = PlayerbotsDatabase.Query("SELECT locale, priority, intent, keywords FROM playerbots_coa_live_intents"))
        do
        {
            Field* f = r->Fetch();
            loaded->AddIntent(f[0].Get<uint8>(), f[1].Get<uint8>(), f[2].Get<std::string>(), f[3].Get<std::string>());
        } while (r->NextRow());

    if (QueryResult r = PlayerbotsDatabase.Query("SELECT locale, full, short FROM playerbots_coa_live_slang"))
        do
        {
            Field* f = r->Fetch();
            loaded->AddSlang(f[0].Get<uint8>(), f[1].Get<std::string>(), f[2].Get<std::string>());
        } while (r->NextRow());

    LOG_INFO("server.loading", ">> CoA Live: {} lines, {} situations{}", loaded->LineCount(), loaded->rules.size(),
             sPlayerbotAIConfig.coaLiveEnable ? "" : " (off: AiPlayerbot.CoaLive.Enable = 0)");

    std::lock_guard<std::mutex> guard(liveLock);
    // Each new version of the bank deletes and inserts its lines again: their ids change, the rings start over.
    bank = loaded->LineCount() ? std::shared_ptr<Bank const>(std::move(loaded)) : nullptr;
    botRings.clear();
    channelRings.clear();
}

class CoaLivePlayerScript : public PlayerScript
{
public:
    CoaLivePlayerScript()
        : PlayerScript("CoaLivePlayerScript",
                       {PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_DUEL_END, PLAYERHOOK_ON_PLAYER_ENTER_COMBAT,
                        PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_LOOT_ITEM})
    {
    }

    // A bot dings in front of a real player; a real player dings next to random bots or in their group.
    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (!Enabled() || !player || player->GetLevel() <= oldLevel)
            return;

        std::string const scope = "l" + std::to_string(player->GetGUID().GetCounter());
        if (uint32 guildId = player->GetGuildId())
            NoteGuildEvent({"guild_levelup", guildId, player->GetGUID(), 0});
        if (IsRandom(player))
        {
            if (!RealPlayersNear(player, HEAR_RANGE).empty())
                React(player, "levelup_self", nullptr, scope);
            return;
        }
        if (!IsRealPlayer(player))
            return;

        std::list<Player*> bots;
        Acore::AnyPlayerInObjectRangeCheck check(player, HEAR_RANGE, true, true);
        Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(player, bots, check);
        Cell::VisitObjects(player, searcher, HEAR_RANGE);
        for (Player* bot : bots)
            if (bot != player && IsRandom(bot) && !bot->IsInCombat() &&
                React(bot, "levelup_seen", player, scope))
                return;  // one congratulation is enough; the pace of the scope keeps the others quiet
    }

    void OnPlayerDuelEnd(Player* winner, Player* loser, DuelCompleteType type) override
    {
        if (!Enabled() || type != DUEL_WON || !winner || !loser)
            return;
        // Someone real must see it: one of the duellists, or a player watching.
        bool const watched = IsRealPlayer(winner) || IsRealPlayer(loser) || !RealPlayersNear(winner, HEAR_RANGE).empty();
        if (!watched)
            return;
        std::string const scope = "d" + std::to_string(winner->GetGUID().GetCounter());
        if (IsRandom(winner))
            React(winner, "duel_won", loser, scope + "w");
        if (IsRandom(loser))
            React(loser, "duel_lost", winner, scope + "l");
    }

    // World thread. A real player logs in: a bot of the guild says hello. A random bot logs in in front of the
    // guild's real players: it says hello itself.
    void OnPlayerLogin(Player* player) override
    {
        if (!Enabled() || !player || !player->GetGuildId())
            return;
        uint32 const guildId = player->GetGuildId();
        std::string const scope = "g" + std::to_string(guildId);
        if (IsRealPlayer(player))
        {
            if (Player* bot = GuildBot(guildId, player))
                React(bot, "guild_login", player, scope);
        }
        else if (IsRandom(player) && RealOnlineInGuild(guildId, nullptr))
        {
            // Logged in a moment ago: the line waits for the bot to be in the world (React needs it there).
            ReactOptions opt;
            React(player, "guild_self_login", nullptr, scope, opt);
        }
    }

    // World thread, the bot still in the world: its goodbye is said at once, it will not be there later.
    void OnPlayerLogout(Player* player) override
    {
        if (!Enabled() || !player || !player->GetGuildId() || !IsRandom(player) ||
            !RealOnlineInGuild(player->GetGuildId(), nullptr))
            return;
        ReactOptions opt;
        opt.now = true;
        ObjectGuid const guid = player->GetGUID();
        if (React(player, "guild_logout", nullptr, "g" + std::to_string(player->GetGuildId()), opt))
            Play([guid](Pending const& p) { return p.bot == guid && p.dueMs == 0; });
    }

    // A guild member loots a rare item or better.
    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/) override
    {
        if (!Enabled() || !player || !item || !player->GetGuildId() || !item->GetTemplate() ||
            item->GetTemplate()->Quality < ITEM_QUALITY_RARE)
            return;
        bool const self = IsRandom(player);
        if (!self && !IsRealPlayer(player))
            return;
        NoteGuildEvent({self ? "guild_self_loot" : "guild_loot", player->GetGuildId(), player->GetGUID(),
                        item->GetEntry()});
    }

    // A bot pulls a boss in a group with a real player.
    void OnPlayerEnterCombat(Player* player, Unit* enemy) override
    {
        if (!Enabled() || !player || !enemy || !IsRandom(player))
            return;
        Creature* creature = enemy->ToCreature();
        if (!creature || !(creature->IsDungeonBoss() || creature->isWorldBoss()) || !GroupHasRealPlayer(player))
            return;
        ReactOptions opt;
        opt.boss = creature;
        React(player, "boss_pull", nullptr, "g" + std::to_string(player->GetGroup()->GetGUID().GetCounter()), opt);
    }
};

class CoaLiveWorldScript : public WorldScript
{
public:
    CoaLiveWorldScript() : WorldScript("CoaLiveWorldScript", {WORLDHOOK_ON_UPDATE}) {}

    void OnUpdate(uint32 /*diff*/) override
    {
        if (Enabled())
        {
            uint64 const nowMs = GameTime::GetGameTimeMS().count();
            Play([nowMs](Pending const& p) { return p.dueMs <= nowMs; });
            PlayGuildEvents();
            ChannelTimers(nowMs);
        }
    }
};
}  // namespace

namespace CoaLive
{
void LoadBank() { Load(); }

void OnBotUpdate(Player* bot, PlayerbotAI* botAI, uint32 diff)
{
    if (!Enabled() || !bot || !botAI || diff == 0)
        return;
    // About every SCAN_MS per bot, spread over the bots, with no state: the bot's turn comes when the clock,
    // shifted by its guid, crosses a multiple of SCAN_MS during this update.
    uint64 const shifted = GameTime::GetGameTimeMS().count() + bot->GetGUID().GetCounter() * 977u;
    if (shifted % SCAN_MS >= std::min<uint64>(diff, SCAN_MS))
        return;
    LookAround(bot, botAI);
}

bool AnswersWhispers(PlayerbotAI* botAI, ObjectGuid from)
{
    // Off: decided before anything is looked up, so the old path is untouched.
    if (!Enabled() || !botAI)
        return false;
    Player* master = botAI->GetMaster();
    return TakesWhisper(true, IsRealPlayer(ObjectAccessor::FindConnectedPlayer(from)), IsRandom(botAI->GetBot()),
                        master && master->GetGUID() == from);
}

bool OnWhisper(Player* from, Player* to, std::string const& msg)
{
    if (!Enabled() || !from || !to)
        return false;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(to);
    if (!botAI)
        return false;
    Player* master = botAI->GetMaster();
    if (!TakesWhisper(Enabled(), IsRealPlayer(from), IsRandom(to), master && master->GetGUID() == from->GetGUID()))
        return false;

    std::shared_ptr<Bank const> current;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        current = bank;
    }
    if (!current)
        return false;

    // Lower case with the core (UTF-8: "OÙ" finds "où"), then the keywords of the bot's language.
    std::wstring wide;
    std::string lowered = msg;
    if (Utf8toWStr(msg, wide))
    {
        wstrToLower(wide);
        WStrToUtf8(wide, lowered);
    }
    std::string const intent = Classify(NormalizeWhisper(lowered), current->IntentsOf(BotLocale()));

    // An answer to the bot's own Trade announce: the deal is done for real (WTS: the item by mail, cash on
    // delivery at the price said), or politely turned down (the item is gone; a WTB: "found one already").
    if (intent != "bot" && intent != "hello" && intent != "thanks")
    {
        Offer offer;
        bool live = false;
        {
            std::lock_guard<std::mutex> guard(liveLock);
            auto it = offers.find(to->GetGUID().GetRawValue());
            if (it != offers.end() && it->second.expiresMs > GameTime::GetGameTimeMS().count())
            {
                offer = it->second;
                live = true;
            }
        }
        if (live)
        {
            ReactOptions opt;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(offer.itemId);
            if (proto)
                opt.extra["item"] = ChatHelper::FormatItem(proto);
            opt.extra["price"] = FormatMoney(offer.price);
            std::string const scope = "w" + std::to_string(to->GetGUID().GetCounter());
            Item* item = offer.wts ? to->GetItemByGuid(offer.item) : nullptr;
            if (offer.wts && item && !item->IsSoulBound() && from->GetTeamId() == to->GetTeamId())
            {
                opt.codItem = offer.item;
                opt.cod = offer.price;
                if (React(to, "wh_offer_cod", from, scope, opt))
                    return ForgetOffer(to), true;
            }
            else if (React(to, "wh_offer_gone", from, scope, opt))
                return ForgetOffer(to), true;
        }
    }

    std::string situation = "wh_" + intent;
    bool invite = false;
    if (intent == "group")
    {
        // Yes when the bot is free: no group, not in an instance or a fight, level within 5 of the player's.
        bool const free = !to->GetGroup() && !from->GetGroup() && !to->GetMap()->Instanceable() && !to->IsInCombat() &&
                          std::abs(int32(to->GetLevel()) - int32(from->GetLevel())) <= 5;
        situation = free ? "wh_group_yes" : "wh_group_no";
        invite = free;
    }
    else if (intent == "where")
        situation = "wh_where_unknown";  // %place is not looked up yet

    ReactOptions opt;
    opt.invite = invite;
    React(to, situation, from, "w" + std::to_string(to->GetGUID().GetCounter()), opt);
    return true;
}

bool SessionsEnabled() { return sPlayerbotAIConfig.coaLiveSessionsEnable; }

namespace
{
uint32 HourPercentNow()
{
    time_t const now = GameTime::GetGameTime().count() + time_t(sPlayerbotAIConfig.coaLiveSessionsHourShift) * HOUR;
    tm const local = Acore::Time::TimeBreakdown(now);
    CoaSessions::Curve curve = CoaSessions::DefaultCurve();
    if (sPlayerbotAIConfig.coaLiveSessionsCurve.size() == curve.size())
        std::copy(sPlayerbotAIConfig.coaLiveSessionsCurve.begin(), sPlayerbotAIConfig.coaLiveSessionsCurve.end(),
                  curve.begin());
    return CoaSessions::HourPercent(curve, local.tm_hour, local.tm_min, local.tm_wday,
                                    sPlayerbotAIConfig.coaLiveSessionsWeekendBonus,
                                    sPlayerbotAIConfig.coaLiveSessionsMinPercent);
}
}  // namespace

uint32 SessionTarget(uint32 count)
{
    return SessionsEnabled() ? CoaSessions::Target(count, HourPercentNow()) : count;
}

uint32 SessionSeconds() { return SessionsEnabled() ? CoaSessions::SessionSeconds(rng.between) : 0; }

uint32 OfflineSeconds() { return SessionsEnabled() ? CoaSessions::OfflineSeconds(HourPercentNow(), rng.between) : 0; }

bool SayLine(Player* bot, std::string const& situation, std::unordered_map<std::string, std::string> const& slots)
{
    if (!Enabled() || !bot)
        return false;
    ReactOptions opt;
    opt.extra.insert(slots.begin(), slots.end());
    // Paced per place: the bot's zone.
    return React(bot, situation, nullptr, situation + "@z" + std::to_string(bot->GetZoneId()), opt);
}

std::string SystemLine(Player* reader, std::string const& situation,
                       std::unordered_map<std::string, std::string> const& slots)
{
    if (!Enabled())
        return "";
    std::shared_ptr<Bank const> current;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        current = bank;
    }
    if (!current)
        return "";
    uint8 locale = LOCALE_enUS;
    if (reader && reader->GetSession())
        locale = static_cast<uint8>(reader->GetSession()->GetSessionDbLocaleIndex());
    std::map<std::string, std::string> const values(slots.begin(), slots.end());
    Pick const pick = Draw(*current, situation, locale, reader ? LevelBand(reader->GetLevel()) : 0, ARCH_NONE,
                           rng.Roll(sPlayerbotAIConfig.coaLiveRareChance), nullptr, nullptr, values, rng);
    return pick.line ? pick.text : "";
}

void OnResurrectAccepted(Player* bot, ObjectGuid by)
{
    if (!Enabled() || !IsRandom(bot))
        return;
    Player* caster = ObjectAccessor::FindPlayer(by);
    if (caster && IsRealPlayer(caster))
        React(bot, "rez_received", caster, "r" + std::to_string(bot->GetGUID().GetCounter()));
}
}  // namespace CoaLive

void AddSC_coa_live()
{
    new CoaLivePlayerScript();
    new CoaLiveWorldScript();
}
