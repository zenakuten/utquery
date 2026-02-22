#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct PlayerInfo {
    std::string name;
    int32_t score;
    int team = -1; // 0=red, 1=blue, 2=spectator, -1=unknown
};

struct ServerInfo {
    std::string address;
    uint16_t port;
    std::string name, map_title, map_name, gametype;
    int32_t max_players = 0, num_players = 0, ping = 0, flags = 0;
    uint8_t skill = 0;
    std::vector<PlayerInfo> players;
    std::multimap<std::string, std::string> variables;
    bool online = false;
    std::string status = "idle";
};

// Must be called once before any queries (WSAStartup)
void query_init();

// Must be called once at shutdown (WSACleanup)
void query_cleanup();

// Query a UT2004 server. Sends UDP queries to game_port + 1.
// Blocking call — run on a worker thread.
ServerInfo query_server(const std::string& ip, uint16_t game_port);
