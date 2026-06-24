add_library(gy_storage STATIC
    migration_runner.h
    migration_runner.cpp
    sqlite_database_proxy.h
    sqlite_database_proxy.cpp
    sqlite_device_proxy.h
    sqlite_device_proxy.cpp
    database_worker.h
    database_worker.cpp
)
target_include_directories(gy_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gy_storage PUBLIC gy_domain PRIVATE Qt6::Core Qt6::Sql)
target_compile_features(gy_storage PUBLIC cxx_std_23)
