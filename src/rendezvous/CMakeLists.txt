# rendezvous/ —— 协调节点服务
# 独立可执行程序，提供设备注册、候选端点拉取、中继邀请信令和流式中继转发

cmake_minimum_required(VERSION 4.2.3)

project(gridyard_rendezvous VERSION 7.26.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

# 顶层构建由 qt_standard_project_setup 打开 AUTOMOC，独立配置时显式开启
set(CMAKE_AUTOMOC ON)

# 独立配置时引入共享原生套接字层；顶层构建中 gy_net 已由 shared 提供
if(NOT TARGET gy_net)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../shared/net
                     ${CMAKE_CURRENT_BINARY_DIR}/shared_net)
endif()
if(NOT TARGET gy_protocol_contracts)
    add_library(gy_protocol_contracts INTERFACE)
    target_include_directories(gy_protocol_contracts INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
endif()

# 服务端实现抽成静态库，供可执行程序与 relay 链路测试共用
add_library(gy_rendezvous STATIC
    online_registry.h
    online_registry.cpp
    rendezvous_limits.h
    line_session.h
    line_session.cpp
    rendezvous_protocol.h
    rendezvous_protocol.cpp
    rendezvous_server.h
    rendezvous_server.cpp
    relay_server.h
    relay_server.cpp
)

target_link_libraries(gy_rendezvous PUBLIC Qt6::Core gy_net gy_protocol_contracts)
target_include_directories(gy_rendezvous PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gridyard_rendezvous WIN32 main.cpp)
target_link_libraries(gridyard_rendezvous PRIVATE gy_rendezvous)

# 安装
set(CMAKE_INSTALL_PREFIX "/opt/${PROJECT_NAME}" CACHE PATH "Install path prefix" FORCE)
install(TARGETS gridyard_rendezvous
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
