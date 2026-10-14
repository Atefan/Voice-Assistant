#include "Sockets.h"
#include "Errors.h"

#include <iostream>


FdGuard::FdGuard() : fd_(-1) {}
FdGuard::FdGuard(int fd) : fd_(fd) {}

FdGuard::FdGuard(FdGuard&& obj) noexcept : fd_(obj.fd_)
{
    obj.fd_ = -1;
}

FdGuard& FdGuard::operator=(FdGuard&& obj) noexcept
{
    // Self-move would otherwise close our own fd and then keep the number,
    // leaving a dead descriptor that gets closed a second time later.
    if (this != &obj)
    {
        if (fd_ >= 0) ::close(fd_);     // whatever we already hold is dropped here
        fd_     = obj.fd_;
        obj.fd_ = -1;
    }
    return *this;
}

FdGuard::~FdGuard() { if (fd_ >= 0) ::close(fd_); }
bool FdGuard::valid() const { return fd_ >= 0; }
int  FdGuard::get()   const { return fd_; }

namespace
{
    SocketError failure(const std::string& what)
    {
        return SocketError(what + ": " + std::strerror(errno));
    }

    void sendAll(int fd, const char* data, size_t n)
    {
        size_t sent = 0;
        while (sent < n)
        {
            const ssize_t k = ::send(fd, data + sent, n - sent, MSG_NOSIGNAL);

            if (k > 0)
            {
                sent += static_cast<size_t>(k);
                continue;
            }
            if (k < 0 && errno == EINTR)
                continue;

            throw failure("send() failed");
        }
    }

    bool recvAll(int fd, char* out, size_t n)
    {
        size_t got = 0;
        while (got < n)
        {
            const ssize_t k = ::recv(fd, out + got, n - got, 0);

            if (k > 0)
            {
                got += static_cast<size_t>(k);
                continue;
            }
            if (k == 0)
            {
                if (got == 0)
                    return false;

                throw SocketError("peer closed after " + std::to_string(got) + " of " +
                                  std::to_string(n) + " bytes: truncated frame");
            }
            if (errno == EINTR)
                continue;

            throw failure("recv() failed");
        }
        return true;
    }
}


FdGuard connectToClient(uint16_t port)
{
    // SOCK_CLOEXEC so the descriptor does not leak into any child process.
    FdGuard listener(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!listener.valid())
        throw failure("socket() failed");

    // Must be set before bind(). Without it, restarting the server inside the
    // 60 s TIME_WAIT window fails with EADDRINUSE.
    const int one = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
        throw failure("setsockopt(SO_REUSEADDR) failed");

    sockaddr_in addr{};                                 // zero-init: sin_zero must be zero
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);           // INADDR_LOOPBACK for localhost only
    addr.sin_port        = htons(port);

    if (::bind(listener.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0)
        throw failure("bind() to port " + std::to_string(port) + " failed");

    if (::listen(listener.get(), LISTEN_BACKLOG) < 0)
        throw failure("listen() failed");

    std::cout << "Server: listening on port " << port << ", waiting for a client\n";

    sockaddr_in peer{};
    socklen_t   peerLen = sizeof(peer);
    int         clientFd = -1;

    while (true)
    {
        clientFd = ::accept(listener.get(), reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (clientFd >= 0)
            break;

        if (errno == EINTR || errno == ECONNABORTED)
            continue;

        throw failure("accept() failed");
    }

    FdGuard client(clientFd);

    // Request/response traffic: without this, the small header write can sit in
    // the kernel waiting for an ACK and collect up to ~40 ms of Nagle delay.
    if (::setsockopt(client.get(), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0)
        throw failure("setsockopt(TCP_NODELAY) failed");

    char peerIp[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &peer.sin_addr, peerIp, sizeof(peerIp));
    std::cout << "Server: client connected from " << peerIp << ":" << ntohs(peer.sin_port)
              << " (fd " << client.get() << ")\n";

    return client;
}


std::optional<Frame> recvFrame(int fd)
{
    char header[FRAME_HEADER_SIZE] = {};

    if (!recvAll(fd, header, sizeof(header)))
        return std::nullopt;                            // clean close, nothing pending

    uint32_t netType = 0;
    uint32_t netLen  = 0;
    std::memcpy(&netType, header,     sizeof(netType));
    std::memcpy(&netLen,  header + 4, sizeof(netLen));

    Frame frame;
    frame.type = ntohl(netType);
    const uint32_t length = ntohl(netLen);

    // The length came off the wire, so it is not to be trusted with a resize().
    if (length > MAX_FRAME_PAYLOAD)
        throw SocketError("frame claims " + std::to_string(length) + " bytes, limit is " +
                          std::to_string(MAX_FRAME_PAYLOAD));

    frame.payload.resize(length);

    // A close here is mid-frame, so recvAll throws rather than returning false.
    if (length > 0)
        recvAll(fd, frame.payload.data(), length);

    return frame;
}


void sendFrame(int fd, const Frame& frame)
{
    if (frame.payload.size() > MAX_FRAME_PAYLOAD)
        throw SocketError("refusing to send " + std::to_string(frame.payload.size()) +
                          " bytes, limit is " + std::to_string(MAX_FRAME_PAYLOAD));

    // One buffer, one send: two writes would risk the header going out as its
    // own small packet.
    std::vector<char> out(FRAME_HEADER_SIZE + frame.payload.size());

    const uint32_t netType = htonl(frame.type);
    const uint32_t netLen  = htonl(static_cast<uint32_t>(frame.payload.size()));
    std::memcpy(out.data(),     &netType, sizeof(netType));
    std::memcpy(out.data() + 4, &netLen,  sizeof(netLen));

    if (!frame.payload.empty())
        std::memcpy(out.data() + FRAME_HEADER_SIZE, frame.payload.data(), frame.payload.size());

    sendAll(fd, out.data(), out.size());
}

void shutdownConnection(const FdGuard &fd)
{
    if(fd.valid())
        ::shutdown(fd.get(),SHUT_RDWR);
}
