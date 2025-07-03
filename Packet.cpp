#include "Packet.h"
#include <cstring>
#include <arpa/inet.h>

void Packet::serialize(std::vector<char> &buffer) const
{
    const size_t header_size = sizeof(src_port) + sizeof(dest_port) + sizeof(seq_num) + sizeof(ack_num) + sizeof(flags) + sizeof(window_size) + sizeof(data_length);

    buffer.resize(header_size + payload.size());

    char *ptr = buffer.data();

    uint16_t net_src_port = htons(src_port);
    memcpy(ptr, &net_src_port, sizeof(net_src_port));
    ptr += sizeof(net_src_port);

    uint16_t net_dest_port = htons(dest_port);
    memcpy(ptr, &net_dest_port, sizeof(net_dest_port));
    ptr += sizeof(net_dest_port);

    uint32_t net_seq_num = htonl(seq_num);
    memcpy(ptr, &net_seq_num, sizeof(net_seq_num));
    ptr += sizeof(net_seq_num);

    uint32_t net_ack_num = htonl(ack_num);
    memcpy(ptr, &net_ack_num, sizeof(net_ack_num));
    ptr += sizeof(net_ack_num);

    memcpy(ptr, &flags, sizeof(flags));
    ptr += sizeof(flags);

    uint16_t net_window_size = htons(window_size);
    memcpy(ptr, &net_window_size, sizeof(net_window_size));
    ptr += sizeof(net_window_size);

    uint16_t net_data_length = htons(payload.size());
    memcpy(ptr, &net_data_length, sizeof(net_data_length));
    ptr += sizeof(net_data_length);

    if (!payload.empty())
    {
        memcpy(ptr, payload.data(), payload.size());
    }
}

void Packet::deserialize(const std::vector<char> &buffer)
{
    const char *ptr = buffer.data();

    uint16_t net_src_port;
    memcpy(&net_src_port, ptr, sizeof(net_src_port));
    src_port = ntohs(net_src_port);
    ptr += sizeof(net_src_port);

    uint16_t net_dest_port;
    memcpy(&net_dest_port, ptr, sizeof(net_dest_port));
    dest_port = ntohs(net_dest_port);
    ptr += sizeof(net_dest_port);

    uint32_t net_seq_num;
    memcpy(&net_seq_num, ptr, sizeof(net_seq_num));
    seq_num = ntohl(net_seq_num);
    ptr += sizeof(net_seq_num);

    uint32_t net_ack_num;
    memcpy(&net_ack_num, ptr, sizeof(net_ack_num));
    ack_num = ntohl(net_ack_num);
    ptr += sizeof(net_ack_num);

    memcpy(&flags, ptr, sizeof(flags));
    ptr += sizeof(flags);

    uint16_t net_window_size;
    memcpy(&net_window_size, ptr, sizeof(net_window_size));
    window_size = ntohs(net_window_size);
    ptr += sizeof(net_window_size);

    uint16_t net_data_length;
    memcpy(&net_data_length, ptr, sizeof(net_data_length));
    data_length = ntohs(net_data_length);
    ptr += sizeof(net_data_length);

    if (data_length > 0)
    {
        payload.assign(ptr, ptr + data_length);
    }
    else
    {
        payload.clear();
    }
}