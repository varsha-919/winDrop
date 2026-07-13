/**
 * Platform Abstraction Layer Implementation
 *
 * Cross-platform networking utilities for WinDrop
 * Supports: Windows, Linux, macOS
 */

#include "platform.h"

// Additional POSIX headers not in platform.h (ifaddrs for getLocalIP)
#ifndef WINDROP_PLATFORM_WINDOWS
#include <ifaddrs.h>
#include <net/if.h>
#include <chrono>
#include <thread>
#endif

// Standard C++ headers
#include <iostream>
#include <sstream>
#include <iomanip>

// Platform initialization state
namespace windrop
{
    namespace internal
    {
        static bool g_platformInitialized = false;
    }
} // namespace windrop

bool windrop::Platform::initialize()
{
#ifdef WINDROP_PLATFORM_WINDOWS
    if (internal::g_platformInitialized)
    {
        return true;
    }

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        std::cerr << "WSAStartup failed with error: " << result << std::endl;
        return false;
    }

    // Verify version 2.2
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
    {
        std::cerr << "Winsock version 2.2 not available" << std::endl;
        WSACleanup();
        return false;
    }

    internal::g_platformInitialized = true;
    return true;
#else
    // POSIX platforms don't need initialization
    internal::g_platformInitialized = true;
    return true;
#endif
}

void windrop::Platform::cleanup()
{
#ifdef WINDROP_PLATFORM_WINDOWS
    if (internal::g_platformInitialized)
    {
        WSACleanup();
        internal::g_platformInitialized = false;
    }
#else
    internal::g_platformInitialized = false;
#endif
}

bool windrop::Platform::isInitialized()
{
    return internal::g_platformInitialized;
}

void windrop::SocketUtils::closeSocket(WindropSocket sock)
{
#ifdef WINDROP_PLATFORM_WINDOWS
    if (sock != WINDROP_INVALID_SOCKET)
    {
        closesocket(sock);
    }
#else
    if (sock != WINDROP_INVALID_SOCKET)
    {
        close(sock);
    }
#endif
}

bool windrop::SocketUtils::setReuseAddr(WindropSocket sock)
{
    int reuse = 1;
#ifdef WINDROP_PLATFORM_WINDOWS
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == 0;
#else
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == 0;
#endif
}

bool windrop::SocketUtils::setBroadcast(WindropSocket sock)
{
    int broadcast = 1;
#ifdef WINDROP_PLATFORM_WINDOWS
    return setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast)) == 0;
#else
    return setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) == 0;
#endif
}

void windrop::SleepUtils::sleep(unsigned int ms)
{
#ifdef WINDROP_PLATFORM_WINDOWS
    ::Sleep(ms);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
}

std::string windrop::NetworkUtils::getHostname()
{
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0)
    {
        return "Unknown_Peer";
    }
    return std::string(hostname);
}

std::string windrop::NetworkUtils::getLocalIP()
{
#ifdef WINDROP_PLATFORM_WINDOWS
    // Windows implementation using gethostbyname
    std::string best_ip = "127.0.0.1";

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0)
    {
        struct hostent *he = gethostbyname(hostname);
        if (he && he->h_addr_list[0])
        {
            struct in_addr **addr_list = (struct in_addr **)he->h_addr_list;
            for (int i = 0; addr_list[i] != nullptr; i++)
            {
                std::string ip = inet_ntoa(*addr_list[i]);
                // Skip localhost
                if (ip != "127.0.0.1")
                {
                    return ip;
                }
            }
        }
    }
    return best_ip;
#else
    // POSIX implementation using getifaddrs
    struct ifaddrs *ifaddr, *ifa;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        return "127.0.0.1";
    }

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr)
        {
            continue;
        }

        if (ifa->ifa_addr->sa_family == AF_INET)
        { // IPv4
            int result = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                                     host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);

            if (result == 0)
            {
                std::string ip = host;

                // Skip localhost
                if (ip != "127.0.0.1")
                {
                    freeifaddrs(ifaddr);
                    return ip;
                }
            }
        }
    }

    freeifaddrs(ifaddr);
    return "127.0.0.1";
#endif
}

bool windrop::Convert::stringToAddr(const std::string &ipStr, struct sockaddr_in &addr)
{
#ifdef WINDROP_PLATFORM_WINDOWS
    addr.sin_addr.s_addr = inet_addr(ipStr.c_str());
    return addr.sin_addr.s_addr != INADDR_NONE;
#else
    return inet_pton(AF_INET, ipStr.c_str(), &addr.sin_addr) > 0;
#endif
}