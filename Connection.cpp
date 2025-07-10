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
void Connection::process_incoming_packet(const Packet &packet)
{
    std::lock_guard<std::mutex> lock(mtx);
    incoming_packet_queue.push(packet);
    cv_send.notify_one();
}
void Connection::_manager_entry()
{
    const size_t MSS = 1400;
    const auto RTO = std::chrono::seconds(1);

    while (active)
    {
        std::unique_lock<std::mutex> lock(mtx);

        cv_send.wait_for(lock, std::chrono::milliseconds(100), [this]
                         { return !send_buffer.empty() || !incoming_packet_queue.empty() || !active; });

        if (!active)
        {
            break;
        }

        auto now = std::chrono::steady_clock::now();
        for (auto &pair : unacked_packets)
        {
            auto &in_flight_packet = pair.second;
            auto time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - in_flight_packet.send_time);

            if (time_elapsed > RTO)
            {
                std::cout << "Timeout for packet SEQ: " << in_flight_packet.packet.seq_num << ". Resending." << std::endl;

                std::vector<char> packet_buffer;
                in_flight_packet.packet.serialize(packet_buffer);
                sendto(main_sockfd, packet_buffer.data(), packet_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

                in_flight_packet.send_time = now;
            }
        }

        while (!incoming_packet_queue.empty())
        {
            Packet packet = incoming_packet_queue.front();
            incoming_packet_queue.pop();

            if (packet.data_length > 0)
            {
                if (packet.seq_num == next_seq_num_to_expect)
                {
                    receive_buffer_in_order.append(packet.payload.begin(), packet.payload.end());
                    next_seq_num_to_expect += packet.data_length;

                    while (receive_buffer_ooo.count(next_seq_num_to_expect))
                    {
                        Packet &ooo_packet = receive_buffer_ooo.at(next_seq_num_to_expect);
                        receive_buffer_in_order.append(packet.payload.begin(), packet.payload.end());
                        next_seq_num_to_expect += ooo_packet.data_length;
                        receive_buffer_ooo.erase(ooo_packet.seq_num);
                    }

                    cv_receive.notify_one();
                }
                else if (packet.seq_num > next_seq_num_to_expect)
                {
                    receive_buffer_ooo[packet.seq_num] = packet;
                }

                Packet ack_packet;
                ack_packet.flags = ACK;
                ack_packet.ack_num = next_seq_num_to_expect;

                lock.unlock();
                std::vector<char> ack_buffer;
                ack_packet.serialize(ack_buffer);
                sendto(main_sockfd, ack_buffer.data(), ack_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
                lock.lock();
            }

            if (packet.flags & ACK)
            {
                uint32_t ack_num = packet.ack_num;

                if (ack_num > last_ack_received)
                {
                    last_ack_received = ack_num;
                    for (auto it = unacked_packets.begin(); it != unacked_packets.end();)
                    {
                        if (it->first < ack_num)
                        {
                            it = unacked_packets.erase(it);
                        }
                        else
                        {
                            ++it;
                        }
                    }
                    duplicate_ack_count = 0;
                }
                else if (ack_num == last_ack_received)
                {
                    duplicate_ack_count++;
                    if (duplicate_ack_count == 3)
                    {
                        std::cout << "3 duplicate ACKs received. Triggering Fast Retransmit for SEQ: " << ack_num << std::endl;

                        auto it = unacked_packets.find(ack_num);
                        if (it != unacked_packets.end())
                        {
                            auto &in_flight_packet = it->second;

                            std::vector<char> packet_buffer;
                            in_flight_packet.packet.serialize(packet_buffer);
                            sendto(main_sockfd, packet_buffer.data(), packet_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

                            in_flight_packet.send_time = std::chrono::steady_clock::now();
                        }
                        duplicate_ack_count = 0;
                    }
                }
            }
        }

        const size_t WINDOW_SIZE_PACKETS = 10;

        if (unacked_packets.size() < WINDOW_SIZE_PACKETS)
        {
            while (!send_buffer.empty())
            {
                if (unacked_packets.size() >= WINDOW_SIZE_PACKETS)
                {
                    break;
                }
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

                InFlightPacket in_flight_packet;
                in_flight_packet.packet = data_packet;
                in_flight_packet.send_time = std::chrono::steady_clock::now();

                unacked_packets[data_packet.seq_num] = in_flight_packet;

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
}