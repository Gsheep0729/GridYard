# GridYard 架构图

本目录记录 GridYard 的架构视图。当前基线为 v4.16.0：Stage 0~4 的局域网 P2P 文件传输已完成，Stage 5 仅完成界面前置改造；聊天、本地数据库和服务端尚未实施。

## 目录结构

```text
doc/diagrams/
├── README.md                              # 本文件：架构图目录索引
├── 类图-应用逻辑层与上下层边界.vpp         # Visual Paradigm 工程文件
└── rendered/                              # 渲染后的架构图（SVG + PNG）
    ├── 01-类图-应用组装与传输会话管理.svg/png
    ├── 02-类图-共享协议与传输运行时.svg/png
    ├── 03-类图-QML表现层与基础设施.svg/png
    ├── 04-组件图-四层架构与外部资源.svg/png
    ├── 05-组件图-CMake构建与QML注册边界.svg/png
    ├── 06-组件图-接收连接资源所有权.svg/png
    ├── 07-模块依赖图-分层依赖方向.svg/png
    ├── 08-模块依赖图-具体头文件依赖.svg/png
    ├── 09-模块依赖图-QML单向数据流.svg/png
    ├── 10-部署图-双端客户端端口与外部资源.svg/png
    ├── 11-线程图-线程亲和性与跨线程通信.svg/png
    ├── 12-协议图-TLV帧结构.svg/png
    ├── 13-协议图-FrameCodec解码状态机.svg/png
    ├── 14-协议图-Type码与帧职责.svg/png
    ├── 15-协议图-关键JSON载荷关系.svg/png
    ├── 16-协议图-版本兼容决策.svg/png
    ├── 17-协议图-Payload限制与错误码.svg/png
    ├── 18-时序图-应用启动与对象组装.svg/png
    ├── 19-时序图-文件发送.svg/png
    ├── 20-时序图-文件接收与后台线程.svg/png
    ├── 21-时序图-取消与协议失败.svg/png
    ├── 22-数据流图-设备发现.svg/png
    ├── 23-数据流图-帧校验错误码与清理.svg/png
    ├── 24-数据流图-文件发送.svg/png
    ├── 25-数据流图-文件接收.svg/png
    ├── 26-数据流图-配置日志与错误.svg/png
    ├── 27-状态图-发送会话.svg/png
    ├── 28-状态图-接收会话.svg/png
    └── 29-状态图-状态与用户操作约束.svg/png
```

每张图同时提供 SVG（无损缩放）和 PNG（固定分辨率）两种格式。

`类图-应用逻辑层与上下层边界.vpp` 是 Visual Paradigm 工程文件，可用于编辑和重新导出类图。

## 图表索引

### 类图（01-03）

展示项目中所有 C++ 类、Q_GADGET 值类型和 QML 组件的属性、方法、信号及依赖关系。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 01 | 01-类图-应用组装与传输会话管理 | AppController、ConfigManager、TransferSessionManager、DiscoveryService、P2pServer 及 SessionRecord 的完整属性与方法；应用逻辑层与领域层的组合/依赖关系。 |
| 02 | 02-类图-共享协议与传输运行时 | gy::protocol 命名空间（Type 码、ErrorCode、Payload 限制）、FrameCodec、PeerInfo、FileEntry、FileItem、FileSenderWorker、FileReceiverWorker、DirSerializer 的完整定义。 |
| 03 | 03-类图-QML表现层与基础设施 | Main.qml、PeerListView、DeviceCard、DeviceSessionView、AcceptDialog、SettingsDialog、TransferTaskCard 的属性与信号；FormatUtils.js、Style.js 工具；Logger、QSettings 基础设施。 |

### 组件图（04-06）

展示项目的物理模块划分、构建依赖和资源所有权。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 04 | 04-组件图-四层架构与外部资源 | src/client 与 src/shared 的模块划分；QML 表现层、应用逻辑层、领域层、共享层的组件及其与外部资源（QSettings、文件系统、局域网）的连接。 |
| 05 | 05-组件图-CMake构建与QML注册边界 | CMakeLists.txt 构建链路；gy_shared 静态库、appGridYard 可执行文件、qt_add_qml_module 的注册关系；Qt6 依赖。 |
| 06 | 06-组件图-接收连接资源所有权 | P2pServer 为每个入站 TCP 连接创建 QThread 和 FileReceiverWorker 的过程；socket、FrameCodec、QTimer、文件 I/O 的线程归属；moveToThread 与 deleteLater 的生命周期管理。 |

### 模块依赖图（07-09）

展示分层依赖方向、头文件依赖和 QML 数据流。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 07 | 07-模块依赖图-分层依赖方向 | 表现层→应用逻辑层→领域层→共享层的单向依赖；下层不依赖上层。 |
| 08 | 08-模块依赖图-具体头文件依赖 | 各 .h 文件之间的 include 和前置声明关系；实现文件承担具体依赖。 |
| 09 | 09-模块依赖图-QML单向数据流 | 用户操作→QML 意图→Q_INVOKABLE/信号→业务对象→Q_PROPERTY+NOTIFY→QML 绑定的单向数据流。 |

### 部署图与线程图（10-11）

展示双端部署拓扑、端口分配和线程亲和性。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 10 | 10-部署图-双端客户端端口与外部资源 | 发送端与接收端的组件分布；UDP 45678 设备发现、TCP 35100 文件传输的端口分配；QSettings、文件系统等本地资源。 |
| 11 | 11-线程图-线程亲和性与跨线程通信 | 主线程（UI、TransferSessionManager、P2pServer）与后台 QThread（FileSenderWorker、FileReceiverWorker）的跨线程信号通信；QueuedConnection 的使用场景。 |

### 协议图（12-17）

展示 TLV 帧格式、Type 码、JSON 载荷和错误码体系。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 12 | 12-协议图-TLV帧结构 | 8 字节帧头（Type 4B + Length 4B，大端序）+ 变长 Payload 的帧格式。 |
| 13 | 13-协议图-FrameCodec解码状态机 | WaitingHeader / WaitingPayload 两状态机；粘包/半包处理逻辑；按 Type 分级 Payload 上限检查。 |
| 14 | 14-协议图-Type码与帧职责 | 7 个 V1.0 Type 码（Hello、TransferReq/Rsp、DataChunk、ChunkAck、TransferDone、Cancel）的方向与载荷说明。 |
| 15 | 15-协议图-关键JSON载荷关系 | TransferReq、FileItem、TransferRsp、ChunkAck 的 JSON 字段定义与关联。 |
| 16 | 16-协议图-版本兼容决策 | protocol_version 主版本号不匹配→拒绝；次版本号不匹配→安全降级的决策流程。 |
| 17 | 17-协议图-Payload限制与错误码 | DataChunk 256MB / 控制帧 1MB 的 Payload 上限；ErrorCode 枚举（1000 连接、2000 帧、3000 协议、4000 I/O、5000 用户、9000 未知）。 |

### 时序图（18-21）

展示关键业务流程的调用时序。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 18 | 18-时序图-应用启动与对象组装 | main.cpp→Logger→QQmlApplicationEngine→ConfigManager→AppController→DiscoveryService→P2pServer→TransferSessionManager→Main.qml 的初始化时序。 |
| 19 | 19-时序图-文件发送 | QML→TransferSessionManager→FileSenderWorker 的完整发送流程：TCP 连接、TransferReq 握手、DataChunk 循环发送、TransferDone。 |
| 20 | 20-时序图-文件接收与后台线程 | P2pServer 接受连接→创建 QThread→FileReceiverWorker.initialize()→TransferReq 解析→用户确认→DataChunk 接收→ChunkAck 校验的完整流程。 |
| 21 | 21-时序图-取消与协议失败 | 用户取消→Cancel 帧→双端清理；帧超限→errorOccurred→cleanup→transferFinished 的异常处理流程。 |

### 数据流图（22-26）

展示数据在各模块间的流转路径。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 22 | 22-数据流图-设备发现 | ConfigManager→DiscoveryService→UDP 广播→PeerInfo 哈希表→QML peers 绑定的数据流。 |
| 23 | 23-数据流图-帧校验错误码与清理 | TCP 字节流→FrameCodec→按 Type 上限校验→Worker→cleanup（socket/timer/文件）→TransferSessionManager→QML 错误通知。 |
| 24 | 24-数据流图-文件发送 | 用户选择文件→QML→TransferSessionManager→FileSenderWorker→DirSerializer→FrameCodec→TCP 对端。 |
| 25 | 25-数据流图-文件接收 | TCP 对端→P2pServer→FileReceiverWorker→FrameCodec→TransferSessionManager→AcceptDialog→接收目录。 |
| 26 | 26-数据流图-配置日志与错误 | SettingsDialog→ConfigManager↔QSettings；Logger→日志文件；FrameCodec→Worker→TransferSessionManager→QML 错误通知。 |

### 状态图（27-29）

展示传输会话的状态迁移和用户操作约束。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 27 | 27-状态图-发送会话 | connecting→waiting_response→transferring→verifying→completed 的状态迁移；各状态允许的用户操作（取消）。 |
| 28 | 28-状态图-接收会话 | validating_request→waiting_confirm→transferring→verifying→completed→removable 的状态迁移；自动接收与手动确认两条路径。 |
| 29 | 29-状态图-状态与用户操作约束 | 各状态下允许的操作（接受/拒绝/取消/移除/删除文件）与结果的约束矩阵。 |

## 当前架构要点

- QML 只通过 `AppController` 与 `ConfigManager` 单例访问 C++ 状态；`TransferSessionManager` 由 `AppController.transfer` 暴露。
- 设备发现使用 UDP Hello；文件传输使用 TCP 和 `FrameCodec` 的 TLV 帧编解码。
- 文件发送在独立 `QThread` 的 `FileSenderWorker` 中运行；每个入站连接由 `P2pServer` 创建独立接收线程和 `FileReceiverWorker`。
- 接收端文件 I/O、SHA-256 校验和超时计时均在接收后台线程完成；主线程只更新会话模型并驱动 QML。
- 当前数据管理仅包括 `QSettings`、文件系统和日志文件；SQLite Repository 属于后续 Stage 6。
