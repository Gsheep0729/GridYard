cmake_minimum_required(VERSION 4.2.3)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
# cmake_minimum_required(VERSION 4.3.2)
# set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD ON)

project(GridYard VERSION 4.7 LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Quick Network QuickControls2)
qt_standard_project_setup(REQUIRES 6.10)

# ---- 子模块引入 --------------------------------------------------------
add_subdirectory(shared)
add_subdirectory(client)

# V2.0 阶段启用：
# add_subdirectory(server)

# Stage 1+ 启用测试：
enable_testing()
add_subdirectory(tests)

