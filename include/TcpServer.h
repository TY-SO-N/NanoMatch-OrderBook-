#ifndef NANOMATCH_TCP_SERVER_H
#define NANOMATCH_TCP_SERVER_H

// ─── Platform-Conditional Networking Headers ─────────────────────────────────
#if defined(_WIN32) || defined(_MSC_VER)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SocketHandle = SOCKET;
    // INVALID_SOCKET is a macro (SOCKET)(~0), cannot be constexpr
    inline const SocketHandle INVALID_SOCK = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>    // TCP_NODELAY
    #include <arpa/inet.h>
    #include <unistd.h>         // close()
    #include <fcntl.h>          // fcntl(), O_NONBLOCK
    #include <cerrno>
    using SocketHandle = int;
    constexpr SocketHandle INVALID_SOCK = -1;
#endif

#include <vector>
#include "Protocol.h"
#include "RingBuffer.h"

// Link with Ws2_32.lib via CMake (Windows only)

namespace NanoMatch {

class TcpServer {
private:
    SocketHandle listen_socket_;
    SocketHandle client_socket_;
    RingBuffer<ClientMessage, 1048576>& queue_; // 1 Million capacity ring buffer
    bool running_;

    // Buffer for TCP Stream Framing
    std::vector<uint8_t> recv_buffer_;

public:
    TcpServer(RingBuffer<ClientMessage, 1048576>& queue);
    ~TcpServer();

    bool start(int port);
    void poll();
    void stop();
};

}

#endif // NANOMATCH_TCP_SERVER_H
