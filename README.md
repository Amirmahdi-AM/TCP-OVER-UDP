# TCP-Over-UDP: A Reliable Transport Protocol Simulation in C++

This project is an academic implementation of a reliable transport protocol that simulates core features of TCP using only UDP sockets. It demonstrates key concepts in computer networking, including connection management, reliable data transfer, flow control, and graceful shutdowns, all built from the ground up in C++11.

## Features Implemented

This implementation covers both mandatory and advanced features of a TCP-like protocol:

### Core Reliability
- **Connection Management:**
  - ✅ **3-Way Handshake:** Establishes connections gracefully (`SYN`, `SYN-ACK`, `ACK`) with random Initial Sequence Numbers (ISN).
  - ✅ **4-Way Handshake:** Terminates connections cleanly (`FIN`, `FIN-ACK`).
- **Reliable Data Transfer:**
  - ✅ **Sequencing & Acknowledgments (ACKs):** Ensures all data is tracked and its receipt is confirmed.
  - ✅ **In-Order Delivery:** Correctly reorders out-of-order packets at the receiver.
  - ✅ **Retransmission Mechanisms:**
    - **Timeout-based Retransmission (RTO):** Resends packets that are not acknowledged within a specific time frame.
    - **Fast Retransmit:** Resends a packet immediately upon receiving 3 duplicate ACKs.

### Advanced Features
- **Multi-Client Server:**
  - ✅ A robust, multi-threaded server architecture capable of handling multiple concurrent clients.
  - ✅ Graceful shutdown and resource management with a dedicated cleanup thread to prevent memory leaks.
- **Flow Control:**
  - ✅ **Sliding Window Protocol:** Implemented on the sender side to prevent overwhelming the receiver.

## Project Structure

The project is organized into several key C++ files:

- **`Packet.h` / `Packet.cpp`**: Defines the protocol's packet structure, including the header. It handles serialization and deserialization of packets for network transmission.
- **`Connection.h` / `Connection.cpp`**: The core of the protocol logic. Each `Connection` object manages a single, active connection, including its state machine, buffers, sequence numbers, and reliability mechanisms.
- **`TCPO_Socket.h` / `TCPO_Socket.cpp`**: Provides the main API (`TCPO_Socket` class) for applications. It acts as a facade for both client and server operations like `bind()`, `listen()`, `accept()`, and `connect()`.
- **`server.cpp`**: An example server application that listens for connections, receives data, and echoes it back to the client.
- **`client.cpp`**: An example client application that connects to the server, sends a series of messages, and receives the responses.

## Requirements

- **C++11 (or newer) compatible compiler** (e.g., `g++` or `clang++`).
- **POSIX-compliant operating system** (e.g., Linux, macOS, WSL on Windows) for socket and threading APIs.
- No external libraries are needed, just the standard C++ library and POSIX APIs.

## Compilation & Execution

You will need two separate terminal windows to run the server and the client.

1.  **Compile the Project:**
    First, compile the source files for the server and the client. The `-lpthread` flag is necessary for linking the threading library.

    ```bash
    # Compile the server
    g++ -std=c++11 -o server server.cpp TCPO_Socket.cpp Connection.cpp Packet.cpp -lpthread

    # Compile the client
    g++ -std=c++11 -o client client.cpp TCPO_Socket.cpp Connection.cpp Packet.cpp -lpthread
    ```

2.  **Start the Server:**
    In the first terminal, run the compiled server executable.
    ```bash
    ./server
    ```

3.  **Start the Client:**
    In the second terminal, run the compiled client executable.
    ```bash
    ./client
    ```
You will see log messages in both terminals detailing the handshake, data transfer, and connection termination processes.