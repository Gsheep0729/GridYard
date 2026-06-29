# GridYard 发布分支构建入口。
# 这里统一设置 C++23/Qt 工程基线，打开 IDE target 分组，并按 shared、client
# 两个子目录组织正式客户端构建；测试 target 保留在开发分支。
cmake_minimum_required(VERSION 4.2.3)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
# cmake_minimum_required(VERSION 4.3.2)
# set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD ON)

project(GridYard VERSION 6.8.1 LANGUAGES CXX)

# QuickDialogs2 提供原生 FileDialog / FolderDialog，替代已废弃的 Qt.labs.platform 方案
find_package(Qt6 REQUIRED COMPONENTS Quick Network QuickControls2 QuickDialogs2 Sql Widgets)
qt_standard_project_setup(REQUIRES 6.10)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---- 子模块引入 --------------------------------------------------------
add_subdirectory(shared)
add_subdirectory(client)

# V2.0 阶段启用：
# add_subdirectory(server)
