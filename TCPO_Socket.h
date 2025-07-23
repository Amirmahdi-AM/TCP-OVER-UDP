#pragma once

#include <string>
#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <utility>
#include "Connection.h"
#include <map>
#include <memory>



class TCPO_Socket
{
public:
    TCPO_Socket();
    ~TCPO_Socket();

    bool bind(const std::string &ip_address, uint16_t port);

    bool listen(int backlog = 5);
    std::pair<std::shared_ptr<Connection>, sockaddr_in> accept();

    bool connect(const std::string &ip_address, uint16_t port);

    void send(const std::vector<char> &data);
    size_t receive(std::vector<char> &buffer, size_t max_len);
    void close();

private:
    int sockfd;
    bool is_listening = false;
    bool is_working = true;

    std::thread listener_thread;
    void _listener_entry();

    std::queue<std::pair<std::shared_ptr<Connection>, sockaddr_in>> accept_queue;
    int backlog_size;
    
    std::mutex queue_mutex;
    std::condition_variable cv;

    std::map<std::string, std::shared_ptr<Connection>> active_connections;
    std::mutex connections_mutex;

    std::shared_ptr<Connection> client_connection;
    std::thread cleanup_thread;
    void _cleanup_entry();
    bool socket_active;
};