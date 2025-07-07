#include "TCPO_Socket.h"
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include "Packet.h"

TCPO_Socket::TCPO_Socket()
{
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0)
    {
        std::cerr << "Socket creation failed" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "Socket created successfully. FD: " << sockfd << std::endl;
}

TCPO_Socket::~TCPO_Socket()
{
    if (sockfd >= 0)
    {
        close(sockfd);
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

        std::vector<char> packet_data(buffer, buffer + bytes_received);
        Packet received_packet;
        received_packet.deserialize(packet_data);

        if (received_packet.flags & SYN)
        {
            std::cout << "SYN Packet received from a client." << std::endl;

            std::unique_lock<std::mutex> lock(queue_mutex);
            if (accept_queue.size() >= backlog_size)
            {
                std::cout << "Accept queue is full. Dropping new connection." << std::endl;
                continue;
            }
            lock.unlock();

            Packet syn_ack_packet;
            syn_ack_packet.flags = SYN | ACK;
            syn_ack_packet.dest_port = received_packet.src_port;

            std::vector<char> syn_ack_buffer;
            syn_ack_packet.serialize(syn_ack_buffer);
            sendto(sockfd, syn_ack_buffer.data(), syn_ack_buffer.size(), 0, (struct sockaddr *)&client_addr, addr_len);

            bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len);
            if (bytes_received > 0)
            {
                received_packet.deserialize(std::vector<char>(buffer, buffer + bytes_received));
                if (received_packet.flags & ACK)
                {
                    std::cout << "Final ACK received. Connection established.";
                    auto new_connection = std::make_shared<Connection>(sockfd, client_addr);
                    new_connection->start();

                    lock.lock();
                    accept_queue.push({new_connection, client_addr});
                    lock.unlock();

                    cv.notify_one();
                }
            }
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
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());

    Packet syn_packet;
    syn_packet.flags = SYN;
    syn_packet.seq_num = 100;

    std::vector<char> syn_buffer;
    syn_packet.serialize(syn_buffer);

    sendto(sockfd, syn_buffer.data(), syn_buffer.size(), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
    std::cout << "SYN packet sent." << std::endl;

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

            std::vector<char> ack_buffer;
            ack_packet.serialize(ack_buffer);
            sendto(sockfd, ack_buffer.data(), ack_buffer.size(), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));

            std::cout << "Connection established with server." << std::endl;
            return true;
        }
    }

    std::cout << "Connection failed." << std::endl;
    return false;
}