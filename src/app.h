#pragma once

#include "master.h"
#include "query.h"

#include <future>
#include <string>
#include <vector>

enum class QueryState { Idle, Querying, Done };

struct ServerEntry {
    ServerInfo info;
    QueryState state = QueryState::Idle;
    std::future<ServerInfo> future;
};

class App {
public:
    // Favorites tab
    std::vector<ServerEntry> servers;
    int selected = -1;

    // Internet tab
    std::vector<ServerEntry> internet_servers;
    int internet_selected = -1;

    void load_servers(const std::string& path);
    void save_servers(const std::string& path) const;
    void add_server(const std::string& ip, uint16_t port);
    void remove_server(int index);
    void refresh_all();
    void refresh_one(int index);
    void poll_results();

    // Internet tab helpers
    void refresh_internet_one(int index);
    void refresh_internet_all();
    void poll_internet_results();

    // Master server list
    struct MasterServer {
        std::string host;
        uint16_t port = 28902;
    };
    std::vector<MasterServer> master_servers;
    int master_selected = 0;

    // Master server query
    void load_cdkey(const std::string& path);
    std::string cdkey;

    void query_master(const std::string& host, uint16_t port,
                      const std::string& gametype_filter = "");
    void poll_master_results();
    bool master_querying() const { return master_future_.valid(); }
    std::string master_status;

private:
    std::future<MasterQueryResult> master_future_;
};
