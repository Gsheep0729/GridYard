cmake_minimum_required(VERSION 4.2.3)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
# cmake_minimum_required(VERSION 4.3.2)
# set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD ON)

project(Qt_Quick_application VERSION 0.1 LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.11)

add_subdirectory(lib)

qt_add_executable(appQt_Quick_application  WIN32)
qt_add_qml_module(appQt_Quick_application
    URI Qt_Quick_application
    VERSION 1.0
    SOURCES     main.cpp
    QML_FILES   Main.qml
)
target_compile_features(appQt_Quick_application PRIVATE cxx_std_23)
target_link_libraries(appQt_Quick_application
    PRIVATE Qt6::Quick mylib
)

set(CMAKE_INSTALL_PREFIX "/opt/${PROJECT_NAME}" CACHE PATH "Install path prefix" FORCE)
install(TARGETS appQt_Quick_application
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# C++ Modules support
set(QT_QUICK_APPLICATION_MODULE_FILES
    # math.cppm                             # 模块文件
)
source_group("Qt_Quick_application" FILES ${QT_QUICK_APPLICATION_MODULE_FILES})
target_sources(appQt_Quick_application
    PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS   ${CMAKE_CURRENT_SOURCE_DIR}
        FILES  ${QT_QUICK_APPLICATION_MODULE_FILES}
)
