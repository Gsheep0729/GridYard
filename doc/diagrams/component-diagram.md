# GridYard 组件图（Stage 0 当前状态）

```mermaid
graph TB
    subgraph "src/shared (gy_shared 静态库)"
        protocol["protocol.h<br/>协议常量"]
        data_types["data_types.h<br/>PeerInfo"]
        frame_codec["frame_codec.h/cpp<br/>FrameCodec"]
    end

    subgraph "src/client (appGridYard 可执行文件)"
        subgraph "client/core"
            app_ctrl["app_controller.h/cpp<br/>AppController"]
        end

        subgraph "client/ui"
            main_qml["Main.qml"]
        end

        main_cpp["main.cpp<br/>程序入口"]
    end

    subgraph "Qt QML 引擎"
        qml_engine["QQmlApplicationEngine"]
        moc["moc 元对象编译器"]
    end

    %% 依赖关系
    frame_codec --> protocol
    frame_codec --> data_types
    app_ctrl --> protocol
    app_ctrl --> data_types

    main_cpp --> qml_engine
    main_cpp --> data_types
    main_qml --> app_ctrl

    moc -.->|编译时处理| app_ctrl
    qml_engine -->|加载| main_qml
    qml_engine -->|实例化| app_ctrl
```

## 模块职责

| 模块 | 路径 | 职责 |
|:-----|:-----|:-----|
| gy_shared | src/shared/ | 客户端 + V2.0 服务端共用静态库 |
| appGridYard | src/client/ | 桌面客户端可执行文件 |
| Qt QML 引擎 | Qt 框架 | 加载 QML、实例化 C++ 单例 |
