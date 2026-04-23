#include <iostream>
#include <fstream>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <ifaddrs.h>
#include <netdb.h>

using namespace std;

string getLocalIP()
{
    char buffer[256];
    string best_ip = "127.0.0.1";

    FILE *pipe = popen("ipconfig.exe", "r");
    if (!pipe)
    {
        return best_ip;
    }

    while (fgets(buffer, sizeof(buffer), pipe) != NULL)
    {
        string line = buffer;

        if (line.find("IPv4") != string::npos)
        {
            size_t colon_pos = line.find(":");
            if (colon_pos != string::npos)
            {
                string ip = line.substr(colon_pos + 1);

                ip.erase(ip.find_last_not_of(" \n\r\t") + 1);
                ip.erase(0, ip.find_first_not_of(" \n\r\t"));

                if (ip.find("172.") != 0 && ip.find("169.254.") != 0 && ip != "127.0.0.1")
                {
                    best_ip = ip;

                    if (best_ip.find("10.") == 0 || best_ip.find("192.168.") == 0)
                    {
                        break;
                    }
                }
            }
        }
    }
    pclose(pipe);
    return best_ip;
}

// on mac
// string getLocalIP()
// {
//     struct ifaddrs *ifaddr, *ifa;
//     char host[NI_MAXHOST];

//     if (getifaddrs(&ifaddr) == -1)
//     {
//         perror("getifaddrs");
//         return "127.0.0.1";
//     }

//     for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
//     {
//         if (ifa->ifa_addr == NULL)
//             continue;

//         if (ifa->ifa_addr->sa_family == AF_INET)
//         {
//             getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
//                         host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

//             string ip = host;

//             // Skip localhost
//             if (ip != "127.0.0.1")
//             {
//                 freeifaddrs(ifaddr);
//                 return ip;
//             }
//         }
//     }

//     freeifaddrs(ifaddr);
//     return "127.0.0.1";
// }

// udp broadcaster
void run_udp_broadcaster()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast_enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    struct sockaddr_in broadcast_addr;
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(8888);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    string ip = getLocalIP();

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown_Peer");
    }
    string name(hostname);

    string message = name + ":" + ip + " Alive";

    while (true)
    {
        sendto(sock, message.c_str(), message.length(), 0, (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        sleep(2);
    }
    close(sock);
}

// udp listener
void run_udp_listener()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(8888);
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr));

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        strcpy(hostname, "Unknown_Peer");
    }
    string my_name(hostname);

    char buffer[1024];
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        recvfrom(sock, buffer, sizeof(buffer), 0, nullptr, nullptr);

        string received_msg(buffer);

        if (received_msg.find(my_name) == string::npos)
        {
            cout << "Founded Peer: " << buffer << "\n";
        }
    }
    close(sock);
}
// file receiver
void run_tcp_server()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(8080);
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);

    while (true)
    {
        int addrlen = sizeof(address);
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);

        char buffer[1024];
        memset(buffer, 0, 1024);

        int bytes_read = recv(new_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0)
        {
            close(new_socket);
            continue;
        }

        string raw_data(buffer, bytes_read);
        size_t newline_pos = raw_data.find('\n');

        if (newline_pos != string::npos)
        {
            string filename = raw_data.substr(0, newline_pos);
            cout << "📥 Receiving file: " << filename << endl;

            ofstream outfile(filename, ios::binary);

            if (bytes_read > newline_pos + 1)
            {
                outfile.write(buffer + newline_pos + 1, bytes_read - (newline_pos + 1));
            }

            while ((bytes_read = recv(new_socket, buffer, sizeof(buffer), 0)) > 0)
            {
                outfile.write(buffer, bytes_read);
            }
            outfile.close();
            cout << "✅ File Saved!" << endl;
        }
        close(new_socket);
    }
}
// main

int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);

    cout << "LIGHTHOUSE CORE ENGINE STARTED\n";

    thread mouth(run_udp_broadcaster);
    thread ear(run_udp_listener);
    thread hands(run_tcp_server);

    mouth.join();
    ear.join();
    hands.join();

    return 0;
}