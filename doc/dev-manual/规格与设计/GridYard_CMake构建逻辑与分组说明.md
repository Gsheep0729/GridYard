# GridYard CMake 构建逻辑与分组说明

| 字段 | 内容 |
| :--- | :--- |
| 文档版本 | v2.0 |
| 日期 | 2026-10-25 |
| 适用版本 | v8.0.0 |
| 适用范围 | `src/` 下的 CMake 构建脚本与 IDE 文件分组 |

## 1. 目标

这份文档说明 GridYard 当前的 CMake 构建入口、模块关系、链接方向和 IDE 展示分组。项目目录按功能保持简洁：`core/`（纯 C++ 核心）、`shared/`（协议常量与网络核心）、`client/`（桌面客户端）、`rendezvous/`（协调与中继服务）、`tests/`。核心层目标零 Qt 依赖并显式关闭 AUTOMOC/AUTOUIC/AUTORCC；历史上的 `gy_domain`、`gy_storage` 与 `gy_net` 三个桥接目标已退役，其内容分别归位客户端源清单与 `src/client/network/`。

本文只整理构建事实；分层动机与红线验证方法见同目录《GridYard_纯C++核心架构说明.md》。

## 2. 构建入口

顶层入口位于 `src/CMakeLists.txt`，负责设置工程级配置并引入子目录。

```text
src/CMakeLists.txt
├── find_package(Qt6 COMPONENTS Quick QuickControls2 QuickDialogs2 Widgets)
├── qt_standard_project_setup(...)
├── add_subdirectory(../third_party ...)   # vendored nlohmann/picosha2/sqlite3
├── add_subdirectory(shared)
├── add_subdirectory(core)
├── add_subdirectory(client)
├── add_subdirectory(rendezvous)
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
| `src/core/` | `gy_core` | STATIC（零 Qt） | 领域类型、帧与聊天编解码、目录哈希、INI 配置读写 |
| `src/core/storage/` | `gy_storage_core` | STATIC（零 Qt） | vendored SQLite3 封装、迁移与三个仓储的纯 C++ 实现 |
| `src/shared/` | `gy_shared`、`gy_protocol_contracts` | STATIC（纯头）/ INTERFACE | TLV 协议常量头；协调行协议键常量 |
| `src/shared/net/core/` | `gy_net_core` | STATIC（零 Qt） | NetReactor 事件泵与 TCP/UDP 套接字核心 |
| `src/client/` | `appGridYard` | EXECUTABLE + QML module | 客户端入口、QML 模块、控制器与模型、套接字适配器、数据层代管者与 Worker（domain 头与 storage 源直接编入本目标） |
| `src/rendezvous/` | `gy_rendezvous`、`gridyard_rendezvous` | STATIC + EXECUTABLE（零 Qt） | 协调节点与流式中继服务 |
| `src/tests/` | 多个 test target | EXECUTABLE | 单元测试和集成测试（按需直编客户端源文件） |

target 名保留 `gy_` 前缀，是为了避免和系统库、Qt 模块或未来服务端 target 重名。阅读源码时仍按目录理解即可：`core` 是纯 C++ 核心层，`shared` 是双端共享常量与网络核心，`client` 是 Qt 桥接的桌面端。

## 4. 链接方向

当前链接关系如下：

```text
appGridYard
├── Qt6::Quick / Qt6::QuickControls2 / Qt6::QuickDialogs2 / Qt6::Widgets
├── gy_shared
├── gy_net_core
├── gy_storage_core
└── gy_core

gy_core          → nlohmann_json、picosha2（PRIVATE）
gy_storage_core  → sqlite3_vendor
gy_net_core      → Threads
gy_shared        → gy_protocol_contracts
gy_rendezvous    → gy_net_core、gy_protocol_contracts、nlohmann_json
gridyard_rendezvous → gy_rendezvous（ldd 零 Qt，仅系统 C++ 运行库）
```

网络收发由 Linux 系统调用完成，源码与构建均不依赖 `Qt6::Network`；全仓 CMake 同样不再出现 `Qt6::Sql`（存储已走 vendored SQLite3 的 C API）。QML、网络和应用控制层不直接执行 SQL，也不直接持有数据库连接。

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
| `SOURCES` | `main.cpp`、`core/*.h/.cpp`（含 Controller/ViewModel 门面）、`network/*.h/.cpp` |
| `QML_FILES` | `Main.qml`、`ui/*.qml`（含 `ConversationTimelineView.qml`）、`utils/*.js` |
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
├── chat_controller.*
├── chat_manager.*
├── history_controller.*
├── peer_discovery_view_model.*
├── transfer_controller.*
└── ...

client/network
├── discovery_service.*
├── p2p_server.*
├── chat_connection.*
└── ...

client/ui
├── DeviceSessionView.qml
├── ConversationTimelineView.qml
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

`shared/`、`client/domain/` 与 `client/storage/` 也使用 `source_group(TREE ...)` 按目录展示，`core/` 与 `rendezvous/` 各自的目标同理。这样在 IDE 里展开时看到的是业务目录，而不是一长串平铺文件。

## 7. 新增文件放置规则

| 新增内容 | 放置目录 | CMake 修改位置 |
| :--- | :--- | :--- |
| 纯 C++ 协议工具、领域类型、配置与哈希 | `src/core/` 对应分组 | `src/core/CMakeLists.txt` 的源清单 |
| 纯 C++ 存储封装与仓储 | `src/core/storage/` | `src/core/storage/CMakeLists.txt` 的源清单 |
| 协调行协议键常量 | `src/shared/` | `src/shared/CMakeLists.txt` |
| 事件泵与套接字核心 | `src/shared/net/core/` | `src/shared/net/CMakeLists.txt` 的 `GY_NET_CORE_SOURCES` |
| TLV 协议常量 | `src/shared/protocol.h` | `src/shared/CMakeLists.txt` 的 `GRIDYARD_SHARED_HEADERS` |
| 应用控制器、模型、Controller/ViewModel 门面、业务编排 | `src/client/core/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_CORE_SOURCES` |
| 套接字适配器、连接对象、传输 Worker | `src/client/network/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_NETWORK_SOURCES` |
| QML 页面或组件 | `src/client/ui/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_QML_UI_FILES` |
| QML 内部 JavaScript 工具 | `src/client/utils/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_UTIL_FILES` |
| 图标和轻量资源 | `src/client/icons/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_ICON_RESOURCES` |
| 领域记录和仓储接口头 | `src/client/domain/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_DOMAIN_HEADERS` |
| 数据层代管者、数据库 Worker | `src/client/storage/` | `src/client/CMakeLists.txt` 的 `GRIDYARD_CLIENT_STORAGE_SOURCES`（需要它们的测试同样直编该目录源文件） |
| 服务端实现 | `src/rendezvous/` | `src/rendezvous/CMakeLists.txt` 的 `gy_rendezvous` 源清单 |
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
| 目录名 | `core/`、`client/network/` | 人阅读源码时的业务分区 |
| target 名 | `gy_core`、`gy_net_core` | CMake 编译、链接和依赖管理 |
| IDE 分组 | `client/core`、`client/ui` | 在 IDE 文件树中降低平铺感 |

日常开发只需要关注目录名；只有改 CMake、链接测试或排查构建问题时，才需要关心 target 名。
