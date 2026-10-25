# rendezvous/ —— 协调节点服务
# 独立可执行程序，提供设备注册、候选端点拉取、中继邀请信令和流式中继转发
# 纯 C++ 实现：事件泵与套接字核心来自 gy_net_core，JSON 用 vendored nlohmann，零 Qt
# 本目录只作为顶层工程（src/CMakeLists.txt）的子目录参与构建，不声明独立工程
# 独立 configure 因缺少顶层工程声明直接报错；单独构建服务端用 --target gridyard_rendezvous

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 独立配置时引入共享原生套接字层与第三方依赖；顶层构建中已由上级提供
if(NOT TARGET gy_net_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../shared/net
                     ${CMAKE_CURRENT_BINARY_DIR}/shared_net)
endif()
if(NOT TARGET gy_protocol_contracts)
    add_library(gy_protocol_contracts INTERFACE)
    target_include_directories(gy_protocol_contracts INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared)
endif()
if(NOT TARGET nlohmann_json)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../third_party
                     ${CMAKE_CURRENT_BINARY_DIR}/third_party)
endif()

# 服务端实现抽成静态库，供可执行程序与 relay 链路测试共用
add_library(gy_rendezvous STATIC
    online_registry.h
    online_registry.cpp
    rendezvous_limits.h
    rendezvous_log.h
    rendezvous_time.h
    line_session.h
    line_session.cpp
    rendezvous_protocol.h
    rendezvous_protocol.cpp
    rendezvous_server.h
    rendezvous_server.cpp
    relay_server.h
    relay_server.cpp
)

target_link_libraries(gy_rendezvous PUBLIC gy_net_core gy_protocol_contracts nlohmann_json)
target_include_directories(gy_rendezvous PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(gy_rendezvous PUBLIC cxx_std_23)
# 纯 C++ 目标显式关闭 Qt 代码生成工具
set_target_properties(gy_rendezvous PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set_property(TARGET gy_rendezvous PROPERTY FOLDER "rendezvous")

add_executable(gridyard_rendezvous WIN32 main.cpp)
target_link_libraries(gridyard_rendezvous PRIVATE gy_rendezvous)
# 可执行目标同样不经过任何 Qt 代码生成工具
set_target_properties(gridyard_rendezvous PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# 安装（前缀遵循 CMake 标准语义，可由 -DCMAKE_INSTALL_PREFIX / cmake --install --prefix 覆盖）
install(TARGETS gridyard_rendezvous
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
