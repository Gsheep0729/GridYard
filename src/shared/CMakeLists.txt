# shared/ —— 客户端与未来 V2.0 服务端共用的协议常量与数据类型头文件集
# 编解码实现已由 src/core 的纯 C++ 双生版承担（客户端经 codec_bridge 桥接），
# 本目录只剩头文件：TLV 协议常量与共用数据类型， INTERFACE 目标供
# appGridYard、storage 和测试目标复用，不再直接链接 Qt6::Core。
set(GRIDYARD_SHARED_HEADERS
    protocol.h
    data_types.h
)

# 原生套接字原语层，客户端、协调节点与测试共用
add_subdirectory(net)

# 协调/中继行协议常量头：header-only 接口目标，服务端与客户端显式链接，
# 字段名单一来源（ADR-001 的最小落地，全面迁移留待 Phase 2）
add_library(gy_protocol_contracts INTERFACE)
target_include_directories(gy_protocol_contracts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(gy_shared STATIC ${GRIDYARD_SHARED_HEADERS})
# 纯头目标：AUTOMOC 仍会为头文件中的 Q_GADGET 生成并编译 moc 载体
set_property(TARGET gy_shared PROPERTY LINKER_LANGUAGE CXX)
target_include_directories(gy_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(gy_shared PUBLIC cxx_std_23)
target_link_libraries(gy_shared PUBLIC gy_net gy_protocol_contracts)
set_property(TARGET gy_shared PROPERTY FOLDER "shared")
