#include "Connection.h"
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>

Connection::Connection(int main_sockfd, const sockaddr_in &peer_addr)
    : main_sockfd(main_sockfd), peer_addr(peer_addr), state(ConnectionState::ESTABLISHED),
      active(false), next_seq_num_to_send(1000), last_ack_received(1000), next_seq_num_to_expect(2000) {}
Connection::~Connection()
{
    active = false;
    if (manager_thread.joinable())
    {
        manager_thread.join();
    }
}

void Connection::start()
{
    active = true;
    manager_thread = std::thread(&Connection::_manager_entry, this);
}

void Connection::send(const std::vector<char> &data)
{
    std::unique_lock<std::mutex> lock(mtx);

    send_buffer.insert(send_buffer.end(), data.begin(), data.end());

    std::cout << data.size() << " bytes added to the send buffer." << std::endl;

    cv_send.notify_one();
}

size_t Connection::receive(std::vector<char> &buffer, size_t max_len)
{
    std::unique_lock<std::mutex> lock(mtx);

    cv_receive.wait(lock, [this]
                    { return !receive_buffer_in_order.empty() || !active; });

    if (!active && receive_buffer_in_order.empty())
    {
        return 0;
    }

    size_t bytes_to_copy = std::min(max_len, receive_buffer_in_order.size());

    buffer.assign(receive_buffer_in_order.begin(), receive_buffer_in_order.begin() + bytes_to_copy);

    receive_buffer_in_order.erase(0, bytes_to_copy);

    return bytes_to_copy;
}

void Connection::close() { /* TODO */ }
void Connection::process_incoming_packet(const Packet &packet) { /* TODO */ }
void Connection::_manager_entry()
{
    const size_t MSS = 1400;

    while (active)
    {
        std::unique_lock<std::mutex> lock(mtx);

        cv_send.wait(lock, [this]
                     { return !send_buffer.empty() || !active; });

        if (!active && send_buffer.empty())
        {
            break;
        }

        while (!send_buffer.empty())
        {
            size_t chunk_size = std::min(send_buffer.size(), MSS);
            std::vector<char> payload(chunk_size);

            for (size_t i = 0; i < chunk_size; i++)
            {
                payload[i] = send_buffer.front();
                send_buffer.pop_front();
            }

            Packet data_packet;
            data_packet.payload = payload;
            data_packet.data_length = payload.size();
            data_packet.seq_num = next_seq_num_to_send;

            data_packet.ack_num = next_seq_num_to_expect;
            data_packet.flags = ACK;

            lock.unlock();

            std::vector<char> packet_buffer;
            data_packet.serialize(packet_buffer);

            sendto(main_sockfd, packet_buffer.data(), packet_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
            std::cout << "Sent a packet of " << data_packet.data_length << " bytes. SEQ: " << data_packet.seq_num << std::endl;

            next_seq_num_to_send += data_packet.data_length;

            lock.lock();
        }
    }
}