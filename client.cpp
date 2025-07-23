#include <iostream>
#include "TCPO_Socket.h"
#include <functional>

void receiver(TCPO_Socket &sock)
{
    while (true)
    {
        std::vector<char> buffer(1024);
        auto bytes_received = sock.receive(buffer, 1024);
        if (bytes_received == -1)
        {
            sock.close();
            break;
        }
        std::string message(buffer.begin(), buffer.end());
        std::cout << "[SEREVR]: " << message << std::endl;
    }
}

int main()
{ 
    TCPO_Socket sock;

    sock.connect("127.0.0.1", 8484);

    std::thread t1(receiver, std::ref(sock));

    while (1)
    {
        std::string message;
        getline(std::cin, message);
        std::vector<char> buffer(message.begin(), message.end());
        sock.send(buffer);
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
    exit(0);
}