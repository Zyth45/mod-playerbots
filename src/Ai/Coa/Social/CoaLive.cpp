/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaLive.h"

#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "CellImpl.h"
#include "Channel.h"
#include "ChannelMgr.h"
#include "ChatHelper.h"
#include "Chat.h"
#include "AttackAction.h"
#include "CoaFreshStartRules.h"
#include "CoaCatchUp.h"
#include "CoaImmersionRules.h"
#include "CoaCleanWorld.h"
#include "CoaLiveRules.h"
#include "CoaGroupEmoteRules.h"
#include "CoaChanAskRules.h"
#include "CoaGearScore.h"
#include "CoaChatRules.h"
#include "GameObject.h"
#include "LootObjectStack.h"
#include "CoaSheathRules.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "CoaSoul.h"
#include "StringFormat.h"
#include "CoaPet.h"
#include "CoaWorldFile.h"
#include "FishingAction.h"
#include "NewRpgInfo.h"
#include "CoaSocial.h"
#include "CoaSocialRules.h"
#include "SpellAuras.h"
#include "CoaSessionRules.h"
#include "Timer.h"
#include "CoaMarketPrices.h"
#include "Creature.h"
#include "DBCStores.h"
#include "CoaDbCheck.h"
#include "CoaSellable.h"
#include "CoaSurnameRules.h"
#include "CharacterCache.h"
#include "ChatCommand.h"
#include "PlayerbotFactory.h"
#include "CoaSoulAddonRules.h"
#include "CoaSpecialization.h"
#include "AscensionSpecialization.h"
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
#include "MapMgr.h"
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

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <tuple>
#include <unordered_set>
#include <vector>

using namespace CoaLive;

namespace
{
constexpr uint32 SCAN_MS = 5000;        // how often a bot looks around
constexpr float HEAR_RANGE = 30.0f;      // a real player this close hears the bot's /say
constexpr uint32 GREET_PLAYER_CD_S = 3600;  // a bot greets the same player once an hour
constexpr size_t QUEUE_MAX = 4096;

std::mutex liveLock;

// The zones where a real player is, refreshed by the world thread every second: a bot's look (about every 5 s)
// stops at once in any other zone, so 2 000 bots cost a set lookup each, not a grid search.
std::shared_ptr<std::unordered_set<uint32> const> peopledZones = std::make_shared<std::unordered_set<uint32>>();
uint64 nextZonesRefresh = 0;

bool ZonePeopled(uint32 zoneId)
{
    std::shared_ptr<std::unordered_set<uint32> const> zones;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        zones = peopledZones;
    }
    return zones->count(zoneId) != 0;
}

void RefreshPeopledZones(uint64 nowMs)
{
    if (nowMs < nextZonesRefresh)
        return;
    nextZonesRefresh = nowMs + 1000;
    auto zones = std::make_shared<std::unordered_set<uint32>>();
    {
        std::shared_lock<std::shared_mutex> playersLock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            if (player && player->IsInWorld() && IsRealPlayer(player))
                zones->insert(player->GetZoneId());
    }
    std::lock_guard<std::mutex> guard(liveLock);
    peopledZones = std::move(zones);
}

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
    std::string sayChannel;  // said in this public channel (its name) instead of the line's channel
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
    // %class: the speaker's own class, in the bots' language (the night bank's class lines).
    if (ChrClassesEntry const* cls = sChrClassesStore.LookupEntry(bot->getClass()))
        if (char const* name = cls->name[locale < TOTAL_LOCALES ? locale : 0]; name && *name)
            v["class"] = name;
        else if (cls->name[0] && *cls->name[0])
            v["class"] = cls->name[0];
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
    bool force = false;                            // the Trade pace: no roll, no scope limit (bot cooldown kept)
    ObjectGuid codItem;                            // mail this item to the subject, cash on delivery
    uint32 cod = 0;
    std::string sayChannel;                        // the public channel to answer in (its name), not the line's
    uint64 dueAtMs = 0;                            // said at this time (game ms) instead of after its typing
    std::string avoidLead;                         // draw again a line naming this first (a second class answer)
    struct Said { uint64 dueMs = 0; std::string lead; } * said = nullptr;  // filled when the line is queued
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
    Archetype soulArch = ARCH_NONE;
    int32 sociability = -1;
    if (CoaSoul::Enabled() && IsRandom(bot))
    {
        CoaSoul::Soul const soul = CoaSoul::Get(bot);
        soulArch = Archetype(soul.archetype);
        sociability = soul.sociability;
    }
    if (!rule || (!opt.force && !rng.Roll(ReactPct(*rule, sociability, opt.populationFactor))))
        return false;
    Rule forced;
    if (opt.force)
    {
        forced = *rule;
        forced.scopeCdS = 0;
        forced.scopePer10Min = 0;
        rule = &forced;
    }

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
    uint32 const ringSize = channel == CH_TRADE && sPlayerbotAIConfig.coaLiveTradeMessagesPerMinute
                                ? std::max<uint32>(sPlayerbotAIConfig.coaLiveNoRepeatChannel, 30)
                                : sPlayerbotAIConfig.coaLiveNoRepeatChannel;
    Ring& channelRing = channelRings.try_emplace(channelScope, ringSize).first->second;
    bool const rareRoll = rng.Roll(sPlayerbotAIConfig.coaLiveRareChance);
    Who const who{bot->getRace(), bot->getClass(), bot->GetZoneId()};
    Pick pick = Draw(*current, situation, locale, LevelBand(bot->GetLevel()), soulArch, rareRoll, &botRing,
                     &channelRing, values, rng, who);
    for (uint32 again = 0; again < 6 && pick.line && !opt.avoidLead.empty() &&
                           CoaChanAsk::Lead(pick.line->text) == opt.avoidLead; ++again)
        pick = Draw(*current, situation, locale, LevelBand(bot->GetLevel()), soulArch, rareRoll, &botRing,
                    &channelRing, values, rng, who);
    if (pick.line && !opt.avoidLead.empty() && CoaChanAsk::Lead(pick.line->text) == opt.avoidLead)
        return false;
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
    if (opt.dueAtMs)
        p.dueMs = opt.dueAtMs;
    if (opt.said)
    {
        opt.said->dueMs = p.dueMs;
        opt.said->lead = CoaChanAsk::Lead(pick.line->text);
    }
    p.bot = bot->GetGUID();
    p.subject = subject ? subject->GetGUID() : ObjectGuid::Empty;
    p.channel = channel;
    p.emote = pick.line->emote;
    p.text = text;
    p.invite = invite;
    p.codItem = opt.codItem;
    p.cod = opt.cod;
    p.sayChannel = opt.sayChannel;
    liveQueue.push_back(std::move(p));
    return true;
}

void PlayEmote(Player* bot, uint32 emote, Player* target)
{
    if (!CoaLive::GroupEmoteAllowed(bot))
        return;
    WorldPacket data(CMSG_TEXT_EMOTE, 4 + 4 + 8);
    data << emote;
    data << EmoteAction::GetNumberOfEmoteVariants(static_cast<TextEmotes>(emote), bot->getRace(), bot->getGender());
    data << (target ? target->GetGUID() : ObjectGuid::Empty);
    bot->GetSession()->HandleTextEmoteOpcode(data);
}

// World thread: an item of the bot's bags by mail to a player, cash on delivery, with no delay. Its subject is the
// item's name and its body is empty: no text to translate.
void SendCod(Player* bot, Player* to, Item* item, uint32 price)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
    item->DeleteFromInventoryDB(trans);
    item->SetOwnerGUID(to->GetGUID());
    item->SaveToDB(trans);
    MailDraft(item->GetTemplate()->Name1, "")
        .AddItem(item)
        .AddCOD(price)
        // No delivery delay: the bot's whispers already play the time it takes (MailDeliveryDelay, 1 h on some
        // realms, held a craft order's mail that long).
        .SendMailTo(trans, MailReceiver(to), MailSender(bot), MAIL_CHECK_MASK_COPIED, 0);
    bot->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
}

std::unordered_map<std::string, WhisperHandler> whisperHandlers;  // other sessions' intents (under liveLock)

// The Trade channel of the CoA client: ChatChannels.dbc id 4, named "Trade" (id 2 is "Newcomers" there, not the
// "Trade - City" of the stock client, so the stock Trade code never reached the players' /4). World thread.
constexpr uint32 COA_TRADE_CHANNEL_ID = 4;
constexpr uint32 COA_WORLD_CHANNEL_ID = 1;  // "Ascension", the world channel of the CoA client (night bank)
std::unordered_set<uint64> tradeJoined;  // bot raw guid -> joined Trade by CoA Live (world thread only)
std::unordered_set<uint64> worldJoined;  // the same for the world channel

bool SayInChannel(Player* bot, uint32 channelId, std::unordered_set<uint64>& joined, std::string const& text)
{
    if (!sPlayerbotAIConfig.coaLiveChatEnable)  // chat off (AiPlayerbot.CoaLive.Chat.Enable = 0)
        return false;
    ChannelMgr* cMgr = ChannelMgr::forTeam(bot->GetTeamId());
    if (!cMgr)
        return false;
    for (auto const& [key, channel] : cMgr->GetChannels())
    {
        if (!channel || channel->GetChannelId() != channelId || channel->GetName().empty())
            continue;
        uint64 const k = bot->GetGUID().GetRawValue();
        if (!joined.count(k))
        {
            channel->JoinChannel(bot, "");
            joined.insert(k);
            if (joined.size() > 20000)
                joined.clear();
        }
        channel->Say(bot->GetGUID(), text, LANG_UNIVERSAL);
        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))  // into Chat.log, as the other channels' lines
            botAI->LogChat("tells channel " + channel->GetName(), text);
        return true;
    }
    return false;  // no player has the channel open: nobody would read it
}

// Into the public channel of that name (the one a player asked in), joined first when the bot is not in it.
bool SayInNamedChannel(Player* bot, std::string const& name, std::string const& text)
{
    if (!sPlayerbotAIConfig.coaLiveChatEnable)
        return false;
    ChannelMgr* cMgr = ChannelMgr::forTeam(bot->GetTeamId());
    if (!cMgr)
        return false;
    for (auto const& [key, channel] : cMgr->GetChannels())
    {
        if (!channel || channel->GetName() != name)
            continue;
        static std::unordered_set<std::string> joined;  // world thread
        std::string const k = std::to_string(bot->GetGUID().GetRawValue()) + "|" + name;
        if (joined.insert(k).second)
            channel->JoinChannel(bot, "");
        if (joined.size() > 20000)
            joined.clear();
        channel->Say(bot->GetGUID(), text, LANG_UNIVERSAL);
        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
            botAI->LogChat("tells channel " + channel->GetName(), text);
        return true;
    }
    return false;
}

// The Trade pace (AiPlayerbot.CoaLive.TradeMessagesPerMinute): when each faction's Trade last heard a line, who
// said the last ones. World thread.
std::unordered_map<uint32, uint64> tradeLastMs;
std::unordered_map<uint32, uint64> tradeGapMs;
std::unordered_map<uint32, std::vector<uint64>> tradeSpeakers;

void NoteTrade(Player* speaker)
{
    if (!speaker)
        return;
    uint32 const team = speaker->GetTeamId();
    tradeLastMs[team] = GameTime::GetGameTimeMS().count();
    tradeGapMs[team] = TradeGapMs(sPlayerbotAIConfig.coaLiveTradeMessagesPerMinute, rng);
    std::vector<uint64>& last = tradeSpeakers[team];
    last.push_back(speaker->GetGUID().GetRawValue());
    if (last.size() > 5)
        last.erase(last.begin());
}

bool SayInTrade(Player* bot, std::string const& text)
{
    if (!SayInChannel(bot, COA_TRADE_CHANNEL_ID, tradeJoined, text))
        return false;
    NoteTrade(bot);
    return true;
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

        // Chat off (AiPlayerbot.CoaLive.Chat.Enable = 0): no text, the rest of the line still happens.
        CoaChat::Parts const parts = CoaChat::PartsOf(sPlayerbotAIConfig.coaLiveChatEnable);
        if (parts.emote && p.emote && p.channel == CH_NEAR)
            PlayEmote(bot, p.emote, emoteTarget);

        if (parts.text && !p.text.empty() && !p.sayChannel.empty())
            SayInNamedChannel(bot, p.sayChannel, p.text);
        else if (parts.text && !p.text.empty())
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
                    SayInTrade(bot, p.text);
                    break;
                case CH_WORLD:
                    SayInChannel(bot, COA_WORLD_CHANNEL_ID, worldJoined, p.text);
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
        if (parts.cod && p.codItem && p.cod && subject)
        {
            Item* item = bot->GetItemByGuid(p.codItem);
            if (item && !item->IsSoulBound() && !item->IsInTrade() && item->GetTemplate())
                SendCod(bot, subject, item, p.cod);
        }

        // "Sure, inviting": the real invite, as the client sends it (world thread opcode).
        if (parts.invite && p.invite && subject && !bot->GetGroup() && !subject->GetGroup())
        {
            WorldPacket invite;
            invite << subject->GetName();
            invite << uint32(0);
            bot->GetSession()->HandleGroupInviteOpcode(invite);
        }
    }
}


// ---------------------------------------------------------------- social bots (L2)

// The attack of the strategies, from outside them: the bot takes the enemy of the player it helps.
class CoaSocialHelp : public AttackAction
{
public:
    explicit CoaSocialHelp(PlayerbotAI* botAI) : AttackAction(botAI, "coa social help") {}
    bool Execute(Event /*event*/) override { return false; }
    bool Help(Unit* enemy) { return Attack(enemy); }
};

std::unordered_map<std::string, uint64> socialLast;  // "act@player" -> last time, any bot (under liveLock)

std::string SocialKey(CoaSocial::Act act, Player* p)
{
    return std::to_string(int(act)) + "@" + std::to_string(p->GetGUID().GetCounter());
}

bool SocialDue(CoaSocial::Act act, Player* p, uint64 nowS)
{
    std::lock_guard<std::mutex> guard(liveLock);
    auto it = socialLast.find(SocialKey(act, p));
    return it == socialLast.end() ||
           nowS >= it->second + CoaSocial::PlayerCooldownS(act, sPlayerbotAIConfig.coaLiveSocialInviteCooldownMinutes * 60);
}

void SocialDone(CoaSocial::Act act, Player* p, uint64 nowS)
{
    std::lock_guard<std::mutex> guard(liveLock);
    socialLast[SocialKey(act, p)] = nowS;
    if (socialLast.size() > 20000)
        socialLast.clear();
}

// A group invite with no line (the bank has none for it yet): the same delay a player takes to decide.
void QueueSilentInvite(Player* bot, Player* player)
{
    Pending p;
    p.dueMs = GameTime::GetGameTimeMS().count() + urand(2000, 6000);
    p.bot = bot->GetGUID();
    p.subject = player->GetGUID();
    p.channel = CH_WHISPER;
    p.invite = true;
    std::lock_guard<std::mutex> guard(liveLock);
    if (liveQueue.size() < QUEUE_MAX)
        liveQueue.push_back(std::move(p));
}

bool HasRule(std::string const& situation)
{
    std::lock_guard<std::mutex> guard(liveLock);
    return bank && bank->RuleOf(situation);
}

// A free random bot passing real players of its faction: one helping act per look at most, most needed first.
bool Social(Player* bot, PlayerbotAI* botAI, std::list<Player*> const& nearby)
{
    if (!sPlayerbotAIConfig.coaLiveSocialEnable)
        return false;

    using namespace CoaSocial;
    uint64 const nowS = GameTime::GetGameTime().count();
    BotView bv;
    bv.level = bot->GetLevel();
    bv.inGroup = bot->GetGroup() != nullptr;
    bv.inInstance = bot->GetMap()->Instanceable();
    bv.canHeal = PlayerbotAI::IsHeal(bot);

    for (Player* p : nearby)
    {
        PlayerView pv;
        pv.sameFaction = p->GetTeamId() == bot->GetTeamId();
        pv.alive = p->IsAlive();
        pv.ghost = p->HasPlayerFlag(PLAYER_FLAGS_GHOST);
        pv.resurrectAsked = p->isResurrectRequested();
        pv.inCombat = p->IsInCombat();
        pv.inGroup = p->GetGroup() != nullptr;
        pv.healthPct = static_cast<uint32>(p->GetHealthPct());
        {
            AreaTableEntry const* zone = sAreaTableStore.LookupEntry(p->GetZoneId());
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(p->GetAreaId());
            pv.inSettlement = CoaSocial::IsSettlement(zone ? zone->flags : 0, area ? area->flags : 0,
                                                      p->HasPlayerFlag(PLAYER_FLAGS_RESTING));
        }
        pv.attackers = static_cast<uint32>(p->getAttackers().size());
        pv.level = p->GetLevel();
        pv.distance = bot->GetDistance(p);

        Act act = Decide(bv, pv);
        if (act == Act::Invite && !SocialDue(act, p, nowS))
            act = Act::Buff;  // asked lately: a buff in passing instead
        if (act == Act::None || !SocialDue(act, p, nowS) || !rng.Roll(ChancePct(act)))
            continue;

        bool done = false;
        std::string situation;
        switch (act)
        {
            case Act::Rez:
                done = CoaCastOnAlly(botAI, p, CoaAllyCast::Resurrect);
                situation = "social_rez";
                break;
            case Act::Help:
                if (bv.canHeal && pv.healthPct < 60)
                    done = CoaCastOnAlly(botAI, p, CoaAllyCast::Heal);
                if (!done)
                    if (Unit* enemy = p->GetVictim(); enemy && enemy->IsAlive() && bot->IsValidAttackTarget(enemy))
                    {
                        botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Set({enemy->GetGUID()});
                        CoaSocialHelp attack(botAI);
                        done = attack.Help(enemy);
                    }
                situation = "social_help";
                break;
            case Act::Buff:
                done = CoaCastOnAlly(botAI, p, CoaAllyCast::Buff);
                situation = "social_buff";
                break;
            case Act::Invite:
            {
                ReactOptions opt;
                opt.invite = true;
                if (HasRule("social_invite"))
                    done = React(bot, "social_invite", p, "s" + std::to_string(p->GetGUID().GetCounter()), opt);
                else
                {
                    QueueSilentInvite(bot, p);
                    done = true;
                }
                break;
            }
            default:
                break;
        }
        if (!done)
            continue;
        SocialDone(act, p, nowS);
        if (act != Act::Invite)
            React(bot, situation, p, "s" + std::to_string(p->GetGUID().GetCounter()));  // a word, when the bank has one
        return true;
    }
    return false;
}


// ---------------------------------------------------------------- Immersion (L2): fishing on the shore

std::atomic<uint32> onlineRandomBots{0};  // refreshed by the world thread every 5 s while Immersion is on
uint64 nextOnlineCount = 0;
std::unordered_map<uint64, uint64> fishingUntil;  // bot -> end of its time at the water (map threads, under liveLock)

bool ImmersionEnabled() { return sPlayerbotAIConfig.coaLiveImmersionEnable; }

// The random bots online, for the fishing cap: one pass every 5 s on the world thread, only while Immersion is on.
void CountOnlineRandomBots(uint64 nowMs)
{
    if (!ImmersionEnabled() || nowMs < nextOnlineCount)
        return;
    nextOnlineCount = nowMs + 5000;
    uint32 n = 0;
    std::shared_lock<std::shared_mutex> playersLock(*HashMapHolder<Player>::GetLock());
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        if (player && player->IsInWorld() && IsRandom(player))
            ++n;
    onlineRandomBots.store(n, std::memory_order_relaxed);
}

// The "new rpg" holds the bot (REST, only the clock is reset: it does not sit) while the fishing strategy works.
void Fish(Player* bot, PlayerbotAI* botAI)
{
    uint64 const nowMs = GameTime::GetGameTimeMS().count();
    uint64 const key = bot->GetGUID().GetRawValue();
    NewRpgInfo& info = botAI->rpgInfo;
    uint64 endMs = 0;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        auto it = fishingUntil.find(key);
        if (it != fishingUntil.end())
            endMs = it->second;
    }

    if (endMs)
    {
        if (CoaImmersion::MustStop(nowMs, endMs, bot->IsInCombat(), bot->GetGroup() != nullptr,
                                   info.GetStatus() == RPG_REST))
        {
            botAI->ChangeStrategy("-master fishing", BOT_STATE_NON_COMBAT);
            if (info.GetStatus() == RPG_REST)
                info.ChangeToIdle();
            std::lock_guard<std::mutex> guard(liveLock);
            fishingUntil.erase(key);
        }
        else
            info.ChangeToRest();  // a few more seconds at the water
        return;
    }

    CoaImmersion::BotView view;
    view.randomBot = IsRandom(bot);
    view.ownedByPlayer = botAI->HasGameClientMaster();
    view.alive = bot->IsAlive();
    view.inCombat = bot->IsInCombat();
    view.inGroup = bot->GetGroup() != nullptr;
    // A capital's shore too (the canals of Stormwind): where the players are.
    view.inInstanceOrCity = bot->GetMap()->Instanceable() || bot->InBattleground();
    NewRpgStatus const status = info.GetStatus();
    view.idleStatus = status == RPG_IDLE || status == RPG_REST || status == RPG_WANDER_RANDOM;
    view.knowsFishing = bot->HasSkill(SKILL_FISHING);
    if (!CoaImmersion::MayStartFishing(view) || urand(1, 1000) > CoaImmersion::START_CHANCE_PER_MILLE)
        return;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        if (!CoaImmersion::FishingRoom(uint32(fishingUntil.size()), onlineRandomBots.load(std::memory_order_relaxed),
                                       sPlayerbotAIConfig.coaLiveImmersionFishingPerThousand))
            return;  // enough bots at the water already
    }

    // Water close by: the costly look, last.
    WorldPosition water = FindWaterRadial(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                                bot->GetMap(), bot->GetPhaseMask(), 10.0f, 60.0f, 10.0f);
    if (!water.IsValid())
        return;

    info.ChangeToRest();
    botAI->ChangeStrategy("+master fishing", BOT_STATE_NON_COMBAT);
    std::lock_guard<std::mutex> guard(liveLock);
    fishingUntil[key] = nowMs + CoaImmersion::FishingMs(rng.between);
    if (fishingUntil.size() > 20000)
        fishingUntil.clear();
}


// ---------------------------------------------------------------- %place: "where is ...?"

// A name made comparable with a whisper: lower case (UTF-8, by the core), then NormalizeWhisper, without the spaces
// at both ends.
std::string Comparable(std::string const& name)
{
    std::wstring wide;
    std::string lowered = name;
    if (Utf8toWStr(name, wide))
    {
        wstrToLower(wide);
        WStrToUtf8(wide, lowered);
    }
    std::string n = NormalizeWhisper(lowered);
    size_t const a = n.find_first_not_of(' '), b = n.find_last_not_of(' ');
    return a == std::string::npos ? "" : n.substr(a, b - a + 1);
}

// Area and creature names, indexed once (world thread, at the first question): name -> area id / creature entry.
struct PlaceIndex
{
    std::unordered_map<std::string, uint32> areas;
    std::unordered_map<std::string, uint32> creatures;
};

PlaceIndex const& Places()
{
    static PlaceIndex const index = []()
    {
        PlaceIndex i;
        for (AreaTableEntry const* area : sAreaTableStore)
            for (uint8 loc = 0; loc < TOTAL_LOCALES; ++loc)
                if (area->area_name[loc] && *area->area_name[loc])
                    i.areas.emplace(Comparable(area->area_name[loc]), area->ID);
        for (auto const& [entry, creature] : *sObjectMgr->GetCreatureTemplates())
            if (!creature.Name.empty() && creature.Name.find('[') == std::string::npos)
                i.creatures.emplace(Comparable(creature.Name), entry);
        return i;
    }();
    return index;
}

std::string AreaAndZone(uint32 areaId, uint8 locale)
{
    AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
    if (!area)
        return "";
    std::string text = AreaName(areaId, locale);
    if (area->zone && area->zone != areaId)
    {
        std::string const zone = AreaName(area->zone, locale);
        if (!zone.empty() && zone != text)
            text += ", " + zone;
    }
    return text;
}

// The place of the first thing the words name: an area ("goldshire"), else a creature's first spawn ("hogger").
std::string FindPlace(std::vector<std::string> const& subjects, uint8 locale)
{
    PlaceIndex const& index = Places();
    for (std::string const& s : subjects)
    {
        if (auto it = index.areas.find(s); it != index.areas.end())
            return AreaAndZone(it->second, locale);
        if (auto it = index.creatures.find(s); it != index.creatures.end())
            for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
                if (data.id == it->second)
                    return AreaAndZone(sMapMgr->GetAreaId(PHASEMASK_NORMAL, data.mapid, data.posX, data.posY, data.posZ),
                                       locale);
    }
    return "";
}


// ---------------------------------------------------------------- Immersion (L2): gathering trips

std::unordered_map<uint64, uint64> gatherUntil;  // bot -> end of its trip (map threads, under liveLock)

struct GatherNodeCheck
{
    Player* bot;
    bool operator()(GameObject* go) const
    {
        return go->GetGoType() == GAMEOBJECT_TYPE_CHEST && go->isSpawned() && go->GetGOInfo()->GetLockId() &&
               bot->IsWithinDistInMap(go, CoaImmersion::GATHER_MAX);
    }
};

// A free random bot idling out in the world with herbalism or mining now and then walks to a node out of its sight;
// once there, the "gather" strategy (every bot has it) gathers it. The rpg is held as for fishing.
void Gather(Player* bot, PlayerbotAI* botAI)
{
    uint64 const nowMs = GameTime::GetGameTimeMS().count();
    uint64 const key = bot->GetGUID().GetRawValue();
    NewRpgInfo& info = botAI->rpgInfo;
    uint64 endMs = 0;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        auto it = gatherUntil.find(key);
        if (it != gatherUntil.end())
            endMs = it->second;
    }
    if (endMs)
    {
        // Arrived (the gather strategy takes over), time is up, a fight, a group or another plan: the trip ends.
        if (nowMs >= endMs || bot->IsInCombat() || bot->GetGroup() || info.GetStatus() != RPG_REST || !bot->isMoving())
        {
            if (info.GetStatus() == RPG_REST && nowMs >= endMs)
                info.ChangeToIdle();
            std::lock_guard<std::mutex> guard(liveLock);
            gatherUntil.erase(key);
        }
        else
            info.ChangeToRest();
        return;
    }

    CoaImmersion::BotView view;
    view.randomBot = IsRandom(bot);
    view.ownedByPlayer = botAI->HasGameClientMaster();
    view.alive = bot->IsAlive();
    view.inCombat = bot->IsInCombat();
    view.inGroup = bot->GetGroup() != nullptr;
    view.inInstanceOrCity = bot->GetMap()->Instanceable() || bot->InBattleground() || InCapital(bot);
    NewRpgStatus const status = info.GetStatus();
    view.idleStatus = status == RPG_IDLE || status == RPG_REST || status == RPG_WANDER_RANDOM;
    view.knowsFishing = bot->HasSkill(SKILL_HERBALISM) || bot->HasSkill(SKILL_MINING);  // here: a gatherer
    if (!CoaImmersion::MayStartFishing(view) || urand(1, 1000) > CoaImmersion::START_CHANCE_PER_MILLE)
        return;

    // The nearest node it can gather, out of its sight (the costly look, after the roll).
    std::list<GameObject*> nodes;
    GatherNodeCheck check{bot};
    Acore::GameObjectListSearcher<GatherNodeCheck> searcher(bot, nodes, check);
    Cell::VisitObjects(bot, searcher, CoaImmersion::GATHER_MAX);
    GameObject* best = nullptr;
    float bestDist = 0.0f;
    for (GameObject* go : nodes)
    {
        LootObject loot(bot, go->GetGUID());
        float const d = bot->GetDistance(go);
        if (!CoaImmersion::WorthATrip(d, loot.skillId != SKILL_NONE && loot.IsLootPossible(bot)))
            continue;
        if (!best || d < bestDist)
        {
            best = go;
            bestDist = d;
        }
    }
    if (!best)
        return;
    info.ChangeToRest();
    bot->GetMotionMaster()->MovePoint(0, best->GetPositionX(), best->GetPositionY(), best->GetPositionZ());
    std::lock_guard<std::mutex> guard(liveLock);
    gatherUntil[key] = nowMs + CoaImmersion::GATHER_TRIP_MS;
    if (gatherUntil.size() > 20000)
        gatherUntil.clear();
}

// The bot looks around: a real player to help or greet, or an inn or a capital to idle in.
void LookAround(Player* bot, PlayerbotAI* botAI)
{
    if (!ZonePeopled(bot->GetZoneId()) || !IsRandom(bot) || !bot->IsAlive() || bot->IsInCombat() ||
        bot->IsInFlight() || bot->InBattleground() || bot->InArena() || botAI->HasGameClientMaster())
        return;

    std::list<Player*> const nearby = RealPlayersNear(bot, HEAR_RANGE);
    if (nearby.empty())
        return;

    if (Social(bot, botAI, nearby))
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
// An item worth naming in Trade: the rule of CoA Economy's bot auctions (one rule everywhere): never ammunition or a
// quiver (no use in CoA), an item of a later expansion CoA does not have, or a vendor good.
bool TradeWorthy(ItemTemplate const* proto) { return CoaEconomy::Sellable::BotMayList(proto, true); }

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
        if (!TradeWorthy(proto))
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
            proto->Quality <= ITEM_QUALITY_EPIC && TradeWorthy(proto))
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
bool NightOn() { return sPlayerbotAIConfig.coaLiveNightBankEnable; }

// A conversation of the night bank on a channel: 2 or 3 different bots of the zone (or the faction) answer each other,
// one step after another, each typing at its own pace. Paced as one line of the first bot ("night_thread").
void PlayThread(std::vector<Player*> const& bots, uint8 channel, std::string const& scope, float populationFactor)
{
    if (bots.size() < 2)
        return;
    std::shared_ptr<Bank const> current;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        current = bank;
    }
    Rule const* rule = current ? current->RuleOf("night_thread") : nullptr;
    if (!rule || !rng.Roll(ReactPct(*rule, -1, populationFactor)))
        return;

    uint8 const locale = BotLocale();
    Player* first = bots[urand(0, bots.size() - 1)];
    Who const who{first->getRace(), first->getClass(), first->GetZoneId()};
    Thread const* thread = DrawThread(*current, channel, locale, LevelBand(first->GetLevel()), who, bots.size(), rng);
    if (!thread)
        return;
    std::vector<Player*> cast{first};
    for (uint32 tries = 0; cast.size() < thread->Speakers() && tries < 20; ++tries)
    {
        Player* other = bots[urand(0, bots.size() - 1)];
        if (std::find(cast.begin(), cast.end(), other) == cast.end())
            cast.push_back(other);
    }
    if (cast.size() < thread->Speakers())
        return;

    // Every step typed first: a placeholder that does not fill drops the whole conversation.
    std::vector<std::pair<Player*, std::string>> said;
    for (ThreadStep const& step : thread->steps)
    {
        Player* speaker = cast[std::min<size_t>(step.speaker, cast.size()) - 1];
        Player* other = speaker == cast[0] ? cast[1] : cast[0];  // %player: the one answered
        std::string text;
        if (!Humanize(step.text, Values(speaker, other, nullptr, locale), current->SlangOf(locale),
                      sPlayerbotAIConfig.coaLiveAbbrevChance, sPlayerbotAIConfig.coaLiveTypoChance,
                      locale == LOCALE_frFR, rng, text))
            return;
        said.emplace_back(speaker, std::move(text));
    }

    uint64 const nowMs = GameTime::GetGameTimeMS().count();
    uint64 const firstKey = first->GetGUID().GetRawValue();
    std::lock_guard<std::mutex> guard(liveLock);
    if (!pacer.Allowed(*rule, firstKey, "night_thread", scope, channel, nowMs / 1000) ||
        liveQueue.size() + said.size() > QUEUE_MAX)
        return;
    pacer.Said(firstKey, "night_thread", scope, channel, nowMs / 1000);
    uint64 due = nowMs;
    for (auto& [speaker, text] : said)
    {
        due += TypingMs(*rule, text.size(), MsPerChar(speaker), rng);
        Pending p;
        p.dueMs = due;
        p.bot = speaker->GetGUID();
        p.channel = channel;
        p.text = std::move(text);
        liveQueue.push_back(std::move(p));
    }
}

// A dungeon group as the night bank sees it: its random bots on this map, and one of its real players there.
struct DungeonGroup
{
    std::vector<Player*> bots;
    Player* real = nullptr;
    std::string scope;
};

bool InDungeonGroup(Player* member, DungeonGroup& out)
{
    Group* group = member ? member->GetGroup() : nullptr;
    if (!group || !member->IsInWorld() || !member->GetMap()->IsDungeon())
        return false;
    uint32 const mapId = member->GetMapId();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* p = ref->GetSource(); p && p->IsInWorld() && p->GetMapId() == mapId)
        {
            if (IsRandom(p))
                out.bots.push_back(p);
            else if (IsRealPlayer(p))
                out.real = p;
        }
    out.scope = "g" + std::to_string(group->GetGUID().GetCounter());
    return !out.bots.empty() && out.real;
}

std::unordered_map<uint64, uint32> dungeonOf;  // random bot -> the dungeon it is in (night bank, under liveLock)

// Night bank: a fine drop (rare or better) in a dungeon group with a real player: the bot that got it shows it off,
// or a bot nods at the player's, with the item's real link.
void NightLoot(Player* looter, Item* item)
{
    if (!Enabled() || !NightOn() || !item || !item->GetTemplate() || item->GetTemplate()->Quality < ITEM_QUALITY_RARE)
        return;
    DungeonGroup g;
    if (!InDungeonGroup(looter, g))
        return;
    ReactOptions opt;
    opt.extra["item"] = ChatHelper::FormatItem(item->GetTemplate());
    if (IsRandom(looter))
        React(looter, "night_dgn_loot_self", g.real, g.scope, opt);
    else if (looter == g.real)
        React(g.bots[urand(0, g.bots.size() - 1)], "night_dgn_loot_good", g.real, g.scope, opt);
}

// One Trade line of a bot in a capital: a sale or a buy that follows the auction house (an offer a player can answer),
// or chatter. False: nothing said.
bool TradeLine(Player* bot, std::string const& scope, ReactOptions const& opt, uint64 nowMs)
{
    ReactOptions trade = opt;
    Offer offer;
    uint32 const roll = urand(0, 2);
    bool offered = false;
    bool said = false;
    if (roll == 0 && WtsValues(bot, trade.extra, offer))
        said = offered = React(bot, "trade_wts", nullptr, scope, trade);
    else if (roll == 1 && WtbValues(bot, trade.extra, offer))
        said = offered = React(bot, "trade_wtb", nullptr, scope, trade);
    if (!said)
    {
        ReactOptions chat = opt;
        said = React(bot, "trade_chat", nullptr, scope, chat);
    }
    if (offered)
    {
        offer.expiresMs = nowMs + OFFER_MS;
        std::lock_guard<std::mutex> guard(liveLock);
        offers[bot->GetGUID().GetRawValue()] = offer;
        if (offers.size() > 4096)
            offers.clear();
    }
    return said;
}

// The Trade pace (AiPlayerbot.CoaLive.TradeMessagesPerMinute): while a real player stands in a capital of a faction
// (or the bench key), its Trade never stays quiet longer than the gap drawn after the last line, whoever said it.
// The world is looked at only when a faction's gap is over. World thread.
void TradePace(uint64 nowMs)
{
    uint32 const perMinute = sPlayerbotAIConfig.coaLiveTradeMessagesPerMinute;
    if (!CoaChat::TradePaceRuns(sPlayerbotAIConfig.coaLiveChatEnable, perMinute))
        return;
    bool due = false;
    for (uint32 team : {uint32(TEAM_ALLIANCE), uint32(TEAM_HORDE)})
    {
        if (!tradeGapMs.count(team))
            tradeGapMs[team] = TradeGapMs(perMinute, rng);
        if (nowMs >= tradeLastMs[team] + tradeGapMs[team])
            due = true;
    }
    if (!due)
        return;

    std::unordered_map<uint32, bool> realInCapital;
    std::unordered_map<uint32, std::vector<Player*>> capitalBots;
    {
        std::shared_lock<std::shared_mutex> playersLock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            if (!player || !player->IsInWorld() || !InCapital(player))
                continue;
            if (IsRealPlayer(player))
                realInCapital[player->GetTeamId()] = true;
            else if (FreeRandomBot(player))
                capitalBots[player->GetTeamId()].push_back(player);
        }
    }
    for (auto& [team, bots] : capitalBots)
    {
        if (bots.empty() || nowMs < tradeLastMs[team] + tradeGapMs[team])
            continue;
        if (!realInCapital[team] && !sPlayerbotAIConfig.coaLiveDebugIgnoreRealPlayer)
            continue;
        std::vector<uint64> ids;
        for (Player* b : bots)
            ids.push_back(b->GetGUID().GetRawValue());
        ReactOptions opt;
        opt.force = true;
        // A few tries: a bot still in its own cooldown, or with nothing to say, hands over to another.
        for (int tries = 0; tries < 4; ++tries)
        {
            size_t const i = PickSpeaker(bots.size(), ids, tradeSpeakers[team], rng);
            if (i == SIZE_MAX)
                break;
            if (TradeLine(bots[i], "z" + std::to_string(bots[i]->GetZoneId()), opt, nowMs))
            {
                // Counted when it is said (SayInTrade); until then, no second line is forced.
                tradeLastMs[team] = nowMs + 15000;
                break;
            }
        }
    }
}

// AiPlayerbot.CoaLive.ChannelQuestions.Enable: the open questions real players asked in a public channel, heard by the
// chat hook (any thread) and answered by the world thread (one bot each). Under liveLock.
struct ChannelAsk
{
    ObjectGuid player;
    uint32 channelId = 0;
    std::string channelName;
    CoaChanAsk::Topic topic = CoaChanAsk::Topic::None;
};
std::vector<ChannelAsk> channelAsks;
std::unordered_map<uint64, uint64> channelAskLast;  // player -> when a bot last took one of their questions (s)
std::unordered_map<std::string, uint64> channelAnswerLast;  // "c<team>|<channel>" -> last answer (s), world thread

// AiPlayerbot.CoaLive.LootNeedAsk: the open asks of the bots on loot rolls (CoaLootAskRules.h). Under liveLock.
CoaLootAsk::Book lootAsks;

// A real player for the asks, or the bench's stand-in (AiPlayerbot.CoaLive.LootNeedAsk.TestPlayer).
bool LootAskPlayer(Player* player)
{
    if (IsRealPlayer(player))
        return true;
    std::string const& test = sPlayerbotAIConfig.coaLiveLootNeedAskTestPlayer;
    return !test.empty() && player->GetName() == test;
}

// A real player's party or raid line (receiver null), or whisper to a bot: an answer to the asks it is part of.
void HearLootAnswer(Player* player, std::string const& msg, Player* receiver)
{
    CoaLootAsk::Answer const answer = CoaLootAsk::Classify(msg);
    if (answer == CoaLootAsk::Answer::None)
        return;
    uint64 const nowMs = GameTime::GetGameTimeMS().count();
    std::lock_guard<std::mutex> guard(liveLock);
    for (auto& [roll, ask] : lootAsks.All())
        if (!receiver || ask.bot == receiver->GetGUID().GetRawValue())
            CoaLootAsk::Hear(ask, player->GetGUID().GetRawValue(), answer, nowMs);
}

void HearChannelQuestion(Player* player, std::string const& msg, ::Channel* channel)
{
    uint32 const id = channel->GetChannelId();
    if (id != 1 && id != 2 && id != 3 && id != 26)  // Ascension, Newcomers, Zone, LookingForGroup
        return;
    CoaChanAsk::Topic const topic = CoaChanAsk::Classify(msg);
    if (topic == CoaChanAsk::Topic::None)
        return;
    uint64 const nowS = uint64(GameTime::GetGameTime().count());
    uint64 const key = player->GetGUID().GetRawValue();
    std::lock_guard<std::mutex> guard(liveLock);
    auto const last = channelAskLast.find(key);
    if (!CoaChanAsk::PlayerMayAsk(last == channelAskLast.end() ? 0 : last->second, nowS) || channelAsks.size() >= 64)
        return;
    channelAskLast[key] = nowS;
    if (channelAskLast.size() > 20000)
        channelAskLast.clear();
    channelAsks.push_back({player->GetGUID(), id, channel->GetName(), topic});
}

// One bot of the player's faction that hears that channel (for Zone, one in the player's zone) answers.
void AnswerChannelQuestions()
{
    std::vector<ChannelAsk> asks;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        asks.swap(channelAsks);
    }
    for (ChannelAsk const& ask : asks)
    {
        Player* player = ObjectAccessor::FindConnectedPlayer(ask.player);
        if (!player || !player->IsInWorld())
            continue;
        std::vector<Player*> hearers;
        {
            std::shared_lock<std::shared_mutex> playersLock(*HashMapHolder<Player>::GetLock());
            for (auto const& [guid, bot] : ObjectAccessor::GetPlayers())
                if (bot && FreeRandomBot(bot) && bot->GetTeamId() == player->GetTeamId() &&
                    (ask.channelId != 3 || bot->GetZoneId() == player->GetZoneId()))
                    hearers.push_back(bot);
        }
        if (hearers.empty())
            continue;
        // A real player's question gets its answer: no chance roll, no scope pace of the bank (the player and the
        // channel have theirs, CoaChanAskRules.h), and the first of a few bots out of its own cooldown speaks.
        std::string const scope = "c" + std::to_string(player->GetTeamId()) + "|" + ask.channelName;
        uint64 const nowS = uint64(GameTime::GetGameTime().count());
        auto const last = channelAnswerLast.find(scope);
        if (!CoaChanAsk::ChannelMayAnswer(last == channelAnswerLast.end() ? 0 : last->second, nowS))
            continue;
        ReactOptions opt;
        opt.sayChannel = ask.channelName;
        opt.force = true;
        char const* situation = ask.topic == CoaChanAsk::Topic::Class ? "chan_class" : "chan_question";
        // The class question gets up to two answers (ClassAnswers), each from another bot: the second names another
        // class, 4 to 12 seconds after the first. A bank with "chan_class_2" lines gives the second its own lines,
        // which may name the first answer's class (%class1); without them it is a second opinion of "chan_class".
        uint32 const wanted = CoaChanAsk::AnswersWanted(ask.topic, sPlayerbotAIConfig.coaLiveChannelClassAnswers,
                                                        hearers.size());
        uint32 answered = 0;
        ReactOptions::Said first;
        for (uint32 tried = 0; tried < CoaChanAsk::BOTS_TRIED * wanted && answered < wanted && !hearers.empty();
             ++tried)
        {
            uint32 const i = urand(0, hearers.size() - 1);
            Player* const bot = hearers[i];
            hearers[i] = hearers.back();
            hearers.pop_back();
            bool said = false;
            if (!answered)
            {
                opt.said = &first;
                said = React(bot, situation, player, scope, opt);
            }
            else
            {
                ReactOptions second = opt;
                second.said = nullptr;
                second.avoidLead = first.lead;
                second.dueAtMs = CoaChanAsk::SecondDueMs(first.dueMs, urand(0, 0x7fffffff));
                second.extra["class1"] = first.lead;
                said = React(bot, "chan_class_2", player, scope, second) || React(bot, situation, player, scope, second);
            }
            if (said)
            {
                channelAnswerLast[scope] = nowS;
                ++answered;
            }
        }
    }
}

void ChannelTimers(uint64 nowMs)
{
    if (nowMs < nextChannelTick)
        return;
    nextChannelTick = nowMs + CHANNEL_TICK_MS;

    std::unordered_map<uint32, uint32> realByZone, realByGuild;
    std::unordered_map<uint32, std::vector<Player*>> botsByZone, botsByGuild;
    std::unordered_map<uint32, uint32> realByTeam;              // night bank: the world channel
    std::unordered_map<uint32, std::vector<Player*>> botsByTeam;
    std::shared_lock<std::shared_mutex> playersLock(*HashMapHolder<Player>::GetLock());
    for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
    {
        if (!player || !player->IsInWorld())
            continue;
        if (IsRealPlayer(player))
        {
            ++realByZone[player->GetZoneId()];
            ++realByTeam[player->GetTeamId()];
            if (player->GetGuildId())
                ++realByGuild[player->GetGuildId()];
        }
        else if (FreeRandomBot(player))
        {
            botsByZone[player->GetZoneId()].push_back(player);
            if (NightOn())
                botsByTeam[player->GetTeamId()].push_back(player);
            if (player->GetGuildId())
                botsByGuild[player->GetGuildId()].push_back(player);
        }
    }

    // Bench only (hidden key): a real player stands in every capital that has bots.
    if (sPlayerbotAIConfig.coaLiveDebugIgnoreRealPlayer)
        for (auto const& [zone, bots] : botsByZone)
            if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(zone); area && (area->flags & AREA_FLAG_CAPITAL))
                realByZone.try_emplace(zone, 1);

    uint8 const locale = BotLocale();
    auto pick = [](std::vector<Player*> const& v) { return v[urand(0, v.size() - 1)]; };
    for (auto const& [zone, real] : realByZone)
    {
        auto it = botsByZone.find(zone);
        if (it == botsByZone.end() || it->second.empty())
            continue;
        ReactOptions opt;
        // A realm with one player is the usual case: the bots of the zone count too (ten for one player), or the
        // channels of a solo realm stayed at a third of their pace (a Trade line every 13 minutes).
        opt.populationFactor = PopulationFactor(real + uint32(it->second.size()) / 10);
        std::string const scope = "z" + std::to_string(zone);

        React(pick(it->second), "general_chat", nullptr, scope, opt);
        if (NightOn())
        {
            React(pick(it->second), "night_zone", nullptr, scope, opt);
            PlayThread(it->second, CH_GENERAL, scope, opt.populationFactor);
        }

        Player* bot = pick(it->second);
        if (InCapital(bot))
            TradeLine(bot, scope, opt, nowMs);

        bot = pick(it->second);
        ReactOptions lfg = opt;
        if (LfgValues(bot, locale, lfg.extra))
            React(bot, urand(0, 1) ? "lfg_lfm" : "lfg_lfg", nullptr, "lfg" + std::to_string(bot->GetTeamId()), lfg);
    }

    // Night bank: the world channel of each faction that has a real player online (or the bench key).
    for (auto const& [team, bots] : botsByTeam)
    {
        auto real = realByTeam.find(team);
        if (bots.empty() || (real == realByTeam.end() && !sPlayerbotAIConfig.coaLiveDebugIgnoreRealPlayer))
            continue;
        ReactOptions opt;
        opt.populationFactor = PopulationFactor((real == realByTeam.end() ? 1 : real->second) + uint32(bots.size()) / 10);
        std::string const scope = "w" + std::to_string(team);
        React(pick(bots), "night_world", nullptr, scope, opt);
        PlayThread(bots, CH_WORLD, scope, opt.populationFactor);
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
    // Off: nothing read at all, as before 3.0 (a database without the CoA Live tables starts the same).
    if (!sPlayerbotAIConfig.coaLiveEnable)
    {
        std::lock_guard<std::mutex> guard(liveLock);
        bank = nullptr;
        botRings.clear();
        channelRings.clear();
        return;
    }
    // On but a table or a column is missing (the 3.0 SQL not applied): the core would abort the server on the
    // query, so each table is checked first. No bank, said once, the bots stay quiet and the server starts.
    bool const tablesOk =
        CoaDbCheck::Require("CoA Live", "playerbots_coa_live_rules",
                            {"situation", "chance_pct", "delay_min_ms", "delay_max_ms", "bot_cd_s", "scope_cd_s",
                             "scope_per_10min", "need_player"}) &&
        CoaDbCheck::Require("CoA Live", "playerbots_coa_live_lines",
                            {"id", "situation", "channel", "locale", "level_band", "emote", "text", "w_lazy",
                             "w_quester", "w_wanderer", "w_trader", "w_pvp", "rare"}) &&
        CoaDbCheck::Require("CoA Live", "playerbots_coa_live_intents", {"locale", "priority", "intent", "keywords"}) &&
        CoaDbCheck::Require("CoA Live", "playerbots_coa_live_slang", {"locale", "full", "short"});
    if (!tablesOk)
    {
        std::lock_guard<std::mutex> guard(liveLock);
        bank = nullptr;
        return;
    }
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

    // The night bank (AiPlayerbot.CoaLive.NightBank.Enable): its single lines into the bank, its conversations aside.
    if (sPlayerbotAIConfig.coaLiveNightBankEnable &&
        CoaDbCheck::Require("CoA Live night bank", "playerbots_coa_night_lines",
                            {"id", "channel", "event", "locale", "race", "class", "level_band", "zone", "thread", "step",
                             "speaker", "weight", "text"}))
    {
        uint32 singles = 0;
        std::map<std::tuple<uint8, uint8, uint32>, Thread> threads;  // (channel, locale, thread) -> steps in order
        if (QueryResult r = PlayerbotsDatabase.Query(
                "SELECT id, channel, event, locale, race, class, level_band, zone, thread, step, speaker, weight, text "
                "FROM playerbots_coa_night_lines ORDER BY thread, step"))
            do
            {
                Field* f = r->Fetch();
                uint8 const channel = f[1].Get<uint8>();
                std::string const event = f[2].Get<std::string>();
                uint8 const locale = f[3].Get<uint8>();
                uint32 const thread = f[8].Get<uint32>();
                if (thread)
                {
                    Thread& t = threads[{channel, locale, thread}];
                    t.race = f[4].Get<uint8>();
                    t.cls = f[5].Get<uint8>();
                    t.levelBand = f[6].Get<uint8>();
                    t.zone = f[7].Get<uint32>();
                    t.steps.push_back({std::max<uint8>(1, f[10].Get<uint8>()), f[12].Get<std::string>()});
                    continue;
                }
                std::string const situation = NightSituation(channel, event);
                if (situation.empty())
                    continue;
                Line line;
                line.id = 0x80000000u | f[0].Get<uint32>();  // apart from the live lines' ids (the no-repeat rings)
                line.situation = situation;
                line.channel = channel;
                line.locale = locale;
                line.race = f[4].Get<uint8>();
                line.cls = f[5].Get<uint8>();
                line.levelBand = f[6].Get<uint8>();
                line.zone = f[7].Get<uint32>();
                line.text = f[12].Get<std::string>();
                uint8 const weight = f[11].Get<uint8>();
                for (uint8 i = 0; i < ARCH_COUNT; ++i)
                    line.weight[i] = weight;
                loaded->Add(std::move(line));
                ++singles;
            } while (r->NextRow());
        for (auto& [key, t] : threads)
            if (std::get<0>(key) == CH_GENERAL || std::get<0>(key) == CH_WORLD || std::get<0>(key) == CH_PARTY)
                loaded->threads[std::get<0>(key)][std::get<1>(key)].push_back(std::move(t));
        AddNightRules(*loaded);
        LOG_INFO("server.loading", ">> CoA Live night bank: {} lines, {} conversations", singles, threads.size());
    }

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
                        PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_LOOT_ITEM, PLAYERHOOK_ON_GIVE_EXP,
                        PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_PLAYER_KILLED_BY_CREATURE,
                        PLAYERHOOK_ON_MAP_CHANGED, PLAYERHOOK_CAN_PLAYER_USE_CHANNEL_CHAT,
                        PLAYERHOOK_CAN_PLAYER_USE_GROUP_CHAT, PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT})
    {
    }

    // A bot dings in front of a real player; a real player dings next to random bots or in their group.
    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (player && player->GetLevel() > oldLevel)
            CoaPet::OnLevelUp(player);  // an alt's class book ("pet auto on"), with or without CoA Live
        if (!Enabled() || !player || player->GetLevel() <= oldLevel)
            return;

        std::string const scope = "l" + std::to_string(player->GetGUID().GetCounter());
        if (uint32 guildId = player->GetGuildId())
            NoteGuildEvent({"guild_levelup", guildId, player->GetGUID(), 0});
        if (IsRandom(player))
        {
            // Night bank: a ding in a dungeon group with a real player is said to the group.
            if (NightOn() && player->GetMap()->IsDungeon() && GroupHasRealPlayer(player) &&
                React(player, "night_dgn_levelup", nullptr, scope))
                return;
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
        if (player)
        {
            std::lock_guard<std::mutex> guard(liveLock);
            fishingUntil.erase(player->GetGUID().GetRawValue());  // a bot gone is no longer at the water
        }
        if (player)
            tradeJoined.erase(player->GetGUID().GetRawValue());
            worldJoined.erase(player->GetGUID().GetRawValue());  // its channels are left with it: join again next time
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
        NightLoot(player, item);
        if (!Enabled() || !player || !item || !player->GetGuildId() || !item->GetTemplate() ||
            item->GetTemplate()->Quality < ITEM_QUALITY_RARE)
            return;
        bool const self = IsRandom(player);
        if (!self && !IsRealPlayer(player))
            return;
        NoteGuildEvent({self ? "guild_self_loot" : "guild_loot", player->GetGuildId(), player->GetGUID(),
                        item->GetEntry()});
    }

    // The launch of a new realm: each random bot levels at a pace of its own (fast, mass, late), always the same,
    // and catches up with the human players when behind (CoaCatchUp). A bot in a group with a real player keeps
    // the normal rate and follows its group; never an alt nor an addclass bot. The simulated dungeons give their
    // experience through GiveXP, so this rule too.
    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* /*victim*/, uint8 /*xpSource*/) override
    {
        if (!sPlayerbotAIConfig.coaFreshStartEnable || !player || !amount || !IsRandom(player) ||
            sRandomPlayerbotMgr.IsAddclassBot(player) || GroupHasRealPlayer(player))
            return;
        amount = static_cast<uint32>(uint64(amount) * CoaCatchUp::XpPercent(player) / 100);
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
        std::string const scope = "g" + std::to_string(player->GetGroup()->GetGUID().GetCounter());
        if (!React(player, "boss_pull", nullptr, scope, opt) && NightOn())
            React(player, "night_dgn_pull_big", nullptr, scope, opt);
    }

    // The Trade pace counts the players' own Trade lines too (they are let through as before).
    // An open question of a real player in a public channel (AiPlayerbot.CoaLive.ChannelQuestions.Enable).
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, ::Channel* channel) override
    {
        if (Enabled() && player && channel && channel->GetChannelId() == COA_TRADE_CHANNEL_ID && IsRealPlayer(player))
            NoteTrade(player);
        if (Enabled() && sPlayerbotAIConfig.coaLiveChannelQuestions && player && channel && IsRealPlayer(player))
            HearChannelQuestion(player, msg, channel);
        return true;
    }

    // A real player answers a bot's loot ask (AiPlayerbot.CoaLive.LootNeedAsk) in party or raid chat, or by whisper.
    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Group* group) override
    {
        if (Enabled() && sPlayerbotAIConfig.coaLiveLootNeedAsk && player && group && IsRealPlayer(player))
            HearLootAnswer(player, msg, nullptr);
        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 /*lang*/, std::string& msg, Player* receiver) override
    {
        if (Enabled() && sPlayerbotAIConfig.coaLiveLootNeedAsk && player && receiver && IsRealPlayer(player) &&
            GET_PLAYERBOT_AI(receiver))
            HearLootAnswer(player, msg, receiver);
        return true;
    }

    using PlayerScript::OnPlayerCanUseChat;

    // Night bank: a boss down in a group with a real player: a bot says it, another may praise the player's role,
    // and now and then two or three bots chat a while (a group conversation).
    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        CoaWorldFile::NoteBossKill(killer, killed);  // world.json "groups" (DataFile), with or without CoA Live
        if (!Enabled() || !NightOn() || !killed || !(killed->IsDungeonBoss() || killed->isWorldBoss()))
            return;
        DungeonGroup g;
        if (!InDungeonGroup(killer, g))
            return;
        ReactOptions opt;
        opt.boss = killed;
        Player* speaker = g.bots[urand(0, g.bots.size() - 1)];
        React(speaker, "night_dgn_boss_kill", g.real, g.scope, opt);
        Player* other = g.bots[urand(0, g.bots.size() - 1)];
        char const* praise = PlayerbotAI::IsTank(g.real)   ? "night_dgn_praise_tank"
                             : PlayerbotAI::IsHeal(g.real) ? "night_dgn_praise_heal"
                                                           : "night_dgn_praise_dps";
        React(other, praise, g.real, g.scope, opt);
        PlayThread(g.bots, CH_PARTY, g.scope, 1.0f);
    }

    // Night bank: a death in a dungeon group. Everyone down there: a wipe; the real player alone: a tease.
    void OnPlayerKilledByCreature(Creature* /*killer*/, Player* killed) override
    {
        CoaWorldFile::NoteDeath(killed);  // world.json "groups" (DataFile), with or without CoA Live
        if (!Enabled() || !NightOn())
            return;
        DungeonGroup g;
        if (!InDungeonGroup(killed, g))
            return;
        bool everyone = !g.real->IsAlive();
        for (Player* bot : g.bots)
            everyone = everyone && !bot->IsAlive();
        Player* speaker = g.bots[urand(0, g.bots.size() - 1)];
        if (everyone)
            React(speaker, "night_dgn_wipe", g.real, g.scope);
        else if (killed == g.real && speaker->IsAlive())
            React(speaker, "night_dgn_tease", g.real, g.scope);
    }

    // Night bank: a random bot of a group with a real player enters a dungeon (the group's hello) or leaves it (the
    // goodbye). One line per group, the pace of the scope keeps the others quiet.
    void OnPlayerMapChanged(Player* player) override
    {
        if (!Enabled() || !NightOn() || !player || !IsRandom(player))
            return;
        uint64 const key = player->GetGUID().GetRawValue();
        bool const inDungeon = player->GetMap() && player->GetMap()->IsDungeon();
        bool wasIn = false;
        {
            std::lock_guard<std::mutex> guard(liveLock);
            auto it = dungeonOf.find(key);
            wasIn = it != dungeonOf.end() && it->second != player->GetMapId();
            if (inDungeon)
                dungeonOf[key] = player->GetMapId();
            else if (it != dungeonOf.end())
                dungeonOf.erase(it);
            if (dungeonOf.size() > 20000)
                dungeonOf.clear();
        }
        Group* group = player->GetGroup();
        if (!group || !GroupHasRealPlayer(player))
            return;
        std::string const scope = "g" + std::to_string(group->GetGUID().GetCounter());
        if (inDungeon)
        {
            DungeonGroup g;
            React(player, "night_dgn_enter", InDungeonGroup(player, g) ? g.real : nullptr, scope);
        }
        else if (wasIn)
            React(player, "night_dgn_end", nullptr, scope);
    }
};

class CoaLiveUnitScript : public UnitScript
{
public:
    CoaLiveUnitScript() : UnitScript("CoaLiveUnitScript", true, {UNITHOOK_ON_AURA_APPLY}) {}

    // Every aura of the realm passes here: leave at once unless it is a real player's buff on a random bot.
    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        if (!Enabled() || !unit || !aura || unit->GetTypeId() != TYPEID_PLAYER)
            return;
        ObjectGuid const casterGuid = aura->GetCasterGUID();
        if (!casterGuid.IsPlayer() || casterGuid == unit->GetGUID() || aura->IsPassive() || !aura->GetSpellInfo() ||
            !aura->GetSpellInfo()->IsPositive() || aura->GetMaxDuration() < 60 * IN_MILLISECONDS)
            return;
        Player* bot = unit->ToPlayer();
        if (!IsRandom(bot))
            return;
        Player* caster = ObjectAccessor::GetPlayer(*bot, casterGuid);
        if (!caster || !IsRealPlayer(caster) || (bot->GetGroup() && bot->GetGroup() == caster->GetGroup()))
            return;
        ReactOptions opt;
        opt.perSubjectCdS = 600;  // one thanks per player and bot in 10 minutes
        React(bot, "buff_received", caster, "b" + std::to_string(caster->GetGUID().GetCounter()), opt);
    }
};

class CoaLiveWorldScript : public WorldScript
{
public:
    CoaLiveWorldScript() : WorldScript("CoaLiveWorldScript", {WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_AFTER_CONFIG_LOAD}) {}

    // ".reload config" reads the CoA Live keys and the bank again (the conf.dist says so of each of them); before,
    // only ".playerbots bot reload", in game, did.
    void OnAfterConfigLoad(bool reload) override
    {
        if (!reload)
            return;
        sPlayerbotAIConfig.LoadCoaLiveConfig();
        LoadBank();
        LOG_INFO("server.loading", ">> CoA Live: settings read again (.reload config)");
    }

    void OnUpdate(uint32 /*diff*/) override
    {
        CoaWorldFile::Tick(GameTime::GetGameTimeMS().count());  // world.json, only while the dashboard watches
        CountOnlineRandomBots(GameTime::GetGameTimeMS().count());
        CoaCatchUp::Tick(GameTime::GetGameTimeMS().count());  // the human front of a new living realm
        CoaSoul::Flush(GameTime::GetGameTimeMS().count());       // new souls into their table
        if (Enabled())
        {
            uint64 const nowMs = GameTime::GetGameTimeMS().count();
            RefreshPeopledZones(nowMs);
            Play([nowMs](Pending const& p) { return p.dueMs <= nowMs; });
            PlayGuildEvents();
            if (sPlayerbotAIConfig.coaLiveChannelQuestions)
                AnswerChannelQuestions();
            ChannelTimers(nowMs);
            TradePace(nowMs);
        }
    }
};
}  // namespace

namespace CoaLive
{
CoaLootAsk::Outcome LootNeedAsk(Player* bot, ObjectGuid itemGuid, uint8 itemSlot, ItemTemplate const* proto,
                                int32 randomProperty, uint32 seenMs)
{
    using CoaLootAsk::Outcome;
    if (!Enabled() || !sPlayerbotAIConfig.coaLiveLootNeedAsk || !bot || !proto || !bot->IsInWorld())
        return Outcome::NotAsked;
    uint64 const roll = itemGuid.GetRawValue() * 31 + itemSlot;
    uint64 const botKey = bot->GetGUID().GetRawValue();
    uint64 const nowMs = GameTime::GetGameTimeMS().count();

    // An ask already open on this roll: its answer, for the bot that asked; the other bots vote as usual.
    std::string say;
    ObjectGuid sayTo;
    Outcome outcome = Outcome::NotAsked;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        lootAsks.Forget(nowMs);
        if (CoaLootAsk::Ask* ask = lootAsks.Find(roll))
        {
            if (ask->bot != botKey)
                return Outcome::NotAsked;
            outcome = CoaLootAsk::Decide(*ask, nowMs);
            if (outcome != Outcome::Wait && !ask->decided)
            {
                ask->decided = true;
                LOG_INFO("playerbots", "coa loot ask: instance {} map {} bot {} item {} answer {} vote {}",
                         uint32(ask->instance & 0xFFFFFFFF), uint32(ask->instance >> 32), bot->GetName(), ask->itemId,
                         CoaLootAsk::AnswerName(*ask), outcome == Outcome::Need ? "need" : "greed");
                if (outcome == Outcome::Need)
                    say = "loot_need_thanks";
                else if (ask->no)
                    say = "loot_need_ok_pass";
                sayTo = ObjectGuid(*ask->players.begin());
            }
        }
    }
    if (outcome != Outcome::NotAsked)
    {
        if (!say.empty())
        {
            ReactOptions opt;
            opt.force = true;
            React(bot, say, ObjectAccessor::FindConnectedPlayer(sayTo), "lna", opt);
        }
        return outcome;
    }

    // A new ask: at the opening of the roll, in a dungeon (no raid yet), with a real player in the group, on gear
    // that is a real upgrade for the bot and none for any real player of the group.
    Group* group = bot->GetGroup();
    Map* map = bot->GetMap();
    if (seenMs > CoaLootAsk::ASK_WITHIN_MS || !group || !map || !map->IsNonRaidDungeon() ||
        proto->Quality < ITEM_QUALITY_UNCOMMON ||
        (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR))
        return Outcome::NotAsked;
    bool emptySlot = false;
    float const pct = CoaGearScore::UpgradePct(bot, proto->ItemId, randomProperty, emptySlot);
    if (!emptySlot && pct < sPlayerbotAIConfig.coaLiveLootNeedAskMinPct)
        return Outcome::NotAsked;
    CoaLootAsk::Ask ask;
    ask.bot = botKey;
    ask.deadlineMs = nowMs - seenMs + CoaLootAsk::WINDOW_MS;
    ask.itemId = proto->ItemId;
    Player* first = nullptr;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld() || !LootAskPlayer(member))
            continue;
        // Never on an item a real player of the group would wear better than what it wears.
        if (member->CanUseItem(proto) == EQUIP_ERR_OK &&
            CoaGearScore::UpgradeGain(member, proto->ItemId, randomProperty) > 0.0f)
            return Outcome::NotAsked;
        ask.players.insert(member->GetGUID().GetRawValue());
        if (!first)
            first = member;
    }
    if (!first)
        return Outcome::NotAsked;
    uint64 const instance = (uint64(map->GetId()) << 32) | map->GetInstanceId();
    uint32 const perDungeon = sPlayerbotAIConfig.coaLiveLootNeedAskPerDungeon;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        if (!lootAsks.CanOpen(instance, roll, perDungeon))
            return Outcome::NotAsked;
    }
    ReactOptions opt;
    opt.force = true;
    opt.extra["item"] = ChatHelper::FormatItem(proto);
    if (!React(bot, "loot_need_ask", first, "lna", opt))
        return Outcome::NotAsked;  // no line in the bank: the usual vote
    ask.instance = instance;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        if (!lootAsks.Open(instance, roll, ask, perDungeon))
            return Outcome::NotAsked;
    }
    // Bench: the stand-in answers through the path of a real player's party line.
    std::string const& testName = sPlayerbotAIConfig.coaLiveLootNeedAskTestPlayer;
    std::string const& testAnswer = sPlayerbotAIConfig.coaLiveLootNeedAskTestAnswer;
    if (!testName.empty() && (testAnswer == "yes" || testAnswer == "no"))
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && member->GetName() == testName)
                HearLootAnswer(member, testAnswer, nullptr);
    return Outcome::Wait;
}

bool GroupEmoteAllowed(Player* bot)
{
    uint32 const perBot = sPlayerbotAIConfig.coaLiveGroupEmotesPerBot;
    uint32 const perGroup = sPlayerbotAIConfig.coaLiveGroupEmotesPerGroup;
    if (!bot || (!perBot && !perGroup) || !bot->GetGroup() || !GroupHasRealPlayer(bot))
        return true;
    static std::mutex capLock;
    static CoaGroupEmote::Cap cap;
    uint64 const nowS = uint64(GameTime::GetGameTime().count());
    std::lock_guard<std::mutex> guard(capLock);
    return cap.Take(bot->GetGUID().GetRawValue(), bot->GetGroup()->GetGUID().GetRawValue(), nowS, perBot, perGroup);
}

void LoadBank()
{
    CoaSoul::Load();
    Load();
}

void OnBotUpdate(Player* bot, PlayerbotAI* botAI, uint32 diff)
{
    if (!bot || !botAI || diff == 0)
        return;
    // The weapon drawn in a fight, put away out of it (#11610): every update, a compare when nothing changes.
    if (sPlayerbotAIConfig.coaLiveSheathEnable && bot->IsInWorld() && !IsSelfBot(bot))
    {
        Item* rangedItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        uint8 const wanted = CoaSheath::Wanted(bot->IsInCombat(), bot->IsMounted(), bot->IsFlying() || bot->IsInFlight(),
                                               PlayerbotAI::IsRanged(bot), rangedItem != nullptr);
        if (bot->GetSheath() != SheathState(wanted))
            bot->SetSheath(SheathState(wanted));
    }
    if (!Enabled() && !ImmersionEnabled() && !sPlayerbotAIConfig.coaLiveCleanWorldEnable)
        return;
    // About every SCAN_MS per bot, spread over the bots, with no state: the bot's turn comes when the clock,
    // shifted by its guid, crosses a multiple of SCAN_MS during this update.
    uint64 const shifted = GameTime::GetGameTimeMS().count() + bot->GetGUID().GetCounter() * 977u;
    if (shifted % SCAN_MS >= std::min<uint64>(diff, SCAN_MS))
        return;
    if (ImmersionEnabled())
    {
        Fish(bot, botAI);
        if (!IsFishing(bot))
            Gather(bot, botAI);
    }
    if (!IsFishing(bot))
        CoaCleanWorld::LeaveWater(bot, botAI);
    if (Enabled())
        LookAround(bot, botAI);
}

bool ChatOn() { return sPlayerbotAIConfig.coaLiveChatEnable; }

bool LegacyChatOn()
{
    return CoaChat::LegacyLinesAllowed(sPlayerbotAIConfig.coaLiveEnable, sPlayerbotAIConfig.coaLiveChatEnable,
                                       sPlayerbotAIConfig.coaLiveLegacyChat);
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
    std::string const normalized = NormalizeWhisper(lowered);
    std::string const intent = Classify(normalized, current->IntentsOf(BotLocale()));

    // An intent another session answers itself (CoA Economy: "craft"): it takes the whisper when it says so.
    WhisperHandler handler;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        auto it = whisperHandlers.find(intent);
        if (it != whisperHandlers.end())
            handler = it->second;
    }
    if (handler && handler(from, to, lowered, intent))
        return true;

    // An answer to the bot's own Trade announce: the deal is done for real (WTS: the item by mail, cash on
    // delivery at the price said), or politely turned down (the item is gone; a WTB: "found one already"). Only on a
    // clear yes (AcceptsOffer): any other whisper gets the usual answer and the offer stays open.
    if (intent != "bot" && intent != "hello" && intent != "thanks")
    {
        Offer offer;
        bool live = false;
        {
            std::lock_guard<std::mutex> guard(liveLock);
            auto it = offers.find(to->GetGUID().GetRawValue());
            if (it != offers.end() && it->second.expiresMs > GameTime::GetGameTimeMS().count() &&
                AcceptsOffer(intent, normalized, lowered, it->second.itemId))
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
    if (intent == "guild")
    {
        // The same checks as a guild invite of this player (GuildAcceptAction): a "why not" is never a lie.
        bool const wouldAccept = from->GetGuildId() &&
                                 botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, true, from, true);
        situation = GuildAnswer(to->GetGuildId() != 0, wouldAccept);
    }
    std::string place;
    if (intent == "where")
    {
        // The words after the "where" keyword, looked up as an area or a creature of the world.
        std::vector<std::string> keywords;
        for (Intent const& i : current->IntentsOf(BotLocale()))
            if (i.intent == "where")
                keywords = i.keywords;
        place = FindPlace(WhereSubjects(normalized, keywords), BotLocale());
        situation = place.empty() ? "wh_where_unknown" : "wh_where_known";
    }

    ReactOptions opt;
    opt.invite = invite;
    if (!place.empty())
        opt.extra["place"] = place;
    React(to, situation, from, "w" + std::to_string(to->GetGUID().GetCounter()), opt);
    return true;
}

bool SessionsEnabled() { return sPlayerbotAIConfig.coaLiveSessionsEnable; }

bool IsFishing(Player* bot)
{
    if (!bot)
        return false;
    std::lock_guard<std::mutex> guard(liveLock);
    return fishingUntil.count(bot->GetGUID().GetRawValue()) != 0;
}

bool IsGathering(Player* bot)
{
    if (!bot)
        return false;
    std::lock_guard<std::mutex> guard(liveLock);
    return gatherUntil.count(bot->GetGUID().GetRawValue()) != 0;
}

uint32 SessionSeconds() { return SessionsEnabled() ? CoaSessions::SessionSeconds(rng.between) : 0; }

uint32 OfflineSeconds() { return SessionsEnabled() ? CoaSessions::OfflineSeconds(rng.between) : 0; }

bool SayLine(Player* bot, std::string const& situation, std::unordered_map<std::string, std::string> const& slots)
{
    if (!Enabled() || !bot)
        return false;
    ReactOptions opt;
    opt.extra.insert(slots.begin(), slots.end());
    // Paced per place: the bot's zone.
    return React(bot, situation, nullptr, situation + "@z" + std::to_string(bot->GetZoneId()), opt);
}

bool SayLine(Player* bot, std::string const& situation, std::unordered_map<std::string, std::string> const& slots,
             ObjectGuid to)
{
    if (!Enabled() || !bot)
        return false;
    Player* subject = to ? ObjectAccessor::FindConnectedPlayer(to) : nullptr;
    if (to && !subject)
        return false;
    ReactOptions opt;
    opt.extra.insert(slots.begin(), slots.end());
    return React(bot, situation, subject, situation + "@w" + std::to_string(bot->GetGUID().GetCounter()), opt);
}

bool SayOffer(Player* bot, std::string const& situation, ObjectGuid item, uint32 price,
              std::unordered_map<std::string, std::string> const& slots)
{
    if (!Enabled() || !bot || !price)
        return false;
    Item* carried = bot->GetItemByGuid(item);
    if (!carried || carried->IsSoulBound() || !carried->GetTemplate())
        return false;
    ReactOptions opt;
    opt.extra.insert(slots.begin(), slots.end());
    if (!opt.extra.count("item"))
        opt.extra["item"] = ChatHelper::FormatItem(carried->GetTemplate(), carried->GetCount() > 1 ? carried->GetCount() : 0);
    if (!opt.extra.count("price"))
        opt.extra["price"] = FormatMoney(price);
    if (!React(bot, situation, nullptr, situation + "@z" + std::to_string(bot->GetZoneId()), opt))
        return false;
    std::lock_guard<std::mutex> guard(liveLock);
    offers[bot->GetGUID().GetRawValue()] = {true, item, carried->GetEntry(), price,
                                            uint64(GameTime::GetGameTimeMS().count()) + OFFER_MS};
    return true;
}

void RegisterWhisperHandler(std::string const& intent, WhisperHandler handler)
{
    std::lock_guard<std::mutex> guard(liveLock);
    whisperHandlers[intent] = std::move(handler);
}

bool MailCod(Player* bot, Player* to, ObjectGuid item, uint32 price)
{
    if (!bot || !to || !price)
        return false;
    Item* carried = bot->GetItemByGuid(item);
    if (!carried || carried->IsSoulBound() || carried->IsInTrade() || !carried->GetTemplate())
        return false;
    SendCod(bot, to, carried, price);
    return true;
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


using Acore::ChatCommands::ChatCommandTable;
using Acore::ChatCommands::Console;

// SquidBots Lite asks the soul of a bot (WORLD-PROTOCOLE-3.0 §0, §3): "SQBW\tSOUL?" whispered (addon) to the bot,
// or "SQBW\tSOUL?\t<name>" whispered to oneself. Only a bot of the asker's group, of the asker's account or under the
// asker's command; anyone else gets "notyours". Only this request word is stopped; any other SQBW word goes on to
// the other bricks' handlers.
std::mutex soulAddonLock;
CoaSoulAddon::Throttle soulAddonThrottle;

class CoaSoulAddonScript : public PlayerScript
{
public:
    CoaSoulAddonScript() : PlayerScript("CoaSoulAddonScript", {PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT}) {}

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg, Player* receiver) override
    {
        std::string name;
        if (type != CHAT_MSG_WHISPER || lang != LANG_ADDON || !player || !receiver ||
            !CoaSoulAddon::ParseRequest(msg, name))
            return true;
        // Off: as before, no answer (the addon takes the feature as off after 6 s).
        if (!sPlayerbotAIConfig.coaSoulEnable || !sPlayerbotAIConfig.coaSoulAddon)
            return true;

        bool const toSelf = receiver == player;
        Player* bot = toSelf ? (name.empty() ? nullptr : ObjectAccessor::FindPlayerByName(name, true)) : receiver;
        {
            std::lock_guard<std::mutex> guard(soulAddonLock);
            if (!soulAddonThrottle.Allow(player->GetGUID().GetRawValue(), bot ? bot->GetGUID().GetRawValue() : 0,
                                         GameTime::GetGameTimeMS().count()))
                return false;  // too many: dropped, no answer
        }

        PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        bool mine = false;
        if (botAI && bot != player)
        {
            Group* group = player->GetGroup();
            mine = (group && group == bot->GetGroup()) || botAI->GetMaster() == player ||
                   (bot->GetSession() && player->GetSession() &&
                    bot->GetSession()->GetAccountId() == player->GetSession()->GetAccountId());
        }

        // The answer comes from where the question went: the bot, or the asker itself.
        Player* from = toSelf ? player : receiver;
        std::string const line = mine ? CoaSoulAddon::Line(bot->GetName(), CoaSoul::Get(bot)) : CoaSoulAddon::NotYours();
        from->Whisper(std::string(CoaSoulAddon::PREFIX) + line, LANG_ADDON, player);
        from->Whisper(std::string(CoaSoulAddon::PREFIX) + "-", LANG_ADDON, player);
        return false;
    }
};

// A bot's full name in any GM command (".guild invite \"Tianor Bot\" ..."): the core turned it into "Tianorbot",
// nobody. The quoted name of a character becomes its guid, which every player argument takes, and the command runs
// again with it; any other command goes on untouched.
class CoaBotNameCommandScript : public AllCommandScript
{
public:
    CoaBotNameCommandScript() : AllCommandScript("CoaBotNameCommandScript", {ALLCOMMANDHOOK_ON_TRY_EXECUTE_COMMAND}) {}

    bool OnTryExecuteCommand(ChatHandler& handler, std::string_view cmdStr) override
    {
        if (cmdStr.find('"') == std::string_view::npos)
            return true;
        std::string const rewritten = CoaQuotedNamesToGuids(std::string(cmdStr), [](std::string const& name) -> uint64_t
        {
            ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(name);
            if (guid.IsEmpty())
            {
                // "tianor bot": each word with a capital, as the bots' names are written.
                std::string capitals = name;
                bool start = true;
                for (char& c : capitals)
                {
                    if (c == ' ')
                        start = true;
                    else if (start)
                    {
                        c = char(std::toupper(static_cast<unsigned char>(c)));
                        start = false;
                    }
                    else
                        c = char(std::tolower(static_cast<unsigned char>(c)));
                }
                guid = sCharacterCache->GetCharacterGuidByName(capitals);
            }
            return guid.IsEmpty() ? 0 : guid.GetCounter();
        });
        if (rewritten.empty())
            return true;
        Acore::ChatCommands::TryExecuteCommand(handler, rewritten);
        return false;  // done with the guid
    }
};

class CoaSoulCommandScript : public CommandScript
{
public:
    CoaSoulCommandScript() : CommandScript("CoaSoulCommandScript") {}

    // ".bot coatalents": the CoA random bots online, and those still short of their build's talents (a capped
    // rebuild in progress, or a difference to look at).
    static bool HandleCoaTalents(ChatHandler* handler)
    {
        uint32 bots = 0, incomplete = 0, entries = 0;
        std::string sample;
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            if (!player || !player->IsInWorld() || !IsRandom(player) || !IsAscensionCustomClassId(player->getClass()))
                continue;
            ++bots;
            if (uint32 const missing = CoaTalentsMissing(player))
            {
                ++incomplete;
                entries += missing;
                if (incomplete <= 5)
                    sample += " " + player->GetName() + "(" + std::to_string(missing) + ")";
            }
        }
        handler->PSendSysMessage("CoA talents: {} CoA random bots online, {} short of their build ({} entries):{}",
                                 bots, incomplete, entries, sample);
        return true;
    }

    // ".bot trainercheck": the random bots online that a trainer could still teach something (AiPlayerbot.CoaLive.
    // TrainerDedup must leave the same share as without it).
    static bool HandleTrainerCheck(ChatHandler* handler)
    {
        uint32 bots = 0, behind = 0, spells = 0;
        std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
        for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
        {
            if (!player || !player->IsInWorld() || !IsRandom(player))
                continue;
            ++bots;
            if (uint32 const n = PlayerbotFactory::CountTeachableSpells(player))
            {
                ++behind;
                spells += n;
            }
        }
        handler->PSendSysMessage("Trainers: {} random bots online, {} could still learn something ({} spells)",
                                 bots, behind, spells);
        return true;
    }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable soulTable = {
            {"set", HandleSoulSet, SEC_GAMEMASTER, Console::Yes},
            {"", HandleSoul, SEC_GAMEMASTER, Console::Yes},
        };
        static ChatCommandTable botTable = {
            {"soul", soulTable},
            {"coatalents", HandleCoaTalents, SEC_GAMEMASTER, Console::Yes},
            {"trainercheck", HandleTrainerCheck, SEC_GAMEMASTER, Console::Yes},
        };
        static ChatCommandTable commandTable = {
            {"bot", botTable},
        };
        return commandTable;
    }

    // "Kegarink Bot", Kegarink Bot or kegarink bot: quotes dropped, the name as written first, then each word with
    // a capital (normalizePlayerName lowers the surname: "Kegarink bot" is nobody).
    static std::string Unquoted(std::string text)
    {
        text = Acore::String::Trim(text);
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
            text = Acore::String::Trim(text.substr(1, text.size() - 2));
        return text;
    }

    static Player* Bot(ChatHandler* handler, std::string const& written)
    {
        std::string const name = Unquoted(written);
        Player* bot = name.empty() ? nullptr : ObjectAccessor::FindPlayerByName(name, true);
        if (!bot && !name.empty())
        {
            std::string capitals = name;
            bool start = true;
            for (char& c : capitals)
            {
                if (c == ' ')
                    start = true;
                else if (start)
                {
                    c = char(std::toupper(static_cast<unsigned char>(c)));
                    start = false;
                }
                else
                    c = char(std::tolower(static_cast<unsigned char>(c)));
            }
            bot = ObjectAccessor::FindPlayerByName(capitals, true);
        }
        if (!bot || !sRandomPlayerbotMgr.IsRandomBot(bot))
        {
            handler->PSendSysMessage("No random bot named {} online.", name);
            return nullptr;
        }
        return bot;
    }

    // .bot soul <name>: the soul of a random bot (drawn even when the souls are off, to see what it would be).
    static bool HandleSoul(ChatHandler* handler, char const* args)
    {
        std::string const name = args ? Acore::String::Trim(std::string(args)) : "";
        if (name.empty())
        {
            handler->SendSysMessage("Usage: .bot soul <name> | .bot soul set <name> <lazy|quester|wanderer|trader|pvp>");
            return true;
        }
        Player* bot = Bot(handler, name);
        if (!bot)
            return true;
        CoaSoul::Soul const s = CoaSoul::Get(bot);
        handler->PSendSysMessage("{}: {} (pace {}, sociability {}, curiosity {}, greed {}, aggression {}){}",
                                 bot->GetName(), CoaSoul::ArchetypeName(s.archetype), s.pace, s.sociability,
                                 s.curiosity, s.greed, s.aggression,
                                 CoaSoul::Enabled() ? "" : " - souls are off (AiPlayerbot.CoaSoul.Enable = 0)");
        return true;
    }

    // .bot soul set <name> <archetype>: for the benches.
    static bool HandleSoulSet(ChatHandler* handler, char const* args)
    {
        std::string const text = args ? std::string(args) : "";
        size_t const space = text.find_last_of(' ');
        if (space == std::string::npos)
        {
            handler->SendSysMessage("Usage: .bot soul set <name> <lazy|quester|wanderer|trader|pvp>");
            return true;
        }
        std::string const name = Unquoted(text.substr(0, space));
        std::string const kind = Acore::String::Trim(text.substr(space + 1));
        uint8 archetype = CoaSoul::ARCHETYPE_COUNT;
        for (uint8 a = 0; a < CoaSoul::ARCHETYPE_COUNT; ++a)
            if (kind == CoaSoul::ArchetypeName(a))
                archetype = a;
        if (archetype == CoaSoul::ARCHETYPE_COUNT)
        {
            handler->SendSysMessage("Archetype: lazy, quester, wanderer, trader or pvp.");
            return true;
        }
        Player* bot = Bot(handler, name);
        if (!bot)
            return true;
        CoaSoul::SetArchetype(bot, archetype);
        handler->PSendSysMessage("{} is now {}.", bot->GetName(), kind);
        return true;
    }
};

void AddSC_coa_live()
{
    new CoaSoulCommandScript();
    new CoaBotNameCommandScript();
    new CoaSoulAddonScript();
    new CoaLivePlayerScript();
    new CoaLiveWorldScript();
    new CoaLiveUnitScript();
}
