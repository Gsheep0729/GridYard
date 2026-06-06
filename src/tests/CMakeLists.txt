find_package(Qt6 REQUIRED COMPONENTS Test)

# FrameCodec 单测
qt_add_executable(test_frame_codec test_frame_codec.cpp)
target_link_libraries(test_frame_codec PRIVATE
    gy_shared
    Qt6::Test
)
target_compile_features(test_frame_codec PRIVATE cxx_std_23)

add_test(NAME test_frame_codec COMMAND test_frame_codec)

# 边缘场景测试
qt_add_executable(test_edge_cases test_edge_cases.cpp
    ${CMAKE_SOURCE_DIR}/client/core/dir_serializer.cpp
)
target_link_libraries(test_edge_cases PRIVATE
    gy_shared
    Qt6::Test
    Qt6::Core
)
target_include_directories(test_edge_cases PRIVATE ${CMAKE_SOURCE_DIR}/client/core)
target_compile_features(test_edge_cases PRIVATE cxx_std_23)

add_test(NAME test_edge_cases COMMAND test_edge_cases)

# 传输功能测试
qt_add_executable(test_transfer test_transfer.cpp
    ${CMAKE_SOURCE_DIR}/client/core/dir_serializer.cpp
)
target_link_libraries(test_transfer PRIVATE
    gy_shared
    Qt6::Test
    Qt6::Core
)
target_include_directories(test_transfer PRIVATE ${CMAKE_SOURCE_DIR}/client/core)
target_compile_features(test_transfer PRIVATE cxx_std_23)

add_test(NAME test_transfer COMMAND test_transfer)
