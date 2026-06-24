# tests/ —— Qt Test 单元测试与集成测试入口。
# 每个测试保持独立可执行文件，按需链接 shared、storage 或客户端实现文件，避免测试之间共享状态。
find_package(Qt6 REQUIRED COMPONENTS Test Gui)

# 通用 include 路径
set(CLIENT_CORE_DIR ${CMAKE_SOURCE_DIR}/client/core)
set(CLIENT_NET_DIR ${CMAKE_SOURCE_DIR}/client/network)

# --- SQLite 数据库基础测试 ---
qt_add_executable(test_storage_database test_storage_database.cpp
    ${CLIENT_CORE_DIR}/application_paths.cpp
)
target_link_libraries(test_storage_database PRIVATE gy_storage Qt6::Test Qt6::Sql)
target_include_directories(test_storage_database PRIVATE
    ${CLIENT_CORE_DIR}
    ${CMAKE_SOURCE_DIR}/client/storage
)
target_compile_features(test_storage_database PRIVATE cxx_std_23)
add_test(NAME test_storage_database COMMAND test_storage_database)

# --- 设备目录 Proxy 测试 ---
qt_add_executable(test_storage_device test_storage_device.cpp
    ${CLIENT_CORE_DIR}/application_paths.cpp
)
target_link_libraries(test_storage_device PRIVATE gy_storage Qt6::Test Qt6::Sql)
target_include_directories(test_storage_device PRIVATE
    ${CLIENT_CORE_DIR}
    ${CMAKE_SOURCE_DIR}/client/storage
)
target_compile_features(test_storage_device PRIVATE cxx_std_23)
add_test(NAME test_storage_device COMMAND test_storage_device)

# --- 聊天消息 Proxy 测试 ---
qt_add_executable(test_storage_message test_storage_message.cpp
    ${CLIENT_CORE_DIR}/application_paths.cpp
)
target_link_libraries(test_storage_message PRIVATE gy_storage Qt6::Test Qt6::Sql)
target_include_directories(test_storage_message PRIVATE
    ${CLIENT_CORE_DIR}
    ${CMAKE_SOURCE_DIR}/client/storage
)
target_compile_features(test_storage_message PRIVATE cxx_std_23)
add_test(NAME test_storage_message COMMAND test_storage_message)

# --- 传输历史 Proxy 测试 ---
qt_add_executable(test_storage_transfer_history test_storage_transfer_history.cpp
    ${CLIENT_CORE_DIR}/application_paths.cpp
)
target_link_libraries(test_storage_transfer_history PRIVATE gy_storage Qt6::Test Qt6::Sql)
target_include_directories(test_storage_transfer_history PRIVATE
    ${CLIENT_CORE_DIR}
    ${CMAKE_SOURCE_DIR}/client/storage
)
target_compile_features(test_storage_transfer_history PRIVATE cxx_std_23)
add_test(NAME test_storage_transfer_history COMMAND test_storage_transfer_history)

# --- FrameCodec 单测 ---
qt_add_executable(test_frame_codec test_frame_codec.cpp)
target_link_libraries(test_frame_codec PRIVATE gy_shared Qt6::Test)
target_compile_features(test_frame_codec PRIVATE cxx_std_23)
add_test(NAME test_frame_codec COMMAND test_frame_codec)

# --- 在线聊天消息测试 ---
qt_add_executable(test_chat_message test_chat_message.cpp)
target_link_libraries(test_chat_message PRIVATE gy_shared Qt6::Test)
target_compile_features(test_chat_message PRIVATE cxx_std_23)
add_test(NAME test_chat_message COMMAND test_chat_message)

# --- 聊天连接与会话测试 ---
qt_add_executable(test_chat_manager test_chat_manager.cpp
    ${CLIENT_CORE_DIR}/chat_manager.cpp
    ${CLIENT_CORE_DIR}/chat_message_model.cpp
    ${CLIENT_CORE_DIR}/config_manager.cpp
    ${CLIENT_CORE_DIR}/dir_serializer.cpp
    ${CLIENT_NET_DIR}/chat_connection.cpp
    ${CLIENT_NET_DIR}/discovery_service.cpp
    ${CLIENT_NET_DIR}/p2p_server.cpp
    ${CLIENT_NET_DIR}/file_receiver_worker.cpp
)
target_link_libraries(test_chat_manager PRIVATE
    gy_shared
    gy_storage
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Sql
)
target_include_directories(test_chat_manager PRIVATE
    ${CLIENT_CORE_DIR}
    ${CLIENT_NET_DIR}
    ${CMAKE_SOURCE_DIR}/client/storage
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/client/domain
)
target_compile_features(test_chat_manager PRIVATE cxx_std_23)
add_test(NAME test_chat_manager COMMAND test_chat_manager)

# --- HistoryController 本地历史视图测试 ---
qt_add_executable(test_history_controller test_history_controller.cpp
    ${CLIENT_CORE_DIR}/chat_manager.cpp
    ${CLIENT_CORE_DIR}/chat_message_model.cpp
    ${CLIENT_CORE_DIR}/config_manager.cpp
    ${CLIENT_CORE_DIR}/dir_serializer.cpp
    ${CLIENT_CORE_DIR}/transfer_session_manager.cpp
    ${CLIENT_NET_DIR}/chat_connection.cpp
    ${CLIENT_NET_DIR}/discovery_service.cpp
    ${CLIENT_NET_DIR}/p2p_server.cpp
    ${CLIENT_NET_DIR}/file_sender_worker.cpp
    ${CLIENT_NET_DIR}/file_receiver_worker.cpp
    ${CLIENT_CORE_DIR}/history_controller.cpp
    ${CLIENT_CORE_DIR}/application_paths.cpp
)
target_link_libraries(test_history_controller PRIVATE
    gy_shared
    gy_storage
    Qt6::Test
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Sql
)
target_include_directories(test_history_controller PRIVATE
    ${CLIENT_CORE_DIR}
    ${CLIENT_NET_DIR}
    ${CMAKE_SOURCE_DIR}/client/storage
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/client/domain
)
target_compile_features(test_history_controller PRIVATE cxx_std_23)
add_test(NAME test_history_controller COMMAND test_history_controller)

# --- 边缘场景测试 ---
qt_add_executable(test_edge_cases test_edge_cases.cpp
    ${CLIENT_CORE_DIR}/dir_serializer.cpp
)
target_link_libraries(test_edge_cases PRIVATE gy_shared Qt6::Test Qt6::Core)
target_include_directories(test_edge_cases PRIVATE ${CLIENT_CORE_DIR})
target_compile_features(test_edge_cases PRIVATE cxx_std_23)
add_test(NAME test_edge_cases COMMAND test_edge_cases)

# --- 传输功能测试 ---
qt_add_executable(test_transfer test_transfer.cpp
    ${CLIENT_CORE_DIR}/dir_serializer.cpp
)
target_link_libraries(test_transfer PRIVATE gy_shared Qt6::Test Qt6::Core)
target_include_directories(test_transfer PRIVATE ${CLIENT_CORE_DIR})
target_compile_features(test_transfer PRIVATE cxx_std_23)
add_test(NAME test_transfer COMMAND test_transfer)

# --- ConfigManager 配置测试 ---
qt_add_executable(test_config_manager test_config_manager.cpp
    ${CLIENT_CORE_DIR}/config_manager.cpp
    ${CLIENT_CORE_DIR}/dir_serializer.cpp
)
target_link_libraries(test_config_manager PRIVATE gy_shared Qt6::Test Qt6::Core Qt6::Gui Qt6::Network)
target_include_directories(test_config_manager PRIVATE ${CLIENT_CORE_DIR})
target_compile_features(test_config_manager PRIVATE cxx_std_23)
add_test(NAME test_config_manager COMMAND test_config_manager)

# --- DiscoveryService 设备发现测试 ---
qt_add_executable(test_discovery test_discovery.cpp
    ${CLIENT_CORE_DIR}/config_manager.cpp
    ${CLIENT_CORE_DIR}/dir_serializer.cpp
    ${CLIENT_NET_DIR}/discovery_service.cpp
)
target_link_libraries(test_discovery PRIVATE gy_shared Qt6::Test Qt6::Core Qt6::Gui Qt6::Network)
target_include_directories(test_discovery PRIVATE ${CLIENT_CORE_DIR} ${CLIENT_NET_DIR})
target_compile_features(test_discovery PRIVATE cxx_std_23)
add_test(NAME test_discovery COMMAND test_discovery)

# --- 文件传输集成测试 ---
qt_add_executable(test_file_transfer test_file_transfer.cpp
    ${CLIENT_CORE_DIR}/config_manager.cpp
    ${CLIENT_CORE_DIR}/dir_serializer.cpp
    ${CLIENT_CORE_DIR}/transfer_session_manager.cpp
    ${CLIENT_NET_DIR}/discovery_service.cpp
    ${CLIENT_NET_DIR}/p2p_server.cpp
    ${CLIENT_NET_DIR}/file_sender_worker.cpp
    ${CLIENT_NET_DIR}/file_receiver_worker.cpp
)
target_link_libraries(test_file_transfer PRIVATE gy_shared Qt6::Test Qt6::Core Qt6::Gui Qt6::Network)
target_include_directories(test_file_transfer PRIVATE
    ${CLIENT_CORE_DIR}
    ${CLIENT_NET_DIR}
    ${CMAKE_SOURCE_DIR}/client/domain
    ${CMAKE_SOURCE_DIR}/shared
)
target_compile_features(test_file_transfer PRIVATE cxx_std_23)
add_test(NAME test_file_transfer COMMAND test_file_transfer)

# --- 会话管理测试 ---
qt_add_executable(test_session_manager test_session_manager.cpp
    ${CLIENT_CORE_DIR}/config_manager.cpp
    ${CLIENT_CORE_DIR}/dir_serializer.cpp
    ${CLIENT_CORE_DIR}/transfer_session_manager.cpp
    ${CLIENT_NET_DIR}/discovery_service.cpp
    ${CLIENT_NET_DIR}/p2p_server.cpp
    ${CLIENT_NET_DIR}/file_sender_worker.cpp
    ${CLIENT_NET_DIR}/file_receiver_worker.cpp
)
target_link_libraries(test_session_manager PRIVATE gy_shared Qt6::Test Qt6::Core Qt6::Gui Qt6::Network)
target_include_directories(test_session_manager PRIVATE
    ${CLIENT_CORE_DIR}
    ${CLIENT_NET_DIR}
    ${CMAKE_SOURCE_DIR}/client/domain
)
target_compile_features(test_session_manager PRIVATE cxx_std_23)
add_test(NAME test_session_manager COMMAND test_session_manager)

# --- 集成测试（单机多实例模拟）---
qt_add_executable(test_integration test_integration.cpp
    ${CLIENT_CORE_DIR}/config_manager.cpp
    ${CLIENT_CORE_DIR}/dir_serializer.cpp
    ${CLIENT_CORE_DIR}/transfer_session_manager.cpp
    ${CLIENT_NET_DIR}/discovery_service.cpp
    ${CLIENT_NET_DIR}/p2p_server.cpp
    ${CLIENT_NET_DIR}/file_sender_worker.cpp
    ${CLIENT_NET_DIR}/file_receiver_worker.cpp
)
target_link_libraries(test_integration PRIVATE gy_shared Qt6::Test Qt6::Core Qt6::Gui Qt6::Network)
target_include_directories(test_integration PRIVATE
    ${CLIENT_CORE_DIR}
    ${CLIENT_NET_DIR}
    ${CMAKE_SOURCE_DIR}/client/domain
    ${CMAKE_SOURCE_DIR}/shared
)
target_compile_features(test_integration PRIVATE cxx_std_23)
add_test(NAME test_integration COMMAND test_integration)
