# storage/ —— SQLite 数据管理层，编译为独立静态库 gy_storage。
# 数据库代码不放进 appGridYard 的 source_group，而是作为单独 target 展示在 client/storage；
# 这样可以隔离 Qt Sql 依赖，也方便存储层单元测试直接链接复用。
set(GRIDYARD_STORAGE_SOURCES
    local_data_broker.h
    local_data_broker.cpp
    migration_runner.h
    migration_runner.cpp
    sqlite_database_broker.h
    sqlite_database_broker.cpp
    sqlite_device_repository.h
    sqlite_device_repository.cpp
    sqlite_message_repository.h
    sqlite_message_repository.cpp
    sqlite_transfer_history_repository.h
    sqlite_transfer_history_repository.cpp
    database_worker.h
    database_worker.cpp
)

add_library(gy_storage STATIC
    ${GRIDYARD_STORAGE_SOURCES}
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "client/storage" FILES ${GRIDYARD_STORAGE_SOURCES})

target_include_directories(gy_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gy_storage PUBLIC gy_domain PRIVATE Qt6::Core Qt6::Sql)
target_compile_features(gy_storage PUBLIC cxx_std_23)
set_property(TARGET gy_storage PROPERTY FOLDER "client/storage")
