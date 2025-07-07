#pragma once

#include <string>
#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <utility>
#include "Connection.h"


class TCPO_Socket
{
public:
    TCPO_Socket();
    ~TCPO_Socket();

    bool bind(const std::string &ip_address, uint16_t port);

    bool listen(int backlog = 5);
    std::pair<std::shared_ptr<Connection>, sockaddr_in> accept();

    bool connect(const std::string &ip_address, uint16_t port);

private:
    int sockfd;
    bool is_listening = false;

    std::thread listener_thread;
    void _listener_entry();

    std::queue<std::pair<std::shared_ptr<Connection>, sockaddr_in>> accept_queue;
    int backlog_size;
    
    std::mutex queue_mutex;
    std::condition_variable cv;
};