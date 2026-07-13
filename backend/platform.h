/**
 * Platform Abstraction Layer for WinDrop C++ Networking Engine
 *
 * Provides unified cross-platform APIs for:
 * - Socket initialization (WSAStartup/WSACleanup on Windows)
 * - Socket types (SOCKET vs int)
 * - Close functions (closesocket vs close)
 * - Sleep functions (Sleep vs sleep)
 * - Network utilities (getLocalIP, etc.)
 *
 * Supports: Windows, Linux, macOS
 */

#ifndef WINDROP_PLATFORM_H
#define WINDROP_PLATFORM_H

#include <string>
#include <cstring>
#include <cstdint>

// Platform detection
#ifdef _WIN32
#define WINDROP_PLATFORM_WINDOWS
#elif defined(__APPLE__)
#define WINDROP_PLATFORM_MACOS
#elif defined(__linux__)
#define WINDROP_PLATFORM_LINUX
#endif

// Platform-specific includes and types
#ifdef WINDROP_PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// Windows uses SOCKET type (unsigned int)
typedef SOCKET WindropSocket;
#define WINDROP_INVALID_SOCKET INVALID_SOCKET
#define WINDROP_SOCKET_ERROR SOCKET_ERROR
#else
// POSIX platforms
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>

// POSIX platforms use int
typedef int WindropSocket;
#define WINDROP_INVALID_SOCKET (-1)
#define WINDROP_SOCKET_ERROR (-1)
#endif

namespace windrop
{

    /**
     * Platform initialization and cleanup
     */
    class Platform
    {
    public:
        /**
         * Initialize platform-specific networking (WSAStartup on Windows)
         * @return true if initialization successful
         */
        static bool initialize();

        /**
         * Cleanup platform-specific networking (WSACleanup on Windows)
         */
        static void cleanup();

        /**
         * Check if platform is initialized
         * @return true if initialized
         */
        static bool isInitialized();
    };

    /**
     * Socket utility functions
     */
    class SocketUtils
    {
    public:
        /**
         * Close a socket
         * @param sock Socket to close
         */
        static void closeSocket(WindropSocket sock);

        /**
         * Set socket to reuse address
         * @param sock Socket to configure
         * @return true if successful
         */
        static bool setReuseAddr(WindropSocket sock);

        /**
         * Set socket to broadcast mode
         * @param sock Socket to configure
         * @return true if successful
         */
        static bool setBroadcast(WindropSocket sock);
    };

    /**
     * Sleep utility - cross-platform sleep
     */
    class SleepUtils
    {
    public:
        /**
         * Sleep for specified milliseconds
         * @param ms Milliseconds to sleep
         */
        static void sleep(unsigned int ms);
    };

    /**
     * Network utility functions
     */
    class NetworkUtils
    {
    public:
        /**
         * Get local IP address
         * @return Local IP as string, or "127.0.0.1" on failure
         */
        static std::string getLocalIP();

        /**
         * Get hostname
         * @return Hostname string
         */
        static std::string getHostname();
    };

    /**
     * Conversion utilities
     */
    class Convert
    {
    public:
        /**
         * Convert string IP to binary format (sockaddr_in)
         * @param ipStr IP address string
         * @param addr Output sockaddr_in structure
         * @return true if conversion successful
         */
        static bool stringToAddr(const std::string &ipStr, struct sockaddr_in &addr);
    };

} // namespace windrop

#endif // WINDROP_PLATFORM_H