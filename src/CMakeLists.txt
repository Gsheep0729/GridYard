# GridYard 顶层构建入口。
# 这里统一设置 C++23/Qt 工程基线，打开 IDE target 分组，并按 shared、client、tests
# 三个子目录组织构建；具体源文件列表由各子目录自己的 CMakeLists 维护。
cmake_minimum_required(VERSION 4.2.3)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
# cmake_minimum_required(VERSION 4.3.2)
# set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
set(CMAKE_CXX_MODULE_STD ON)

project(GridYard VERSION 8.3.0 LANGUAGES CXX)

# 安装规则（client 与 rendezvous 的 install()）使用的 bin/lib 目标目录变量由这里提供
include(GNUInstallDirs)

# Windows 布局 opt-out（第 7 轮 CI 取证定案，完整叙事见 .github/workflows/ci.yml windows job
# 第 6 步注释）：qt6_standard_project_setup 是 macro，其 WIN32 分支（Qt 6.11.1
# Qt6CoreMacros.cmake:3643-3648）在 CMAKE_RUNTIME_OUTPUT_DIRECTORY 未设时把它 opinionated 地
# 定为调用方的 CMAKE_CURRENT_BINARY_DIR（=build-win 顶层），全部 RUNTIME 产物被搬离各自子目录。
# 此处必须在 qt_standard_project_setup 之前预置，macro 的 if(NOT ...) 守卫见值即跳过；
# 取 ${CMAKE_INSTALL_BINDIR} 镜像上方安装 bin 布局、兼顾 Qt「Windows 无 RPATH，exe 与 DLL
# 同目录」的本意。仅 WIN32 生效：Linux 布局逐字节不变，exe 仍在各自子目录
# （check_core_isolation 防线5 的 build-ninja/rendezvous/gridyard_rendezvous 路径不受影响），
# tests/CTestTestfile.cmake 的 add_test COMMAND 展开随 target 真实落盘路径自动一致。
if(WIN32)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})
endif()

# QuickDialogs2 提供原生 FileDialog / FolderDialog，替代已废弃的 Qt.labs.platform 方案
find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Widgets)
qt_standard_project_setup(REQUIRES 6.10)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---- 子模块引入 --------------------------------------------------------
# 第三方纯 C/C++ 头文件库（vendored，零 Qt，见 third_party/checksums.txt）
add_subdirectory(../third_party ${CMAKE_BINARY_DIR}/third_party)
add_subdirectory(shared)
# 纯 C++23 核心库（零 Qt，重构 Phase 1 建立，严禁链接任何 Qt target）
add_subdirectory(core)
add_subdirectory(client)
add_subdirectory(rendezvous)

# V2.0 阶段启用：
# add_subdirectory(server)

# Stage 1+ 启用测试：
enable_testing()
add_subdirectory(tests)
