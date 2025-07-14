#include "TCPO_Socket.h"
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "Packet.h"
#include <string>
#include <random>
#include <stdexcept>

TCPO_Socket::TCPO_Socket()
    : sockfd(-1), is_listening(false), backlog_size(0), socket_active(true)
{
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0)
    {
        throw std::runtime_error("Socket creation failed");
    }

    std::cout << "Socket created successfully. FD: " << sockfd << std::endl;
}

TCPO_Socket::~TCPO_Socket()
{
    is_listening = false;
    socket_active = false;

    if (listener_thread.joinable())
    {
        listener_thread.join();
    }
    if (cleanup_thread.joinable())
    {
        cleanup_thread.join();
    }

    if (sockfd >= 0)
    {
        ::close(sockfd);
        std::cout << "Socket FD " << sockfd << " closed." << std::endl;
    }
}

bool TCPO_Socket::bind(const std::string &ip_address, uint16_t port)
{
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());
    if (ip_address == "0.0.0.0" || ip_address.empty())
    {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (::bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Bind failed" << std::endl;
        return false;
    }

    std::cout << "Socket bound successfully to " << ip_address << ":" << port << std::endl;
    return true;
}

bool TCPO_Socket::listen(int backlog)
{
    if (sockfd < 0)
    {
        return false;
    }

    this->backlog_size = backlog;
    this->is_listening = true;

    listener_thread = std::thread(&TCPO_Socket::_listener_entry, this);
    // cleanup_thread = std::thread(&TCPO_Socket::_cleanup_entry, this);

    std::cout << "Server is now listening..." << std::endl;
    return true;
}

void TCPO_Socket::_listener_entry()
{
    char buffer[4096];
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (is_listening)
    {
        int bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (bytes_received <= 0)
        {
            continue;
        }

        std::string client_key = std::string(inet_ntoa(client_addr.sin_addr)) + ":" + std::to_string(ntohs(client_addr.sin_port));

        std::vector<char> packet_data(buffer, buffer + bytes_received);
        Packet received_packet;
        received_packet.deserialize(packet_data);

        std::shared_ptr<Connection> connection = nullptr;

        {
            std::lock_guard<std::mutex> lock(connections_mutex);
            auto it = active_connections.find(client_key);
            if (it != active_connections.end())
            {
                connection = it->second;
            }
        }

        if (connection)
        {
            connection->process_incoming_packet(received_packet);
        }

        else if (received_packet.flags & SYN)
        {
            std::cout << "SYN packet received from a new client: " << client_key << std::endl;

            std::unique_lock<std::mutex> lock(queue_mutex);
            if (accept_queue.size() >= backlog_size)
            {
                std::cout << "Accept queue is full. Dropping new connection." << std::endl;
                continue;
            }
            lock.unlock();

            uint32_t client_initial_seq = received_packet.seq_num;
            
            Packet syn_ack_packet;
            syn_ack_packet.flags = SYN | ACK;
            syn_ack_packet.dest_port = received_packet.src_port;

            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<uint32_t> distrib;

            uint32_t initial_seq_num = distrib(gen);
            syn_ack_packet.seq_num = initial_seq_num;
            syn_ack_packet.ack_num = client_initial_seq + 1;

            std::vector<char> syn_ack_buffer;
            syn_ack_packet.serialize(syn_ack_buffer);
            sendto(sockfd, syn_ack_buffer.data(), syn_ack_buffer.size(), 0, (struct sockaddr *)&client_addr, addr_len);

            struct timeval tv;
            tv.tv_sec = 20;
            tv.tv_usec = 0;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
            
            bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len);

            tv.tv_sec = 0;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

            if (bytes_received > 0)
            {
                received_packet.deserialize(std::vector<char>(buffer, buffer + bytes_received));
                if (received_packet.flags & ACK)
                {
                    std::cout << "Final ACK received. Connection established." << std::endl;

                    uint32_t server_isn = syn_ack_packet.seq_num;

                    auto new_connection = std::make_shared<Connection>(sockfd, client_addr, server_isn + 1, client_initial_seq + 1);
                    new_connection->start();

                    {
                        std::lock_guard<std::mutex> conn_lock(connections_mutex);
                        active_connections[client_key] = new_connection;
                    }

                    lock.lock();
                    accept_queue.push({new_connection, client_addr});
                    lock.unlock();

                    cv.notify_one();
                }
            }
        }
        else
        {
            std::cout << "Received an unknown packet from " << client_key << std::endl;
        }
    }
}

std::pair<std::shared_ptr<Connection>, sockaddr_in> TCPO_Socket::accept()
{
    std::unique_lock<std::mutex> lock(queue_mutex);

    cv.wait(lock, [this]
            { return !accept_queue.empty(); });

    auto connection_info = accept_queue.front();
    accept_queue.pop();

    std::cout << "Connection accepted." << std::endl;
    return connection_info;
}

bool TCPO_Socket::connect(const std::string &ip_address, uint16_t port)
{
    struct timeval tv;
    tv.tv_sec = 20;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) < 0)
    {
        perror("setsockopt failed");
        return false;
    }

    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());

    Packet syn_packet;
    syn_packet.flags = SYN;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> distrib;

    uint32_t initial_seq_num = distrib(gen);
    syn_packet.seq_num = initial_seq_num;

    std::vector<char> syn_buffer;
    syn_packet.serialize(syn_buffer);

    const int MAX_RETRIES = 3;
    for (int i = 0; i < MAX_RETRIES; i++)
    {
        sendto(sockfd, syn_buffer.data(), syn_buffer.size(), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
        std::cout << "SYN sent (Attempt " << i + 1 << ")" << std::endl;

        char buffer[4096];
        socklen_t addr_len = sizeof(server_addr);
        int bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, &addr_len);

        if (bytes_received > 0)
        {
            Packet response_packet;
            response_packet.deserialize(std::vector<char>(buffer, buffer + bytes_received));

            if (response_packet.flags == (SYN | ACK))
            {
                Packet ack_packet;
                ack_packet.flags = ACK;
                ack_packet.seq_num = initial_seq_num + 1;
                ack_packet.ack_num = response_packet.seq_num + 1;

                std::vector<char> ack_buffer;
                ack_packet.serialize(ack_buffer);
                sendto(sockfd, ack_buffer.data(), ack_buffer.size(), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

                std::cout << "Connection established with server." << std::endl;

                uint32_t next_send_seq = initial_seq_num + 1;
                uint32_t next_expect_seq = response_packet.seq_num + 1;

                client_connection = std::make_shared<Connection>(sockfd, server_addr, next_send_seq, next_expect_seq);
                client_connection->start();
                client_connection->start_receiver();

                tv.tv_sec = 0;
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
                return true;
            }
        }
        std::cout << "Timeout, retrying..." << std::endl;
    }

    std::cout << "Connection failed." << std::endl;
    tv.tv_sec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    return false;
}

void TCPO_Socket::send(const std::vector<char> &data)
{
    if (client_connection)
    {
        client_connection->send(data);
    }
    else
    {
        std::cerr << "Error: Cannot send data. Not connected." << std::endl;
    }
}

size_t TCPO_Socket::receive(std::vector<char> &buffer, size_t max_len)
{
    if (client_connection)
    {
        return client_connection->receive(buffer, max_len);
    }

    std::cerr << "Error: Cannot receive data. Not connected." << std::endl;
    return 0;
}

void TCPO_Socket::_cleanup_entry()
{
    while (socket_active)
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::cout << "Cleanup thread running..." << std::endl;

        std::lock_guard<std::mutex> lock(connections_mutex);

        for (auto it = active_connections.begin(); it != active_connections.end();)
        {
            if (it->second->is_closed())
            {
                std::cout << "Removing closed connection for client: " << it->first << std::endl;
                it = active_connections.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

void TCPO_Socket::close()
{
    if (client_connection)
    {
        client_connection->close();
    }
    else if (is_listening)
    {
        is_listening = false;
        socket_active = false;

        std::lock_guard<std::mutex> lock(queue_mutex);
        while (!accept_queue.empty())
        {
            auto conn_pair = accept_queue.front();
            accept_queue.pop();
            conn_pair.first->close();
        }

       if (sockfd >= 0)
        {
            ::close(sockfd);
            sockfd = -1;
        }
    }
}