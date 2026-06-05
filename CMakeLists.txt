cmake_minimum_required(VERSION 3.10)
project(server)
set(CMAKE_CXX_STANDARD 17)

# 分别扫描每个子目录
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/net_core NET_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/logic    LOGIC_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/utils UTILS)

find_package(nlohmann_json REQUIRED)

add_executable(server src/main.cpp ${NET_SRC} ${LOGIC_SRC} ${UTILS})
target_include_directories(server PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

target_link_libraries(server PRIVATE nlohmann_json::nlohmann_json)