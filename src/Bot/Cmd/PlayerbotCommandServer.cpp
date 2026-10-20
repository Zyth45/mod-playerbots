/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "PlayerbotCommandServer.h"
#include "IoContext.h"
#include "Log.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotOperation.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "RandomPlayerbotMgr.h"
#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <cerrno>
#include <poll.h>
#endif

using boost::asio::ip::tcp;

namespace
{
constexpr uint32 MaxSessions = 4;
constexpr std::size_t MaxLineBytes = 4096;
constexpr std::chrono::seconds ReplyTimeout(5);
constexpr std::chrono::seconds IdleTimeout(60);

std::atomic<uint32> activeSessions{0};

// Set on world shutdown: the detached session threads must not queue work once the world has stopped.
std::atomic<bool> stopping{false};

// Holds one of the MaxSessions slots until destroyed, whichever way the session ends.
class SessionSlot
{
public:
    SessionSlot() : m_held(activeSessions.fetch_add(1) < MaxSessions)
    {
        if (!m_held)
            activeSessions.fetch_sub(1);
    }

    SessionSlot(SessionSlot&& other) noexcept : m_held(std::exchange(other.m_held, false)) {}

    ~SessionSlot()
    {
        if (m_held)
            activeSessions.fetch_sub(1);
    }

    SessionSlot(SessionSlot const&) = delete;
    SessionSlot& operator=(SessionSlot const&) = delete;
    SessionSlot& operator=(SessionSlot&&) = delete;

    bool IsHeld() const { return m_held; }

private:
    bool m_held;
};

// HandleRemoteCommand reads live bot state that map threads write, so it runs in the world thread, which drains
// this queue while no map is updating. The operation owns the promise, so it stays valid after the socket thread
// has given up waiting, and a failed set_value is caught so it never throws into the world thread.
class RemoteCommandOperation : public PlayerbotOperation
{
public:
    RemoteCommandOperation(std::string request, std::shared_ptr<std::promise<std::string>> reply)
        : m_request(std::move(request)), m_reply(std::move(reply))
    {
    }

    bool Execute() override
    {
        std::string response = sRandomPlayerbotMgr.HandleRemoteCommand(m_request);

        try
        {
            m_reply->set_value(std::move(response));
        }
        catch (std::future_error const&)
        {
            return false;
        }

        return true;
    }

    bool IsValid() const override { return m_reply != nullptr; }

    std::string GetName() const override { return "RemoteCommandOperation"; }

private:
    std::string m_request;
    std::shared_ptr<std::promise<std::string>> m_reply;
};

std::string HandleOnWorldThread(std::string const& request)
{
    if (stopping)
        return "error: shutting down";

    auto reply = std::make_shared<std::promise<std::string>>();
    std::future<std::string> future = reply->get_future();

    // A full queue destroys the operation, which breaks the promise.
    if (!PlayerbotWorldThreadProcessor::instance().QueueOperation(
            std::make_unique<RemoteCommandOperation>(request, std::move(reply))))
        return "error: server busy";

    if (future.wait_for(ReplyTimeout) != std::future_status::ready)
        return "error: timeout";

    try
    {
        return future.get();
    }
    catch (std::exception const&)
    {
        return "error: command failed";
    }
}

// Blocks until sock has data (or EOF/error) to read, so an idle client cannot hold a session slot forever.
bool WaitReadable(tcp::socket& sock)
{
    int const timeoutMs = static_cast<int>(std::chrono::milliseconds(IdleTimeout).count());
#ifdef _WIN32
    WSAPOLLFD fd{};
    fd.fd = sock.native_handle();
    fd.events = POLLRDNORM;
    int const ready = ::WSAPoll(&fd, 1, timeoutMs);
    int const lastError = ready < 0 ? ::WSAGetLastError() : 0;
#else
    pollfd fd{};
    fd.fd = sock.native_handle();
    fd.events = POLLIN;
    int ready;
    do
    {
        ready = ::poll(&fd, 1, timeoutMs);
    } while (ready < 0 && errno == EINTR);
    int const lastError = ready < 0 ? errno : 0;
#endif

    if (ready > 0)
        return true;

    if (stopping)
        return false;

    if (ready == 0)
        LOG_INFO("playerbots", "Playerbots Command Server: no request for {} seconds, closing connection",
                 IdleTimeout.count());
    else
        LOG_ERROR("playerbots", "Playerbots Command Server: waiting for a request failed: {}",
                  boost::system::error_code(lastError, boost::system::system_category()).message());

    return false;
}

bool ReadLine(tcp::socket& sock, std::string& buffer, std::string& line)
{
    // Do the real reading from fd until buffer has '\n'.
    for (;;)
    {
        std::string::size_type const pos = buffer.find('\n');
        if ((pos == std::string::npos ? buffer.size() : pos) > MaxLineBytes)
        {
            LOG_WARN("playerbots", "Playerbots Command Server: request line longer than {} bytes, closing connection",
                     MaxLineBytes);
            return false;
        }

        if (pos != std::string::npos)
        {
            line.assign(buffer, 0, pos);
            buffer.erase(0, pos + 1);
            return true;
        }

        if (!WaitReadable(sock))
            return false;

        char buf[1024];
        boost::system::error_code error;
        std::size_t const n = sock.read_some(boost::asio::buffer(buf), error);
        if (error == boost::asio::error::eof)
            return false;
        else if (error)
            throw boost::system::system_error(error);  // Some other error.

        buffer.append(buf, n);
    }
}

void session(tcp::socket sock, SessionSlot /*slot*/)
{
    try
    {
        std::string buffer, request;
        while (!stopping && ReadLine(sock, buffer, request))
        {
            std::string const response = HandleOnWorldThread(request) + "\n";
            boost::asio::write(sock, boost::asio::buffer(response.c_str(), response.size()));
        }
    }
    catch (std::exception& e)
    {
        LOG_ERROR("playerbots", "{}", e.what());
    }
}

void server(Acore::Asio::IoContext& io_service, tcp::endpoint const& endpoint)
{
    tcp::acceptor a(io_service, endpoint);
    for (;;)
    {
        tcp::socket sock(io_service);
        boost::system::error_code error;
        a.accept(sock, error);
        if (error)
        {
            // Keep the loop (and so io_service) alive: running sessions still use it.
            if (!stopping)
                LOG_ERROR("playerbots", "Playerbots Command Server: accept failed: {}", error.message());
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        SessionSlot slot;
        if (!slot.IsHeld())
        {
            LOG_WARN("playerbots", "Playerbots Command Server: {} sessions already open, refusing connection",
                     MaxSessions);
            sock.close(error);
            continue;
        }

        // Best effort: lets the OS notice a peer that vanished without closing the connection.
        sock.set_option(tcp::socket::keep_alive(true), error);

        try
        {
            std::thread(session, std::move(sock), std::move(slot)).detach();
        }
        catch (std::exception const& e)
        {
            LOG_ERROR("playerbots", "Playerbots Command Server: cannot start session: {}", e.what());
        }
    }
}

void Run(boost::asio::ip::address const address, uint16 const port)
{
    LOG_INFO("playerbots", "Starting Playerbots Command Server on {} port {}", address.to_string(), port);

    try
    {
        Acore::Asio::IoContext io_service;
        server(io_service, tcp::endpoint(address, port));
    }

    catch (std::exception& e)
    {
        LOG_ERROR("playerbots", "{}", e.what());
    }
}
}  // namespace

void PlayerbotCommandServer::Start()
{
    uint16 const port = sPlayerbotAIConfig.commandServerPort;
    if (!port)
    {
        return;
    }

    boost::system::error_code error;
    boost::asio::ip::address const address = boost::asio::ip::make_address(sPlayerbotAIConfig.commandServerBind, error);
    if (error)
    {
        LOG_ERROR("playerbots", "Playerbots Command Server not started: invalid AiPlayerbot.CommandServerBind '{}': {}",
                  sPlayerbotAIConfig.commandServerBind, error.message());
        return;
    }

    if (!address.is_loopback())
        LOG_WARN("playerbots", "Playerbots Command Server has no authentication but binds to non-loopback address {}",
                 address.to_string());

    std::thread serverThread(Run, address, port);
    serverThread.detach();
}

void PlayerbotCommandServer::Stop() { stopping = true; }
