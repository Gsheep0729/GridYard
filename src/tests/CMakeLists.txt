find_package(Qt6 REQUIRED COMPONENTS Test)

# FrameCodec 单测
qt_add_executable(test_frame_codec test_frame_codec.cpp)
target_link_libraries(test_frame_codec PRIVATE
    gy_shared
    Qt6::Test
)
target_compile_features(test_frame_codec PRIVATE cxx_std_23)

add_test(NAME test_frame_codec COMMAND test_frame_codec)
