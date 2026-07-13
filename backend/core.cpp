/**
 * WinDrop Core - UDP Broadcaster, UDP Listener, and TCP File Receiver
 *
 * Uses platform abstraction layer for cross-platform compatibility:
 * - Windows (WinSock2)
 * - Linux (POSIX sockets)
 * - macOS (POSIX sockets)
 */

#include <iostream>
#include <fstream>
#include <thread>
#include <cstring>
#include "platform.h"

using namespace std;

// Configuration constants
namespace config
{
    constexpr unsigned short UDP_BROADCAST_PORT = 8888;
    constexpr unsigned short TCP_LISTEN_PORT = 8080;
    constexpr unsigned int BROADCAST_INTERVAL_MS = 2000;
    constexpr int SOCKET_BUFFER_SIZE = 4096;
    constexpr int FILE_BUFFER_SIZE = 1024;
}

/**
 * UDP Broadcaster - announces presence on the network
 */
void runUdpBroadcaster()
{
    WindropSocket sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == WINDROP_INVALID_SOCKET)
    {
        cerr << "UDP Broadcaster: Socket creation failed" << endl;
        return;
    }

    // Enable broadcast
    if (!windrop::SocketUtils::setBroadcast(sock))
    {
        cerr << "UDP Broadcaster: Failed to enable broadcast" << endl;
        windrop::SocketUtils::closeSocket(sock);
        return;
    }

    struct sockaddr_in broadcastAddr;
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(config::UDP_BROADCAST_PORT);
    broadcastAddr.sin_addr.s_addr = inet_addr("192.168.29.255");

    string ip = windrop::NetworkUtils::getLocalIP();
    string hostname = windrop::NetworkUtils::getHostname();
    string message = hostname + ":" + ip + " Alive";

    cout << "📡 Broadcasting on port " << config::UDP_BROADCAST_PORT
         << " (IP: " << ip << ")" << endl;

    while (true)
    {
        sendto(sock, message.c_str(), message.length(), 0,
               (struct sockaddr *)&broadcastAddr, sizeof(broadcastAddr));
        windrop::SleepUtils::sleep(config::BROADCAST_INTERVAL_MS);
    }

    windrop::SocketUtils::closeSocket(sock);
}

/**
 * UDP Listener - discovers other peers on the network
 */
void runUdpListener()
{
    WindropSocket sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == WINDROP_INVALID_SOCKET)
    {
        cerr << "UDP Listener: Socket creation failed" << endl;
        return;
    }

    // Enable address reuse
    if (!windrop::SocketUtils::setReuseAddr(sock))
    {
        cerr << "UDP Listener: Failed to set reuse address" << endl;
        windrop::SocketUtils::closeSocket(sock);
        return;
    }

    struct sockaddr_in listenAddr;
    memset(&listenAddr, 0, sizeof(listenAddr));
    listenAddr.sin_family = AF_INET;
    listenAddr.sin_port = htons(config::UDP_BROADCAST_PORT);
    listenAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(sock, (struct sockaddr *)&listenAddr, sizeof(listenAddr)) < 0)
    {
        cerr << "UDP Listener: Bind failed" << endl;
        windrop::SocketUtils::closeSocket(sock);
        return;
    }

    string myName = windrop::NetworkUtils::getHostname();
    char buffer[config::SOCKET_BUFFER_SIZE];

    cout << "👂 Listening for peers on port " << config::UDP_BROADCAST_PORT << endl;

    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        struct sockaddr_in senderAddr;
        socklen_t addrLen = sizeof(senderAddr);

        int bytesRead = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                                 (struct sockaddr *)&senderAddr, &addrLen);

        if (bytesRead > 0)
        {
            buffer[bytesRead] = '\0';
            string receivedMsg(buffer);

            // Skip own broadcasts
            if (receivedMsg.find(myName) == string::npos)
            {
                cout << "👤 Discovered peer: " << buffer << endl;
            }
        }
    }

    windrop::SocketUtils::closeSocket(sock);
}

/**
 * TCP Server - receives files from other peers
 */
void runTcpServer()
{
    WindropSocket serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == WINDROP_INVALID_SOCKET)
    {
        cerr << "TCP Server: Socket creation failed" << endl;
        return;
    }

    if (!windrop::SocketUtils::setReuseAddr(serverFd))
    {
        cerr << "TCP Server: Failed to set reuse address" << endl;
        windrop::SocketUtils::closeSocket(serverFd);
        return;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config::TCP_LISTEN_PORT);

    if (::bind(serverFd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        cerr << "TCP Server: Bind failed" << endl;
        windrop::SocketUtils::closeSocket(serverFd);
        return;
    }

    if (listen(serverFd, 5) < 0)
    {
        cerr << "TCP Server: Listen failed" << endl;
        windrop::SocketUtils::closeSocket(serverFd);
        return;
    }

    cout << "📥 TCP Server listening on port " << config::TCP_LISTEN_PORT << endl;

    while (true)
    {
        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        WindropSocket newSocket = accept(serverFd, (struct sockaddr *)&clientAddr, &addrLen);

        if (newSocket == WINDROP_INVALID_SOCKET)
        {
            continue;
        }

        char buffer[config::SOCKET_BUFFER_SIZE];
        memset(buffer, 0, sizeof(buffer));

        int bytesRead = recv(newSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead <= 0)
        {
            windrop::SocketUtils::closeSocket(newSocket);
            continue;
        }

        string rawData(buffer, bytesRead);
        size_t newlinePos = rawData.find('\n');

        if (newlinePos != string::npos)
        {
            string filename = rawData.substr(0, newlinePos);
            cout << "📥 Receiving file: " << filename << endl;

            ofstream outfile(filename, ios::binary);

            // Write any data that came with the header
            if (bytesRead > static_cast<int>(newlinePos + 1))
            {
                outfile.write(buffer + newlinePos + 1,
                              bytesRead - (newlinePos + 1));
            }

            // Receive file data
            while ((bytesRead = recv(newSocket, buffer, sizeof(buffer), 0)) > 0)
            {
                outfile.write(buffer, bytesRead);
            }

            outfile.close();
            cout << "✅ File saved: " << filename << endl;
        }

        windrop::SocketUtils::closeSocket(newSocket);
    }
}

/**
 * Main entry point
 */
int main()
{
    // Disable output buffering for real-time logging
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Initialize platform-specific networking
    if (!windrop::Platform::initialize())
    {
        cerr << "Failed to initialize platform networking" << endl;
        return 1;
    }

    cout << "⚡ WinDrop Core Engine Started" << endl;
    cout << "   Platform: "
#ifdef WINDROP_PLATFORM_WINDOWS
         << "Windows"
#elif defined(WINDROP_PLATFORM_MACOS)
         << "macOS"
#else
         << "Linux"
#endif
         << endl;

    // Start all networking components
    thread broadcaster(runUdpBroadcaster);
    thread listener(runUdpListener);
    thread receiver(runTcpServer);

    broadcaster.join();
    listener.join();
    receiver.join();

    // Cleanup
    windrop::Platform::cleanup();

    return 0;
}