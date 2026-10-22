# shared/net —— 原生套接字原语层
# 只依赖 QtCore 的事件分发与容器，网络收发全部由系统调用完成。

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
target_link_libraries(gy_net PUBLIC Qt6::Core)
target_compile_features(gy_net PUBLIC cxx_std_23)
# 独立项目引用本目录时也需要生成 Q_OBJECT 元对象
set_target_properties(gy_net PROPERTIES AUTOMOC ON)
set_property(TARGET gy_net PROPERTY FOLDER "shared")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "shared/net" FILES ${GY_NET_SOURCES})
