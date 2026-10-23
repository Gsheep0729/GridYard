# third_party/ —— 纯 C/C++ 第三方依赖（vendored，离线可复现）
# 引入来源与 SHA256 校验和见同目录 checksums.txt，升级版本时必须同步更新。
# 两个库均为 header-only，使用 INTERFACE 目标暴露 include 目录，不参与编译。

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

set_property(TARGET nlohmann_json PROPERTY FOLDER "third_party")
set_property(TARGET picosha2 PROPERTY FOLDER "third_party")
