# sqlite/ —— SQLite amalgamation 3.45.0（Public Domain，vendored 编译为静态库）
# 来源 https://sqlite.org/2024/sqlite-amalgamation-3450000.zip，
# SHA256 见同目录上级 checksums.txt；升级版本时必须同步更新校验和。
# 纯 C 实现：开启线程安全、裁剪废弃接口与动态加载扩展，供 src/core/storage
# 的 RAII 封装直接使用，替代 QSqlDatabase 的执行引擎。

enable_language(C)

add_library(sqlite3_vendor STATIC
    sqlite3.c
)

target_include_directories(sqlite3_vendor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sqlite3_vendor PRIVATE
    SQLITE_THREADSAFE=1        # 多线程模式：连接独占、全局互斥保护（与 QSqlDatabase 并存所需）
    SQLITE_OMIT_DEPRECATED     # 裁剪废弃 API，缩小攻击面
    SQLITE_OMIT_LOAD_EXTENSION # 禁止动态加载扩展，离线环境不可用且不安全
)

# amalgamation 单文件 25 万行，第三方代码告警一律屏蔽，保证零新警告门槛不误报
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sqlite3_vendor PRIVATE -w)
endif()

set_property(TARGET sqlite3_vendor PROPERTY FOLDER "third_party")
# 全局 qt_standard_project_setup 打开了 AUTOMOC 等生成器，纯 C 库显式关闭
set_target_properties(sqlite3_vendor PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
