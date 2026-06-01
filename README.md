# GridYard — 鸽邮

> 局域网 P2P 文件传输 + IM 桌面应用。两台同网段电脑互相能看见、能直传文件，全程不走公网。

| 字段       | 内容                                                                  |
| :--------- | :-------------------------------------------------------------------- |
| 项目版本   | v4.5.1                                                                |
| 当前阶段   | **Stage 4 健壮性增强** 进行中                                          |
| 技术栈     | C++23 · Qt 6.11 · QML · CMake 4.2.3 · GCC 16.1                        |
| 部署平台   | Manjaro Linux（开发、编译、运行三端统一）                              |
| 团队成员   | 高扬 · 杜若贤 · 冯春霖                                                |

---

## 快速加入开发

### 1. 环境准备

确保本地已安装：
- GCC 15+
- CMake 4.2.3+
- Qt 6.5+（含 Quick、Network、QuickControls2 模块）
- Ninja（推荐）

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
cmake --build build -j

# 运行客户端
./build/client/appGridYard
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
./build/client/appGridYard --port 35100 --name "接收端" &
./build/client/appGridYard --port 35101 --name "发送端" &
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
│   ├── spec/                              # 规格与设计文档
│   ├── manuals/                           # 开发手册与踩坑心得
│   ├── plans/                             # 开发计划
│   ├── diagrams/                          # UML 架构图
│   └── templates/                         # 代码模板与集成示例
├── scripts/                               # 辅助脚本
│   ├── for_md.py                          # 代码归档工具
│   └── test_loopback.sh                   # 本机回环测试脚本
└── src/                                   # 源代码
    ├── CMakeLists.txt                     # 顶层构建配置
    ├── shared/                            # 共用核心库 gy_shared
    │   ├── protocol.h                     # 通信协议 Type 码定义
    │   ├── data_types.h                   # 跨模块数据类型（PeerInfo 等）
    │   └── frame_codec.{h,cpp}            # TLV 帧编解码器
    ├── client/                            # 桌面客户端
    │   ├── main.cpp                       # 程序入口
    │   ├── Main.qml                       # QML 根窗口
    │   ├── core/                          # 业务逻辑层
    │   │   ├── app_controller.{h,cpp}     # 全局控制器（QML_SINGLETON）
    │   │   ├── config_manager.{h,cpp}     # 配置管理器
    │   │   └── transfer_session_manager.* # 传输会话管理（Stage 3）
    │   ├── network/                       # 网络通信层
    │   │   ├── discovery_service.{h,cpp}  # UDP 设备发现
    │   │   ├── p2p_server.{h,cpp}         # TCP 文件传输服务器
    │   │   ├── file_sender_worker.*       # 文件发送 Worker
    │   │   └── file_receiver_worker.*     # 文件接收 Worker
    │   └── ui/                            # QML 界面组件
    │       ├── DeviceCard.qml             # 设备卡片
    │       ├── PeerListView.qml           # 设备列表
    │       ├── SettingsDialog.qml         # 设置对话框
    │       ├── AcceptDialog.qml           # 接收确认弹窗
    │       └── TransferPanel.qml          # 传输面板
    ├── server/                            # V2.0 服务端（占位）
    └── tests/                             # 单元测试
        └── test_frame_codec.cpp           # FrameCodec 测试
```

---

## 开发阶段

| 阶段 | 名称 | 状态 | 说明 |
|:-----|:-----|:-----|:-----|
| Stage 0 | 工程奠基 | ✅ 完成 | 项目骨架、构建配置 |
| Stage 1 | 通信基石 | ✅ 完成 | 协议定义、帧编解码、C++↔QML 通信 |
| Stage 2 | 设备发现 | ✅ 完成 | UDP 广播、设备列表、配置管理 |
| Stage 3 | 文件传输 | 🔄 进行中 | P2P 文件传输、进度显示 |
| Stage 4 | 健壮性 | ⏳ 待开始 | 文件夹传输、SHA-256 校验 |
| Stage 5 | 体验打磨 | ⏳ 待开始 | 托盘、配置持久化 |

---

## 相关文档

| 文档 | 路径 | 说明 |
|:-----|:-----|:-----|
| 阶段计划 | `doc/plans/鸽邮(GridYard)——软件开发阶段计划.md` | 现在该做什么 |
| 架构设计 | `doc/spec/鸽邮(GridYard)——V1.0架构设计与V2.0演进说明书.md` | 架构决策 |
| 开发手册 | `doc/manuals/鸽邮(GridYard)——团队开发者手册.md` | 工程实践 |
| 开发心得 | `doc/manuals/开发心得_从架构设计到踩坑记录.md` | 踩坑记录 |
| UML 图 | `doc/diagrams/` | 类图、组件图、序列图 |
