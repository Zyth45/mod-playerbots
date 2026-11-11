/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: guild charters (AiPlayerbot.CoaWorld.Guilds.Charters, under Guilds.Enable; user's idea, 09/11).
 *
 *  1. A real player's charter: a random bot of its own without a guild that is offered the charter signs it after a
 *     few seconds, refuses now and then, with a word (PetitionSignAction asks CoaWorldCharterOffered). A player
 *     holding a charter who asks aloud for signatures ("sign", "charter"...) gets the guildless bots around to sign
 *     it, one after the other, up to the count required. Always allowed, whatever the floor.
 *  2. A bot's charter: now and then a free guildless bot in a capital walks to the guild master, buys a charter, has
 *     the guildless bots passing by sign it (rarely asks a real player near, once an hour per player at most) and
 *     founds its guild, a living bot guild.
 *  Floor (Guilds.UnguildedPercent, 30): no bot charter and no bot joining a bot guild (PlayerbotGuildMgr) while the
 *  random bots without a guild would fall under that share, so players always meet guildless bots.
 * The core's own handlers do the work (CMSG_PETITION_BUY, _SIGN, CMSG_OFFER_PETITION, CMSG_TURN_IN_PETITION).
 * World thread. Lines from IA MIND's bank (charter_*), English fallback.
 */

#include "CoaWorldCharters.h"

#include "CoaWorldBots.h"
#include "CoaWorldCharterRules.h"
#include "CoaWorldGuilds.h"

#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "Item.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PetitionMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

namespace
{
using namespace CoaWorld;
using namespace CoaWorldCharterRules;

constexpr char const* PREFIX = "AiPlayerbot.CoaWorld.Guilds.";
constexpr uint32 UPDATE_MS = 5 * IN_MILLISECONDS;
constexpr uint32 BOT_OWNER_CHARTER = 600;  // + side
constexpr uint32 MAX_MINUTES = 90;

struct Settings
{
    bool enabled = false;    // Guilds.Enable and Guilds.Charters
    uint32 floorPct = 30;    // Guilds.UnguildedPercent
    uint32 everyMinutes = 60;  // Guilds.CharterMinutes, per side
};
Settings settings;
std::atomic<uint32> g_unguilded{ 0 };
std::atomic<uint32> g_total{ 0 };

bool IsHuman(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsBot() && !GET_PLAYERBOT_AI(player);
}

// A random bot of its own (no real-player master), the only one a stranger's charter may reach.
bool OwnRandomBot(Player* bot)
{
    PlayerbotAI* botAI = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!botAI || !sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;
    Player* master = botAI->GetMaster();
    return !(master && IsHuman(master));
}

bool Guildless(Player* bot) { return !bot->GetGuildId() && !bot->GetGuildIdInvited(); }

void Line(Player* bot, char const* situation, Slots const& slots, std::vector<char const*> const& fallback,
          std::vector<char const*> const& fallbackFr)
{
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || BotLine(bot, situation, slots) || !BotChatOn())
        return;
    auto const& pool = BotsFrench() ? fallbackFr : fallback;
    std::string text = pool[urand(0, pool.size() - 1)];
    auto const who = slots.find("player");
    if (who != slots.end())
        text = Acore::StringFormat(text, who->second);
    botAI->Say(text);
}

void Sign(Player* signer, ObjectGuid petition)
{
    WorldPacket data(CMSG_PETITION_SIGN, 9);
    data << petition << uint8(0);
    signer->GetSession()->HandlePetitionSignOpcode(data);
}

uint32 Signed(ObjectGuid petition)
{
    Signatures const* signatures = sPetitionMgr->GetSignature(petition);
    return signatures ? uint32(signatures->signatureMap.size()) : 0;
}

uint32 Required() { return sWorld->getIntConfig(CONFIG_MIN_PETITION_SIGNS); }

struct PendingSign
{
    ObjectGuid signer, petition, owner;
    time_t at;
    bool offered;  // offered by the owner (a refusal declines it)
};

struct BotCharter
{
    ObjectGuid founder, npc, petition;
    WorldLocation npcAt;
    time_t deadline = 0, nextAsk = 0;
};

class Charters
{
public:
    static Charters& Instance()
    {
        static Charters instance;
        return instance;
    }

    void Queue(PendingSign sign)
    {
        std::lock_guard<std::mutex> lock(_lock);
        _pending.push_back(sign);
    }

    void Update(uint32 diff)
    {
        _timer += diff;
        if (_timer < UPDATE_MS)
            return;
        _timer = 0;
        time_t const now = time(nullptr);
        Count();
        Signs(now);
        if (!settings.enabled)
            return;
        for (uint8 side = 0; side < 2; ++side)
        {
            if (_charters[side].founder)
                Follow(side, now);
            else if (now >= _next[side])
            {
                _next[side] = now + settings.everyMinutes * MINUTE;
                Begin(side, now);
            }
        }
    }

    // A real player asks aloud: the guildless bots around sign, one after the other.
    void Asked(Player* player, time_t now)
    {
        Item* charter = player->GetItemByEntry(GUILD_CHARTER);
        if (!charter || player->GetGuildId())
            return;
        auto& last = _asked[player->GetGUID().GetCounter()];
        if (last && now - last < MINUTE)
            return;
        last = now;
        ObjectGuid const petition = charter->GetGUID();
        uint32 const required = Required();
        uint32 have = Signed(petition);
        time_t at = now + urand(3, 8);
        for (auto const& [guid, bot] : RandomBots())
        {
            if (have >= required)
                break;
            if (!bot || !bot->IsInWorld() || bot->GetMapId() != player->GetMapId() || !bot->IsWithinDistInMap(player, 25.0f) ||
                bot->GetTeamId() != player->GetTeamId() || !OwnRandomBot(bot) || !Guildless(bot) || bot->IsInCombat())
                continue;
            Queue({ bot->GetGUID(), petition, player->GetGUID(), at, false });
            at += urand(3, 10);
            ++have;
        }
    }

private:
    void Count()
    {
        uint32 total = 0, unguilded = 0;
        for (auto const& [guid, bot] : RandomBots())
            if (bot && bot->IsInWorld())
            {
                ++total;
                if (!bot->GetGuildId())
                    ++unguilded;
            }
        g_total = total;
        g_unguilded = unguilded;
    }

    void Signs(time_t now)
    {
        std::vector<PendingSign> due;
        {
            std::lock_guard<std::mutex> lock(_lock);
            for (auto itr = _pending.begin(); itr != _pending.end();)
                if (itr->at <= now)
                {
                    due.push_back(*itr);
                    itr = _pending.erase(itr);
                }
                else
                    ++itr;
        }
        for (PendingSign const& sign : due)
        {
            Player* bot = ObjectAccessor::FindPlayer(sign.signer);
            Player* owner = ObjectAccessor::FindConnectedPlayer(sign.owner);
            if (!bot || !bot->IsInWorld() || !Guildless(bot) || !sPetitionMgr->GetPetition(sign.petition))
                continue;
            Slots const slots = { { "player", owner ? owner->GetName() : std::string() } };
            if (sign.offered && urand(1, 100) <= 10)
            {
                WorldPacket data(MSG_PETITION_DECLINE);
                data << sign.petition;
                bot->GetSession()->HandlePetitionDeclineOpcode(data);
                Line(bot, "charter_refuse", slots, { "sorry, not looking for a guild", "nah, I'm fine solo for now" },
                     { "d\u00e9sol\u00e9, je cherche pas de guilde", "non merci, je suis bien solo pour l'instant" });
                continue;
            }
            Sign(bot, sign.petition);
            if (urand(0, 2) == 0 || sign.offered)
                Line(bot, "charter_sign", slots, { "signed, good luck {}", "sure, signed", "there you go", "signed! gl with the guild" },
                     { "sign\u00e9, bonne chance {}", "ok, sign\u00e9", "voil\u00e0", "sign\u00e9 ! bonne chance avec la guilde" });
        }
    }

    // The guild masters' entries (npcflag petitioner), found once.
    std::vector<uint32> const& Petitioners()
    {
        if (_petitionersRead)
            return _petitioners;
        _petitionersRead = true;
        for (auto const& [entry, cInfo] : *sObjectMgr->GetCreatureTemplates())
            if (cInfo.npcflag & UNIT_NPC_FLAG_PETITIONER)
                _petitioners.push_back(entry);
        return _petitioners;
    }

    bool InCapital(Player* bot)
    {
        AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId());
        return zone && (zone->flags & AREA_FLAG_CAPITAL);
    }

    void Begin(uint8 side, time_t now)
    {
        if (!AboveFloor(g_unguilded, g_total, Required() + 1, settings.floorPct))
            return;
        uint32 const cost = sWorld->getIntConfig(CONFIG_CHARTER_COST_GUILD);
        std::vector<Player*> free;
        for (auto const& [guid, bot] : RandomBots())
            if (bot && bot->GetTeamId() == side && IsFreeBot(bot) && Guildless(bot) && bot->GetLevel() >= 10 &&
                bot->GetMoney() >= cost && !bot->GetItemByEntry(GUILD_CHARTER) && InCapital(bot))
                free.push_back(bot);
        std::shuffle(free.begin(), free.end(), std::mt19937(urand(0, 0x7FFFFFFF)));
        for (Player* bot : free)
        {
            Creature* npc = nullptr;
            for (uint32 entry : Petitioners())
                if (Creature* found = bot->FindNearestCreature(entry, 200.0f))
                    if (!npc || bot->GetDistance(found) < bot->GetDistance(npc))
                        npc = found;
            if (!npc)
                continue;
            BorrowRequest request;
            request.mapId = npc->GetMapId();
            request.goal = npc->GetPosition();
            request.rally = npc->GetPosition();
            request.nearRadius = 250.0f;
            request.teleport = false;
            if (!SendBot(BOT_OWNER_CHARTER + side, bot, request))
                continue;
            BotCharter& charter = _charters[side];
            charter = BotCharter();
            charter.founder = bot->GetGUID();
            charter.npc = npc->GetGUID();
            charter.npcAt = WorldLocation(npc->GetMapId(), npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ());
            charter.deadline = now + MAX_MINUTES * MINUTE;
            LOG_INFO("module", "CoA World: {} goes to buy a guild charter", bot->GetName());
            return;
        }
    }

    void End(uint8 side)
    {
        ReleaseBots(BOT_OWNER_CHARTER + side, false);
        _charters[side] = BotCharter();
    }

    void Follow(uint8 side, time_t now)
    {
        BotCharter& charter = _charters[side];
        Player* founder = ObjectAccessor::FindPlayer(charter.founder);
        if (!founder || !founder->IsInWorld() || founder->GetGuildId() || now >= charter.deadline)
        {
            End(side);
            return;
        }

        // 1. At the guild master: buy the charter (as BuyPetitionAction).
        if (!charter.petition)
        {
            Creature* npc = founder->GetNPCIfCanInteractWith(charter.npc, UNIT_NPC_FLAG_PETITIONER);
            if (!npc)
                return;  // still walking
            std::string const name = RandomPlayerbotFactory::CreateRandomGuildName();
            if (name.empty())
            {
                End(side);
                return;
            }
            WorldPacket data(CMSG_PETITION_BUY);
            data << npc->GetGUID() << uint32(0) << uint64(0) << name.c_str() << std::string("");
            for (int i = 0; i < 7; ++i)
                data << uint32(0);
            data << uint16(0) << uint32(0) << uint32(0) << uint32(0);
            for (uint8 i = 0; i < 10; ++i)
                data << std::string("");
            data << uint32(0) << uint32(0);
            founder->GetSession()->HandlePetitionBuyOpcode(data);
            Item* item = founder->GetItemByEntry(GUILD_CHARTER);
            if (!item)
            {
                End(side);
                return;
            }
            charter.petition = item->GetGUID();
            Line(founder, "charter_ask", {}, { "starting a guild, need signatures, anyone?", "who wants to sign my guild charter?",
                                               "need a few signatures for a new guild!" },
                 { "je monte une guilde, il me faut des signatures, quelqu'un ?", "qui veut signer ma charte de guilde ?",
                   "il me faut quelques signatures pour une nouvelle guilde !" });
            return;
        }

        // 2. Signatures, then the guild.
        if (Signed(charter.petition) >= Required())
        {
            WorldPacket data(CMSG_TURN_IN_PETITION, 8);
            data << charter.petition;
            founder->GetSession()->HandleTurnInPetitionOpcode(data);
            if (founder->GetGuildId())
            {
                Line(founder, "charter_founded", {}, { "guild founded, thanks all!", "we have a guild! ty everyone" },
                     { "guilde fond\u00e9e, merci \u00e0 tous !", "on a une guilde ! merci tout le monde" });
                LogEvent("guild", founder->GetName(), "founded a guild with a charter");
            }
            End(side);
            return;
        }
        if (now < charter.nextAsk)
            return;
        charter.nextAsk = now + urand(40, 120);

        // A guildless bot passing by, while the floor allows it.
        if (AboveFloor(g_unguilded, g_total, 1, settings.floorPct))
            for (auto const& [guid, bot] : RandomBots())
                if (bot && bot != founder && bot->IsInWorld() && bot->GetMapId() == founder->GetMapId() &&
                    bot->IsWithinDistInMap(founder, 40.0f) && bot->GetTeamId() == founder->GetTeamId() && Guildless(bot) &&
                    OwnRandomBot(bot) && !bot->IsInCombat() && !IsBorrowedBot(bot->GetGUID()))
                {
                    Signatures const* signatures = sPetitionMgr->GetSignature(charter.petition);
                    if (signatures && signatures->signatureMap.count(bot->GetGUID()))
                        continue;
                    Queue({ bot->GetGUID(), charter.petition, charter.founder, now + urand(2, 6), false });
                    break;
                }

        // Rarely a real player near, once an hour each at most.
        if (urand(1, 100) <= 10)
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            {
                if (!IsHuman(player) || !player->IsInWorld() || player->GetMapId() != founder->GetMapId() ||
                    !player->IsWithinDistInMap(founder, 15.0f) || player->GetTeamId() != founder->GetTeamId() ||
                    player->GetGuildId() || CoaWorldJustLoggedIn(player))
                    continue;
                time_t& last = _offered[player->GetGUID().GetCounter()];
                if (last && now - last < HOUR)
                    continue;
                last = now;
                WorldPacket data(CMSG_OFFER_PETITION, 20);
                data << uint32(0) << charter.petition << player->GetGUID();
                founder->GetSession()->HandleOfferPetitionOpcode(data);
                Line(founder, "charter_ask_player", { { "player", player->GetName() } },
                     { "hey {}, would you sign my guild charter?", "{}, mind signing my charter?" },
                     { "salut {}, tu voudrais signer ma charte de guilde ?", "{}, \u00e7a te d\u00e9range de signer ma charte ?" });
                break;
            }
    }

    std::mutex _lock;
    std::vector<PendingSign> _pending;
    std::unordered_map<uint32, time_t> _asked;    // real player -> last ask aloud
    std::unordered_map<uint32, time_t> _offered;  // real player -> last bot offer
    BotCharter _charters[2];
    time_t _next[2] = { 0, 0 };
    std::vector<uint32> _petitioners;
    bool _petitionersRead = false;
    uint32 _timer = 0;
};

void LoadSettings()
{
    auto key = [](char const* name) { return std::string(PREFIX) + name; };
    settings.enabled = sConfigMgr->GetOption<bool>(key("Enable"), false, false) &&
                       sConfigMgr->GetOption<bool>(key("Charters"), false, false);
    settings.floorPct = std::min<uint32>(90, sConfigMgr->GetOption<uint32>(key("UnguildedPercent"), 30, false));
    settings.everyMinutes = std::max<uint32>(5, sConfigMgr->GetOption<uint32>(key("CharterMinutes"), 60, false));
}

class CoaWorldChartersWorldScript : public WorldScript
{
public:
    CoaWorldChartersWorldScript()
        : WorldScript("CoaWorldChartersWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE })
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
            Charters::Instance().Update(diff);
    }
};

class CoaWorldChartersPlayerScript : public PlayerScript
{
public:
    CoaWorldChartersPlayerScript() : PlayerScript("CoaWorldChartersPlayerScript", { PLAYERHOOK_CAN_PLAYER_USE_CHAT }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 lang, std::string& msg) override
    {
        if (!settings.enabled || lang == LANG_ADDON || (type != CHAT_MSG_SAY && type != CHAT_MSG_YELL) || !IsHuman(player))
            return true;
        if (AsksSignatures(msg))
            Charters::Instance().Asked(player, time(nullptr));
        return true;
    }
};
}  // namespace

bool CoaWorldCharterOffered(Player* bot, ObjectGuid petition, Player* owner, bool guildCharter)
{
    if (!settings.enabled || !bot || !owner || !IsHuman(owner))
        return false;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    Player* master = botAI ? botAI->GetMaster() : nullptr;
    if (!botAI || !StrangerMaySign(guildCharter, sRandomPlayerbotMgr.IsRandomBot(bot), master && IsHuman(master),
                                   bot->GetGuildId() != 0, bot->GetGuildIdInvited() != 0))
        return false;
    Charters::Instance().Queue({ bot->GetGUID(), petition, owner->GetGUID(), time(nullptr) + urand(3, 15), true });
    return true;
}

bool CoaWorldGuildMayJoin()
{
    if (!settings.enabled)
        return true;
    return AboveFloor(g_unguilded, g_total, 1, settings.floorPct);
}

void AddSC_coa_world_charters()
{
    new CoaWorldChartersWorldScript();
    new CoaWorldChartersPlayerScript();
}
