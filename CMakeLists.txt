cmake_minimum_required(VERSION 3.10)
project(server)
set(CMAKE_CXX_STANDARD 17)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC_LIST)

find_package(nlohmann_json REQUIRED)

add_executable(server ${SRC_LIST})
target_link_libraries(server PRIVATE nlohmann_json::nlohmann_json)