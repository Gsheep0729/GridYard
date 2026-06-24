# GridYard CMake 构建逻辑与分组说明

| 字段 | 内容 |
| :--- | :--- |
| 文档版本 | v1.0 |
| 日期 | 2026-06-25 |
| 适用版本 | v6.6.2 |
| 适用范围 | `src/` 下的 CMake 构建脚本与 IDE 文件分组 |

## 1. 目标

这份文档说明 GridYard 当前的 CMake 构建入口、模块关系、链接方向和 IDE 展示分组。项目目录按功能保持简洁：`shared/`、`client/`、`domain/`、`storage/`、`network/`、`ui/`。构建脚本中的 `gy_shared`、`gy_domain`、`gy_storage` 只是 CMake 内部 target 名，用来表达编译和链接关系，不代表源码目录里又拆了一层文件夹。

本轮分组优化只整理 CMake 列表和 IDE 展示，不改变协议、QML 注册方式、应用版本号或运行行为。

## 2. 构建入口

顶层入口位于 `src/CMakeLists.txt`，负责设置工程级配置并引入子目录。

```text
src/CMakeLists.txt
├── find_package(Qt6 ...)
├── qt_standard_project_setup(...)
├── set_property(GLOBAL PROPERTY USE_FOLDERS ON)
├── add_subdirectory(shared)
├── add_subdirectory(client)
└── add_subdirectory(tests)
```

`USE_FOLDERS` 用于让 Qt Creator、Visual Studio 等支持 CMake folder 的 IDE 按模块折叠 target。它不影响命令行构建结果，也不会改变生成的可执行文件。

常用构建命令仍保持不变：

```bash
cmake -S src -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-ninja -j
ctest --test-dir build-ninja --output-on-failure
```

## 3. 目录与 target 关系

| 源码目录 | CMake target | 类型 | 主要职责 |
| :--- | :--- | :--- | :--- |
| `src/shared/` | `gy_shared` | STATIC | TLV 协议常量、共享数据类型、聊天消息编解码、帧编解码 |
| `src/client/domain/` | `gy_domain` | INTERFACE | 本地历史相关值类型和 Repository 接口 |
| `src/client/storage/` | `gy_storage` | STATIC | SQLite 连接、迁移、设备目录、消息和传输历史持久化 |
| `src/client/` | `appGridYard` | EXECUTABLE + QML module | 桌面客户端入口、QML 模块、应用控制器、网络与界面 |
| `src/tests/` | 多个 test target | EXECUTABLE | 单元测试和集成测试 |

target 名保留 `gy_` 前缀，是为了避免和系统库、Qt 模块或未来服务端 target 重名。阅读源码时仍按目录理解即可：`shared` 是共享协议层，`domain` 是领域接口层，`storage` 是 SQLite 持久化层。

## 4. 链接方向

当前链接关系如下：

```text
appGridYard
├── Qt6::Quick
├── Qt6::QuickControls2
├── Qt6::Network
├── gy_shared
└── gy_storage

gy_storage
├── gy_domain
├── Qt6::Core
└── Qt6::Sql

gy_domain
├── Qt6::Core
└── gy_shared

gy_shared
├── Qt6::Core
└── Qt6::Network
```

`Qt6::Sql` 只在 `storage` 层使用，不传播到 QML 页面和网络 Worker。QML、网络和应用控制层不直接执行 SQL，也不直接持有数据库连接。

## 5. 客户端 QML 模块

`src/client/CMakeLists.txt` 使用单个 `qt_add_qml_module(appGridYard ...)` 注册客户端 QML 模块：

```cmake
qt_add_qml_module(appGridYard
    URI "cqnu.gridyard.client"
    VERSION 1.0
    SOURCES ...
    QML_FILES ...
    RESOURCES ...
)
```

这个模块同时收纳三类内容：

| 分组 | 内容 |
| :--- | :--- |
| `SOURCES` | `main.cpp`、`core/*.h/.cpp`、`network/*.h/.cpp` |
| `QML_FILES` | `Main.qml`、`ui/*.qml`、`utils/*.js` |
| `RESOURCES` | `icons/*.svg`、`icons/gridyard.png` |

`utils/FormatUtils.js` 和 `utils/Style.js` 设置了 `QT_QML_SKIP_QMLDIR_ENTRY TRUE`，表示它们作为内部 JavaScript 工具文件打包，不作为可直接 import 的 QML 类型暴露。

## 6. IDE 文件分组

客户端 CMake 里使用 `source_group()` 把文件按实际目录展示：

```text
client/entry
├── main.cpp
└── Main.qml

client/core
├── app_controller.*
├── chat_manager.*
├── history_controller.*
└── ...

client/network
├── discovery_service.*
├── p2p_server.*
├── chat_connection.*
└── ...

client/ui
├── DeviceSessionView.qml
├── ChatView.qml
├── TransferHistoryView.qml
└── ...

client/utils
├── FormatUtils.js
└── Style.js

client/icons
├── gridyard.png
├── folder.svg
└── file-*.svg
```

`shared/` 和 `storage/` 也使用 `source_group(TREE ...)` 按目录展示。这样在 IDE 里展开时看到的是业务目录，而不是一长串平铺文件。

## 7. 新增文件放置规则

| 新增内容 | 放置目录 | CMake 修改位置 |
| :--- | :--- | :--- |
| 共享协议、共享值类型、帧编解码辅助 | `src/shared/` | `src/shared/CMakeLists.txt` 的 `GRIDYARD_SHARED_SOURCES` |
| 应用控制器、模型、业务编排 | `src/client/core/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_CORE_SOURCES` |
| UDP/TCP、连接对象、传输 Worker | `src/client/network/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_NETWORK_SOURCES` |
| QML 页面或组件 | `src/client/ui/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_QML_UI_FILES` |
| QML 内部 JavaScript 工具 | `src/client/utils/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_UTIL_FILES` |
| 图标和轻量资源 | `src/client/icons/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_ICON_RESOURCES` |
| 领域记录和 Repository 接口 | `src/client/domain/` | 通常只需放入目录；若新增编译单元，再调整对应 CMake |
| SQLite Proxy、migration、数据库 Worker | `src/client/storage/` | `src/client/storage/CMakeLists.txt` 的 `GRIDYARD_STORAGE_SOURCES` |
| 测试用例 | `src/tests/` | `src/tests/CMakeLists.txt` 新增对应 test target |

新增 C++ 文件后需要同时确认：

- 头文件和源文件都进入对应 `SOURCES` 变量。
- 若类需要暴露给 QML，继续使用 `QML_ELEMENT` 或 `QML_SINGLETON`，并由 `qt_add_qml_module()` 自动注册。
- 若只是内部业务类，不要额外手工注册 QML 类型。
- 测试中需要复用客户端实现文件时，优先复用已有 include 路径和链接 target，不复制源码。

## 8. 调整边界

CMake 文件维护时优先做增量修改：新增源文件加入对应变量，新增库加入明确的 `target_link_libraries()`，新增目录再考虑 `add_subdirectory()`。不要为了整理展示而重写整个 `CMakeLists.txt`，也不要把稳定的 P2P、聊天或 SQLite 链路拆成临时 target。

target 名、目录名和 IDE 分组各有职责：

| 名称类型 | 示例 | 用途 |
| :--- | :--- | :--- |
| 目录名 | `shared/`、`storage/` | 人阅读源码时的业务分区 |
| target 名 | `gy_shared`、`gy_storage` | CMake 编译、链接和依赖管理 |
| IDE 分组 | `client/core`、`client/ui` | 在 IDE 文件树中降低平铺感 |

日常开发只需要关注目录名；只有改 CMake、链接测试或排查构建问题时，才需要关心 target 名。
