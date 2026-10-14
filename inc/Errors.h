#ifndef SRC_ERRORS_H
#define SRC_ERRORS_H

#include <stdexcept>

class RequestError : public std::runtime_error
{
public:
    // for loading the constructors
    using std::runtime_error::runtime_error;
};

class ModelError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// anything the socket layer cannot recover from: a failed syscall, a malformed
// frame, a peer that vanished mid-message. what() carries strerror(errno).
class SocketError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

#endif
