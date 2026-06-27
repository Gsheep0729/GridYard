# client/ —— GridYard 桌面客户端（C++ + QML）
# 整个客户端注册为一个 QML 模块，URI cqnu.gridyard.client
# 所有带 QML_ELEMENT / QML_SINGLETON 宏的 C++ 类型由 qt_add_qml_module
# 自动注册；QML 文件统一打包到资源系统 /qt/qml/<URI> 标准路径
# 本文件只直接收纳客户端入口、core、network、QML、JS 和图标资源。
# domain 与 storage 作为独立子模块进入构建，再通过 target_link_libraries 链接到客户端。

add_subdirectory(domain)
add_subdirectory(storage)

qt_add_executable(appGridYard WIN32)

set(GRIDYARD_CLIENT_CORE_SOURCES
    core/app_controller.h
    core/app_controller.cpp
    core/application_paths.h
    core/application_paths.cpp
    core/chat_manager.h
    core/chat_manager.cpp
    core/chat_controller.h
    core/chat_controller.cpp
    core/chat_message_model.h
    core/chat_message_model.cpp
    core/history_controller.h
    core/history_controller.cpp
    core/config_manager.h
    core/config_manager.cpp
    core/dir_serializer.h
    core/dir_serializer.cpp
    core/logger.h
    core/logger.cpp
    core/peer_discovery_view_model.h
    core/peer_discovery_view_model.cpp
    core/transfer_controller.h
    core/transfer_controller.cpp
    core/transfer_session_manager.h
    core/transfer_session_manager.cpp
)

set(GRIDYARD_CLIENT_NETWORK_SOURCES
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
)

set(GRIDYARD_CLIENT_QML_ENTRY_FILES
    Main.qml
)

set(GRIDYARD_CLIENT_QML_UI_FILES
    ui/DeviceCard.qml
    ui/PeerListView.qml
    ui/SettingsDialog.qml
    ui/AcceptDialog.qml
    ui/TransferPanel.qml
    ui/TransferHistoryView.qml
    ui/TransferTaskCard.qml
    ui/DeviceSessionView.qml
    ui/ConversationTimelineView.qml  # 统一会话时间线：消息与传输卡片混排
    ui/ChatView.qml
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

    QML_FILES
        ${GRIDYARD_CLIENT_QML_ENTRY_FILES}
        ${GRIDYARD_CLIENT_QML_UI_FILES}
        ${GRIDYARD_CLIENT_UTIL_FILES}

    RESOURCES
        ${GRIDYARD_CLIENT_ICON_RESOURCES}
)

source_group("client/entry" FILES main.cpp ${GRIDYARD_CLIENT_QML_ENTRY_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/core PREFIX "client/core" FILES ${GRIDYARD_CLIENT_CORE_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/network PREFIX "client/network" FILES ${GRIDYARD_CLIENT_NETWORK_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/ui PREFIX "client/ui" FILES ${GRIDYARD_CLIENT_QML_UI_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/utils PREFIX "client/utils" FILES ${GRIDYARD_CLIENT_UTIL_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/icons PREFIX "client/icons" FILES ${GRIDYARD_CLIENT_ICON_RESOURCES})

target_compile_features(appGridYard PRIVATE cxx_std_23)

target_link_libraries(appGridYard PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2  # 原生文件/文件夹选择对话框
    Qt6::Network
    gy_shared
    gy_storage
)

target_include_directories(appGridYard PRIVATE core network)
set_property(TARGET appGridYard PROPERTY FOLDER "client")

# ---- 安装规则 ---------------------------------------------------------
set(CMAKE_INSTALL_PREFIX "/opt/${PROJECT_NAME}" CACHE PATH "Install path prefix" FORCE)
install(TARGETS appGridYard
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
