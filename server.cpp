#include <iostream>
#include "TCPO_Socket.h"

void receiver(std::shared_ptr<Connection> &&conn)
{
    while (true)
    {
        std::vector<char> buffer(1024);
        auto bytes_received = conn->receive(buffer, 1024);
        if (bytes_received <= 0)
        {
            break;
        }
        std::string message(buffer.begin(), buffer.end());
        std::cout << "[CLIENT]: " << message << std::endl;
    }
}

int main()
{   
    TCPO_Socket sock;
    sock.bind("0.0.0.0", 8484);
    sock.listen();
    auto [conn, addr] = sock.accept();

    std::thread t1(receiver, conn);

    while (true)
    {
        std::string message;
        getline(std::cin, message);
        if (message == "CLOSE")
        {
            conn->close();
            break;
        }
        std::vector<char> buffer(message.begin(), message.end());
        conn->send(buffer);
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
    exit(0);
}