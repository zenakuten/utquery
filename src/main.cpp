#include "app.h"
#include "query.h"
#include "utcolor.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

static const char* CONFIG_PATH = "servers.json";

// Render the server table + detail panel for a given server list.
// table_id must be unique per tab. Returns remove_idx or -1.
static void draw_server_list(
    std::vector<ServerEntry>& servers, int& selected,
    const char* table_id, const char* child_id, const char* detail_id,
    const char* splitter_id, ImGuiIO& io,
    float& detail_height, bool show_remove)
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

int main(int, char**) {
    query_init();

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

                ImGui::Separator();

                int prev_fav_sel = app.selected;
                draw_server_list(app.servers, app.selected,
                    "FavServers", "FavServerList", "FavDetails", "##favsplit",
                    io, fav_detail_height, true);
                if (app.selected >= 0 && app.selected != prev_fav_sel) {
                    app.refresh_one(app.selected);
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
                draw_server_list(app.internet_servers, app.internet_selected,
                    "InetServers", "InetServerList", "InetDetails", "##inetsplit",
                    io, inet_detail_height, false);
                // Auto-refresh when a new server is selected
                if (app.internet_selected >= 0 && app.internet_selected != prev_inet_sel) {
                    app.refresh_internet_one(app.internet_selected);
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
