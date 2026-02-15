#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct MasterServerEntry {
    std::string ip;
    uint16_t port = 0;
    uint16_t query_port = 0;
    std::string name;
    std::string map_name;
    std::string game_type;
    int current_players = 0;
    int max_players = 0;
    int flags = 0;
};

struct MasterQueryResult {
    std::vector<MasterServerEntry> servers;
    std::string error; // empty on success
};

// Query the UT2004 master server for a list of game servers.
// cdkey: CD key string like "XXXXX-XXXXX-XXXXX-XXXXX"
// gametype_filter: class name like "xDeathMatch", or empty for all.
// Blocking call — run on a worker thread.
MasterQueryResult query_master_server(
    const std::string& master_host, uint16_t master_port,
    const std::string& cdkey,
    const std::string& gametype_filter = "");
