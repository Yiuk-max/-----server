cmake_minimum_required(VERSION 3.10)
project(server)
set(CMAKE_CXX_STANDARD 17)

# 分别扫描每个子目录
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo            REPO_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo_interface  REPO_INTERFACE_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/db/mysql           MYSQL_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/net_core           NET_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/logic              LOGIC_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/utils              UTILS)

find_package(nlohmann_json REQUIRED)

include_directories(/usr/include/mysql-cppconn)
# MySQL Connector/C++ 的 JDBC 头在 jdbc 子目录（mysql_driver.h / cppconn/...）
include_directories(/usr/include/mysql-cppconn/jdbc)

add_executable(server src/main.cpp ${NET_SRC} ${LOGIC_SRC} ${UTILS} ${REPO_SRC} ${REPO_INTERFACE_SRC} ${MYSQL_SRC})

target_include_directories(server PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net_common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo_interface
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db/mysql
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(server PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(server PRIVATE mysqlcppconn)

# ---------------------------------------------------------------------------
# 可选单元测试：不依赖 MySQL，默认关闭。
#   cmake -S . -B build -DSERVER_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build --output-on-failure
# ---------------------------------------------------------------------------
option(SERVER_BUILD_TESTS "Build M0 protocol/session unit tests" OFF)
if(SERVER_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)

    set(SERVER_TEST_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net_common
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo
        ${CMAKE_CURRENT_SOURCE_DIR}/src/db/repo_interface
        ${CMAKE_CURRENT_SOURCE_DIR}/src/db/mysql
        ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    add_executable(test_tcp_protocol
        tests/test_tcp_protocol.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/net_core/receiver_sender.cpp)
    target_include_directories(test_tcp_protocol PRIVATE ${SERVER_TEST_INCLUDES})
    target_link_libraries(test_tcp_protocol PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
    add_test(NAME tcp_protocol COMMAND test_tcp_protocol)

    # 在线表测试需要 client_session 完整实现，因此复用除 main.cpp 外的全部源文件。
    add_executable(test_session_manager
        tests/test_session_manager.cpp
        ${NET_SRC} ${LOGIC_SRC} ${UTILS} ${REPO_SRC} ${REPO_INTERFACE_SRC} ${MYSQL_SRC})
    target_include_directories(test_session_manager PRIVATE ${SERVER_TEST_INCLUDES})
    target_link_libraries(test_session_manager PRIVATE nlohmann_json::nlohmann_json mysqlcppconn Threads::Threads)
    add_test(NAME session_manager COMMAND test_session_manager)
endif()