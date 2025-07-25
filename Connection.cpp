#include "Connection.h"
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cmath>

const size_t MSS = 1400;

Connection::Connection(int main_sockfd, const sockaddr_in &peer_addr, uint32_t initial_send_seq, uint32_t initial_expect_seq)
    : main_sockfd(main_sockfd), peer_addr(peer_addr), state(ConnectionState::ESTABLISHED),
      active(false),
      next_seq_num_to_send(initial_send_seq),
      last_ack_received(initial_send_seq),
      next_seq_num_to_expect(initial_expect_seq),
      rwnd(MSS),
      cwnd(MSS),
      bytes_in_slide_window(0)
{
}

Connection::~Connection()
{
    active = false;

    cv_send.notify_one();

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

void Connection::start_receiver()
{
    receiver_thread = std::thread(&::Connection::_client_receiver, this);
}

void Connection::_client_receiver()
{
    char buffer[4096];
    sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    while (!this->is_closed())
    {
        int bytes_received = recvfrom(main_sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&server_addr, &addr_len);
        if (bytes_received <= 0)
        {
            continue;
        }

        if (server_addr.sin_addr.s_addr != this->peer_addr.sin_addr.s_addr || server_addr.sin_port != this->peer_addr.sin_port)
        {
            std::cout << "Received a packet from an unknown source. Ignoring." << std::endl;
            continue;
        }

        std::vector<char> packet_data(buffer, buffer + bytes_received);
        Packet received_packet;
        received_packet.deserialize(packet_data);

        this->process_incoming_packet(received_packet);
    }
}

void Connection::send(const std::vector<char> &data)
{
    std::unique_lock<std::mutex> lock(mtx);

    send_buffer.insert(send_buffer.end(), data.begin(), data.end());

    std::cout << data.size() << " bytes added to the send buffer." << std::endl;

    cv_send.notify_one();

    latest_activity = std::chrono::steady_clock::now();
}

size_t Connection::receive(std::vector<char> &buffer, size_t max_len)
{
    std::unique_lock<std::mutex> lock(mtx);

    cv_receive.wait_for(lock, std::chrono::minutes(5), [this]
                        { return !receive_buffer_in_order.empty() || state != ConnectionState::ESTABLISHED; });

    if (state != ConnectionState::ESTABLISHED && receive_buffer_in_order.empty())
    {
        return -1;
    }
    else if (receive_buffer_in_order.empty())
    {
        return 0;
    }

    latest_activity = std::chrono::steady_clock::now();

    size_t bytes_to_copy = std::min(max_len, receive_buffer_in_order.size());

    if ((10 * MSS) - receive_buffer_in_order.size() < MSS && (10 * MSS + bytes_to_copy) - receive_buffer_in_order.size() >= MSS)
    {
        Packet ack_packet;
        ack_packet.seq_num = next_seq_num_to_send;
        ack_packet.ack_num = next_seq_num_to_expect;
        ack_packet.flags = ACK;
        ack_packet.window_size = (10 * MSS + bytes_to_copy) - receive_buffer_in_order.size();

        std::vector<char> ack_buffer;
        ack_packet.serialize(ack_buffer);

        sendto(main_sockfd, ack_buffer.data(), ack_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
    }

    buffer.assign(receive_buffer_in_order.begin(), receive_buffer_in_order.begin() + bytes_to_copy);

    receive_buffer_in_order.erase(0, bytes_to_copy);

    return bytes_to_copy;
}

void Connection::close()
{
    std::unique_lock<std::mutex> lock(mtx);

    if (state == ConnectionState::ESTABLISHED)
    {
        state = ConnectionState::FIN_WAIT_1;
        std::cout << "Connection state changed to FIN_WAIT_1" << std::endl;

        Packet fin_packet;
        fin_packet.flags = FIN | ACK;
        fin_packet.seq_num = next_seq_num_to_send;
        fin_packet.ack_num = next_seq_num_to_expect;

        fin_sent_seq = fin_packet.seq_num;
        next_seq_num_to_send++;

        lock.unlock();

        std::vector<char> buffer;
        fin_packet.serialize(buffer);
        sendto(main_sockfd, buffer.data(), buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

        std::cout << "Sent FIN packet." << std::endl;
    }
    else if (state == ConnectionState::CLOSE_WAIT)
    {
        state = ConnectionState::LAST_ACK;
        std::cout << "Connection state changed to LAST_ACK" << std::endl;

        Packet fin_packet;
        fin_packet.flags = FIN | ACK;
        fin_packet.seq_num = next_seq_num_to_send;
        fin_packet.ack_num = next_seq_num_to_expect;

        fin_sent_seq = fin_packet.seq_num;
        next_seq_num_to_send++;

        lock.unlock();

        std::vector<char> buffer;
        fin_packet.serialize(buffer);
        sendto(main_sockfd, buffer.data(), buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

        std::cout << "Sent FIN packet from CLOSE_WAIT." << std::endl;
    }
}

void Connection::process_incoming_packet(const Packet &packet)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (active)
    {
        incoming_packet_queue.push(packet);
    }
    cv_send.notify_one();
}

void Connection::_manager_entry()
{
    auto RTO = std::chrono::milliseconds(1000);
    auto estimatedRTT = std::chrono::milliseconds(1000);
    auto devRTT = std::chrono::milliseconds(0);

    std::chrono::steady_clock::time_point probe_send_time;
    auto ZWPT = std::chrono::milliseconds(0);

    auto active_timer = std::chrono::minutes(10);
    latest_activity = std::chrono::steady_clock::now();

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

                lock.unlock();

                sendto(main_sockfd, packet_buffer.data(), packet_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

                lock.lock();

                in_flight_packet.send_time = now;

                cwnd = MSS;
            }
        }

        if (rwnd <= 0)
        {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - probe_send_time) > ZWPT)
            {
                Packet probe_packet;
                probe_packet.seq_num = next_seq_num_to_send - 1;
                probe_packet.ack_num = next_seq_num_to_expect;
                probe_packet.flags = ACK;
                probe_packet.window_size = (10 * MSS) - receive_buffer_in_order.size();
                probe_packet.payload.resize(1);
                probe_packet.payload[0] = 'P';

                std::vector<char> probe_buffer;
                probe_packet.serialize(probe_buffer);

                lock.unlock();

                sendto(main_sockfd, probe_buffer.data(), probe_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

                lock.lock();

                probe_send_time = now;
                ZWPT = ZWPT * 2;
            }
        }

        if (std::chrono::duration_cast<std::chrono::minutes>(now - latest_activity) > active_timer)
        {
            this->close();
        }

        while (!incoming_packet_queue.empty())
        {
            Packet packet = incoming_packet_queue.front();
            incoming_packet_queue.pop();

            if (packet.flags & FIN)
            {
                std::cout << "FIN packet received. Current state: " << (int)state << std::endl;

                if (state == ConnectionState::ESTABLISHED)
                {
                    state = ConnectionState::CLOSE_WAIT;

                    Packet ack_packet;
                    ack_packet.flags = ACK;
                    ack_packet.ack_num = packet.seq_num + 1;
                    ack_packet.seq_num = next_seq_num_to_send;

                    std::vector<char> buffer;
                    ack_packet.serialize(buffer);

                    lock.unlock();

                    sendto(main_sockfd, buffer.data(), buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

                    lock.lock();

                    cv_receive.notify_all();
                }
                else if (state == ConnectionState::FIN_WAIT_1)
                {
                    state = ConnectionState::TIME_WAIT;

                    Packet ack_packet;
                    ack_packet.flags = ACK;
                    ack_packet.ack_num = packet.seq_num + 1;
                    ack_packet.seq_num = next_seq_num_to_send;

                    lock.unlock();

                    std::vector<char> buffer;
                    ack_packet.serialize(buffer);
                    sendto(main_sockfd, buffer.data(), buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

                    active = false;
                }
                else if (state == ConnectionState::FIN_WAIT_2)
                {
                    state = ConnectionState::CLOSED;

                    Packet ack_packet;
                    ack_packet.flags = ACK;
                    ack_packet.ack_num = packet.seq_num + 1;
                    ack_packet.seq_num = next_seq_num_to_send;

                    lock.unlock();

                    std::vector<char> buffer;
                    ack_packet.serialize(buffer);
                    sendto(main_sockfd, buffer.data(), buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

                    std::cout << "Sent LAST_ACK. Connection CLOSED." << std::endl;

                    active = false;
                }
            }

            if (packet.data_length > 0 && state == ConnectionState::ESTABLISHED)
            {
                std::cout << "Packet with SQE=" << packet.seq_num << " received" << std::endl;
                if (packet.seq_num == next_seq_num_to_expect)
                {
                    receive_buffer_in_order.append(packet.payload.begin(), packet.payload.end());
                    next_seq_num_to_expect += packet.data_length;

                    while (receive_buffer_ooo.count(next_seq_num_to_expect))
                    {
                        Packet &ooo_packet = receive_buffer_ooo.at(next_seq_num_to_expect);
                        receive_buffer_in_order.append(ooo_packet.payload.begin(), ooo_packet.payload.end());
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
                ack_packet.seq_num = next_seq_num_to_send;
                ack_packet.window_size = (10 * MSS) - receive_buffer_in_order.size();
                if (ack_packet.window_size < MSS)
                {
                    ack_packet.window_size = 0;
                }
                std::cout << "Send a packet with ACK=" << ack_packet.ack_num << " and window_size=" << ack_packet.window_size << std::endl;

                lock.unlock();
                std::vector<char> ack_buffer;
                ack_packet.serialize(ack_buffer);
                sendto(main_sockfd, ack_buffer.data(), ack_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
                lock.lock();
            }

            if (packet.flags & ACK)
            {
                uint32_t ack_num = packet.ack_num;

                if (state == ConnectionState::FIN_WAIT_1)
                {
                    if (packet.ack_num == fin_sent_seq + 1)
                    {
                        state = ConnectionState::FIN_WAIT_2;
                        std::cout << "Connection state changed to FIN_WAIT_2" << std::endl;
                    }
                }
                else if (state == ConnectionState::LAST_ACK)
                {
                    if (packet.ack_num == fin_sent_seq + 1)
                    {
                        state = ConnectionState::CLOSED;
                        active = false;
                        std::cout << "Connection CLOSED." << std::endl;
                    }
                }

                else
                {
                    rwnd = packet.window_size;
                    if (rwnd <= 0)
                    {
                        probe_send_time = now;
                        if (ZWPT == std::chrono::milliseconds(0))
                        {
                            ZWPT = std::chrono::milliseconds(1000);
                        }
                    }
                    else
                    {
                        ZWPT = std::chrono::milliseconds(0);
                    }

                    if (ack_num > next_seq_num_to_send)
                    {
                        Packet rst_packet;
                        rst_packet.flags = RST;
                        std::vector<char> rst_buffer;
                        rst_packet.serialize(rst_buffer);
                        sendto(main_sockfd, rst_buffer.data(), rst_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
                        state = ConnectionState::CLOSED;
                        active = false;
                        std::cout << "Connection was CLOSED with RST falg." << std::endl;
                    }

                    else if (ack_num > last_ack_received)
                    {
                        std::cout << "ACK " << ack_num << " received." << std::endl;
                        last_ack_received = ack_num;
                        for (auto it = unacked_packets.begin(); it != unacked_packets.end();)
                        {
                            if (it->first < ack_num)
                            {
                                auto sampleRTT = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.send_time);
                                estimatedRTT = std::chrono::duration_cast<std::chrono::milliseconds>(0.875 * estimatedRTT + 0.125 * sampleRTT);
                                devRTT = std::chrono::duration_cast<std::chrono::milliseconds>(0.75 * devRTT + 0.25 * abs(sampleRTT - estimatedRTT));
                                RTO = std::chrono::duration_cast<std::chrono::milliseconds>(estimatedRTT + 4 * devRTT);

                                bytes_in_slide_window -= it->second.packet.data_length;
                                it = unacked_packets.erase(it);
                            }
                            else
                            {
                                ++it;
                            }
                        }
                        duplicate_ack_count = 0;

                        cwnd += MSS;
                    }
                    else if (ack_num == last_ack_received)
                    {
                        duplicate_ack_count++;
                        if (duplicate_ack_count == 3)
                        {

                            auto it = unacked_packets.find(ack_num);
                            if (it != unacked_packets.end())
                            {
                                std::cout << "3 duplicate ACKs received. Triggering Fast Retransmit for SEQ: " << ack_num << std::endl;
                                auto &in_flight_packet = it->second;

                                std::vector<char> packet_buffer;
                                in_flight_packet.packet.serialize(packet_buffer);

                                lock.unlock();

                                sendto(main_sockfd, packet_buffer.data(), packet_buffer.size(), 0, (struct sockaddr *)&peer_addr, sizeof(peer_addr));

                                lock.lock();

                                in_flight_packet.send_time = std::chrono::steady_clock::now();

                                cwnd /= 2;
                            }
                            duplicate_ack_count = 0;
                        }
                    }
                }
            }

            if (packet.flags & RST)
            {
                state = ConnectionState::CLOSED;
                active = false;
                std::cout << "Connection was CLOSED with RST falg." << std::endl;
            }
        }

        const size_t WINDOW_SIZE_PACKETS = 10;

        if (unacked_packets.size() < WINDOW_SIZE_PACKETS)
        {
            while (!send_buffer.empty())
            {
                if (unacked_packets.size() >= WINDOW_SIZE_PACKETS || bytes_in_slide_window >= rwnd || bytes_in_slide_window >= cwnd)
                {
                    break;
                }
                size_t chunk_size = std::min(send_buffer.size(), MSS);
                chunk_size = std::min((size_t)rwnd, chunk_size);
                chunk_size = std::min((size_t)cwnd, chunk_size);
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
                data_packet.window_size = (10 * MSS) - receive_buffer_in_order.size();
                if (data_packet.window_size < MSS)
                {
                    data_packet.window_size = 0;
                }

                InFlightPacket in_flight_packet;
                in_flight_packet.packet = data_packet;
                in_flight_packet.send_time = std::chrono::steady_clock::now();

                unacked_packets[data_packet.seq_num] = in_flight_packet;
                bytes_in_slide_window += chunk_size;

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

bool Connection::is_closed() const
{
    std::lock_guard<std::mutex> lock(mtx);
    return state == ConnectionState::CLOSED;
}