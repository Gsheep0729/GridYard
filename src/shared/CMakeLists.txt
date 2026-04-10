# shared/ —— 客户端与未来 V2.0 服务端共用的静态库 gy_shared
# 内含 TLV 协议常量、共用数据类型、帧编解码

add_library(gy_shared STATIC
    protocol.h
    data_types.h
    frame_codec.h
    frame_codec.cpp
)

target_include_directories(gy_shared PUBLIC .)
target_compile_features(gy_shared    PUBLIC cxx_std_23)
target_link_libraries(gy_shared      PUBLIC Qt6::Core Qt6::Network)
