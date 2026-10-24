# storage/ —— 纯 C++23 存储核心
# 直接使用 vendored SQLite3 C API（sqlite3_vendor），严禁链接任何 Qt target：
# 本目录在 check_core_isolation.py --strict 下必须零违规。
# 原 Qt Sql 影子对照实现（gy_storage）已随 Phase 5b 退役，SQL 语义由本目录
# 独立维护，调用方为客户端 broker 与存储测试。
add_library(gy_storage_core STATIC
    sqlite_conn.h
    sqlite_conn.cpp
    sqlite_stmt.h
    sqlite_stmt.cpp
    sqlite_tx.h
    sqlite_tx.cpp
    iso_time.h
    iso_time.cpp
    storage_records.h
    migration_core.h
    migration_core.cpp
    database_core.h
    database_core.cpp
    device_repository_core.h
    device_repository_core.cpp
    message_repository_core.h
    message_repository_core.cpp
    transfer_repository_core.h
    transfer_repository_core.cpp
)

target_compile_features(gy_storage_core PUBLIC cxx_std_23)
# 以 src/ 为公共 include 根，消费方统一使用 "core/storage/..." 前缀包含
target_include_directories(gy_storage_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
# 头文件公开包含 <sqlite3.h>，连接与 include 目录随目标传递
target_link_libraries(gy_storage_core PUBLIC sqlite3_vendor)

# 顶层 qt_standard_project_setup 会全局打开 AUTOMOC/AUTOUIC/AUTORCC，
# 纯 C++ 目标显式关闭，确保任何 Qt 代码生成工具不进入核心层
set_target_properties(gy_storage_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set_property(TARGET gy_storage_core PROPERTY FOLDER "core")

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "core/storage" FILES
    sqlite_conn.h
    sqlite_conn.cpp
    sqlite_stmt.h
    sqlite_stmt.cpp
    sqlite_tx.h
    sqlite_tx.cpp
    iso_time.h
    iso_time.cpp
    storage_records.h
    migration_core.h
    migration_core.cpp
    database_core.h
    database_core.cpp
    device_repository_core.h
    device_repository_core.cpp
    message_repository_core.h
    message_repository_core.cpp
    transfer_repository_core.h
    transfer_repository_core.cpp
)
