# GridYard 业务逻辑说明

## 1. 设备发现流程

```mermaid
flowchart TD
    A[启动 GridYard] --> B[DiscoveryService 绑定 UDP 45678]
    B --> C[每 5 秒广播 Hello 包]
    C --> D{收到其他设备广播?}
    D -->|是| E[解析 JSON 更新设备列表]
    D -->|否| F[等待下次广播]
    E --> G{超过 15 秒未收到心跳?}
    G -->|是| H[标记设备离线]
    G -->|否| I[保持在线状态]
    H --> J[emit peersChanged]
    I --> J
    J --> K[QML 刷新设备列表]
```

**关键代码**：
- `DiscoveryService`：UDP 广播发现服务
- `ConfigManager`：配置管理（设备名、端口等）
- `PeerListView.qml`：设备列表 UI 组件

## 2. 文件传输流程

```mermaid
sequenceDiagram
    participant Sender as 发送端
    participant Receiver as 接收端

    Sender->>Sender: 1. 拖拽文件 / 选择文件
    Sender->>Sender: 2. TransferSessionManager.createSendSession()
    Sender->>Sender: 3. 创建 FileSenderWorker, moveToThread
    Sender->>Receiver: 4. connectToHost(IP, 35100)
    Receiver->>Receiver: 5. P2pServer 收到连接, 创建 FileReceiverWorker
    Sender->>Receiver: 6. 发送 TransferReq 帧 (文件名/大小/SHA-256)
    Receiver->>Receiver: 7. 弹出 AcceptDialog 确认
    Receiver-->>Sender: 8. 用户接受, 发送 TransferRsp 帧

    loop 逐文件 8MB 分块
        Sender->>Receiver: 9. 发送 DataChunk 帧
        Receiver->>Receiver: 10. 写入磁盘 + 增量 SHA-256
    end

    Receiver-->>Sender: 11. 文件完成, 发送 ChunkAck (SHA-256 校验)
    Sender->>Receiver: 12. 全部 ACK 通过, 发送 TransferDone
    Sender->>Sender: 13. 双端完成会话并显示结果
```

**关键代码**：
- `TransferSessionManager`：传输会话管理器
- `FileSenderWorker`：文件发送 Worker
- `FileReceiverWorker`：文件接收 Worker
- `P2pServer`：TCP 服务器，监听端口 35100

## 3. 界面布局

```mermaid
graph LR
    subgraph 主窗口
        subgraph 左侧[工具栏 62px]
            TB1[发现]
            TB2[设置]
        end
        subgraph 中间[设备列表 210px]
            DEV1[在线 设备A 192.168.1.100]
            DEV2[在线 设备B 192.168.1.101]
            DEV3[离线 设备C 192.168.1.102]
        end
        subgraph 右侧[会话页]
            HDR[Header: 设备名 + IP + 在线状态]
            subgraph Timeline
                TAB1[聊天]
                TAB2[传输]
                BODY[聊天气泡 / 传输任务卡片]
            end
            CMP[Composer: 文本输入 + 发送文件/文件夹]
        end
    end
```

三栏布局：左侧工具栏 + 中间设备列表 + 右侧会话页。会话页分为 Header（72px 设备信息）、Timeline（聊天/传输切换）和 Composer（72px 输入区域）三段式。

**关键代码**：
- `Main.qml`：根窗口，三栏布局（左侧工具栏 + 中间设备列表 + 右侧会话页）
- `PeerListView.qml`：设备列表组件
- `DeviceSessionView.qml`：会话页（Header / Timeline / Composer 三段式）
- `ChatView.qml`：聊天气泡列表
- `TransferHistoryView.qml`：传输历史视图
- `TransferTaskCard.qml`：单个传输任务卡片
- `AcceptDialog.qml`：接收确认弹窗

## 4. 使用方法

### 4.1 发送文件

1. 启动 GridYard
2. 从文件管理器拖拽文件到左侧设备卡片
3. 等待接收端确认
4. 传输进度在右侧面板显示

### 4.2 接收文件

1. 收到传输请求时弹出确认对话框
2. 点击"接受"开始接收
3. 接收完成后可点击"打开"查看文件

### 4.3 取消传输

- 点击任务卡片上的"取消"按钮
- 发送方和接收方都会收到取消通知

### 4.4 查看历史

- 聊天页默认显示最近消息，向上加载更早记录
- 传输历史支持按设备和状态筛选
- 用户可以删除单条消息、清空会话、删除传输历史或清空全部传输历史
- 历史管理只影响本地 SQLite 记录，不删除真实文件
- 保留期限可设置为永久、7 天、30 天或 90 天，过期清理不删除设备目录

## 5. 核心模块说明

### 5.1 DiscoveryService（设备发现）

- **功能**：UDP 广播发现局域网设备
- **端口**：45678
- **机制**：每 5 秒广播一次，超时 15 秒标记离线
- **数据**：设备名、IP 地址、TCP 端口

### 5.2 P2pServer（P2P 服务器）

- **功能**：TCP 服务器，监听文件传输和聊天请求
- **端口**：35100（可配置）
- **机制**：收到连接后按首帧 Type 分流——`kTypeTransferReq` 创建 FileReceiverWorker，`kTypeChatText` 交给 ChatManager

### 5.3 TransferSessionManager（传输会话管理）

- **功能**：管理所有进行中的传输会话
- **QML 门面**：`TransferController`（通过 `AppController.transferController` 访问）
- **职责**：
  - 创建发送/接收会话
  - 跟踪传输进度
  - 处理取消操作
  - 发射结束态快照供持久化

### 5.4 ChatManager（在线聊天管理）

- **功能**：管理在线聊天连接复用、内存消息会话和持久化投递
- **QML 门面**：`ChatController`（通过 `AppController.chatController` 访问）
- **职责**：
  - 按设备维护可复用 TCP 聊天连接
  - 消息去重和内存模型管理
  - 成功收发后提交本地历史

### 5.5 FileSenderWorker（文件发送）

- **功能**：在工作线程中发送文件
- **特性**：
  - 8MB 分块发送
  - SHA-256 校验
  - 超时检测（30 秒）
  - 进度信号节流

### 5.6 FileReceiverWorker（文件接收）

- **功能**：在工作线程中接收文件
- **特性**：
  - 接收数据落盘
  - SHA-256 校验
  - 超时检测（30 秒）
  - 资源清理

### 5.7 LocalDataBroker（本地数据层代管）

- **功能**：初始化 SQLite 存储、Repository 和数据库任务线程
- **职责**：
  - 收拢所有本地历史持久化、查询和清理入口
  - 专用存储线程串行执行数据库操作
  - 向应用层提供语义化接口，不暴露 SQL 细节

## 6. 协议格式

### 6.1 TLV 帧格式

| 字段 | Type (4 bytes) | Length (4 bytes) | Payload (Length bytes) |
|------|----------------|------------------|------------------------|
| 字节序 | 大端序 | 大端序 | — |

### 6.2 Type 码定义

| Type 码 | 名称 | 说明 |
|---------|------|------|
| 0x0001 | Hello | UDP 广播：设备上线/心跳 |
| 0x0101 | TransferReq | TCP：文件元数据握手请求 |
| 0x0102 | TransferRsp | TCP：握手响应（接受/拒绝） |
| 0x0201 | DataChunk | TCP：文件数据分块 |
| 0x0301 | ChunkAck | TCP：单文件完成确认与校验 |
| 0x0302 | TransferDone | TCP：全部文件发送完毕 |
| 0x0401 | Cancel | TCP：取消本次传输 |
| 0x0501 | ChatText | TCP：P2P 在线文本消息 |
| 0x0502 | ChatAck | TCP：在线文本消息送达回执 |

### 6.3 TransferRequest 帧格式

```json
{
  "session_id": "uuid",
  "sender_device_id": "发送方设备 UUID",
  "sender_name": "设备名",
  "total_files": 2,
  "total_bytes": 1024000,
  "files": [
    {
      "file_index": 0,
      "relative_path": "test.pdf",
      "size_bytes": 1024000,
      "sha256": "abc123..."
    }
  ]
}
```

### 6.4 DataChunk 帧格式

| 字段 | FileIndex (4B) | Offset (8B) | ChunkSize (4B) | IsLastChunk (4B) | Data (N bytes) |
|------|----------------|-------------|----------------|------------------|----------------|

## 7. 测试方法

### 7.1 单元测试

```bash
# 运行所有测试
ctest --test-dir build --output-on-failure

# 运行特定测试
./build/tests/test_transfer
```

### 7.2 本机回环测试

```bash
# 启动两个实例
./build/client/appGridYard --port 35100 --name "接收端" &
./build/client/appGridYard --port 35101 --name "发送端" &
```

### 7.3 测试用例

1. **单个 PDF 文件**：1-10MB，验证完整性
2. **多个文件**：2-3 个文件，验证批量传输
3. **大文件**：>100MB，验证进度显示
4. **取消传输**：传输中取消，验证清理
5. **拒绝接收**：接收端拒绝，验证错误处理

## 8. 日志系统

### 8.1 日志位置

- 日志文件：`logs/gridyard_yyyyMMdd.log`
- 控制台输出：运行时的终端窗口

### 8.2 日志格式

```
[2026-06-05 12:26:18.639] [DEBUG] [P2pServer] 监听端口 35100 成功
[2026-06-05 12:26:18.639] [WARNING] [FileReceiver] 无法创建文件: 权限不足
```

### 8.3 日志级别

- **DEBUG**：调试信息
- **INFO**：一般信息
- **WARNING**：警告信息
- **ERROR**：错误信息

## 9. 配置管理

### 9.1 配置文件位置

- 默认：`~/.config/CQNU-SED/GridYard.conf`
- 命令行指定：`--config <path>`

### 9.2 配置项

- `deviceName`：设备名称
- `tcpPort`：TCP 端口（默认 35100）
- `receivePath`：接收路径（默认 `~/GridYard/document`）
- `deviceId`：设备唯一标识（自动生成）

## 10. 常见问题

### 10.1 设备无法发现

- 检查防火墙是否允许 UDP 45678 端口
- 确认两台设备在同一局域网
- 检查日志是否有 DiscoveryService 错误

### 10.2 文件传输失败

- 检查防火墙是否允许 TCP 35100 端口
- 确认接收端在线且 P2pServer 启动成功
- 查看日志中的错误信息

### 10.3 传输速度慢

- 检查网络带宽
- 确认 socket buffer 已优化（4MB）
- 检查磁盘写入速度

## 11. 后续业务演进

### 11.1 P2P 在线聊天（已完成 Stage 5）

GridYard 复用现有设备发现与 P2P TCP 通道，在 V1.0 低位段新增文本 Type 码（`0x0501` ChatText、`0x0502` ChatAck），实现在线设备之间的文本聊天：

- 与文件传输共用监听端口，P2pServer 按首帧 Type 区分这是文件传输还是聊天连接。
- 仅支持向在线设备发送，对端离线时不可发送，消息不静默丢弃。
- 聊天网络与连接管理在 C++（`ChatManager`），QML 只负责气泡渲染与文本输入。
- 聊天消息已接入本机 SQLite 历史（Stage 6），跨设备漫游仍属于后续服务端阶段。
- QML 通过 `AppController.chatController` 门面访问聊天能力，不直接依赖 `ChatManager`。

### 11.2 LocalSend 设计借鉴

GridYard 保留当前 TLV 文件传输协议，不整体切换为 LocalSend REST。后续借鉴：

- 发现包增加协议版本、设备类型、稳定指纹和能力列表。
- 收到新设备上线声明后主动回应。
- 传输会话和单文件使用短期 token。
- 接收端支持部分接受文件列表。
- 增加最大帧长、并发会话数、请求频率和稳定错误码。
- 发现失败时支持手动 IP 或扫描兜底。
- 增加可选 PIN、TLS 或可信设备配对。

### 11.3 本地历史与异常恢复（已完成 Stage 6）

SQLite 本地历史不依赖登录，优先于服务端开发完成。聊天记录保存消息 ID、方向、发送方快照、内容和 UTC 时间；传输历史保存全局记录 ID、方向、文件显示名、总大小、状态、对端设备信息、开始/结束时间和错误信息。默认不保存或同步敏感源路径。QML 不直接查询数据库，应用层通过 `LocalDataBroker` 编排存储任务，基础设施层以 Repository 实现端口并隔离 Qt Sql。

数据库不可用时，应用进入无历史模式，设备发现、在线聊天和文件传输继续运行。缺少 `QSQLITE` 驱动、锁超时或单次写入失败只影响本地记录；如果启动时发现数据库文件损坏，先备份为 `.corrupt-{timestamp}`，再重建空库。

### 11.4 登录与记录漫游

登录用于建立稳定用户身份和跨设备同步，不作为 P2P 文件传输的前置条件。开启记录漫游后，客户端仅同步传输元数据，不上传文件内容；服务端按用户身份校验记录归属，并按全局记录 ID 去重。

用户可以关闭漫游、清空本地记录和删除服务端记录。登录与同步必须使用 TLS；密码使用专用密码哈希，不使用普通 SHA-256。

建议顺序：~~P2P 在线聊天~~ ✅ → ~~本地 SQLite 历史（聊天记录与传输历史）~~ ✅ → 登录与服务端部署 → 对话历史与传输记录漫游 → 离线消息漫游。
