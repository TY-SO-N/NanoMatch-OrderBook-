#include "TcpServer.h"
#include <iostream>
#include <cstring>

namespace NanoMatch {

TcpServer::TcpServer(RingBuffer<ClientMessage, 1048576>& queue) 
    : listen_socket_(INVALID_SOCKET), client_socket_(INVALID_SOCKET), queue_(queue), running_(false) {
    
    // Initialize Winsock
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
    }
}

TcpServer::~TcpServer() {
    stop();
    WSACleanup();
}

bool TcpServer::start(int port) {
    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Set Socket to Non-Blocking Mode
    u_long mode = 1;
    ioctlsocket(listen_socket_, FIONBIO, &mode);

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(listen_socket_);
        return false;
    }

    if (listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(listen_socket_);
        return false;
    }

    running_ = true;
    std::cout << "[TCP Server] Listening on port " << port << "..." << std::endl;
    return true;
}

void TcpServer::poll() {
    if (!running_) return;

    // 1. Accept new client (Single client for this HFT architecture)
    if (client_socket_ == INVALID_SOCKET) {
        SOCKET new_socket = accept(listen_socket_, NULL, NULL);
        if (new_socket != INVALID_SOCKET) {
            std::cout << "[TCP Server] Client connected!" << std::endl;
            
            // Bypass Nagle's Algorithm for zero-latency
            int flag = 1;
            setsockopt(new_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
            
            // Set client to non-blocking
            u_long mode = 1;
            ioctlsocket(new_socket, FIONBIO, &mode);

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

            // TCP Framer: Extract exact 16-byte ClientMessages
            size_t msg_size = sizeof(ClientMessage);
            while (recv_buffer_.size() >= msg_size) {
                ClientMessage msg;
                std::memcpy(&msg, recv_buffer_.data(), msg_size);
                
                // Push to Lock-Free Queue
                while (!queue_.push(msg)) {
                    // Backpressure if the engine is too slow (unlikely at 13M ops/sec)
                }

                // Erase the framed message from the buffer
                recv_buffer_.erase(recv_buffer_.begin(), recv_buffer_.begin() + msg_size);
            }
        } else if (bytes_read == 0 || (bytes_read == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)) {
            // Client disconnected gracefully or violently
            std::cout << "[TCP Server] Client disconnected." << std::endl;
            closesocket(client_socket_);
            client_socket_ = INVALID_SOCKET;
            recv_buffer_.clear();
        }
    }
}

void TcpServer::stop() {
    running_ = false;
    if (client_socket_ != INVALID_SOCKET) {
        closesocket(client_socket_);
        client_socket_ = INVALID_SOCKET;
    }
    if (listen_socket_ != INVALID_SOCKET) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }
}

}
