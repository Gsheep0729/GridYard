# core/ —— 纯 C++23 核心库（重构 Phase 1 影子层）
# 严禁链接任何 Qt target：本目标在 check_core_isolation.py --strict 下必须零违规。
# 影子化策略：现有 Qt 实现保持原样，本目录提供并行纯 C++ 接口，
# Phase 4 桥接层下沉时统一切换调用点。
add_library(gy_core STATIC
    types/core_types.h
    codec/frame_codec_core.h
    codec/frame_codec_core.cpp
    codec/chat_message_core.h
    codec/chat_message_core.cpp
    utils/dir_hasher.h
    utils/dir_hasher.cpp
)

target_compile_features(gy_core PUBLIC cxx_std_23)
# 以 src/ 为公共 include 根，消费方统一使用 "core/..." 前缀包含
target_include_directories(gy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gy_core PRIVATE nlohmann_json picosha2)

# 顶层 qt_standard_project_setup 会全局打开 AUTOMOC/AUTOUIC/AUTORCC，
# 纯 C++ 目标显式关闭，确保任何 Qt 代码生成工具不进入核心层
set_target_properties(gy_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set_property(TARGET gy_core PROPERTY FOLDER "core")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "core" FILES
    types/core_types.h
    codec/frame_codec_core.h
    codec/frame_codec_core.cpp
    codec/chat_message_core.h
    codec/chat_message_core.cpp
    utils/dir_hasher.h
    utils/dir_hasher.cpp
)

# 纯 C++23 存储核心（Phase 3 影子层）：vendored SQLite3 C API 封装，
# 与本目录同样零 Qt；独立于 gy_core，避免把 sqlite 链接传染给全部核心消费者
add_subdirectory(storage)
