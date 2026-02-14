#include "app.h"
#include "query.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <cstdio>
#include <cstring>
#include <string>

static const char* CONFIG_PATH = "servers.json";

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

    char ip_buf[64] = "";
    int port_val = 7777;
    bool running = true;

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

        // Server table
        float detail_height = (app.selected >= 0) ? 250.0f : 0.0f;
        float table_height = ImGui::GetContentRegionAvail().y - detail_height;

        if (ImGui::BeginChild("ServerList", ImVec2(0, table_height))) {
            if (ImGui::BeginTable("Servers", 8,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_Sortable)) {

                ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Gametype", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableHeadersRow();

                int remove_idx = -1;
                for (int i = 0; i < static_cast<int>(app.servers.size()); ++i) {
                    auto& se = app.servers[i];
                    ImGui::TableNextRow();
                    ImGui::PushID(i);

                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::SmallButton("X")) {
                        remove_idx = i;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("R")) {
                        app.refresh_one(i);
                    }

                    // Name column — clickable to select
                    ImGui::TableSetColumnIndex(1);
                    bool is_selected = (app.selected == i);
                    std::string label = se.info.name.empty()
                        ? (se.info.address + ":" + std::to_string(se.info.port))
                        : se.info.name;
                    if (ImGui::Selectable(label.c_str(), is_selected,
                                          ImGuiSelectableFlags_SpanAllColumns)) {
                        app.selected = is_selected ? -1 : i;
                    }

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(se.info.map_name.c_str());

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(se.info.gametype.c_str());

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", se.info.num_players);

                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%d", se.info.max_players);

                    ImGui::TableSetColumnIndex(6);
                    if (se.info.online)
                        ImGui::Text("%d", se.info.ping);
                    else
                        ImGui::TextUnformatted("-");

                    ImGui::TableSetColumnIndex(7);
                    ImGui::TextUnformatted(se.info.status.c_str());

                    ImGui::PopID();
                }
                ImGui::EndTable();

                if (remove_idx >= 0) {
                    app.remove_server(remove_idx);
                }
            }
        }
        ImGui::EndChild();

        // Detail panel
        if (app.selected >= 0 && app.selected < static_cast<int>(app.servers.size())) {
            ImGui::Separator();
            auto& se = app.servers[app.selected];

            if (ImGui::BeginChild("Details", ImVec2(0, 0))) {
                ImGui::Text("Server: %s:%d", se.info.address.c_str(), se.info.port);
                ImGui::SameLine(0, 20);
                ImGui::Text("Map: %s (%s)", se.info.map_name.c_str(), se.info.map_title.c_str());
                ImGui::SameLine(0, 20);
                ImGui::Text("Gametype: %s", se.info.gametype.c_str());

                // Two columns: players on left, variables on right
                if (ImGui::BeginTable("DetailColumns", 2, ImGuiTableFlags_Resizable)) {
                    ImGui::TableNextRow();

                    // Players
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("Players (%d/%d):", se.info.num_players, se.info.max_players);
                    if (ImGui::BeginTable("PlayerList", 2,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY,
                            ImVec2(0, 180))) {
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                        ImGui::TableHeadersRow();
                        for (auto& p : se.info.players) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(p.name.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%d", p.score);
                        }
                        ImGui::EndTable();
                    }

                    // Variables
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("Server Variables:");
                    if (ImGui::BeginTable("VarList", 2,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_ScrollY,
                            ImVec2(0, 180))) {
                        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();
                        for (auto& [k, v] : se.info.variables) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(k.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextUnformatted(v.c_str());
                        }
                        ImGui::EndTable();
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
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
