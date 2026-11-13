// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Caleb Kemere, Rice University

#include "LhtrClient.h"

#include <chrono>
#include <cstring>
#include <random>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using socklen_t = int;
    #define LHTR_CLOSE closesocket
#else
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define LHTR_CLOSE ::close
#endif

LhtrClient::LhtrClient()
    : juce::Thread ("LHTR ACK listener")
{
    // Random start so a restarted sender never lands inside the broker's
    // duplicate-suppression memory of our previous run -- a trigger whose seq
    // the broker has already seen is answered DUPLICATE and does not fire.
    // Same reasoning as the Python client.
    std::random_device rd;
    seqCounter = ((uint32_t) rd() << 16) ^ (uint32_t) rd();
}

LhtrClient::~LhtrClient()
{
    close();
}

uint64_t LhtrClient::nowMicros()
{
    using namespace std::chrono;
    return (uint64_t) duration_cast<microseconds> (steady_clock::now().time_since_epoch()).count();
}

bool LhtrClient::open (const juce::String& host, int port)
{
    close();
    lastError.clear();
    peerAddress.clear();

    if (host.isEmpty())
    {
        lastError = "no host set";
        return false;
    }

#ifdef _WIN32
    static bool wsaStarted = false;
    if (! wsaStarted)
    {
        WSADATA wsa;
        if (WSAStartup (MAKEWORD (2, 2), &wsa) != 0)
        {
            lastError = "WSAStartup failed";
            return false;
        }
        wsaStarted = true;
    }
#endif

    // getaddrinfo, not inet_pton: the host box will usually be named
    // ("laserhat.local", a DNS name) rather than dotted-quad, and inet_pton
    // rejects anything that is not already numeric. This is the one blocking
    // call in the class, which is why open() is message-thread only.
    struct addrinfo hints;
    std::memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_INET; // the broker binds IPv4
    hints.ai_socktype = SOCK_DGRAM;

    const juce::String portText (port);
    struct addrinfo* res = nullptr;
    const int gai = ::getaddrinfo (host.toRawUTF8(), portText.toRawUTF8(), &hints, &res);
    if (gai != 0 || res == nullptr)
    {
        lastError = "cannot resolve " + host;
        return false;
    }

    int fd = (int) ::socket (res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0)
    {
        ::freeaddrinfo (res);
        lastError = "socket() failed";
        return false;
    }

    // connect() on a UDP socket sends nothing; it just fixes the peer. That
    // makes every later send() a bare write with no address copy, and it is
    // what lets the listener thread recv() without filtering by sender.
    const bool connected = (::connect (fd, res->ai_addr, (socklen_t) res->ai_addrlen) == 0);

    if (connected)
    {
        char dotted[INET_ADDRSTRLEN] = { 0 };
        const auto* in4 = (const struct sockaddr_in*) res->ai_addr;
        if (::inet_ntop (AF_INET, &in4->sin_addr, dotted, sizeof (dotted)) != nullptr)
            peerAddress = juce::String (dotted) + ":" + juce::String (port);
    }

    ::freeaddrinfo (res);

    if (! connected)
    {
        LHTR_CLOSE (fd);
        lastError = "connect() failed";
        return false;
    }

    // Non-blocking for the LIFETIME of the socket, set once here rather than
    // per call. The audio thread must never block in send(), and the listener
    // polls with a short wait instead of parking in recv().
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket (fd, FIONBIO, &nb);
#else
    const int fl = fcntl (fd, F_GETFL, 0);
    fcntl (fd, F_SETFL, fl | O_NONBLOCK);
#endif

    socketFd = fd;
    resetCounters();
    startThread();
    return true;
}

void LhtrClient::close()
{
    if (isThreadRunning())
        stopThread (500);

    if (socketFd >= 0)
    {
        LHTR_CLOSE (socketFd);
        socketFd = -1;
    }
}

bool LhtrClient::sendRequest (uint8_t msgType)
{
    if (socketFd < 0)
        return false;

    uint8_t buf[lhtr::REQUEST_BYTES];
    lhtr::packRequest (buf, msgType, ++seqCounter, nowMicros());

#ifdef _WIN32
    const int n = ::send (socketFd, (const char*) buf, (int) lhtr::REQUEST_BYTES, 0);
#else
    const ssize_t n = ::send (socketFd, buf, lhtr::REQUEST_BYTES, 0);
#endif

    // send() returns int on Windows and ssize_t elsewhere; both are signed, so
    // comparing against a plain int is portable and warning-free.
    if (n != (int) lhtr::REQUEST_BYTES)
    {
        sendErrors.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    sent.fetch_add (1, std::memory_order_relaxed);
    return true;
}

bool LhtrClient::fire()
{
    return sendRequest (lhtr::TYPE_TRIGGER);
}

bool LhtrClient::ping()
{
    return sendRequest (lhtr::TYPE_PING);
}

void LhtrClient::run()
{
    uint8_t buf[128];

    while (! threadShouldExit())
    {
        if (socketFd < 0)
        {
            wait (50);
            continue;
        }

#ifdef _WIN32
        const int n = ::recv (socketFd, (char*) buf, (int) sizeof (buf), 0);
#else
        const ssize_t n = ::recv (socketFd, buf, sizeof (buf), 0);
#endif

        lhtr::Reply reply;
        if (n <= 0 || ! lhtr::parseReply (buf, (size_t) n, reply))
        {
            // Usually EWOULDBLOCK on an empty socket; otherwise a datagram that
            // is not ours. Sleeping here is fine -- nothing downstream waits on
            // an ACK, it only drives the UI.
            wait (2);
            continue;
        }

        acked.fetch_add (1, std::memory_order_relaxed);
        lastStatus.store ((int) reply.status, std::memory_order_relaxed);
        lastFlags.store (reply.flags, std::memory_order_relaxed);

        if (reply.is (lhtr::Status::Fired))
            firedCount.fetch_add (1, std::memory_order_relaxed);

        if ((reply.flags & lhtr::F_MCU_BUSY) != 0)
            mcuBusy.fetch_add (1, std::memory_order_relaxed);

        const uint64_t now = nowMicros();
        if (reply.clientTs != 0 && now > reply.clientTs)
            lastRttUs.store ((uint32_t) (now - reply.clientTs), std::memory_order_relaxed);

        lastPiUs.store ((uint32_t) (reply.piLatencyNs() / 1000), std::memory_order_relaxed);
    }
}

juce::String LhtrClient::getLastStatus() const
{
    const int s = lastStatus.load (std::memory_order_relaxed);
    if (s < 0)
        return "no reply yet";

    juce::String text;
    switch ((lhtr::Status) s)
    {
        case lhtr::Status::Fired: text = "fired"; break;
        case lhtr::Status::Duplicate: text = "duplicate"; break;
        case lhtr::Status::Pong: text = "pong"; break;
        case lhtr::Status::BadVersion: text = "bad version"; break;
        case lhtr::Status::BadType: text = "bad type"; break;
        case lhtr::Status::NoGpio: text = "no GPIO"; break;
        default: text = "status " + juce::String (s); break;
    }

    const uint8_t f = lastFlags.load (std::memory_order_relaxed);
    if ((f & lhtr::F_MCU_BUSY) != 0)
        text += " (MCU busy)";
    if ((f & lhtr::F_MCU_DOWN) != 0)
        text += " (MCU down)";
    if ((f & lhtr::F_GPIO_SIM) != 0)
        text += " (simulated GPIO)";

    return text;
}

void LhtrClient::resetCounters()
{
    sent.store (0);
    acked.store (0);
    firedCount.store (0);
    mcuBusy.store (0);
    sendErrors.store (0);
    lastRttUs.store (0);
    lastPiUs.store (0);
    lastStatus.store (-1);
    lastFlags.store (0);
}
