# domain/ —— 本地历史相关的领域值类型与 Repository 端口。
# 目前都是头文件接口，因此使用 INTERFACE target；头文件作为 PRIVATE sources
# 只挂在 gy_domain 自己的项目树下，避免通过 INTERFACE_SOURCES 传播到消费目标。
add_library(gy_domain INTERFACE)
target_sources(gy_domain PRIVATE
    history_records.h
    history_repositories.h
)
target_include_directories(gy_domain INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gy_domain INTERFACE Qt6::Core gy_shared)
target_compile_features(gy_domain INTERFACE cxx_std_23)
set_property(TARGET gy_domain PROPERTY FOLDER "client/domain")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "client/domain"
    FILES history_records.h history_repositories.h
)
