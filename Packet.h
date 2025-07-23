#pragma once
#include <cstdint>
#include <vector>


enum ControlFlags
{
    FIN = 1 << 0,
    SYN = 1 << 1,
    RST = 1 << 2,
    ACK = 1 << 3,
};

class Packet
{
public:
    Packet();
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t flags;
    uint16_t window_size;
    uint16_t data_length;
    // uint16_t checksum;

    std::vector<char> payload;

    void serialize(std::vector<char> &buffer) const;
    void deserialize(std::vector<char> &buffer);
};