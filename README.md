# GridYard — 鸽邮

> 局域网 P2P 文件传输 + IM 桌面应用。两台同网段电脑互相能看见、能直传文件，全程不走公网。

| 字段       | 内容                                                                  |
| :--------- | :-------------------------------------------------------------------- |
| 项目版本   | v4.16.6                                                               |
| 当前阶段   | **Stage 5 局域网在线聊天**（阶段 D 已完成，准备会话页集成）                  |
| 技术栈     | C++23 · Qt 6.11 · QML · CMake 4.2.3 · GCC 16.1                        |
| 部署平台   | Manjaro Linux（开发、编译、运行三端统一）                              |
| 团队成员   | 高扬 · 杜若贤 · 冯春霖                                                |

---

## 四层架构

项目按老师要求，以“表现层、应用逻辑层、领域层、数据管理层”作为架构目标。当前已经完成前三层的主要职责划分，数据管理层仍处于过渡状态，因此属于**基本符合四层架构，但还没有完全分层到位**。

```text
表现层
  Main.qml、client/ui/
  页面布局、用户交互、属性绑定和状态展示
        ↓ 用户操作                  ↑ 界面状态
应用逻辑层
  AppController（QML 单例）、TransferSessionManager
  组织设备发现、文件传输和后续在线聊天用例，协调各领域对象
        ↓ 调用                      ↑ 结果与事件
领域层
  DiscoveryService、P2pServer、FileSenderWorker、FileReceiverWorker
  DirSerializer、FrameCodec、protocol.h、data_types.h
  实现设备发现、传输规则、协议编解码和目录处理
        ↓ 读写                      ↑ 数据
数据管理层
  ConfigManager、Logger、QSettings、文件系统和网络连接
  负责配置持久化、日志、文件读写与外部数据访问
```

| 层级 | 当前代码位置 | 符合情况 |
|:-----|:-------------|:---------|
| 表现层 | `src/client/Main.qml`、`src/client/ui/` | 较完整。QML 只负责页面和交互，没有直接操作网络与文件系统 |
| 应用逻辑层 | `src/client/core/app_controller.*`、`transfer_session_manager.*` | 较完整。负责组装对象、组织发送和接收流程 |
| 领域层 | `src/client/network/`、`dir_serializer.*`、`src/shared/` | 基本形成。包含设备发现、传输过程、协议和核心数据类型 |
| 数据管理层 | `config_manager.*`、`logger.*`，以及 Worker 内部的文件和网络 I/O | 尚未完全独立。持久化和外部访问仍分散在 `core/` 与 `network/` 中 |

后续若加入 SQLite 传输历史，应新增独立的数据管理模块，并通过仓储接口向应用逻辑层提供数据；同时逐步把 Worker 中可分离的文件访问细节下沉。这样才能从“职责上基本符合”推进到“目录和依赖方向都严格符合”。

---

## 快速加入开发

### 1. 环境准备

确保本地已安装：
- GCC 15+
- CMake 4.2.3+
- Qt 6.5+（含 Quick、Network、QuickControls2 模块）
- Ninja（推荐）
- Linux 桌面门户及对应桌面后端（KDE 使用 `xdg-desktop-portal-kde`）

### 2. 克隆仓库

```bash
git clone <仓库地址>
cd GridYard
```

### 3. 构建与运行

```bash
# 进入源码目录
cd src

# 首次配置（使用 Ninja）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 增量构建
cmake --build build-ninja -j

# 运行客户端
./build-ninja/client/appGridYard
```

### 4. 运行测试

```bash
# 跑全部单测
ctest --test-dir build --output-on-failure
```

### 5. 本机回环测试（Stage 3+）

单机测试文件传输功能，启动两个实例：

```bash
# 方式 1：使用测试脚本
./scripts/test_loopback.sh

# 方式 2：手动启动两个实例
./build-ninja/client/appGridYard --port 35100 --name "接收端" &
./build-ninja/client/appGridYard --port 35101 --name "发送端" &
```

**命令行参数**：
- `--port <port>`：指定 TCP 端口（默认 35100）
- `--name <name>`：指定设备名称
- `--config <path>`：指定配置文件路径

---

## 仓库目录结构

```text
GridYard/
├── README.md                              # 本文件
├── .gitignore
├── doc/                                   # 文档目录
│   ├── diagrams/                          # UML 架构图
│   │   ├── rendered/                      # PNG/SVG 格式图
│   │   └── README.md
│   └── dev-manual/                        # 开发手册
│       ├── 规格与设计/                     # 技术规格、架构设计
│       ├── 开发心得/                       # 团队手册、踩坑记录
│       └── 测试与部署/                     # 测试流程、打包指南
├── scripts/                               # 辅助脚本
│   └── for_md.py                          # 代码归档工具
└── src/                                   # 源代码
    ├── CMakeLists.txt                     # 顶层构建配置
    ├── shared/                            # 共用核心库 gy_shared
    │   ├── protocol.h                     # 通信协议 Type 码定义
    │   ├── data_types.h                   # 跨模块数据类型（PeerInfo 等）
    │   ├── chat_message.{h,cpp}           # 在线聊天消息 JSON 编解码
    │   └── frame_codec.{h,cpp}            # TLV 帧编解码器
    ├── client/                            # 桌面客户端
    │   ├── main.cpp                       # 程序入口
    │   ├── Main.qml                       # QML 根窗口
    │   ├── core/                          # 业务逻辑层
    │   │   ├── app_controller.{h,cpp}     # 全局控制器（QML_SINGLETON）
    │   │   ├── config_manager.{h,cpp}     # 配置管理器
    │   │   ├── transfer_session_manager.* # 传输会话管理
    │   │   └── dir_serializer.{h,cpp}     # 目录序列化工具
    │   ├── network/                       # 网络通信层
    │   │   ├── discovery_service.{h,cpp}  # UDP 设备发现
    │   │   ├── p2p_server.{h,cpp}         # TCP 文件传输服务器
    │   │   ├── file_sender_worker.*       # 文件发送 Worker
    │   │   └── file_receiver_worker.*     # 文件接收 Worker
    │   ├── ui/                            # QML 界面组件
    │   │   ├── DeviceCard.qml             # 设备卡片
    │   │   ├── PeerListView.qml           # 设备列表
    │   │   ├── DeviceSessionView.qml       # 设备会话页
    │   │   ├── SettingsDialog.qml         # 设置对话框
    │   │   ├── AcceptDialog.qml           # 接收确认弹窗
    │   │   ├── TransferPanel.qml          # 传输面板
    │   │   ├── TransferTaskCard.qml       # 传输任务卡片
    │   │   └── FileTypeIcon.qml          # 文件类型图标
    │   └── utils/                         # QML 工具模块
    │       ├── Style.js                   # 样式常量
    │       └── FormatUtils.js             # 格式化工具
    ├── server/                            # V2.0 服务端（占位）
    └── tests/                             # 单元测试
        ├── test_frame_codec.cpp           # FrameCodec 编解码测试
        ├── test_edge_cases.cpp            # 边缘场景测试
        ├── test_transfer.cpp              # 传输功能测试
        ├── test_config_manager.cpp        # 配置管理器测试
        ├── test_discovery.cpp             # 设备发现测试
        ├── test_file_transfer.cpp         # 文件传输集成测试
        ├── test_session_manager.cpp       # 会话管理测试
        └── test_integration.cpp           # 综合集成测试
```

---

## 开发阶段

| 阶段 | 名称 | 状态 | 说明 |
|:-----|:-----|:-----|:-----|
| Stage 0 | 工程奠基 | ✅ 完成 | 项目骨架、构建配置 |
| Stage 1 | 通信基石 | ✅ 完成 | 协议定义、帧编解码、C++↔QML 通信 |
| Stage 2 | 设备发现 | ✅ 完成 | UDP 广播、设备列表、配置管理 |
| Stage 3 | 文件传输 | ✅ 完成 | P2P 文件传输、拖拽传输、取消功能 |
| Stage 4 | 健壮性增强 | ✅ 完成 | 目录传输、SHA-256、协议分级防护、错误码和接收侧后台化 |
| Stage 5 | 局域网在线聊天 | 🔧 进行中 | 阶段 A/B/C/D 已完成：消息协议、首帧分流、连接管理、内存会话与聊天视图；下一步会话页集成 |
| Stage 6 | 本地数据层 | ⏳ 待开始 | SQLite 聊天记录与传输历史、托盘、体验打磨 |
| Stage 7 | 服务端与漫游 | ⭐ 加分 | 登录、对话历史与传输记录漫游、离线消息 |

---

## 表现层内部职责

四层架构中的表现层采用“QML 页面、JavaScript 交互编排、C++ 状态入口”的实现方式：

| 层级 | 主要职责 | 不应承担的职责 |
|:-----|:---------|:---------------|
| QML 表现层 | 页面结构、组件样式、属性绑定、视觉状态和基础声明式动画 | 文件读写、网络通信、传输业务规则 |
| JavaScript 表现逻辑层 | 较复杂的动画编排、交互状态转换、格式化等纯展示计算 | 网络和文件系统操作、持久化、核心业务状态 |
| C++ 状态入口 | 向表现层提供设备、配置和传输状态，以及用户操作入口 | 逐帧控制界面动画、直接依赖 QML 页面对象 |

其中动画仍由 QML 的 `Behavior`、`Transition`、`NumberAnimation` 等类型负责实际执行；当动画步骤较多或需要复用时，再由独立 JavaScript 资源负责触发顺序和参数计算。QML 通过信号向上汇报用户操作，通过 `Q_PROPERTY` 和属性绑定接收 C++ 状态。

### 当前界面情况

- QML 页面目前负责布局、交互和展示状态，符合表现层定位。
- 当前尚未加入实际界面动画，也没有独立 `.js` 资源。
- 少量格式化、列表筛选和状态文字计算仍是 QML 内联函数，后续会按复用程度逐步抽离。

---

## 近期修复

- v4.14.1：深度优化传输记录清理对话框，修复长文件名显示裁剪问题，强化危险操作视觉提示
- v4.14.0：传输记录支持单条或批量清理，并可删除接收成功的本地文件
- v4.13.3：修复文件夹传输任务闪烁，并保持根目录预览下拉栏的展开状态
- v4.13.2：优化接收文件夹确认信息，显示文件夹名、递归文件数量、总大小和根目录预览
- v4.13.1：传输记录使用内置 SVG 文件类型图标，修复文件夹名称、展开列表和操作按钮布局
- v4.12.1：修复文件夹传输重复读取和大型文件传输超时，增加写队列背压与完整完成握手
- v4.12.0：抽离共享展示格式化逻辑，增加设备、任务、设置页和提示弹窗的状态过渡
- v4.11.0：优化文件夹传输，支持空目录、保留顶层目录、接收重名避让和自动接收保存
- v4.10.1：修复传输记录显示问题，补齐接收会话字段，代码规范整改（头注释版本号、QML id 命名）
- v4.10.0：改进任务卡片布局（方向标识、时间、设备名称），支持移除已完成记录
- v4.9.0：主界面改为按设备切换传输会话，点击设备后可查看该设备任务并选择文件或文件夹发送
- v4.8.3：文件传输请求使用发送方当前设备别名，不再显示系统主机名
- v4.8.2：KDE 环境下通过桌面门户打开系统原生文件选择器，不再依赖 `kdialog` 或 `zenity`

---

## 相关文档

| 文档 | 路径 | 说明 |
|:-----|:-----|:-----|
| 阶段计划 | `doc/plans/鸽邮(GridYard)——软件开发阶段计划.md` | 现在该做什么 |
| 需求规格 | `doc/spec/鸽邮(GridYard)——技术需求与系统设计规格说明书.md` | 功能与协议需求 |
| 架构设计 | `doc/spec/鸽邮(GridYard)——V1.0架构设计与V2.0演进说明书.md` | 架构决策 |
| 业务逻辑 | `doc/spec/GridYard业务逻辑说明.md` | 流程图与协议格式 |
| 开发手册 | `doc/manuals/鸽邮(GridYard)——团队开发者手册.md` | 工程实践 |
| 开发心得 | `doc/manuals/开发心得_从架构设计到踩坑记录.md` | 踩坑记录 |
| UML 图 | `doc/diagrams/` | 类图、组件图、序列图 |
