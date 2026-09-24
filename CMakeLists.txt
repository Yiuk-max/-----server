cmake_minimum_required(VERSION 3.10)
project(server)
set(CMAKE_CXX_STANDARD 17)

# 分别扫描每个子目录
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo            REPO_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo_interface  REPO_INTERFACE_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/db/mysql           MYSQL_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/net_core           NET_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/net_core_ws        WS_NET_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/logic              LOGIC_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/utils              UTILS)

find_package(nlohmann_json REQUIRED)

# WebSocket 网络层（Boost.Asio + Beast）。net_layer 是运行时选择，
# 因此同一二进制始终包含 WS 代码，Boost 为编译期依赖。
# CMake 3.30+ 已移除 FindBoost 模块，Boost 1.70+ 自带 BoostConfig.cmake，
# 显式走 config 模式以消除 CMP0167 开发警告。
find_package(Boost 1.74 REQUIRED COMPONENTS system CONFIG)
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

# ---------------------------------------------------------------------------
# Redis 缓存：redis-plus-plus（依赖 hiredis）。通过 FetchContent 拉取源码并静态链接，
# 避免要求系统预装 libhiredis / libredis++。
# ---------------------------------------------------------------------------
include(FetchContent)

# hiredis：redis-plus-plus 的底层 C 客户端，静态编译
set(BUILD_SHARED_LIBS OFF CACHE BOOL "build hiredis static" FORCE)
# redis++ 的 FindHiredis 约定 hiredis 源码位于 _deps/hiredis（以便 <hiredis/hiredis.h> 解析），
# 故显式指定 SOURCE_DIR，去掉默认的 -src 后缀。
FetchContent_Declare(hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG        v1.4.1
    GIT_SHALLOW    TRUE
    OVERRIDE_FIND_PACKAGE
    SOURCE_DIR     ${CMAKE_BINARY_DIR}/_deps/hiredis
)
FetchContent_MakeAvailable(hiredis)

# redis-plus-plus：现代 C++ 封装；只编静态库，不编测试
set(REDIS_PLUS_PLUS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(REDIS_PLUS_PLUS_BUILD_STATIC  ON  CACHE BOOL "" FORCE)
set(REDIS_PLUS_PLUS_BUILD_TEST    OFF CACHE BOOL "" FORCE)
FetchContent_Declare(redis-plus-plus
    GIT_REPOSITORY https://github.com/sewenew/redis-plus-plus.git
    GIT_TAG        1.3.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(redis-plus-plus)

# 生成 protobuf C++ 源文件（message.pb.h / message.pb.cc）
set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/proto/message.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

include_directories(/usr/include/mysql-cppconn)
# MySQL Connector/C++ 的 JDBC 头在 jdbc 子目录（mysql_driver.h / cppconn/...）
include_directories(/usr/include/mysql-cppconn/jdbc)

add_executable(server src/main.cpp ${NET_SRC} ${WS_NET_SRC} ${LOGIC_SRC} ${UTILS} ${REPO_SRC} ${REPO_INTERFACE_SRC} ${MYSQL_SRC} ${PROTO_SRCS})

target_include_directories(server PRIVATE 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net_common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core_ws
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo_interface
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db/mysql
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(server PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(server PRIVATE mysqlcppconn)
target_link_libraries(server PRIVATE Boost::system Threads::Threads)
target_link_libraries(server PRIVATE protobuf::libprotobuf)
target_link_libraries(server PRIVATE redis++::redis++_static)






# ---------------------------------------------------------------------------
# 可选单元测试：不依赖 MySQL，默认关闭。
#   cmake -S . -B build -DSERVER_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure
# ---------------------------------------------------------------------------
option(SERVER_BUILD_TESTS "Build M0/M1 protocol/session unit tests" OFF)
if(SERVER_BUILD_TESTS)
    enable_testing()

    set(SERVER_TEST_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net_common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core_ws
        ${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo
        ${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo_interface
        ${CMAKE_CURRENT_SOURCE_DIR}/src/db/mysql
        ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    add_executable(test_tcp_protocol
        tests/test_tcp_protocol.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core/receiver_sender.cpp
        ${PROTO_SRCS})
    target_include_directories(test_tcp_protocol PRIVATE ${SERVER_TEST_INCLUDES} ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(test_tcp_protocol PRIVATE nlohmann_json::nlohmann_json Threads::Threads protobuf::libprotobuf)
    add_test(NAME tcp_protocol COMMAND test_tcp_protocol)

    # 在线表测试需要 client_session 完整实现，因此复用除 main.cpp 外的全部源文件。
    add_executable(test_session_manager
        tests/test_session_manager.cpp
        ${NET_SRC} ${LOGIC_SRC} ${UTILS} ${REPO_SRC} ${REPO_INTERFACE_SRC} ${MYSQL_SRC} ${PROTO_SRCS})
    target_include_directories(test_session_manager PRIVATE ${SERVER_TEST_INCLUDES} ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(test_session_manager PRIVATE nlohmann_json::nlohmann_json mysqlcppconn Threads::Threads protobuf::libprotobuf redis++::redis++_static)
    add_test(NAME session_manager COMMAND test_session_manager)

    add_executable(test_ws_protocol
        tests/test_ws_protocol.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core_ws/ws_protocol.cpp)
    target_include_directories(test_ws_protocol PRIVATE ${SERVER_TEST_INCLUDES})
    target_link_libraries(test_ws_protocol PRIVATE nlohmann_json::nlohmann_json Threads::Threads protobuf::libprotobuf)
    add_test(NAME ws_protocol COMMAND test_ws_protocol)
endif()
