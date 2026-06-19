# GridYard 组件图（v4.16.0 当前架构）

按 `src/` 当前目录树与 CMake 目标划分组件，体现「共享静态库 + 客户端可执行文件 + 测试 + 服务端占位」四个构建单元，以及 Qt QML 引擎与外部资源的关系。

```mermaid
graph TB
    subgraph "src/shared  (gy_shared 静态库)"
        protocol["protocol.h<br/>Type 码 / 版本号 / ErrorCode / maxPayloadForType"]
        data_types["data_types.h<br/>PeerInfo / FileEntry / TransferSession"]
        frame_codec["frame_codec.h/cpp<br/>FrameCodec"]
    end

    subgraph "src/client  (appGridYard 可执行文件)"
        main_cpp["main.cpp<br/>程序入口 / Logger 初始化"]

        subgraph "client/core  应用逻辑层"
            app_ctrl["app_controller.h/cpp<br/>AppController (QML_SINGLETON)"]
            config["config_manager.h/cpp<br/>ConfigManager (QML_SINGLETON)"]
            session["transfer_session_manager.h/cpp<br/>TransferSessionManager"]
            dir_ser["dir_serializer.h/cpp<br/>DirSerializer / FileItem"]
            logger["logger.h/cpp<br/>Logger (单例)"]
        end

        subgraph "client/network  领域层"
            discovery["discovery_service.h/cpp<br/>DiscoveryService (UDP)"]
            p2p["p2p_server.h/cpp<br/>P2pServer (TCP / 后台线程)"]
            sender["file_sender_worker.h/cpp<br/>FileSenderWorker (Worker-Object)"]
            receiver["file_receiver_worker.h/cpp<br/>FileReceiverWorker (Worker-Object)"]
        end

        subgraph "client/utils  表现层工具"
            format_js["FormatUtils.js<br/>.pragma library<br/>formatBytes / formatTime"]
            style_js["Style.js<br/>.pragma library<br/>Color / Radius / Space / Motion"]
        end

        subgraph "client/ui  QML 表现层"
            main_qml["Main.qml<br/>ApplicationWindow"]
            ui_components["DeviceCard / PeerListView<br/>DeviceSessionView / TransferPanel<br/>TransferTaskCard / AcceptDialog<br/>SettingsDialog / FileTypeIcon"]
        end
    end

    subgraph "src/server  V2.0 占位"
        server_stub["占位（Stage 7 实施）"]
    end

    subgraph "src/tests  Qt Test"
        tests["test_frame_codec / test_edge_cases<br/>test_transfer / test_config_manager<br/>test_discovery / test_file_transfer<br/>test_session_manager / test_integration"]
    end

    subgraph "Qt QML 引擎"
        qml_engine["QQmlApplicationEngine"]
        moc["moc 元对象编译器"]
    end

    subgraph "外部资源"
        qsettings[("QSettings<br/>~/.config/GridYard.conf")]
        filesystem["FileSystem<br/>~/GridYard/{document,logs}"]
        network["UDP / TCP Sockets"]
    end

    %% 项目内依赖
    frame_codec --> protocol
    frame_codec --> data_types
    app_ctrl --> config
    app_ctrl --> discovery
    app_ctrl --> p2p
    app_ctrl --> session
    session --> config
    session --> discovery
    session --> p2p
    session --> sender
    session --> receiver
    discovery --> config
    discovery --> data_types
    p2p --> config
    p2p --> receiver
    sender --> dir_ser
    sender --> frame_codec
    receiver --> dir_ser
    receiver --> frame_codec

    %% 程序入口与引擎
    main_cpp --> qml_engine
    main_cpp --> logger
    main_cpp --> app_ctrl
    qml_engine -->|loadFromModule| main_qml
    qml_engine -->|实例化| app_ctrl
    qml_engine -->|实例化| config
    moc -.->|编译时处理 Q_OBJECT| app_ctrl
    moc -.->|编译时处理 Q_OBJECT| config

    %% QML 端
    main_qml --> app_ctrl
    main_qml --> config
    main_qml --> ui_components
    ui_components --> format_js
    ui_components --> style_js

    %% 外部资源
    config --> qsettings
    config --> filesystem
    logger --> filesystem
    discovery --> network
    p2p --> network
    sender --> network
    receiver --> network

    %% 测试依赖
    tests -.->|链接| frame_codec
    tests -.->|链接| config
    tests -.->|链接| session

    style protocol fill:#ffe0b2
    style data_types fill:#ffe0b2
    style frame_codec fill:#ffe0b2
    style app_ctrl fill:#c8e6c9
    style config fill:#c8e6c9
    style session fill:#c8e6c9
    style dir_ser fill:#c8e6c9
    style logger fill:#c8e6c9
    style discovery fill:#bbdefb
    style p2p fill:#bbdefb
    style sender fill:#bbdefb
    style receiver fill:#bbdefb
    style main_qml fill:#f8bbd0
    style ui_components fill:#f8bbd0
    style format_js fill:#f8bbd0
    style style_js fill:#f8bbd0
    style server_stub fill:#eeeeee,stroke-dasharray:5 5
    style tests fill:#fff9c4
```

## 构建单元与模块职责

| 构建单元 | 路径 | CMake 目标 | 职责 |
|:---|:---|:---|:---|
| 共享静态库 | `src/shared/` | `gy_shared` | 客户端 + V2.0 服务端共用：TLV 协议、Type 码、ErrorCode、FrameCodec、PeerInfo/FileEntry/TransferSession POD |
| 客户端可执行文件 | `src/client/` | `appGridYard` | QML + C++ 桌面客户端，包含 core / network / utils / ui 四个子目录 |
| 服务端占位 | `src/server/` | （Stage 7） | V2.0 登录鉴权、对话历史漫游、离线消息暂存 |
| 单元测试 | `src/tests/` | `gy_tests` | Qt Test 框架，8 个测试可执行文件覆盖协议、配置、发现、传输、会话与集成 |

## 四层架构颜色对照

| 颜色 | 层 | 关键组件 |
|:---|:---|:---|
| 橙色 | 共享层（领域层基础） | protocol.h / data_types.h / frame_codec |
| 绿色 | 应用逻辑层 | AppController / ConfigManager / TransferSessionManager / DirSerializer / Logger |
| 蓝色 | 领域层（网络与传输） | DiscoveryService / P2pServer / FileSenderWorker / FileReceiverWorker |
| 粉色 | 表现层 | Main.qml / 8 个 QML 子组件 / FormatUtils.js |
| 黄色 | 测试 | 8 个 Qt Test 可执行文件 |
| 灰色虚线 | 占位 | V2.0 服务端（Stage 7 才实施） |
