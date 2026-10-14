#ifndef SRC_SOCKETS_H
#define SRC_SOCKETS_H

// Everything the POSIX socket API needs. Kept in the header so Sockets.cpp and
// anything that drives it (Server.cpp) see the same declarations.
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// This layer deliberately knows nothing about Task, json or the models: it
// moves framed byte blobs over a socket, nothing more. Protocol.h is what turns
// a Frame into a Task.

constexpr uint16_t TALK_PORT = 5555;

constexpr size_t FRAME_HEADER_SIZE = 8;

constexpr uint32_t FRAME_TYPE_REPLY = 0xFFFFFFFFu;

constexpr uint32_t MAX_FRAME_PAYLOAD = 16u * 1024u * 1024u;

constexpr int LISTEN_BACKLOG = 4;

struct Frame
{
    uint32_t type = 0;
    std::vector<char> payload;
};

class FdGuard
{
public:
    FdGuard();
    // explicit: without it an int silently converts to an FdGuard, and whatever
    // produced that int suddenly has its descriptor closed for it
    explicit FdGuard(int fd);
    ~FdGuard();

    FdGuard(const FdGuard&)            = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    // move constructor: the source must give up the fd, or both destructors
    // close the same number. noexcept so containers will actually use it.
    FdGuard(FdGuard&& obj) noexcept;
    FdGuard& operator=(FdGuard&& obj) noexcept;

    bool valid() const;
    int  get()   const;

private:
    int fd_ = -1;
};

// By value, not by reference: the guard is a local inside connectToClient, so
// returning FdGuard&& would hand back a reference to a destroyed object.
FdGuard connectToClient(uint16_t port = TALK_PORT);

std::optional<Frame> recvFrame(int fd);
void sendFrame(int fd, const Frame& frame);
void shutdownConnection(const FdGuard& fd);

#endif
