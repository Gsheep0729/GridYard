# shared/net —— 原生套接字原语层
# gy_net_core：纯 C++23 事件泵与套接字核心（零 Qt），服务端、客户端与测试
# 直接链接；Qt 信号适配层（原 gy_net 六对文件）已归位 src/client/network/，
# 随客户端目标与相关测试一起编译。

find_package(Threads REQUIRED)

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
