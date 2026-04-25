cmake_minimum_required(VERSION 3.10)
project(server)
set(CMAKE_CXX_STANDARD 17)

add_executable(server
    main.cpp
    server.cpp
    session.cpp
)
# 线程支持：用于 main.cpp 里的 std::thread / 线程池实现。
find_package(Threads REQUIRED)
# Boost.System：boost::asio 的 error_code 和网络基础能力依赖该组件。
find_package(Boost REQUIRED COMPONENTS system)
target_link_libraries(server PRIVATE Threads::Threads)
target_link_libraries(server PRIVATE Boost::system)