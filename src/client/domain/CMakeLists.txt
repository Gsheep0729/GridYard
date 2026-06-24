# domain/ —— 本地历史相关的领域值类型与 Repository 端口。
# 目前都是头文件接口，因此使用 INTERFACE target，只向上游传播 include 路径和依赖。
add_library(gy_domain INTERFACE)
target_sources(gy_domain INTERFACE
    history_records.h
    history_repositories.h
)
target_include_directories(gy_domain INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gy_domain INTERFACE Qt6::Core gy_shared)
target_compile_features(gy_domain INTERFACE cxx_std_23)
set_property(TARGET gy_domain PROPERTY FOLDER "client/domain")
source_group("client/domain" FILES history_records.h history_repositories.h)
