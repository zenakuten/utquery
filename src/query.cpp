#include "query.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef _WIN32
using socket_t = SOCKET;
static constexpr socket_t SOCKET_INVALID = INVALID_SOCKET;
#else
using socket_t = int;
static constexpr socket_t SOCKET_INVALID = -1;
#endif

// Skip the 1-byte length prefix that UT2004 prepends to string fields.
static std::string skip_length_prefix(const std::string& s) {
    return s.size() > 1 ? s.substr(1) : s;
}

static std::string strip_control_chars(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0x1B && i + 3 < s.size()) {
            // Preserve UT2004 color code: ESC + R + G + B
            result.push_back(s[i]);
            result.push_back(s[i + 1]);
            result.push_back(s[i + 2]);
            result.push_back(s[i + 3]);
            i += 3;
        } else if (c >= 32 && c != 127) {
            if (c >= 0x80) {
                // Latin-1 to UTF-8: encode as 2-byte sequence
                result.push_back(static_cast<char>(0xC0 | (c >> 6)));
                result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            } else {
                result.push_back(static_cast<char>(c));
            }
        }
    }
    return result;
}

static std::vector<std::string> split_nulls(const uint8_t* data, size_t len) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == 0) {
            parts.emplace_back(reinterpret_cast<const char*>(data + start), i - start);
            start = i + 1;
        }
    }
    if (start < len)
        parts.emplace_back(reinterpret_cast<const char*>(data + start), len - start);
    return parts;
}

static int32_t read_int32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void query_init() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

void query_cleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Send a UT2004 query packet and receive response.
// Returns number of bytes received, or -1 on error/timeout.
// Validates that the response query type matches; drains stale packets.
static int send_query(socket_t sock, const sockaddr_in& addr, uint8_t query_type,
                      uint8_t* buf, size_t buf_size) {
    uint8_t packet[5] = {0x78, 0x00, 0x00, 0x00, query_type};
    sendto(sock, reinterpret_cast<const char*>(packet), 5, 0,
           reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));

    // Keep reading packets until we get one matching our query type or timeout
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        auto now = std::chrono::steady_clock::now();
        auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
        if (remaining.count() <= 0) return -1;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv;
        tv.tv_sec = static_cast<long>(remaining.count() / 1000000);
        tv.tv_usec = static_cast<long>(remaining.count() % 1000000);

#ifdef _WIN32
        int sel = select(0, &fds, nullptr, nullptr, &tv);
#else
        int sel = select(sock + 1, &fds, nullptr, nullptr, &tv);
#endif
        if (sel <= 0) return -1;

        int n = recvfrom(sock, reinterpret_cast<char*>(buf), static_cast<int>(buf_size), 0,
                         nullptr, nullptr);
        if (n < 5) continue;

        // Response header: 0x80 0x00 0x00 0x00 <query_type>
        if (buf[4] == query_type)
            return n;
        // Wrong query type — stale packet from a previous query, drain and retry
    }
}

static void parse_players(ServerInfo& info, const uint8_t* data, int len) {
    if (len < 5) return;

    int offset = 5; // skip header
    while (offset + 4 < len) {
        int32_t score = read_int32(data + offset);
        offset += 4;

        // Read null-terminated name (first byte is a length prefix, skip it)
        std::string name;
        while (offset < len && data[offset] != 0) {
            name.push_back(static_cast<char>(data[offset]));
            ++offset;
        }
        if (offset < len) ++offset; // skip null

        name = strip_control_chars(skip_length_prefix(name));

        // 3 trailing int32 fields: ping(4) + statsid(4) + team_raw(4)
        if (offset + 12 > len)
            break;
        int32_t team_raw = read_int32(data + offset + 8);
        offset += 12;

        // team_raw == 0 with empty name means metadata entry (team scores, round info) — skip
        if (team_raw == 0 && name.empty())
            continue;

        if (!name.empty()) {
            int team;
            if (team_raw == 0x20000000) team = 0;      // red
            else if (team_raw == 0x40000000) team = 1;  // blue
            else if (team_raw == 0) team = 2;            // spectator (no team)
            else team = 2;                               // spectator/other
            info.players.push_back({name, score, team});
        }
    }
}

static void parse_server_info(ServerInfo& info, const uint8_t* data, int len) {
    auto parts = split_nulls(data, len);

    // Server info response has fields split by null bytes
    // Fields at indices: 15=server name, 16=map name, 17=gametype
    // Each string field has a 1-byte length prefix that must be skipped.
    if (parts.size() > 17) {
        info.name = strip_control_chars(skip_length_prefix(parts[15]));
        info.map_name = strip_control_chars(skip_length_prefix(parts[16]));
        info.gametype = strip_control_chars(skip_length_prefix(parts[17]));
    }

    // Binary trailer follows the gametype string.
    // Can't use split_nulls for this because int32 values contain 0x00 bytes.
    // Calculate the raw offset by summing parts[0..17] sizes + their null terminators.
    if (parts.size() > 17) {
        size_t trailer_offset = 0;
        for (size_t i = 0; i <= 17; ++i) {
            trailer_offset += parts[i].size() + 1; // +1 for null terminator
        }
        size_t remaining = len - trailer_offset;
        if (remaining >= 13) {
            const uint8_t* trailer = data + trailer_offset;
            info.num_players = read_int32(trailer);
            info.max_players = read_int32(trailer + 4);
            info.flags = read_int32(trailer + 8);
            info.skill = trailer[12];
        }
    }
}

static void parse_variables(ServerInfo& info, const uint8_t* data, int len) {
    auto parts = split_nulls(data, len);

    // Variables come as key-value pairs starting at index 3, step 2
    for (size_t i = 3; i + 1 < parts.size(); i += 2) {
        std::string key = strip_control_chars(parts[i]);
        std::string val = strip_control_chars(parts[i + 1]);
        if (!key.empty()) {
            info.variables.emplace(key, val);
        }
    }
}

ServerInfo query_server(const std::string& ip, uint16_t game_port) {
    ServerInfo info;
    info.address = ip;
    info.port = game_port;
    info.status = "querying";

    uint16_t query_port = game_port + 1;

    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == SOCKET_INVALID) {
        info.status = "socket error";
        return info;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(query_port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    uint8_t buf[65535];

    // Query 0x02: players
    int n = send_query(sock, addr, 0x02, buf, sizeof(buf));
    if (n > 0) {
        parse_players(info, buf, n);
    }

    // Query 0x00: server info — measure ping from this single round-trip
    auto ping_start = std::chrono::steady_clock::now();
    n = send_query(sock, addr, 0x00, buf, sizeof(buf));
    auto ping_end = std::chrono::steady_clock::now();
    if (n > 0) {
        parse_server_info(info, buf, n);
        info.online = true;
        info.ping = static_cast<int32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(ping_end - ping_start).count());
    }

    // Query 0x01: variables
    n = send_query(sock, addr, 0x01, buf, sizeof(buf));
    if (n > 0) {
        parse_variables(info, buf, n);
    }

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif

    info.status = info.online ? "online" : "timeout";
    return info;
}
