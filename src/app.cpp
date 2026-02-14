#include "app.h"

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void App::load_servers(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;

    json j;
    try {
        f >> j;
    } catch (...) {
        return;
    }

    servers.clear();
    selected = -1;
    for (auto& entry : j["servers"]) {
        ServerEntry se;
        se.info.address = entry.value("address", "");
        se.info.port = entry.value("port", 7777);
        se.info.status = "idle";
        servers.push_back(std::move(se));
    }
}

void App::save_servers(const std::string& path) const {
    json j;
    j["servers"] = json::array();
    for (auto& se : servers) {
        j["servers"].push_back({
            {"address", se.info.address},
            {"port", se.info.port}
        });
    }

    std::ofstream f(path);
    if (f.is_open()) {
        f << j.dump(2) << std::endl;
    }
}

void App::add_server(const std::string& ip, uint16_t port) {
    ServerEntry se;
    se.info.address = ip;
    se.info.port = port;
    se.info.status = "idle";
    servers.push_back(std::move(se));
}

void App::remove_server(int index) {
    if (index >= 0 && index < static_cast<int>(servers.size())) {
        servers.erase(servers.begin() + index);
        if (selected == index) selected = -1;
        else if (selected > index) --selected;
    }
}

void App::refresh_all() {
    for (int i = 0; i < static_cast<int>(servers.size()); ++i) {
        refresh_one(i);
    }
}

void App::refresh_one(int index) {
    if (index < 0 || index >= static_cast<int>(servers.size())) return;
    auto& se = servers[index];
    if (se.state == QueryState::Querying) return;

    se.state = QueryState::Querying;
    se.info.status = "querying";
    std::string ip = se.info.address;
    uint16_t port = se.info.port;
    se.future = std::async(std::launch::async, [ip, port]() {
        return query_server(ip, port);
    });
}

void App::poll_results() {
    for (auto& se : servers) {
        if (se.state != QueryState::Querying) continue;
        if (!se.future.valid()) continue;

        auto status = se.future.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            auto result = se.future.get();
            // Preserve address/port from config
            std::string addr = se.info.address;
            uint16_t port = se.info.port;
            se.info = std::move(result);
            se.info.address = addr;
            se.info.port = port;
            se.state = QueryState::Done;
        }
    }
}
