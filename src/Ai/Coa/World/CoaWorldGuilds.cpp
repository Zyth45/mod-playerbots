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
 *    Crafting on order for guildmates goes with CoA Economie's work.
 *
 * Lines are English for now; they move to IA MIND's phrase bank with the other CoA World lines.
 * Level-ups and loot come from map threads and are queued; the world thread speaks.
 * Config: AiPlayerbot.CoaWorld.Guilds.*, off by default.
 */

#include "CoaWorldGuilds.h"

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
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
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
    struct Event
    {
        enum Kind : uint8 { LEVEL, LOOT, LOGIN } kind;
        ObjectGuid guid;
        uint32 value;  // level, or item entry
    };

    std::mutex g_lock;
    std::vector<Event> g_events;

    void Push(Event e)
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
            for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
                if (bot && bot->IsInWorld() && bot->GetGuildId() == guildId && bot->GetGUID() != notGuid)
                    found.push_back(bot);
            return found.empty() ? nullptr : found[urand(0, found.size() - 1)];
        }

        void Say(Player* speaker, std::string text, time_t at)
        {
            if (speaker && speaker->GetGuildId())
                _speeches.push_back({ speaker->GetGUID(), speaker->GetGuildId(), std::move(text), at });
        }

        void HandleEvents(time_t now)
        {
            std::vector<Event> events;
            {
                std::lock_guard<std::mutex> lock(g_lock);
                events.swap(g_events);
            }

            for (Event const& e : events)
            {
                Player* player = ObjectAccessor::FindPlayer(e.guid);
                if (!player || !player->IsInWorld() || !player->GetGuildId())
                    continue;

                switch (e.kind)
                {
                    case Event::LEVEL:
                    {
                        static char const* const ding[] = { "ding {}", "{}, finally", "ding! {} now", "and {}. slowly getting there" };
                        static char const* const gz[] = { "gz", "grats!", "gz {}", "nice, gz" };
                        Say(player, Acore::StringFormat(Pick(ding), e.value), now + urand(1, 4));
                        if (Player* other = OnlineGuildBot(player->GetGuildId(), player->GetGUID()))
                            Say(other, Acore::StringFormat(Pick(gz), player->GetName()), now + urand(6, 15));
                        break;
                    }
                    case Event::LOOT:
                    {
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
                    case Event::LOGIN:
                        Welcome(player, now);
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
            if (roll_chance_i(60))
                Say(bot, Acore::StringFormat(Pick(hi), player->GetName()), now + urand(4, 12));

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
            Say(bot, Acore::StringFormat("{}, sent you a few potions", player->GetName()), now + urand(20, 40));
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
            for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
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
                guild->HandleSetMOTD(bot->GetSession(), Pick(motds));
            }
        }

        // A member of a bot guild that is not full calls for members in General.
        void Recruit()
        {
            std::vector<Player*> callers;
            for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
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
            if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
                botAI->SayToChannel(Acore::StringFormat(Pick(calls), guild->GetName()), GENERAL);
        }

        std::vector<Speech> _speeches;
        std::unordered_map<uint32, time_t> _lastMotd;
        std::unordered_map<uint32, time_t> _lastGift;
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
        : PlayerScript("CoaWorldGuildsPlayerScript", { PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_LOOT_ITEM, PLAYERHOOK_ON_LOGIN })
    {
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!settings.enabled || !settings.chat || !player || !player->GetGuildId() || !IsBot(player))
            return;
        uint8 level = player->GetLevel();
        if (level % 10 == 0 || level >= 55)
            Push({ Event::LEVEL, player->GetGUID(), level });
    }

    void OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/) override
    {
        if (!settings.enabled || !settings.chat || !player || !item || !player->GetGuildId() || !IsBot(player))
            return;
        if (item->GetTemplate()->Quality >= ITEM_QUALITY_RARE)
            Push({ Event::LOOT, player->GetGUID(), item->GetEntry() });
    }

    void OnPlayerLogin(Player* player) override
    {
        if (!settings.enabled || !player || !player->GetGuildId() || IsBot(player))
            return;
        Push({ Event::LOGIN, player->GetGUID(), 0 });
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
