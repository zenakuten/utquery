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

static std::string strip_control_chars(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        if (c >= 32 && c != 127)
            result.push_back(static_cast<char>(c));
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
static int send_query(socket_t sock, const sockaddr_in& addr, uint8_t query_type,
                      uint8_t* buf, size_t buf_size) {
    uint8_t packet[5] = {0x78, 0x00, 0x00, 0x00, query_type};
    sendto(sock, reinterpret_cast<const char*>(packet), 5, 0,
           reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sock, &fds);
    timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

#ifdef _WIN32
    int sel = select(0, &fds, nullptr, nullptr, &tv);
#else
    int sel = select(sock + 1, &fds, nullptr, nullptr, &tv);
#endif
    if (sel <= 0) return -1;

    int n = recvfrom(sock, reinterpret_cast<char*>(buf), static_cast<int>(buf_size), 0,
                     nullptr, nullptr);
    return n;
}

static void parse_players(ServerInfo& info, const uint8_t* data, int len) {
    if (len < 5) return;

    int offset = 5; // skip header
    while (offset + 4 < len) {
        int32_t score = read_int32(data + offset);
        offset += 4;

        // Read null-terminated name
        std::string name;
        while (offset < len && data[offset] != 0) {
            name.push_back(static_cast<char>(data[offset]));
            ++offset;
        }
        if (offset < len) ++offset; // skip null

        name = strip_control_chars(name);

        // Stop at team score entries
        if (name == "Red Team Score" || name == "Blue Team Score")
            break;

        // Skip the 4-byte ping field after name
        if (offset + 4 <= len)
            offset += 4;

        if (!name.empty()) {
            info.players.push_back({name, score});
        }
    }
}

static void parse_server_info(ServerInfo& info, const uint8_t* data, int len) {
    auto parts = split_nulls(data, len);

    // Server info response has fields split by null bytes
    // Typical layout: header bytes, then null-separated strings
    // Fields at indices: 15=server name, 16=map title, 17=map name
    if (parts.size() > 17) {
        info.name = strip_control_chars(parts[15]);
        info.map_title = strip_control_chars(parts[16]);
        info.map_name = strip_control_chars(parts[17]);
    }

    // After the map name, look for gametype string followed by binary data
    if (parts.size() > 18) {
        info.gametype = strip_control_chars(parts[18]);
    }

    // Find binary fields after gametype: look for the raw bytes
    // after the last null-separated string section
    // The binary trailer typically contains: num_players(4), max_players(4), ping(4), flags(4), skill(1)
    if (parts.size() > 19 && !parts[19].empty()) {
        const uint8_t* trailer = reinterpret_cast<const uint8_t*>(parts[19].data());
        size_t tlen = parts[19].size();
        if (tlen >= 13) {
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
            info.variables[key] = val;
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

    uint8_t buf[2048];
    auto start = std::chrono::steady_clock::now();

    // Query 0x02: players
    int n = send_query(sock, addr, 0x02, buf, sizeof(buf));
    if (n > 0) {
        parse_players(info, buf, n);
    }

    // Query 0x00: server info
    n = send_query(sock, addr, 0x00, buf, sizeof(buf));
    if (n > 0) {
        parse_server_info(info, buf, n);
        info.online = true;
    }

    // Query 0x01: variables
    n = send_query(sock, addr, 0x01, buf, sizeof(buf));
    if (n > 0) {
        parse_variables(info, buf, n);
    }

    auto end = std::chrono::steady_clock::now();
    info.ping = static_cast<int32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif

    info.status = info.online ? "online" : "timeout";
    return info;
}
