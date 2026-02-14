#pragma once

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
    std::vector<ServerEntry> servers;
    int selected = -1;

    void load_servers(const std::string& path);
    void save_servers(const std::string& path) const;
    void add_server(const std::string& ip, uint16_t port);
    void remove_server(int index);
    void refresh_all();
    void refresh_one(int index);
    void poll_results();
};
