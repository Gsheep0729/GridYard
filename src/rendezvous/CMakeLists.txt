# rendezvous/ —— 协调节点服务
# 独立可执行程序，提供设备注册和候选端点拉取服务

cmake_minimum_required(VERSION 4.2.3)

project(gridyard_rendezvous VERSION 7.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

add_executable(gridyard_rendezvous WIN32
    main.cpp
    online_registry.h
    online_registry.cpp
    rendezvous_protocol.h
    rendezvous_protocol.cpp
    rendezvous_server.h
    rendezvous_server.cpp
    relay_server.h
    relay_server.cpp
)

target_link_libraries(gridyard_rendezvous PRIVATE
    Qt6::Core
    Qt6::Network
)

target_include_directories(gridyard_rendezvous PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 安装
set(CMAKE_INSTALL_PREFIX "/opt/${PROJECT_NAME}" CACHE PATH "Install path prefix" FORCE)
install(TARGETS gridyard_rendezvous
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)