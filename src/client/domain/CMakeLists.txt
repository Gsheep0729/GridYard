add_library(gy_domain INTERFACE)
target_include_directories(gy_domain INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gy_domain INTERFACE Qt6::Core gy_shared)
target_compile_features(gy_domain INTERFACE cxx_std_23)
