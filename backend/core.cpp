/**
 * WinDrop Core - UDP Broadcaster, UDP Listener, and TCP File Receiver
 *
 * Uses platform abstraction layer for cross-platform compatibility:
 * - Windows (WinSock2)
 * - Linux (POSIX sockets)
 * - macOS (POSIX sockets)
 *
 * Enhanced with resumable file transfer:
 * - Temporary file (.part) during transfer
 * - Metadata persistence for resume capability
 * - Chunk-based transfer with ACKs
 */

#include <iostream>
#include <fstream>
#include <thread>
#include <cstring>

#include <sstream>
#include <algorithm>
#include "platform.h"
#include "transfer_meta.h"

using namespace std;

// Configuration constants
namespace config
{
    constexpr unsigned short UDP_BROADCAST_PORT = 8888;
    constexpr unsigned short TCP_LISTEN_PORT = 8080;
    constexpr unsigned int BROADCAST_INTERVAL_MS = 2000;
    constexpr int SOCKET_BUFFER_SIZE = 4096;
    constexpr int FILE_BUFFER_SIZE = 4096;
    constexpr int CHUNK_SIZE = 4096;
    constexpr int ACK_TIMEOUT_MS = 5000;
    constexpr int MAX_RETRIES = 3;
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
                cout << "Discovered peer: " << buffer << endl;
            }
        }
    }

    windrop::SocketUtils::closeSocket(sock);
}

/**
 * TCP Server - receives files from other peers with resume capability
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
        // serverFd continuously listens and newSocket is for communication with this client.

        if (newSocket == WINDROP_INVALID_SOCKET)
        {
            continue;
        }

        string clientIP = inet_ntoa(clientAddr.sin_addr);
        cout << "🔗 Client connected: " << clientIP << endl;

        // Receive messages in a loop
        string buffer;
        bool transferComplete = false;
        TransferMetadata meta;
        ofstream outfile;
        int nextExpectedChunk = 0;

        while (!transferComplete)
        {
            // Read until we have a complete message (newline delimited)
            char recvBuffer[config::SOCKET_BUFFER_SIZE];
            int bytesRead = recv(newSocket, recvBuffer, sizeof(recvBuffer) - 1, 0);

            if (bytesRead <= 0)
            {
                cout << "⚠️ Connection lost (error: " << (bytesRead == 0 ? "closed" : strerror(errno))
                     << ", last chunk: " << nextExpectedChunk << ")" << endl;
                // Save state before closing
                if (outfile.is_open())
                {
                    outfile.close();
                    meta.lastAckedChunk = nextExpectedChunk - 1;
                    meta.updatedAt = TransferMetadata::getTimestamp();
                    string metaPath = windrop::FileUtils::getMetaPath(meta.filename);
                    meta.save(metaPath);
                    cout << "💾 Transfer state saved (last chunk: " << meta.lastAckedChunk << ")" << endl;
                }
                break;
            }

            recvBuffer[bytesRead] = '\0';
            buffer.append(recvBuffer, bytesRead);

            // Process all complete messages in buffer
            size_t newlinePos;
            while (true)
            {
                // Find first newline to peek at message header
                newlinePos = buffer.find('\n');
                if (newlinePos == string::npos)
                    break;

                // Peek at the message to check if it's a CHUNK
                string peekMsg = buffer.substr(0, newlinePos);
                string peekType, peekPayload;
                TransferProtocol::parseMessage(peekMsg, peekType, peekPayload);

                // For CHUNK messages, we need to wait for the full data (not just to first \n)
                string message;
                if (peekType == transfer::MSG_CHUNK)
                {
                    // Parse the header to get expected data size
                    size_t pipePos = peekPayload.find(transfer::FIELD_DELIMITER);
                    if (pipePos != string::npos)
                    {
                        try
                        {
                            // Payload format: index|size
                            int chunkDataSize = stoi(peekPayload.substr(pipePos + 1));
                            // Total message = message up to \n + \n + data
                            // peekMsg includes everything up to first \n
                            // We need peekMsg.length() + 1 (for \n) + chunkDataSize
                            size_t totalNeeded = peekMsg.length() + 1 + chunkDataSize;

                            if (buffer.size() < totalNeeded)
                            {
                                // Not enough data yet, wait for more
                                break;
                            }

                            // Extract full CHUNK message including data
                            message = buffer.substr(0, totalNeeded);
                            buffer.erase(0, totalNeeded);
                        }
                        catch (...)
                        {
                            // Fall back to normal processing
                            message = buffer.substr(0, newlinePos);
                            buffer.erase(0, newlinePos + 1);
                        }
                    }
                    else
                    {
                        // Can't parse, use normal processing
                        message = buffer.substr(0, newlinePos);
                        buffer.erase(0, newlinePos + 1);
                    }
                }
                else
                {
                    // Normal message - extract up to newline
                    message = buffer.substr(0, newlinePos);
                    buffer.erase(0, newlinePos + 1);
                }

                // Parse message type and payload
                string msgType, payload;
                TransferProtocol::parseMessage(message, msgType, payload);

                // Handle each message type
                if (msgType == transfer::MSG_HEADER)
                {
                    cout << "[RECEIVER] Step 1: HEADER received" << endl;
                    // New transfer request
                    string filename;
                    int64_t fileSize;
                    int chunkSize, totalChunks;

                    if (TransferProtocol::parseHeader(payload, filename, fileSize, chunkSize, totalChunks))
                    {
                        cout << "📥 Transfer started: " << filename
                             << " (" << fileSize << " bytes, "
                             << totalChunks << " chunks)" << endl;

                        // Check for existing temp file (resume attempt)
                        string tempPath = windrop::FileUtils::getTempPath(filename);
                        string metaPath = windrop::FileUtils::getMetaPath(filename);

                        if (windrop::FileUtils::tempExists(filename) &&
                            windrop::FileUtils::metaExists(filename) &&
                            meta.load(metaPath))
                        {
                            // Validate resume
                            if (meta.filename == filename &&
                                meta.fileSize == fileSize &&
                                meta.lastAckedChunk >= 0)
                            {
                                cout << "🔄 Existing transfer found, last chunk: "
                                     << meta.lastAckedChunk << endl;

                                // Send OK response with last chunk
                                string response = TransferProtocol::buildResumeResponse(true, meta.lastAckedChunk);
                                cout << "[RECEIVER] Step 2: Sending HEADER response (resume OK)" << endl;
                                send(newSocket, response.c_str(), response.length(), 0);

                                // Open temp file in append mode
                                outfile.open(tempPath, ios::binary | ios::app);
                                nextExpectedChunk = meta.lastAckedChunk + 1;

                                cout << "🔄 Resuming from chunk " << nextExpectedChunk << endl;
                            }
                            else
                            {
                                // Can't resume - mismatch
                                cout << "🔄 Resume mismatch, starting fresh" << endl;
                                windrop::FileUtils::cleanupTemp(filename);

                                outfile.open(tempPath, ios::binary);
                                nextExpectedChunk = 0;

                                // Send NO response
                                string response = TransferProtocol::buildResumeResponse(false);
                                cout << "[RECEIVER] Step 2: Sending HEADER response (fresh)" << endl;
                                send(newSocket, response.c_str(), response.length(), 0);
                            }
                        }
                        else
                        {
                            // Fresh transfer
                            outfile.open(tempPath, ios::binary);
                            nextExpectedChunk = 0;

                            // Initialize metadata
                            meta.filename = filename;
                            meta.fileSize = fileSize;
                            meta.chunkSize = chunkSize;
                            meta.totalChunks = totalChunks;
                            meta.lastAckedChunk = -1;
                            meta.createdAt = TransferMetadata::getTimestamp();
                            meta.updatedAt = meta.createdAt;
                            meta.senderIP = clientIP;
                            meta.checksum = 0;

                            // Save initial metadata
                            meta.save(metaPath);

                            // Send NO response (fresh transfer)
                            string response = TransferProtocol::buildResumeResponse(false);
                            cout << "[RECEIVER] Step 2: Sending HEADER response (fresh)" << endl;
                            send(newSocket, response.c_str(), response.length(), 0);

                            cout << "📤 Waiting for chunks..." << endl;
                        }
                    }
                }
                else if (msgType == transfer::MSG_RESUME_QUERY)
                {
                    cout << "[RECEIVER] RESUME_QUERY received" << endl;
                    // Resume query from sender
                    string filename;
                    int64_t fileSize;

                    if (TransferProtocol::parseResumeQuery(payload, filename, fileSize))
                    {
                        string metaPath = windrop::FileUtils::getMetaPath(filename);

                        if (windrop::FileUtils::tempExists(filename) &&
                            meta.load(metaPath) &&
                            meta.filename == filename &&
                            meta.fileSize == fileSize &&
                            meta.lastAckedChunk >= 0)
                        {
                            // Can resume
                            string response = TransferProtocol::buildResumeResponse(true, meta.lastAckedChunk);
                            cout << "[RECEIVER] Sending RESUME_RESPONSE (OK)" << endl;
                            send(newSocket, response.c_str(), response.length(), 0);

                            cout << "📋 Resume query: OK, last chunk " << meta.lastAckedChunk << endl;
                        }
                        else
                        {
                            // Can't resume
                            string response = TransferProtocol::buildResumeResponse(false);
                            cout << "[RECEIVER] Sending RESUME_RESPONSE (NO)" << endl;
                            send(newSocket, response.c_str(), response.length(), 0);

                            cout << "📋 Resume query: NO (no existing transfer)" << endl;
                        }
                    }
                }
                else if (msgType == transfer::MSG_CHUNK)
                {
                    cout << "[RECEIVER] Step 3: CHUNK received" << endl;
                    // Chunk data received - format: index|size\ndata
                    // (with my fix, payload now includes the data after \n)
                    size_t pipePos = payload.find(transfer::FIELD_DELIMITER);
                    size_t newlinePos = payload.find('\n');
                    if (pipePos != string::npos && newlinePos != string::npos)
                    {
                        int chunkIndex;
                        int chunkDataSize;
                        try
                        {
                            chunkIndex = stoi(payload.substr(0, pipePos));
                            chunkDataSize = stoi(payload.substr(pipePos + 1, newlinePos - pipePos - 1));
                        }
                        catch (...)
                        {
                            cout << "!!! Failed to parse chunk header" << endl;
                            continue;
                        }

                        // Extract data after the newline - use all remaining data in payload
                        // This handles the case where the declared size includes trailing newlines
                        // that aren't part of the actual data we extract
                        string chunkData = payload.substr(newlinePos + 1);
                        int actualSize = static_cast<int>(chunkData.size());

                        // Validate chunk
                        if (chunkIndex < nextExpectedChunk)
                        {
                            // Duplicate or late chunk - send ACK anyway
                            cout << "🔄 Duplicate chunk " << chunkIndex << " (expected " << nextExpectedChunk << ")" << endl;
                        }
                        else if (chunkIndex > nextExpectedChunk)
                        {
                            // Out of order - gap, request retransmission
                            cout << "⚠️ Out of order chunk " << chunkIndex << " (expected " << nextExpectedChunk << ")" << endl;
                            continue;
                        }
                        else
                        {
                            // Correct chunk - write to file
                            outfile.write(chunkData.c_str(), actualSize);
                            nextExpectedChunk++;

                            cout << "📦 Chunk " << chunkIndex << " received (" << actualSize << " bytes)" << endl;
                        }

                        // Send ACK
                        string ack = TransferProtocol::buildAck(chunkIndex);
                        cout << "[RECEIVER] Step 4: Sending ACK for chunk " << chunkIndex << endl;
                        send(newSocket, ack.c_str(), ack.length(), 0);

                        // Update metadata periodically (every 10 chunks)
                        if (nextExpectedChunk % 10 == 0)
                        {
                            meta.lastAckedChunk = nextExpectedChunk - 1;
                            meta.updatedAt = TransferMetadata::getTimestamp();
                            string metaPath = windrop::FileUtils::getMetaPath(meta.filename);
                            meta.save(metaPath);
                            cout << "💾 Progress saved (chunk " << meta.lastAckedChunk << ")" << endl;
                        }
                    }
                }
                else if (msgType == transfer::MSG_COMPLETE)
                {
                    cout << "[RECEIVER] Step 5: COMPLETE received" << endl;
                    // Transfer complete
                    uint32_t receivedChecksum = 0;
                    try
                    {
                        receivedChecksum = static_cast<uint32_t>(stoul(payload));
                    }
                    catch (...)
                    {
                        receivedChecksum = 0;
                    }

                    outfile.close();

                    // Compute actual checksum
                    string tempPath = windrop::FileUtils::getTempPath(meta.filename);
                    uint32_t actualChecksum = windrop::FileUtils::computeChecksum(tempPath);

                    if (receivedChecksum == actualChecksum)
                    {
                        // Rename temp to final
                        string finalPath = meta.filename;
                        if (windrop::FileUtils::atomicRename(tempPath, finalPath))
                        {
                            // Clean up metadata
                            windrop::FileUtils::cleanupTemp(meta.filename);
                            cout << "✅ Transfer completed and verified (" << actualChecksum << ")" << endl;
                            cout << "📁 File saved as: " << finalPath << endl;
                        }
                        else
                        {
                            cout << "❌ Failed to rename temp file" << endl;
                        }
                    }
                    else
                    {
                        cout << "❌ Checksum mismatch! Expected: " << receivedChecksum
                             << ", Got: " << actualChecksum << endl;
                        // Keep temp file for debugging
                    }

                    transferComplete = true;
                }
                else if (msgType == transfer::MSG_RESET)
                {
                    // Sender requested reset
                    cout << "🔄 Reset requested: " << payload << endl;
                    windrop::FileUtils::cleanupTemp(meta.filename);
                    transferComplete = true;
                }
            }
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