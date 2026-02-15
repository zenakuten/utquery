#include "app.h"
#include "query.h"
#include "utcolor.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using json = nlohmann::json;

static const char* CONFIG_PATH = "servers.json";

// Render the server table + detail panel for a given server list.
// table_id must be unique per tab. Returns remove_idx or -1.
static void draw_server_list(
    std::vector<ServerEntry>& servers, int& selected,
    const char* table_id, const char* child_id, const char* detail_id,
    const char* splitter_id, ImGuiIO& io,
    float& detail_height, bool show_remove,
    bool& auto_refresh, float& refresh_interval,
    int* add_favorite_idx = nullptr)
{
    float splitter_thickness = 6.0f;
    float avail_height = ImGui::GetContentRegionAvail().y;
    float table_height;
    if (selected >= 0) {
        detail_height = std::clamp(detail_height, 100.0f, avail_height - 100.0f);
        table_height = avail_height - detail_height - splitter_thickness;
    } else {
        table_height = avail_height;
    }

    if (ImGui::BeginChild(child_id, ImVec2(0, table_height))) {
        if (ImGui::BeginTable(table_id, show_remove ? 8 : 7,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Sortable)) {

            if (show_remove)
                ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, 50.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Gametype", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableHeadersRow();

            // Sorting
            if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
                if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
                    const auto& spec = sort_specs->Specs[0];
                    // Map column index to data field (offset by 1 if Action column present)
                    int col = show_remove ? spec.ColumnIndex - 1 : spec.ColumnIndex;
                    bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                    // Remember which server is selected so we can track it
                    ServerEntry* sel_ptr = (selected >= 0 && selected < static_cast<int>(servers.size()))
                        ? &servers[selected] : nullptr;
                    std::string sel_addr;
                    uint16_t sel_port = 0;
                    if (sel_ptr) {
                        sel_addr = sel_ptr->info.address;
                        sel_port = sel_ptr->info.port;
                    }
                    std::sort(servers.begin(), servers.end(),
                        [col, ascending](const ServerEntry& a, const ServerEntry& b) {
                            int cmp = 0;
                            switch (col) {
                                case 0: cmp = a.info.name.compare(b.info.name); break;
                                case 1: cmp = a.info.map_name.compare(b.info.map_name); break;
                                case 2: cmp = a.info.gametype.compare(b.info.gametype); break;
                                case 3: cmp = a.info.num_players - b.info.num_players; break;
                                case 4: cmp = a.info.max_players - b.info.max_players; break;
                                case 5: cmp = a.info.ping - b.info.ping; break;
                                case 6: cmp = a.info.status.compare(b.info.status); break;
                                default: break;
                            }
                            return ascending ? cmp < 0 : cmp > 0;
                        });
                    // Restore selection to the same server
                    if (sel_ptr) {
                        selected = -1;
                        for (int i = 0; i < static_cast<int>(servers.size()); ++i) {
                            if (servers[i].info.address == sel_addr && servers[i].info.port == sel_port) {
                                selected = i;
                                break;
                            }
                        }
                    }
                    sort_specs->SpecsDirty = false;
                }
            }

            int remove_idx = -1;
            for (int i = 0; i < static_cast<int>(servers.size()); ++i) {
                auto& se = servers[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);

                if (show_remove) {
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::SmallButton("X")) {
                        remove_idx = i;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("R")) {
                        // Caller handles refresh
                    }
                }

                // Name column — clickable to select
                int name_col = show_remove ? 1 : 0;
                ImGui::TableSetColumnIndex(name_col);
                bool is_selected = (selected == i);
                std::string raw_label = se.info.name.empty()
                    ? (se.info.address + ":" + std::to_string(se.info.port))
                    : se.info.name;
                ImVec2 text_pos = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable(("##srv" + std::to_string(i)).c_str(), is_selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected = is_selected ? -1 : i;
                }
                if (show_remove && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("FAV_REORDER", &i, sizeof(int));
                    std::string drag_label = se.info.name.empty()
                        ? (se.info.address + ":" + std::to_string(se.info.port))
                        : strip_ut_colors(se.info.name);
                    ImGui::Text("Move: %s", drag_label.c_str());
                    ImGui::EndDragDropSource();
                }
                if (show_remove && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FAV_REORDER")) {
                        int src = *static_cast<const int*>(payload->Data);
                        int dst = i;
                        if (src != dst) {
                            ServerEntry tmp = std::move(servers[src]);
                            servers.erase(servers.begin() + src);
                            servers.insert(servers.begin() + dst, std::move(tmp));
                            if (selected == src)
                                selected = dst;
                            else if (src < dst && selected > src && selected <= dst)
                                --selected;
                            else if (src > dst && selected >= dst && selected < src)
                                ++selected;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (add_favorite_idx && ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Add to Favorites")) {
                        *add_favorite_idx = i;
                    }
                    ImGui::EndPopup();
                }
                TextUTOverlay(ImGui::GetWindowDrawList(), text_pos, raw_label);

                ImGui::TableSetColumnIndex(name_col + 1);
                TextUT(se.info.map_name);

                ImGui::TableSetColumnIndex(name_col + 2);
                TextUT(se.info.gametype);

                ImGui::TableSetColumnIndex(name_col + 3);
                ImGui::Text("%d", se.info.num_players);

                ImGui::TableSetColumnIndex(name_col + 4);
                ImGui::Text("%d", se.info.max_players);

                ImGui::TableSetColumnIndex(name_col + 5);
                if (se.info.online)
                    ImGui::Text("%d", se.info.ping);
                else
                    ImGui::TextUnformatted("-");

                ImGui::TableSetColumnIndex(name_col + 6);
                ImGui::TextUnformatted(se.info.status.c_str());

                ImGui::PopID();
            }
            ImGui::EndTable();

            if (remove_idx >= 0 && show_remove) {
                if (selected == remove_idx) selected = -1;
                else if (selected > remove_idx) --selected;
                servers.erase(servers.begin() + remove_idx);
            }
        }
    }
    ImGui::EndChild();

    // Detail panel
    if (selected >= 0 && selected < static_cast<int>(servers.size())) {
        // Draggable horizontal splitter
        ImGui::InvisibleButton(splitter_id, ImVec2(-1, splitter_thickness));
        if (ImGui::IsItemActive())
            detail_height -= io.MouseDelta.y;
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

        auto& se = servers[selected];

        if (ImGui::BeginChild(detail_id, ImVec2(0, 0))) {
            ImGui::Text("Server: %s:%d", se.info.address.c_str(), se.info.port);
            ImGui::SameLine(0, 20);
            {
                std::string map_plain = strip_ut_colors(se.info.map_name);
                std::string title_plain = strip_ut_colors(se.info.map_title);
                ImGui::Text("Map: %s (%s)", map_plain.c_str(), title_plain.c_str());
            }
            ImGui::SameLine(0, 20);
            {
                std::string gt_plain = strip_ut_colors(se.info.gametype);
                ImGui::Text("Gametype: %s", gt_plain.c_str());
            }
            ImGui::SameLine(0, 30);
            ImGui::Checkbox("Auto Refresh", &auto_refresh);
            ImGui::SameLine();
            if (!auto_refresh) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("##RefreshInterval", &refresh_interval, 10.0f, 60.0f, "%.0f s");
            if (!auto_refresh) ImGui::EndDisabled();

            // Two columns: players on left, variables on right
            if (ImGui::BeginTable("DetailColumns", 2, ImGuiTableFlags_Resizable)) {
                ImGui::TableNextRow();

                // Players
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Players (%d/%d):", se.info.num_players, se.info.max_players);
                float sub_table_height = ImGui::GetContentRegionAvail().y;
                if (ImGui::BeginTable("PlayerList", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_ScrollY,
                        ImVec2(0, sub_table_height))) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableHeadersRow();
                    for (auto& p : se.info.players) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImVec4 team_col;
                        switch (p.team) {
                            case 0:  team_col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break;
                            case 1:  team_col = ImVec4(0.4f, 0.5f, 1.0f, 1.0f); break;
                            case 2:  team_col = ImVec4(1.0f, 1.0f, 0.3f, 1.0f); break;
                            default: team_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); break;
                        }
                        std::string plain_name = strip_ut_colors(p.name);
                        ImGui::TextColored(team_col, "%s", plain_name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%d", p.score);
                    }
                    ImGui::EndTable();
                }

                // Variables
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("Server Variables:");
                float var_table_height = ImGui::GetContentRegionAvail().y;
                if (ImGui::BeginTable("VarList", 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_ScrollY,
                        ImVec2(0, var_table_height))) {
                    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    for (auto& [k, v] : se.info.variables) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(k.c_str());
                        ImGui::TableSetColumnIndex(1);
                        TextUT(v);
                    }
                    ImGui::EndTable();
                }

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
}

#ifdef _WIN32
#include <windows.h>
static void hide_console() {
    HWND hw = GetConsoleWindow();
    if (hw) ShowWindow(hw, SW_HIDE);
}
#endif

static void print_help(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --help                Show this help message and exit\n"
        "  --query <servers>     Query servers and output JSON to stdout\n"
        "                        <servers> is a comma-separated list of host:port\n"
        "                        If port is omitted, 7777 is assumed\n"
        "  --file <path>         Write JSON output to a file instead of stdout\n"
        "                        (used with --query)\n"
        "\n"
        "Examples:\n"
        "  %s --query 192.168.1.1:7777,10.0.0.1,example.com:7778\n"
        "  %s --query myserver.com\n"
        "  %s --query myserver.com --file results.json\n"
        "\n"
        "If no options are given, the GUI server browser is launched.\n",
        prog, prog, prog, prog);
}

static std::string strip_colors(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (static_cast<unsigned char>(s[i]) == 0x1B && i + 3 < s.size()) {
            i += 3;
        } else {
            result.push_back(s[i]);
        }
    }
    return result;
}

static int run_query(const char* server_list, const char* output_file) {
    query_init();

    // Parse comma-separated server list
    std::vector<std::pair<std::string, uint16_t>> targets;
    std::string input(server_list);
    size_t pos = 0;
    while (pos < input.size()) {
        size_t comma = input.find(',', pos);
        if (comma == std::string::npos) comma = input.size();
        std::string token = input.substr(pos, comma - pos);
        pos = comma + 1;

        if (token.empty()) continue;

        std::string host;
        uint16_t port = 7777;
        size_t colon = token.rfind(':');
        if (colon != std::string::npos) {
            host = token.substr(0, colon);
            int p = std::atoi(token.substr(colon + 1).c_str());
            if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
        } else {
            host = token;
        }
        if (!host.empty())
            targets.push_back({host, port});
    }

    if (targets.empty()) {
        std::fprintf(stderr, "Error: no valid servers specified\n");
        query_cleanup();
        return 1;
    }

    // Query each server and build JSON array
    json results = json::array();
    for (auto& [host, port] : targets) {
        ServerInfo info = query_server(host, port);
        json server;
        server["address"] = info.address;
        server["port"] = info.port;
        server["name"] = strip_colors(info.name);
        server["map_name"] = strip_colors(info.map_name);
        server["map_title"] = strip_colors(info.map_title);
        server["gametype"] = strip_colors(info.gametype);
        server["num_players"] = info.num_players;
        server["max_players"] = info.max_players;
        server["ping"] = info.ping;
        server["online"] = info.online;
        server["status"] = info.status;

        json player_list = json::array();
        for (auto& p : info.players) {
            player_list.push_back({
                {"name", strip_colors(p.name)},
                {"score", p.score},
                {"team", p.team}
            });
        }
        server["players"] = player_list;

        json vars = json::object();
        for (auto& [k, v] : info.variables) {
            vars[strip_colors(k)] = strip_colors(v);
        }
        server["variables"] = vars;

        results.push_back(server);
    }

    std::string json_str = results.dump(2) + "\n";

    if (output_file) {
        FILE* fp = fopen(output_file, "w");
        if (!fp) {
            std::fprintf(stderr, "Error: could not open file '%s' for writing\n", output_file);
            query_cleanup();
            return 1;
        }
        std::fwrite(json_str.data(), 1, json_str.size(), fp);
        fclose(fp);
        std::fprintf(stderr, "Wrote %zu bytes to %s\n", json_str.size(), output_file);
    } else {
        std::fwrite(json_str.data(), 1, json_str.size(), stdout);
        fflush(stdout);
    }

    query_cleanup();
    return 0;
}

int main(int argc, char** argv) {
    // Handle CLI options before GUI init
    const char* query_arg = nullptr;
    const char* file_arg = nullptr;
    bool show_help = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            show_help = true;
        } else if (arg == "--query" && i + 1 < argc) {
            query_arg = argv[++i];
        } else if (arg == "--file" && i + 1 < argc) {
            file_arg = argv[++i];
        }
    }
    if (show_help) {
        print_help(argv[0]);
        return 0;
    }
    if (query_arg) {
        return run_query(query_arg, file_arg);
    }
    if (file_arg) {
        std::fprintf(stderr, "Error: --file requires --query\n");
        print_help(argv[0]);
        return 1;
    }

    // No CLI args — launch GUI mode
#ifdef _WIN32
    hide_console();
#endif

    query_init();
    SDL_SetMainReady();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("utquery - UT2004 Server Browser",
                                          1024, 700, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    App app;
    app.load_servers(CONFIG_PATH);
    app.load_cdkey("cdkey");

    char ip_buf[64] = "";
    int port_val = 7777;
    bool running = true;

    // Internet tab state
    static int gametype_idx = 0;
    struct GametypeEntry { const char* label; const char* classname; };
    static GametypeEntry gametypes[] = {
        { "All",              "" },
        { "Deathmatch",       "xDeathMatch" },
        { "Team Deathmatch",  "xTeamGame" },
        { "Capture the Flag", "xCTFGame" },
        { "Bombing Run",      "xBombingRun" },
        { "Double Domination","xDoubleDom" },
        { "Onslaught",        "ONSOnslaughtGame" },
        { "Assault",          "ASGameInfo" },
        { "Invasion",         "Invasion" },
        { "Mutant",           "xMutantGame" },
        { "Last Man Standing","xLastManStandingGame" },
    };
    static const int gametype_count = sizeof(gametypes) / sizeof(gametypes[0]);
    static float fav_detail_height = 250.0f;
    static float inet_detail_height = 250.0f;
    static bool fav_auto_refresh = false;
    static float fav_refresh_interval = 10.0f;
    static bool inet_auto_refresh = false;
    static float inet_refresh_interval = 10.0f;
    static auto last_fav_refresh = std::chrono::steady_clock::now();
    static auto last_inet_refresh = std::chrono::steady_clock::now();
    static bool fav_all_auto_refresh = false;
    static float fav_all_refresh_interval = 30.0f;
    static auto last_fav_all_refresh = std::chrono::steady_clock::now();
    static const char* font_size_labels[] = { "Small", "Normal", "Large", "Extra Large" };
    static const float font_size_scales[] = { 0.85f, 1.0f, 1.25f, 1.5f };
    io.FontGlobalScale = font_size_scales[app.font_size_idx];

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        app.poll_results();

        // Auto-refresh timers
        auto now = std::chrono::steady_clock::now();
        if (fav_auto_refresh && app.selected >= 0) {
            float elapsed = std::chrono::duration<float>(now - last_fav_refresh).count();
            if (elapsed >= fav_refresh_interval) {
                app.refresh_one(app.selected);
                last_fav_refresh = now;
            }
        }
        if (fav_all_auto_refresh && !app.servers.empty()) {
            float elapsed = std::chrono::duration<float>(now - last_fav_all_refresh).count();
            if (elapsed >= fav_all_refresh_interval) {
                app.refresh_all();
                last_fav_all_refresh = now;
            }
        }
        if (inet_auto_refresh && app.internet_selected >= 0) {
            float elapsed = std::chrono::duration<float>(now - last_inet_refresh).count();
            if (elapsed >= inet_refresh_interval) {
                app.refresh_internet_one(app.internet_selected);
                last_inet_refresh = now;
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Main window fills the viewport
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Server Browser", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        float combo_width = 120.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - combo_width - 120.0f);
        ImGui::Text("Font Size:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(combo_width);
        if (ImGui::Combo("##FontSize", &app.font_size_idx, font_size_labels, 4)) {
            io.FontGlobalScale = font_size_scales[app.font_size_idx];
        }

        if (ImGui::BeginTabBar("MainTabs")) {
            // ---- Favorites Tab ----
            if (ImGui::BeginTabItem("Favorites")) {
                // Top bar: add server + refresh + save
                ImGui::SetNextItemWidth(200);
                ImGui::InputText("IP", ip_buf, sizeof(ip_buf));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);
                ImGui::InputInt("Port", &port_val, 0);
                ImGui::SameLine();
                if (ImGui::Button("Add")) {
                    if (ip_buf[0] != '\0' && port_val > 0 && port_val < 65536) {
                        app.add_server(ip_buf, static_cast<uint16_t>(port_val));
                        ip_buf[0] = '\0';
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Refresh All")) {
                    app.refresh_all();
                }
                ImGui::SameLine();
                if (ImGui::Button("Save")) {
                    app.save_servers(CONFIG_PATH);
                }
                ImGui::SameLine(0, 20);
                ImGui::Checkbox("Auto Refresh All", &fav_all_auto_refresh);
                ImGui::SameLine();
                if (!fav_all_auto_refresh) ImGui::BeginDisabled();
                ImGui::SetNextItemWidth(150);
                ImGui::SliderFloat("##FavAllRefresh", &fav_all_refresh_interval, 10.0f, 120.0f, "%.0f s");
                if (!fav_all_auto_refresh) ImGui::EndDisabled();

                ImGui::Separator();

                int prev_fav_sel = app.selected;
                draw_server_list(app.servers, app.selected,
                    "FavServers", "FavServerList", "FavDetails", "##favsplit",
                    io, fav_detail_height, true,
                    fav_auto_refresh, fav_refresh_interval);
                if (app.selected >= 0 && app.selected != prev_fav_sel) {
                    app.refresh_one(app.selected);
                    last_fav_refresh = std::chrono::steady_clock::now();
                }

                ImGui::EndTabItem();
            }

            // ---- Internet Tab ----
            if (ImGui::BeginTabItem("Internet")) {
                // Top bar: master server + gametype dropdown + query button
                ImGui::SetNextItemWidth(250);
                if (!app.master_servers.empty()) {
                    if (app.master_selected < 0 || app.master_selected >= static_cast<int>(app.master_servers.size()))
                        app.master_selected = 0;
                    const char* ms_preview = app.master_servers[app.master_selected].host.c_str();
                    if (ImGui::BeginCombo("Master", ms_preview)) {
                        for (int n = 0; n < static_cast<int>(app.master_servers.size()); ++n) {
                            bool is_selected = (app.master_selected == n);
                            if (ImGui::Selectable(app.master_servers[n].host.c_str(), is_selected))
                                app.master_selected = n;
                            if (is_selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200);
                if (ImGui::BeginCombo("Gametype", gametypes[gametype_idx].label)) {
                    for (int n = 0; n < gametype_count; ++n) {
                        bool is_selected = (gametype_idx == n);
                        if (ImGui::Selectable(gametypes[n].label, is_selected))
                            gametype_idx = n;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                bool querying_master = app.master_querying();
                if (querying_master) ImGui::BeginDisabled();
                if (ImGui::Button("Query")) {
                    auto& ms = app.master_servers[app.master_selected];
                    app.query_master(ms.host, ms.port,
                                     gametypes[gametype_idx].classname);
                }
                if (querying_master) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Refresh All##inet")) {
                    app.refresh_internet_all();
                }
                ImGui::SameLine();
                if (!app.master_status.empty()) {
                    ImGui::TextUnformatted(app.master_status.c_str());
                }

                ImGui::Separator();

                int prev_inet_sel = app.internet_selected;
                int add_fav_idx = -1;
                draw_server_list(app.internet_servers, app.internet_selected,
                    "InetServers", "InetServerList", "InetDetails", "##inetsplit",
                    io, inet_detail_height, false,
                    inet_auto_refresh, inet_refresh_interval, &add_fav_idx);
                if (add_fav_idx >= 0 && add_fav_idx < static_cast<int>(app.internet_servers.size())) {
                    auto& se = app.internet_servers[add_fav_idx];
                    app.add_server(se.info.address, se.info.port);
                    app.save_servers(CONFIG_PATH);
                }
                // Auto-refresh when a new server is selected
                if (app.internet_selected >= 0 && app.internet_selected != prev_inet_sel) {
                    app.refresh_internet_one(app.internet_selected);
                    last_inet_refresh = std::chrono::steady_clock::now();
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    app.save_servers(CONFIG_PATH);

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    query_cleanup();

    return 0;
}
