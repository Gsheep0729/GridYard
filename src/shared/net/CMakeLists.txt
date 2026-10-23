# shared/net —— 原生套接字原语层
# gy_net_core：纯 C++23 事件泵与套接字核心（零 Qt，服务端直接链接）；
# gy_net：Qt 信号桥接层，仅保留 QObject/QString/QStringList 接口适配，
# 供客户端与既有测试过渡使用。

find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

# ---- 纯 C++ 核心目标（严禁链接任何 Qt target）----
set(GY_NET_CORE_SOURCES
    core/net_reactor.h
    core/net_reactor.cpp
    core/tcp_socket_core.h
    core/tcp_socket_core.cpp
    core/tcp_server_core.h
    core/tcp_server_core.cpp
    core/udp_socket_core.h
    core/udp_socket_core.cpp
)

add_library(gy_net_core STATIC ${GY_NET_CORE_SOURCES})

target_include_directories(gy_net_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gy_net_core PUBLIC Threads::Threads)
target_compile_features(gy_net_core PUBLIC cxx_std_23)
# 纯 C++ 目标显式关闭 Qt 代码生成工具
set_target_properties(gy_net_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set_property(TARGET gy_net_core PROPERTY FOLDER "shared")

# ---- Qt 桥接层（换心后的接口适配）----
set(GY_NET_SOURCES
    posix_net_types.h
    posix_net_error.h
    posix_net_error.cpp
    ip_address.h
    ip_address.cpp
    net_interfaces.h
    net_interfaces.cpp
    posix_tcp_socket.h
    posix_tcp_socket.cpp
    posix_tcp_server.h
    posix_tcp_server.cpp
    posix_udp_socket.h
    posix_udp_socket.cpp
)

add_library(gy_net STATIC ${GY_NET_SOURCES})

target_include_directories(gy_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gy_net PUBLIC gy_net_core Qt6::Core Threads::Threads)
target_compile_features(gy_net PUBLIC cxx_std_23)
# 独立项目引用本目录时也需要生成 Q_OBJECT 元对象
set_target_properties(gy_net PROPERTIES AUTOMOC ON)
set_property(TARGET gy_net PROPERTY FOLDER "shared")
