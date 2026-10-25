# client/ —— GridYard 桌面客户端（C++ + QML）
# 整个客户端注册为一个 QML 模块，URI cqnu.gridyard.client
# 所有带 QML_ELEMENT / QML_SINGLETON 宏的 C++ 类型由 qt_add_qml_module
# 自动注册；QML 文件统一打包到资源系统 /qt/qml/<URI> 标准路径
# 本文件直接收纳客户端入口、core、network、storage、QML、JS 和图标资源。

qt_add_executable(appGridYard WIN32)

set(GRIDYARD_CLIENT_CORE_SOURCES
    core/app_controller.h
    core/app_controller.cpp
    core/application_paths.h
    core/application_paths.cpp
    core/chat_manager.h
    core/chat_manager.cpp
    core/codec_bridge.h
    core/chat_message.h
    core/chat_controller.h
    core/chat_controller.cpp
    core/chat_message_model.h
    core/chat_message_model.cpp
    core/conversation_timeline_model.h
    core/conversation_timeline_model.cpp
    core/history_controller.h
    core/backup_controller.cpp
    core/history_controller.cpp
    core/config_manager.h
    core/config_manager.cpp
    core/data_types.h
    core/dir_serializer.h
    core/dir_serializer.cpp
    core/logger.h
    core/logger.cpp
    core/peer_discovery_view_model.h
    core/peer_discovery_view_model.cpp
    core/reachability_controller.h
    core/reachability_controller.cpp
    core/transfer_controller.h
    core/transfer_controller.cpp
    core/transfer_session_manager.h
    core/transfer_session_manager.cpp
    core/transfer_session_mapper.h
    core/transfer_session_mapper.cpp
    core/transfer_session_model.h
    core/transfer_session_model.cpp
    core/rendezvous_coordinator.h
    core/rendezvous_coordinator.cpp
    core/history_wiring.h
    core/history_wiring.cpp
    core/shutdown_controller.h
    core/shutdown_controller.cpp
)

set(GRIDYARD_CLIENT_NETWORK_SOURCES
    network/ip_address.h
    network/ip_address.cpp
    network/net_interfaces.h
    network/net_interfaces.cpp
    network/posix_net_error.h
    network/posix_net_error.cpp
    network/posix_net_types.h
    network/posix_tcp_server.h
    network/posix_tcp_server.cpp
    network/posix_tcp_socket.h
    network/posix_tcp_socket.cpp
    network/posix_udp_socket.h
    network/posix_udp_socket.cpp
    network/discovery_service.h
    network/discovery_service.cpp
    network/chat_connection.h
    network/chat_connection.cpp
    network/p2p_server.h
    network/p2p_server.cpp
    network/file_sender_worker.h
    network/file_sender_worker.cpp
    network/file_receiver_worker.h
    network/file_receiver_worker.cpp
    network/endpoint_probe.h
    network/endpoint_probe.cpp
    network/invite_codec.h
    network/invite_codec.cpp
    network/rendezvous_client.h
    network/rendezvous_client.cpp
)

set(GRIDYARD_CLIENT_STORAGE_SOURCES
    storage/local_data_broker.h
    storage/local_data_broker.cpp
    storage/database_worker.h
    storage/database_worker.cpp
)

# 领域记录与仓储端口两头文件（原 gy_domain 接口目标退役后归客户端直挂）
set(GRIDYARD_CLIENT_DOMAIN_HEADERS
    domain/history_records.h
    domain/history_repositories.h
)

set(GRIDYARD_CLIENT_QML_ENTRY_FILES
    Main.qml
)

set(GRIDYARD_CLIENT_QML_UI_FILES
    ui/Toast.qml
    ui/TrayIcon.qml
    ui/Sidebar.qml
    ui/CloseConfirmDialog.qml
    ui/HideDeviceDialog.qml
    ui/DeleteDeviceDialog.qml
    ui/BackupExportDialog.qml
    ui/DeviceMergeDialog.qml
    ui/MultiTargetDialog.qml
    ui/BackupImportDialog.qml
    ui/DeviceAliasDialog.qml
    ui/HomePlaceholder.qml
    ui/CompletionToast.qml
    ui/RelayConfirmDialog.qml
    ui/DangerButton.qml
    ui/DialogHeader.qml
    ui/DeviceCard.qml
    ui/RefreshIcon.qml
    ui/PeerListView.qml
    ui/SettingsCard.qml
    ui/SettingsDialog.qml
    ui/AcceptDialog.qml
    ui/AddDeviceDialog.qml
    ui/DeviceHistoryDialog.qml
    ui/TransferHistoryView.qml
    ui/TransferTaskCard.qml
    ui/DeviceSessionView.qml
    ui/ConversationTimelineView.qml  # 统一会话时间线：消息与传输卡片混排
    ui/FileTypeIcon.qml
)

set(GRIDYARD_CLIENT_UTIL_FILES
    utils/FormatUtils.js
    utils/Style.js
)

set(GRIDYARD_CLIENT_ICON_RESOURCES
    icons/folder.svg
    icons/file-generic.svg
    icons/file-image.svg
    icons/file-video.svg
    icons/file-audio.svg
    icons/file-archive.svg
    icons/file-code.svg
    icons/file-document.svg
    icons/file-executable.svg
    icons/gridyard.png
)

set_source_files_properties(utils/FormatUtils.js utils/Style.js PROPERTIES
    QT_QML_SKIP_QMLDIR_ENTRY TRUE
)

qt_add_qml_module(appGridYard
    URI         "cqnu.gridyard.client"
    VERSION     1.0

    SOURCES
        main.cpp
        ${GRIDYARD_CLIENT_CORE_SOURCES}
        ${GRIDYARD_CLIENT_NETWORK_SOURCES}
        ${GRIDYARD_CLIENT_STORAGE_SOURCES}
        ${GRIDYARD_CLIENT_DOMAIN_HEADERS}

    QML_FILES
        ${GRIDYARD_CLIENT_QML_ENTRY_FILES}
        ${GRIDYARD_CLIENT_QML_UI_FILES}
        ${GRIDYARD_CLIENT_UTIL_FILES}

    RESOURCES
        ${GRIDYARD_CLIENT_ICON_RESOURCES}
)

source_group("client/entry" FILES main.cpp ${GRIDYARD_CLIENT_QML_ENTRY_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/core PREFIX "client/core" FILES ${GRIDYARD_CLIENT_CORE_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/domain PREFIX "client/domain" FILES ${GRIDYARD_CLIENT_DOMAIN_HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/storage PREFIX "client/storage" FILES ${GRIDYARD_CLIENT_STORAGE_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/network PREFIX "client/network" FILES ${GRIDYARD_CLIENT_NETWORK_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/ui PREFIX "client/ui" FILES ${GRIDYARD_CLIENT_QML_UI_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/utils PREFIX "client/utils" FILES ${GRIDYARD_CLIENT_UTIL_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/icons PREFIX "client/icons" FILES ${GRIDYARD_CLIENT_ICON_RESOURCES})

target_compile_features(appGridYard PRIVATE cxx_std_23)

target_link_libraries(appGridYard PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2  # 原生文件/文件夹选择对话框
    Qt6::Widgets        # SystemTrayIcon / Menu 等需要 Widgets
    gy_shared
    gy_net_core         # 网络适配器引用的事件泵核心（core/ 前缀头与符号）
    gy_storage_core
    gy_core
)

target_include_directories(appGridYard PRIVATE core network domain storage)
set_property(TARGET appGridYard PROPERTY FOLDER "client")

# ---- 安装规则（前缀遵循 CMake 标准语义，可由 -DCMAKE_INSTALL_PREFIX / cmake --install --prefix 覆盖）----
install(TARGETS appGridYard
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
