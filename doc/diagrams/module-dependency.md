# GridYard 模块依赖关系图（v4.16.0 当前架构）

按 `#include` 真实依赖绘制，颜色对应四层架构。所有依赖方向均为「上层 → 下层」或「同层互引」，无环依赖。

```mermaid
graph TD
    subgraph "表现层 (QML)"
        mainQml[Main.qml]
        uiComp["DeviceCard / PeerListView<br/>DeviceSessionView / TransferPanel<br/>TransferTaskCard / AcceptDialog<br/>SettingsDialog / FileTypeIcon"]
        fmtJs[FormatUtils.js]
        styleJs[Style.js]
    end

    subgraph "应用逻辑层 (C++)"
        appCtrl[AppController]
        config[ConfigManager]
        tsm[TransferSessionManager]
        dirSer[DirSerializer]
        logger[Logger]
    end

    subgraph "领域层 (C++)"
        disc[DiscoveryService]
        p2p[P2pServer]
        sender[FileSenderWorker]
        receiver[FileReceiverWorker]
    end

    subgraph "共享层 (C++)"
        fc[FrameCodec]
        proto[protocol.h<br/>Type / ErrorCode / 版本]
        dt[data_types.h<br/>PeerInfo / FileEntry / TransferSession]
        fi[gy::FileItem<br/>DirSerializer.h 内定义]
    end

    subgraph "程序入口"
        mainCpp[main.cpp]
    end

    subgraph "Qt QML 引擎"
        qmlEngine[QQmlApplicationEngine]
    end

    %% 程序入口
    mainCpp --> qmlEngine
    mainCpp --> logger
    mainCpp --> appCtrl

    %% QML 表现层
    mainQml --> appCtrl
    mainQml --> config
    mainQml --> uiComp
    uiComp --> fmtJs
    uiComp --> styleJs
    uiComp --> appCtrl

    %% 应用逻辑层互引
    appCtrl --> config
    appCtrl --> tsm
    appCtrl --> disc
    appCtrl --> p2p

    tsm --> config
    tsm --> disc
    tsm --> p2p
    tsm --> sender
    tsm --> receiver

    %% 领域层
    disc --> config
    disc --> dt
    p2p --> config
    p2p --> receiver
    sender --> dirSer
    sender --> fc
    receiver --> dirSer
    receiver --> fc

    %% 共享层
    fc --> proto
    dirSer --> fi

    %% QML 引擎
    qmlEngine -.->|loadFromModule| mainQml
    qmlEngine -.->|实例化 QML_SINGLETON| appCtrl
    qmlEngine -.->|实例化 QML_SINGLETON| config

    style mainQml fill:#f8bbd0
    style uiComp fill:#f8bbd0
    style fmtJs fill:#f8bbd0
    style styleJs fill:#f8bbd0
    style appCtrl fill:#c8e6c9
    style config fill:#c8e6c9
    style tsm fill:#c8e6c9
    style dirSer fill:#c8e6c9
    style logger fill:#c8e6c9
    style disc fill:#bbdefb
    style p2p fill:#bbdefb
    style sender fill:#bbdefb
    style receiver fill:#bbdefb
    style fc fill:#ffe0b2
    style proto fill:#ffe0b2
    style dt fill:#ffe0b2
    style fi fill:#ffe0b2
    style mainCpp fill:#e1f5fe
    style qmlEngine fill:#eeeeee
```

## 颜色对照

| 颜色 | 层 | 关键模块 | 职责 |
|:---|:---|:---|:---|
| 蓝色 | 程序入口 | `main.cpp` | QGuiApplication + Logger 初始化 + 加载 QML |
| 粉色 | 表现层 | QML 文件 + FormatUtils.js | 页面、绑定、无状态展示计算 |
| 绿色 | 应用逻辑层 | AppController / ConfigManager / TransferSessionManager / DirSerializer / Logger | 组装对象、配置、会话生命周期、序列化与日志 |
| 蓝色（深） | 领域层 | DiscoveryService / P2pServer / Workers | UDP 发现、TCP 入站、文件收发 |
| 橙色 | 共享层 | FrameCodec / protocol.h / data_types.h / FileItem | TLV 协议、Type 码、ErrorCode、POD |
| 灰色 | Qt QML 引擎 | QQmlApplicationEngine | 加载 QML、实例化 C++ 单例 |

## 依赖方向约束

- **自上而下**：表现层 → 应用逻辑层 → 领域层 → 共享层，禁止反向依赖。
- **共享层零依赖**：`protocol.h` 只依赖 `<QtGlobal>`；`data_types.h` 只依赖 Qt 头；`frame_codec.h` 依赖 `protocol.h`。
- **应用逻辑层不直接操作网络**：`TransferSessionManager` 通过 `P2pServer` 与 `Workers` 间接访问 TCP，本身不持有 socket。
- **领域层不感知 QML**：所有领域类（DiscoveryService 除外，带 `QML_ANONYMOUS` 标记）不暴露给 QML，仅通过 `AppController` 与 `TransferSessionManager` 中转。
- **Worker 之间不互引**：`FileSenderWorker` 与 `FileReceiverWorker` 没有直接 `#include`，协议交互通过 FrameCodec 与 TCP 帧完成。

## 同层互引明细

| 调用方 | 被调用方 | 用途 |
|:---|:---|:---|
| AppController | ConfigManager | 获取全局实例 |
| AppController | DiscoveryService / P2pServer / TransferSessionManager | 创建并持有 |
| TransferSessionManager | ConfigManager | 读取 deviceId / deviceName / receivePath / autoAcceptFiles |
| TransferSessionManager | DiscoveryService | 查询目标设备 PeerInfo |
| TransferSessionManager | P2pServer | 订阅入站 transferRequestReceived |
| TransferSessionManager | FileSenderWorker / FileReceiverWorker | 创建 worker、连接信号、跨线程 invoke |
| P2pServer | FileReceiverWorker | 为每个入站连接创建 worker 实例 |
| DiscoveryService | ConfigManager | 读取本机身份与端口 |
| FileSenderWorker / FileReceiverWorker | DirSerializer | 枚举文件与计算 SHA-256（共享 FileItem 定义） |
| FileSenderWorker / FileReceiverWorker | FrameCodec | TLV 编解码 |
