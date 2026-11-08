/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CoaLive.h"

#include "CellImpl.h"
#include "Chat.h"
#include "CoaLiveRules.h"
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
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "Util.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <cstdlib>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

using namespace CoaLive;

namespace
{
constexpr uint32 SCAN_MS = 5000;        // how often a bot looks around
constexpr float HEAR_RANGE = 30.0f;      // a real player this close hears the bot's /say
constexpr uint32 GREET_PLAYER_CD_S = 3600;  // a bot greets the same player once an hour
constexpr size_t QUEUE_MAX = 4096;

struct Pending
{
    uint64 dueMs = 0;
    ObjectGuid bot;
    ObjectGuid subject;  // the player the bot reacts to (emote target, whisper, invite)
    uint8 channel = CH_NEAR;
    uint32 emote = 0;
    std::string text;
    bool invite = false;
};

std::mutex liveLock;
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
bool React(Player* bot, std::string const& situation, Player* subject, std::string const& scope, Unit* boss = nullptr,
           uint32 perSubjectCdS = 0, bool invite = false)
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
    if (!rule || !rng.Roll(ReactPct(*rule, -1, 1.0f)))
        return false;

    uint8 const locale = BotLocale();
    std::map<std::string, std::string> const values = Values(bot, subject, boss, locale);
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
    p.dueMs = nowMs + TypingMs(*rule, text.size(), text.empty() ? 0 : MsPerChar(bot), rng);
    p.bot = bot->GetGUID();
    p.subject = subject ? subject->GetGUID() : ObjectGuid::Empty;
    p.channel = channel;
    p.emote = pick.line->emote;
    p.text = text;
    p.invite = invite;
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

// World thread: plays what is due.
void Play(uint64 nowMs)
{
    std::vector<Pending> due;
    {
        std::lock_guard<std::mutex> guard(liveLock);
        if (liveQueue.empty())
            return;
        auto split = std::partition(liveQueue.begin(), liveQueue.end(), [nowMs](Pending const& p) { return p.dueMs > nowMs; });
        due.assign(std::make_move_iterator(split), std::make_move_iterator(liveQueue.end()));
        liveQueue.erase(split, liveQueue.end());
    }

    for (Pending const& p : due)
    {
        Player* bot = ObjectAccessor::FindPlayer(p.bot);
        if (!bot || !bot->IsInWorld() || !bot->GetSession() || !bot->IsAlive())
            continue;
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
                default:
                    break;
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

    std::list<Player*> const near = RealPlayersNear(bot, HEAR_RANGE);
    if (near.empty())
        return;

    float const greetRange = static_cast<float>(sPlayerbotAIConfig.coaLiveGreetRange);
    for (Player* player : near)
    {
        if (player->GetTeamId() != bot->GetTeamId() || !bot->IsWithinDist(player, greetRange))
            continue;
        // Only one bot greets a player at a time (the scope is the player).
        if (React(bot, "greet_pass", player, "p" + std::to_string(player->GetGUID().GetCounter()), nullptr,
                  GREET_PLAYER_CD_S))
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
                       {PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_DUEL_END, PLAYERHOOK_ON_PLAYER_ENTER_COMBAT})
    {
    }

    // A bot dings in front of a real player; a real player dings next to random bots or in their group.
    void OnPlayerLevelChanged(Player* player, uint8 oldLevel) override
    {
        if (!Enabled() || !player || player->GetLevel() <= oldLevel)
            return;

        std::string const scope = "l" + std::to_string(player->GetGUID().GetCounter());
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

    // A bot pulls a boss in a group with a real player.
    void OnPlayerEnterCombat(Player* player, Unit* enemy) override
    {
        if (!Enabled() || !player || !enemy || !IsRandom(player))
            return;
        Creature* creature = enemy->ToCreature();
        if (!creature || !(creature->IsDungeonBoss() || creature->isWorldBoss()) || !GroupHasRealPlayer(player))
            return;
        React(player, "boss_pull", nullptr, "g" + std::to_string(player->GetGroup()->GetGUID().GetCounter()), creature);
    }
};

class CoaLiveWorldScript : public WorldScript
{
public:
    CoaLiveWorldScript() : WorldScript("CoaLiveWorldScript", {WORLDHOOK_ON_UPDATE}) {}

    void OnUpdate(uint32 /*diff*/) override
    {
        if (Enabled())
            Play(GameTime::GetGameTimeMS().count());
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

    React(to, situation, from, "w" + std::to_string(to->GetGUID().GetCounter()), nullptr, 0, invite);
    return true;
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
