cmake_minimum_required(VERSION 4.2.3)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
set(CMAKE_CXX_MODULE_STD ON)

project(integration_demo VERSION 0.1 LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appintegration_demo  WIN32)
qt_add_qml_module(appintegration_demo
    URI         "cqnu.gridyard.demo"
    VERSION     1.0
    SOURCES     main.cpp
    SOURCES     peer_info.h
                app_controller.h app_controller.cpp
                discovery_stub.h discovery_stub.cpp
    QML_FILES   Main.qml DeviceCard.qml
)
target_compile_features(appintegration_demo PRIVATE cxx_std_23)
target_link_libraries(appintegration_demo
    PRIVATE Qt6::Quick
)

set(CMAKE_INSTALL_PREFIX "/opt/${PROJECT_NAME}" CACHE PATH "Install path prefix" FORCE)
install(TARGETS appintegration_demo
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
