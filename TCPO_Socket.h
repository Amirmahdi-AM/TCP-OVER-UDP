#pragma once

#include <string>
#include <cstdint>

class TCPO_Socket
{
public:
    TCPO_Socket();
    ~TCPO_Socket();

    bool bind(const std::string &ip_address, uint16_t port);

private:
    int sockfd;
};