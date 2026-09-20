cmake_minimum_required(VERSION 3.25)
project(utquery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)

set(NLOHMANN_JSON_INCLUDE_DIR "" CACHE PATH "Path containing the nlohmann headers")
if(NLOHMANN_JSON_INCLUDE_DIR)
    add_library(nlohmann_json_vendor INTERFACE)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_vendor)
    target_include_directories(nlohmann_json_vendor INTERFACE
        "${NLOHMANN_JSON_INCLUDE_DIR}"
    )
else()
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

set(IMGUI_SOURCE_DIR "" CACHE PATH "Path to an ImGui source tree")
if(IMGUI_SOURCE_DIR)
    add_library(imgui_vendor STATIC
        "${IMGUI_SOURCE_DIR}/imgui.cpp"
        "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
        "${IMGUI_SOURCE_DIR}/imgui_tables.cpp"
        "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
        "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
        "${IMGUI_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
    )
    add_library(imgui::imgui ALIAS imgui_vendor)
    target_include_directories(imgui_vendor PUBLIC
        "${IMGUI_SOURCE_DIR}"
        "${IMGUI_SOURCE_DIR}/backends"
    )
    target_link_libraries(imgui_vendor PUBLIC SDL3::SDL3)
else()
    find_package(imgui CONFIG REQUIRED)
endif()

add_executable(utquery
    src/main.cpp
    src/query.cpp
    src/app.cpp
    src/master.cpp
)

if(WIN32)
    # Console subsystem so CLI mode (--query, --help) works with pipes and redirects.
    # The console window is hidden at startup when running in GUI mode.
    set_target_properties(utquery PROPERTIES WIN32_EXECUTABLE FALSE)
endif()

target_link_libraries(utquery PRIVATE
    SDL3::SDL3
    imgui::imgui
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(utquery PRIVATE ws2_32)
endif()
