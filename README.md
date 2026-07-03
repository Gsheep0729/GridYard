# GridYard - 鸽邮

GridYard 是一个面向局域网环境的 P2P 文件传输与在线聊天桌面应用。项目基于 Qt 6.11 / QML / C++23 构建，支持节点发现、点对点连接、聊天消息、文件传输、传输历史记录和运行日志查看。

当前版本：`v6.8.1`

| 项目 | 说明 |
| --- | --- |
| 应用类型 | Linux 桌面应用 |
| 主要平台 | Manjaro 定制系统环境 |
| 技术栈 | Qt 6.11、QML、C++23、SQLite |
| 网络模型 | 局域网 UDP 发现 + TCP P2P 传输 |
| 发布形式 | AppImage、portable tar.gz、源码构建 |

## 获取与运行

### 方式一：直接运行 AppImage

适合大多数 Linux 桌面环境。下载后赋予执行权限即可启动。

下载地址：

```text
https://github.com/Gsheep0729/GridYard/releases/download/v6.8.1/GridYard-v6.8.1-x86_64.AppImage
```

运行：

```bash
chmod +x GridYard-v6.8.1-x86_64.AppImage
./GridYard-v6.8.1-x86_64.AppImage
```

### 方式二：一键部署到应用菜单

仓库提供了 AppImage 一键部署脚本，适合需要把 GridYard 安装到系统应用菜单的场景。

```bash
sudo ./src/install-gridyard.sh
```

脚本会下载约 `92.2 MB` 的 AppImage 到 `/opt/GridYard/`，创建 `/usr/share/applications/gridyard.desktop`，复制应用图标，并刷新桌面应用数据库。执行前请确认当前网络环境稳定，并确保脚本具有 root 权限。

### 方式三：portable tar.gz 解压运行包

适合在目标环境中解压后直接运行，不需要安装到系统目录。

下载地址：

```text
https://github.com/Gsheep0729/GridYard/releases/download/v6.8.1/GridYard-v6.8.1-linux-x86_64.tar.gz
```

运行：

```bash
tar -xzf GridYard-v6.8.1-linux-x86_64.tar.gz
cd GridYard-v6.8.1-linux-x86_64
./bin/appGridYard
```

portable 包主要面向本项目的 Manjaro 定制系统环境；如果需要更强的跨发行版兼容性，优先使用 AppImage。

## 从源码构建

### 环境要求

- Manjaro Linux x86_64
- CMake 4.2.3
- GCC 16.1 或其他支持 C++23 的编译器
- Qt 6.11，需要包含 `Quick`、`Network`、`QuickControls2`、`QuickDialogs2`、`Sql`、`Widgets`
- Ninja

### 构建与运行

```bash
cmake -S src -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja -j
./build-ninja/client/appGridYard
```

### 运行测试

如果当前本地构建目录包含测试目标，可执行：

```bash
ctest --test-dir build-ninja --output-on-failure
```

## 双实例测试

GridYard 支持通过不同配置目录和端口启动多个实例，便于在同一台机器上验证聊天、节点发现和文件传输。

```bash
./build-ninja/client/appGridYard --config ~/.gridyard-a --port 45455 --name node-a
./build-ninja/client/appGridYard --config ~/.gridyard-b --port 45456 --name node-b
```

## 主要功能

- 局域网节点发现与在线状态显示
- P2P 文本消息发送与接收
- 点对点文件传输
- SQLite 本地历史记录
- 传输任务与传输进度展示
- 运行日志与调试信息查看
- QML 桌面界面
- AppImage 一键部署脚本
- portable 解压运行包

## 运行数据位置

GridYard 会在用户目录下保存运行配置、数据库和日志。不同实例可使用不同 `--config` 参数隔离运行数据。

| 类型 | 默认位置 |
| --- | --- |
| 配置目录 | `~/.config/GridYard` |
| 数据目录 | `~/.local/share/GridYard` |
| 数据库 | `~/.local/share/GridYard/database/gridyard-history.sqlite` |
| 日志 | `~/.local/share/GridYard/logs/` |
| 接收文件 | `~/GridYard/document` |

## 技术概要

GridYard 的实现重点放在桌面应用、局域网通信和可交付部署三个方面。

- 界面层：QML 负责页面、状态展示和用户交互。
- 适配层：C++ ViewModel 将界面事件转换为业务调用，并向 QML 暴露状态。
- 业务层：负责节点管理、消息处理、文件传输、历史记录和日志。
- 基础设施层：封装 UDP、TCP、SQLite、配置目录和文件系统操作。

网络通信采用 UDP 广播进行局域网节点发现，使用 TCP 建立点对点连接。应用内部协议采用 TLV 风格的消息封装，便于区分聊天消息、文件元信息、文件块、传输确认和错误信息。

更完整的架构、协议、测试和部署说明见 `doc/dev-manual/`。

## 仓库目录结构

```text
GridYard/
├── README.md
├── CLAUDE.md
├── doc/
│   ├── api-docs/
│   ├── delivery/
│   ├── dev-manual/
│   │   ├── 规格与设计/
│   │   ├── 开发心得/
│   │   └── 测试与部署/
│   ├── diagrams/
│   ├── 试卷要求符合性审查结果.local.md
│   ├── 5-软件工程综合实训2-1(C++方向)_试卷.local.md
│   ├── 团队分工说明.md
│   └── CQNU.png
├── release/
│   └── v6.8.1/
└── src/
    ├── CMakeLists.txt
    ├── install-gridyard.sh
    ├── shared/
    │   ├── protocol.h
    │   ├── data_types.h
    │   ├── chat_message.h
    │   ├── chat_message.cpp
    │   ├── frame_codec.h
    │   └── frame_codec.cpp
    ├── client/
    │   ├── main.cpp
    │   ├── Main.qml
    │   ├── core/
    │   ├── domain/
    │   ├── storage/
    │   ├── network/
    │   ├── ui/
    │   ├── icons/
    │   ├── images/
    │   └── utils/
    ├── scripts/
    └── server/
```

### 主要目录说明

| 路径 | 说明 |
| --- | --- |
| `src/shared/` | 协议常量、共享数据结构、聊天消息和 TLV 帧编解码 |
| `src/client/` | 桌面客户端入口、QML 界面、应用逻辑和网络实现 |
| `src/client/core/` | 应用控制器、配置、日志、聊天、传输和历史管理 |
| `src/client/network/` | 节点发现、P2P 服务、聊天连接和文件传输 Worker |
| `src/client/storage/` | SQLite 数据库 Broker 与历史记录 Repository |
| `src/install-gridyard.sh` | AppImage 一键部署脚本 |
| `doc/dev-manual/` | 开发、测试、部署和设计文档 |
| `release/` | 本地 release 打包输出目录，默认不纳入 Git 跟踪 |

## 文档索引

- [技术需求与系统设计规格说明书](doc/dev-manual/规格与设计/鸽邮(GridYard)——技术需求与系统设计规格说明书.md)
- [架构设计与演进说明](doc/dev-manual/规格与设计/鸽邮(GridYard)——V1.0架构设计与V2.0演进说明书.md)
- [业务逻辑说明](doc/dev-manual/规格与设计/GridYard业务逻辑说明.md)
- [本地数据层设计](doc/dev-manual/规格与设计/GridYard_Stage6_本地数据层设计.md)
- [CMake 构建逻辑与分组说明](doc/dev-manual/规格与设计/GridYard_CMake构建逻辑与分组说明.md)
- [开发心得与踩坑记录](doc/dev-manual/开发心得/开发心得_从架构设计到踩坑记录.md)
- [Linux AppImage 与 portable 打包指南](doc/dev-manual/测试与部署/打包指南_Linux_AppImage.md)

## 团队

软件工程综合实训 2-1 C++ 方向课程项目。

## 许可证

本项目用于课程实训与学习交流。如需用于其他用途，请先确认课程与团队要求。
