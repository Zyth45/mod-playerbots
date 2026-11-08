/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

/*
 * CoA World: events.jsonl for the dashboard (D:\CoA-Dashboard\docs\LIAISON-3.0.md §3 and §6).
 *
 * One line per event, as it happens: {"at":…,"kind":"…","bot":"…","text":"…"}. Kinds of CoA World: invasion, rush,
 * server_first, guild, stuck, unstuck. The bricks call CoaWorld::LogEvent from any thread; the lines are queued and
 * appended by a thread of its own, so the world thread never waits on the disk. The file turns over at 5 MB,
 * keeping events.jsonl.1. Same directory as the other dashboard files: AiPlayerbot.CoaDataDir, else
 * <LogsDir>/squidbot.
 *
 * Off by default: AiPlayerbot.CoaWorld.Events.Enable = 1, and AiPlayerbot.CoaDataFiles not 0. Read at start-up.
 */

#include "CoaWorldBots.h"

#include "Config.h"
#include "ScriptMgr.h"

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    constexpr uintmax_t ROTATE_BYTES = 5u * 1024u * 1024u;

    bool g_enabled = false;
    std::string g_path;

    std::mutex g_lock;
    std::condition_variable g_wake;
    std::vector<std::string> g_lines;
    bool g_stop = false;
    std::thread g_thread;

    void Escape(std::string& out, std::string const& text)
    {
        out += '"';
        for (unsigned char c : text)
        {
            switch (c)
            {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                        out += char(c);
            }
        }
        out += '"';
    }

    void WriterLoop()
    {
        std::unique_lock<std::mutex> lock(g_lock);
        while (true)
        {
            g_wake.wait_for(lock, std::chrono::seconds(2), [] { return g_stop || !g_lines.empty(); });
            if (g_lines.empty() && g_stop)
                return;
            std::vector<std::string> lines;
            lines.swap(g_lines);
            lock.unlock();

            std::error_code error;
            std::filesystem::path path(g_path);
            std::filesystem::create_directories(path.parent_path(), error);
            if (std::filesystem::exists(path, error) && std::filesystem::file_size(path, error) >= ROTATE_BYTES)
            {
                std::filesystem::path old = path;
                old += ".1";
                std::filesystem::remove(old, error);
                std::filesystem::rename(path, old, error);
            }
            {
                std::ofstream out(path, std::ios::app | std::ios::binary);
                for (std::string const& line : lines)
                    out << line << '\n';
            }

            lock.lock();
        }
    }

    void Start()
    {
        g_enabled = sConfigMgr->GetOption<bool>("AiPlayerbot.CoaWorld.Events.Enable", false, false) &&
                    sConfigMgr->GetOption<bool>("AiPlayerbot.CoaDataFiles", true, false);
        if (!g_enabled || g_thread.joinable())
            return;

        std::string dir = sConfigMgr->GetOption<std::string>("AiPlayerbot.CoaDataDir", "", false);
        if (dir.empty())
        {
            std::string logs = sConfigMgr->GetOption<std::string>("LogsDir", "", false);
            if (!logs.empty() && logs.back() != '/' && logs.back() != '\\')
                logs += '/';
            dir = logs + "squidbot";
        }
        g_path = (std::filesystem::path(dir) / "events.jsonl").string();
        g_thread = std::thread(&WriterLoop);
    }

    void Stop()
    {
        if (!g_thread.joinable())
            return;
        {
            std::lock_guard<std::mutex> lock(g_lock);
            g_stop = true;
        }
        g_wake.notify_one();
        g_thread.join();
    }
}

namespace CoaWorld
{
    void LogEvent(char const* kind, std::string const& bot, std::string const& text)
    {
        if (!g_enabled)
            return;
        std::string line = "{\"at\":" + std::to_string(uint64(time(nullptr))) + ",\"kind\":";
        Escape(line, kind);
        line += ",\"bot\":";
        Escape(line, bot);
        line += ",\"text\":";
        Escape(line, text);
        line += '}';
        {
            std::lock_guard<std::mutex> lock(g_lock);
            if (g_lines.size() < 10000)  // never grows without bound if the disk stalls
                g_lines.push_back(std::move(line));
        }
        g_wake.notify_one();
    }
}

class CoaWorldEventsWorldScript : public WorldScript
{
public:
    CoaWorldEventsWorldScript() : WorldScript("CoaWorldEventsWorldScript", { WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_SHUTDOWN })
    {
    }

    void OnStartup() override { Start(); }
    void OnShutdown() override { Stop(); }
};

void AddSC_coa_world_events()
{
    new CoaWorldEventsWorldScript();
}
