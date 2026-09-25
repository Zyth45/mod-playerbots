/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

/*
 * bot-status.json: a snapshot of every random bot online, written every few seconds for the
 * SquidBots dashboard (its map and bot cards): where each bot is, its health and power, what it is
 * doing, its group and its quests. Off unless AiPlayerbot.CoaStatusFile names a file.
 *
 * The snapshot is built in the world update, which runs once the map threads have finished their
 * update, so the bots read here are not moving under us. Building it is all the world thread does:
 * the text goes to a writer thread of its own, which writes it whole to a .tmp beside the file and
 * renames that over the old one, so a reader never sees half of it. When the disk is slower than the
 * interval, only the newest snapshot waits; older ones are dropped.
 *
 * "us" in the file is how long the world thread spent building the previous snapshot, "io_us" how long
 * the writer thread spent writing it, both in microseconds. Nothing a bot does reads this file or
 * anything kept for it.
 */

#include "Config.h"
#include "Engine.h"
#include "Group.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>

namespace
{
using Clock = std::chrono::steady_clock;

uint32 MicrosecondsSince(Clock::time_point start)
{
    return uint32(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}

// A JSON string, quotes included. Names and titles rarely hold anything to escape, so the text is
// copied in runs between the characters that need it.
void AppendJsonString(std::string& out, std::string_view text)
{
    out += '"';
    size_t run = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char const c = text[i];
        if (c >= 0x20 && c != '"' && c != '\\')
            continue;
        out.append(text.data() + run, i - run);
        run = i + 1;
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
        }
    }
    out.append(text.data() + run, text.size() - run);
    out += '"';
}

void AppendUInt(std::string& out, uint32 value)
{
    char buf[16];
    auto const result = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, result.ptr);
}

// One decimal is all the map needs.
void AppendCoord(std::string& out, float value)
{
    char buf[32];
    auto const result = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::fixed, 1);
    out.append(buf, result.ptr);
}

uint32 Percent(uint32 value, uint32 max)
{
    return max ? std::min<uint32>(100, uint32((uint64(value) * 100 + max / 2) / max)) : 0;
}

// Quest titles and creature and object names, already in JSON form, kept from one snapshot to the
// next: most of the file is the same few hundred quest titles. World thread only.
class JsonNames
{
public:
    // Empty when the quest is unknown.
    std::string const& Quest(uint32 questId)
    {
        auto it = quests.find(questId);
        if (it == quests.end())
        {
            ::Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            it = quests.emplace(questId, quest ? Json(quest->GetTitle()) : std::string()).first;
        }
        return it->second;
    }

    std::string const& Creature(uint32 entry)
    {
        auto it = creatures.find(entry);
        if (it == creatures.end())
        {
            CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(entry);
            it = creatures.emplace(entry, info ? Json(info->Name) : std::string()).first;
        }
        return it->second;
    }

    std::string const& Object(uint32 entry)
    {
        auto it = objects.find(entry);
        if (it == objects.end())
        {
            GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(entry);
            it = objects.emplace(entry, info ? Json(info->name) : std::string()).first;
        }
        return it->second;
    }

private:
    // The text without its closing quote, so that a prefix can go in front: "\"Questing: " + ...
    static std::string Json(std::string const& text)
    {
        if (text.empty())
            return std::string();
        std::string out;
        AppendJsonString(out, text);
        out.pop_back();
        return out;
    }

    std::unordered_map<uint32, std::string> quests;
    std::unordered_map<uint32, std::string> creatures;
    std::unordered_map<uint32, std::string> objects;
};

// `"Prefix name"` from a cached name (opening quote, no closing one); `"fallback"` when it is unknown.
void AppendNamed(std::string& out, char const* prefix, std::string const& cached, char const* fallback)
{
    if (cached.empty())
    {
        out += '"';
        out += fallback;
        out += '"';
        return;
    }
    out += '"';
    out += prefix;
    out.append(cached, 1, std::string::npos);
    out += '"';
}

// The last action a bot carried out, told the way a player would say it, for a bot with no rpg status.
// The actions seen most on a realm of 1000 bots; any other shows under its own name.
std::unordered_map<std::string, char const*> const ActionTasks = {
    { "new rpg status update", "\"Choosing what to do\"" },
    { "suggest what to do", "\"Choosing what to do\"" },
    { "new rpg do quest", "\"Questing\"" },
    { "new rpg move npcs", "\"Visiting NPCs\"" },
    { "new rpg wander random", "\"Wandering\"" },
    { "new rpg travel flight", "\"Going to a flight master\"" },
    { "new rpg go grind", "\"Going to grind\"" },
    { "new rpg go camp", "\"Going back to camp\"" },
    { "xp gain", "\"Won a fight\"" },
    { "dps assist", "\"Looking for a fight\"" },
    { "tank assist", "\"Looking for a fight\"" },
    { "coa attack", "\"Attacking\"" },
    { "reset botAI", "\"Choosing what to do\"" },
    { "attack anything", "\"Looking for a fight\"" },
    { "auto maintenance on levelup", "\"Levelled up, sorting gear\"" },
    { "add all loot", "\"Looting\"" },
    { "add gathering loot", "\"Looting\"" },
    { "store loot", "\"Looting\"" },
    { "loot", "\"Looting\"" },
    { "move to loot", "\"Looting\"" },
    { "coa buff", "\"Buffing\"" },
    { "drink", "\"Drinking\"" },
    { "food", "\"Eating\"" },
    { "follow", "\"Following\"" },
    { "check mount state", "\"Mounting up\"" },
    { "revive from corpse", "\"Reviving\"" },
    { "release", "\"Releasing spirit\"" },
};

// "Fighting X", "Questing: Y"...: what the bot is at, from its state and its rpg status, then from
// the last action it carried out. Appended to `out` as a JSON string.
void AppendTask(std::string& out, Player* bot, PlayerbotAI* botAI, JsonNames& names)
{
    if (!bot->IsAlive())
    {
        out += bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? "\"Dead, running back\"" : "\"Dead\"";
        return;
    }
    if (bot->IsInFlight())
    {
        out += "\"Flying\"";
        return;
    }
    if (bot->IsInCombat())
    {
        // Between two swings a bot has no victim: whoever is hitting it says what the fight is.
        Unit* victim = bot->GetVictim();
        if (!victim && !bot->getAttackers().empty())
            victim = *bot->getAttackers().begin();
        if (!victim)
            out += "\"In combat\"";
        else if (Creature* creature = victim->ToCreature())
            AppendNamed(out, "Fighting ", names.Creature(creature->GetEntry()), "In combat");
        else
            AppendJsonString(out, "Fighting " + victim->GetName());
        return;
    }

    bool done = true;
    std::visit([&](auto&& arg)
    {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, NewRpgInfo::GoGrind>)
            out += "\"Going to grind\"";
        else if constexpr (std::is_same_v<T, NewRpgInfo::GoCamp>)
            out += "\"Going back to camp\"";
        else if constexpr (std::is_same_v<T, NewRpgInfo::WanderRandom>)
            out += "\"Wandering\"";
        else if constexpr (std::is_same_v<T, NewRpgInfo::Rest>)
            out += "\"Resting\"";
        else if constexpr (std::is_same_v<T, NewRpgInfo::TravelFlight>)
            out += "\"Going to a flight master\"";
        else if constexpr (std::is_same_v<T, NewRpgInfo::OutdoorPvP>)
            out += "\"Outdoor PvP\"";
        else if constexpr (std::is_same_v<T, NewRpgInfo::DoQuest>)
            AppendNamed(out, "Questing: ", names.Quest(arg.questId), "Questing");
        else if constexpr (std::is_same_v<T, NewRpgInfo::WanderNpc>)
        {
            static std::string const none;
            std::string const& name = arg.npcOrGo.IsCreature()     ? names.Creature(arg.npcOrGo.GetEntry())
                                      : arg.npcOrGo.IsGameObject() ? names.Object(arg.npcOrGo.GetEntry())
                                                                   : none;
            AppendNamed(out, "Visiting ", name, "Visiting NPCs");
        }
        else
            done = false;
    }, botAI->rpgInfo.data);
    if (done)
        return;

    Engine* engine = botAI->GetEngine(botAI->GetState());
    std::string const* action = engine ? &engine->GetLastExecutedAction() : nullptr;
    if (!action || action->empty())
    {
        out += "\"Idle\"";
        return;
    }
    auto const known = ActionTasks.find(*action);
    if (known != ActionTasks.end())
    {
        out += known->second;
        return;
    }
    // "cast::Sunflare", "cast melee::Skulltaker": a spell cast by its name.
    size_t const spell = action->rfind("::");
    if (spell != std::string::npos && action->compare(0, 4, "cast") == 0)
    {
        AppendJsonString(out, "Casting " + action->substr(spell + 2));
        return;
    }
    size_t const at = out.size();
    AppendJsonString(out, *action);
    out[at + 1] = char(toupper(static_cast<unsigned char>(out[at + 1])));
}

void AppendBot(std::string& out, Player* bot, PlayerbotAI* botAI, JsonNames& names)
{
    Powers const power = bot->getPowerType();
    out += "{\"n\":";
    AppendJsonString(out, bot->GetName());
    out += ",\"l\":";
    AppendUInt(out, bot->GetLevel());
    out += ",\"hp\":";
    AppendUInt(out, Percent(bot->GetHealth(), bot->GetMaxHealth()));
    out += ",\"pt\":";
    AppendUInt(out, uint32(power));
    out += ",\"pw\":";
    AppendUInt(out, Percent(bot->GetPower(power), bot->GetMaxPower(power)));
    out += ",\"m\":";
    AppendUInt(out, bot->GetMapId());
    out += ",\"zone\":";
    AppendUInt(out, bot->GetZoneId());
    out += ",\"x\":";
    AppendCoord(out, bot->GetPositionX());
    out += ",\"y\":";
    AppendCoord(out, bot->GetPositionY());
    out += bot->IsInCombat() ? ",\"combat\":true" : ",\"combat\":false";
    out += bot->IsAlive() ? ",\"dead\":false" : ",\"dead\":true";

    Group* group = bot->GetGroup();
    out += ",\"grp\":";
    AppendUInt(out, group ? group->GetMembersCount() : 0);
    if (group && group->GetLeaderName())
    {
        out += ",\"lead\":";
        AppendJsonString(out, group->GetLeaderName());
    }

    out += ",\"task\":";
    AppendTask(out, bot, botAI, names);

    out += ",\"quests\":[";
    bool first = true;
    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
        std::string const& title = names.Quest(questId);
        if (title.empty())
            continue;
        if (!first)
            out += ',';
        first = false;
        out += title;
        out += '"';
    }
    out += "]}";
}

// Writes the snapshots handed to it, one at a time, off the world thread. Only the newest waiting
// snapshot is kept: a slow disk costs lost snapshots, never a world update.
class StatusWriter
{
public:
    ~StatusWriter() { Stop(); }

    // Takes `text` (swapped with an empty buffer that keeps the old capacity) to be written to `path`.
    void Post(std::string const& path, std::string& text)
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            if (!thread.joinable())
                thread = std::thread(&StatusWriter::Run, this);
            pendingPath = path;
            pending.swap(text);
            hasPending = true;
        }
        wake.notify_one();
    }

    void Stop()
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            stopping = true;
        }
        wake.notify_one();
        if (thread.joinable())
            thread.join();
        std::lock_guard<std::mutex> guard(lock);
        stopping = false;
        hasPending = false;
    }

    uint32 LastWriteMicroseconds() const { return lastWrite.load(std::memory_order_relaxed); }

private:
    void Run()
    {
        std::string text;
        std::string path;
        for (;;)
        {
            {
                std::unique_lock<std::mutex> guard(lock);
                wake.wait(guard, [this] { return hasPending || stopping; });
                if (stopping)
                    return;
                text.swap(pending);
                path.swap(pendingPath);
                hasPending = false;
            }
            Clock::time_point const start = Clock::now();
            if (Write(path, text))
                lastWrite.store(MicrosecondsSince(start), std::memory_order_relaxed);
        }
    }

    // A .tmp beside the file, then renamed over it. On Windows the rename fails while a reader holds
    // the old file open: it is tried again for a moment, until a newer snapshot is waiting.
    bool Write(std::string const& path, std::string const& text)
    {
        std::string const tmp = path + ".tmp";
        {
            std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
            file.write(text.data(), std::streamsize(text.size()));
            file.close();
            if (!file)
            {
                if (!failed)
                    LOG_ERROR("playerbots.coa", "bot status: cannot write {}", tmp);
                failed = true;
                return false;
            }
        }
        failed = false;
        for (uint32 attempt = 0; attempt < 50; ++attempt)
        {
            std::error_code error;
            std::filesystem::rename(tmp, path, error);
            if (!error)
                return true;
            {
                std::lock_guard<std::mutex> guard(lock);
                if (hasPending || stopping)
                    return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }

    std::mutex lock;
    std::condition_variable wake;
    std::thread thread;
    std::string pending;
    std::string pendingPath;
    bool hasPending = false;
    bool stopping = false;
    bool failed = false;  // writer thread only: the error is logged once, not every interval
    std::atomic<uint32> lastWrite{0};
};

class CoaStatusFileWorldScript : public WorldScript
{
public:
    CoaStatusFileWorldScript()
        : WorldScript("CoaStatusFileWorldScript",
                      { WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_SHUTDOWN })
    {
    }

    void OnUpdate(uint32 diff) override
    {
        if (!sPlayerbotAIConfig.coaStatusEnabled)
            return;
        elapsed += diff;
        if (elapsed < sPlayerbotAIConfig.coaStatusIntervalSeconds * IN_MILLISECONDS)
            return;
        elapsed = 0;
        Build();
        writer.Post(sPlayerbotAIConfig.coaStatusFile, text);
    }

    // ".reload config" turns the file on or off, or moves it, without a restart.
    void OnAfterConfigLoad(bool reload) override
    {
        if (!reload)
            return;
        sPlayerbotAIConfig.coaStatusFile = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaStatusFile", "");
        sPlayerbotAIConfig.coaStatusIntervalSeconds =
            std::max<uint32>(1, sConfigMgr->GetOption<uint32>("AiPlayerbot.CoaStatusIntervalSeconds", 5));
        sPlayerbotAIConfig.coaStatusEnabled = !sPlayerbotAIConfig.coaStatusFile.empty();
    }

    void OnShutdown() override { writer.Stop(); }

private:
    void Build()
    {
        Clock::time_point const start = Clock::now();
        text.clear();
        text.reserve(lastSize + lastSize / 8);
        text += "{\"at\":";
        text += std::to_string(uint64(time(nullptr)));
        text += ",\"us\":";
        AppendUInt(text, lastBuild);
        text += ",\"io_us\":";
        AppendUInt(text, writer.LastWriteMicroseconds());
        text += ",\"bots\":[";
        bool first = true;
        for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
        {
            Player* bot = it->second;
            if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
                continue;
            PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
            if (!botAI)
                continue;
            if (!first)
                text += ",\n";
            first = false;
            AppendBot(text, bot, botAI, names);
        }
        text += "]}\n";
        lastSize = text.size();
        lastBuild = MicrosecondsSince(start);
    }

    uint32 elapsed = 0;
    uint32 lastBuild = 0;
    size_t lastSize = 64 * 1024;
    std::string text;
    JsonNames names;
    StatusWriter writer;
};
}  // namespace

void AddSC_coa_status_file() { new CoaStatusFileWorldScript(); }
