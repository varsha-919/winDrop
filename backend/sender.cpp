#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "❌ Usage: ./sender <target_ip> <file_path>" << std::endl;
        return 1;
    }

    std::string target_ip = argv[1];
    std::string file_path = argv[2];

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        std::cerr << "❌ Socket creation error" << std::endl;
        return 1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(8080);

    if (inet_pton(AF_INET, target_ip.c_str(), &serv_addr.sin_addr) <= 0)
    {
        std::cerr << "❌ Invalid address/ Address not supported" << std::endl;
        return 1;
    }

    std::cout << "🔄 Attempting connection to " << target_ip << "..." << std::endl;
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        std::cerr << "❌ Connection Failed. Is the other C++ engine running?" << std::endl;
        return 1;
    }

    std::string filename = file_path.substr(file_path.find_last_of("/\\") + 1);
    std::string header = filename + "\n";
    send(sock, header.c_str(), header.length(), 0);

    std::ifstream infile(file_path, std::ios::binary);
    if (!infile.is_open())
    {
        std::cerr << "❌ Could not open file: " << file_path << std::endl;
        return 1;
    }

    char buffer[4096]; // Larger buffer for speed
    while (infile.read(buffer, sizeof(buffer)) || infile.gcount() > 0)
    {
        send(sock, buffer, infile.gcount(), 0);
    }

    std::cout << "🚀 SUCCESS: " << filename << " sent!" << std::endl;
    infile.close();
    close(sock);
    return 0;
}