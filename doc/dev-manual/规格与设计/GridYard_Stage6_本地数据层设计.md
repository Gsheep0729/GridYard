# GridYard Stage 6 本地数据层设计

| 字段 | 内容 |
| :--- | :--- |
| 设计版本 | v1.2 |
| 日期 | 2026-06-28 |
| 适用阶段 | Stage 6 本地数据层与体验打磨 |
| 当前状态 | 阶段 A~G 已验收完成，v6.7.0 补充 LocalDataBroker 代管层与 Repository 命名对齐 |

## 1. 目标与边界

Stage 6 为客户端建立独立的数据管理层。应用退出后，已连接设备目录、聊天消息和已结束传输记录仍可查询；Stage 5 的在线 P2P 收发链路、发现协议和文件内容传输保持不变。

SQLite 只保存本机业务元数据，不上传文件，也不保存发送源文件的绝对路径。本阶段不实现账号登录、跨设备漫游、服务端同步、离线消息投递、群聊或文件内容入库。

### 1.1 功能范围

- 保存发现过且实际建立过业务会话的设备信息。
- 保存收发聊天消息，按对端设备和时间查询。
- 保存完成、失败、拒绝或取消的传输历史。
- 支持记录按设备、状态和时间筛选，以及清空和保留期限清理。
- 保持 QML 不直接使用 `QSqlDatabase`、SQL 或本地数据库文件。

### 1.2 非目标

- 不以数据库写入结果阻塞在线聊天和文件传输。
- 不把 SQLite 当作在线消息队列；对端离线仍按 Stage 5 的规则拒绝发送。
- 不保存文件二进制内容、SHA-256 原始数据块或完整发送源路径。
- 不在 Stage 6 引入用户表、密码、token 或服务端连接。

## 2. DDD 分层与 Proxy 架构

Stage 6 采用适合桌面客户端的轻量 DDD，而不是为简单 CRUD 引入庞大聚合体系。领域层只定义记录值对象、状态约束和持久化端口；应用层编排用例；基础设施层的 SQLite Proxy 实现端口并隔离 Qt Sql。该结构参考 CourseSelectionSystem 的应用、领域、基础设施分层和数据代理思想，但保留 GridYard 的离线降级要求。

### 2.1 分层职责

| 层级 | 建议目录 | 责任 | 禁止内容 |
| :--- | :--- | :--- | :--- |
| 表现层 | `client/ui/` | 绑定模型、发出用户意图、展示历史状态 | SQL、`QSqlDatabase`、数据库路径 |
| 应用层 | `client/core/` | 编排保存、加载、清理和错误降级 | SQL 拼接、直接操作 `QSqlQuery` |
| 领域层 | `client/domain/` | `PeerRecord`、`MessageRecord`、`TransferRecord`、游标和状态规则 | Qt Sql、QML、网络 socket |
| 持久化端口 | `client/domain/` | `IDeviceRepository`、`IMessageRepository`、`ITransferHistoryRepository` | SQLite 类型、SQL 文本 |
| 基础设施层 | `client/storage/` | `SqliteDatabaseBroker`、各 Repository、`LocalDataBroker`、migration、行映射 | UI 逻辑、P2P 状态机 |

领域记录使用稳定 UUID、`device_id` 和 UTC 时间值，不直接复用 `QVariantMap`。`ChatManager` 与 `TransferSessionManager` 保持现有运行时职责，应用服务负责把它们的最终事件转换为领域记录。

### 2.2 依赖方向

```text
QML 页面
    │ 属性绑定、用户操作
    ▼
AppController / Controller 门面 / HistoryController（应用层）
    │ 通过 LocalDataBroker 编排持久化、查询和清理
    ▼
领域记录与 Repository 接口（领域层）
    │ 无 SQL、无 Qt Sql 依赖
    ▼
SqliteDeviceRepository / SqliteMessageRepository / SqliteTransferHistoryRepository
    │ 数据映射、预编译 SQL、事务
    ▼
SqliteDatabaseBroker / MigrationRunner
    │ 连接生命周期、事务、版本迁移
    ▼
DatabaseWorker（专用线程串行执行存储任务）
    ▼
SQLite 数据库文件
```

应用层依赖领域接口，基础设施层实现接口，不能反向依赖 `AppController`、QML 或网络 Worker。`client/storage/` 只依赖 Qt Core、Qt Sql 与领域值对象，不依赖页面组件、`QTcpSocket` 或传输 Worker。

### 2.3 Proxy 的职责边界

每个 Proxy 同时承担 Data Mapper：将查询行转换为领域记录，并将领域记录绑定到预编译 SQL 参数。Proxy 对外提供 `save`、`find`、`remove`、`clearExpired` 等对象化操作；不向应用层暴露“执行任意 SQL”的通用方法。

SQL 仅允许出现在 `client/storage/` 的 `.cpp` 文件中，且所有业务参数必须使用 `QSqlQuery::bindValue()`。禁止借鉴参考项目中 `std::format` 拼接 SQL 的做法，以防设备名称、聊天内容和错误文本造成注入或转义错误。

## 3. 数据库位置与连接策略

### 3.1 文件位置

默认数据库文件：

```text
QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
└── gridyard-history.sqlite
```

首次启动前创建父目录。测试环境必须通过专用构造参数或环境配置覆盖数据库路径，不能复用用户真实历史库。

### 3.2 连接生命周期

- `SqliteDatabaseBroker` 负责打开数据库、设置连接参数和运行 migration。
- 每个线程使用独立的命名 `QSqlDatabase` 连接；连接不能跨线程传递。
- UI 线程只持有列表模型和结果值，不直接执行耗时查询或批量删除。
- 写入和分页查询在专用数据库 Worker 线程中执行；结果通过值类型信号回到应用线程。
- 应用退出时先停止新任务，等待数据库 Worker 清空已提交任务，再关闭并移除连接名。

### 3.3 SQLite 参数

建连后统一执行：

```sql
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA busy_timeout = 5000;
```

WAL 降低读写互相阻塞的概率；`busy_timeout` 只处理短暂锁竞争，不能用来掩盖跨线程共享连接或长事务问题。单条消息和单条传输记录各自使用短事务，清空和保留清理使用批量短事务。

### 3.4 应用数据目录与运行日志

数据库和运行日志需要共享同一套应用数据路径规则，但不应互相依赖。新增位于 `client/core/` 的 `ApplicationPaths` 只负责基于 `QStandardPaths::AppDataLocation` 提供数据库目录、日志目录和测试覆盖路径；`Logger` 与 `SqliteDatabaseBroker` 都依赖它，`ApplicationPaths` 不依赖 Qt Sql。

Stage 6 需要把当前 Logger 的构建目录相对路径迁移到应用数据目录下的 `logs/`。迁移时保留可选的显式目录参数，便于测试和开发环境覆盖。数据库文件与日志文件必须分目录保存，不能把日志写入数据库目录根部。

存储日志统一使用 `[Storage]` 模块前缀，记录以下内容：驱动是否可用、数据库打开/关闭、migration 版本、事务失败类型、锁超时和降级状态。禁止记录聊天内容、完整 SQL、绑定参数、文件绝对路径或数据库中可识别的隐私数据；可记录脱敏后的记录 UUID 和错误码。

## 4. CMake 模块化与兼容性

### 4.1 目标划分

Stage 6 不应把 SQLite 源文件继续追加到 `client/CMakeLists.txt` 的 QML 模块源列表。新增两个子目录和目标：

| 目录 | 目标 | 类型 | 依赖 |
| :--- | :--- | :--- | :--- |
| `client/domain/` | `gy_domain` | 初期可为 `INTERFACE`，出现 `.cpp` 后改静态库 | Qt Core、`gy_shared` |
| `client/storage/` | `gy_storage` | 静态库 | `gy_domain`、Qt Core、Qt Sql |

`client/CMakeLists.txt` 在创建 QML 模块前添加 `add_subdirectory(domain)` 和 `add_subdirectory(storage)`，再让 `appGridYard` 私有链接 `gy_storage`。根 `src/CMakeLists.txt` 的 `find_package(Qt6 REQUIRED COMPONENTS ...)` 增加 `Sql`，但 `Qt6::Sql` 不传播到 `gy_shared`、网络 Worker 或 QML 文件。

测试单独链接 `gy_domain`、`gy_storage` 与 `Qt6::Sql`。数据库测试创建临时数据库文件，不依赖应用默认目录，也不与其他 Qt Test 目标共享连接名。

### 4.2 推荐 CMake 骨架

```cmake
# client/domain/CMakeLists.txt
add_library(gy_domain INTERFACE)
target_link_libraries(gy_domain INTERFACE Qt6::Core gy_shared)

# client/storage/CMakeLists.txt
add_library(gy_storage STATIC
    sqlite_database_proxy.h
    sqlite_database_proxy.cpp
    sqlite_device_proxy.h
    sqlite_device_proxy.cpp
    sqlite_message_proxy.h
    sqlite_message_proxy.cpp
    sqlite_transfer_history_proxy.h
    sqlite_transfer_history_proxy.cpp
    migration_runner.h
    migration_runner.cpp
)
target_link_libraries(gy_storage PUBLIC gy_domain PRIVATE Qt6::Core Qt6::Sql)
target_compile_features(gy_storage PUBLIC cxx_std_23)

# client/CMakeLists.txt
add_subdirectory(domain)
add_subdirectory(storage)
target_link_libraries(appGridYard PRIVATE gy_storage)
```

实际文件名以阶段 A 最终接口为准，但目标边界不变：应用层不能因方便而直接链接或调用 `Qt6::Sql`。

### 4.3 QSQLITE 驱动与打包

编译链接 `Qt6::Sql` 不代表运行环境一定具备 SQLite 驱动。`SqliteDatabaseBroker::initialize()` 必须检查 `QSqlDatabase::drivers()` 包含 `QSQLITE`；缺失时记录 `[Storage]` 错误，向应用层报告历史不可用并进入无历史模式。

Linux 开发环境需安装 Qt SQLite 驱动包。AppImage、deb 和 pacman 打包验收必须检查 Qt 插件目录中存在 `sqldrivers/libqsqlite.so`，并在干净环境运行时验证驱动可用。不能只以“编译成功”判断数据库功能可发布。

SQLite 数据库文件只支持本机应用数据目录。禁止把默认数据库放在 NFS、SMB、同步盘或临时构建目录，以避免 WAL 锁和文件替换语义在非本地文件系统上失效。

### 4.4 损坏数据库恢复

初始化阶段如果检测到 SQLite 文件损坏，例如底层错误包含 `file is not a database` 或 `database disk image is malformed`，不能直接删除用户旧库。处理流程为：先关闭并移除当前命名连接，释放文件句柄；再把主库文件重命名为 `gridyard-history.sqlite.corrupt-{timestamp}`，同时尽量备份同组的 `-wal` 与 `-shm` 文件；最后创建新的空库并重新执行 migration。

损坏恢复只针对明确的 SQLite 文件损坏。缺少 `QSQLITE` 驱动、数据库目录不可创建、文件权限不足、锁超时或普通写入失败不触发备份重建，避免把临时环境问题误判为库损坏。恢复过程的日志只记录操作类型，不输出数据库绝对路径、SQL 文本、聊天正文或绑定参数。

## 5. Schema 与迁移

### 4.1 Migration 规则

数据库使用单调递增的 schema 版本。每个版本由一份 C++ migration 定义，全部在事务内执行：成功后写入版本，失败后回滚，不允许留下半完成表结构。

```sql
CREATE TABLE schema_version (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL
);
```

首版 schema 为版本 `1`。以后只能新增 migration，不能修改已经发布的 migration 内容；升级代码必须兼容已有数据库和空数据库两种路径。

### 4.2 设备目录表

`peer_devices` 保存曾被发现或参与过聊天、传输的对端快照。`device_id` 是稳定主键，名称和网络地址可随再次发现更新。

```sql
CREATE TABLE peer_devices (
    device_id TEXT PRIMARY KEY NOT NULL,
    device_name TEXT NOT NULL,
    last_ip_address TEXT,
    last_tcp_port INTEGER,
    first_seen_at TEXT NOT NULL,
    last_seen_at TEXT NOT NULL,
    last_chat_at TEXT,
    last_transfer_at TEXT
);
```

保存设备目录不等于保存在线状态。在线状态继续由 `DiscoveryService` 的内存节点表决定；数据库中的时间只用于历史排序、最近联系设备和离线浏览。

### 4.3 聊天会话与消息表

单聊以对端 `device_id` 为会话键。`chat_conversations` 提供稳定会话行和清理边界，`chat_messages` 保存每条消息。消息 ID 复用 Stage 5 UUID，作为幂等键和未来漫游键。

```sql
CREATE TABLE chat_conversations (
    peer_device_id TEXT PRIMARY KEY NOT NULL,
    created_at TEXT NOT NULL,
    last_message_at TEXT NOT NULL,
    FOREIGN KEY (peer_device_id) REFERENCES peer_devices(device_id)
        ON DELETE CASCADE
);

CREATE TABLE chat_messages (
    message_id TEXT PRIMARY KEY NOT NULL,
    peer_device_id TEXT NOT NULL,
    direction INTEGER NOT NULL CHECK (direction IN (0, 1)),
    sender_device_id TEXT NOT NULL,
    sender_name TEXT NOT NULL,
    content TEXT NOT NULL,
    sent_at TEXT NOT NULL,
    local_status INTEGER NOT NULL,
    created_at TEXT NOT NULL,
    FOREIGN KEY (peer_device_id) REFERENCES chat_conversations(peer_device_id)
        ON DELETE CASCADE
);

CREATE INDEX idx_chat_messages_peer_time
    ON chat_messages(peer_device_id, sent_at DESC, message_id DESC);
```

方向 `0` 表示收到，`1` 表示发出。`local_status` 与 Stage 5 模型状态对应，用于保留发送失败或已写入连接的结果；首版不把它解释为送达回执。

### 4.4 传输历史表

一条记录对应一个传输会话，不展开每个文件的完整清单。需要展示文件夹时保存根名称、文件数量和总字节数；接收路径可保存相对展示名称，不能保存发送源绝对路径。

```sql
CREATE TABLE transfer_history (
    record_id TEXT PRIMARY KEY NOT NULL,
    session_id TEXT NOT NULL UNIQUE,
    peer_device_id TEXT NOT NULL,
    peer_name TEXT NOT NULL,
    direction INTEGER NOT NULL CHECK (direction IN (0, 1)),
    display_name TEXT NOT NULL,
    is_directory INTEGER NOT NULL CHECK (is_directory IN (0, 1)),
    file_count INTEGER NOT NULL,
    total_bytes INTEGER NOT NULL,
    status TEXT NOT NULL,
    started_at TEXT NOT NULL,
    finished_at TEXT,
    error_code INTEGER,
    error_message TEXT,
    FOREIGN KEY (peer_device_id) REFERENCES peer_devices(device_id)
        ON DELETE RESTRICT
);

CREATE INDEX idx_transfer_history_peer_time
    ON transfer_history(peer_device_id, started_at DESC, record_id DESC);
CREATE INDEX idx_transfer_history_status_time
    ON transfer_history(status, started_at DESC);
```

### 4.5 数据保留与删除

- 默认保留期限为永久，配置值 `0` 表示不自动清理。
- 用户可按会话删除聊天记录、按条件删除传输记录，或清空全部本地历史。
- 删除设备目录前必须先处理其关联会话和传输记录；默认清空历史时保留设备目录，以便保留最近设备体验。
- 保留清理按 `sent_at`、`started_at` 分批执行，并在完成后刷新对应模型。

## 6. Repository 接口与 Proxy 实现

### 5.1 持久化端口

领域层定义以下接口，应用层只持有接口引用或注入后的对象：

```text
IDeviceRepository
IMessageRepository
ITransferHistoryRepository
```

接口参数和返回值使用 `PeerRecord`、`MessageRecord`、`TransferRecord`、`MessageCursor` 与 `TransferQuery` 等领域值类型。基础设施层分别由 `SqliteDeviceRepository`、`SqliteMessageRepository`、`SqliteTransferHistoryRepository` 实现。应用层通过 `LocalDataBroker` 编排存储任务，不直接持有 Repository 或 `DatabaseWorker`。测试可使用内存实现替代接口，不需要连接 SQLite。

### 5.2 LocalDataBroker

`LocalDataBroker` 收拢 `SqliteDatabaseBroker`、三个 Repository 和 `DatabaseWorker` 的创建与生命周期管理。应用层（`AppController`、`HistoryController`）通过 `LocalDataBroker` 提供的语义化方法访问本地历史，避免数据管理层细节泄漏到应用层。

```text
bool initialize(const QString &databasePath, QString *errorMessage)
bool isAvailable() const
void persistDiscoveredPeer(const PeerInfo &peer)
void persistChatMessage(const MessageRecord &record, const QVariantMap &endpoint)
void persistTransferRecord(const TransferRecord &record, const QVariantMap &endpoint)
void loadRecentChatHistories(QObject *receiver, const ChatHistoriesCallback &callback)
void loadRecentTransferHistories(QObject *receiver, const TransferHistoriesCallback &callback)
void loadRecentPeers(QObject *receiver, int limit, const PeersCallback &callback)
void loadMessages(QObject *receiver, const MessageCursor &cursor, int limit, const MessagesCallback &callback)
void queryTransfers(QObject *receiver, const TransferQuery &query, int limit, const TransfersCallback &callback)
void deleteMessage(QObject *receiver, const QString &messageId, const OperationCallback &callback)
void deleteConversation(QObject *receiver, const QString &deviceId, const OperationCallback &callback)
void deleteTransfer(QObject *receiver, const QString &recordId, const OperationCallback &callback)
void clearAllMessages(QObject *receiver, const OperationCallback &callback)
void clearAllTransfers(QObject *receiver, const OperationCallback &callback)
void deleteExpiredRecords(const QDateTime &before)
void beginShutdown()
void closeStorage()
```

`LocalDataBroker` 在专用存储线程中持有 `DatabaseWorker`，所有数据库操作通过 `submitSave`/`submitLoad`/`submitDelete` 串行执行。回调通过 `QObject` 生命周期保护确保接收方销毁后不会悬挂。

### 5.3 SqliteDatabaseBroker

职责：初始化路径、创建命名连接、执行参数设置、migration 与健康检查。它不包含聊天或传输业务 SQL，也不向 QML 或应用层暴露任意 SQL 执行能力。

```text
bool initialize(const QString &databasePath, QString *errorMessage)
QSqlDatabase connectionForWorkerThread(QString *errorMessage)
void closeConnectionForCurrentThread()
bool runInTransaction(const TransactionTask &task, QString *errorMessage)
int schemaVersion() const
bool isAvailable() const
```

`runInTransaction()` 只在基础设施层内部为跨表操作使用。例如保存消息时先确保设备与会话存在，再插入消息并更新会话时间；应用层不手动提交事务。

### 5.4 SqliteDeviceRepository

职责：写入发现到的设备快照、读取最近联系设备、在聊天或传输完成时更新时间字段。

```text
upsertPeer(const PeerRecord &record, QString *errorMessage)
markChatActivity(const QString &deviceId, const QDateTime &time, QString *errorMessage)
markTransferActivity(const QString &deviceId, const QDateTime &time, QString *errorMessage)
recentPeers(int limit, QString *errorMessage)
```

设备发现频繁更新时应做节流：名称、地址和端口未变化且距离上次持久化不足设定间隔时不写库。实际业务成功或收到消息时必须更新活动时间。

### 5.5 SqliteMessageRepository

职责：以 `message_id` 幂等写入消息、按设备分页加载、删除和保留清理。消息写入失败不能撤销已经完成的网络发送，只记录日志并向界面报告“本地保存失败”。

```text
saveMessage(const MessageRecord &record, QString *errorMessage)
loadMessages(const MessageCursor &cursor, int limit, QString *errorMessage)
deleteMessage(const QString &messageId, QString *errorMessage)
deleteConversation(const QString &deviceId, QString *errorMessage)
deleteExpiredMessages(const QDateTime &before, QString *errorMessage)
clearAllMessages(QString *errorMessage)
```

启动恢复时先加载最近一页消息并填充 `ChatMessageModel`；向上滚动时按游标继续加载，避免一次性读取全部历史。

### 5.6 SqliteTransferHistoryRepository

职责：在传输进入最终状态时幂等写入或更新历史，并提供筛选、删除和保留清理。

```text
upsertFinishedTransfer(const TransferRecord &record, QString *errorMessage)
queryTransfers(const TransferQuery &query, int limit, QString *errorMessage)
deleteTransfer(const QString &recordId, QString *errorMessage)
deleteExpiredTransfers(const QDateTime &before, QString *errorMessage)
clearAllTransfers(QString *errorMessage)
```

进行中的传输仍由 `TransferSessionManager` 管理。数据库只保存最终或需要跨重启展示的状态，避免把高频进度更新写入 SQLite。

## 7. 应用事件与一致性

### 6.1 聊天写入时机

1. `ChatManager` 创建本地消息后，先更新内存模型为发送中。
2. 消息成功写入 socket 后更新内存状态，并异步提交持久化任务。
3. 收到有效消息后先去重并进入内存模型，再异步提交持久化任务。
4. Repository 使用 `message_id` 主键去重；重复任务视为成功，不重复显示。

本地持久化失败不影响在线聊天可用性，但需要通过 `HistoryController` 或非阻塞提示让用户知道记录未保存。

### 6.2 传输写入时机

传输开始、进度和临时失败不持续写库。`TransferSessionManager` 收到最终完成、失败、取消或拒绝事件后，构造不可变历史快照交给 Repository。该快照必须包含对端 ID，不能只依赖显示名称。

### 6.3 设备目录写入时机

`DiscoveryService` 发现新设备或设备元数据变化时通知应用逻辑层；持久化层进行节流写入。聊天和传输事件额外标记最近活动时间，保证离线时仍能列出真实使用过的设备。

## 8. 错误处理与恢复

| 场景 | 处理 |
| :--- | :--- |
| 数据库目录不可创建 | 记录错误，禁用历史功能，P2P 主链路继续运行 |
| 打开数据库失败 | 不反复创建连接，提示本地历史不可用，支持下次启动重试 |
| migration 失败 | 回滚当前迁移并保留旧版本；不执行后续 Repository 操作 |
| 数据库锁超时 | 单次任务失败并记录日志，不阻塞网络线程；允许后续任务重试 |
| 单条记录违反约束 | 记录结构化错误，拒绝该任务，不影响其他消息或传输 |
| 历史库损坏 | 先备份为 `.corrupt-{timestamp}`，再重建空库并提示历史已降级 |

## 9. 安全与隐私

- 数据库位于当前操作系统用户的应用数据目录，依赖文件系统权限隔离。
- 聊天内容默认本地明文保存；如后续加入数据库加密，必须通过独立 migration 和用户可见设置完成。
- 传输历史只保存展示所需元数据，不保存文件内容、发送源绝对路径或网络数据块。
- 任何 Stage 7 漫游字段均不在本阶段启用；未来同步前须明确用户授权和 TLS 通道。

## 10. 验收口径

- 空数据库首次启动、旧库重复启动和连续 migration 都可预测完成。
- 缺失 SQLite 驱动时进入无历史模式，应用不崩溃。
- 损坏数据库会被备份后重建，不静默删除旧文件。
- 数据库锁或单次写入失败只影响当前历史任务，P2P 主链路继续运行。
- 两个设备产生的聊天和传输记录在重启后能按设备、时间正确查询。
- 删除、清空和保留清理只影响本地 SQLite，不影响在线 P2P 连接、发现和实际文件。
- 数据库故障时，在线聊天与文件传输仍可继续使用。
- QML 不直接出现 `QSqlDatabase`、SQL 文本或数据库文件访问。

## 11. 后续扩展

Stage 7 可在当前 UUID、设备 ID 和时间字段之上增加同步游标、同步状态和服务端版本号。扩展必须通过新的 migration 完成，不能改变 Stage 6 已发布表的既有语义。
