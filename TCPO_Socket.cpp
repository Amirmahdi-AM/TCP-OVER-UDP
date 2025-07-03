#include "TCPO_Socket.h"
#include <iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

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