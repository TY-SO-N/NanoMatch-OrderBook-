#ifndef NANOMATCH_TCP_SERVER_H
#define NANOMATCH_TCP_SERVER_H

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include "Protocol.h"
#include "RingBuffer.h"

// Link with Ws2_32.lib via CMake

namespace NanoMatch {

class TcpServer {
private:
    SOCKET listen_socket_;
    SOCKET client_socket_;
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
