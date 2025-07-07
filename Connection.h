#pragma once

#include <vector>
#include <string>
#include <deque>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <netinet/in.h>
#include "Packet.h"

enum class ConnectionState
{
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    TIME_WAIT,
    CLOSED
};

class Connection
{
    
public:
    Connection(int main_sockfd, const sockaddr_in &peer_addr);
    ~Connection();

    void send(const std::vector<char> &data);
    void receive();

    void start();
    void close();

    void process_incoming_packet(const Packet &packet);

private:
    void _manager_entry();

    int main_sockfd;
    sockaddr_in peer_addr;
    ConnectionState state;
    bool active;

    std::deque<char> send_buffer;
    std::map<uint32_t, Packet> receive_buffer_ooo;
    std::string receive_buffer_in_order;

    uint32_t next_seq_num_to_send;
    uint32_t last_ack_received;
    uint32_t next_seq_num_to_expect;

    std::thread manager_thread;
    std::mutex mtx;
    std::condition_variable cv_send;
    std::condition_variable cv_receive;
};