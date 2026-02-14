cmake_minimum_required(VERSION 3.25)
project(utquery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(utquery
    src/main.cpp
    src/query.cpp
    src/app.cpp
)

if(WIN32)
    set_target_properties(utquery PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(utquery PRIVATE
    SDL3::SDL3
    imgui::imgui
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(utquery PRIVATE ws2_32)
endif()
