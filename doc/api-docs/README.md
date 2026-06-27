# GridYard API 文档索引

本文档是 GridYard 项目内部 API 接口文档的入口索引，按总览和模块文档组织。

---

## 文档列表

| 序号 | 模块 | 文档 | 核心内容 |
|------|------|------|----------|
| 00 | 接口总览与数据库连接指南 | [00-接口总览与数据库连接指南.md](./00-接口总览与数据库连接指南.md) | QML/C++ 公共接口总表、命令行入口、SQLite Schema、数据库连接与排查教程 |
| 01 | 设备发现模块 | [01-设备发现模块.md](./01-设备发现模块.md) | DiscoveryService、PeerInfo、UDP 广播、心跳机制 |
| 02 | 传输管理模块 | [02-传输管理模块.md](./02-传输管理模块.md) | TransferSessionManager、FileSenderWorker、FileReceiverWorker |
| 03 | 应用与配置模块 | [03-应用与配置模块.md](./03-应用与配置模块.md) | AppController、ConfigManager、Logger、命令行配置隔离 |
| 04 | 协议与编解码模块 | [04-协议与编解码模块.md](./04-协议与编解码模块.md) | protocol.h、FrameCodec、TLV 帧格式、ErrorCode |
| 05 | UI 组件模块 | [05-UI组件模块.md](./05-UI组件模块.md) | QML 组件、Style.js、FormatUtils.js、ChatView |
| 06 | 全局 UI 设计规范 | [06-全局UI设计规范.md](./06-全局UI设计规范.md) | 配色、圆角、间距、动画、交互、字体规范 |
| 07 | 在线聊天模块 | [07-在线聊天模块.md](./07-在线聊天模块.md) | ChatManager、ChatConnection、ChatMessageModel、P2P 文本聊天 |
| 08 | 本地数据层模块 | [08-本地数据层模块.md](./08-本地数据层模块.md) | Repository 端口、SQLite Proxy、HistoryController、数据库设计、连接教程、异常恢复 |

---

## 模块依赖关系

```
QML (表现层)
    ↓ 通过 AppController 单例访问
AppController (应用逻辑层)
    ↓ 暴露 Controller/ViewModel 门面
    ├── PeerDiscoveryViewModel ← 持有 DiscoveryService
    ├── TransferController ← 持有 TransferSessionManager
    │       ↓ 使用
    │       ├── FileSenderWorker (发送)
    │       ├── FileReceiverWorker (接收)
    │       └── DirSerializer (目录遍历)
    ├── ChatController ← 持有 ChatManager
    │       ↓ 使用
    │       ├── ChatConnection (单条 TCP 连接)
    │       └── ChatMessageModel (消息列表模型)
    ├── HistoryController ← 通过 LocalDataBroker 访问
    │       ↓ 使用
    │       ├── IMessageRepository (聊天记录端口)
    │       └── ITransferHistoryRepository (传输历史端口)
    └── P2pServer (TCP 服务器)
            ↓ 按首帧 Type 分流
            ├── FileReceiverWorker (文件传输)
            └── ChatConnection (在线聊天)
                    ↓ 使用
                    └── FrameCodec (TLV 编解码)
                            ↓ 引用
                            └── protocol.h (协议常量与错误码)
```

---

## 快速入口

### QML 调用入口

```qml
AppController.peerDiscoveryViewModel.refresh()
AppController.transferController.createSendSession(deviceId, filePath)
AppController.chatController.sendText(deviceId, content)
AppController.historyController.loadMoreMessages(deviceId)
ConfigManager.openFolder(ConfigManager.receivePath)
```

### 本地数据库入口

```text
<QStandardPaths::AppDataLocation>/database/gridyard-history.sqlite
```

核心规则：

```text
聊天记录和传输历史按“对端 device_id”归属，不按设备名、IP 或端口归属。
```

---

## 四层架构映射

| 层级 | 模块 | 文档 |
|------|------|------|
| 表现层 | Main.qml、DeviceCard、PeerListView、DeviceSessionView、ChatView、AcceptDialog、SettingsDialog、TransferTaskCard | 05-UI组件模块 |
| 应用逻辑层 | AppController、PeerDiscoveryViewModel、TransferController、ChatController、TransferSessionManager、ChatManager、HistoryController | 03-应用与配置模块、02-传输管理模块、07-在线聊天模块、08-本地数据层模块 |
| 领域层 | DiscoveryService、P2pServer、FileSenderWorker、FileReceiverWorker、ChatConnection、FrameCodec、protocol.h、Repository 端口和值对象 | 01-设备发现模块、02-传输管理模块、04-协议与编解码模块、07-在线聊天模块、08-本地数据层模块 |
| 数据管理层 | ConfigManager、Logger、LocalDataBroker、SqliteDatabaseBroker、SqliteDeviceRepository、SqliteMessageRepository、SqliteTransferHistoryRepository | 03-应用与配置模块、08-本地数据层模块 |

---

## 编写规范

本文档体系参考了 Qt 官方开源项目文档、阿里桌面客户端及网易云音乐 PC 端 Qt 项目的文档管理规范，结合 GridYard 项目的 C++23 + Qt6 QML 技术栈特点进行了适配：

- 每个模块包含：版本历史、概述、数据类型、公共接口、信号、QML 调用示例、数据流
- 版本历史放在文档开头，基于源码 Change Log 和 Git 提交记录交叉核实
- 接口描述包含：参数表、返回值、线程安全说明、文件路径
- 跨模块引用使用 `[模块名](./xx-模块名.md)` 格式
- 全局 UI 设计规范独立成文，配色/圆角/间距/动画常量统一管理
