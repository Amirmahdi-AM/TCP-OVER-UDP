#pragma once

#include <vector>
#include <string>
#include <deque>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <netinet/in.h>
#include <queue>
#include "Packet.h"
#include <chrono>

enum class ConnectionState
{
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    TIME_WAIT,
    CLOSE_WAIT,
    LAST_ACK,
    CLOSED
};

class Connection
{
    
public:
    Connection(int main_sockfd, const sockaddr_in &peer_addr, uint32_t initial_send_seq, uint32_t initial_expect_seq);
    ~Connection();

    void send(const std::vector<char> &data);
    size_t receive(std::vector<char> &buffer, size_t max_len);

    void start();
    void close();

    void process_incoming_packet(const Packet &packet);

    bool is_closed() const;

    void start_receiver();

private:
    struct InFlightPacket
    {
        Packet packet;
        std::chrono::steady_clock::time_point send_time;
    };

    void _manager_entry();
    void _client_receiver();

    int main_sockfd;
    sockaddr_in peer_addr;
    ConnectionState state;
    bool active;

    std::deque<char> send_buffer;
    std::map<uint32_t, Packet> receive_buffer_ooo;
    std::string receive_buffer_in_order;
    std::queue<Packet> incoming_packet_queue;
    std::map<uint32_t, InFlightPacket> unacked_packets;

    uint32_t next_seq_num_to_send;
    uint32_t last_ack_received;
    uint32_t next_seq_num_to_expect;
    uint32_t duplicate_ack_count;
    uint32_t last_ack_num_for_retransmit;
    uint32_t fin_sent_seq;
    uint16_t rwnd;
    uint16_t cwnd;

    std::thread manager_thread;
    mutable std::mutex mtx;
    std::condition_variable cv_send;
    std::condition_variable cv_receive;
    std::thread receiver_thread;

    std::chrono::steady_clock::time_point latest_activity;
};