# GridYard — 鸽邮

> 局域网 P2P 文件传输与即时通讯桌面应用，全程零公网流量。

当前版本：v7.8.0

| 项目版本 | v7.8.0 |
| :--- | :--- |

GridYard 是一款面向局域网场景的桌面文件传输与聊天工具。两台接入同一网段的电脑即可互相发现、直传文件与文件夹、收发文本消息，无需任何中心服务器、账号登录或公网连接。基于自研 TLV 二进制协议与 Qt6 全 QML 技术栈构建，支持多文件目录传输、SHA-256 完整性校验、断线自动重连与本地历史持久化。

---

## 核心特性

- **即时设备发现**：UDP 广播自动发现同网段设备，5 秒心跳保活，15 秒离线剔除，支持多网卡环境
- **P2P 直传**：TCP 点对点传输，单文件与多文件目录均支持，8MB 分块 + SHA-256 校验 + 背压控制
- **在线聊天**：复用 P2P 通道的文本消息收发，连接复用、消息去重、断线按需重连
- **协议自研**：TLV 二进制帧格式，按 Type 分级 Payload 上限，粘包/半包状态机，协议版本协商
- **本地持久化**：SQLite 存储聊天记录、传输历史与设备目录，重启可查；WAL 模式 + 异步写入不阻塞主链路
- **历史管理**：聊天记录分页加载，传输历史按设备/状态筛选，支持删除、清空和保留期限设置
- **桌面体验**：系统托盘后台运行，聊天和传输事件非阻塞通知，本地历史不可用时自动降级
- **零公网**：全程局域网通信，不依赖云服务、账号体系或第三方中转
- **跨文件系统安全**：路径穿越防护、磁盘空间预检、零字节文件处理、重名避让
- **校园网可达性增强**：协调服务器支持跨 AP 设备发现，Relay 中继支持极端网络隔离场景

---

## 技术栈

| 类别 | 选型 |
|:-----|:-----|
| 语言标准 | C++23 |
| GUI 框架 | Qt 6.11（全 QML 路线） |
| 构建系统 | CMake 4.2.3 + Ninja |
| 编译器 | GCC 16.1 |
| 数据库 | SQLite（WAL 模式） |
| 加密校验 | SHA-256（QCryptographicHash） |
| 部署平台 | Manjaro Linux |

---

## 快速开始

### 环境要求

- GCC 15+ 或 Clang 17+
- CMake 4.2.3+
- Qt 6.5+（含 Quick、Network、QuickControls2、Sql 模块）
- Ninja（推荐）
- Linux 桌面门户（KDE 使用 `xdg-desktop-portal-kde`）

### 构建与运行

```bash
git clone <仓库地址>
cd GridYard

# 配置（在 src 目录下）
cmake -S src -B src/build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build src/build-ninja -j

# 运行
./src/build-ninja/client/appGridYard
```

### 运行测试

```bash
ctest --test-dir src/build-ninja --output-on-failure
```

### 本机双实例测试

```bash
# 实例 A（接收端）
./src/build-ninja/client/appGridYard \
  --config ~/gridyard_alice.ini \
  --port 35100 \
  --name "接收端" &

# 实例 B（发送端）
./src/build-ninja/client/appGridYard \
  --config ~/gridyard_bob.ini \
  --port 35101 \
  --name "发送端" &
```

默认启动时，配置文件会写入系统配置目录。单机双实例测试时建议显式指定两份不同的稳定配置文件（如 `~/gridyard_alice.ini` 和 `~/gridyard_bob.ini`），避免两个实例共用同一份设备身份。

**命令行参数**

| 参数 | 说明 | 默认值 |
|:-----|:-----|:-------|
| `--port <port>` | TCP 监听端口 | 35100 |
| `--name <name>` | 设备显示名称 | 系统主机名 |
| `--config <path>` | 配置文件路径 | 见下方"运行时数据目录" |

### 运行时数据目录

程序运行时会自动创建子目录，存放配置、数据库和日志文件。这些数据都是缓存性质，删除后不影响程序编译和重新启动，但会丢失设备 ID、聊天记录和传输历史。

| 数据 | 位置规则 | 文件说明 |
|:-----|:---------|:---------|
| 配置文件 | 系统配置目录 | `gridyard.ini`：设备 ID、设备名、TCP 端口、接收路径等 |
| 数据库 | 系统应用数据目录的 `database/` 子目录 | `gridyard-history.sqlite`：聊天记录、传输历史、设备目录（SQLite WAL 模式） |
| 日志 | 系统应用数据目录的 `logs/` 子目录 | `gridyard_YYYYMMDD.log`：运行日志，按日期自动切换 |

Linux 示例：

| 数据 | 默认路径 |
|:-----|:---------|
| 配置文件 | `~/.config/CQNU-SED/GridYard/gridyard.ini` |
| 数据库 | `~/.local/share/CQNU-SED/GridYard/database/gridyard-history.sqlite` |
| 日志 | `~/.local/share/CQNU-SED/GridYard/logs/gridyard_YYYYMMDD.log` |

AppImage 和压缩包运行时也使用上述系统目录，不在发布包同级写入配置、数据库或日志。

使用 `--config` 参数时，配置文件路径以命令行指定的为准，数据库和日志仍在系统应用数据目录下。

**v6.8.1 最终发布验证**

| 测试项 | 结果 |
|:------|:-----|
| `--config` 参数 | 通过，`/tmp/config_test/test.ini` 正确创建 |
| 系统默认配置目录 | 通过，`~/.config/CQNU-SED/GridYard/gridyard.ini` 存在 |
| 系统默认数据库 | 通过，`~/.local/share/CQNU-SED/GridYard/database/` 有 SQLite 文件 |
| 系统默认日志 | 通过，`~/.local/share/CQNU-SED/GridYard/logs/` 有日志文件 |
| 设备别名持久化 | 通过，重启后 `name=GY-PC` 保持不变 |

结论：AppImage 使用系统标准目录策略，配置、数据库和日志都在用户目录下正确持久化。修改设备别名后重启不会丢失。

---

## 系统架构

GridYard 采用四层架构，职责严格隔离：

```mermaid
graph TD
    ENTRY["main.cpp<br/>QGuiApplication"]
    subgraph 表现层["表现层 · QML + JavaScript"]
        UI["Main.qml · DeviceSessionView<br/>ChatView · TransferPanel"]
    end
    subgraph 应用逻辑层["应用逻辑层 · Controller + Manager"]
        APP["AppController（组合根 / QML 单例）<br/>TransferSessionManager · ChatManager"]
    end
    subgraph 领域层["领域层 · 协议 + 网络 + 业务规则"]
        DOM["DiscoveryService · P2pServer<br/>FileSender/ReceiverWorker · ChatConnection<br/>FrameCodec · DirSerializer"]
    end
    subgraph 数据管理层["数据管理层 · 配置 + 日志 + 持久化"]
        DATA["ConfigManager · Logger<br/>LocalDataBroker<br/>SqliteDatabaseBroker · Repository"]
    end

    ENTRY -->|"显式创建"| APP
    APP -->|"初始化 / 持有 UI 引擎"| UI
    UI -->|"属性绑定 / 信号上报"| APP
    APP -->|"接口调用 / 事件回调"| DOM
    APP -->|"Broker 编排"| DATA
    DOM -->|"运行期数据"| APP
```

**分层约束**

- `main.cpp` 只负责创建 `QGuiApplication`、解析启动参数、初始化日志和显式创建 `AppController`
- `AppController` 是客户端组合根，负责初始化应用层对象和 UI 层
- QML 禁止直接访问 `QSqlDatabase`、SQL 或文件系统
- 应用层只依赖 Repository 接口，不感知 SQLite 实现细节
- SQL 仅存在于 `client/storage/*.cpp`，所有参数通过 `bindValue()` 绑定
- 数据库不可用时自动降级为无历史模式，P2P 主链路不受影响

**模块依赖方向**

```mermaid
graph LR
    subgraph 客户端模块
        QML["QML 模块<br/>appGridYard"]
        DOM_LIB["gy_domain<br/>（INTERFACE）"]
        STORAGE_LIB["gy_storage<br/>（STATIC）"]
        SHARED_LIB["gy_shared<br/>（STATIC）"]
    end

    QML --> DOM_LIB
    QML --> SHARED_LIB
    STORAGE_LIB --> DOM_LIB
    STORAGE_LIB -.->|"PRIVATE"| QTSQL["Qt6::Sql"]
    DOM_LIB --> SHARED_LIB

    style QTSQL fill:#f96,stroke:#333
```

`Qt6::Sql` 通过 PRIVATE 链接隔离，不传播到 QML 模块或网络 Worker。

---

## 协议设计

GridYard 使用自研 TLV（Type-Length-Value）二进制协议：

| 字段 | 长度 | 字节序 | 说明 |
|:-----|:-----|:-------|:-----|
| Type | 4 字节 | 大端 | 帧类型码 |
| Length | 4 字节 | 大端 | Payload 字节数 |
| Payload | Length 字节 | — | 业务数据 |

**帧处理状态机**

```mermaid
stateDiagram-v2
    [*] --> 等待帧头
    等待帧头 --> 读取帧头: 累计 8 字节
    读取帧头 --> 校验长度: 解析 Type + Length
    校验长度 --> 等待Payload: Length ≤ 分级上限
    校验长度 --> 帧错误: Length 超限
    等待Payload --> 帧完成: 累计 Length 字节
    帧完成 --> 等待帧头: emit frameReady
    帧错误 --> [*]: emit errorOccurred
```

| Type 码 | 方向 | 用途 |
|:--------|:-----|:-----|
| 0x0001 | UDP | 设备 Hello / 心跳 |
| 0x0101 | TCP | 传输握手请求 |
| 0x0102 | TCP | 握手响应 |
| 0x0201 | TCP | 文件数据分块 |
| 0x0301 | TCP | 单文件完成确认 |
| 0x0302 | TCP | 全部传输完成 |
| 0x0401 | TCP | 取消传输 |
| 0x0501 | TCP | 聊天文本消息 |
| 0x0502 | TCP | 聊天送达回执 |

Payload 按 Type 分级限制：控制帧 1MB，数据帧 256MB，聊天帧 64KB。协议版本字段（`kProtocolVersion = 0x0100`）支持主版本不兼容拒绝与次版本安全降级。

---

## 设备发现与传输流程

**设备发现时序**

```mermaid
sequenceDiagram
    participant A as 设备 A
    participant B as 设备 B
    participant DS as DiscoveryService

    A->>DS: 启动 UDP 监听 (45678)
    B->>DS: 启动 UDP 监听 (45678)

    Note over A,B: 每 5 秒广播 Hello
    A->>B: UDP Hello (deviceId, name, ip, port)
    B->>A: UDP Hello (deviceId, name, ip, port)

    DS->>DS: 更新内存节点表
    DS-->>A: nodeDiscovered / peerUpdated 信号
    DS-->>B: nodeDiscovered / peerUpdated 信号

    Note over A,B: 15 秒无心跳 → 剔除
```

**文件传输时序**

```mermaid
sequenceDiagram
    participant Sender as 发送端 Worker
    participant Server as 接收端 P2pServer
    participant Receiver as 接收端 Worker
    participant UI as 接收端 UI

    Sender->>Server: TCP 连接 + TransferReq
    Server->>Receiver: 首帧分流，移交 socket
    Receiver->>UI: 接收确认弹窗
    UI-->>Receiver: 用户接受
    Receiver->>Sender: TransferRsp (accept)

    loop 每 8MB
        Sender->>Receiver: DataChunk
        Receiver->>Receiver: 写盘 + SHA-256 增量
    end

    Receiver->>Sender: ChunkAck (单文件校验通过)
    Sender->>Receiver: TransferDone
    Receiver->>UI: 传输完成通知
```

---

## 校园网协调服务器

当设备处于校园网等复杂网络环境时，UDP 广播可能被 AP 隔离阻断，导致同网段的设备无法互相发现。协调服务器提供了设备发现的兜底方案。

### 网络隔离场景示意

```mermaid
graph LR
    subgraph 校园网核心["校园网核心交换机"]
        SW["核心交换机"]
    end

    subgraph 宿舍楼A["宿舍楼 A（10.0.1.x）"]
        AP_A["AP A"]
        PC_A["你的电脑"]
    end

    subgraph 宿舍楼B["宿舍楼 B（10.0.2.x）"]
        AP_B["AP B"]
        PC_B["室友的电脑"]
    end

    subgraph 服务器["协调/中继服务器（10.10.10.100）"]
        RV["gridyard_rendezvous"]
    end

    PC_A -->|"UDP 广播被 AP 隔离"| AP_A
    PC_B -->|"UDP 广播被 AP 隔离"| AP_B
    PC_A -.->|"TCP 单播可达？<br/>（取决于 AP 安全策略）"| PC_B
    PC_A -->|"注册/拉取设备"| RV
    PC_B -->|"注册/拉取设备"| RV
```

**关键点**：
- 你的电脑和室友的电脑分别在不同的宿舍楼，接入了不同的 AP
- AP 可能会隔离同一网段的 UDP 广播，导致两台电脑无法通过 UDP 广播互相发现
- 协调服务器是一台**在校园网内**有固定 IP 的机器（只要 TCP 能到达就行，不要求 UDP）
- 客户端开机后自动向协调服务器注册自己的 IP:端口，并周期性拉取在线设备列表

### 工作原理

协调服务器不参与任何文件传输，只做两件事：**登记在线设备**和**返回设备列表**。

```mermaid
sequenceDiagram
    participant A as 你的电脑
    participant RV as 协调服务器
    participant B as 室友的电脑

    Note over A: 开机，启用协调服务器
    A->>RV: TCP 连接（WebSocket/JSON）
    A->>RV: register {"deviceId": "A", "ip": "10.0.1.50", "tcpPort": 35100}
    RV-->>A: register_ack {"status": "ok"}

    Note over B: 开机，启用协调服务器
    B->>RV: TCP 连接
    B->>RV: register {"deviceId": "B", "ip": "10.0.2.80", "tcpPort": 35100}
    RV-->>B: register_ack {"status": "ok"}

    Note over A: 每 10 秒刷新 TTL
    A->>RV: register {"deviceId": "A", ...}
    RV-->>A: register_ack {"status": "ok"}

    Note over A: 每 10 秒拉取设备列表
    A->>RV: list_peers
    RV-->>A: peers {"peers": [{"deviceId": "B", "ip": "10.0.2.80", "tcpPort": 35100}]}
```

**结果**：你的电脑在设备列表里看到了室友的电脑（来自协调服务器），双方的 IP 和端口都已知。此时两台电脑可以尝试**直接 TCP 连接**传输文件，**不经过协调服务器**。

### 部署架构：只需要一台能 SSH 的旧电脑/服务器

```mermaid
graph LR
    subgraph 局域网["任意可达的局域网（WiFi / 有线）"]
        SERVER["低配旧电脑或服务器<br/>运行 gridyard_rendezvous<br/>端口 45678"]
        PC1["设备 A"]
        PC2["设备 B"]
        PC3["设备 C"]
    end

    SERVER -->|"注册/查询"| PC1
    SERVER -->|"注册/查询"| PC2
    SERVER -->|"注册/查询"| PC3
    PC1 <-->|"直连 P2P 传输<br/>不经过 SERVER"| PC2
    PC2 <-->|"直连 P2P 传输<br/>不经过 SERVER"| PC3
    PC1 <-->|"直连 P2P 传输<br/>不经过 SERVER"| PC3
```

**部署要求极低**：
- 任意一台能 SSH 连上的电脑/服务器/树莓派
- **不需要公网 IP**，只需要在校园网内能 TCP 到达
- **不需要域名**，IP + 端口就行
- **内存 512MB 就够**，因为只做连接登记不做文件转发（Relay 模式除外）
- 建议用 `--token` 参数加简单密码，客户端配置相同的 Token 防止蹭网

### 两种运行模式

| 模式 | 命令 | 用途 |
|:-----|:-----|:-----|
| 协调节点（默认） | `./gridyard_rendezvous --port 45678` | 仅发现兜底，流量不经过此机器 |
| 中继服务器 | `./gridyard_rendezvous --mode relay --port 45679` | 极端网络隔离时转发流量 |

**协调节点**（默认）：
```bash
# 编译
cd src
cmake --build build-ninja -j

# 启动协调节点（默认端口 45678）
./build-ninja/rendezvous/gridyard_rendezvous

# 指定端口和密码
./build-ninja/rendezvous/gridyard_rendezvous --port 45780 --token my-secret-token
```

**中继服务器**（仅在 P2P 直连完全失败时使用）：
```bash
./build-ninja/rendezvous/gridyard_rendezvous --mode relay --port 45679
```
> 中继模式流量经服务器转发，大文件会占带宽，只有 AP 完全隔离 TCP 的极端场景才需要。

### 配置客户端

1. 打开 **设置 → 协调服务器**
2. 启用协调服务器
3. 填入服务器地址（如 `10.10.10.100`）和端口（如 `45678`）
4. 如果服务器设置了 Token，填入相同的 Token
5. 选择 **Relay 策略**：
   - **询问后中继**（默认）：P2P 失败时弹窗询问用户
   - **自动中继**：P2P 失败后自动通过中继服务器转发
   - **从不**：P2P 失败则传输失败

### 设备来源优先级

设备列表按以下优先级排序（高优先级排在前面）：

| 优先级 | 来源 | 说明 |
|:-------|:-----|:-----|
| 1 | broadcast | UDP 广播直接发现（同 AP 内，最优先） |
| 2 | directed | 定向 Hello 探测成功 |
| 3 | rendezvous | 协调服务器返回的候选端点 |
| 4 | manual | 用户手动添加的 IP 端点 |
| 5 | history | 历史记录中的设备 |

**连接优先级**：发现服务会按优先级尝试建立连接，成功即停，不逐个尝试。

### 数据流总览

```mermaid
graph TD
    subgraph 发现阶段["发现阶段"]
        A["设备 A"]
        B["设备 B"]
        RV["协调服务器"]
    end

    subgraph 连接阶段["连接阶段（A 主动连接 B）"]
        A -->|"1. 已知 B 的 IP:端口<br/>直接 TCP 连接"| B
        A -.->|"2. 直连失败时尝试<br/>中继转发"| RV
    end

    subgraph 传输阶段["传输阶段"]
        A <-.->|"3. 成功后直传文件<br/>不经过协调服务器"| B
    end
```

### 端口说明

| 服务 | 默认端口 | 说明 |
|:-----|:---------|:-----|
| 协调节点 | 45678 | 设备注册与列表查询 |
| 中继服务器 | 45679 | 流量转发（仅 relay 模式） |
| 客户端 TCP 监听 | 35100 | 接收文件/聊天连接 |

---

## 仓库目录结构

```text
GridYard/
├── README.md
├── doc/
│   ├── diagrams/                        # UML 类图、组件图、序列图
│   │   └── rendered/                    # PNG/SVG 导出
│   ├── dev-manual/
│   │   ├── 规格与设计/                   # 技术规格、架构设计
│   │   ├── 开发心得/                     # 踩坑记录、开发手册
│   │   └── 测试与部署/                   # 测试与打包指南
│   ├── plan/                            # 阶段开发计划
│   └── api-docs/                        # 模块 API 文档
└── src/
    ├── CMakeLists.txt                   # 顶层 CMake
    ├── shared/                          # 共用静态库 gy_shared
    │   ├── CMakeLists.txt
    │   ├── protocol.h                   # TLV 协议 Type 码与常量
    │   ├── data_types.h                 # PeerInfo / FileEntry / TransferSession
    │   ├── chat_message.{h,cpp}         # 聊天消息 JSON 编解码
    │   └── frame_codec.{h,cpp}          # TLV 帧编解码（粘包状态机）
    ├── client/
    │   ├── CMakeLists.txt               # 客户端 QML 模块与 IDE 分组
    │   ├── main.cpp                     # 程序入口，显式创建 AppController
    │   ├── Main.qml                     # QML 根窗口，由 AppController 初始化
    │   ├── core/                        # 应用逻辑层
    │   │   ├── app_controller.{h,cpp}   # 全局控制器，初始化应用层与 UI 层
    │   │   ├── config_manager.{h,cpp}   # 配置管理（QSettings）
    │   │   ├── transfer_session_manager.{h,cpp}
    │   │   ├── chat_manager.{h,cpp}     # 聊天连接与内存会话
    │   │   ├── chat_message_model.{h,cpp}
    │   │   ├── history_controller.{h,cpp} # 本地历史分页、筛选和清理
    │   │   ├── application_paths.{h,cpp}
    │   │   ├── dir_serializer.{h,cpp}   # 目录遍历 + SHA-256
    │   │   └── logger.{h,cpp}           # 日志拦截器
    │   ├── network/                     # 网络层
    │   │   ├── discovery_service.{h,cpp}    # UDP 设备发现
    │   │   ├── p2p_server.{h,cpp}           # TCP 服务器（首帧分流）
    │   │   ├── chat_connection.{h,cpp}      # 聊天 TCP 连接
    │   │   ├── file_sender_worker.{h,cpp}  # 发送 Worker（后台线程）
    │   │   └── file_receiver_worker.{h,cpp}# 接收 Worker（后台线程）
    │   ├── domain/                      # 领域层（Repository 端口）
    │   │   ├── CMakeLists.txt
    │   │   ├── history_records.h        # PeerRecord / MessageRecord / TransferRecord
    │   │   └── history_repositories.h   # IDevice/Message/TransferHistory Repository
    │   ├── storage/                     # 基础设施层（SQLite Broker + Repository）
    │   │   ├── CMakeLists.txt
    │   │   ├── local_data_broker.{h,cpp}       # 本地数据层代管者
    │   │   ├── sqlite_database_broker.{h,cpp}  # 连接、WAL、事务
    │   │   ├── sqlite_device_repository.{h,cpp} # 设备目录 Data Mapper
    │   │   ├── sqlite_message_repository.{h,cpp} # 聊天消息 Data Mapper
    │   │   ├── sqlite_transfer_history_repository.{h,cpp} # 传输历史 Data Mapper
    │   │   ├── migration_runner.{h,cpp}       # Schema 版本迁移
    │   │   └── database_worker.{h,cpp}        # 异步数据库线程
    │   ├── ui/                          # QML 界面组件
    │   │   ├── DeviceCard.qml
    │   │   ├── PeerListView.qml
    │   │   ├── DeviceSessionView.qml
    │   │   ├── ChatView.qml             # 聊天气泡视图
    │   │   ├── SettingsDialog.qml
    │   │   ├── AcceptDialog.qml
    │   │   ├── TransferPanel.qml
    │   │   ├── TransferHistoryView.qml
    │   │   ├── TransferTaskCard.qml
    │   │   └── FileTypeIcon.qml
    │   ├── icons/                       # 文件类型与应用图标资源
    │   │   ├── gridyard.png
    │   │   ├── folder.svg
    │   │   └── file-*.svg
    │   ├── images/                      # 预留图片资源
    │   └── utils/                       # QML 工具模块
    │       ├── Style.js                 # 样式常量
    │       └── FormatUtils.js           # 格式化工具
    ├── scripts/                         # 辅助脚本
    │   └── for_md.py                    # 代码归档工具
    ├── server/                          # V2 服务端（规划中）
    └── tests/                           # 单元测试与集成测试
        ├── CMakeLists.txt
        ├── test_frame_codec.cpp
        ├── test_chat_message.cpp
        ├── test_chat_manager.cpp
        ├── test_discovery.cpp
        ├── test_file_transfer.cpp
        ├── test_session_manager.cpp
        ├── test_transfer.cpp
        ├── test_config_manager.cpp
        ├── test_edge_cases.cpp
        ├── test_integration.cpp
        ├── test_storage_database.cpp
        ├── test_storage_device.cpp
        ├── test_storage_message.cpp
        ├── test_storage_transfer_history.cpp
        └── test_history_controller.cpp
```

---

## 关键设计决策

| 决策 | 选择 | 理由 |
|:-----|:-----|:-----|
| 通信模型 | P2P 直连 | 局域网场景无需中转，避免单点故障与公网依赖 |
| 协议格式 | TLV 二进制 | 比 JSON 紧凑，天然支持粘包状态机 |
| 多线程模型 | Worker-Object + moveToThread | 避免 QThread::run 重写，信号槽跨线程安全 |
| 数据库访问 | Repository 端口 + Data Mapper | 应用层不感知 SQL，便于测试与未来替换 |
| 接收线程 | 每连接独立 QThread | 写盘与 SHA-256 不阻塞 UI |
| 界面技术 | 全 QML | 声明式绑定，C++ 只暴露状态入口 |
| 配置存储 | QSettings | 跨平台原生配置 API |

---

## 性能与限制

| 指标 | 数值 |
|:-----|:-----|
| 单 chunk 大小 | 8 MB |
| 最大帧载荷 | 256 MB（数据帧）/ 1 MB（控制帧） |
| 心跳间隔 | 5 秒 |
| 离线剔除 | 15 秒无心跳 |
| 发现节流 | 30 秒（相同设备快照） |
| 数据库模式 | WAL，synchronous=NORMAL，busy_timeout=5000ms |
| 损坏库恢复 | 备份为 `.corrupt-{timestamp}` 后重建空库 |
| 聊天消息上限 | 4000 字符 / 64 KB Payload |

---

## 文档索引

| 文档 | 路径 |
|:-----|:-----|
| 需求规格 | `doc/dev-manual/规格与设计/鸽邮(GridYard)——技术需求与系统设计规格说明书.md` |
| 架构设计 | `doc/dev-manual/规格与设计/鸽邮(GridYard)——V1.0架构设计与V2.0演进说明书.md` |
| 业务逻辑 | `doc/dev-manual/规格与设计/GridYard业务逻辑说明.md` |
| 开发心得 | `doc/dev-manual/开发心得/开发心得_从架构设计到踩坑记录.md` |
| Stage 6 数据层设计 | `doc/dev-manual/规格与设计/GridYard_Stage6_本地数据层设计.md` |
| 模块 API 文档 | `doc/api-docs/` |
| UML 图 | `doc/diagrams/` |

---

## 团队

| 成员 | 职责 |
|:-----|:----|
| 高扬 | 架构设计、协议层、CMake、打包部署、阶段验收 |
| 冯春霖 | 网络传输、Worker 实现、聊天连接、性能调优 |
| 杜若贤 | QML 界面、UI 组件、测试用例、文档与 UML |

---

## 许可证

本项目为课程作业，版权归开发团队所有，保留所有权利。详情见 [LICENSE](./LICENSE)。
