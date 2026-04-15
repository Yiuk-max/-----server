cmake_minimum_required(VERSION 3.10)
project(server)
set(CMAKE_CXX_STANDARD 17)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC_LIST)
# target(server
#     PRIVATE
#         main.cpp
    
# )
add_executable(server ${SRC_LIST})