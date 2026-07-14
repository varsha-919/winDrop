/**
 * WinDrop Transfer Protocol Metadata
 *
 * Defines message types, parsing, and metadata structures
 * for resumable file transfer protocol.
 *
 * Message Format:
 *   TYPE:payload\n
 *
 * All messages are newline-delimited for easy parsing.
 */

#ifndef WINDROP_TRANSFER_META_H
#define WINDROP_TRANSFER_META_H

#include <string>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

// Configuration constants
namespace transfer
{
    // Default chunk size (4KB)
    constexpr int DEFAULT_CHUNK_SIZE = 4096;

    // Message types
    constexpr const char *MSG_HEADER = "HEADER";
    constexpr const char *MSG_RESUME_QUERY = "RESUME_QUERY";
    constexpr const char *MSG_RESUME_RESPONSE = "RESUME_RESPONSE";
    constexpr const char *MSG_CHUNK = "CHUNK";
    constexpr const char *MSG_ACK = "ACK";
    constexpr const char *MSG_COMPLETE = "COMPLETE";
    constexpr const char *MSG_RESET = "RESET";
    constexpr const char *MSG_ERROR = "ERROR";

    // ==== Transfer Request Messages ====
    constexpr const char *MSG_REQUEST = "REQUEST";
    constexpr const char *MSG_REQUEST_ACCEPT = "REQUEST_ACCEPT";
    constexpr const char *MSG_REQUEST_REJECT = "REQUEST_REJECT";
    constexpr const char *MSG_DELIVERED_ACK = "DELIVERED_ACK";

    // Response codes
    constexpr const char *RESUME_OK = "OK";
    constexpr const char *RESUME_NO = "NO";

    // Delimiters
    constexpr char MSG_DELIMITER = '\n';
    constexpr char FIELD_DELIMITER = '|';
}

/**
 * Transfer metadata structure
 * Persisted to .part.meta file for resume capability
 */
struct TransferMetadata
{
    std::string filename;  // Original filename
    int64_t fileSize;      // Total file size in bytes
    int chunkSize;         // Size of each chunk
    int totalChunks;       // Total number of chunks
    int lastAckedChunk;    // Last successfully acknowledged chunk
    std::string createdAt; // ISO 8601 timestamp
    std::string updatedAt; // ISO 8601 timestamp
    std::string senderIP;  // IP of sender
    uint32_t checksum;     // Final file checksum
    std::string requestId; // Transfer request ID (for request mode)

    TransferMetadata()
        : fileSize(0), chunkSize(transfer::DEFAULT_CHUNK_SIZE), totalChunks(0), lastAckedChunk(-1), checksum(0)
    {
    }

    /**
     * Save metadata to file
     */
    bool save(const std::string &filepath) const
    {
        std::ofstream out(filepath);
        if (!out.is_open())
            return false;

        out << "{\n";
        out << "  \"filename\": \"" << filename << "\",\n";
        out << "  \"fileSize\": " << fileSize << ",\n";
        out << "  \"chunkSize\": " << chunkSize << ",\n";
        out << "  \"totalChunks\": " << totalChunks << ",\n";
        out << "  \"lastAckedChunk\": " << lastAckedChunk << ",\n";
        out << "  \"createdAt\": \"" << createdAt << "\",\n";
        out << "  \"updatedAt\": \"" << updatedAt << "\",\n";
        out << "  \"senderIP\": \"" << senderIP << "\",\n";
        out << "  \"checksum\": " << checksum << ",\n";
        out << "  \"requestId\": \"" << requestId << "\"\n";
        out << "}\n";

        out.close();
        return true;
    }

    /**
     * Load metadata from file
     */
    bool load(const std::string &filepath)
    {
        std::ifstream in(filepath);
        if (!in.is_open())
            return false;

        // Simple JSON parsing (no external dependencies)
        std::string line;
        while (std::getline(in, line))
        {
            size_t colonPos = line.find(':');
            if (colonPos == std::string::npos)
                continue;

            std::string key = line.substr(0, colonPos);
            size_t valueStart = line.find('"', colonPos);
            size_t valueEnd = line.find_last_of('"');

            if (valueStart == std::string::npos || valueEnd == std::string::npos)
            {
                // Try parsing numbers
                std::string numStr = line.substr(colonPos + 1);
                // Remove commas
                size_t commaPos = numStr.find(',');
                if (commaPos != std::string::npos)
                    numStr = numStr.substr(0, commaPos);

                if (key.find("fileSize") != std::string::npos)
                    fileSize = std::stoll(numStr);
                else if (key.find("chunkSize") != std::string::npos)
                    chunkSize = std::stoi(numStr);
                else if (key.find("totalChunks") != std::string::npos)
                    totalChunks = std::stoi(numStr);
                else if (key.find("lastAckedChunk") != std::string::npos)
                    lastAckedChunk = std::stoi(numStr);
                else if (key.find("checksum") != std::string::npos)
                    checksum = static_cast<uint32_t>(std::stoul(numStr));
                continue;
            }

            std::string value = line.substr(valueStart + 1, valueEnd - valueStart - 1);

            if (key.find("filename") != std::string::npos)
                filename = value;
            else if (key.find("createdAt") != std::string::npos)
                createdAt = value;
            else if (key.find("updatedAt") != std::string::npos)
                updatedAt = value;
            else if (key.find("senderIP") != std::string::npos)
                senderIP = value;
            else if (key.find("requestId") != std::string::npos)
                requestId = value;
        }

        in.close();
        return true;
    }

    /**
     * Get current timestamp as ISO 8601 string
     */
    static std::string getTimestamp()
    {
        std::time_t now = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
        return std::string(buf);
    }
};

/**
 * Message parser for transfer protocol
 */
class TransferProtocol
{
public:
    /**
     * Parse a complete message
     * @param raw Raw message string
     * @param type Output: message type
     * @param payload Output: message payload
     * @return true if parsing successful
     */
    static bool parseMessage(const std::string &raw, std::string &type, std::string &payload)
    {
        size_t colonPos = raw.find(':');
        if (colonPos == std::string::npos)
        {
            // No colon - treat entire string as type
            type = raw;
            payload = "";
            return true;
        }

        type = raw.substr(0, colonPos);
        payload = raw.substr(colonPos + 1);
        return true;
    }

    /**
     * Build HEADER message
     * Format: HEADER:filename|filesize|chunksize|totalChunks\n
     */
    static std::string buildHeader(const std::string &filename, int64_t fileSize, int chunkSize, int totalChunks)
    {
        std::ostringstream oss;
        oss << transfer::MSG_HEADER << ":"
            << filename << transfer::FIELD_DELIMITER
            << fileSize << transfer::FIELD_DELIMITER
            << chunkSize << transfer::FIELD_DELIMITER
            << totalChunks << "\n";
        return oss.str();
    }

    /**
     * Parse HEADER message
     */
    static bool parseHeader(const std::string &payload, std::string &filename, int64_t &fileSize, int &chunkSize, int &totalChunks)
    {
        std::istringstream iss(payload);
        std::string fileSizeStr, chunkSizeStr, totalChunksStr;

        std::getline(iss, filename, transfer::FIELD_DELIMITER);
        std::getline(iss, fileSizeStr, transfer::FIELD_DELIMITER);
        std::getline(iss, chunkSizeStr, transfer::FIELD_DELIMITER);
        std::getline(iss, totalChunksStr, transfer::FIELD_DELIMITER);

        try
        {
            fileSize = std::stoll(fileSizeStr);
            chunkSize = std::stoi(chunkSizeStr);
            totalChunks = std::stoi(totalChunksStr);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * Build RESUME_QUERY message
     * Format: RESUME_QUERY:filename|filesize\n
     */
    static std::string buildResumeQuery(const std::string &filename, int64_t fileSize)
    {
        std::ostringstream oss;
        oss << transfer::MSG_RESUME_QUERY << ":"
            << filename << transfer::FIELD_DELIMITER
            << fileSize << "\n";
        return oss.str();
    }

    /**
     * Parse RESUME_QUERY message
     */
    static bool parseResumeQuery(const std::string &payload, std::string &filename, int64_t &fileSize)
    {
        std::istringstream iss(payload);
        std::string fileSizeStr;

        std::getline(iss, filename, transfer::FIELD_DELIMITER);
        std::getline(iss, fileSizeStr, transfer::FIELD_DELIMITER);

        try
        {
            fileSize = std::stoll(fileSizeStr);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * Build RESUME_RESPONSE message
     * Format: RESUME_RESPONSE:OK|lastChunk\n or RESUME_RESPONSE:NO\n
     */
    static std::string buildResumeResponse(bool canResume, int lastChunk = -1)
    {
        std::ostringstream oss;
        oss << transfer::MSG_RESUME_RESPONSE << ":";
        if (canResume)
        {
            oss << transfer::RESUME_OK << transfer::FIELD_DELIMITER << lastChunk;
        }
        else
        {
            oss << transfer::RESUME_NO;
        }
        oss << "\n";
        return oss.str();
    }

    /**
     * Parse RESUME_RESPONSE message
     */
    static bool parseResumeResponse(const std::string &payload, bool &canResume, int &lastChunk)
    {
        std::istringstream iss(payload);
        std::string status, lastChunkStr;

        std::getline(iss, status, transfer::FIELD_DELIMITER);
        std::getline(iss, lastChunkStr, transfer::FIELD_DELIMITER);

        canResume = (status == transfer::RESUME_OK);
        if (canResume && !lastChunkStr.empty())
        {
            try
            {
                lastChunk = std::stoi(lastChunkStr);
            }
            catch (...)
            {
                lastChunk = -1;
            }
        }
        else
        {
            lastChunk = -1;
        }
        return true;
    }

    /**
     * Build CHUNK message
     * Format: CHUNK:index|size\ndata
     * Note: size is included so receiver knows exact byte count (handles binary data with \n)
     */
    static std::string buildChunk(int index, const char *data, int size)
    {
        std::ostringstream oss;
        oss << transfer::MSG_CHUNK << ":" << index << transfer::FIELD_DELIMITER << size << "\n";
        std::string result = oss.str();
        result.append(data, size);
        return result;
    }

    /**
     * Parse CHUNK message header (index only, not data)
     */
    static bool parseChunkHeader(const std::string &payload, int &index)
    {
        size_t pipePos = payload.find(transfer::FIELD_DELIMITER);
        if (pipePos == std::string::npos)
            return false;

        try
        {
            index = std::stoi(payload.substr(0, pipePos));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * Build ACK message
     * Format: ACK:index\n
     */
    static std::string buildAck(int index)
    {
        std::ostringstream oss;
        oss << transfer::MSG_ACK << ":" << index << "\n";
        return oss.str();
    }

    /**
     * Parse ACK message
     */
    static bool parseAck(const std::string &payload, int &index)
    {
        try
        {
            index = std::stoi(payload);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * Build COMPLETE message
     * Format: COMPLETE:checksum\n
     */
    static std::string buildComplete(uint32_t checksum)
    {
        std::ostringstream oss;
        oss << transfer::MSG_COMPLETE << ":" << checksum << "\n";
        return oss.str();
    }

    /**
     * Build RESET message
     * Format: RESET:reason\n
     */
    static std::string buildReset(const std::string &reason)
    {
        std::ostringstream oss;
        oss << transfer::MSG_RESET << ":" << reason << "\n";
        return oss.str();
    }

    /**
     * Build ERROR message
     * Format: ERROR:message\n
     */
    static std::string buildError(const std::string &message)
    {
        std::ostringstream oss;
        oss << transfer::MSG_ERROR << ":" << message << "\n";
        return oss.str();
    }

    // ========================================================
    // Transfer Request Messages
    // ========================================================

    /**
     * Build REQUEST message
     * Format: REQUEST:requestId|filename|filesize|fileType|senderName|senderIP\n
     */
    static std::string buildRequest(const std::string &requestId, const std::string &filename,
                                    int64_t fileSize, const std::string &fileType,
                                    const std::string &senderName, const std::string &senderIP)
    {
        std::ostringstream oss;
        oss << transfer::MSG_REQUEST << ":"
            << requestId << transfer::FIELD_DELIMITER
            << filename << transfer::FIELD_DELIMITER
            << fileSize << transfer::FIELD_DELIMITER
            << fileType << transfer::FIELD_DELIMITER
            << senderName << transfer::FIELD_DELIMITER
            << senderIP << "\n";
        return oss.str();
    }

    /**
     * Parse REQUEST message
     */
    static bool parseRequest(const std::string &payload, std::string &requestId,
                             std::string &filename, int64_t &fileSize,
                             std::string &fileType, std::string &senderName,
                             std::string &senderIP)
    {
        std::istringstream iss(payload);
        std::string fileSizeStr;

        std::getline(iss, requestId, transfer::FIELD_DELIMITER);
        std::getline(iss, filename, transfer::FIELD_DELIMITER);
        std::getline(iss, fileSizeStr, transfer::FIELD_DELIMITER);
        std::getline(iss, fileType, transfer::FIELD_DELIMITER);
        std::getline(iss, senderName, transfer::FIELD_DELIMITER);
        std::getline(iss, senderIP, transfer::FIELD_DELIMITER);

        try
        {
            fileSize = std::stoll(fileSizeStr);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * Build REQUEST_ACCEPT message
     * Format: REQUEST_ACCEPT:requestId\n
     */
    static std::string buildRequestAccept(const std::string &requestId)
    {
        std::ostringstream oss;
        oss << transfer::MSG_REQUEST_ACCEPT << ":" << requestId << "\n";
        return oss.str();
    }

    /**
     * Parse REQUEST_ACCEPT message
     */
    static bool parseRequestAccept(const std::string &payload, std::string &requestId)
    {
        requestId = payload;
        return !requestId.empty();
    }

    /**
     * Build REQUEST_REJECT message
     * Format: REQUEST_REJECT:requestId|reason\n
     */
    static std::string buildRequestReject(const std::string &requestId, const std::string &reason)
    {
        std::ostringstream oss;
        oss << transfer::MSG_REQUEST_REJECT << ":"
            << requestId << transfer::FIELD_DELIMITER
            << reason << "\n";
        return oss.str();
    }

    /**
     * Parse REQUEST_REJECT message
     */
    static bool parseRequestReject(const std::string &payload, std::string &requestId, std::string &reason)
    {
        std::istringstream iss(payload);
        std::getline(iss, requestId, transfer::FIELD_DELIMITER);
        std::getline(iss, reason, transfer::FIELD_DELIMITER);
        return true;
    }

    /**
     * Build DELIVERED_ACK message
     * Format: DELIVERED_ACK:requestId\n
     */
    static std::string buildDeliveredAck(const std::string &requestId)
    {
        std::ostringstream oss;
        oss << transfer::MSG_DELIVERED_ACK << ":" << requestId << "\n";
        return oss.str();
    }

    /**
     * Parse DELIVERED_ACK message
     */
    static bool parseDeliveredAck(const std::string &payload, std::string &requestId)
    {
        requestId = payload;
        return !requestId.empty();
    }
};

#endif // WINDROP_TRANSFER_META_H