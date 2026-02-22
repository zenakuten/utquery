#include "app.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using json = nlohmann::json;

static const std::vector<App::MasterServer> default_master_servers = {
    {"utmaster.openspy.net", 28902},
    {"ut2004master.333networks.com", 28902},
};

void App::load_servers(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        master_servers = default_master_servers;
        return;
    }

    json j;
    try {
        f >> j;
    } catch (...) {
        master_servers = default_master_servers;
        return;
    }

    servers.clear();
    selected = -1;
    int ord = 0;
    for (auto& entry : j["servers"]) {
        ServerEntry se;
        se.info.address = entry.value("address", "");
        se.info.port = entry.value("port", 7777);
        se.info.status = "idle";
        se.order = entry.value("order", ord);
        servers.push_back(std::move(se));
        ++ord;
    }
    std::sort(servers.begin(), servers.end(),
        [](const ServerEntry& a, const ServerEntry& b) { return a.order < b.order; });
    // Normalize order values
    for (int i = 0; i < static_cast<int>(servers.size()); ++i)
        servers[i].order = i + 1;

    master_servers.clear();
    if (j.contains("master_servers")) {
        for (auto& entry : j["master_servers"]) {
            MasterServer ms;
            ms.host = entry.value("host", "");
            ms.port = entry.value("port", 28902);
            if (!ms.host.empty())
                master_servers.push_back(std::move(ms));
        }
    }
    if (master_servers.empty())
        master_servers = default_master_servers;
    master_selected = 0;

    if (j.contains("font_size_idx"))
        font_size_idx = std::clamp(j["font_size_idx"].get<int>(), 0, 3);
}

void App::save_servers(const std::string& path) const {
#ifndef _WIN32
    // Ensure parent directory exists
    auto slash = path.rfind('/');
    if (slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        mkdir(dir.c_str(), 0755);
    }
#endif

    json j;
    j["servers"] = json::array();
    // Save in user-defined order
    std::vector<const ServerEntry*> ordered;
    for (auto& se : servers)
        ordered.push_back(&se);
    std::sort(ordered.begin(), ordered.end(),
        [](const ServerEntry* a, const ServerEntry* b) { return a->order < b->order; });
    for (auto* se : ordered) {
        j["servers"].push_back({
            {"address", se->info.address},
            {"port", se->info.port},
            {"order", se->order}
        });
    }

    j["master_servers"] = json::array();
    for (auto& ms : master_servers) {
        j["master_servers"].push_back({
            {"host", ms.host},
            {"port", ms.port}
        });
    }

    j["font_size_idx"] = font_size_idx;

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
    // Assign order after last entry
    int max_order = 0;
    for (auto& s : servers)
        if (s.order > max_order) max_order = s.order;
    se.order = max_order + 1;
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
    poll_internet_results();
    poll_master_results();
}

void App::refresh_internet_one(int index) {
    if (index < 0 || index >= static_cast<int>(internet_servers.size())) return;
    auto& se = internet_servers[index];
    if (se.state == QueryState::Querying) return;

    se.state = QueryState::Querying;
    se.info.status = "querying";
    std::string ip = se.info.address;
    uint16_t port = se.info.port;
    se.future = std::async(std::launch::async, [ip, port]() {
        return query_server(ip, port);
    });
}

void App::refresh_internet_all() {
    for (int i = 0; i < static_cast<int>(internet_servers.size()); ++i) {
        refresh_internet_one(i);
    }
}

void App::poll_internet_results() {
    for (auto& se : internet_servers) {
        if (se.state != QueryState::Querying) continue;
        if (!se.future.valid()) continue;

        auto status = se.future.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            auto result = se.future.get();
            std::string addr = se.info.address;
            uint16_t port = se.info.port;
            se.info = std::move(result);
            se.info.address = addr;
            se.info.port = port;
            se.state = QueryState::Done;
        }
    }
}

// Normalize a raw cdkey string: filter characters, uppercase, insert dashes.
static std::string normalize_cdkey(const std::string& raw) {
    std::string key;
    for (char ch : raw) {
        if ((ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            ch == '-')
            key.push_back(ch);
        else if (ch == ' ' || ch == '_')
            key.push_back('-');
        else
            break; // stop at first invalid char (like UT2004 does)
    }
    for (auto& c : key) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (key.find('-') == std::string::npos && key.size() == 20) {
        key = key.substr(0,5) + "-" + key.substr(5,5) + "-" +
              key.substr(10,5) + "-" + key.substr(15,5);
    }
    if (key.size() > 23)
        key = key.substr(0, 23);
    return key;
}

#ifdef _WIN32
#include <windows.h>
static std::string read_cdkey_from_registry() {
    // Try WOW6432Node first (32-bit app on 64-bit Windows, or explicit path)
    const char* subkeys[] = {
        "SOFTWARE\\WOW6432Node\\Unreal Technology\\Installed Apps\\UT2004",
        "SOFTWARE\\Unreal Technology\\Installed Apps\\UT2004",
    };
    for (auto subkey : subkeys) {
        HKEY hkey = nullptr;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
            char buf[256] = {};
            DWORD size = sizeof(buf) - 1;
            DWORD type = 0;
            if (RegQueryValueExA(hkey, "CDKey", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS
                && type == REG_SZ) {
                RegCloseKey(hkey);
                return std::string(buf);
            }
            RegCloseKey(hkey);
        }
    }
    return {};
}
#endif

void App::load_cdkey(const std::string& path) {
    std::string raw;

    // Try file first
    std::ifstream f(path);
    if (f.is_open()) {
        std::getline(f, raw);
        std::fprintf(stderr, "cdkey: read from file '%s'\n", path.c_str());
    }

#ifdef _WIN32
    // Fall back to Windows registry
    if (raw.empty()) {
        raw = read_cdkey_from_registry();
        if (!raw.empty())
            std::fprintf(stderr, "cdkey: read from Windows registry\n");
    }
#endif

    if (raw.empty()) {
        std::fprintf(stderr, "cdkey: not found (no file '%s'%s)\n",
                     path.c_str(),
#ifdef _WIN32
                     " and not in registry"
#else
                     ""
#endif
                     );
        return;
    }

    cdkey = normalize_cdkey(raw);
    std::fprintf(stderr, "cdkey: loaded key len=%zu fmt='%.5s-...-%.5s'\n",
                 cdkey.size(), cdkey.c_str(),
                 cdkey.size() >= 5 ? cdkey.c_str() + cdkey.size() - 5 : "");
}

void App::query_master(const std::string& host, uint16_t port,
                       const std::string& gametype_filter) {
    if (master_future_.valid()) return; // already querying
    if (cdkey.empty()) {
        master_status = "error: no cdkey (create a 'cdkey' file"
#ifndef _WIN32
            " in ~/.ut2004/, ~/.utquery/, or current dir"
#endif
            ")";
        return;
    }
    master_status = "querying master...";
    std::string key = cdkey;
    master_future_ = std::async(std::launch::async, [host, port, key, gametype_filter]() {
        return query_master_server(host, port, key, gametype_filter);
    });
}

void App::poll_master_results() {
    if (!master_future_.valid()) return;

    auto status = master_future_.wait_for(std::chrono::milliseconds(0));
    if (status != std::future_status::ready) return;

    auto qr = master_future_.get();
    internet_servers.clear();
    internet_selected = -1;

    for (auto& me : qr.servers) {
        ServerEntry se;
        se.info.address = me.ip;
        se.info.port = me.port;
        se.info.name = me.name;
        se.info.map_name = me.map_name;
        se.info.gametype = me.game_type;
        se.info.num_players = me.current_players;
        se.info.max_players = me.max_players;
        se.info.flags = me.flags;
        se.info.status = "idle";
        se.info.online = true;
        internet_servers.push_back(std::move(se));
    }

    if (!qr.error.empty()) {
        master_status = "error: " + qr.error;
    } else if (qr.servers.empty()) {
        master_status = "no servers found";
    } else {
        master_status = std::to_string(qr.servers.size()) + " servers";
    }
}
