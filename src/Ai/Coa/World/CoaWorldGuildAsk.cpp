/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: a guild for the player who asks (AiPlayerbot.CoaWorld.Guilds.AnswerAsk, under Guilds.Enable).
 *
 * A real player without a guild asks for one aloud (say, yell) or in a public channel (General, Trade,
 * LookingForGroup, a world channel such as "Ascension"): "LF guild", "any guild?", "cherche une guilde"... An
 * officer bot of a bot guild of the player's side, with the right to invite, answers a few
 * seconds later (aloud when it stands near, else by whisper) and sends the guild invitation. One guild at a time:
 * nothing while the player has an invitation or an answer on its way, and a rest per player (Guilds.AnswerAskMinutes)
 * whatever the player did with the last one. The core's own handler sends the invitation
 * (Guild::HandleInviteMember: ignore list, side, rights).
 * Lines from IA MIND's bank (guild_ask_reply aloud, guild_ask_reply_w whispered), no text of our own: without a
 * line (no bank, or the situation has none) the officer says nothing and sends no invitation.
 * A whisper is an ask too: any whisper to the bot that called for members in General ("/w me") in the last 15
 * minutes, or a whisper asking for a guild in words to an officer bot; that bot answers, by whisper.
 * A bot guild takes a real player up to the game's limit (Guild.MemberLimit, else 1000): RandomBotGuildSizeMax is
 * only the bots' own fill.
 * Chat hooks queue the ask; the world thread picks the officer and acts.
 */

#include "CoaWorldGuildAsk.h"

#include "CoaWorldBots.h"
#include "CoaWorldGuildAskRules.h"

#include "Channel.h"
#include "CoaLive.h"
#include "Config.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
using namespace CoaWorld;
using namespace CoaWorldGuildAskRules;

constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.Guilds.";
constexpr uint32 UPDATE_MS = 1 * IN_MILLISECONDS;
constexpr float NEAR_YARDS = 30.0f;  // the officer answers aloud within this, else by whisper

struct Settings
{
    bool enabled = false;       // Guilds.Enable and Guilds.AnswerAsk
    uint32 restSeconds = 1800;  // Guilds.AnswerAskMinutes
};
Settings settings;

bool IsHuman(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot() && !GET_PLAYERBOT_AI(player) &&
           !CoaWorld::IsTestDriver(player);
}

// A random bot of its own (no real-player master).
bool OwnRandomBot(Player* bot)
{
    PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!botAI || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;
    Player* master = botAI->GetMaster();
    return !(master && IsHuman(master));
}

// An officer of a bot guild (its leader a random bot) that may invite, with a place under the game's own limit.
// AiPlayerbot.RandomBotGuildSizeMax is the bots' own fill (every bot guild reaches it): a real player still has one.
Guild* OfficerGuild(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || !bot->GetGuildId() || !OwnRandomBot(bot) ||
        IsBorrowedBot(bot->GetGUID()))
        return nullptr;
    Guild* guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    if (!guild || !sRandomPlayerbotMgr.IsRandomBot(guild->GetLeaderGUID().GetCounter()) ||
        !guild->HasRankRight(bot, GR_RIGHT_INVITE) ||
        !GuildHasPlace(guild->GetMemberSize(), sWorld->getIntConfig(CONFIG_GUILD_MEMBER_LIMIT)))
        return nullptr;
    return guild;
}

struct Ask
{
    ObjectGuid player, officer;  // officer: the bot the player whispered, else empty
};

struct Answer
{
    ObjectGuid player, officer;
    time_t speakAt = 0, inviteAt = 0;
    bool whisper = false;  // the player whispered: the answer is a whisper
    bool spoke = false;
};

class GuildAsk
{
public:
    static GuildAsk& Instance()
    {
        static GuildAsk instance;
        return instance;
    }

    // Any thread (chat hooks): the world thread looks at it.
    void Asked(Player* player, ObjectGuid officer = ObjectGuid::Empty)
    {
        std::lock_guard<std::mutex> guard(_lock);
        _asks.push_back({ player->GetGUID(), officer });
    }

    // The world thread (the call in General).
    void Called(Player* bot)
    {
        std::lock_guard<std::mutex> guard(_lock);
        time_t const now = time(nullptr);
        for (auto it = _calls.begin(); it != _calls.end();)
            it = now - it->second > time_t(RECRUIT_WHISPER_SECONDS) ? _calls.erase(it) : std::next(it);
        _calls[bot->GetGUID().GetCounter()] = now;
    }

    // Any thread (whisper hook).
    time_t CalledAt(Player* bot)
    {
        std::lock_guard<std::mutex> guard(_lock);
        auto it = _calls.find(bot->GetGUID().GetCounter());
        return it == _calls.end() ? 0 : it->second;
    }

    void Update(uint32 diff)
    {
        _timer += diff;
        if (_timer < UPDATE_MS)
            return;
        _timer = 0;
        time_t const now = time(nullptr);

        std::vector<Ask> asks;
        {
            std::lock_guard<std::mutex> guard(_lock);
            asks.swap(_asks);
        }
        for (Ask const& ask : asks)
            Hear(ask, now);

        for (auto it = _answers.begin(); it != _answers.end();)
            it = Play(*it, now) ? _answers.erase(it) : std::next(it);
    }

private:
    bool Pending(ObjectGuid player) const
    {
        return std::any_of(_answers.begin(), _answers.end(), [&player](Answer const& a) { return a.player == player; });
    }

    void Hear(Ask const& ask, time_t now)
    {
        ObjectGuid const guid = ask.player;
        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (!IsHuman(player) || !player->IsInWorld())
            return;
        time_t& last = _last[guid.GetCounter()];
        if (!MayAnswer(Pending(guid), player->GetGuildId() != 0, player->GetGuildIdInvited() != 0, last, now,
                       settings.restSeconds))
            return;

        // The bot the player whispered when it may invite; else the nearest officer of the player's side: near
        // (aloud), else in the same zone, else anywhere.
        std::vector<Player*> best;
        int bestRank = 3;
        Player* whispered = ask.officer ? ObjectAccessor::FindConnectedPlayer(ask.officer) : nullptr;
        if (whispered && whispered->GetTeamId() == player->GetTeamId() && OfficerGuild(whispered))
            best.push_back(whispered);
        for (auto const& [botGuid, bot] : RandomBots())
        {
            if (!best.empty() && best.front() == whispered)
                break;
            if (!bot || bot->GetTeamId() != player->GetTeamId() || !OfficerGuild(bot))
                continue;
            int rank = 2;
            if (bot->GetMapId() == player->GetMapId() && bot->GetZoneId() == player->GetZoneId())
                rank = bot->IsWithinDistInMap(player, NEAR_YARDS) ? 0 : 1;
            if (rank < bestRank)
            {
                bestRank = rank;
                best.clear();
            }
            if (rank == bestRank)
                best.push_back(bot);
        }
        if (best.empty())
            return;

        last = now;
        Player* officer = best[urand(0, best.size() - 1)];
        Answer answer;
        answer.player = guid;
        answer.officer = officer->GetGUID();
        answer.speakAt = now + urand(4, 12);  // reads, thinks, types
        answer.inviteAt = answer.speakAt + urand(3, 8);
        answer.whisper = !ask.officer.IsEmpty();
        _answers.push_back(answer);
    }

    // True when done (invitation sent or dropped).
    bool Play(Answer& answer, time_t now)
    {
        if (now < answer.speakAt)
            return false;
        Player* player = ObjectAccessor::FindConnectedPlayer(answer.player);
        Player* officer = ObjectAccessor::FindConnectedPlayer(answer.officer);
        if (!IsHuman(player) || !player->IsInWorld() || player->GetGuildId() || player->GetGuildIdInvited())
            return true;
        Guild* guild = OfficerGuild(officer);
        if (!guild)
            return true;

        if (!answer.spoke)
        {
            answer.spoke = true;
            Slots const slots = { { "player", player->GetName() }, { "guild", guild->GetName() } };
            bool const nearby = !answer.whisper && officer->GetMap() == player->GetMap() &&
                                officer->IsWithinDistInMap(player, NEAR_YARDS);
            bool const said = nearby ? BotLine(officer, "guild_ask_reply", slots)
                                     : CoaLive::SayLine(officer, "guild_ask_reply_w", slots, player->GetGUID());
            if (!said)
                return true;  // no line: no invitation out of the blue
        }
        if (now < answer.inviteAt)
            return false;

        guild->HandleInviteMember(officer->GetSession(), player->GetName());
        if (player->GetGuildIdInvited() == guild->GetId())
            LogEvent("guild", officer->GetName(),
                     Acore::StringFormat("invited {} to <{}> (the player asked for a guild)", player->GetName(),
                                         guild->GetName()));
        return true;
    }

    std::mutex _lock;
    std::vector<Ask> _asks;
    std::unordered_map<uint32, time_t> _calls;  // bot -> its last call for members in General
    std::vector<Answer> _answers;
    std::unordered_map<uint32, time_t> _last;  // real player -> last ask answered
    uint32 _timer = 0;
};

void LoadSettings()
{
    auto key = [](char const* name) { return std::string(PREFIX) + name; };
    settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), true, false) &&
                       sConfigMgr->GetOption<bool>(key("AnswerAsk"), ANSWER_ASK_DEFAULT, false);
    settings.restSeconds = std::max<uint32>(1, sConfigMgr->GetOption<uint32>(key("AnswerAskMinutes"), 30, false)) * MINUTE;
}

// A public channel: the game's own (General, Trade, LookingForGroup...) or a world channel ("Ascension" on CoA).
bool PublicChannel(Channel* channel)
{
    if (!channel)
        return false;
    if (channel->IsConstant() || channel->IsLFG())
        return true;
    return PublicChannelName(channel->GetName(), sPlayerbotAIConfig.broadcastWorldChannelName);
}

void Heard(Player* player, uint32 lang, std::string const& msg)
{
    if (!settings.enabled || lang == LANG_ADDON || !IsHuman(player) || player->GetGuildId() || player->GetGuildIdInvited())
        return;
    if (AsksForGuild(msg))
        GuildAsk::Instance().Asked(player);
}

// A whisper to a bot: an ask when it called for members a moment ago ("/w me"), or when the words ask for a guild.
void Whispered(Player* player, uint32 lang, std::string const& msg, Player* receiver)
{
    if (!settings.enabled || lang == LANG_ADDON || !receiver || IsHuman(receiver) || !GET_PLAYERBOT_AI(receiver) ||
        !IsHuman(player) || player->GetGuildId() || player->GetGuildIdInvited())
        return;
    if (WhisperAsks(GuildAsk::Instance().CalledAt(receiver), time(nullptr), AsksForGuild(msg)))
        GuildAsk::Instance().Asked(player, receiver->GetGUID());
}

class CoaWorldGuildAskWorldScript : public WorldScript
{
public:
    CoaWorldGuildAskWorldScript()
        : WorldScript("CoaWorldGuildAskWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
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
            GuildAsk::Instance().Update(diff);
    }
};

class CoaWorldGuildAskPlayerScript : public PlayerScript
{
public:
    CoaWorldGuildAskPlayerScript()
        : PlayerScript("CoaWorldGuildAskPlayerScript",
                       { PLAYERHOOK_CAN_PLAYER_USE_CHAT, PLAYERHOOK_CAN_PLAYER_USE_CHANNEL_CHAT,
                         PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT })
    {
    }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg) override
    {
        if (type == CHAT_MSG_SAY || type == CHAT_MSG_YELL)
            Heard(player, lang, msg);
        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg, Player* receiver) override
    {
        Whispered(player, lang, msg, receiver);
        return true;
    }

    bool OnPlayerCanUseChat(Player* player, uint32 /*type*/, uint32 lang, std::string& msg, Channel* channel) override
    {
        if (PublicChannel(channel))
            Heard(player, lang, msg);
        return true;
    }
};
}  // namespace

Guild* CoaWorldGuildAsk::InvitingGuild(Player* bot) { return OfficerGuild(bot); }

void CoaWorldGuildAsk::RecruitCall(Player* bot)
{
    if (bot)
        GuildAsk::Instance().Called(bot);
}

void AddSC_coa_world_guild_ask()
{
    new CoaWorldGuildAskWorldScript();
    new CoaWorldGuildAskPlayerScript();
}
