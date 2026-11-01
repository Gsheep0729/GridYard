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
)

add_library(gy_net_core STATIC ${GY_NET_CORE_SOURCES})

target_include_directories(gy_net_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gy_net_core PUBLIC Threads::Threads gy_asio)

# Windows：WinSock2 / IP Helper 系统库。事件泵与套接字核心的 Windows 后端已按
# 同文件 #ifdef _WIN32 分支落位（net_reactor / tcp_socket_core / tcp_server_core
# 三文件，不新建 win_* 实现文件，原 target_sources 占位方案随之取消）；ws2_32
# 供三文件的 Winsock 调用（WSAStartup/WSAEventSelect/closesocket 等）链接，
# iphlpapi 为客户端网络适配层移植（GetAdaptersAddresses，报告 N8）的预留，当前
# 尚无消费点。见 doc/plan/平台扩展/Windows可行性报告.local.md §6.2 批次 1
if(WIN32)
    target_link_libraries(gy_net_core PRIVATE ws2_32 iphlpapi)
endif()

target_compile_features(gy_net_core PUBLIC cxx_std_23)
# 纯 C++ 目标显式关闭 Qt 代码生成工具
set_target_properties(gy_net_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set_property(TARGET gy_net_core PROPERTY FOLDER "shared")
