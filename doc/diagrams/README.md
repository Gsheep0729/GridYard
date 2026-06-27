# GridYard 架构图

本目录记录 GridYard 的架构视图。当前基线为 v6.6.2：Stage 0~5 的局域网 P2P 文件传输与在线聊天已完成，Stage 6 本地数据层（SQLite 设备目录、消息与传输历史、异步 Worker、保留期限清理）已交付，并完成托盘后台、非阻塞通知与历史恢复验收。在线聊天复用 P2P 通道（Type 码 `0x0501` ChatText / `0x0502` ChatAck），不依赖服务端或登录。

## 目录结构

```text
doc/diagrams/
├── README.md                              # 本文件：架构图目录索引
├── 类图-应用逻辑层与上下层边界.vpp         # Visual Paradigm 工程文件
└── rendered/                              # 渲染后的架构图（SVG + PNG）
    ├── 01-类图-应用组装与传输会话管理.svg/png
    ├── 02-类图-共享协议与传输运行时.svg/png
    ├── 03-类图-QML表现层与基础设施.svg/png
    ├── 04-类图-在线聊天链路.svg/png
    ├── 05-类图-本地数据层.svg/png
    ├── 06-组件图-四层架构与外部资源.svg/png
    ├── 07-组件图-CMake构建与QML注册边界.svg/png
    ├── 08-组件图-接收连接资源所有权与首帧路由.svg/png
    ├── 09-组件图-本地数据层组件协作.svg/png
    ├── 10-模块依赖图-分层依赖方向.svg/png
    ├── 11-模块依赖图-具体头文件依赖.svg/png
    ├── 12-模块依赖图-QML单向数据流与封装契约.svg/png
    ├── 13-部署图-双端客户端端口与外部资源.svg/png
    ├── 14-线程图-线程亲和性与跨线程通信.svg/png
    ├── 15-协议图-TLV帧结构.svg/png
    ├── 16-协议图-FrameCodec解码状态机.svg/png
    ├── 17-协议图-Type码与帧职责.svg/png
    ├── 18-协议图-关键JSON载荷关系.svg/png
    ├── 19-协议图-版本兼容决策.svg/png
    ├── 20-协议图-Payload限制与错误码.svg/png
    ├── 21-时序图-应用启动与对象组装.svg/png
    ├── 22-时序图-文件发送.svg/png
    ├── 23-时序图-文件接收与首帧分流.svg/png
    ├── 24-时序图-取消与协议失败.svg/png
    ├── 25-时序图-在线聊天收发.svg/png
    ├── 26-时序图-聊天消息持久化与历史恢复.svg/png
    ├── 27-数据流图-设备发现.svg/png
    ├── 28-数据流图-帧校验错误码与清理.svg/png
    ├── 29-数据流图-文件发送.svg/png
    ├── 30-数据流图-文件接收.svg/png
    ├── 31-数据流图-配置日志与异步持久化.svg/png
    ├── 32-数据流图-在线聊天消息流.svg/png
    ├── 33-数据流图-历史查询删除与保留清理.svg/png
    ├── 34-状态图-发送会话.svg/png
    ├── 35-状态图-接收会话.svg/png
    └── 36-状态图-状态与用户操作约束.svg/png
```

每张图同时提供 SVG（无损缩放）和 PNG（固定分辨率）两种格式。

`类图-应用逻辑层与上下层边界.vpp` 是 Visual Paradigm 工程文件，可用于编辑和重新导出类图。

## 图表索引

### 类图（01-05）

展示项目中所有 C++ 类、Q_GADGET 值类型、Repository 端口和 QML 组件的属性、方法、信号及依赖关系。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 01 | 01-类图-应用组装与传输会话管理 | AppController、ConfigManager、TransferSessionManager、DiscoveryService、P2pServer 及 SessionRecord 的完整属性与方法；应用层持有的 storage 全家桶、HistoryController 与 retentionTimer；ConfigManager 的 isMyDevice / fillHelloPayload / fillSenderInfo 等委托方法；DiscoveryService 的 transferEndpoint 快照查询。 |
| 02 | 02-类图-共享协议与传输运行时 | gy::protocol 命名空间（Type 码含 ChatText / ChatAck、聊天 JSON 字段、Payload 上限、ErrorCode）、FrameCodec、PeerInfo、FileEntry、FileItem、FileSenderWorker、FileReceiverWorker、DirSerializer 的完整定义；Worker 通过 receiveRequestSnapshot() 一次交付快照，不暴露内部状态 getter。 |
| 03 | 03-类图-QML表现层与基础设施 | Main.qml、PeerListView、DeviceCard、DeviceSessionView、ChatView、TransferHistoryView、FileTypeIcon、AcceptDialog、SettingsDialog、TransferTaskCard 的属性与信号；FormatUtils.js、Style.js 工具；Logger、ApplicationPaths、QSettings 基础设施；QML id 不带 tw_ 前缀。 |
| 04 | 04-类图-在线聊天链路 | gy::ChatMessage 与 ChatMessageCodec / ChatMessageError、ChatManager、ChatMessageModel、ChatConnection 的完整属性、方法与信号；ChatManager 经 P2pServer.chatConnectionReceived 接管 socket，按 deviceId 维护可复用连接与稳定模型。 |
| 05 | 05-类图-本地数据层 | PeerRecord / MessageRecord / TransferRecord / MessageCursor / TransferQuery 值类型；IDeviceRepository / IMessageRepository / ITransferHistoryRepository 端口；SqliteDatabaseProxy / MigrationRunner / DatabaseWorker 基础设施；SqliteDeviceProxy / SqliteMessageProxy / SqliteTransferHistoryProxy 实现；HistoryController 作为应用层入口聚合端口。 |

### 组件图（06-09）

展示项目的物理模块划分、构建依赖、首帧路由和数据层组件协作。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 06 | 06-组件图-四层架构与外部资源 | src/client 与 src/shared 的四层架构（表现层、应用逻辑层、领域层、数据管理层）和共享协议层；与外部资源（QSettings、SQLite、接收目录、日志与数据目录、局域网）的连接。 |
| 07 | 07-组件图-CMake构建与QML注册边界 | CMakeLists.txt 构建链路；gy_shared 静态库、gy_domain INTERFACE 库、gy_storage 静态库、appGridYard 可执行文件、qt_add_qml_module 的注册关系；Qt6 依赖；test_* 测试目标。 |
| 08 | 08-组件图-接收连接资源所有权与首帧路由 | P2pServer 入站连接的资源所有权：monitorFirstFrame + routeFirstFrame 使用 peek 副本按 Type 分流；TransferReq 才创建后台 QThread 和 FileReceiverWorker；ChatText 直接交给主线程的 ChatManager；socket、FrameCodec、QTimer、文件 I/O 的线程归属与 deleteLater 生命周期管理。 |
| 09 | 09-组件图-本地数据层组件协作 | AppController 持有 storage 与 storageThread；HistoryController 通过 Repository 端口访问 SqliteDeviceProxy / SqliteMessageProxy / SqliteTransferHistoryProxy；DatabaseWorker 串行执行保存、加载、删除任务；ChatManager 与 TransferSessionManager 通过 AppController 异步投递持久化请求。 |

### 模块依赖图（10-12）

展示分层依赖方向、头文件依赖和 QML 单向数据流。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 10 | 10-模块依赖图-分层依赖方向 | 表现层→应用逻辑层→领域层→数据管理层→共享协议层的单向依赖；下层不依赖上层；gy_storage 实现 gy_domain 的 Repository 端口。 |
| 11 | 11-模块依赖图-具体头文件依赖 | 各 .h 文件之间的 include 和前置声明关系，覆盖 chat_*、history_*、sqlite_*、application_paths 等新增头文件；实现文件承担具体依赖。 |
| 12 | 12-模块依赖图-QML单向数据流与封装契约 | 用户操作→QML 意图→Q_INVOKABLE / 信号→业务对象→Q_PROPERTY + NOTIFY→QML 绑定的单向数据流；应用层只通过 transferEndpoint / receiveRequestSnapshot 等语义化方法或值快照获取数据，避免散点 getter。 |

### 部署图与线程图（13-14）

展示双端部署拓扑、端口分配和线程亲和性。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 13 | 13-部署图-双端客户端端口与外部资源 | 发送端与接收端的组件分布；UDP 45678 设备发现、TCP 35100 文件传输与聊天共享端口的分流；QSettings、SQLite、接收目录、日志与数据目录等本地资源。 |
| 14 | 14-线程图-线程亲和性与跨线程通信 | 主线程（UI、TransferSessionManager、ChatManager、HistoryController、P2pServer、ChatConnection）与后台 QThread（FileSenderWorker、FileReceiverWorker、DatabaseWorker）的跨线程信号通信；QueuedConnection 的使用场景。 |

### 协议图（15-20）

展示 TLV 帧格式、Type 码、JSON 载荷和错误码体系。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 15 | 15-协议图-TLV帧结构 | 8 字节帧头（Type 4B + Length 4B，大端序）+ 变长 Payload 的帧格式。 |
| 16 | 16-协议图-FrameCodec解码状态机 | WaitingHeader / WaitingPayload 两状态机；粘包/半包处理逻辑；按 Type 分级 Payload 上限检查。 |
| 17 | 17-协议图-Type码与帧职责 | V1.0 全部 Type 码（Hello、TransferReq/Rsp、DataChunk、ChunkAck、TransferDone、Cancel、ChatText、ChatAck）的方向与载荷说明。 |
| 18 | 18-协议图-关键JSON载荷关系 | TransferReq、FileItem、TransferRsp、ChunkAck、ChatMessage 的 JSON 字段定义与关联。 |
| 19 | 19-协议图-版本兼容决策 | protocol_version 主版本号不匹配→拒绝；次版本号不匹配→安全降级的决策流程；聊天帧同样要求 major 一致。 |
| 20 | 20-协议图-Payload限制与错误码 | DataChunk 256MB / 控制帧 1MB / ChatText 64KB 的 Payload 上限；单条消息 4000 字符上限；ErrorCode 枚举（1000 连接、2000 帧、3000 协议、4000 I/O、5000 用户、9000 未知）。 |

### 时序图（21-26）

展示关键业务流程的调用时序。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 21 | 21-时序图-应用启动与对象组装 | main.cpp→Logger→ApplicationPaths→QQmlApplicationEngine→ConfigManager→AppController→SqliteDatabaseProxy+Migration→DatabaseWorker→DiscoveryService→P2pServer→TransferSessionManager→ChatManager→HistoryController→retentionTimer 的初始化时序。 |
| 22 | 22-时序图-文件发送 | QML→TransferSessionManager→Discovery.transferEndpoint→FileSenderWorker 的完整发送流程：TCP 连接、TransferReq 握手、DataChunk 循环发送、TransferDone、finalizeSession 触发 transferToPersist 异步落库。 |
| 23 | 23-时序图-文件接收与首帧分流 | P2pServer 首帧路由：monitorFirstFrame→routeFirstFrame→TransferReq 才创建 QThread 和 FileReceiverWorker→initialize()→TransferReq 解析→receiveRequestSnapshot→用户确认→DataChunk 接收→ChunkAck 校验；ChatText 走 chatConnectionReceived 交给 ChatManager；未知首帧关闭。 |
| 24 | 24-时序图-取消与协议失败 | 用户取消→Cancel 帧→双端清理；帧超限→errorOccurred→cleanup→transferFinished 的异常处理流程。 |
| 25 | 25-时序图-在线聊天收发 | 本端发送：QML→ChatManager.sendText→Discovery.transferEndpoint→ChatConnection.sendMessage→FrameCodec→TCP，messageWritten 后通过 AppController 异步提交 SqliteMessageProxy。对端到达：P2pServer.chatConnectionReceived→ChatConnection.adoptSocket→ChatMessageCodec.decode→ChatManager.appendMessage→incomingMessageReceived。 |
| 26 | 26-时序图-聊天消息持久化与历史恢复 | 启动期：AppController→DatabaseWorker 异步查询 recentPeers 和按设备 loadMessages，通过 HistoryController 把更早一页消息 prependHistoryMessages 回 ChatManager；运行期：ChatManager.messageToPersist 与 TransferSessionManager.transferToPersist 异步落库；retentionTimer 触发 cleanupExpiredRecords。 |

### 数据流图（27-33）

展示数据在各模块间的流转路径。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 27 | 27-数据流图-设备发现 | ConfigManager.fillHelloPayload→DiscoveryService→UDP 广播→PeerInfo 哈希表→QML peers 绑定；peerUpdated 经 AppController 异步投递 SqliteDeviceProxy，30 秒发现节流。 |
| 28 | 28-数据流图-帧校验错误码与清理 | TCP 字节流→FrameCodec→按 Type 上限校验→Worker→cleanup（socket/timer/文件）→TransferSessionManager→QML 错误通知。 |
| 29 | 29-数据流图-文件发送 | 用户选择文件→QML→TransferSessionManager→Discovery.transferEndpoint→ConfigManager.fillSenderInfo→FileSenderWorker→DirSerializer→FrameCodec→TCP 对端；finalizeSession 触发 transferToPersist。 |
| 30 | 30-数据流图-文件接收 | TCP 对端→P2pServer→monitorFirstFrame 路由→FileReceiverWorker→receiveRequestSnapshot→TransferSessionManager→AcceptDialog→接收目录；transferToPersist 落库。 |
| 31 | 31-数据流图-配置日志与异步持久化 | SettingsDialog→ConfigManager↔QSettings；Logger→ApplicationPaths.logDir→日志文件；FrameCodec→Worker→TransferSessionManager→QML 错误通知；ChatManager 与 TransferSessionManager 通过 DatabaseWorker 异步落库到 SQLite messages / transfers / peers 表。 |
| 32 | 32-数据流图-在线聊天消息流 | 本端发送：Composer 输入→ChatManager→ChatConnection→ChatMessageCodec→FrameCodec→TCP；messageWritten 后入 ChatMessageModel 并 messageToPersist 落库。对端到达：ChatText 帧→ChatMessageCodec.decode→ChatMessageModel→QML messagesChanged 与 incomingMessageReceived。 |
| 33 | 33-数据流图-历史查询删除与保留清理 | 查询：QML→HistoryController→DatabaseWorker→SqliteMessageProxy / SqliteTransferHistoryProxy→SQLite，回包通过 ChatManager.prependHistoryMessages 与 transfersChanged 还给 QML。删除：deleteMessage / deleteConversation / deleteTransfer / clearAll*，并同步清理运行期会话与消息。保留清理：retentionTimer→cleanupExpiredRecords→deleteExpiredMessages / deleteExpiredTransfers。 |

### 状态图（34-36）

展示传输会话的状态迁移和用户操作约束。

| 编号 | 文件名 | 内容 |
|:-----|:-------|:-----|
| 34 | 34-状态图-发送会话 | connecting→waiting_response→transferring→verifying→completed 的状态迁移；最终状态触发 finalizeSession 异步落库；各状态允许的用户操作（取消）。 |
| 35 | 35-状态图-接收会话 | validating_request→waiting_confirm→transferring→verifying→completed→removable 的状态迁移；自动接收与手动确认两条路径；finalizeSession 异步落库。 |
| 36 | 36-状态图-状态与用户操作约束 | 各状态下允许的操作（接受/拒绝/取消/移除/删除文件/按设备清空）与结果的约束矩阵；保留期限到期触发的后台清理。 |

## 当前架构要点

- QML 只通过 `AppController` 与 `ConfigManager` 单例访问 C++ 状态；`TransferSessionManager`、`ChatManager`、`HistoryController` 经 AppController 的 `transfer` / `chat` / `history` 属性暴露。
- 设备发现使用 UDP Hello；文件传输与在线聊天共享 TCP 端口（默认 35100），P2pServer 按首个完整 TLV 帧的 Type 分流到 `FileReceiverWorker` 或 `ChatManager`。
- 文件发送在独立 `QThread` 的 `FileSenderWorker` 中运行；每个入站文件连接由 `P2pServer` 创建独立接收线程和 `FileReceiverWorker`；聊天连接由主线程的 `ChatManager` 直接接管，不创建额外线程。
- 接收端文件 I/O、SHA-256 校验和超时计时均在接收后台线程完成；主线程只更新会话模型并驱动 QML。
- 应用层与下层之间通过值快照交互：`DiscoveryService.transferEndpoint()` 返回对端不可变 `QVariantMap`，`FileReceiverWorker.receiveRequestSnapshot()` 一次交付完整会话信息，避免拆分 getter 调用。
- 本地数据层采用轻量 DDD：`src/client/domain/` 提供 Repository 端口与值类型，`src/client/storage/` 以 SQLite Proxy + Data Mapper 实现；所有 SQL 写操作通过参数绑定，禁止字符串拼接业务参数。
- `DatabaseWorker` 持有专用 `QThread` 串行执行保存、加载、删除任务；任务只访问自己线程的命名 SQLite 连接，不阻塞 UI 或网络收发。
- 聊天消息和传输历史在成功收发或会话结束后通过 `messageToPersist` / `transferToPersist` 异步落库；启动期通过 `loadRecentChatHistories` 和 `restoreFinishedTransfers` 把上一会话的历史回填到运行期模型。
- 配置 `retentionDays` 决定保留期限，`retentionTimer` 周期触发 `HistoryController.cleanupExpiredRecords` 删除早于阈值的 messages 与 transfers；清空操作可限定到当前 deviceId，不影响其他设备的历史。
