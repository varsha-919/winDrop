/**
 * WinDrop Sender - TCP File Sender with Resume Capability
 *
 * Sends files to peers via TCP connection
 * Uses platform abstraction layer for cross-platform compatibility
 *
 * Enhanced with:
 * - Chunk-based transfer with sequence numbers
 * - Resume capability
 * - ACK-based reliability
 */

#include <iostream>
#include <fstream>
#include <cstring>
#include <sstream>
#include <chrono>
#include <thread>
#include <map>
#include "platform.h"
#include "transfer_meta.h"

using namespace std;

// Configuration constants
namespace config
{
    constexpr unsigned short TCP_TARGET_PORT = 8080;
    constexpr int FILE_BUFFER_SIZE = 4096;
    constexpr int CHUNK_SIZE = 4096;
    constexpr int ACK_TIMEOUT_MS = 3000;
    constexpr int MAX_RETRIES = 5;
    constexpr int SOCKET_TIMEOUT_MS = 10000;
}

/**
 * Send a message and wait for response with timeout
 */
bool sendAndWait(WindropSocket sock, const string &message, string &response, int timeoutMs)
{
    // Set socket timeout
#ifdef WINDROP_PLATFORM_WINDOWS
    DWORD tv = timeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    // Send message - but skip if empty (just wait for response in that case)
    if (!message.empty())
    {
        int sent = send(sock, message.c_str(), message.length(), 0);
        if (sent < 0)
        {
            return false;
        }
    }

    // Wait for response
    char buffer[config::FILE_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);

    if (bytesRead <= 0)
    {
        return false;
    }

    buffer[bytesRead] = '\0';
    response = string(buffer, bytesRead);

    // Remove newline
    size_t newlinePos = response.find('\n');
    if (newlinePos != string::npos)
    {
        response = response.substr(0, newlinePos);
    }

    return true;
}

/**
 * Set socket to non-blocking with timeout
 */
void setSocketTimeout(WindropSocket sock, int timeoutMs)
{
#ifdef WINDROP_PLATFORM_WINDOWS
    DWORD tv = timeoutMs;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

/**
 * Send a single chunk and wait for ACK
 */
bool sendChunkWithAck(WindropSocket sock, int chunkIndex, const char *data, int size, int timeoutMs)
{
    // Build chunk message
    string chunkMsg = TransferProtocol::buildChunk(chunkIndex, data, size);
    cout << ">>> Sending chunk " << chunkIndex << " (" << size << " bytes)" << endl;

    // Send chunk
    int sent = send(sock, chunkMsg.c_str(), chunkMsg.length(), 0);
    if (sent < 0)
    {
        cerr << "Failed to send chunk " << chunkIndex << endl;
        return false;
    }

    // Wait for ACK
    char ackBuffer[256];
    setSocketTimeout(sock, timeoutMs);

    int bytesRead = recv(sock, ackBuffer, sizeof(ackBuffer) - 1, 0);
    if (bytesRead <= 0)
    {
        // Timeout
        cerr << "Timeout waiting for ACK on chunk " << chunkIndex << endl;
        return false;
    }

    ackBuffer[bytesRead] = '\0';
    string ackResponse(ackBuffer, bytesRead);

    // Parse ACK
    string msgType, payload;
    TransferProtocol::parseMessage(ackResponse, msgType, payload);

    if (msgType == transfer::MSG_ACK)
    {
        int ackIndex;
        if (TransferProtocol::parseAck(payload, ackIndex))
        {
            return (ackIndex == chunkIndex);
        }
    }

    return false;
}

/**
 * Main file sending function with resume capability
 */
int sendFile(const string &targetIp, const string &filePath, int64_t fileSize = -1)
{
    // Create socket
    WindropSocket sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == WINDROP_INVALID_SOCKET)
    {
        cerr << "Socket creation failed" << endl;
        return 1;
    }

    // Setup server address
    struct sockaddr_in servAddr;
    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(config::TCP_TARGET_PORT);

    // Convert IP string to binary
    if (!windrop::Convert::stringToAddr(targetIp, servAddr))
    {
        cerr << "Invalid address / Address not supported: " << targetIp << endl;
        windrop::SocketUtils::closeSocket(sock);
        return 1;
    }

    cout << "🔄 Connecting to " << targetIp << "..." << endl;

    // Connect to receiver
    if (connect(sock, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0)
    {
        cerr << "❌ Connection failed. Is the receiver running?" << endl;
        windrop::SocketUtils::closeSocket(sock);
        return 1;
    }

    cout << "✅ Connected to " << targetIp << endl;

    // Extract filename from path
    string filename = filePath;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != string::npos)
    {
        filename = filename.substr(lastSlash + 1);
    }

    // Get file size if not provided
    if (fileSize < 0)
    {
        fileSize = windrop::FileUtils::getFileSize(filePath);
    }

    if (fileSize < 0)
    {
        cerr << "Could not determine file size: " << filePath << endl;
        windrop::SocketUtils::closeSocket(sock);
        return 1;
    }

    // Calculate chunks
    int chunkSize = config::CHUNK_SIZE;
    int totalChunks = static_cast<int>((fileSize + chunkSize - 1) / chunkSize);

    cout << "📤 Sending: " << filename
         << " (" << fileSize << " bytes, "
         << totalChunks << " chunks)" << endl;

    // Send RESUME_QUERY to check for existing transfer
    cout << "🔍 Checking for existing transfer..." << endl;
    string resumeQuery = TransferProtocol::buildResumeQuery(filename, fileSize);
    string resumeResponse;

    if (sendAndWait(sock, resumeQuery, resumeResponse, config::SOCKET_TIMEOUT_MS))
    {
        bool canResume;
        int lastChunk;
        TransferProtocol::parseResumeResponse(resumeResponse, canResume, lastChunk);

        if (canResume && lastChunk >= 0)
        {
            cout << "🔄 Resuming from chunk " << (lastChunk + 1) << endl;
            // We'll continue from this point
        }
        else
        {
            cout << "🆕 Starting fresh transfer" << endl;
        }
    }
    else
    {
        cout << "🆕 Starting fresh transfer (no response)" << endl;
    }

    // Send HEADER message
    string header = TransferProtocol::buildHeader(filename, fileSize, chunkSize, totalChunks);
    send(sock, header.c_str(), header.length(), 0);

    // Wait for resume response using a simple recv
    char responseBuf[512];
    setSocketTimeout(sock, config::SOCKET_TIMEOUT_MS);
    int respLen = recv(sock, responseBuf, sizeof(responseBuf) - 1, 0);
    if (respLen <= 0)
    {
        cerr << "Failed to get resume response (timeout or error)" << endl;
        windrop::SocketUtils::closeSocket(sock);
        return 1;
    }
    responseBuf[respLen] = '\0';

    string headerResponse(responseBuf, respLen);
    // Remove trailing newline
    if (!headerResponse.empty() && headerResponse.back() == '\n')
        headerResponse.pop_back();

    // Extract payload from message (everything after the colon)
    size_t colonPos = headerResponse.find(':');
    string headerPayload = (colonPos != string::npos) ? headerResponse.substr(colonPos + 1) : headerResponse;

    bool canResume = false;
    int startChunk = 0;
    TransferProtocol::parseResumeResponse(headerPayload, canResume, startChunk);

    if (canResume)
    {
        startChunk = startChunk + 1; // Continue from next chunk
        cout << "🔄 Receiver accepted resume from chunk " << startChunk << endl;
    }
    else
    {
        startChunk = 0; // Fresh transfer starts from chunk 0
    }

    // Open file for reading
    ifstream infile(filePath, ios::binary);
    if (!infile.is_open())
    {
        cerr << "Could not open file: " << filePath << endl;
        windrop::SocketUtils::closeSocket(sock);
        return 1;
    }

    // Seek to start position (only if resuming with more chunks to send)
    if (startChunk > 0)
    {
        infile.seekg(startChunk * chunkSize, ios::beg);

        // Try to read a small amount to check if there's data left
        char testBuffer[1];
        infile.read(testBuffer, 1);
        if (infile.gcount() == 0 || infile.eof() || !infile.good())
        {
            cout << "✅ File already complete (nothing to resume)" << endl;
            infile.close();
            windrop::SocketUtils::closeSocket(sock);
            return 0;
        }
        // Put back the character we read (seek back by 1)
        infile.seekg(-1, ios::cur);
    }

    // Send chunks
    char buffer[config::FILE_BUFFER_SIZE];
    int currentChunk = startChunk;
    int retries = 0;
    int sentChunks = 0;

    while (infile.read(buffer, sizeof(buffer)) || infile.gcount() > 0)
    {
        streamsize bytesRead = infile.gcount();

        // Send chunk with retries
        bool success = false;
        retries = 0;

        while (!success && retries < config::MAX_RETRIES)
        {
            if (sendChunkWithAck(sock, currentChunk, buffer, static_cast<int>(bytesRead), config::ACK_TIMEOUT_MS))
            {
                success = true;
                sentChunks++;
            }
            else
            {
                retries++;
                if (retries < config::MAX_RETRIES)
                {
                    cout << "⏳ Chunk " << currentChunk << " timeout, retry " << retries << "/" << config::MAX_RETRIES << endl;
                }
            }
        }

        if (!success)
        {
            cerr << "❌ Failed to send chunk " << currentChunk << " after " << config::MAX_RETRIES << " retries" << endl;
            infile.close();
            windrop::SocketUtils::closeSocket(sock);
            return 1;
        }

        currentChunk++;

        // Progress update every 50 chunks
        if (currentChunk % 50 == 0)
        {
            int progress = static_cast<int>((currentChunk * 100.0) / totalChunks);
            cout << "📊 Progress: " << progress << "% (" << currentChunk << "/" << totalChunks << " chunks)" << endl;
        }
    }

    infile.close();

    // Send COMPLETE message with checksum
    cout << "🔐 Computing checksum..." << endl;
    uint32_t checksum = windrop::FileUtils::computeChecksum(filePath);

    string completeMsg = TransferProtocol::buildComplete(checksum);
    if (send(sock, completeMsg.c_str(), completeMsg.length(), 0) < 0)
    {
        cerr << "Failed to send complete message" << endl;
        windrop::SocketUtils::closeSocket(sock);
        return 1;
    }

    cout << "✅ SUCCESS: " << filename << " sent! (" << sentChunks << " chunks, checksum: " << checksum << ")" << endl;

    windrop::SocketUtils::closeSocket(sock);
    return 0;
}

/**
 * Main entry point
 * Usage: ./sender <target_ip> <file_path> [file_size]
 */
int main(int argc, char *argv[])
{
    // Initialize platform-specific networking
    if (!windrop::Platform::initialize())
    {
        cerr << "Failed to initialize platform networking" << endl;
        return 1;
    }

    // Validate arguments
    if (argc < 3)
    {
        cerr << "Usage: ./sender <target_ip> <file_path> [file_size]" << endl;
        cerr << "  file_size is optional but recommended for resume support" << endl;
        windrop::Platform::cleanup();
        return 1;
    }

    string targetIp = argv[1];
    string filePath = argv[2];
    int64_t fileSize = -1;

    // Parse optional file size
    if (argc >= 4)
    {
        try
        {
            fileSize = stoll(argv[3]);
        }
        catch (...)
        {
            cerr << "Invalid file size: " << argv[3] << endl;
            windrop::Platform::cleanup();
            return 1;
        }
    }

    // Send the file
    int result = sendFile(targetIp, filePath, fileSize);

    // Cleanup
    windrop::Platform::cleanup();

    return result;
}