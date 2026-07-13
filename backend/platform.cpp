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
#else
#include <filesystem>
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

// ============================================================================
// FileUtils Implementation
// ============================================================================

#include <sys/stat.h>
#include <fstream>

#ifdef WINDROP_PLATFORM_WINDOWS
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <sys/types.h>
#endif

std::string windrop::FileUtils::getTempPath(const std::string &filename)
{
    return filename + ".part";
}

std::string windrop::FileUtils::getMetaPath(const std::string &filename)
{
    return filename + ".part.meta";
}

bool windrop::FileUtils::tempExists(const std::string &filename)
{
    std::string tempPath = getTempPath(filename);
#ifdef WINDROP_PLATFORM_WINDOWS
    return fs::exists(tempPath);
#else
    struct stat buffer;
    return (stat(tempPath.c_str(), &buffer) == 0);
#endif
}

bool windrop::FileUtils::metaExists(const std::string &filename)
{
    std::string metaPath = getMetaPath(filename);
#ifdef WINDROP_PLATFORM_WINDOWS
    return fs::exists(metaPath);
#else
    struct stat buffer;
    return (stat(metaPath.c_str(), &buffer) == 0);
#endif
}

bool windrop::FileUtils::atomicRename(const std::string &tempPath, const std::string &finalPath)
{
#ifdef WINDROP_PLATFORM_WINDOWS
    // Windows: use std::filesystem::rename (which is atomic on Windows)
    try {
        fs::rename(tempPath, finalPath);
        return true;
    } catch (const fs::system_error& e) {
        std::cerr << "Rename failed: " << e.what() << std::endl;
        return false;
    }
#else
    // POSIX: rename() is atomic
    return rename(tempPath.c_str(), finalPath.c_str()) == 0;
#endif
}

bool windrop::FileUtils::cleanupTemp(const std::string &filename)
{
    bool success = true;
    std::string tempPath = getTempPath(filename);
    std::string metaPath = getMetaPath(filename);

#ifdef WINDROP_PLATFORM_WINDOWS
    try {
        if (fs::exists(tempPath)) fs::remove(tempPath);
        if (fs::exists(metaPath)) fs::remove(metaPath);
    } catch (const fs::system_error& e) {
        std::cerr << "Cleanup failed: " << e.what() << std::endl;
        success = false;
    }
#else
    if (remove(tempPath.c_str()) != 0 && errno != ENOENT) success = false;
    if (remove(metaPath.c_str()) != 0 && errno != ENOENT) success = false;
#endif

    return success;
}

int64_t windrop::FileUtils::getFileSize(const std::string &filepath)
{
#ifdef WINDROP_PLATFORM_WINDOWS
    try {
        return fs::file_size(filepath);
    } catch (const fs::system_error& e) {
        return -1;
    }
#else
    struct stat st;
    if (stat(filepath.c_str(), &st) == 0) {
        return st.st_size;
    }
    return -1;
#endif
}

uint32_t windrop::FileUtils::computeChecksum(const std::string &filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return 0;

    uint32_t sum = 0;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        std::streamsize bytes = file.gcount();
        for (std::streamsize i = 0; i < bytes; ++i) {
            sum += static_cast<unsigned char>(buffer[i]);
        }
    }
    return sum;
}