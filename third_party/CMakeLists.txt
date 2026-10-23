# third_party/ —— 纯 C/C++ 第三方依赖（vendored，离线可复现）
# 引入来源与 SHA256 校验和见同目录 checksums.txt，升级版本时必须同步更新。
# nlohmann 与 picosha2 为 header-only（INTERFACE 目标）；sqlite 为编译型
# 静态库目标，源码同样 vendored，离线可复现。

# nlohmann/json v3.11.3 —— MIT 许可证，替代 QJsonDocument 的 JSON 编解码
# 头文件位于 nlohmann/json.hpp，包含目录指向 third_party 根以支持 <nlohmann/json.hpp>
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# picosha2 —— MIT 许可证，SHA-256 单头文件实现（字节串一次性哈希场景）
add_library(picosha2 INTERFACE)
target_include_directories(picosha2 INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/picosha2
)

# SQLite 3.45.0 amalgamation（Public Domain）——编译为静态库 sqlite3_vendor，
# 供 src/core/storage 的纯 C++ RAII 封装直接使用，替代 QSqlDatabase 执行引擎
add_subdirectory(sqlite)

set_property(TARGET nlohmann_json PROPERTY FOLDER "third_party")
set_property(TARGET picosha2 PROPERTY FOLDER "third_party")
