/**
 * WinDrop Sender - TCP File Sender
 *
 * Sends files to peers via TCP connection
 * Uses platform abstraction layer for cross-platform compatibility
 */

#include <iostream>
#include <fstream>
#include <cstring>
#include "platform.h"

using namespace std;

// Configuration constants
namespace config
{
    constexpr unsigned short TCP_TARGET_PORT = 8080;
    constexpr int FILE_BUFFER_SIZE = 4096;
}

/**
 * Send a file to a target IP address
 * @param targetIp Target peer IP address
 * @param filePath Path to file to send
 * @return 0 on success, 1 on failure
 */
int sendFile(const string &targetIp, const string &filePath)
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

    // Send filename header (with newline delimiter)
    string header = filename + "\n";
    if (send(sock, header.c_str(), header.length(), 0) < 0)
    {
        cerr << "Failed to send filename header" << endl;
        windrop::SocketUtils::closeSocket(sock);
        return 1;
    }

    // Open file for reading
    ifstream infile(filePath, ios::binary);
    if (!infile.is_open())
    {
        cerr << "Could not open file: " << filePath << endl;
        windrop::SocketUtils::closeSocket(sock);
        return 1;
    }

    cout << "📤 Sending: " << filename << endl;

    // Send file data in chunks
    char buffer[config::FILE_BUFFER_SIZE];
    while (infile.read(buffer, sizeof(buffer)) || infile.gcount() > 0)
    {
        streamsize bytesRead = infile.gcount();
        if (send(sock, buffer, bytesRead, 0) < 0)
        {
            cerr << "Error sending file data" << endl;
            infile.close();
            windrop::SocketUtils::closeSocket(sock);
            return 1;
        }
    }

    infile.close();
    windrop::SocketUtils::closeSocket(sock);

    cout << "✅ SUCCESS: " << filename << " sent!" << endl;
    return 0;
}

/**
 * Main entry point
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
        cerr << "Usage: ./sender <target_ip> <file_path>" << endl;
        windrop::Platform::cleanup();
        return 1;
    }

    string targetIp = argv[1];
    string filePath = argv[2];

    // Send the file
    int result = sendFile(targetIp, filePath);

    // Cleanup
    windrop::Platform::cleanup();

    return result;
}