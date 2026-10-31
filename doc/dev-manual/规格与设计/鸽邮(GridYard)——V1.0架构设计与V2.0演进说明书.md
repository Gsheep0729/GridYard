# 鸽邮 (GridYard) — V1.0 架构设计与 V2.0 演进说明书

---

| 字段       | 内容                                 |
| :--------- | :----------------------------------- |
| 文档编号   | GY-SPEC-001                          |
| 文档版本   | v2.2                                 |
| 创建日期   | 2026-04-23                           |
| 项目负责人 | 冯春霖                               |
| 核心成员   | 高扬 / 杜若贤 / 冯春霖              |
| 文档状态   | 当前设计基线                         |
| 开发平台   | Manjaro Linux                        |
| 语言标准   | C++23 / Qt 框架（传统头文件机制）    |
| 构建系统   | CMake                                |

---

## 修订历史

| 版本  | 日期       | 修改人 | 变更说明                                             |
| :---- | :--------- | :----- | :--------------------------------------------------- |
| v1.0  | 2026-04-23 | 冯春霖 | 初版发布，基于完整功能集的全量架构设计               |
| v2.0  | 2026-04-23 | 高扬   | 采用敏捷策略拆分为 V1.0/V2.0 双期；新增 V2.0 演进规划 |
| v2.1  | 2026-06-15 | GridYard Team | 补充 LocalSend 调研结论、本地历史、登录与记录漫游边界 |
| v2.2  | 2026-06-17 | GridYard Team | 新增 V1.0 P2P 在线文本聊天模块与文本 Type 码；在线单聊由 P2P 提供，服务端聚焦鉴权、漫游与离线兜底 |

---

## 目录

1. [项目背景与定位](#1-项目背景与定位)
2. [敏捷迭代策略说明](#2-敏捷迭代策略说明)
3. [V1.0 核心功能需求](#3-v10-核心功能需求)
4. [V1.0 非功能性需求](#4-v10-非功能性需求)
5. [V1.0 系统架构设计](#5-v10-系统架构设计)
6. [V1.0 技术栈与工程规范](#6-v10-技术栈与工程规范)
7. [V1.0 应用层通信协议规格](#7-v10-应用层通信协议规格)
8. [V1.0 关键技术攻关方案](#8-v10-关键技术攻关方案)
9. [V1.0 团队开发里程碑](#9-v10-团队开发里程碑)
10. [V2.0 架构升级演进规划](#10-v20-架构升级演进规划)

---

## 1. 项目背景与定位

### 1.1 背景与痛点

在高校实验室、机房和宿舍等场景中，师生团队频繁面临以下典型痛点：

- 互传数 GB 的虚拟机镜像、深度学习数据集或大型工程代码时，受制于公网云服务器的带宽限速与外网流量消耗，传输效率极低；
- 现有的局域网工具（如飞鸽传书）缺乏完整的账号体系，无法实现离线消息漫游，换机后协作上下文完全丢失；
- 主流即时通讯工具（微信、QQ）强依赖公网服务器，在校园局域网内既没有带宽优势，数据也须绕道公网，存在隐私顾虑。

### 1.2 项目定位

"鸽邮 (GridYard)"是一款专为校园局域网环境设计的综合协同终端，目标是将**高速文件直传**与**可靠即时通讯**融合于同一个桌面应用内，提供完全基于内网的一站式协同体验。

项目名称蕴含清晰的架构隐喻：

- **"鸽"（G）— P2P 直连层**：象征去中心化、点对点的高速直传特性。如飞鸽传书般在节点间直达，不经任何公网中转，完整榨取千兆局域网或 Wi-Fi 6 的物理带宽上限。
- **"邮"（Y）— C/S 服务层**：象征中央服务端作为"邮局"的调度与托管职能，负责用户鉴权、状态维护与离线消息的可靠暂存投递，确保协作上下文持久可追溯。

### 1.3 竞品分析

| 竞品                     | 核心优势                         | 核心痛点                                         | 鸽邮的差异化定位                                     |
| :----------------------- | :------------------------------- | :----------------------------------------------- | :--------------------------------------------------- |
| 飞鸽传书 / IP Messenger  | 纯 P2P，无需服务端，极致轻量     | 无账号体系，不支持离线漫游，换机记录丢失         | C/S 架构接管关系链与离线数据，记录跨设备可追溯       |
| QQ / 微信                | UI 成熟，用户粘性高              | 强依赖外网，文件传输受带宽限速，消耗校园流量     | 直接在内网 TCP 层传输，带宽不受公网瓶颈约束         |
| LocalSend / 互传         | 跨平台免配置，局域网发现设备     | 纯文件快递功能，零社交属性，无即时通讯能力       | 将高并发文件 I/O 与低延迟文本通信融合于同一终端     |

### 1.4 LocalSend 调研后的设计取舍

GridYard V1.0 的产品定位继续对标 LocalSend：无需账号即可发现附近设备并直传文件。LocalSend 的优势不在于 HTTP 本身，而在于协议对跨平台、兼容性和不可靠局域网环境的完整考虑。

后续重点借鉴协议版本与能力声明、主动发现回应、会话和文件 token、部分接受、请求限流、发现兜底以及可选 PIN/TLS。当前不切换为 LocalSend REST/HTTP 协议；现有 TLV 协议已经稳定支持文件夹、逐文件 ACK、SHA-256 和大文件背压，整体改写会增加回归风险。如确有互操作需求，再新增独立兼容适配层。

---

## 2. 敏捷迭代策略说明

### 2.1 决策背景

在初期需求评审完成后，团队评估了完整功能集（包含 P2P 传输 + C/S 即时通讯 + 离线漫游）在单一开发周期内交付的风险，识别出以下主要问题：

- 完整架构同时涉及客户端、无头服务端、PostgreSQL 三个独立工件，首次集成风险较高；
- 高并发服务端与离线漫游逻辑属于复杂后端工程，开发周期不可控；
- 团队中两名成员（杜若贤、冯春霖）处于 Qt 学习阶段，过早引入 C/S 服务端会分散精力。

### 2.2 双期迭代计划

为了降低风险、保障核心价值的优先交付，团队采用敏捷策略，将项目拆分为两个相互独立、平滑演进的版本：

| 版本    | 定位         | 架构模式      | 核心目标                                         | 交付条件                   |
| :------ | :----------- | :------------ | :----------------------------------------------- | :------------------------- |
| **V1.0** | MVP（最小可行产品）| 纯 P2P，无服务端 | 对标 LocalSend，实现零配置局域网高速文件传输     | 课程期限内必须交付         |
| **V2.0** | 功能完整版   | P2P + C/S 混合 | 在 V1.0 基础上接入服务端，引入 IM 与离线漫游     | 有空余时间时迭代，非强制   |

### 2.3 关键设计约束

> **[架构约束] V1.0 对 V2.0 的向前兼容保证**
>
> V1.0 的全部代码——包括协议类、网络服务类、数据序列化类——在开发时须以"模块边界清晰"为原则进行封装。当 V2.0 引入服务端时，**V1.0 已有的代码无需重构**，仅以新增模块的方式平滑叠加 C/S 能力。

---

## 3. V1.0 核心功能需求

V1.0 定位为**纯去中心化 P2P 工具**，不含中央服务端、不含注册登录；即时通讯能力以**在线设备间的 P2P 文本聊天**形式提供，同样不依赖服务端。账号、离线消息与跨设备漫游属于 V2.0 演进范围。

### 3.1 模块一：设备发现与身份标识

| 编号  | 需求名称           | 详细描述                                                                                               | 优先级 |
| :---- | :----------------- | :----------------------------------------------------------------------------------------------------- | :----- |
| F-101 | UDP 广播嗅探       | 应用启动后自动向局域网广播上线通告，持续监听其他节点的通告包，将其纳入在线设备列表                   | 高     |
| F-102 | 心跳保活与超时剔除 | 以固定间隔（5 秒）发送心跳广播包；超过约定超时时间（15 秒）无心跳的节点自动标记为离线并从列表移除   | 高     |
| F-103 | 设备名自定义       | 用户可自定义本机在局域网内显示的设备名称；未配置时使用系统 Hostname；变更在下一次心跳广播后即时生效 | 中     |

### 3.2 模块二：传输会话建立

| 编号  | 需求名称     | 详细描述                                                                           | 优先级 |
| :---- | :----------- | :--------------------------------------------------------------------------------- | :----- |
| F-201 | 传输请求发起 | 用户选择目标节点与本地文件/文件夹后，向目标发送包含完整文件元数据的传输请求         | 高     |
| F-202 | 接收端鉴权   | 接收端须在弹窗中明确执行"接受"或"拒绝"；任何文件不得在未经确认的情况下静默写入磁盘 | 高     |
| F-203 | 接收路径配置 | 用户可预设默认接收目录；未配置时在首次接收前弹出系统目录选择对话框                 | 中     |

### 3.3 模块三：文件传输核心

| 编号  | 需求名称         | 详细描述                                                                                             | 优先级 |
| :---- | :--------------- | :--------------------------------------------------------------------------------------------------- | :----- |
| F-301 | P2P TCP 直连传输 | 握手完成后，发送方与接收方建立直接 TCP 连接传输数据，全程不经任何第三方节点中转                     | 高     |
| F-302 | 大文件分块传输   | 文件内容以固定大小的块（默认 4 MB）为单位进行读取与发送，严禁整文件载入内存                         | 高     |
| F-303 | 文件夹传输支持   | 发送文件夹时完整保留并还原源目录的层级结构；接收端在写入数据前须递归创建所有子目录                   | 高     |
| F-304 | 传输完整性校验   | 每个文件传输完成后，接收端计算落盘文件的 SHA-256 哈希值并与发送端预附的哈希值比对，不一致则标记失败 | 高     |
| F-305 | 任务生命周期控制 | 支持对进行中的任务执行取消操作（双端响应、清理临时文件、释放连接）；同时支持暂停与恢复               | 中     |

### 3.4 模块四：用户界面交互

| 编号  | 需求名称         | 详细描述                                                                                         | 优先级 |
| :---- | :--------------- | :----------------------------------------------------------------------------------------------- | :----- |
| F-401 | 在线设备面板     | 主界面持续展示局域网内所有已发现的在线设备，显示设备名与最后活跃时间；列表实时刷新，无需手动操作 | 高     |
| F-402 | 拖拽发起传输     | 支持将本地文件或文件夹直接拖拽至目标设备条目上，以此快捷发起传输请求                             | 中     |
| F-403 | 传输进度可视化   | 传输进行中实时展示进度条、已传输字节、当前速度（MB/s）及预估剩余时间；界面全程保持响应不卡顿     | 高     |
| F-404 | 传输历史记录     | 维护本次运行期间的传输记录，展示文件名、大小、耗时、状态（成功/失败/已取消）及对端设备名         | 低     |
| F-405 | 本地历史持久化   | 使用 SQLite 保存传输元数据，应用重启后仍可查询；默认不保存文件内容或敏感绝对路径                 | 高     |

### 3.5 模块五：基础配置

| 编号  | 需求名称     | 详细描述                                                                           | 优先级 |
| :---- | :----------- | :--------------------------------------------------------------------------------- | :----- |
| F-501 | 配置持久化   | 设备名、接收路径等配置修改须持久化写入本地配置文件，重启后自动加载生效             | 高     |
| F-502 | 系统托盘常驻 | 关闭主窗口时应用不退出而是最小化至系统托盘，维持广播与监听；收到传输请求时弹出气泡 | 中     |
| F-503 | 隐私与保留策略 | 用户可配置历史保留天数、清空本地历史，并决定未来是否允许账号同步传输记录             | 中     |

### 3.6 模块六：P2P 在线文本聊天

> 在线聊天复用设备发现与 P2P TCP 通道，与文件传输共用监听端口，由首帧 Type 区分连接用途。本模块不依赖服务端与登录，仅支持向在线设备发送；离线投递与跨设备漫游由 V2.0 服务端承担。

| 编号  | 需求名称       | 详细描述                                                                                   | 优先级 |
| :---- | :------------- | :----------------------------------------------------------------------------------------- | :----- |
| F-601 | 在线文本收发   | 向局域网内在线设备发送和接收 UTF-8 文本消息，毫秒级到达，全程不经服务端                     | 高     |
| F-602 | 会话视图       | 按设备组织聊天会话，气泡区分发件方与收件方，展示发送时间                                     | 高     |
| F-603 | 在线约束       | 仅允许向在线设备发送；对端离线时输入区禁用并提示，消息不静默丢弃                             | 高     |
| F-604 | 连接生命周期   | 聊天连接与设备在线状态联动，断线安全清理，不影响设备发现与文件传输主链路                     | 中     |
| F-605 | 送达回执       | 在线消息可返回送达确认，发送方据此标记"已送达"                                               | 低     |

---

## 4. V1.0 非功能性需求

| 编号   | 类别         | 指标                                                                                   |
| :----- | :----------- | :------------------------------------------------------------------------------------- |
| NF-101 | 吞吐量       | P2P 传输在千兆局域网下目标速率不低于 80 MB/s，力争达到物理带宽极限（约 100 MB/s）   |
| NF-102 | 界面响应性   | 任何 I/O 操作（文件读写、网络收发）严禁在 Qt 主线程执行，确保界面始终保持响应状态   |
| NF-103 | 资源占用     | 客户端后台驻留时 CPU 占用率低于 1%，内存占用处于合理范围，不影响宿主机正常使用       |
| NF-201 | 数据可靠性   | 所有落盘文件须执行 SHA-256 校验；TCP 连接意外断开时双端均能优雅清理资源，不崩溃       |
| NF-202 | 网络适应性   | 适应 DHCP 动态 IP 环境；多网卡（有线 + WiFi 并存）场景下广播地址筛选须正确无误       |
| NF-301 | 架构解耦性   | P2P 网络层、业务逻辑层、UI 视图层须保持清晰的代码物理边界，不得产生跨层依赖          |
| NF-302 | 协议防护     | 所有网络入口必须限制帧大小、字段长度、文件数量和请求频率；未知能力应安全降级          |
| NF-303 | 数据隐私     | 本地及漫游记录默认只保存必要元数据；服务端不得保存 P2P 文件内容，敏感路径不得上传      |

---

## 5. V1.0 系统架构设计

### 5.1 整体架构

V1.0 采用**纯去中心化 P2P 架构**，局域网内所有运行"鸽邮"的节点地位完全对等，每个客户端实例在内部同时扮演两个角色：

- **文件发送端（Initiator）**：主动发起连接，读取本地文件并推送。
- **文件接收端（Responder）**：内嵌 TCP 监听服务，接受连接并将流量落盘。

```
                        局域网 (Local Area Network)
  ┌──────────────────────────────────────────────────────┐
  │                                                      │
  │   节点 A（鸽邮客户端）      节点 B（鸽邮客户端）       │
  │  ┌─────────────────┐      ┌─────────────────┐       │
  │  │  Qt Quick/QML    │      │  Qt Quick/QML    │       │
  │  │  表现层          │      │  表现层          │       │
  │  ├─────────────────┤      ├─────────────────┤       │
  │  │  业务逻辑层      │      │  业务逻辑层      │       │
  │  │  SessionMgr      │      │  SessionMgr      │       │
  │  ├─────────────────┤      ├─────────────────┤       │
  │  │  网络层          │      │  网络层          │       │
  │  │  DiscoverySvc    │      │  DiscoverySvc    │       │
  │  │  P2pServer       │      │  P2pServer       │       │
  │  │  FileSender      │      │  FileSender      │       │
  │  └────────┬────────┘      └────────┬────────┘       │
  │           │                        │                 │
  │           │◄── UDP Broadcast ─────►│                 │
  │           │    (Hello / Heartbeat) │                 │
  │           │                        │                 │
  │           │══════ TCP Direct ══════►                 │
  │           │    (文件元数据 + 数据块)                  │
  │                                                      │
  └──────────────────────────────────────────────────────┘
```

### 5.2 客户端内部分层结构

```
┌─────────────────────────────────────────────────────┐
│                Qt Quick/QML 表现层（View）            │
│  Main / SettingsDialog / AcceptDialog                │
│  只绑定状态并向 AppController 汇报用户意图            │
├─────────────────────────────────────────────────────┤
│             应用与业务逻辑层（Application）           │
│  AppController           TransferSessionManager      │
│  ConfigManager           DirSerializer               │
├─────────────────────────────────────────────────────┤
│                    网络层（Network）                   │
│  DiscoveryService    P2pServer                       │
│  FileSenderWorker    FileReceiverWorker               │
│  FrameCodec                                          │
├─────────────────────────────────────────────────────┤
│                    数据层（Storage）                   │
│  TransferHistoryRepository / SQLite                  │
└─────────────────────────────────────────────────────┘
         ▲ Qt Signal/Slot (Queued Connection)
         │ 跨层通信全部通过信号/槽，禁止直接调用
```

### 5.3 核心数据流

**P2P 文件传输完整时序：**

```
发送端 (S)                                    接收端 (R)
   │                                               │
   │  [S 与 R 已互相通过 UDP 广播嗅探到对方存在]     │
   │  [S 从 Hello 包中获得 R 的 tcp_port]            │
   │                                               │
   │──── TCP connect(R.ip, R.tcp_port) ───────────►│
   │──── TYPE_TRANSFER_REQ (文件元数据 JSON) ──────►│
   │                                               │  [弹出确认弹窗]
   │◄─── TYPE_TRANSFER_RSP (accepted: true) ───────│
   │                                               │
   │  [循环: 以 4MB 为单位读取文件]                 │
   │──── TYPE_DATA_CHUNK (file_index=0, chunk) ───►│  [写盘]
   │──── TYPE_DATA_CHUNK (is_last_chunk=1) ────────►│  [SHA-256 校验]
   │◄─── TYPE_CHUNK_ACK (verified: true) ──────────│
   │                                               │
   │  [所有文件传输完成]                             │
   │──── TYPE_TRANSFER_DONE ───────────────────────►│
   │                                               │  [关闭连接，通知用户]
```

---

## 6. V1.0 技术栈与工程规范

### 6.1 核心技术选型

| 层次           | 选型                               | 职责说明                                                     |
| :------------- | :--------------------------------- | :----------------------------------------------------------- |
| 图形界面框架   | Qt Quick / QML / Quick Controls 2  | 构建客户端 UI，通过属性绑定与信号连接应用逻辑层              |
| 多线程         | `QThread` + Worker Object 模式     | 将所有阻塞 I/O 操作隔离至独立后台线程，保证主线程响应        |
| 设备发现       | `QUdpSocket` + `QNetworkInterface` | 局域网广播发现在线节点                                       |
| 数据传输       | `QTcpServer` + `QTcpSocket`        | P2P 文件直传长连接通道                                       |
| 数据序列化     | `QDataStream` + `QByteArray`       | 统一大小端对齐，实现跨平台安全的二进制协议帧编解码           |
| 文件 I/O       | C++23 `<filesystem>` / `QFile`     | 目录遍历、分块读写、SHA-256 哈希计算                         |
| 配置存储       | `QSettings`                        | 设备名、接收路径等配置的本地持久化读写                       |
| 定时器         | `QTimer`                           | 心跳包定时广播与在线节点超时剔除                             |
| 系统集成       | `QSystemTrayIcon`                  | 后台驻留、系统通知气泡弹窗                                   |

### 6.2 开发环境约束

| 环境项目        | 具体配置                                                      |
| :-------------- | :------------------------------------------------------------ |
| 操作系统        | Manjaro Linux（开发、编译、部署三端统一此环境）               |
| 构建系统        | CMake（多 target 工程，V2.0 扩展时覆盖 server target）        |
| C++ 语言标准    | C++23                                                         |
| Qt 模块交互方式 | 强制使用传统 `#include` 头文件预处理机制（原因见下方说明）    |

> **[重要约束] C++23 Module 与 Qt MOC 兼容性**
>
> Qt 框架的核心机制依赖元对象编译器（MOC）。当前版本的 MOC 对 C++23 模块化
> 语法（`import std;` 等）的解析支持极为薄弱，在含 `Q_OBJECT` 宏的类文件中
> 使用 Module 语法将直接引发编译失败。
>
> **决策**：所有涉及 Qt 框架的代码文件强制回退到传统 `#include` 头文件预处理
> 机制。仅在与 Qt 模块完全无交叉的纯逻辑静态库中，可视情况使用 C++23 特性。

### 6.3 C++ 代码规范

#### 6.3.1 命名规范总表

| 类别             | 规范                  | 示例                                         |
| :--------------- | :-------------------- | :------------------------------------------- |
| 类名             | PascalCase            | `DiscoveryService`, `FileSenderWorker`       |
| 函数 / 方法名    | camelCase             | `sendHelloPacket()`, `onDatagramReceived()`  |
| 成员变量         | `m_` 前缀 + camelCase | `m_socket`, `m_heartbeatTimer`               |
| 局部变量         | camelCase             | `totalBytes`, `chunkSize`                    |
| 常量 / 枚举值    | `k` 前缀 + PascalCase | `kChunkSize`, `kDiscoveryPort`               |
| QML 文件 / `id` | PascalCase / camelCase | `TransferPage.qml`, `transferProgress`       |
| 信号函数         | camelCase 动词        | `progressChanged(int)`, `nodeDiscovered()`   |
| 槽函数           | `on` 前缀 + camelCase | `onReadyRead()`, `onTransferFinished()`      |

#### 6.3.2 QML 与 C++ 集成规范

- QML 文件使用 PascalCase，组件 `id` 使用小写开头，不添加 Widgets 的 `tw_` 前缀。
- C++ 类型通过 `QML_ELEMENT` 和 `qt_add_qml_module()` 注册。
- QML 不直接访问网络、文件系统或数据库，只调用 Controller 暴露的意图接口。
- AppController 负责依赖组装；业务服务和 Storage 不依赖 QML。
- Worker 使用 `moveToThread`，通过 Queued Connection 回传状态。

#### 6.3.3 关键编码原则

**原则一：禁止阻塞 Qt 主线程**

任何耗时超过约 16ms 的操作（文件读写、网络等待、数据库查询）都必须移至后台线程执行，通过 Qt 信号/槽（Queued Connection）向主线程回传结果。

```cpp
// 错误示范 — 在主线程直接读取大文件，会导致 UI 冻结
void MainWindow::onSendClicked() {
    QByteArray data = file.readAll(); // 禁止
    socket->write(data);
}

// 正确示范 — 将工作委托给后台线程
void MainWindow::onSendClicked() {
    auto *worker = new FileSenderWorker(filePath, targetIp, targetPort);
    auto *thread = new QThread(this);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &FileSenderWorker::start);
    connect(worker, &FileSenderWorker::progressChanged,
            tw_ProgressBar_Transfer, &QProgressBar::setValue);
    connect(worker, &FileSenderWorker::finished, thread, &QThread::quit);
    thread->start();
}
```

**原则二：禁止整文件内存载入（readAll 禁令）**

文件 I/O 必须使用固定大小的分块循环读取，防止大文件场景下爆内存崩溃。

```cpp
// 错误示范
QByteArray allData = file.readAll(); // 传输 2GB 文件时直接 OOM

// 正确示范 — 分块读取
constexpr qint64 kChunkSize = 4 * 1024 * 1024; // 4 MB
while (!file.atEnd()) {
    QByteArray chunk = file.read(kChunkSize);
    sendChunkFrame(chunk);
    emit progressChanged(calculatePercent());
}
```

**原则三：跨线程通信必须通过信号/槽**

后台线程不得直接调用 UI 控件的任何方法（包括 `setText`、`setValue` 等），必须通过发射信号，由 Qt 事件循环在主线程中安全响应。

```cpp
// FileSenderWorker 中（后台线程）— 正确
emit progressChanged(percent);      // 安全：通过 Queued Connection 跨线程

// FileSenderWorker 中（后台线程）— 错误
tw_ProgressBar_Transfer->setValue(percent); // 危险：直接跨线程操作 UI 对象
```

---

## 7. V1.0 应用层通信协议规格

由于 TCP 是无边界的字节流协议，须在应用层定义帧格式以解决粘包与半包问题。

### 7.1 通用帧结构（TLV 变长帧）

所有通过 **TCP** 发送的协议帧均采用固定 8 字节帧头 + 变长载荷的结构：

```
 字节偏移:   0        1        2        3        4        5        6        7
           +--------+--------+--------+--------+--------+--------+--------+--------+
           |               Type (uint32_t, 网络大端序)           |              Length (uint32_t, 网络大端序)          |
           +--------+--------+--------+--------+--------+--------+--------+--------+
           |                         Payload (Length 字节)                         |
           |                               ...                                     |
           +--------+--------+--------+--------+--------+--------+--------+--------+

 帧头大小 = 8 字节（固定）
 帧总大小 = 8 + Length 字节
```

| 字段    | 偏移 | 大小   | C++ 类型   | 字节序 | 说明                       |
| :------ | :--- | :----- | :--------- | :----- | :------------------------- |
| Type    | 0    | 4 字节 | `uint32_t` | 大端序 | 帧类型码，标识业务含义     |
| Length  | 4    | 4 字节 | `uint32_t` | 大端序 | Payload 的字节数 N         |
| Payload | 8    | N 字节 | `byte[]`   | —      | 业务数据（JSON 或二进制）  |

> **注意**：UDP 设备发现包不使用此帧头，直接发送紧凑 JSON 字符串。TLV 帧头
> 仅用于 TCP 长连接通道，通过 `QDataStream` 处理字节序对齐。

### 7.2 V1.0 Type 码定义总表

| Type 码（16进制） | 常量名（`protocol.h`）  | 方向             | 载荷格式 | 说明                         |
| :---------------- | :---------------------- | :--------------- | :------- | :--------------------------- |
| `0x0001`          | `kTypeHello`            | UDP 广播（双向） | JSON     | 设备上线 / 心跳通告          |
| `0x0101`          | `kTypeTransferReq`      | 发送端 -> 接收端 | JSON     | 文件元数据握手请求           |
| `0x0102`          | `kTypeTransferRsp`      | 接收端 -> 发送端 | JSON     | 握手响应（接受 / 拒绝）      |
| `0x0201`          | `kTypeDataChunk`        | 发送端 -> 接收端 | 混合二进制 | 文件数据分块                |
| `0x0301`          | `kTypeChunkAck`         | 接收端 -> 发送端 | JSON     | 单文件传输完成确认与校验报告 |
| `0x0302`          | `kTypeTransferDone`     | 发送端 -> 接收端 | JSON     | 全部文件发送完毕通知         |
| `0x0401`          | `kTypeCancel`           | 任意端 -> 对端   | JSON     | 取消本次传输会话             |
| `0x0501`          | `kTypeChatText`         | 在线端 -> 在线端 | JSON     | P2P 在线文本消息             |
| `0x0502`          | `kTypeChatAck`          | 接收端 -> 发送端 | JSON     | 在线文本消息送达回执         |

### 7.3 各类型 Payload 详细定义

---

**`kTypeHello` — 设备上线 / 心跳通告（UDP，JSON）**

```json
{
  "device_id":   "a3f1c2d4-5b6e-7f8a-9b0c-1d2e3f4a5b6c",
  "device_name": "高扬的工作站",
  "app_version": "1.0.0",
  "tcp_port":    35100
}
```

| 字段          | 类型   | 说明                                                         |
| :------------ | :----- | :----------------------------------------------------------- |
| `device_id`   | string | 设备唯一 UUID，首次启动时生成并持久化，用于节点去重识别     |
| `device_name` | string | 用户自定义名称或系统 Hostname，展示于对方的设备列表          |
| `app_version` | string | 应用版本号，为 V2.0 版本兼容性判断预留                       |
| `tcp_port`    | int    | 本机监听传输请求的 TCP 端口号（接收端口）                    |

---

**`kTypeTransferReq` — 文件元数据握手请求（TCP，JSON）**

```json
{
  "session_id":  "b7e2a1f0-3c4d-5e6f-7a8b-9c0d1e2f3a4b",
  "sender_device_id": "发送方设备 UUID",
  "sender_name": "高扬的工作站",
  "total_files": 2,
  "total_bytes": 2147483648,
  "files": [
    {
      "file_index":    0,
      "relative_path": "project/main.cpp",
      "size_bytes":    10240,
      "sha256":        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    },
    {
      "file_index":    1,
      "relative_path": "dataset.zip",
      "size_bytes":    2147473408,
      "sha256":        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"
    }
  ]
}
```

---

**`kTypeTransferRsp` — 握手响应（TCP，JSON）**

```json
{
  "session_id": "b7e2a1f0-3c4d-5e6f-7a8b-9c0d1e2f3a4b",
  "accepted":   true,
  "reason":     ""
}
```

---

**`kTypeDataChunk` — 文件数据分块（TCP，混合二进制）**

此帧 Payload 前 20 字节为定长二进制元数据，后续为原始文件二进制内容，以避免 JSON 解析开销影响大文件吞吐速率。

```
 Payload 内部布局：
 偏移   大小    字段            类型           说明
  0     4 B    file_index      uint32, 大端   当前块属于第几个文件（对应 file_index）
  4     8 B    chunk_offset    uint64, 大端   此块在文件内的字节起始偏移（支持断点续传定位）
 12     4 B    chunk_size      uint32, 大端   此块的实际有效数据字节数 M
 16     4 B    is_last_chunk   uint32, 大端   1 = 此文件的最后一块，0 = 否
 20     M B    chunk_data      raw bytes      原始文件二进制内容
```

---

**`kTypeChunkAck` — 单文件完成确认（TCP，JSON）**

```json
{
  "session_id":  "b7e2a1f0-3c4d-5e6f-7a8b-9c0d1e2f3a4b",
  "file_index":  1,
  "verified":    true,
  "error_msg":   ""
}
```

---

**`kTypeTransferDone` — 全量完成通知（TCP，JSON）**

```json
{
  "session_id":     "b7e2a1f0-3c4d-5e6f-7a8b-9c0d1e2f3a4b",
  "total_files":    2,
  "success_count":  2,
  "failed_indexes": []
}
```

---

**`kTypeCancel` — 取消传输（TCP，JSON）**

```json
{
  "session_id": "b7e2a1f0-3c4d-5e6f-7a8b-9c0d1e2f3a4b",
  "reason":     "用户主动取消"
}
```

---

**`kTypeChatText` — P2P 在线文本消息（TCP，JSON）**

```json
{
  "message_id":     "c8f3b2a1-4d5e-6f7a-8b9c-0d1e2f3a4b5c",
  "from_device_id": "发送方设备 UUID",
  "from_name":      "高扬的工作站",
  "content":        "晚上一起调试服务端吗？",
  "sent_at":        "2026-06-17T20:15:00"
}
```

| 字段             | 类型   | 说明                                               |
| :--------------- | :----- | :------------------------------------------------- |
| `message_id`     | string | 消息全局 UUID，用于送达回执与未来漫游去重           |
| `from_device_id` | string | 发送方稳定设备 ID，接收端据此归类会话               |
| `from_name`      | string | 发送方当前设备别名                                  |
| `content`        | string | UTF-8 文本内容                                      |
| `sent_at`        | string | 发送时间（ISO 8601）                                |

### 7.4 协议演进与兼容策略

V1.0 保留现有 8 字节 TLV 帧，不改为 LocalSend REST 或 IPMSG 文本命令字。在线聊天复用同一 TLV 帧与 TCP 通道，文本 Type 码（`0x05xx`）与文件传输码（`0x01xx`~`0x04xx`）同处 V1.0 低位段，接收端按首帧 Type 区分连接用途。后续协议升级遵循：

1. UDP Hello 增加 `protocol_version`、`device_type`、`fingerprint` 和 `capabilities`。
2. 传输请求增加 `session_token`；接收端为获准文件返回独立 `file_token`。
3. 接收响应支持部分接受文件列表，发送端只传输获准文件。
4. `FrameCodec` 按 Type 限制最大 Payload，控制帧、数据帧使用不同上限。
5. 协议错误使用稳定错误码，例如无效请求、需要 PIN、拒绝、会话冲突和请求过多。
6. 未识别能力安全忽略；不兼容主版本拒绝会话并提示升级。
7. PIN、TLS 和可信设备配对属于安全增强，不能用 SHA-256 文件校验替代身份认证。

---

## 8. V1.0 关键技术攻关方案

### 8.1 多网卡环境下的 UDP 广播精确发送

**问题本质**：在同时连接有线局域网和 WiFi 的主机上，若向错误的网卡接口发送广播，其他设备将无法收到。若直接使用 `QHostAddress::Broadcast`，Qt 底层可能仅向默认网卡发送，遗漏其他网段。

**解决方案**：

```cpp
void DiscoveryService::sendHelloPacket() {
    const QJsonObject payload = buildHelloPayload();
    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    // 遍历所有激活的网络接口
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        // 过滤：必须是激活中的、非回环的、支持广播的物理网卡
        if (!(iface.flags() & QNetworkInterface::IsUp))       continue;
        if (!(iface.flags() & QNetworkInterface::CanBroadcast)) continue;
        if (  iface.flags() & QNetworkInterface::IsLoopBack)  continue;

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;

            // 向该网卡对应子网的广播地址精确发送
            m_socket->writeDatagram(data, entry.broadcast(), kDiscoveryPort);
        }
    }
}
```

**心跳超时剔除逻辑**：

```cpp
// 定时器每 3 秒触发，剔除超过 15 秒未更新的节点
void DiscoveryService::pruneOfflineNodes() {
    const QDateTime threshold = QDateTime::currentDateTimeUtc().addSecs(-15);
    QMutableMapIterator<QString, PeerInfo> it(m_peerTable);
    while (it.hasNext()) {
        it.next();
        if (it.value().lastSeen < threshold) {
            emit nodeExpired(it.key()); // 通知 UI 层移除
            it.remove();
        }
    }
}
```

### 8.2 TCP 粘包处理——接收端状态机

**问题本质**：TCP 是字节流协议。多次 `write()` 的数据可能在接收端合并为一次 `readyRead()` 信号（粘包），也可能一次 `readyRead()` 中只到达半帧数据（半包）。

**解决方案**——`FrameCodec` 类实现两状态接收状态机：

```cpp
// frame_codec.h
class FrameCodec : public QObject {
    Q_OBJECT
public:
    void feed(const QByteArray &newData); // 将 socket 新数据喂入解码器
signals:
    void frameReady(quint32 type, const QByteArray &payload); // 完整帧就绪
private:
    enum class State { WaitingHeader, WaitingPayload };
    State      m_state  = State::WaitingHeader;
    QByteArray m_buffer;        // 接收缓冲区
    quint32    m_pendingType   = 0;
    quint32    m_pendingLength = 0;
};

void FrameCodec::feed(const QByteArray &newData) {
    m_buffer.append(newData);

    while (true) {
        if (m_state == State::WaitingHeader) {
            if (m_buffer.size() < 8) return; // 帧头未到齐，等待
            QDataStream ds(m_buffer.left(8));
            ds.setByteOrder(QDataStream::BigEndian);
            ds >> m_pendingType >> m_pendingLength;
            m_buffer.remove(0, 8);
            m_state = State::WaitingPayload;
        }

        if (m_state == State::WaitingPayload) {
            if ((quint32)m_buffer.size() < m_pendingLength) return; // Payload 未到齐，等待
            QByteArray payload = m_buffer.left(m_pendingLength);
            m_buffer.remove(0, m_pendingLength);
            emit frameReady(m_pendingType, payload); // 完整帧，交付业务层
            m_state = State::WaitingHeader;           // 重置，处理下一帧（处理粘包）
        }
    }
}
```

### 8.3 异步文件 I/O——Worker Object 模式

**问题本质**：在 Qt 主线程中直接进行大文件磁盘读写，会导致事件循环无法处理绘制和输入事件，界面表现为"未响应"。

**解决方案**——Worker Object 模式，将 I/O 完全隔离至后台线程：

```
主线程（UI 线程）
  │ 创建 FileSenderWorker + QThread
  │ worker->moveToThread(thread)
  │ connect(worker::progressChanged -> tw_ProgressBar::setValue)  [Queued Connection]
  │ connect(worker::finished -> thread::quit)
  │ thread->start()  // 启动后台线程，开始发送
  │
  └── 主线程继续正常处理用户输入和 UI 渲染

后台线程（FileSenderWorker::start 槽函数执行）
  │ QFile file(m_filePath);  file.open(ReadOnly)
  │ while (!file.atEnd()) {
  │     QByteArray chunk = file.read(kChunkSize);   // 4MB 分块读取
  │     buildAndSendChunkFrame(chunk);               // 封装 TLV 帧写入 socket
  │     emit progressChanged(calculatePercent());    // 跨线程安全信号
  │ }
  └── emit finished()
```

**进阶优化（冲刺高性能）**：使用 `QFileDevice::map()` 实现 mmap 内存映射文件读取，减少内核态/用户态数据复制次数，接近 SSD 的物理吞吐上限。

---

## 9. V1.0 团队开发里程碑

> 本章保留项目早期任务拆分，作为过程记录。其中 Qt Designer、`tw_` 控件等内容已被 QML 实现替代；当前待办、优先级与验收标准以《鸽邮(GridYard)——软件开发阶段计划》v1.2 为准。

### 9.1 成员角色定义

| 成员   | 技术背景                                           | V1.0 核心职责域                                     |
| :----- | :------------------------------------------------- | :-------------------------------------------------- |
| 高扬   | 网络底层、多线程、C/S 架构；有完整网盘项目经验     | UDP 发现引擎、TCP 传输双端引擎、协议编解码、多线程  |
| 杜若贤 | Qt 框架学习中                                      | 全部 Qt UI 设计（Designer）、控件交互逻辑、拖拽     |
| 冯春霖 | Qt 框架学习中                                      | 文件系统处理、会话管理、配置持久化、信号/槽业务串联 |

### 9.2 Phase 1 — 工程奠基（Foundation）

**目标**：统一开发环境，定义接口边界，跑通可编译的空壳 Demo。各成员任务相互独立，可完全并行推进。

| # | 任务描述 | 负责人 | 产出物 |
|:--|:---------|:-------|:-------|
| 1.1 | 初始化 CMake 工程，规划目录结构（`/network`、`/ui`、`/core`、`/config`） | 高扬 | 可编译的空壳工程骨架 |
| 1.2 | 编写协议头文件，定义全部 Type 码常量、帧头结构体与 Payload 字段常量 | 高扬 | `protocol.h` |
| 1.3 | 实现 `FrameCodec` 类：TLV 帧的 `encode()` 与带状态机的 `decode()`（含粘包处理）| 高扬 | `frame_codec.h/.cpp` |
| 1.4 | 用 Qt Designer 设计主窗口骨架（左侧设备面板、右侧传输任务区、底部状态栏） | 杜若贤 | `mainwindow.ui`（全控件遵守 `tw_` 规范）|
| 1.5 | 用 Qt Designer 设计设置对话框（设备名输入、默认接收路径、版本展示） | 杜若贤 | `settingsdialog.ui` |
| 1.6 | 定义 `PeerInfo`、`FileEntry`、`TransferSession` 等核心数据结构 | 冯春霖 | `data_types.h` |
| 1.7 | 实现 `ConfigManager` 单例：通过 `QSettings` 管理设备名、接收路径、TCP 端口 | 冯春霖 | `config_manager.h/.cpp` |
| 1.8 | 实现设备 ID 的首次生成（UUID）与持久化存储逻辑，集成至 `ConfigManager` | 冯春霖 | 集成于 `ConfigManager` |

### 9.3 Phase 2 — 设备发现（Discovery）

**目标**：局域网内多机运行的"鸽邮"可以互相感知，设备列表动态刷新。此阶段是 Phase 3 的前置条件，须优先完成。

| # | 任务描述 | 负责人 | 产出物 |
|:--|:---------|:-------|:-------|
| 2.1 | 实现 `DiscoveryService`：遍历 `QNetworkInterface` 筛出有效网卡，向各子网广播地址定向发送 UDP Hello 包 | 高扬 | `discovery_service.h/.cpp` |
| 2.2 | 实现 Hello 包监听、节点表更新与心跳超时剔除；暴露 `nodeDiscovered` 和 `nodeExpired` 两个信号 | 高扬 | 集成于 `DiscoveryService` |
| 2.3 | 实现设备列表控件 `tw_ListPeers` 的动态渲染：响应 `nodeDiscovered` 新增卡片，响应 `nodeExpired` 移除卡片 | 杜若贤 | 设备列表 UI 逻辑 |
| 2.4 | 为设备卡片设计视觉样式：展示设备名、IP 地址、在线时长；区分在线（绿色）与离线（灰色）状态 | 杜若贤 | 设备卡片样式 |
| 2.5 | 将设置对话框与 `ConfigManager` 联通：保存设备名后，下一次心跳广播中即携带新名称 | 冯春霖 | 设置界面业务逻辑 |
| 2.6 | 实现本机 IP 自身过滤逻辑，防止本机广播包被自己纳入在线列表 | 冯春霖 | 集成于发现服务调用层 |

**Phase 2 验收标准**：局域网内启动两个实例，双方设备列表均能实时展示对方；关闭任一实例，对端在 15 秒内自动将其移除。

### 9.4 Phase 3 — 传输核心（Transfer Engine）

**目标**：跑通完整文件传输流程，包括握手、分块传输、SHA-256 校验。这是 V1.0 的核心技术攻关阶段。

**Phase 3A — 发送侧**

| # | 任务描述 | 负责人 | 产出物 |
|:--|:---------|:-------|:-------|
| 3.1 | 实现 `P2pServer`：客户端内嵌 `QTcpServer`，启动时监听 `ConfigManager` 指定端口 | 高扬 | `p2p_server.h/.cpp` |
| 3.2 | 实现 `FileSenderWorker`（运行于独立 `QThread`）：建立 TCP 连接、发送 `kTypeTransferReq`、等待 `kTypeTransferRsp` | 高扬 | `file_sender_worker.h/.cpp` |
| 3.3 | 在 `FileSenderWorker` 中实现以 4MB 为单位的分块发送循环，封装 `kTypeDataChunk` 帧 | 高扬 | 集成于 `FileSenderWorker` |
| 3.4 | 实现 `DirSerializer`：将文件/文件夹序列化为 `FileEntry` 列表，计算各文件 SHA-256，构建 `relative_path` | 冯春霖 | `dir_serializer.h/.cpp` |
| 3.5 | 实现 `TransferSessionManager`：管理本机所有进行中的发送/接收 Session 的生命周期状态机 | 冯春霖 | `transfer_session_manager.h/.cpp` |
| 3.6 | 实现 Qt Drag & Drop 拖拽捕获：从拖入主窗口的事件中提取本地文件/文件夹路径列表 | 杜若贤 | 拖拽事件处理逻辑 |
| 3.7 | 实现发送侧传输任务 UI 卡片（`tw_TaskPanel`）：目标设备名、文件名、进度条、速度、剩余时间、取消按钮 | 杜若贤 | 发送任务 UI 组件 |

**Phase 3B — 接收侧**

| # | 任务描述 | 负责人 | 产出物 |
|:--|:---------|:-------|:-------|
| 3.8 | 实现 `FileReceiverWorker`（运行于独立 `QThread`）：由 `P2pServer` 为每个入站连接创建 | 高扬 | `file_receiver_worker.h/.cpp` |
| 3.9 | 实现接收侧写盘逻辑：根据 `relative_path` 递归创建目录，Chunk 数据落盘，文件完成后执行 SHA-256 校验并回送 `kTypeChunkAck` | 高扬 | 集成于 `FileReceiverWorker` |
| 3.10 | 实现接收确认弹窗 `tw_AcceptDialog`：展示发送方设备名、文件清单与总大小，提供"接受"与"拒绝"按钮 | 杜若贤 | 接收确认弹窗 |
| 3.11 | 实现接收侧传输任务 UI 卡片：与发送侧对称，展示来源设备名、接收进度与校验状态标识 | 杜若贤 | 接收任务 UI 组件 |
| 3.12 | 将 `FileSenderWorker` / `FileReceiverWorker` 的进度信号（`progressChanged`、`transferFinished`）连接至对应 UI 卡片 | 冯春霖 | 跨线程信号/槽桥接 |
| 3.13 | 实现 `kTypeCancel` 帧的发送与接收处理：触发后双端清理临时文件、关闭连接、更新 UI 状态为"已取消" | 冯春霖 | 取消逻辑 |

**Phase 3 验收标准**：两机之间完整传输一个含多级子目录的文件夹（总大小 > 1GB），接收端 SHA-256 全部校验通过，全程 UI 不卡顿，无"未响应"现象。

### 9.5 Phase 4 — 打磨与收尾（Polish & QA）

**目标**：修复已知问题，补齐体验细节，压力测试，完成答辩准备。

| # | 任务描述 | 负责人 | 产出物 |
|:--|:---------|:-------|:-------|
| 4.1 | 异常场景处理：传输中途对端强退（TCP 断开）时，本端能优雅检测、清理资源，不崩溃 | 高扬 | 异常健壮性修复 |
| 4.2 | 多网卡环境测试：在同时插有有线与 WiFi 的机器上验证广播地址筛选的正确性 | 高扬 | 多网卡测试报告 |
| 4.3 | 实现 `QSystemTrayIcon` 系统托盘：关闭主窗口时最小化至托盘；收到传输请求时弹出气泡通知 | 杜若贤 | 系统托盘模块 |
| 4.4 | 实现传输历史记录列表（`tw_HistoryList`）：展示文件名、大小、速度、状态（成功/失败/取消）、对端设备名 | 杜若贤 | 历史记录 UI |
| 4.5 | UI 整体视觉打磨：统一字体、间距、颜色；为在线状态、传输成功/失败/取消增加明确视觉区分 | 杜若贤 | 最终 UI 稿 |
| 4.6 | 配置持久化验证：设备名、接收路径在重启后正确恢复；重启后不残留旧 Session 状态 | 冯春霖 | 持久化回归测试 |
| 4.7 | 大文件压力测试：千兆交换机下传输单文件 5GB，记录平均速度与 CPU/内存峰值 | 全员 | 性能测试报告 |
| 4.8 | 边缘场景测试：并发两个 Session、零字节文件、文件名含特殊字符、磁盘空间不足 | 全员 | 问题清单与修复记录 |

### 9.6 里程碑总览

```
  Phase 1              Phase 2              Phase 3              Phase 4
  工程奠基         -->  设备发现         -->  传输核心         -->  打磨收尾
  [2~3 天]             [2~3 天]             [5~7 天]             [2~3 天]
  
  高扬: 协议定义        高扬: UDP广播引擎    高扬: TCP双端引擎    高扬: 异常处理
  杜若贤: UI骨架        杜若贤: 设备列表UI   杜若贤: 收发任务UI   杜若贤: 托盘/历史
  冯春霖: 数据结构      冯春霖: 过滤/持久化  冯春霖: 信号桥接     冯春霖: 配置验证
       │                    │                    │                    │
  可编译空壳           双机互相看见        1GB传输校验通过      答辩质量交付
```

---

## 10. V2.0 架构升级演进规划 (Future Work)

### 10.1 演进动机与设计原则

V1.0 以纯 P2P 架构快速交付了核心文件传输能力，但其去中心化特性从根本上限制了三类业务场景：

- **离线投递不可能**：目标节点不在线时，消息无处暂存，发送操作只能失败。
- **持久身份不存在**：节点以 IP + 设备名标识，换机后历史协作上下文完全丢失。
- **群体协同无支点**：无中心节点无法实现局域网范围的广播通知或聊天大厅。

V2.0 的演进目标是**在完全不破坏 V1.0 已有 P2P 传输能力的前提下**，以"热插拔"的方式为系统接入一个独立的 C/S 服务端，平滑补全上述三项能力。

**核心设计原则：**

| 原则         | 说明                                                                                         |
| :----------- | :------------------------------------------------------------------------------------------- |
| P2P 核心不重写 | 保留稳定的发现与文件传输协议；围绕 AppController、Repository 和 `ServerLink` 扩展应用能力 |
| 服务端职责边界清晰 | 服务端**永远不参与文件传输**；文件数据始终走 P2P TCP 直连通道，服务端仅处理 IM 与鉴权   |
| 协议向下兼容 | V2.0 新增 Type 码从 `0x1000` 起，不与 V1.0 的 `0x0001~0x0401` 冲突，旧版客户端可忽略未知码 |

### 10.2 网络流向变化：V1.0 vs V2.0

**V1.0 网络流向（纯 P2P，无服务端）：**

```
节点 A                                          节点 B
  │                                               │
  │  ◄──── UDP 广播 Hello (45678) ────────────►   │
  │                                               │
  │  (用户拖拽文件至 B 的设备卡片，发起传输)       │
  │                                               │
  │  ══════ TCP 直连 (B 的 tcp_port) ══════════►  │
  │  ─────  kTypeTransferReq (握手)  ──────────►  │
  │  ◄────  kTypeTransferRsp (接受)  ────────────  │
  │  ─────  kTypeDataChunk * N       ──────────►  │
  │  ◄────  kTypeChunkAck (校验通过) ────────────  │
  │  ══════ TCP 连接关闭             ══════════►  │

特点：B 不在线时，A 无法向 B 传递任何信息。
```

**V2.0 网络流向（P2P 保留 + C/S 叠加）：**

```
节点 A          中心服务端 S (GridYard-Server)           节点 B
  │                       │                              │
  │  ══ TCP 长连接 (登录) ═►                              │
  │  ── kTypeLoginReq ──► │                              │
  │  ◄─ kTypeLoginRsp ─── │                              │
  │                       │ ◄══ TCP 长连接 (登录) ══      │
  │                       │ ◄── kTypeLoginReq ──         │
  │                       │ ─── kTypeLoginRsp ──►        │
  │                       │                              │
  │  (B 此刻离线，A 向 B 发 IM 消息)                      │
  │  ── kTypeMsgSend ───► │                              │
  │                       │ [B 离线，写入 PostgreSQL]     │
  │                       │                              │
  │                       │        (次日 B 上线)          │
  │                       │ ◄══ TCP 长连接 (登录) ══      │
  │                       │ ─── kTypeOfflinePullRsp ──►  │
  │                       │                              │
  │  文件传输：完全绕过服务端 S，仍走 P2P 直连             │
  │                                                      │
  │  ══════════════ TCP P2P 直连 (文件) ════════════════► │
  │  (S 不参与文件流量，带宽不受影响)                      │
  │  ══════════════════════════════════════════════════► │

关键区别：
  文件流量  -> P2P TCP 直连（与 V1.0 完全相同）
  IM 消息   -> 经由 S 路由或离线暂存
  设备发现  -> UDP 广播（与 V1.0 相同）+ S 在线表作次级兜底
```

### 10.3 GridYard-Server 独立服务端设计

#### 10.3.1 程序形态与工程结构

`GridYard-Server` 是一个无图形界面（Headless）的 C++ 守护进程，与客户端共用同一 CMake 工程，编译为独立的可执行 target。

```
GridYard/                              # 项目根目录
├── CMakeLists.txt                  # 顶层构建文件，定义 client / server 两个 target
├── shared/                         # V1.0 + V2.0 双端共用
│   └── protocol.h                  # 全部 Type 码定义（V1.0 原码 + V2.0 扩展码）
├── client/                         # V1.0 已有，V2.0 在稳定边界上增量扩展
│   ├── network/
│   │   ├── discovery_service.h/.cpp
│   │   ├── p2p_server.h/.cpp
│   │   ├── file_sender_worker.h/.cpp
│   │   ├── file_receiver_worker.h/.cpp
│   │   ├── frame_codec.h/.cpp
│   │   └── [V2.0 新增] server_link.h/.cpp      # C/S 长连接管理
│   ├── core/
│   │   ├── transfer_session_manager.h/.cpp
│   │   ├── dir_serializer.h/.cpp
│   │   ├── config_manager.h/.cpp
│   │   └── [V2.0 新增] im_manager.h/.cpp       # IM 消息收发封装
│   ├── storage/
│   │   └── transfer_history_repository.h/.cpp  # 本地传输记录
│   └── qml/
│       └── [V2.0 新增] LoginDialog.qml         # 登录/注册对话框
└── server/                         # V2.0 新增
    ├── main.cpp                    # 守护进程入口，epoll 事件循环
    ├── client_session.h/.cpp       # 每个 TCP 连接的会话对象
    ├── msg_router.h/.cpp           # 消息路由：在线投递 / 离线暂存
    ├── offline_mailbox.h/.cpp      # PostgreSQL 离线消息读写
    └── db_pool.h/.cpp              # libpqxx PostgreSQL 连接池
```

#### 10.3.2 PostgreSQL 数据库设计（gridyard 库）

数据库名称使用 `gridyard`，先通过 migration 建立账号、会话与记录漫游所需的核心表；IM 表在启动对应子阶段时再增加：

```sql
-- 用户账户表
CREATE TABLE users (
    user_id     SERIAL PRIMARY KEY,
    username    VARCHAR(64) UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,                -- Argon2id/bcrypt 编码结果
    device_name VARCHAR(128),
    last_seen   TIMESTAMPTZ DEFAULT NOW()
);

-- 登录会话表
CREATE TABLE auth_sessions (
    session_id  UUID PRIMARY KEY,
    user_id     INT NOT NULL REFERENCES users(user_id),
    token_hash  TEXT NOT NULL,
    expires_at  TIMESTAMPTZ NOT NULL,
    revoked_at  TIMESTAMPTZ
);

-- 传输记录漫游表，只保存必要元数据
CREATE TABLE transfer_records (
    record_id    UUID PRIMARY KEY,
    user_id      INT NOT NULL REFERENCES users(user_id),
    direction    SMALLINT NOT NULL,
    display_name TEXT NOT NULL,
    total_bytes  BIGINT NOT NULL,
    status       SMALLINT NOT NULL,
    peer_name    TEXT,
    occurred_at  TIMESTAMPTZ NOT NULL,
    updated_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- 离线消息表（信箱模型）
CREATE TABLE offline_messages (
    msg_id      BIGSERIAL PRIMARY KEY,
    sender_id   INT REFERENCES users(user_id),
    receiver_id INT REFERENCES users(user_id),
    content     TEXT NOT NULL,
    sent_at     TIMESTAMPTZ DEFAULT NOW(),
    delivered   BOOLEAN DEFAULT FALSE           -- 投递成功后标记，定时任务清理
);

-- 局域网聊天大厅消息表（公共频道）
CREATE TABLE lobby_messages (
    msg_id      BIGSERIAL PRIMARY KEY,
    sender_id   INT REFERENCES users(user_id),
    content     TEXT NOT NULL,
    sent_at     TIMESTAMPTZ DEFAULT NOW()
);
```

#### 10.3.3 服务端并发模型

服务端需同时维护数十至数百条 TCP 长连接，采用如下高并发模型：

- **I/O 事件驱动**：基于 Linux `epoll` 的事件循环，主线程监听所有客户端的可读事件，不为每个连接创建独立线程，降低上下文切换开销。
- **数据库操作异步化**：PostgreSQL 读写操作统一转给独立的数据库工作线程池（生产者-消费者队列），避免数据库 I/O 阻塞主事件循环。
- **连接池**：使用 `libpqxx` 维护固定大小的 PostgreSQL 连接池（建议初始值为 CPU 核数 × 2），避免频繁建立数据库连接的开销。

### 10.4 V2.0 客户端新增模块（平滑叠加，不改已有代码）

| 新增模块             | 职责                                                                       |
| :------------------- | :------------------------------------------------------------------------- |
| `ServerLink`         | 维护与 GridYard-Server 的 TCP 长连接，处理登录、TCP 心跳、断线自动重连       |
| `ImManager`          | 封装离线/漫游消息的发送（`kTypeMsgSend`）与接收（`kTypeMsgRecv`）；在线单聊由 V1.0 P2P 的 `ChatManager` 提供 |
| `LobbyManager`       | 封装聊天大厅消息的订阅（接收广播）与发布（`kTypeLobbyBroadcast`）          |
| `RoamingSyncManager` | 登录后向服务端拉取离线消息（`kTypeOfflinePullReq`），写入本地 SQLite 缓存  |
| `TransferHistoryRepository` | 管理本地传输历史，并为记录漫游提供稳定数据边界                    |
| `MessageRepository` | 管理本地聊天记录，V1.0 在线聊天阶段即开始写入，登录后用于对话历史漫游      |

新增 UI 界面（杜若贤负责）：

| 新增界面         | 说明                                                                   |
| :--------------- | :--------------------------------------------------------------------- |
| 登录/注册对话框  | 账号密码输入，首次使用时可原地注册                                     |
| 单聊会话界面     | 使用 QML ListView/Delegate 渲染气泡消息，区分发件方与收件方           |
| 局域网聊天大厅   | 全部在线用户可见的公共频道，类似 IRC 聊天室                            |

### 10.5 V2.0 协议扩展码（向下兼容，不与 V1.0 冲突）

| Type 码  | 常量名                   | 说明                             |
| :------- | :----------------------- | :------------------------------- |
| `0x1001` | `kTypeLoginReq`          | 客户端登录请求                   |
| `0x1002` | `kTypeLoginRsp`          | 服务端登录响应                   |
| `0x1003` | `kTypeRegisterReq`       | 账号注册请求                     |
| `0x1004` | `kTypeRegisterRsp`       | 账号注册响应                     |
| `0x1010` | `kTypeServerHeartbeat`   | 客户端向服务端的 TCP 保活包      |
| `0x1011` | `kTypeOnlineStatus`      | 服务端推送好友在线状态变更       |
| `0x1020` | `kTypeMsgSend`           | 接收方离线时经服务端暂存消息     |
| `0x1021` | `kTypeMsgRecv`           | 服务端向上线客户端投递离线/漫游消息 |
| `0x1022` | `kTypeMsgAck`            | 消息送达确认                     |
| `0x1030` | `kTypeOfflinePullReq`    | 客户端请求拉取离线消息           |
| `0x1031` | `kTypeOfflinePullRsp`    | 服务端批量下发离线消息           |
| `0x1040` | `kTypeLobbyMsgSend`      | 客户端向服务端发送大厅消息       |
| `0x1041` | `kTypeLobbyBroadcast`    | 服务端广播大厅消息至所有在线客户端 |

### 10.6 数据库、登录与记录漫游边界

数据库能力分两步建设，避免把本地历史和账号漫游强耦合。

**V1.0 本地 SQLite：**

- 不要求登录即可使用。
- 保存全局记录 ID、方向、文件显示名、总大小、状态、对端设备信息、开始/结束时间和错误信息。
- 默认不保存文件内容，不向数据库写入敏感源路径。
- 数据访问位于独立 Storage/Repository 层，QML 只通过 Controller 查询。

**V2.0 登录与记录漫游：**

- 登录建立稳定 `user_id`，用于跨设备同步和权限控制；P2P 文件直传仍可匿名使用。
- 记录漫游只同步必要元数据，不上传文件内容，默认关闭并由用户显式开启。
- 每条记录使用客户端生成的全局 UUID 作为幂等键，服务端按用户归属校验并去重。
- 登录、注册、记录同步和 IM 必须运行在 TLS 或等价安全通道上。
- 密码不得直接使用 SHA-256 存储，应使用 Argon2id、bcrypt 等密码哈希并保存随机盐。

建议 V2.0 实施顺序为：账号与会话鉴权 -> 对话历史与传输记录漫游 -> 离线消息漫游。在线单聊已由 V1.0 的 P2P 聊天提供，V2.0 不再重复实现。

---

*本文档由"鸽邮 (GridYard)"项目团队整理，版本 v2.2。随开发进展持续修订更新。*
