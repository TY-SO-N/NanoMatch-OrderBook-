#include "TcpServer.h"
#include <iostream>
#include <cstring>

// ─── Platform Abstraction Layer ──────────────────────────────────────────────
// These inline helpers isolate every platform-specific socket call so the
// TcpServer methods themselves remain 100% platform-neutral.

namespace platform {

inline void initNetworking() {
    #if defined(_WIN32) || defined(_MSC_VER)
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
    }
    #endif
    // No-op on Unix — BSD sockets need no global init
}

inline void cleanupNetworking() {
    #if defined(_WIN32) || defined(_MSC_VER)
    WSACleanup();
    #endif
}

inline void closeSocket(SocketHandle s) {
    #if defined(_WIN32) || defined(_MSC_VER)
    closesocket(s);
    #else
    close(s);
    #endif
}

inline void setNonBlocking(SocketHandle s) {
    #if defined(_WIN32) || defined(_MSC_VER)
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
    #else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    #endif
}

inline int getLastError() {
    #if defined(_WIN32) || defined(_MSC_VER)
    return WSAGetLastError();
    #else
    return errno;
    #endif
}

inline bool wouldBlock() {
    #if defined(_WIN32) || defined(_MSC_VER)
    return WSAGetLastError() == WSAEWOULDBLOCK;
    #else
    return errno == EWOULDBLOCK || errno == EAGAIN;
    #endif
}

} // namespace platform

// ─── TcpServer Implementation ───────────────────────────────────────────────

namespace NanoMatch {

TcpServer::TcpServer(RingBuffer<ClientMessage, 1048576>& queue) 
    : listen_socket_(INVALID_SOCK), client_socket_(INVALID_SOCK), queue_(queue), running_(false) {
    
    platform::initNetworking();
}

TcpServer::~TcpServer() {
    stop();
    platform::cleanupNetworking();
}

bool TcpServer::start(int port) {
    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCK) {
        std::cerr << "Socket creation failed: " << platform::getLastError() << std::endl;
        return false;
    }

    // Set Socket to Non-Blocking Mode
    platform::setNonBlocking(listen_socket_);

    // Unix-only: Allow immediate port reuse on restart (avoids TIME_WAIT bind failures)
    #if !defined(_WIN32) && !defined(_MSC_VER)
    int opt = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #endif

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed: " << platform::getLastError() << std::endl;
        platform::closeSocket(listen_socket_);
        listen_socket_ = INVALID_SOCK;
        return false;
    }

    if (listen(listen_socket_, SOMAXCONN) < 0) {
        std::cerr << "Listen failed: " << platform::getLastError() << std::endl;
        platform::closeSocket(listen_socket_);
        listen_socket_ = INVALID_SOCK;
        return false;
    }

    running_ = true;
    std::cout << "[TCP Server] Listening on port " << port << "..." << std::endl;
    return true;
}

void TcpServer::poll() {
    if (!running_) return;

    // 1. Accept new client (Single client for this HFT architecture)
    if (client_socket_ == INVALID_SOCK) {
        SocketHandle new_socket = accept(listen_socket_, NULL, NULL);
        if (new_socket != INVALID_SOCK) {
            std::cout << "[TCP Server] Client connected!" << std::endl;
            
            // Bypass Nagle's Algorithm for zero-latency
            int flag = 1;
            setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));

            // Unix-only: Enable kernel busy-polling for ultra-low latency socket reads
            #if defined(__linux__) && defined(SO_BUSY_POLL)
            int busy_poll_us = 50; // Spin for up to 50μs before blocking
            setsockopt(new_socket, SOL_SOCKET, SO_BUSY_POLL, &busy_poll_us, sizeof(busy_poll_us));
            #endif
            
            // Set client to non-blocking
            platform::setNonBlocking(new_socket);

            client_socket_ = new_socket;
        }
    } 
    // 2. Read from connected client
    else {
        uint8_t temp_buf[4096];
        int bytes_read = recv(client_socket_, (char*)temp_buf, sizeof(temp_buf), 0);
        
        if (bytes_read > 0) {
            // Append to framer buffer
            recv_buffer_.insert(recv_buffer_.end(), temp_buf, temp_buf + bytes_read);

            // TCP Framer: Extract exact 16-byte ClientMessages efficiently
            size_t msg_size = sizeof(ClientMessage);
            size_t total_bytes = recv_buffer_.size();
            size_t num_msgs = total_bytes / msg_size;
            size_t bytes_to_consume = num_msgs * msg_size;

            for (size_t i = 0; i < bytes_to_consume; i += msg_size) {
                ClientMessage msg;
                std::memcpy(&msg, recv_buffer_.data() + i, msg_size);
                
                // Push to Lock-Free Queue
                while (!queue_.push(msg)) {
                    // Backpressure if the engine is too slow (unlikely at 13M ops/sec)
                }
            }

            // Bulk erase processed messages in a single O(N) operation instead of O(N^2)
            if (bytes_to_consume > 0) {
                recv_buffer_.erase(recv_buffer_.begin(), recv_buffer_.begin() + bytes_to_consume);
            }
        } else if (bytes_read == 0 || (bytes_read < 0 && !platform::wouldBlock())) {
            // Client disconnected gracefully or violently
            std::cout << "[TCP Server] Client disconnected." << std::endl;
            platform::closeSocket(client_socket_);
            client_socket_ = INVALID_SOCK;
            recv_buffer_.clear();
        }
    }
}

void TcpServer::stop() {
    running_ = false;
    if (client_socket_ != INVALID_SOCK) {
        platform::closeSocket(client_socket_);
        client_socket_ = INVALID_SOCK;
    }
    if (listen_socket_ != INVALID_SOCK) {
        platform::closeSocket(listen_socket_);
        listen_socket_ = INVALID_SOCK;
    }
}

}
