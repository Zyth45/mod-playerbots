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
#include "CoaArtisansAddonRules.h"
#include "CoaCraftOrders.h"
#include "CoaEconomyData.h"

#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
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

    struct Cod
    {
        uint32 quoteId = 0;
        ObjectGuid::LowType bot = 0;
        std::string botName;
        uint32 itemId = 0;
        uint32 qty = 0;
        uint32 copper = 0;
        uint32 at = 0;
        bool paid = false;
    };
    std::unordered_map<ObjectGuid::LowType, std::deque<Cod>> cods;  // per player, newest last

    uint32 Now() { return uint32(GameTime::GetGameTime().count()); }
    uint64 NowMs() { return uint64(GameTime::GetGameTimeMS().count()); }

    // The profession skill lines a bot may craft with (primary and secondary, not fishing), from the client data.
    std::vector<uint32> const& ProfessionSkills()
    {
        static std::vector<uint32> skills = []
        {
            std::vector<uint32> list;
            for (uint32 i = 0; i < sSkillLineStore.GetNumRows(); ++i)
                if (SkillLineEntry const* skill = sSkillLineStore.LookupEntry(i))
                    if ((skill->categoryId == SKILL_CATEGORY_PROFESSION || skill->categoryId == SKILL_CATEGORY_SECONDARY) &&
                        skill->id != SKILL_FISHING)
                        list.push_back(skill->id);
            return list;
        }();
        return skills;
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
        struct Candidate { Player* bot; uint32 best; std::string skills; };
        std::vector<Candidate> candidates;
        for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        {
            if (!bot || !bot->IsInWorld() || bot->GetTeamId() != player->GetTeamId())
                continue;
            Candidate candidate{bot, 0, ""};
            for (uint32 skill : ProfessionSkills())
                if (uint16 value = bot->GetSkillValue(skill))
                {
                    candidate.skills += (candidate.skills.empty() ? "" : ",") + std::to_string(skill) + ":" + std::to_string(value);
                    candidate.best = std::max<uint32>(candidate.best, value);
                }
            if (candidate.best)
                candidates.push_back(std::move(candidate));
        }
        std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) { return a.best > b.best; });
        for (Candidate const& candidate : candidates)
        {
            if (lines.size() >= Rules::MaxArtisans)
                break;
            if (!Artisans::KnownRecipes(candidate.bot).empty())
                lines.push_back("AB\t" + Rules::Clean(candidate.bot->GetName()) + "\t" + candidate.skills);
        }
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
        std::vector<uint32> seen;
        for (Artisans::Recipe const& recipe : Artisans::KnownRecipes(bot))
        {
            if (lines.size() >= Rules::MaxRecipes)
                break;
            if (std::find(seen.begin(), seen.end(), recipe.itemId) != seen.end())
                continue;
            seen.push_back(recipe.itemId);
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(recipe.itemId);
            lines.push_back("AR\t" + name + "\t" + std::to_string(recipe.itemId) + "\t" + std::to_string(recipe.spell->Id) +
                            "\t" + std::to_string(recipe.skillId) + "\t" + std::to_string(proto ? proto->Quality : 0));
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
    }

    // Makes `qty` pieces from the reagents (already used) and mails them cash on delivery, one mail.
    void MailOrder(Player* bot, Player* player, Artisans::Recipe const& recipe, uint32 qty, uint32 copper)
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(recipe.itemId);
        MailDraft draft(proto ? proto->Name1 : "", "");
        for (uint32 i = 0; i < qty; ++i)
            if (Item* item = Item::CreateItem(recipe.itemId, recipe.count, player, false,
                                              Item::GenerateItemRandomPropertyId(recipe.itemId)))
            {
                item->SaveToDB(trans);
                draft.AddItem(item);
            }
        draft.AddCOD(copper).SendMailTo(trans, MailReceiver(player), MailSender(bot), MAIL_CHECK_MASK_COPIED,
                                        sWorld->getIntConfig(CONFIG_MAIL_DELIVERY_DELAY));
        bot->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
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
        MailOrder(bot, player, recipe, quote.qty, quote.copper);
        CraftOrders::MarkBusy(bot);
        Data::RecordOrder(player->GetName(), bot->GetName(), quote.itemId, quote.copper, "mailed", recipe.skillId, "player");

        std::deque<Cod>& log = cods[player->GetGUID().GetCounter()];
        log.push_back({quoteId, bot->GetGUID().GetCounter(), quote.botName, quote.itemId, quote.qty, quote.copper, Now(), false});
        while (log.size() > Rules::MaxMails)
            log.pop_front();
        answer("mailed");
    }

    void Mails(Player* player, std::vector<std::string>& lines)
    {
        auto found = cods.find(player->GetGUID().GetCounter());
        if (found == cods.end())
            return;
        uint32 const now = Now();
        std::deque<Cod>& log = found->second;
        while (!log.empty() && log.front().at + Rules::MailDays * DAY < now)
            log.pop_front();
        for (auto it = log.rbegin(); it != log.rend(); ++it)
        {
            bool inMailbox = false;
            for (Mail const* mail : player->GetMails())
                if (mail && mail->state != MAIL_STATE_DELETED && mail->sender == it->bot && mail->COD == it->copper)
                    inMailbox = true;
            lines.push_back("AM\t" + std::to_string(it->quoteId) + "\t" + Rules::Clean(it->botName) + "\t" +
                            std::to_string(it->itemId) + "\t" + std::to_string(it->qty) + "\t" + std::to_string(it->copper) +
                            "\t" + std::to_string(it->at) + "\t" + Rules::CodState(it->paid, inMailbox));
        }
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
                Order(player, fields, lines);
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
            auto found = cods.find(sender.GetSenderId());
            if (found == cods.end())
                return;
            for (Cod& cod : found->second)
                if (!cod.paid && cod.bot == receiver.GetPlayerGUIDLow() && cod.copper == draft->GetMoney())
                {
                    cod.paid = true;
                    return;
                }
        }
    };
}

} // namespace CoaEconomy::ArtisansAddon

void AddSC_coa_economy_artisans_addon()
{
    // Off by default; read at start-up (restart to change it).
    if (!sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.Artisans.Enable", false, false) ||
        !sConfigMgr->GetOption<bool>("AiPlayerbot.CoaEconomy.Artisans.Addon", false, false))
        return;
    new CoaEconomy::ArtisansAddon::CoaArtisansAddonPlayer();
    new CoaEconomy::ArtisansAddon::CoaArtisansAddonMail();
}
