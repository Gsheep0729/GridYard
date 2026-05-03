# client/ —— GridYard 桌面客户端（C++ + QML）
# 整个客户端注册为一个 QML 模块，URI cqnu.gridyard.client
# 所有带 QML_ELEMENT / QML_SINGLETON 宏的 C++ 类型由 qt_add_qml_module
# 自动注册；QML 文件统一打包到资源系统 /qt/qml/<URI> 标准路径

qt_add_executable(appGridYard WIN32)

qt_add_qml_module(appGridYard
    URI         "cqnu.gridyard.client"
    VERSION     1.0

    SOURCES
        main.cpp
        core/app_controller.h
        core/app_controller.cpp
        core/config_manager.h
        core/config_manager.cpp

    QML_FILES
        Main.qml
)

target_compile_features(appGridYard PRIVATE cxx_std_23)

target_link_libraries(appGridYard PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    gy_shared
)

target_include_directories(appGridYard PRIVATE core)

# ---- 安装规则 ---------------------------------------------------------
set(CMAKE_INSTALL_PREFIX "/opt/${PROJECT_NAME}" CACHE PATH "Install path prefix" FORCE)
install(TARGETS appGridYard
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
